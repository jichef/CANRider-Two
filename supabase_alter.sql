-- Agregar columnas faltantes a can_configurations

ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS vb_id integer default 1285;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS vb_start integer default 2;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS vb_len integer default 2;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS vb_factor float default 0.01;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS vb_be boolean default true;

ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS cb_id integer default 1285;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS cb_start integer default 4;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS cb_len integer default 2;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS cb_factor float default 0.1;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS cb_signed boolean default true;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS cb_be boolean default true;

ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS sb_id integer default 1345;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS sb_start integer default 0;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS sb_len integer default 1;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS sb_factor float default 1.0;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS sb_be boolean default true;

ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS tb_id integer default 1345;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS tb_start integer default 3;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS tb_len integer default 1;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS tb_factor float default 1.0;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS tb_be boolean default true;

ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS time_tx_id integer default 1296;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS time_hour_byte integer default 5;
ALTER TABLE can_configurations ADD COLUMN IF NOT EXISTS time_min_byte integer default 6;

-- Tabla para eventos de robo (movimiento GPS sin CAN activo)
CREATE TABLE IF NOT EXISTS theft_events (
  id uuid default gen_random_uuid() primary key,
  motorcycle_id text not null,
  start_time timestamp with time zone not null,
  end_time timestamp with time zone,
  status text default 'active',
  start_latitude double precision,
  start_longitude double precision,
  end_latitude double precision,
  end_longitude double precision,
  distance_km double precision default 0,
  max_speed double precision default 0,
  location_history jsonb,
  battery_level_start integer,
  battery_level_end integer,
  signal_strength integer,
  created_at timestamp with time zone default timezone('utc'::text, now()) not null
);

CREATE INDEX IF NOT EXISTS theft_events_motorcycle_id_idx ON theft_events(motorcycle_id);
CREATE INDEX IF NOT EXISTS theft_events_start_time_idx ON theft_events(start_time desc);

-- Agregar columna is_theft_detected a tabla trips (si no existe)
ALTER TABLE trips ADD COLUMN IF NOT EXISTS is_theft_detected boolean default false;
