from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
)
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN


async def async_setup_entry(hass, entry, async_add_entities):
    coordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([
        CanRiderChargingSensor(coordinator),
        CanRiderTheftSensor(coordinator),
    ])


class CanRiderChargingSensor(CoordinatorEntity, BinarySensorEntity):
    _attr_name = "Moto Cargando"
    _attr_device_class = BinarySensorDeviceClass.BATTERY_CHARGING

    def __init__(self, coordinator):
        super().__init__(coordinator)
        self._attr_unique_id = f"{coordinator.vehicle_id}_charging"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, coordinator.vehicle_id)},
        }

    @property
    def is_on(self):
        tel = self.coordinator.data.get("telemetry")
        if not tel:
            return False
        # bms_charging viene de CAN (float 1.0/0.0); is_charging de AT+CBC (bool)
        bms = tel.get("bms_charging")
        if bms is not None:
            return float(bms) > 0
        return tel.get("is_charging") is True


class CanRiderTheftSensor(CoordinatorEntity, BinarySensorEntity):
    _attr_name = "Movimiento Sin CAN"
    _attr_device_class = BinarySensorDeviceClass.TAMPER

    def __init__(self, coordinator):
        super().__init__(coordinator)
        self._attr_unique_id = f"{coordinator.vehicle_id}_moving_without_can"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, coordinator.vehicle_id)},
        }

    @property
    def is_on(self):
        tel = self.coordinator.data.get("telemetry")
        if not tel:
            return False
        # GPS midiendo movimiento real con el bus CAN en silencio (moto
        # apagada) — puesto por el firmware, ver CAN_ALIVE_TIMEOUT_MS /
        # THEFT_SPEED_KMH en main/main.ino.
        return tel.get("moving_without_can") is True
