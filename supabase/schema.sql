-- ═══════════════════════════════════════════════════════════════════════════
-- CanRider — esquema completo de la base de datos
-- ═══════════════════════════════════════════════════════════════════════════
-- Este es el ÚNICO archivo SQL que hace falta ejecutar para dejar Supabase
-- listo. Crea las dos tablas que usa el proyecto (telemetry, trips), sus
-- índices y sus permisos de acceso — y limpia cualquier resto de una
-- versión anterior del proyecto (otras tablas, columnas o tipos) que
-- pudiera quedar en la base de datos.
--
-- Es seguro ejecutarlo más de una vez: todas las instrucciones comprueban
-- primero si la tabla, columna o índice ya existe antes de crearlo (y las
-- que corrigen tipos de una versión anterior no hacen nada si ya están
-- bien), así que si lo vuelves a pegar y ejecutar por error no vas a
-- romper nada ni a duplicar datos.
--
-- Instrucciones de uso paso a paso (para quien nunca ha tocado una base de
-- datos): ver el apartado "Volcar la base de datos" de docs/index.html,
-- o el paso 2 del README.
-- ═══════════════════════════════════════════════════════════════════════════


-- ─────────────────────────────────────────────────────────────────────────
-- Tabla: telemetry
-- ─────────────────────────────────────────────────────────────────────────
-- Tabla principal de telemetría. Cada fila es una lectura que envía el
-- ESP32 — la tabla que más crece, y la que alimenta el mapa y las gráficas
-- del portal.
--
-- Columnas del módem/GPS (AT+CSQ, AT+CGNSINF/AT+CLBS):
--   motorcycle_id, latitude, longitude, speed, signal_strength,
--   position_source ('gps' o 'lbs' — de dónde viene la posición cuando no
--   hay cobertura GPS), position_accuracy (radio de precisión en metros,
--   solo LBS), timestamp
--   moving_without_can → true si el GPS mide movimiento real mientras el
--   bus CAN lleva 8s+ en silencio (moto apagada) — la moto no se mueve
--   sola apagada, así que esto es indicio de sustracción/transporte sin
--   llave (salvo viaje manual sin CAN en marcha, ver manualTripActive en
--   main.ino). Ver CAN_ALIVE_TIMEOUT_MS/THEFT_SPEED_KMH en main/main.ino.
--   board_battery_voltage / board_battery_level → LiPo/18650 por el ADC
--   propio del ESP32 (BOARD_BAT_ADC_PIN) — NO por AT+CBC del módem, que se
--   quitó por poco fiable (reportaba bcs=0 "no cargando" incluso con el
--   dispositivo claramente en USB). Ver readBoardBatteryVoltage() en
--   main.ino.
--   board_on_usb → por diseño de esta placa (confirmado en el ejemplo
--   oficial de LilyGo), el circuito de detección de batería se
--   desconecta físicamente al conectar USB — voltaje ~0 no es un fallo
--   de lectura, es la señal de "está en USB". Cuando es true,
--   board_battery_voltage/level vienen NULL a propósito (no se manda un
--   "0%" que parecería, incorrectamente, batería agotada).
--
-- Columnas CAN (pack de la moto): las únicas que el firmware actual
-- escribe de verdad son moto_battery/moto_battery_b (SoC de cada modo del
-- BMS) y bms_charging — ver setupCANSignals() en main.ino. Si añades más
-- señales ahí, añade también su columna aquí.

CREATE TABLE IF NOT EXISTS telemetry (
    id                  bigint    GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    timestamp           timestamptz NOT NULL DEFAULT now(),

    -- Identificación
    motorcycle_id       text      NOT NULL,
    connection_type     text,           -- 'wifi' o 'lte' — por qué camino se mandó esta fila

    -- GPS / modem
    latitude            float,
    longitude           float,
    speed               float,
    position_source     text,
    position_accuracy   float,          -- radio de precisión en metros — solo LBS (AT+CLBS), GPS no lo manda
    moving_without_can  boolean,        -- GPS en movimiento con el bus CAN en silencio: posible sustracción

    -- Batería del módulo (ADC propio del ESP32 — ver comentario arriba)
    board_on_usb           boolean,      -- true = circuito de detección desconectado (en USB) — voltage/level se omiten en ese caso
    board_battery_voltage  float,
    board_battery_level    int,

    -- Señal de red (AT+CSQ → dBm)
    signal_strength     smallint,

    -- CAN: estado del pack EV (únicas señales que el firmware escribe hoy)
    moto_battery        int,            -- SoC (%) — modo A del BMS (0x540)
    moto_battery_b      int,            -- SoC (%) — modo B del BMS (0x541); en la práctica solo uno de los dos trae dato real a la vez
    bms_charging        float           -- 1.0 = cargando, 0.0 = no (viene como float del firmware)
);

