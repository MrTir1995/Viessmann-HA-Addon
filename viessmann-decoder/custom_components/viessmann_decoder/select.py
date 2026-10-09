"""Local requested base operating mode."""

from homeassistant.components.select import SelectEntity
from homeassistant.core import callback
from homeassistant.exceptions import HomeAssistantError

from .const import DOMAIN
from .data import BASE_MODES
from .entity import DecoderControl, async_register_discovery


async def async_setup_entry(hass, entry, async_add_entities):
    coordinator = hass.data[DOMAIN][entry.entry_id]
    discovered = False

    @callback
    def discover(data):
        nonlocal discovered
        if data["protocol"] == 4 and not discovered:
            discovered = True
            return [DecoderRemoteMode(coordinator)]
        return []

    async_register_discovery(entry, coordinator, async_add_entities, discover)


class DecoderRemoteMode(DecoderControl, SelectEntity):
    _attr_options = list(BASE_MODES.values())

    def __init__(self, coordinator):
        super().__init__(coordinator, "remote_base_mode_setting", "mode")

    @property
    def current_option(self):
        return BASE_MODES.get(self.coordinator.data.get("remote", {}).get("mode"))

    async def async_select_option(self, option):
        if option not in self._attr_options:
            raise HomeAssistantError("Unsupported base operating mode")
        await self.async_send_command({"mode": option})
