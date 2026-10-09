"""Home Assistant integration for Viessmann Decoder."""

from homeassistant.const import Platform
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import DecoderApi
from .const import CONF_URL, DOMAIN
from .coordinator import DecoderCoordinator

PLATFORMS = [
    Platform.SENSOR, Platform.BINARY_SENSOR, Platform.NUMBER, Platform.SELECT, Platform.SWITCH,
]


async def async_setup_entry(hass, entry):
    """Connect before creating entities; retry transient startup failures."""
    coordinator = DecoderCoordinator(
        hass, DecoderApi(async_get_clientsession(hass), entry.data[CONF_URL]), entry
    )
    await coordinator.async_config_entry_first_refresh()
    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = coordinator
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass, entry):
    """Unload platforms and their coordinator/discovery subscriptions."""
    if await hass.config_entries.async_unload_platforms(entry, PLATFORMS):
        hass.data[DOMAIN].pop(entry.entry_id)
        return True
    return False
