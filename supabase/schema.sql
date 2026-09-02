-- ═══════════════════════════════════════════════════════════════════════════
-- CanRider — esquema completo de la base de datos
-- ═══════════════════════════════════════════════════════════════════════════
-- Este es el ÚNICO archivo SQL que hace falta ejecutar para dejar Supabase
-- listo. Crea las tres tablas que usa el proyecto (can_signals, telemetry,
-- trips), sus índices y sus permisos de acceso.
--
-- Es seguro ejecutarlo más de una vez: todas las instrucciones comprueban
-- primero si la tabla, columna o índice ya existe antes de crearlo, así que
-- si lo vuelves a pegar y ejecutar por error no vas a romper nada ni a
-- duplicar datos.
--
-- Instrucciones de uso paso a paso (para quien nunca ha tocado una base de
-- datos): ver el apartado "Volcar la base de datos" de docs/index.html,
-- o el paso 2 del README.
-- ═══════════════════════════════════════════════════════════════════════════


-- ─────────────────────────────────────────────────────────────────────────
-- Tabla 1: can_signals (referencia — el firmware YA NO la usa en tiempo real)
-- ─────────────────────────────────────────────────────────────────────────
-- Pensada en su día para que el firmware descargara de aquí qué tramas CAN
-- leer/emitir. Se cambió a señales hardcodeadas en main/main.ino
-- (setupCANSignals()) por fiabilidad en roaming y para no exponer en una
-- tabla de lectura pública el protocolo CAN real de cada moto. El firmware
-- actual ni lee ni escribe esta tabla — se mantiene solo como documentación
-- manual opcional de tu propio protocolo CAN, si te resulta útil llevarla.
--
-- direction:      'rx' → tu firmware ESCUCHA esta trama y extrae el valor
--                 'tx' → el firmware EMITE esta trama cada tx_interval_ms ms
-- tx_interval_ms: intervalo de emisión en ms (solo aplica cuando direction='tx')
-- dual_mode:      true → el firmware también escucha frame_id+1 (solo RX)
-- bit_mask:       0    → extracción de bytes normal (byte_length bytes desde byte_start)
--                 N>0  → extracción de bit: result = (data[byte_start] & N) ? 1 : 0
--                 Útil para flags individuales dentro de un byte de estado.

CREATE TABLE IF NOT EXISTS can_signals (
    id              uuid    DEFAULT gen_random_uuid() PRIMARY KEY,
    vehicle_id      text    NOT NULL,
    frame_id        integer NOT NULL,
    direction       text    NOT NULL DEFAULT 'rx' CHECK (direction IN ('rx', 'tx')),
    tx_interval_ms  integer NOT NULL DEFAULT 200,
    dual_mode       boolean NOT NULL DEFAULT false,
    signal_name     text    NOT NULL,
    byte_start      integer NOT NULL,
    byte_length     integer NOT NULL DEFAULT 1,
    bit_mask        integer NOT NULL DEFAULT 0,
    big_endian      boolean NOT NULL DEFAULT true,
    is_signed       boolean NOT NULL DEFAULT false,
    scale           float   NOT NULL DEFAULT 1.0,
    offset_val      float   NOT NULL DEFAULT 0.0,

    CONSTRAINT uniq_vehicle_signal UNIQUE (vehicle_id, signal_name)
);

-- Por si la tabla ya existía de una versión anterior sin estas columnas:
ALTER TABLE can_signals
  ADD COLUMN IF NOT EXISTS direction      text    NOT NULL DEFAULT 'rx' CHECK (direction IN ('rx', 'tx')),
  ADD COLUMN IF NOT EXISTS tx_interval_ms integer NOT NULL DEFAULT 200,
  ADD COLUMN IF NOT EXISTS dual_mode      boolean NOT NULL DEFAULT false,
  ADD COLUMN IF NOT EXISTS bit_mask       integer NOT NULL DEFAULT 0;

CREATE INDEX IF NOT EXISTS idx_can_signals_vehicle
    ON can_signals (vehicle_id);

ALTER TABLE can_signals ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "lectura autenticada" ON can_signals;
CREATE POLICY "lectura autenticada"
    ON can_signals FOR SELECT
    USING (auth.role() = 'anon');


