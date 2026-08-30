-- La tabla telemetry en producción no tiene las columnas que el firmware
-- necesita para las señales CAN (se creó con un esquema antiguo de doble
-- batería: moto_battery, bat_a_*, bat_b_*). Sin esto, en cuanto el CAN
-- esté conectado, cada POST con datos CAN fallará porque Supabase no
-- puede insertar columnas que no existen.

ALTER TABLE telemetry
  ADD COLUMN IF NOT EXISTS soc                 float,
  ADD COLUMN IF NOT EXISTS pack_voltage        float,
  ADD COLUMN IF NOT EXISTS battery_current     float,
  ADD COLUMN IF NOT EXISTS charge_current      float,
  ADD COLUMN IF NOT EXISTS cell_voltage        float,
  ADD COLUMN IF NOT EXISTS bms_charging        float,
  ADD COLUMN IF NOT EXISTS charging_status_raw float,
  ADD COLUMN IF NOT EXISTS battery_mode_raw    float,
  ADD COLUMN IF NOT EXISTS temp1               float,
  ADD COLUMN IF NOT EXISTS temp2               float,
  ADD COLUMN IF NOT EXISTS temp3               float,
  ADD COLUMN IF NOT EXISTS temp4               float,
  ADD COLUMN IF NOT EXISTS max_voltage         float,
  ADD COLUMN IF NOT EXISTS max_charge_current  float;
