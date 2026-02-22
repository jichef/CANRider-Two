-- INSERT de prueba para viaje detectado por robo
INSERT INTO trips (
  motorcycle_id,
  start_time,
  end_time,
  distance,
  avg_speed,
  max_speed,
  consumption,
  start_battery_level,
  end_battery_level,
  path,
  is_theft_detected
) VALUES (
  '550e8400-e29b-41d4-a716-446655440000',
  now() - interval '30 minutes',
  now() - interval '5 minutes',
  3.5,
  18.5,
  42.3,
  15,
  85,
  70,
  '[[40.4168, -3.7038], [40.4200, -3.7100]]'::jsonb,
  true
);
