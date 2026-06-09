import SignalsConfig from '@/components/SignalsConfig';
import { getSignals, CANSignalRow } from '@/app/actions/signals';

export const dynamic = 'force-dynamic';

export default async function SignalsPage() {
  const vehicleId = process.env.NEXT_PUBLIC_VEHICLE_ID || undefined;

  let initialSignals: CANSignalRow[] = [];
  if (vehicleId) {
    try {
      initialSignals = await getSignals(vehicleId);
    } catch {
      // Si falta SUPABASE_SERVICE_ROLE_KEY o hay error de red, mostrar vacío
    }
  }

  return <SignalsConfig vehicleId={vehicleId} initialSignals={initialSignals} />;
}
