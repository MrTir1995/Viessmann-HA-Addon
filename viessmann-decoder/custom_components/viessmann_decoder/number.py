"""Local supplied room temperature and requested remote temperature settings."""

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.const import UnitOfTemperature
from homeassistant.core import callback

from .const import DOMAIN
from .data import TEMPERATURE_LIMITS
from .entity import DecoderControl, async_register_discovery


async def async_setup_entry(hass, entry, async_add_entities):
    coordinator = hass.data[DOMAIN][entry.entry_id]
    discovered = set()

    @callback
    def discover(data):
        entities = []
        if data["protocol"] == 4:
            remote = data["remote"]
            for field in TEMPERATURE_LIMITS:
                if field not in discovered and field in remote:
                    if field == "party_room_temperature" and remote.get("profile") != "wifi":
                        continue
                    discovered.add(field)
                    entities.append(DecoderRemoteNumber(coordinator, field))
        return entities

    async_register_discovery(entry, coordinator, async_add_entities, discover)


class DecoderRemoteNumber(DecoderControl, NumberEntity):
    _attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
    _attr_mode = NumberMode.BOX

    def __init__(self, coordinator, field):
        super().__init__(
            coordinator, f"remote_{field}_setting", field,
            wifi_only=field == "party_room_temperature",
        )
        self._attr_native_min_value, self._attr_native_max_value, self._attr_native_step = (
            TEMPERATURE_LIMITS[field]
        )

    @property
    def native_value(self):
        return self.coordinator.data.get("remote", {}).get(self._field)

    async def async_set_native_value(self, value):
        command = {self._field: value}
        if self._field == "party_room_temperature":
            command["mode"] = "party_on"
        await self.async_send_command(command)
