"""Polling coordinator for a decoder instance."""

import logging

from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import CannotConnect, DecoderApi
from .const import DOMAIN, UPDATE_INTERVAL
from .data import InvalidDecoderData

_LOGGER = logging.getLogger(__name__)


class DecoderCoordinator(DataUpdateCoordinator):
    """Poll once for all entities and propagate communication failures."""

    def __init__(self, hass, api: DecoderApi, entry):
        super().__init__(hass, _LOGGER, name=DOMAIN, update_interval=UPDATE_INTERVAL)
        self.api = api
        self.entry = entry

    async def _async_update_data(self):
        try:
            return await self.api.async_get_data()
        except (CannotConnect, InvalidDecoderData) as err:
            raise UpdateFailed(str(err)) from err
