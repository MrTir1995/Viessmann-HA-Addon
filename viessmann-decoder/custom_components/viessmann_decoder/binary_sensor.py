"""Relay and decoder diagnostic binary sensors."""

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.core import callback
from homeassistant.helpers.entity import EntityCategory

from .const import DOMAIN
from .data import REMOTE_COUNTERS
from .entity import DecoderEntity, async_register_discovery, received_status_available


async def async_setup_entry(hass, entry, async_add_entities):
    coordinator = hass.data[DOMAIN][entry.entry_id]
    discovered = set()

    @callback
    def discover(data):
        entities = []
        for key in ("serial_connection", "data_ready", "compatibility"):
            if key not in discovered:
                discovered.add(key)
                entities.append(DecoderDiagnosticBinarySensor(coordinator, key))
        for index in range(len(data["relays"])):
            key = f"relay_{index}"
            if key not in discovered:
                discovered.add(key)
                entities.append(DecoderRelay(coordinator, index))
        if data["protocol"] == 4 and "remote_online" not in discovered:
            discovered.add("remote_online")
            entities.append(DecoderDiagnosticBinarySensor(coordinator, "remote_online"))
        if data["protocol"] == 4:
            for key in ("controller_fault", "heating_enabled", "communication_problem"):
                if key not in discovered and (
                    key == "controller_fault"
                    or key in data["remote"]
                    or (key == "communication_problem" and all(
                        field in data["remote"] for field in REMOTE_COUNTERS[1:]
                    ))
                ):
                    discovered.add(key)
                    entities.append(DecoderRemoteBinarySensor(coordinator, key))
        return entities

    async_register_discovery(entry, coordinator, async_add_entities, discover)


class DecoderDiagnosticBinarySensor(DecoderEntity, BinarySensorEntity):
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator, key):
        super().__init__(coordinator, key, remote=key == "remote_online")
        self._key = key
        self._attr_translation_key = key
        if key in ("serial_connection", "remote_online"):
            self._attr_device_class = BinarySensorDeviceClass.CONNECTIVITY

    @property
    def is_on(self):
        data = self.coordinator.data
        if self._key == "remote_online":
            return data.get("remote", {}).get("online")
        field = {
            "serial_connection": "serialConnected",
            "data_ready": "ready",
            "compatibility": "compatible",
        }[self._key]
        return data[field]


class DecoderRelay(DecoderEntity, BinarySensorEntity):
    def __init__(self, coordinator, index):
        super().__init__(coordinator, f"relay_{index}", bus_reading=True)
        self._index = index
        self._attr_translation_key = "relay"
        self._attr_translation_placeholders = {"channel": str(index + 1)}
        self._attr_icon = "mdi:electric-switch"

    @property
    def available(self):
        return super().available and self._index < len(self.coordinator.data["relays"])

    @property
    def is_on(self):
        values = self.coordinator.data["relays"]
        return values[self._index] if self._index < len(values) else None


class DecoderRemoteBinarySensor(DecoderEntity, BinarySensorEntity):
    def __init__(self, coordinator, key):
        super().__init__(
            coordinator, f"remote_{key}", remote=True,
            bus_reading=key != "communication_problem",
        )
        self._key = key
        self._attr_translation_key = f"remote_{key}"
        if key in ("communication_problem", "controller_fault"):
            self._attr_device_class = BinarySensorDeviceClass.PROBLEM
        if key == "communication_problem":
            self._attr_entity_category = EntityCategory.DIAGNOSTIC

    @property
    def available(self):
        if not super().available:
            return False
        remote = self.coordinator.data["remote"]
        if self._key == "communication_problem":
            return all(field in remote for field in REMOTE_COUNTERS[1:])
        return received_status_available(remote, self._key)

    @property
    def extra_state_attributes(self):
        if self._key != "controller_fault":
            return None
        remote = (self.coordinator.data or {}).get("remote", {})
        verified = (
            remote.get("measurements_verified") is True
            and isinstance(remote.get("controller_fault"), bool)
        )
        return {
            "mapping_status": "verified" if verified else "unverified",
            "source_field": "controller_fault",
        }

    @property
    def is_on(self):
        data = self.coordinator.data
        remote = data.get("remote", {})
        if self._key == "communication_problem":
            return (
                not data["serialConnected"] or not data["ready"]
                or not data["compatible"] or not remote.get("online", False)
                or any(remote.get(field, 0) > 0 for field in REMOTE_COUNTERS[1:])
            )
        return remote.get(self._key)