-- Por si la tabla ya existía de una versión anterior sin estas columnas:
ALTER TABLE telemetry
  ADD COLUMN IF NOT EXISTS connection_type     text,
  ADD COLUMN IF NOT EXISTS position_source     text,
  ADD COLUMN IF NOT EXISTS position_accuracy   float,
  ADD COLUMN IF NOT EXISTS moving_without_can  boolean,
  ADD COLUMN IF NOT EXISTS moto_battery        int,
  ADD COLUMN IF NOT EXISTS moto_battery_b      int,
  ADD COLUMN IF NOT EXISTS bms_charging        float,
  ADD COLUMN IF NOT EXISTS board_battery_voltage float,
  ADD COLUMN IF NOT EXISTS board_battery_level   int,
  ADD COLUMN IF NOT EXISTS board_on_usb          boolean;

CREATE INDEX IF NOT EXISTS idx_telemetry_motorcycle_ts
    ON telemetry (motorcycle_id, timestamp DESC);

ALTER TABLE telemetry ENABLE ROW LEVEL SECURITY;

-- El firmware inserta con la anon key — permitir INSERT anónimo.
-- Ajusta a una policy más restrictiva si añades autenticación de usuario.
DROP POLICY IF EXISTS "insert anon" ON telemetry;
CREATE POLICY "insert anon"
    ON telemetry FOR INSERT
    WITH CHECK (true);

-- El portal ya tiene login (Supabase Auth, ver src/middleware.ts) — solo
-- un usuario autenticado puede leer telemetría. El firmware NUNCA
-- necesita leer esta tabla (solo inserta), así que no le afecta.
DROP POLICY IF EXISTS "select anon" ON telemetry;
DROP POLICY IF EXISTS "select authenticated" ON telemetry;
CREATE POLICY "select authenticated"
    ON telemetry FOR SELECT
    USING (auth.role() = 'authenticated');

-- Triggers de la versión anterior sobre telemetry/trips (por ejemplo, uno
-- que mantenía is_trip_active) — se eliminan ANTES de quitar las columnas
-- de abajo: un trigger que referencia una columna ya borrada no falla al
-- borrar la columna, falla en el momento de cada INSERT futuro con un
-- error de Postgres tipo "record new has no field ...". El RAISE NOTICE
-- deja ver en el resultado del SQL Editor qué se ha eliminado, si algo.
DO $$
DECLARE
  trig record;
BEGIN
  FOR trig IN
    SELECT t.tgname, c.relname
    FROM pg_trigger t
    JOIN pg_class c ON c.oid = t.tgrelid
    WHERE c.relname IN ('telemetry', 'trips')
      AND NOT t.tgisinternal
  LOOP
    RAISE NOTICE 'Eliminando trigger % de tabla %', trig.tgname, trig.relname;
    EXECUTE format('DROP TRIGGER %I ON %I', trig.tgname, trig.relname);
  END LOOP;
END $$;

-- Columnas sueltas de una versión anterior del proyecto (con PostGIS y
-- otro diseño de tablas) que ya no se usa — el firmware actual nunca las
-- lee ni las escribe. Se quitan para que el esquema real coincida con lo
-- que este archivo documenta.
ALTER TABLE telemetry
  DROP COLUMN IF EXISTS location,
  DROP COLUMN IF EXISTS bat_a_volts,
  DROP COLUMN IF EXISTS bat_a_amps,
  DROP COLUMN IF EXISTS bat_a_temp,
  DROP COLUMN IF EXISTS bat_b_volts,
  DROP COLUMN IF EXISTS bat_b_amps,
  DROP COLUMN IF EXISTS bat_b_temp,
  DROP COLUMN IF EXISTS is_charging_b,
  DROP COLUMN IF EXISTS location_type,
  DROP COLUMN IF EXISTS date,
  DROP COLUMN IF EXISTS is_trip_active,
  DROP COLUMN IF EXISTS trip_duration,
  DROP COLUMN IF EXISTS trip_start,
  DROP COLUMN IF EXISTS trip_end,
  DROP COLUMN IF EXISTS start_time,
  DROP COLUMN IF EXISTS end_time,
  DROP COLUMN IF EXISTS duration;

