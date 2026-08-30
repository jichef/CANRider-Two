from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorStateClass,
)
from homeassistant.const import (
    PERCENTAGE,
    SIGNAL_STRENGTH_DECIBELS_MILLIWATT,
    UnitOfElectricPotential,
    UnitOfSpeed,
)
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import BOARD_MODEL_NAMES, DEFAULT_BOARD_MODEL, DOMAIN


async def async_setup_entry(hass, entry, async_add_entities):
    coordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([
        BatteryASensor(coordinator),
        BatteryBSensor(coordinator),
        EspBatterySensor(coordinator),
        EspBatteryVoltageSensor(coordinator),
        SpeedSensor(coordinator),
        SignalStrengthSensor(coordinator),
        LastTripDistanceSensor(coordinator),
        LastTripMaxSpeedSensor(coordinator),
        LastTripConsumptionSensor(coordinator),
    ])


class CanRiderSensor(CoordinatorEntity, SensorEntity):
    def __init__(self, coordinator):
        super().__init__(coordinator)
        board_model = getattr(coordinator, "board_model", DEFAULT_BOARD_MODEL)
        self._attr_device_info = {
            "identifiers": {(DOMAIN, coordinator.vehicle_id)},
            "name": f"CanRider {coordinator.vehicle_id}",
            "model": BOARD_MODEL_NAMES.get(board_model, board_model),
            "manufacturer": "LilyGo",
        }

    @property
    def _tel(self):
        return self.coordinator.data.get("telemetry")

    @property
    def _trip(self):
        return self.coordinator.data.get("last_trip")


# ── Batería de la moto (CAN) ─────────────────────────────────────────────────────

class BatteryASensor(CanRiderSensor):
    _attr_name = "Batería A"
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_device_class = SensorDeviceClass.BATTERY
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_moto_battery"

    @property
    def native_value(self):
        return self._tel.get("moto_battery") if self._tel else None


class BatteryBSensor(CanRiderSensor):
    _attr_name = "Batería B"
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_device_class = SensorDeviceClass.BATTERY
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_moto_battery_b"

    @property
    def native_value(self):
        return self._tel.get("moto_battery_b") if self._tel else None


# ── Batería del propio ESP32 (AT+CBC del módem) ──────────────────────────────────

class EspBatterySensor(CanRiderSensor):
    _attr_name = "Batería ESP32"
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_device_class = SensorDeviceClass.BATTERY
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_battery_level"

    @property
    def native_value(self):
        return self._tel.get("battery_level") if self._tel else None


class EspBatteryVoltageSensor(CanRiderSensor):
    _attr_name = "Tensión Batería ESP32"
    _attr_native_unit_of_measurement = UnitOfElectricPotential.VOLT
    _attr_device_class = SensorDeviceClass.VOLTAGE
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_battery_voltage"

    @property
    def native_value(self):
        return self._tel.get("battery_voltage") if self._tel else None


# ── Otros ──────────────────────────────────────────────────────────────────────

class SpeedSensor(CanRiderSensor):
    _attr_name = "Velocidad"
    _attr_native_unit_of_measurement = UnitOfSpeed.KILOMETERS_PER_HOUR
    _attr_device_class = SensorDeviceClass.SPEED
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_speed"

    @property
    def native_value(self):
        return self._tel.get("speed") if self._tel else None


class SignalStrengthSensor(CanRiderSensor):
    _attr_name = "Señal de Red"
    _attr_native_unit_of_measurement = SIGNAL_STRENGTH_DECIBELS_MILLIWATT
    _attr_device_class = SensorDeviceClass.SIGNAL_STRENGTH
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_signal_strength"

    @property
    def native_value(self):
        return self._tel.get("signal_strength") if self._tel else None


# ── Viajes ─────────────────────────────────────────────────────────────────────

class LastTripDistanceSensor(CanRiderSensor):
    _attr_name = "Distancia Último Viaje"
    _attr_native_unit_of_measurement = "km"
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_suggested_display_precision = 1

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_trip_distance"

    @property
    def native_value(self):
        return self._trip.get("distance") if self._trip else None


class LastTripMaxSpeedSensor(CanRiderSensor):
    _attr_name = "Velocidad Máxima Viaje"
    _attr_native_unit_of_measurement = UnitOfSpeed.KILOMETERS_PER_HOUR
    _attr_device_class = SensorDeviceClass.SPEED
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_trip_max_speed"

    @property
    def native_value(self):
        return self._trip.get("max_speed") if self._trip else None


class LastTripConsumptionSensor(CanRiderSensor):
    _attr_name = "Consumo Último Viaje"
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_suggested_display_precision = 1

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_trip_consumption"

    @property
    def native_value(self):
        return self._trip.get("consumption") if self._trip else None
