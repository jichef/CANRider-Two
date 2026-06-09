from homeassistant.components.device_tracker import SourceType, TrackerEntity
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN


async def async_setup_entry(hass, entry, async_add_entities):
    coordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([CanRiderTracker(coordinator)])


class CanRiderTracker(CoordinatorEntity, TrackerEntity):

    def __init__(self, coordinator):
        super().__init__(coordinator)
        self._attr_name = "Ubicación Moto"
        self._attr_unique_id = f"{coordinator.vehicle_id}_tracker"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, coordinator.vehicle_id)},
            "name": f"CanRider {coordinator.vehicle_id}",
        }

    @property
    def source_type(self):
        return SourceType.GPS

    @property
    def _tel(self):
        return self.coordinator.data.get("telemetry")

    @property
    def latitude(self):
        return self._tel.get("latitude") if self._tel else None

    @property
    def longitude(self):
        return self._tel.get("longitude") if self._tel else None

    @property
    def battery_level(self):
        return self._tel.get("battery_level") if self._tel else None

    @property
    def icon(self):
        return "mdi:motorbike"