-- Columnas de CanRider mismo que dejaron de usarse:
--   battery_level/battery_voltage/is_charging → venían de AT+CBC del
--   módem, sustituido por board_battery_* (ADC propio del ESP32, más
--   fiable). Se pierde el histórico de estas tres, a propósito.
--   soc/pack_voltage/battery_current/charge_current/cell_voltage/
--   charging_status_raw/battery_mode_raw/temp1..4/max_voltage/
--   max_charge_current → pensadas para un decodificador CAN más completo
--   que nunca llegó a tener señales reales configuradas (setupCANSignals()
--   en main.ino solo define moto_battery/moto_battery_b/bms_charging) —
--   no ha habido firmware en este repo que las escribiera nunca.
ALTER TABLE telemetry
  DROP COLUMN IF EXISTS battery_level,
  DROP COLUMN IF EXISTS battery_voltage,
  DROP COLUMN IF EXISTS is_charging,
  DROP COLUMN IF EXISTS soc,
  DROP COLUMN IF EXISTS pack_voltage,
  DROP COLUMN IF EXISTS battery_current,
  DROP COLUMN IF EXISTS charge_current,
  DROP COLUMN IF EXISTS cell_voltage,
  DROP COLUMN IF EXISTS charging_status_raw,
  DROP COLUMN IF EXISTS battery_mode_raw,
  DROP COLUMN IF EXISTS temp1,
  DROP COLUMN IF EXISTS temp2,
  DROP COLUMN IF EXISTS temp3,
  DROP COLUMN IF EXISTS temp4,
  DROP COLUMN IF EXISTS max_voltage,
  DROP COLUMN IF EXISTS max_charge_current;


-- ─────────────────────────────────────────────────────────────────────────
-- Limpieza: tablas de una versión anterior del proyecto (no se usan)
-- ─────────────────────────────────────────────────────────────────────────
-- motorcycles, locations, theft_events y can_configurations vienen de un
-- diseño anterior (con PostGIS) que se abandonó. can_signals fue un
-- intento posterior de que el firmware descargara de ahí qué tramas CAN
-- leer/emitir — se cambió a señales hardcodeadas en main/main.ino
-- (setupCANSignals()) por fiabilidad en roaming y para no exponer en una
-- tabla de lectura pública el protocolo CAN real de la moto; el firmware
-- nunca llegó a leerla ni escribirla en producción. El firmware y el
-- portal actuales no leen ni escriben ninguna de estas cinco — se eliminan
-- para que el esquema real de Supabase coincida con lo que usa el
-- proyecto. CASCADE se lleva también la clave foránea que
-- trips.motorcycle_id tenía hacia motorcycles(id), heredada del diseño
-- con PostGIS.

DROP TABLE IF EXISTS motorcycles       CASCADE;
DROP TABLE IF EXISTS locations         CASCADE;
DROP TABLE IF EXISTS theft_events      CASCADE;
DROP TABLE IF EXISTS can_configurations CASCADE;
DROP TABLE IF EXISTS can_signals       CASCADE;


-- ─────────────────────────────────────────────────────────────────────────
-- Tabla: trips
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
    track                 jsonb,       -- [[lat,lon,velocidad_kmh], ...] — traza real del recorrido
    in_progress           boolean     NOT NULL DEFAULT true  -- true mientras el viaje se sigue actualizando por checkpoints, false en el cierre final (ver updateTrip()/buildTripBody() en main.ino)
);

-- Por si la tabla ya existía de una versión anterior sin esta columna:
ALTER TABLE trips
  ADD COLUMN IF NOT EXISTS track jsonb;
ALTER TABLE trips
  ADD COLUMN IF NOT EXISTS in_progress boolean NOT NULL DEFAULT true;

-- Backfill de una sola vez: todo lo que ya existía en la tabla antes de
-- este flag (16/09/2026) se guardó siempre como un único POST de cierre —
-- los checkpoints intermedios no llegaban a intentarse en el hardware
-- desplegado hasta ese día (ver el fix de shouldSendTripCheckpoint() en
-- main.ino) — así que ya está completo, no "en curso". El corte por fecha
-- (en vez de "UPDATE ... WHERE in_progress = true" a secas) hace que
-- repetir este script más adelante no le borre el estado a un viaje que
-- sí esté en curso de verdad en ese momento.
UPDATE trips SET in_progress = false
  WHERE start_time < '2026-09-16T16:00:00+00:00' AND in_progress = true;

