"""Temperature, pump speed, status and remote read-only sensors."""

from homeassistant.components.sensor import SensorDeviceClass, SensorEntity, SensorStateClass
from homeassistant.const import PERCENTAGE, UnitOfTemperature
from homeassistant.core import callback
from homeassistant.helpers.entity import EntityCategory

from .const import DOMAIN, PROTOCOL_NAMES
from .entity import DecoderEntity, async_register_discovery

REMOTE_KEYS = (
    "room_temperature",
    "desired_room_temperature",
    "mode",
    "slot",
    "model",
    "last_master_dataset",
)


async def async_setup_entry(hass, entry, async_add_entities):
    coordinator = hass.data[DOMAIN][entry.entry_id]
    discovered = set()

    @callback
    def discover(data):
        entities = []
        for key in ("status", "protocol", "serial_port"):
            if key not in discovered:
                discovered.add(key)
                entities.append(DecoderDiagnosticSensor(coordinator, key))
        for group in ("temperatures", "pumps"):
            for index in range(len(data[group])):
                key = f"{group}_{index}"
                if key not in discovered:
                    discovered.add(key)
                    entities.append(DecoderChannelSensor(coordinator, group, index))
        if data["protocol"] == 4:
            for key in REMOTE_KEYS:
                identity = f"remote_{key}"
                if identity not in discovered:
                    discovered.add(identity)
                    entities.append(DecoderRemoteSensor(coordinator, key))
        return entities

    async_register_discovery(entry, coordinator, async_add_entities, discover)


class DecoderDiagnosticSensor(DecoderEntity, SensorEntity):
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator, key):
        super().__init__(coordinator, key)
        self._key = key
        self._attr_translation_key = key

    @property
    def native_value(self):
        data = self.coordinator.data
        if self._key == "protocol":
            return PROTOCOL_NAMES[data["protocol"]]
        return data["serialPort" if self._key == "serial_port" else self._key]


class DecoderChannelSensor(DecoderEntity, SensorEntity):
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, coordinator, group, index):
        super().__init__(coordinator, f"{group}_{index}", bus_reading=True)
        self._group = group
        self._index = index
        self._attr_translation_key = "temperature" if group == "temperatures" else "pump"
        self._attr_translation_placeholders = {"channel": str(index + 1)}
        if group == "temperatures":
            self._attr_device_class = SensorDeviceClass.TEMPERATURE
            self._attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
        else:
            self._attr_native_unit_of_measurement = PERCENTAGE
            self._attr_icon = "mdi:pump"

    @property
    def available(self):
        return super().available and self._index < len(self.coordinator.data[self._group])

    @property
    def native_value(self):
        values = self.coordinator.data[self._group]
        return values[self._index] if self._index < len(values) else None


class DecoderRemoteSensor(DecoderEntity, SensorEntity):
    def __init__(self, coordinator, key):
        reading = key in ("room_temperature", "desired_room_temperature", "mode", "last_master_dataset")
        super().__init__(coordinator, f"remote_{key}", bus_reading=reading, remote=True)
        self._key = key
        self._attr_translation_key = f"remote_{key}"
        if key.endswith("temperature"):
            self._attr_device_class = SensorDeviceClass.TEMPERATURE
            self._attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
            self._attr_state_class = SensorStateClass.MEASUREMENT
        elif key in ("model", "slot", "last_master_dataset"):
            self._attr_entity_category = EntityCategory.DIAGNOSTIC

    @property
    def native_value(self):
        return self.coordinator.data.get("remote", {}).get(self._key)
