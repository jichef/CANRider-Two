from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorStateClass,
)
from homeassistant.const import (
    PERCENTAGE,
    SIGNAL_STRENGTH_DECIBELS_MILLIWATT,
    UnitOfElectricCurrent,
    UnitOfElectricPotential,
    UnitOfSpeed,
    UnitOfTemperature,
)
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN


async def async_setup_entry(hass, entry, async_add_entities):
    coordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([
        BatteryLevelSensor(coordinator),
        PackVoltageSensor(coordinator),
        BatteryCurrentSensor(coordinator),
        ChargeCurrentSensor(coordinator),
        CellVoltageSensor(coordinator),
        *[TemperatureSensor(coordinator, i) for i in range(1, 5)],
        SpeedSensor(coordinator),
        SignalStrengthSensor(coordinator),
        LastTripDistanceSensor(coordinator),
        LastTripMaxSpeedSensor(coordinator),
        LastTripConsumptionSensor(coordinator),
    ])


class CanRiderSensor(CoordinatorEntity, SensorEntity):
    def __init__(self, coordinator):
        super().__init__(coordinator)
        self._attr_device_info = {
            "identifiers": {(DOMAIN, coordinator.vehicle_id)},
            "name": f"CanRider {coordinator.vehicle_id}",
            "model": "T-A7670G ESP32",
            "manufacturer": "LilyGo",
        }

    @property
    def _tel(self):
        return self.coordinator.data.get("telemetry")

    @property
    def _trip(self):
        return self.coordinator.data.get("last_trip")


# ── Batería ────────────────────────────────────────────────────────────────────

class BatteryLevelSensor(CanRiderSensor):
    _attr_name = "Estado de Carga"
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_device_class = SensorDeviceClass.BATTERY
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_battery_level"

    @property
    def native_value(self):
        if not self._tel:
            return None
        # soc (CAN) es más preciso que battery_level (AT+CBC del módulo ESP32)
        soc = self._tel.get("soc")
        return soc if soc is not None else self._tel.get("battery_level")


class PackVoltageSensor(CanRiderSensor):
    _attr_name = "Tensión del Pack"
    _attr_native_unit_of_measurement = UnitOfElectricPotential.VOLT
    _attr_device_class = SensorDeviceClass.VOLTAGE
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_pack_voltage"

    @property
    def native_value(self):
        return self._tel.get("pack_voltage") if self._tel else None


class BatteryCurrentSensor(CanRiderSensor):
    _attr_name = "Corriente de Batería"
    _attr_native_unit_of_measurement = UnitOfElectricCurrent.AMPERE
    _attr_device_class = SensorDeviceClass.CURRENT
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_battery_current"

    @property
    def native_value(self):
        return self._tel.get("battery_current") if self._tel else None


class ChargeCurrentSensor(CanRiderSensor):
    _attr_name = "Corriente de Carga"
    _attr_native_unit_of_measurement = UnitOfElectricCurrent.AMPERE
    _attr_device_class = SensorDeviceClass.CURRENT
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_charge_current"

    @property
    def native_value(self):
        return self._tel.get("charge_current") if self._tel else None


class CellVoltageSensor(CanRiderSensor):
    _attr_name = "Tensión por Celda"
    _attr_native_unit_of_measurement = UnitOfElectricPotential.VOLT
    _attr_device_class = SensorDeviceClass.VOLTAGE
    _attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_cell_voltage"

    @property
    def native_value(self):
        return self._tel.get("cell_voltage") if self._tel else None


class TemperatureSensor(CanRiderSensor):
    _attr_device_class = SensorDeviceClass.TEMPERATURE
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS

    def __init__(self, coordinator, index: int):
        super().__init__(coordinator)
        self._index = index
        self._attr_name = f"Temperatura Celda {index}"

    @property
    def unique_id(self):
        return f"{self.coordinator.vehicle_id}_temp{self._index}"

    @property
    def native_value(self):
        return self._tel.get(f"temp{self._index}") if self._tel else None


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