-- El portal se suscribe a cambios en tiempo real de esta tabla (para que
-- el flag in_progress y el resto de datos del viaje en curso se reflejen
-- en el historial sin recargar la página) — hace falta la tabla en la
-- publicación de Realtime de Supabase. Guardado con comprobación previa
-- porque ALTER PUBLICATION ... ADD TABLE falla si ya estaba añadida (p.ej.
-- si ya se había activado a mano desde Database > Replication).
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_publication_tables
    WHERE pubname = 'supabase_realtime' AND schemaname = 'public' AND tablename = 'trips'
  ) THEN
    ALTER PUBLICATION supabase_realtime ADD TABLE trips;
  END IF;
END $$;

-- Por si la tabla ya existía de la versión anterior con otros tipos u
-- otras columnas (ver comentario de la sección de limpieza más arriba):
-- CREATE TABLE IF NOT EXISTS no toca una tabla que ya existe, así que sin
-- esto los tipos incorrectos se quedarían para siempre y el firmware
-- nunca podría guardar un viaje (duration llegaba como texto contra una
-- columna integer, consumption como decimal contra integer, etc).
-- Repetir estas líneas en una tabla ya corregida es una operación sin
-- efecto, no rompe nada.
ALTER TABLE trips ALTER COLUMN duration            TYPE text  USING duration::text;
ALTER TABLE trips ALTER COLUMN consumption         TYPE float USING consumption::float;
ALTER TABLE trips ALTER COLUMN start_battery_level TYPE float USING start_battery_level::float;
ALTER TABLE trips ALTER COLUMN end_battery_level   TYPE float USING end_battery_level::float;
ALTER TABLE trips ALTER COLUMN motorcycle_id       TYPE text  USING motorcycle_id::text;

ALTER TABLE trips
  DROP COLUMN IF EXISTS avg_speed,
  DROP COLUMN IF EXISTS path,
  DROP COLUMN IF EXISTS trip_start,
  DROP COLUMN IF EXISTS trip_end,
  DROP COLUMN IF EXISTS trip_duration,
  DROP COLUMN IF EXISTS is_theft_detected,
  DROP COLUMN IF EXISTS created_at;

CREATE INDEX IF NOT EXISTS idx_trips_motorcycle_ts
    ON trips (motorcycle_id, start_time DESC);

ALTER TABLE trips ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "insert anon" ON trips;
CREATE POLICY "insert anon" ON trips FOR INSERT WITH CHECK (true);

-- El portal ya tiene login (Supabase Auth, ver src/middleware.ts) — solo
-- un usuario autenticado puede leer el historial de viajes. El firmware
-- nunca lee esta tabla (solo inserta/actualiza su propio viaje), así que
-- esto no le afecta.
DROP POLICY IF EXISTS "select anon" ON trips;
DROP POLICY IF EXISTS "select authenticated" ON trips;
CREATE POLICY "select authenticated" ON trips FOR SELECT USING (auth.role() = 'authenticated');

-- Faltaba esta: el propio firmware asume que existe (Prefer: resolution=
-- merge-duplicates en cada checkpoint intermedio del viaje, ver
-- httpPostTo()/postTripUpdate() en main.ino, pensado como upsert sobre la
-- primary key) pero sin política de UPDATE, RLS bloqueaba esa mitad del
-- upsert en cuanto un checkpoint SÍ coincidía con una fila ya existente —
-- solo pasaba desapercibido porque en el hardware desplegado (SIM7000G,
-- WiFi de rescate desactivado) los checkpoints intermedios no llegan a
-- intentarse nunca (shouldSendTripCheckpoint() exige WiFi ahí) y cada
-- viaje se guarda entero de una sola vez al cerrar, como un INSERT limpio
-- sin conflicto. Detectado el 15/09/2026 al intentar corregir a mano un
-- viaje ya guardado con puntos de LBS colados en la ruta (ver el fix de
-- updateTrip() en main.ino) y no poder hacer el UPDATE con la anon key.
DROP POLICY IF EXISTS "update anon" ON trips;
CREATE POLICY "update anon" ON trips FOR UPDATE USING (true) WITH CHECK (true);

-- Permite borrar viajes desde el portal (botón de la papelera en el
-- historial) — el firmware nunca borra un viaje, solo el portal (con
-- login), así que esto va a authenticated igual que el SELECT de arriba.
DROP POLICY IF EXISTS "delete anon" ON trips;
DROP POLICY IF EXISTS "delete authenticated" ON trips;
CREATE POLICY "delete authenticated" ON trips FOR DELETE USING (auth.role() = 'authenticated');