-- ─────────────────────────────────────────────────────────────────────────
-- Tabla 2: telemetry
-- ─────────────────────────────────────────────────────────────────────────
-- Tabla principal de telemetría. Cada fila es una lectura que envía el
-- ESP32 — la tabla que más crece, y la que alimenta el mapa y las gráficas
-- del portal.
--
-- Columnas del módem/GPS (AT+CBC, AT+CSQ):
--   motorcycle_id, latitude, longitude, speed, battery_level,
--   battery_voltage (tensión del ESP32/LiPo, NO la de la moto), is_charging,
--   signal_strength, position_source ('gps' o 'lbs' — de dónde viene la
--   posición cuando no hay cobertura GPS), timestamp
--   moving_without_can → true si el GPS mide movimiento real mientras el
--   bus CAN lleva 8s+ en silencio (moto apagada) — la moto no se mueve
--   sola apagada, así que esto es indicio de sustracción/transporte sin
--   llave. Ver CAN_ALIVE_TIMEOUT_MS/THEFT_SPEED_KMH en main/main.ino.
--
-- Columnas CAN (pack de la moto, ver supabase/seed_cpx.sql si lo tienes):
--   soc             → State of Charge del pack EV (%)
--   pack_voltage    → Tensión del pack (V)   — ≠ battery_voltage (son distintas)
--   battery_current → Corriente (A): + = carga, - = descarga
--   charge_current  → Corriente de carga activa (A)
--   cell_voltage    → Tensión por celda (V)
--   bms_charging    → Flag BMS: 1=cargando  — ≠ is_charging del modem
--   charging_status_raw / battery_mode_raw → bytes de estado sin procesar
--   temp1..temp4    → Temperaturas de celdas (°C)
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
    position_source     text,
    moving_without_can  boolean,        -- GPS en movimiento con el bus CAN en silencio: posible sustracción

    -- Batería del módulo (AT+CBC — ESP32/LiPo)
    battery_level       int,
    battery_voltage     float,
    is_charging          boolean,

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

-- Por si la tabla ya existía de una versión anterior sin estas columnas:
ALTER TABLE telemetry
  ADD COLUMN IF NOT EXISTS position_source     text,
  ADD COLUMN IF NOT EXISTS moving_without_can  boolean,
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

CREATE INDEX IF NOT EXISTS idx_telemetry_motorcycle_ts
    ON telemetry (motorcycle_id, timestamp DESC);

ALTER TABLE telemetry ENABLE ROW LEVEL SECURITY;

-- El firmware inserta con la anon key — permitir INSERT anónimo.
-- Ajusta a una policy más restrictiva si añades autenticación de usuario.
DROP POLICY IF EXISTS "insert anon" ON telemetry;
CREATE POLICY "insert anon"
    ON telemetry FOR INSERT
    WITH CHECK (true);

-- Lectura pública (anon) para que el portal web funcione sin login.
-- Cambia a auth.role() = 'authenticated' si añades login al portal.
DROP POLICY IF EXISTS "select anon" ON telemetry;
CREATE POLICY "select anon"
    ON telemetry FOR SELECT
    USING (true);


-- ─────────────────────────────────────────────────────────────────────────
-- Tabla 3: trips
-- ─────────────────────────────────────────────────────────────────────────
-- El firmware inserta una fila al final de cada viaje.
-- Un viaje comienza con la primera trama CAN recibida (moto encendida) y
-- termina cuando el bus lleva 8s sin ninguna trama (moto apagada) — no se
-- basa en la velocidad GPS. Distancia y velocidad máxima sí vienen del GPS.

CREATE TABLE IF NOT EXISTS trips (
    id                    uuid        DEFAULT gen_random_uuid() PRIMARY KEY,
    motorcycle_id         text        NOT NULL,
    start_time            timestamptz NOT NULL,
    end_time              timestamptz,
    distance              float,       -- km (Haversine acumulado por GPS)
    duration              text,        -- "1h 23min"
    max_speed             float,       -- km/h
    start_battery_level   float,       -- SoC al inicio (%)
    end_battery_level     float,       -- SoC al final (%)
    consumption           float,       -- start_battery_level − end_battery_level (%)
    track                 jsonb        -- [[lat,lon,velocidad_kmh], ...] — traza real del recorrido
);

-- Por si la tabla ya existía de una versión anterior sin esta columna:
ALTER TABLE trips
  ADD COLUMN IF NOT EXISTS track jsonb;

CREATE INDEX IF NOT EXISTS idx_trips_motorcycle_ts
    ON trips (motorcycle_id, start_time DESC);

ALTER TABLE trips ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "insert anon" ON trips;
CREATE POLICY "insert anon" ON trips FOR INSERT WITH CHECK (true);

DROP POLICY IF EXISTS "select anon" ON trips;
CREATE POLICY "select anon" ON trips FOR SELECT USING (true);

-- ═══════════════════════════════════════════════════════════════════════════
-- Fin. Si el editor de Supabase dice "Success. No rows returned" al final,
-- todo se ha creado correctamente.
-- ═══════════════════════════════════════════════════════════════════════════
