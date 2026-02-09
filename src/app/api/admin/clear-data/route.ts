import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';

const supabaseAdmin = createClient(
  process.env.NEXT_PUBLIC_SUPABASE_URL!,
  process.env.SUPABASE_SERVICE_ROLE_KEY!
);

export async function POST(req: NextRequest) {
  try {
    // Verificación básica de seguridad (podrías añadir una API key o Auth de admin)
    // Para simplificar, asumiremos que solo se llama desde el dashboard configurado
    
    // Borrar telemetría
    const { error: telemetryError } = await supabaseAdmin
      .from('telemetry')
      .delete()
      .neq('id', 0); // Truco para borrar todo sin afectar RLS si existiera

    if (telemetryError) throw telemetryError;

    // Borrar viajes
    const { error: tripsError } = await supabaseAdmin
      .from('trips')
      .delete()
      .neq('id', '00000000-0000-0000-0000-000000000000');

    if (tripsError) throw tripsError;

    return NextResponse.json({ success: true, message: 'Datos eliminados correctamente' });
  } catch (error: any) {
    console.error('Error al borrar datos:', error);
    return NextResponse.json({ error: error.message }, { status: 500 });
  }
}
