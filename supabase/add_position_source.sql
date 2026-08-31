-- Distingue si la posición de una lectura de telemetría viene de GPS real
-- o de una estimación aproximada por LBS (triangulación de celda), usada
-- como respaldo automático por el firmware cuando no hay fix GPS.
ALTER TABLE telemetry
  ADD COLUMN IF NOT EXISTS position_source text;
