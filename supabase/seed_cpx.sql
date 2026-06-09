-- ── Señales CAN — SuperSoco / VMoto CPX ──────────────────────────────────────
-- Sustituye :VEHICLE_ID por el UUID real de tu moto.
--
-- La CPX usa dos conjuntos de IDs según el modo de batería:
--   Modo A: 0x504, 0x506, 0x540, 0x54E
--   Modo B: 0x505, 0x507, 0x541, 0x54F  (= modo A + 1)
-- dual_mode=true hace que el firmware escuche ambos IDs con una sola fila.
--
-- Endianness: se asume big-endian en todos los campos multi-byte.
-- Temperaturas: escala 1:1 Celsius directo — verificar con datos reales.
-- Corriente 0x504 bytes 4-5: signed → positivo=carga, negativo=descarga
--   (convención pendiente de confirmar en banco).

INSERT INTO can_signals
  (vehicle_id, frame_id, direction, dual_mode, signal_name,
   byte_start, byte_length, bit_mask, big_endian, is_signed, scale, offset_val)
VALUES

-- ── 0x504 / 0x505  Charging Information ──────────────────────────────────────
-- Voltaje del pack (V) = raw × 0.1  — "pack_voltage" para no colisionar con battery_voltage del modem
(':VEHICLE_ID', 0x504, 'rx', true, 'pack_voltage',         2, 2, 0, true,  false, 0.1,  0.0),

-- Corriente (A) = raw × 0.1 — signed: positivo=carga, negativo=descarga
(':VEHICLE_ID', 0x504, 'rx', true, 'battery_current',      4, 2, 0, true,  true,  0.1,  0.0),

-- Estado de carga raw: 0x95=cargando, 0x15=carga finalizada, 0x00=uso normal
(':VEHICLE_ID', 0x504, 'rx', true, 'charging_status_raw',  7, 1, 0, true,  false, 1.0,  0.0),

-- ── 0x506 / 0x507  Battery State and Charging Information ────────────────────
-- Corriente de carga (A) = raw × 0.1
(':VEHICLE_ID', 0x506, 'rx', true, 'charge_current',       2, 2, 0, true,  false, 0.1,  0.0),

-- Voltaje por celda (V) = raw × 0.001
(':VEHICLE_ID', 0x506, 'rx', true, 'cell_voltage',         4, 2, 0, true,  false, 0.001,0.0),

-- Flag carga en progreso (byte 1, bit 4): 1=cargando, 0=no  — "bms_charging" para no colisionar con is_charging del modem
(':VEHICLE_ID', 0x506, 'rx', true, 'bms_charging',         1, 1, 0x10, true, false, 1.0, 0.0),

-- Modo batería raw: 0x10=charging mode, 0x20=riding mode
(':VEHICLE_ID', 0x506, 'rx', true, 'battery_mode_raw',     7, 1, 0, true,  false, 1.0,  0.0),

-- ── 0x540 / 0x541  Estado general de batería ──────────────────────────────────
-- SoC (%) — confirmado que coincide con cuadro de instrumentos
(':VEHICLE_ID', 0x540, 'rx', true, 'soc',                  0, 1, 0, true,  false, 1.0,  0.0),

-- Temperaturas (°C) — verificar offset si los valores parecen desplazados
(':VEHICLE_ID', 0x540, 'rx', true, 'temp1',                3, 1, 0, true,  false, 1.0,  0.0),
(':VEHICLE_ID', 0x540, 'rx', true, 'temp2',                4, 1, 0, true,  false, 1.0,  0.0),
(':VEHICLE_ID', 0x540, 'rx', true, 'temp3',                5, 1, 0, true,  false, 1.0,  0.0),
(':VEHICLE_ID', 0x540, 'rx', true, 'temp4',                6, 1, 0, true,  false, 1.0,  0.0),

-- ── 0x54E / 0x54F  Parámetros generales de batería ───────────────────────────
-- Voltaje máximo configurado (V) = raw × 0.1
(':VEHICLE_ID', 0x54E, 'rx', true, 'max_voltage',          0, 2, 0, true,  false, 0.1,  0.0),

-- Corriente máxima de carga configurada (A) = raw × 0.1
(':VEHICLE_ID', 0x54E, 'rx', true, 'max_charge_current',   2, 2, 0, true,  false, 0.1,  0.0),

-- ── 0x510  Hora hacia la pantalla (el ESP32 emite, la pantalla lee) ──────────
(':VEHICLE_ID', 0x510, 'tx', false, 'clock_hours',          5, 1, 0, true,  false, 1.0,  0.0),
(':VEHICLE_ID', 0x510, 'tx', false, 'clock_minutes',        6, 1, 0, true,  false, 1.0,  0.0);

-- Notas:
-- • 0x54E byte 4 también contiene SoC — omitido porque 0x540[0] está confirmado.
-- • 0x54E byte 7 (0x30=cargando / 0x00=no) omitido porque bms_charging (0x506) es más preciso.
-- • 0x506 byte 0 bitmask flags omitidos — añadir si se necesitan en HA.
-- • "pack_voltage" y "bms_charging" evitan conflicto con battery_voltage/is_charging del modem (AT+CBC).
