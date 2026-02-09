import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';

// Inicializamos el cliente de Supabase con la service_role_key para poder insertar datos
// La service_role_key NUNCA debe estar en el frontend
const supabaseAdmin = createClient(
  process.env.NEXT_PUBLIC_SUPABASE_URL!,
  process.env.SUPABASE_SERVICE_ROLE_KEY!
);

export async function POST(req: NextRequest) {
  try {
    // Verificación de API Key simple para el ESP32
    const apiKey = req.headers.get('x-api-key');
    if (apiKey !== process.env.TELEMETRY_API_KEY) {
      return NextResponse.json({ error: 'No autorizado' }, { status: 401 });
    }

    const data = await req.json();
    
    // Validar datos mínimos
    if (!data.motorcycle_id) {
      return NextResponse.json({ error: 'Falta motorcycle_id' }, { status: 400 });
    }

    const { error } = await supabaseAdmin
      .from('telemetry')
      .insert([{
        motorcycle_id: data.motorcycle_id,
        latitude: data.latitude,
        longitude: data.longitude,
        speed: data.speed,
        battery_level: data.battery_level,
        battery_voltage: data.battery_voltage,
        is_charging: data.is_charging,
        signal_strength: data.signal_strength,
        timestamp: data.timestamp || new Date().toISOString(),
        // Nuevos campos de Batería A y B
        moto_battery: data.moto_battery,
        moto_battery_b: data.moto_battery_b,
        bat_a_volts: data.bat_a_volts,
        bat_a_amps: data.bat_a_amps,
        bat_a_temp: data.bat_a_temp,
        bat_b_volts: data.bat_b_volts,
        bat_b_amps: data.bat_b_amps,
        bat_b_temp: data.bat_b_temp,
        is_charging_b: data.is_charging_b,
        location_type: data.location_type
      }]);

    if (error) {
      console.error('Error de Supabase:', error);
      return NextResponse.json({ error: error.message, details: error.details }, { status: 400 });
    }

    return NextResponse.json({ success: true });
  } catch (error: any) {
    console.error('Error en telemetría:', error);
    return NextResponse.json({ error: error.message }, { status: 500 });
  }
}
