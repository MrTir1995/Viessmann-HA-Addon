"""UI configuration for a remote HTTP decoder."""

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import CannotConnect, DecoderApi
from .const import CONF_URL, DOMAIN
from .data import InvalidDecoderData, normalize_url


class DecoderConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Configure one decoder per normalized base URL."""

    VERSION = 1

    async def async_step_user(self, user_input=None):
        errors = {}
        if user_input is not None:
            try:
                url = normalize_url(user_input[CONF_URL])
            except (ValueError, UnicodeError):
                errors[CONF_URL] = "invalid_url"
            else:
                await self.async_set_unique_id(url)
                self._abort_if_unique_id_configured()
                try:
                    await DecoderApi(async_get_clientsession(self.hass), url).async_get_data()
                except CannotConnect:
                    errors["base"] = "cannot_connect"
                except InvalidDecoderData:
                    errors["base"] = "invalid_response"
                else:
                    return self.async_create_entry(title="Viessmann Decoder", data={CONF_URL: url})
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({vol.Required(CONF_URL): str}),
            errors=errors,
        )
