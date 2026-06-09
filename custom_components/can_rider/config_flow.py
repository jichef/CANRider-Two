import aiohttp
import voluptuous as vol
from homeassistant import config_entries
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .const import CONF_SUPABASE_KEY, CONF_SUPABASE_URL, CONF_VEHICLE_ID, DOMAIN


class CanRiderConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        errors = {}

        if user_input is not None:
            errors = await self._validate_credentials(
                user_input[CONF_SUPABASE_URL],
                user_input[CONF_SUPABASE_KEY],
                user_input[CONF_VEHICLE_ID],
            )
            if not errors:
                return self.async_create_entry(
                    title=f"Moto {user_input[CONF_VEHICLE_ID]}",
                    data=user_input,
                )

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({
                vol.Required(CONF_SUPABASE_URL): str,
                vol.Required(CONF_SUPABASE_KEY): str,
                vol.Required(CONF_VEHICLE_ID, default="test01"): str,
            }),
            errors=errors,
        )

    async def _validate_credentials(self, raw_url: str, key: str, vehicle_id: str) -> dict:
        url = raw_url.split("/rest/v1")[0].rstrip("/")
        test_url = (
            f"{url}/rest/v1/telemetry"
            f"?motorcycle_id=eq.{vehicle_id}&select=id&limit=1"
        )
        headers = {"apikey": key, "Authorization": f"Bearer {key}"}
        try:
            session = async_get_clientsession(self.hass)
            async with session.get(
                test_url,
                headers=headers,
                timeout=aiohttp.ClientTimeout(total=10),
            ) as resp:
                if resp.status == 401:
                    return {"base": "invalid_auth"}
                if resp.status not in (200, 206):
                    return {"base": "cannot_connect"}
        except Exception:
            return {"base": "cannot_connect"}
        return {}