-- ─────────────────────────────────────────────────────────────────────────
-- Tabla: device_commands
-- ─────────────────────────────────────────────────────────────────────────
-- Cola de comandos remotos desde el portal (Vercel) hacia el ESP32. El
-- dispositivo nunca acepta conexiones entrantes — solo hace peticiones
-- salientes, cada 15s — así que el portal no puede "llamarlo" directamente
-- como sí hace el portal OTA local (que está en la misma red WiFi). En vez
-- de eso, el portal inserta aquí un comando pendiente (done=false), y el
-- firmware lo recoge solo en su siguiente ciclo por LTE (checkRemoteCommand()
-- en main.ino, cada ~60s — no en cada ciclo de 15s, para no doblar el
-- tráfico de red/consumo en cada telemetría normal), lo ejecuta, y escribe
-- el resultado de vuelta en la misma fila.
--
-- command soportados hoy (ver checkRemoteCommand() en main/main.ino):
--   'lbs_check' — fuerza una lectura de posición aproximada por celda
--   'gps_reset' — reinicia el receptor GNSS (por si se queda "encendido
--                 pero sordo", ver comentario de readGPS() en main.ino)
--
-- Solo lo procesa el firmware del SIM7000G (checkRemoteCommand() está
-- dentro del mismo #else SIM7000G que sim7000Request()) — el A7670G no
-- tiene esta cola todavía.

CREATE TABLE IF NOT EXISTS device_commands (
    id            bigint      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    motorcycle_id text        NOT NULL,
    command       text        NOT NULL,
    created_at    timestamptz NOT NULL DEFAULT now(),
    done          boolean     NOT NULL DEFAULT false,
    result        text
);

CREATE INDEX IF NOT EXISTS idx_device_commands_pending
    ON device_commands (motorcycle_id, created_at)
    WHERE done = false;

ALTER TABLE device_commands ENABLE ROW LEVEL SECURITY;

-- Solo el portal inserta comandos (el firmware únicamente los lee/marca
-- hechos, nunca crea uno) — con login ya en el portal, esto pasa a
-- authenticated. No le afecta al firmware, que no inserta aquí.
DROP POLICY IF EXISTS "insert anon" ON device_commands;
DROP POLICY IF EXISTS "insert authenticated" ON device_commands;
CREATE POLICY "insert authenticated" ON device_commands FOR INSERT WITH CHECK (auth.role() = 'authenticated');

-- El firmware necesita leer qué comando hay pendiente con la anon key
-- (checkRemoteCommand() en main.ino) — esta SÍ se queda anon a la fuerza,
-- no hay forma de que un ESP32 desatendido haga login interactivo. Deja
-- visible con la anon key la cola de comandos/resultados (p.ej. la
-- posición de un lbs_check) — menos sensible que trips/telemetry, pero
-- queda como hueco conocido mientras el firmware no tenga otra forma de
-- autenticarse.
DROP POLICY IF EXISTS "select anon" ON device_commands;
CREATE POLICY "select anon" ON device_commands FOR SELECT USING (true);

-- El firmware marca el comando como hecho (done=true) y deja el resultado.
DROP POLICY IF EXISTS "update anon" ON device_commands;
CREATE POLICY "update anon" ON device_commands FOR UPDATE USING (true) WITH CHECK (true);

-- Faltaba esta también (mismo fallo que ya se encontró en trips): el
-- portal borra la fila al terminar (éxito o timeout, ver
-- sendDeviceCommand()/pollDeviceCommand() en DashboardContent.tsx) para
-- que checkRemoteCommand() no vuelva a recoger la misma fila para
-- siempre — sin política de DELETE, esos DELETE llevaban fallando en
-- silencio (200 OK, 0 filas afectadas) desde que se añadió esta tabla.
-- Detectado el 15/09/2026: una fila de una prueba del 14/09 seguía
-- pendiente 24h después, reejecutándose cada 60s sin parar. El firmware
-- nunca borra aquí (solo el portal), así que esto también pasa a
-- authenticated con el login.
DROP POLICY IF EXISTS "delete anon" ON device_commands;
DROP POLICY IF EXISTS "delete authenticated" ON device_commands;
CREATE POLICY "delete authenticated" ON device_commands FOR DELETE USING (auth.role() = 'authenticated');

-- ═══════════════════════════════════════════════════════════════════════════
-- Fin. Si el editor de Supabase dice "Success. No rows returned" al final,
-- todo se ha creado correctamente.
-- ═══════════════════════════════════════════════════════════════════════════
