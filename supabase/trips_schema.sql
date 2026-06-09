-- ── trips ─────────────────────────────────────────────────────────────────────
-- El firmware inserta una fila al final de cada viaje.
-- Un viaje comienza cuando speed >= 5 km/h y termina tras 2 min parado.

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
    consumption           float        -- start_battery_level − end_battery_level (%)
);

CREATE INDEX IF NOT EXISTS idx_trips_motorcycle_ts
    ON trips (motorcycle_id, start_time DESC);

ALTER TABLE trips ENABLE ROW LEVEL SECURITY;

CREATE POLICY "insert anon" ON trips FOR INSERT WITH CHECK (true);
CREATE POLICY "select anon" ON trips FOR SELECT USING (true);
