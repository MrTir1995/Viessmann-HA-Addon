"""Common identity, availability and dynamic discovery."""

from homeassistant.core import callback
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN


@callback
def async_register_discovery(entry, coordinator, async_add_entities, discover):
    """Discover on every successful poll, including channels added later."""
    @callback
    def update():
        if coordinator.last_update_success and coordinator.data is not None:
            entities = discover(coordinator.data)
            if entities:
                async_add_entities(entities)

    entry.async_on_unload(coordinator.async_add_listener(update))
    update()


class DecoderEntity(CoordinatorEntity):
    """Entities share a device, but never share identities across entries."""

    _attr_has_entity_name = True

    def __init__(self, coordinator, key, *, bus_reading=False, remote=False):
        super().__init__(coordinator)
        self._attr_unique_id = f"{coordinator.entry.entry_id}_{key}"
        self._bus_reading = bus_reading
        self._remote = remote
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, coordinator.entry.entry_id)},
            name=coordinator.entry.title,
            manufacturer="Viessmann",
            model="Multi-protocol decoder",
            configuration_url=coordinator.api.url,
        )

    @property
    def available(self):
        if not super().available or self.coordinator.data is None:
            return False
        data = self.coordinator.data
        if self._bus_reading and not (
            data["ready"] and data["serialConnected"] and data["compatible"]
        ):
            return False
        if self._remote and data["protocol"] != 4:
            return False
        if self._remote and self._bus_reading and not data["remote"]["online"]:
            return False
        return True
