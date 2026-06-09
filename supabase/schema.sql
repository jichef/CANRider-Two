-- ── can_signals ───────────────────────────────────────────────────────────────
-- Define qué tramas CAN leer (RX) o emitir (TX) y cómo decodificar/codificar.
-- El firmware descarga esta tabla al arrancar y la usa en el parser CAN.
--
-- direction:      'rx' → el firmware ESCUCHA esta trama y extrae el valor
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

-- ── Migración para bases de datos existentes ──────────────────────────────────
-- Ejecuta esto en Supabase SQL Editor si la tabla ya existe:
--
-- ALTER TABLE can_signals
--   ADD COLUMN IF NOT EXISTS direction text NOT NULL DEFAULT 'rx'
--     CHECK (direction IN ('rx', 'tx')),
--   ADD COLUMN IF NOT EXISTS tx_interval_ms integer NOT NULL DEFAULT 200;

CREATE INDEX IF NOT EXISTS idx_can_signals_vehicle
    ON can_signals (vehicle_id);

-- Row Level Security (recomendado en Supabase)
ALTER TABLE can_signals ENABLE ROW LEVEL SECURITY;

CREATE POLICY "lectura autenticada"
    ON can_signals FOR SELECT
    USING (auth.role() = 'anon');
