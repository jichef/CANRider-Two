-- ── telemetry ─────────────────────────────────────────────────────────────────
-- Tabla principal de telemetría. Cada fila es una lectura del ESP32.
-- Ejecutar en Supabase → SQL Editor antes de arrancar el firmware.
--
-- Columnas AT+CBC (modem/GPS):
--   motorcycle_id, latitude, longitude, speed, battery_level,
--   battery_voltage (tensión del módulo ESP32/LiPo), is_charging, timestamp
--
-- Columnas CAN (CPX seed_cpx.sql):
--   soc           → State of Charge del pack EV (%)
--   pack_voltage  → Tensión del pack (V)   — ≠ battery_voltage (son diferentes)
--   battery_current → Corriente (A): + = carga, - = descarga
--   charge_current  → Corriente de carga activa (A)
--   cell_voltage    → Tensión por celda (V)
--   bms_charging    → Flag BMS: 1=cargando  — ≠ is_charging del modem
--   charging_status_raw / battery_mode_raw → bytes de estado sin procesar
--   temp1..temp4  → Temperaturas de celdas (°C)
--   max_voltage, max_charge_current → límites configurados en BMS

CREATE TABLE IF NOT EXISTS telemetry (
    id                  bigint    GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    timestamp           timestamptz NOT NULL DEFAULT now(),

    -- Identificación
    motorcycle_id       text      NOT NULL,

    -- GPS / modem
    latitude            float,
    longitude           float,
    speed               float,

    -- Batería del módulo (AT+CBC — ESP32/LiPo)
    battery_level       int,
    battery_voltage     float,
    is_charging         boolean,

    -- Señal de red (AT+CSQ → dBm)
    signal_strength     smallint,

    -- CAN: estado general del pack EV
    soc                 float,
    pack_voltage        float,
    battery_current     float,
    charge_current      float,
    cell_voltage        float,
    bms_charging        float,          -- 1.0 = cargando, 0.0 = no (viene como float del firmware)

    -- CAN: bytes de estado raw (diagnóstico)
    charging_status_raw float,
    battery_mode_raw    float,

    -- CAN: temperaturas de celdas
    temp1               float,
    temp2               float,
    temp3               float,
    temp4               float,

    -- CAN: límites del BMS
    max_voltage         float,
    max_charge_current  float
);

CREATE INDEX IF NOT EXISTS idx_telemetry_motorcycle_ts
    ON telemetry (motorcycle_id, timestamp DESC);

-- Row Level Security
ALTER TABLE telemetry ENABLE ROW LEVEL SECURITY;

-- El firmware inserta con la anon key — permitir INSERT anónimo.
-- Ajusta a una policy más restrictiva si usas autenticación de usuario.
CREATE POLICY "insert anon"
    ON telemetry FOR INSERT
    WITH CHECK (true);

-- Lectura pública (anon) para el portal web sin login.
-- Cambia a auth.role() = 'authenticated' si añades login al portal.
CREATE POLICY "select anon"
    ON telemetry FOR SELECT
    USING (true);
