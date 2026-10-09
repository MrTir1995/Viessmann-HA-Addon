"""Dependency-free validation of decoder responses and base URLs."""

import ipaddress
import math
import posixpath
from urllib.parse import quote, unquote, urlsplit, urlunsplit


class InvalidDecoderData(ValueError):
    """The server did not return a supported decoder response."""


TEMPERATURE_LIMITS = {
    "room_temperature": (-20, 50, 0.1),
    "desired_room_temperature": (5, 35, 1),
    "reduced_room_temperature": (5, 35, 1),
    "party_room_temperature": (5, 35, 1),
}
BASE_MODES = {200: "off", 201: "water", 202: "heat_water"}
COMMAND_MODES = {*BASE_MODES.values(), "party_on", "party_off", "economy_on", "economy_off"}
REMOTE_COUNTERS = ("pending_commands", "crc_errors", "malformed_frames", "unknown_commands")
CONTROL_FIELDS = {
    "profile", "reduced_room_temperature", "party_room_temperature",
    "requested_party_mode", "requested_economy_mode", "pending_commands",
}


def validate_command(payload):
    """Allow only documented finite, bounded remote commands."""
    if not isinstance(payload, dict) or not payload:
        raise InvalidDecoderData("Expected a non-empty command object")
    if payload.keys() - (TEMPERATURE_LIMITS.keys() | {"mode"}):
        raise InvalidDecoderData("Unknown remote command field")
    for key, value in payload.items():
        if key == "mode":
            if not isinstance(value, str) or value not in COMMAND_MODES:
                raise InvalidDecoderData("Unsupported operating mode")
            continue
        minimum, maximum, step = TEMPERATURE_LIMITS[key]
        _number(value, key)
        if not minimum <= value <= maximum or not math.isclose(
            value / step, round(value / step), abs_tol=1e-7, rel_tol=0
        ):
            raise InvalidDecoderData(f"Invalid {key} range or step")
    if "party_room_temperature" in payload and payload.get("mode") != "party_on":
        raise InvalidDecoderData("Party temperature requires activating party mode")
    return dict(payload)


def normalize_url(value):
    """Return a canonical HTTP base URL, without credentials or URL metadata."""
    if not isinstance(value, str) or not value.strip():
        raise ValueError("A URL is required")
    value = value.strip()
    if any(ord(char) < 33 for char in value) or "\\" in value:
        raise ValueError("Invalid URL characters")
    parts = urlsplit(value)
    if (
        parts.scheme.lower() not in ("http", "https")
        or not parts.hostname
        or parts.username is not None
        or parts.password is not None
        or "?" in value
        or "#" in value
    ):
        raise ValueError("Use an HTTP(S) URL without credentials, query or fragment")
    port = parts.port
    if port is not None and not 1 <= port <= 65535:
        raise ValueError("Invalid port")
    host = parts.hostname.lower()
    if ":" in host:
        host = f"[{ipaddress.IPv6Address(host).compressed}]"
    else:
        host = host.encode("idna").decode("ascii").rstrip(".")
        if not host or any(char not in "abcdefghijklmnopqrstuvwxyz0123456789-." for char in host):
            raise ValueError("Invalid hostname")
    if port is not None and port != (443 if parts.scheme.lower() == "https" else 80):
        host = f"{host}:{port}"
    decoded_path = unquote(parts.path)
    if any(ord(char) < 32 for char in decoded_path) or "\\" in decoded_path:
        raise ValueError("Invalid URL path")
    path = posixpath.normpath("/" + decoded_path.lstrip("/")).rstrip("/")
    path = quote(path, safe="/:@-._~!$&'()*+,;=")
    return urlunsplit((parts.scheme.lower(), host, path, "", ""))


def _number(value, name, integer=False):
    if type(value) not in (int, float) or (integer and type(value) is not int):
        raise InvalidDecoderData(f"{name} must be a number")
    try:
        finite = math.isfinite(value)
    except (OverflowError, ValueError):
        finite = False
    if not finite:
        raise InvalidDecoderData(f"{name} must be finite")
    return value


def validate_data(payload):
    """Validate required fields, returning only the documented API fields."""
    if not isinstance(payload, dict):
        raise InvalidDecoderData("Expected a JSON object")
    result = {}
    for key in ("serialConnected", "compatible", "ready"):
        if type(payload.get(key)) is not bool:
            raise InvalidDecoderData(f"{key} must be a boolean")
        result[key] = payload[key]
    for key in ("status", "serialPort"):
        if not isinstance(payload.get(key), str):
            raise InvalidDecoderData(f"{key} must be a string")
        result[key] = payload[key]
    protocol = _number(payload.get("protocol"), "protocol", integer=True)
    if protocol not in range(5):
        raise InvalidDecoderData("Unsupported protocol")
    result["protocol"] = protocol
    for key in ("temperatures", "pumps", "relays"):
        values = payload.get(key)
        if not isinstance(values, list) or len(values) > 32:
            raise InvalidDecoderData(f"{key} must be a list of at most 32 channels")
        result[key] = []
        for value in values:
            if key == "relays":
                if type(value) is not bool:
                    raise InvalidDecoderData("Relay states must be booleans")
            else:
                _number(value, key, integer=key == "pumps")
                if key == "pumps" and not 0 <= value <= 100:
                    raise InvalidDecoderData("Pump speed must be a percentage")
            result[key].append(value)
    if protocol == 4:
        remote = payload.get("remote")
        if not isinstance(remote, dict):
            raise InvalidDecoderData("Remote protocol requires remote data")
        if not isinstance(remote.get("model"), str) or type(remote.get("online")) is not bool:
            raise InvalidDecoderData("Invalid remote model or online state")
        validated_remote = {"model": remote["model"], "online": remote["online"]}
        for key in ("slot", "mode", "last_master_dataset", "room_temperature", "desired_room_temperature"):
            integer = key in ("slot", "mode", "last_master_dataset")
            value = _number(remote.get(key), key, integer=integer)
            if integer and value < 0:
                raise InvalidDecoderData(f"{key} must be non-negative")
            validated_remote[key] = value
        for key in ("reduced_room_temperature", "party_room_temperature", "outside_temperature"):
            if key in remote:
                value = remote[key]
                if value is not None or key != "outside_temperature":
                    _number(value, key)
                validated_remote[key] = value
        for key in REMOTE_COUNTERS + ("status_dataset_age_ms",):
            if key in remote:
                value = remote[key]
                if value is not None or key != "status_dataset_age_ms":
                    _number(value, key, integer=True)
                    if value < 0:
                        raise InvalidDecoderData(f"{key} must be non-negative")
                validated_remote[key] = value
        for key in (
            "requested_party_mode", "requested_economy_mode", "measurements_verified",
            "heating_enabled", "controller_fault",
        ):
            if key in remote:
                value = remote[key]
                nullable = key in ("heating_enabled", "controller_fault")
                if type(value) is not bool and not (nullable and value is None):
                    raise InvalidDecoderData(f"{key} must be a boolean")
                validated_remote[key] = value
        if "profile" in remote:
            if remote["profile"] not in ("wifi", "openv"):
                raise InvalidDecoderData("Unsupported remote profile")
            validated_remote["profile"] = remote["profile"]
        result["remote"] = validated_remote
    return result
