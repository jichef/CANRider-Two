import logging
from pathlib import Path

import aiohttp
import voluptuous as vol
from homeassistant import config_entries
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .const import (
    CONF_BOARD_MODEL,
    CONF_SUPABASE_KEY,
    CONF_SUPABASE_URL,
    CONF_VEHICLE_ID,
    DEFAULT_BOARD_MODEL,
    DOMAIN,
)

_LOGGER = logging.getLogger(__name__)

# Candidatos donde puede vivir un .env.local de banco de pruebas: junto a
# este archivo (despliegue real copiado a custom_components/can_rider, p.ej.
# \\192.168.14.3\config\custom_components\can_rider\.env.local) o en la raíz
# del repo (cuando HA corre directo contra este checkout/symlink). En una
# instalación real ninguno existe y _load_dev_defaults() no hace nada.
_DEV_ENV_CANDIDATES = [
    Path(__file__).resolve().parent / ".env.local",
    Path(__file__).resolve().parents[2] / ".env.local",
]
_DEV_ENV_MAP = {
    "NEXT_PUBLIC_SUPABASE_URL": CONF_SUPABASE_URL,
    "NEXT_PUBLIC_SUPABASE_ANON_KEY": CONF_SUPABASE_KEY,
    "NEXT_PUBLIC_VEHICLE_ID": CONF_VEHICLE_ID,
}


def _load_dev_defaults() -> dict:
    """Prerrellena el formulario con .env.local si estamos en banco de pruebas.

    Nunca lee SUPABASE_SERVICE_ROLE_KEY: la integración solo debe usar la
    clave anon/publishable, igual que el dashboard web.
    """
    env_file = next((p for p in _DEV_ENV_CANDIDATES if p.is_file()), None)
    if env_file is None:
        return {}
    defaults = {}
    try:
        for line in env_file.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            conf_key = _DEV_ENV_MAP.get(key.strip())
            if conf_key:
                defaults[conf_key] = value.strip().strip('"').strip("'")
    except OSError:
        return {}
    if defaults:
        _LOGGER.debug("CanRider: usando defaults de %s (banco de pruebas)", env_file)
    return defaults


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

        # Prioridad de defaults: lo que el usuario ya escribió en un intento
        # anterior > .env.local del repo (solo en banco de pruebas) > vacío.
        # Así un error no te obliga a reescribir todo desde cero, y en el
        # entorno de desarrollo el formulario ya viene relleno.
        dev_defaults = _load_dev_defaults()
        previous = {**dev_defaults, **(user_input or {})}
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({
                vol.Required(
                    CONF_SUPABASE_URL, default=previous.get(CONF_SUPABASE_URL, "")
                ): str,
                vol.Required(
                    CONF_SUPABASE_KEY, default=previous.get(CONF_SUPABASE_KEY, "")
                ): str,
                vol.Required(
                    CONF_VEHICLE_ID, default=previous.get(CONF_VEHICLE_ID, "")
                ): str,
                vol.Optional(
                    CONF_BOARD_MODEL,
                    default=previous.get(CONF_BOARD_MODEL, DEFAULT_BOARD_MODEL),
                ): vol.In(["A7670G", "SIM7000G"]),
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
                if resp.status == 400:
                    # El motivo típico: motorcycle_id no es un UUID válido
                    # (la columna en producción es tipo uuid, no texto libre).
                    body = await resp.text()
                    _LOGGER.error(
                        "CanRider: Supabase rechazó la consulta (400) para "
                        "vehicle_id=%r: %s",
                        vehicle_id,
                        body,
                    )
                    return {CONF_VEHICLE_ID: "invalid_vehicle_id"}
                if resp.status not in (200, 206):
                    body = await resp.text()
                    _LOGGER.error(
                        "CanRider: fallo al conectar con Supabase (status %s): %s",
                        resp.status,
                        body,
                    )
                    return {"base": "cannot_connect"}
        except Exception:
            _LOGGER.exception("CanRider: excepción validando credenciales de Supabase")
            return {"base": "cannot_connect"}
        return {}
