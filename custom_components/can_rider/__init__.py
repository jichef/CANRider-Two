import asyncio
import logging
from datetime import timedelta

import aiohttp
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .const import CONF_SUPABASE_KEY, CONF_SUPABASE_URL, CONF_VEHICLE_ID, DOMAIN

_LOGGER = logging.getLogger(__name__)

PLATFORMS = ["sensor", "device_tracker", "binary_sensor"]


async def async_setup_entry(hass: HomeAssistant, entry) -> bool:
    session = async_get_clientsession(hass)
    raw_url = entry.data.get(CONF_SUPABASE_URL, "")
    url = raw_url.split("/rest/v1")[0].rstrip("/")
    key = entry.data[CONF_SUPABASE_KEY]
    vehicle_id = entry.data[CONF_VEHICLE_ID]

    headers = {
        "apikey": key,
        "Authorization": f"Bearer {key}",
        "Content-Type": "application/json",
    }

    async def async_update_data():
        try:
            async with asyncio.timeout(15):
                results = {"telemetry": None, "last_trip": None}

                tel_url = (
                    f"{url}/rest/v1/telemetry"
                    f"?motorcycle_id=eq.{vehicle_id}&select=*&order=timestamp.desc&limit=1"
                )
                async with session.get(tel_url, headers=headers) as resp:
                    if resp.status == 200:
                        data = await resp.json()
                        if data:
                            results["telemetry"] = data[0]

                trip_url = (
                    f"{url}/rest/v1/trips"
                    f"?motorcycle_id=eq.{vehicle_id}&select=*&order=start_time.desc&limit=1"
                )
                async with session.get(trip_url, headers=headers) as resp:
                    if resp.status == 200:
                        data = await resp.json()
                        if data:
                            results["last_trip"] = data[0]

                return results
        except Exception as err:
            raise UpdateFailed(f"Error conectando con Supabase: {err}")

    coordinator = DataUpdateCoordinator(
        hass,
        _LOGGER,
        name="can_rider_telemetry",
        update_method=async_update_data,
        update_interval=timedelta(seconds=30),
    )
    # Adjuntamos el vehicle_id al coordinator para que las entidades
    # puedan construir unique_ids estables sin depender del id del registro.
    coordinator.vehicle_id = vehicle_id

    await coordinator.async_config_entry_first_refresh()

    hass.data.setdefault(DOMAIN, {})
    hass.data[DOMAIN][entry.entry_id] = coordinator

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry) -> bool:
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        hass.data[DOMAIN].pop(entry.entry_id)
    return unloaded
