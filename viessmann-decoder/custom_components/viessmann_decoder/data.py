"""Dependency-free validation of decoder responses and base URLs."""

import ipaddress
import math
import posixpath
from urllib.parse import quote, unquote, urlsplit, urlunsplit


class InvalidDecoderData(ValueError):
    """The server did not return a supported decoder response."""


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
        result["remote"] = validated_remote
    return result
