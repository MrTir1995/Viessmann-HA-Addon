"""Local requested party and economy modes."""

from homeassistant.components.switch import SwitchEntity
from homeassistant.core import callback

from .const import DOMAIN
from .entity import DecoderControl, async_register_discovery


async def async_setup_entry(hass, entry, async_add_entities):
    coordinator = hass.data[DOMAIN][entry.entry_id]
    discovered = set()

    @callback
    def discover(data):
        entities = []
        if data["protocol"] == 4:
            for mode in ("party", "economy"):
                if mode not in discovered and f"requested_{mode}_mode" in data["remote"]:
                    discovered.add(mode)
                    entities.append(DecoderRemoteSwitch(coordinator, mode))
        return entities

    async_register_discovery(entry, coordinator, async_add_entities, discover)


class DecoderRemoteSwitch(DecoderControl, SwitchEntity):
    def __init__(self, coordinator, mode):
        super().__init__(
            coordinator, f"remote_{mode}_setting", f"requested_{mode}_mode"
        )
        self._mode = mode

    @property
    def is_on(self):
        return self.coordinator.data.get("remote", {}).get(self._field)

    async def async_turn_on(self, **kwargs):
        await self.async_send_command({"mode": f"{self._mode}_on"})

    async def async_turn_off(self, **kwargs):
        await self.async_send_command({"mode": f"{self._mode}_off"})
