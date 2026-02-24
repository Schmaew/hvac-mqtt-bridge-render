-- Migration: Drop unused columns from sensor_readings table
-- Date: 2026-02-24
-- Reason: Simplify schema - remove supply_air_temp, return_air_temp, fan_current, created_at, weather_humidity

-- Drop unused columns
ALTER TABLE sensor_readings DROP COLUMN IF EXISTS supply_air_temp;
ALTER TABLE sensor_readings DROP COLUMN IF EXISTS return_air_temp;
ALTER TABLE sensor_readings DROP COLUMN IF EXISTS fan_current;
ALTER TABLE sensor_readings DROP COLUMN IF EXISTS created_at;
ALTER TABLE sensor_readings DROP COLUMN IF EXISTS weather_humidity;

-- Verify remaining columns
-- SELECT column_name, data_type FROM information_schema.columns WHERE table_name = 'sensor_readings';
