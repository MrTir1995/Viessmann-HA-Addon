"""Common identity, availability and dynamic discovery."""

from homeassistant.core import callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .api import CannotConnect
from .data import CONTROL_FIELDS, InvalidDecoderData


def received_status_available(remote, key):
    """Never expose unverified, absent or stale received controller readings."""
    age = remote.get("status_dataset_age_ms")
    return (
        remote.get("measurements_verified") is True
        and remote.get(key) is not None
        and age is not None and age <= 180000
    )


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


class DecoderControl(DecoderEntity):
    """Local requested controls, never optimistically confirmed controller state."""

    def __init__(self, coordinator, key, field, *, wifi_only=False):
        super().__init__(coordinator, key, bus_reading=True, remote=True)
        self._field = field
        self._wifi_only = wifi_only
        self._attr_translation_key = key

    @property
    def available(self):
        if not super().available:
            return False
        remote = self.coordinator.data["remote"]
        return (
            CONTROL_FIELDS <= remote.keys()
            and self._field in remote
            and (not self._wifi_only or remote["profile"] == "wifi")
        )

    async def async_send_command(self, command):
        if not self.available:
            raise HomeAssistantError("Remote control is unavailable or unsupported")
        try:
            await self.coordinator.api.async_set_remote(command)
        except (CannotConnect, InvalidDecoderData) as err:
            raise HomeAssistantError(str(err)) from err
        await self.coordinator.async_request_refresh()
