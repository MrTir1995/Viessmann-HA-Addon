"""Bounded HTTP client using Home Assistant's shared session."""

import asyncio
import json

import aiohttp

from .const import MAX_RESPONSE_BYTES, REQUEST_TIMEOUT
from .data import InvalidDecoderData, normalize_url, validate_command, validate_data


class CannotConnect(Exception):
    """The decoder could not be reached."""


class DecoderApi:
    """Poll data and submit validated remote commands."""

    def __init__(self, session, url):
        self._session = session
        self.url = normalize_url(url)

    async def async_get_data(self):
        return validate_data(await self._async_request("get", "/data"))

    async def async_set_remote(self, command):
        payload = await self._async_request(
            "post", "/api/remote", json=validate_command(command)
        )
        if not isinstance(payload, dict) or payload.get("status") != "queued":
            raise InvalidDecoderData("Decoder did not accept the remote command")

    async def _async_request(self, method, path, **kwargs):
        try:
            async with asyncio.timeout(REQUEST_TIMEOUT):
                async with getattr(self._session, method)(
                    f"{self.url}{path}",
                    timeout=aiohttp.ClientTimeout(total=REQUEST_TIMEOUT),
                    allow_redirects=False,
                    headers={"Accept": "application/json"},
                    **kwargs,
                ) as response:
                    if response.status != 200:
                        raise CannotConnect(f"Unexpected HTTP status {response.status}")
                    body = bytearray()
                    async for chunk in response.content.iter_chunked(8192):
                        body.extend(chunk)
                        if len(body) > MAX_RESPONSE_BYTES:
                            raise InvalidDecoderData("Decoder response is too large")
                    try:
                        payload = json.loads(body)
                    except (ValueError, UnicodeError, RecursionError) as err:
                        raise InvalidDecoderData("Decoder response is not valid JSON") from err
                    return payload
        except (aiohttp.ClientError, TimeoutError) as err:
            raise CannotConnect("Unable to connect to decoder") from err
