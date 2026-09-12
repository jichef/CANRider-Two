'use client';

import {
  Battery,
  BatteryCharging,
  MapPin,
  Navigation,
  Zap,
  Clock,
  Route as RouteIcon,
  ShieldCheck,
  Activity,
  Thermometer,
  Signal,
  SignalHigh,
  SignalMedium,
  SignalLow,
  SignalZero,
  Wifi,
  WifiHigh,
  WifiLow,
  WifiZero,
  Trash2,
} from 'lucide-react';
import dynamic from 'next/dynamic';
import { useCallback, useEffect, useRef, useState } from 'react';
import { createClient } from '@/lib/supabase';

// Algunos campos CAN (moto_battery, moto_battery_b, bms_charging...) llegan
// null en una fila cuando esa lectura concreta no se completó a tiempo del
// envío — no significa que el dato ya no exista, así que en vez de pisar el
// estado con ese null, se conserva el último valor no-null que se vio.
function mergeTelemetry(prev: any, next: any) {
  if (!next) return prev;
  if (!prev) {
    return {
      ...next,
      _positionAt: (next.latitude != null && next.longitude != null) ? next.timestamp : undefined,
    };
  }
  const merged: any = { ...next };
  for (const key of Object.keys(next)) {
    if (next[key] === null || next[key] === undefined) {
      if (prev[key] !== undefined) merged[key] = prev[key];
    }
  }
  // La posición se mantiene igual que el resto de campos (último valor
  // conocido), pero además se guarda CUÁNDO fue esa última lectura real —
  // telemetry.timestamp por sí solo no sirve para esto, porque puede ser
  // más reciente que la posición si esa fila concreta trajo otros datos
  // pero no GPS.
  merged._positionAt = (next.latitude != null && next.longitude != null)
    ? next.timestamp
    : prev._positionAt;
  return merged;
}

// Icono de intensidad de señal, graduado según dBm en vez de un simple
// on/off. signal_strength viene siempre de AT+CSQ en el módem celular
// (main.ino, readSignalStrength(): -113 a -51 dBm) — el firmware lo manda
// así incluso cuando el envío se hizo por WiFi (todavía no hay lectura de
// RSSI de WiFi guardada en Supabase), así que por ahora las barras de WiFi
// reflejan la cobertura celular del módem, no la del propio WiFi.
function SignalIcon({ connectionType, dbm, size = 12 }: { connectionType?: string; dbm?: number | null; size?: number }) {
  if (connectionType === 'wifi') {
    if (dbm == null) return <WifiZero size={size} />;
    if (dbm >= -60) return <Wifi size={size} />;
    if (dbm >= -75) return <WifiHigh size={size} />;
    if (dbm >= -90) return <WifiLow size={size} />;
    return <WifiZero size={size} />;
  }
  if (dbm == null) return <SignalZero size={size} />;
  if (dbm >= -65) return <Signal size={size} />;
  if (dbm >= -75) return <SignalHigh size={size} />;
  if (dbm >= -85) return <SignalMedium size={size} />;
  if (dbm >= -95) return <SignalLow size={size} />;
  return <SignalZero size={size} />;
}

function timeAgo(isoString?: string | null) {
  if (!isoString) return null;
  const diffMs = Date.now() - new Date(isoString).getTime();
  const mins = Math.floor(diffMs / 60000);
  if (mins < 1) return 'ahora mismo';
  if (mins < 60) return `hace ${mins} min`;
  const hours = Math.floor(mins / 60);
  if (hours < 24) return `hace ${hours}h ${mins % 60}min`;
  return `hace ${Math.floor(hours / 24)}d`;
}

const Map = dynamic(() => import('@/components/Map'), {
  ssr: false,
  loading: () => (
    <div className="h-full w-full bg-zinc-900 animate-pulse flex items-center justify-center min-h-[400px]">
      <span className="text-zinc-500 font-mono text-[10px] tracking-widest">INITIALIZING_GPS...</span>
    </div>
  )
});

export default function DashboardContent() {
  // OJO: createClient() en sí es barato pero devuelve una instancia NUEVA
  // cada vez que se llama — si se llamara directo en el cuerpo del
  // componente, cada re-render (cada telemetría nueva, cada 15s) generaría
  // un "supabase" distinto, y todo efecto con [supabase] en dependencias
  // se desmontaría y volvería a montar en cada render: reconexión constante
  // del canal realtime y refetch constante del historial de viajes. useState
  // con inicializador perezoso crea el cliente una sola vez por instancia
  // del componente y mantiene la misma referencia mientras esté montado.
  const [supabase] = useState(() => createClient());
  const [telemetry, setTelemetry] = useState<any>(null);
  const [trips, setTrips] = useState<any[]>([]);
  const TRIPS_PER_PAGE = 5;
  const [tripPage, setTripPage] = useState(0);
  const [tripCount, setTripCount] = useState(0);
  const [selectedTrip, setSelectedTrip] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [isConfigured] = useState(!!supabase);
  const [isStale, setIsStale] = useState(false);
  const [mobileTab, setMobileTab] = useState<'live' | 'trips' | 'map'>('live');

  const [currentPosition, setCurrentPosition] = useState<[number, number] | null>(null);
  const [hasLiveFix, setHasLiveFix] = useState(false);
  const [address, setAddress] = useState<string | null>(null);

  useEffect(() => {
    const checkStale = () => {
      if (telemetry?.timestamp) {
        const diff = Date.now() - new Date(telemetry.timestamp).getTime();
        setIsStale(diff > 120000);
      } else {
        setIsStale(true);
      }
    };
    const interval = setInterval(checkStale, 30000);
    checkStale();
    return () => clearInterval(interval);
  }, [telemetry]);

  useEffect(() => {
    if (!supabase) {
      setLoading(false);
      return;
    }

    const fetchData = async () => {
      // Se piden las últimas 50 filas (no solo la más reciente) y se
      // rellena cada campo con el último valor no-nulo visto entre ellas.
      // Si se carga la página con el CAN ya en silencio desde hace rato,
      // la fila más reciente sola vendría con todo a null — así el primer
      // render ya arranca con el último estado conocido de verdad, no con
      // ---. La suscripción realtime de abajo sigue usando mergeTelemetry
      // sobre este estado ya poblado para las actualizaciones siguientes.
      const { data: telRows } = await supabase
        .from('telemetry')
        .select('*')
        .order('timestamp', { ascending: false })
        .limit(50);

      if (telRows && telRows.length > 0) {
        const telData = telRows.reduce((acc: any, row: any) => mergeTelemetry(row, acc));

        // Las últimas 50 filas (~12 min) pueden venir todas sin posición si
        // el GPS/LBS lleva más tiempo sin fix (túnel, garaje, sin cobertura).
        // En vez de no mostrar nada (o peor, un centro por defecto inventado),
        // se busca la última fila de SIEMPRE que sí tuviera coordenadas — la
        // moto debe mostrar siempre su última posición real conocida, por
        // vieja que sea, igual que ya se hace con la batería.
        let posLat = telData.latitude;
        let posLon = telData.longitude;
        let posSource = telData.position_source;
        let posAt = (posLat != null && posLon != null) ? telData.timestamp : undefined;

        if (posLat == null || posLon == null) {
          const { data: lastPosRows } = await supabase
            .from('telemetry')
            .select('latitude,longitude,position_source,timestamp')
            .not('latitude', 'is', null)
            .order('timestamp', { ascending: false })
            .limit(1);
          const fb = lastPosRows?.[0];
          if (fb) {
            posLat = fb.latitude;
            posLon = fb.longitude;
            posSource = fb.position_source;
            posAt = fb.timestamp;
          }
        }

        // Mismo caso que la posición: si moto_battery/moto_battery_b llevan
        // más de la ventana de 50 filas sin un valor real (señal CAN caída
        // un buen rato), se busca el último dato real conocido de cada una
        // en vez de dejarlas en null — BATERÍA A/B no deben volver a "---"
        // mientras exista algún valor real en el historial.
        let batA = telData.moto_battery;
        let batB = telData.moto_battery_b;
        if (batA == null) {
          const { data: rows } = await supabase
            .from('telemetry')
            .select('moto_battery')
            .not('moto_battery', 'is', null)
            .order('timestamp', { ascending: false })
            .limit(1);
          if (rows?.[0]) batA = rows[0].moto_battery;
        }
        if (batB == null) {
          const { data: rows } = await supabase
            .from('telemetry')
            .select('moto_battery_b')
            .not('moto_battery_b', 'is', null)
            .order('timestamp', { ascending: false })
            .limit(1);
          if (rows?.[0]) batB = rows[0].moto_battery_b;
        }

        setTelemetry((prev: any) => {
          const merged = mergeTelemetry(prev, telData);
          if (posLat != null && posLon != null) {
            merged.latitude = posLat;
            merged.longitude = posLon;
            merged.position_source = posSource;
            merged._positionAt = posAt;
          }
          if (batA != null) merged.moto_battery = batA;
          if (batB != null) merged.moto_battery_b = batB;
          return merged;
        });
        if (posLat != null && posLon != null) {
          setCurrentPosition([posLat, posLon]);
          setHasLiveFix(true);
        }
      }

      setLoading(false);
    };

    fetchData();

    const channel = supabase
      .channel('realtime_telemetry')
      .on(
        'postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'telemetry' },
        (payload) => {
          const newData = payload.new;
          setTelemetry((prev: any) => mergeTelemetry(prev, newData));
          if (newData.latitude && newData.longitude) {
            setCurrentPosition([newData.latitude, newData.longitude]);
            setHasLiveFix(true);
          }
        }
      )
      .subscribe();

    return () => { supabase.removeChannel(channel); };
  }, [supabase]);

  // Historial de viajes paginado: count exacto (para saber cuántas páginas
  // hay) + range() para traer solo los 5 de la página pedida, no toda la
  // tabla. Se saca del efecto de telemetría de arriba porque tiene su
  // propio "cuándo recargar" (cambio de página, o al borrar un viaje).
  const fetchTrips = useCallback(async (page: number) => {
    if (!supabase) return;
    const from = page * TRIPS_PER_PAGE;
    const to = from + TRIPS_PER_PAGE - 1;
    const { data, count } = await supabase
      .from('trips')
      .select('*', { count: 'exact' })
      .order('start_time', { ascending: false })
      .range(from, to);
    setTrips(data ?? []);
    setTripCount(count ?? 0);
  }, [supabase]);

  useEffect(() => {
    fetchTrips(tripPage);
  }, [fetchTrips, tripPage]);

  // Municipio/calle aproximados a partir de las coordenadas (Nominatim,
  // OpenStreetMap — gratis, sin API key). Solo se repite la consulta si la
  // moto se ha movido más de ~80m o han pasado más de 60s desde la última
  // vez, para no machacar el servicio con cada tick de telemetría.
  const lastGeocodedRef = useRef<{ lat: number; lon: number; at: number } | null>(null);
  useEffect(() => {
    if (!hasLiveFix || !currentPosition) return;
    const [lat, lon] = currentPosition;
    const prev = lastGeocodedRef.current;
    if (prev) {
      const dLat = (lat - prev.lat) * 111_320;
      const dLon = (lon - prev.lon) * 111_320 * Math.cos((lat * Math.PI) / 180);
      const movedMeters = Math.sqrt(dLat * dLat + dLon * dLon);
      if (movedMeters < 80 && Date.now() - prev.at < 60_000) return;
    }

    let cancelled = false;
    const timer = setTimeout(async () => {
      try {
        const res = await fetch(
          `https://nominatim.openstreetmap.org/reverse?format=jsonv2&lat=${lat}&lon=${lon}&zoom=18&addressdetails=1`
        );
        if (!res.ok || cancelled) return;
        const data = await res.json();
        const a = data?.address ?? {};
        const place = a.city || a.town || a.village || a.municipality || a.county;
        const road = a.road;
        const num = a.house_number;
        const parts = [road ? `${road}${num ? ' ' + num : ''}` : null, place].filter(Boolean);
        if (!cancelled) setAddress(parts.length ? parts.join(', ') : null);
      } catch {
        // Sin conexión al servicio de geocodificación: se queda con las coordenadas
      }
      lastGeocodedRef.current = { lat, lon, at: Date.now() };
    }, 400);

    return () => { cancelled = true; clearTimeout(timer); };
  }, [currentPosition, hasLiveFix]);

  // SoC por pack: la moto tiene dos IDs de batería (modo A / modo B, uno
  // activo cada vez según qué pack esté conectado) — se muestran por
  // separado para saber cuál está puesto sin ambigüedad.
  const socA = telemetry?.moto_battery;
  const socB = telemetry?.moto_battery_b;
  // Carga: BMS CAN tiene prioridad sobre AT+CBC
  const isCharging = !!(telemetry?.bms_charging || telemetry?.is_charging);
  // Temperatura media de celdas disponibles
  const temps = [telemetry?.temp1, telemetry?.temp2, telemetry?.temp3, telemetry?.temp4]
    .filter((v): v is number => v != null);
  const avgTemp = temps.length > 0 ? temps.reduce((a, b) => a + b, 0) / temps.length : undefined;

  // Traza del viaje seleccionado (solo existe en viajes guardados con el
  // firmware que ya registra puntos — los viajes antiguos no tienen
  // "track" y el mapa cae a mostrar solo un aviso). Los dos últimos campos
  // (segundos desde el inicio, batería) son más recientes que "track" en
  // sí — viajes guardados justo tras añadirlo pueden no tenerlos, y el
  // mapa se queda sin waypoints intermedios para esos en vez de romper.
  const selectedTripData = trips.find((t) => t.id === selectedTrip);
  const track: [number, number, number, number?, number?][] | undefined = selectedTripData?.track;
  const trackSpeeds = track?.map(([, , v]) => v) ?? [];
  const avgSpeed = trackSpeeds.length > 0
    ? trackSpeeds.reduce((a, b) => a + b, 0) / trackSpeeds.length
    : null;
  const movingSpeeds = trackSpeeds.filter((v) => v > 2); // excluye paradas/ruido en reposo
  const minSpeed = movingSpeeds.length > 0 ? Math.min(...movingSpeeds) : null;

  const hasCAN = telemetry?.pack_voltage != null
    || telemetry?.battery_current != null
    || avgTemp != null
    || telemetry?.charge_current != null
    || telemetry?.cell_voltage != null;

  const stats = [
    {
      label: 'BATERÍA A',
      value: socA != null ? `${Math.round(socA)}` : '---',
      unit: socA != null ? '%' : '',
      pct: socA != null ? Math.max(0, Math.min(100, socA)) : null,
      icon: Battery,
      color: 'text-emerald-400',
      glow: 'shadow-[0_0_15px_rgba(52,211,153,0.3)]',
      border: 'border-emerald-500/20'
    },
    {
      label: 'BATERÍA B',
      value: socB != null ? `${Math.round(socB)}` : '---',
      unit: socB != null ? '%' : '',
      pct: socB != null ? Math.max(0, Math.min(100, socB)) : null,
      icon: Battery,
      color: 'text-teal-400',
      glow: 'shadow-[0_0_15px_rgba(45,212,191,0.3)]',
      border: 'border-teal-500/20'
    },
    {
      label: 'VELOCIDAD',
      value: telemetry?.speed != null ? Math.round(telemetry.speed) : '---',
      unit: telemetry?.speed != null ? 'km/h' : '',
      pct: null,
      icon: Navigation,
      color: 'text-cyan-400',
      glow: 'shadow-[0_0_15px_rgba(34,211,238,0.3)]',
      border: 'border-cyan-500/20'
    },
    {
      label: 'TENSIÓN',
      value: telemetry?.pack_voltage != null ? telemetry.pack_voltage.toFixed(1) : '---',
      unit: telemetry?.pack_voltage != null ? 'V' : '',
      pct: null,
      icon: Zap,
      color: 'text-amber-400',
      glow: 'shadow-[0_0_15px_rgba(251,191,36,0.3)]',
      border: 'border-amber-500/20'
    },
    {
      label: 'SISTEMA',
      value: telemetry ? (isCharging ? 'CHARGING' : 'READY') : '---',
      unit: '',
      pct: null,
      icon: ShieldCheck,
      color: isCharging ? 'text-amber-400' : (telemetry ? 'text-indigo-400' : 'text-zinc-600'),
      glow: isCharging ? 'shadow-[0_0_15px_rgba(251,191,36,0.3)]' : 'shadow-[0_0_15px_rgba(129,140,248,0.3)]',
      border: 'border-indigo-500/20'
    },
    {
      label: 'SEÑAL',
      value: telemetry?.signal_strength != null ? String(telemetry.signal_strength) : '---',
      unit: telemetry?.signal_strength != null ? 'dBm' : '',
      pct: null,
      icon: Signal,
      color: telemetry?.signal_strength != null && telemetry.signal_strength > -85
        ? 'text-fuchsia-400'
        : 'text-zinc-600',
      glow: 'shadow-[0_0_15px_rgba(232,121,249,0.3)]',
      border: 'border-fuchsia-500/20'
    },
  ];

  const canStats = [
    {
      label: 'CORRIENTE',
      value: telemetry?.battery_current != null
        ? `${telemetry.battery_current > 0 ? '+' : ''}${telemetry.battery_current.toFixed(1)}`
        : null,
      unit: 'A',
      icon: Activity,
      color: (telemetry?.battery_current ?? 0) >= 0 ? 'text-emerald-400' : 'text-red-400',
      glow: 'shadow-[0_0_15px_rgba(52,211,153,0.2)]',
      border: 'border-emerald-500/20',
    },
    {
      label: 'TEMP CELDAS',
      value: avgTemp != null ? avgTemp.toFixed(0) : null,
      unit: '°C',
      icon: Thermometer,
      color: avgTemp != null && avgTemp > 45 ? 'text-red-400' : 'text-sky-400',
      glow: 'shadow-[0_0_15px_rgba(56,189,248,0.2)]',
      border: 'border-sky-500/20',
    },
    {
      label: 'I CARGA',
      value: telemetry?.charge_current != null ? telemetry.charge_current.toFixed(1) : null,
      unit: 'A',
      icon: Zap,
      color: 'text-violet-400',
      glow: 'shadow-[0_0_15px_rgba(167,139,250,0.2)]',
      border: 'border-violet-500/20',
    },
    {
      label: 'V CELDA',
      value: telemetry?.cell_voltage != null ? telemetry.cell_voltage.toFixed(3) : null,
      unit: 'V',
      icon: Battery,
      color: 'text-fuchsia-400',
      glow: 'shadow-[0_0_15px_rgba(232,121,249,0.2)]',
      border: 'border-fuchsia-500/20',
    },
  ];

  const StatCard = ({ stat }: { stat: (typeof stats)[0] }) => (
    <div className={`group bg-zinc-900/40 backdrop-blur-xl border ${stat.border} ${stat.glow} p-3.5 md:p-6 rounded-2xl md:rounded-3xl transition-all hover:scale-[1.02] hover:bg-zinc-900/60`}>
      <div className="flex items-center justify-between mb-2.5 md:mb-6">
        <div className={`p-1.5 md:p-3 rounded-lg md:rounded-2xl bg-zinc-950/50 ${stat.color} border border-white/5`}>
          <stat.icon className="w-4 h-4 md:w-[22px] md:h-[22px]" />
        </div>
        {stat.pct != null && (
          <div className="h-1 w-8 md:w-12 bg-zinc-800 rounded-full overflow-hidden">
            <div className={`h-full bg-current ${stat.color} opacity-70`} style={{ width: `${stat.pct}%` }} />
          </div>
        )}
      </div>
      <p className="text-[9px] md:text-[10px] font-black tracking-[0.15em] md:tracking-[0.2em] text-zinc-500 mb-0.5 md:mb-1 uppercase truncate">{stat.label}</p>
      <div className="flex items-baseline gap-1">
        <h3 className="text-xl md:text-3xl font-black text-white font-mono">{stat.value}</h3>
        {stat.unit && <span className="text-[10px] md:text-xs font-bold text-zinc-600">{stat.unit}</span>}
      </div>
    </div>
  );

  void loading; // usado implícitamente via isConfigured + telemetry===null

  const deleteTrip = async (tripId: string) => {
    if (!supabase) return;
    if (!window.confirm('¿Eliminar este viaje? No se puede deshacer.')) return;
    const { error } = await supabase.from('trips').delete().eq('id', tripId);
    if (error) {
      alert('No se pudo eliminar el viaje: ' + error.message);
      return;
    }
    if (selectedTrip === tripId) setSelectedTrip(null);
    // Si era el único viaje de esta página (y no es la primera), retrocede
    // una página; si no, recarga la página actual para que el siguiente
    // viaje de la lista suba a rellenar el hueco.
    if (trips.length === 1 && tripPage > 0) {
      setTripPage((p) => p - 1);
    } else {
      fetchTrips(tripPage);
    }
  };

  return (
    <div className="min-h-[100dvh] md:h-[100dvh] md:overflow-y-auto bg-black text-zinc-300 font-sans selection:bg-cyan-500/30 pb-24 md:pb-0">
      <div className="fixed inset-0 bg-[radial-gradient(circle_at_50%_-20%,_#1e1b4b_0%,_#000_80%)] pointer-events-none" />

      <div className="relative max-w-7xl mx-auto p-4 md:p-6 md:h-full md:flex md:flex-col">
        {!isConfigured && (
          <div className="mb-8 p-4 bg-amber-500/10 border border-amber-500/20 rounded-2xl flex items-center gap-4 animate-pulse md:shrink-0">
            <div className="p-2 bg-amber-500/20 rounded-lg text-amber-500">
              <ShieldCheck size={20} />
            </div>
            <div>
              <p className="text-xs font-black tracking-widest text-amber-500 uppercase">System Alert: Database Offline</p>
              <p className="text-[10px] text-amber-500/70 uppercase">Faltan las credenciales de Supabase en .env.local</p>
            </div>
          </div>
        )}

        <header className="flex flex-wrap items-center gap-x-2.5 gap-y-1.5 mb-3 md:mb-6 md:shrink-0">
          <h1 className="text-lg md:text-2xl font-black text-white tracking-tight mr-0.5">CanRider</h1>

          <div className={`inline-flex items-center gap-1.5 px-2.5 py-1.5 md:px-3 md:py-1.5 rounded-xl border transition-all ${
            isConfigured && telemetry && !isStale
              ? 'bg-emerald-500/10 text-emerald-400 border-emerald-500/20'
              : 'bg-red-500/10 text-red-400 border-red-500/20'
          }`}>
            <div className={`w-2 h-2 rounded-full ${
              isConfigured && telemetry && !isStale ? 'bg-emerald-500 animate-ping' : 'bg-red-500'
            }`} />
            <span className="text-[10px] md:text-xs font-bold uppercase tracking-wider">
              {isConfigured && telemetry && !isStale ? 'Online' : 'Offline'}
            </span>
          </div>

          {/* Por qué camino se mandó la última telemetría — ver connection_type en main.ino */}
          {telemetry?.connection_type && (
            <div className={`inline-flex items-center gap-1.5 px-2.5 py-1.5 md:px-3 md:py-1.5 rounded-xl border transition-all ${
              telemetry.connection_type === 'wifi'
                ? 'bg-sky-500/10 text-sky-400 border-sky-500/20'
                : 'bg-violet-500/10 text-violet-400 border-violet-500/20'
            }`}>
              <SignalIcon connectionType={telemetry.connection_type} dbm={telemetry.signal_strength} />
              <span className="text-[10px] md:text-xs font-bold uppercase tracking-wider">
                {telemetry.connection_type === 'wifi' ? 'WiFi' : 'LTE'}
              </span>
            </div>
          )}

          {/* Batería interna ESP32 (AT+CBC) */}
          {telemetry?.battery_level != null && (
            <div className={`flex items-center gap-1.5 px-2.5 py-1.5 md:px-3 md:py-1.5 rounded-xl border transition-all ${
              telemetry.is_charging
                ? 'text-amber-400 border-amber-500/20 bg-amber-500/10'
                : (telemetry.battery_level < 20
                    ? 'text-red-400 border-red-500/20 bg-red-500/10'
                    : 'text-zinc-400 border-white/10')
            }`}>
              {telemetry.is_charging
                ? <BatteryCharging size={14} />
                : <Battery size={14} />}
              <span className="text-xs font-bold font-mono">{telemetry.battery_level}%</span>
              {telemetry.battery_voltage != null && (
                <span className="text-[10px] font-mono text-zinc-500">{telemetry.battery_voltage.toFixed(2)}V</span>
              )}
            </div>
          )}

          {telemetry?.timestamp && (
            <div className="flex items-center gap-1.5 text-zinc-500 font-mono text-[10px] ml-auto">
              <Clock size={11} />
              <span>{new Date(telemetry.timestamp).toLocaleTimeString('es-ES')}</span>
            </div>
          )}
        </header>

        {/* Pestaña LIVE (móvil: solo esta sección; escritorio: siempre visible) */}
        <div className={`md:shrink-0 ${mobileTab === 'live' ? '' : 'hidden md:block'}`}>
          {/* Stats principales */}
          <div className="grid grid-cols-2 lg:grid-cols-6 gap-2.5 md:gap-4 mb-3 md:mb-4">
            {stats.map((stat) => <StatCard key={stat.label} stat={stat} />)}
          </div>

          {/* Stats CAN — solo visibles cuando el bus ha enviado datos */}
          {hasCAN && (
            <div className="grid grid-cols-2 lg:grid-cols-4 gap-2.5 md:gap-3 mb-6 md:mb-4">
              {canStats.map((stat) => stat.value != null && (
                <div key={stat.label} className={`bg-zinc-900/30 backdrop-blur-xl border ${stat.border} ${stat.glow} px-3.5 py-3 md:px-4 md:py-2.5 rounded-xl md:rounded-2xl transition-all hover:bg-zinc-900/50`}>
                  <div className="flex items-center gap-2 mb-1 md:mb-1">
                    <stat.icon size={14} className={stat.color} />
                    <p className="text-[9px] font-black tracking-[0.2em] text-zinc-500 uppercase truncate">{stat.label}</p>
                  </div>
                  <div className="flex items-baseline gap-1">
                    <span className={`text-lg md:text-xl font-black font-mono ${stat.color}`}>{stat.value}</span>
                    <span className="text-[10px] font-bold text-zinc-600">{stat.unit}</span>
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>

        <div className="grid lg:grid-cols-3 gap-8 md:flex-1 md:min-h-0">
          {/* Mapa — pestaña MAP en móvil */}
          <div className={`lg:col-span-2 group bg-zinc-900/40 backdrop-blur-xl border border-white/10 rounded-3xl overflow-hidden shadow-2xl flex-col transition-all hover:border-white/20 h-[calc(100dvh-220px)] md:h-full ${mobileTab === 'map' ? 'flex' : 'hidden md:flex'}`}>
            <div className="p-5 border-b border-white/5 flex items-center justify-between bg-zinc-950/20">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-red-500/10 text-red-500">
                  <MapPin size={18} />
                </div>
                <div>
                  <div className="flex items-center gap-2">
                    <span className="text-sm font-bold text-white block">
                      {selectedTrip ? 'ROUTE_ANALYSIS' : 'LIVE_LOCATION'}
                    </span>
                    {!selectedTrip && hasLiveFix && (
                      telemetry?.position_source === 'lbs' ? (
                        <span
                          className="px-1.5 py-0.5 rounded text-[9px] font-bold tracking-wider bg-amber-500/10 text-amber-400 border border-amber-500/20"
                          title="Posición aproximada por triangulación de celda — sin fix GPS"
                        >
                          LBS · APROX.
                        </span>
                      ) : (
                        <span className="px-1.5 py-0.5 rounded text-[9px] font-bold tracking-wider bg-emerald-500/10 text-emerald-400 border border-emerald-500/20">
                          GPS
                        </span>
                      )
                    )}
                  </div>
                  {selectedTrip ? (
                    trackSpeeds.length > 0 ? (
                      <div className="flex items-center gap-3 text-[10px] font-mono">
                        <span className="text-red-400">MAX {Math.round(selectedTripData?.max_speed ?? 0)} km/h</span>
                        <span className="text-amber-400">MEDIA {Math.round(avgSpeed ?? 0)} km/h</span>
                        <span className="text-cyan-400">MIN {Math.round(minSpeed ?? 0)} km/h</span>
                      </div>
                    ) : (
                      <span className="text-[10px] text-zinc-500 font-mono uppercase">Sin traza guardada para este viaje</span>
                    )
                  ) : hasLiveFix && currentPosition ? (
                    <div className="flex flex-wrap items-baseline gap-x-2">
                      {address && (
                        <span className="text-[10px] text-zinc-500 font-mono uppercase">{address} (aprox.)</span>
                      )}
                      <span className="text-[10px] text-zinc-600 font-mono">
                        {Math.abs(currentPosition[0]).toFixed(4)}° {currentPosition[0] >= 0 ? 'N' : 'S'}, {Math.abs(currentPosition[1]).toFixed(4)}° {currentPosition[1] >= 0 ? 'E' : 'W'}
                      </span>
                      {timeAgo(telemetry?._positionAt) && (
                        <span className="text-[10px] text-zinc-600 font-mono">· {timeAgo(telemetry?._positionAt)}</span>
                      )}
                      <a
                        href={`https://www.google.com/maps?q=${currentPosition[0]},${currentPosition[1]}`}
                        target="_blank"
                        rel="noopener noreferrer"
                        className="text-[10px] font-mono text-cyan-500 hover:text-cyan-400 underline underline-offset-2"
                      >
                        Google Maps ↗
                      </a>
                    </div>
                  ) : (
                    <span className="text-[10px] text-zinc-500 font-mono uppercase">Esperando posición GPS...</span>
                  )}
                </div>
              </div>
              {selectedTrip && (
                <button
                  onClick={() => setSelectedTrip(null)}
                  className="px-3 py-1 rounded-lg bg-zinc-800 text-[10px] font-bold text-zinc-400 hover:text-white transition-colors"
                >
                  RESET_VIEW
                </button>
              )}
            </div>
            <div className="flex-1 relative md:min-h-[300px]">
              {currentPosition ? (
                <Map
                  center={currentPosition}
                  track={selectedTrip ? track : undefined}
                  tripStartTime={selectedTrip ? selectedTripData?.start_time : undefined}
                />
              ) : (
                <div className="h-full w-full bg-zinc-900 flex items-center justify-center">
                  <span className="text-zinc-500 font-mono text-[10px] tracking-widest">ESPERANDO_POSICIÓN...</span>
                </div>
              )}
              <div className="absolute inset-0 pointer-events-none bg-[linear-gradient(rgba(18,16,16,0)_50%,rgba(0,0,0,0.1)_50%),linear-gradient(90deg,rgba(255,0,0,0.03),rgba(0,255,0,0.01),rgba(0,0,255,0.03))] bg-[length:100%_2px,3px_100%] z-20 opacity-20" />
            </div>
          </div>

          {/* Historial de viajes — pestaña TRIPS en móvil */}
          <div className={`bg-zinc-900/40 backdrop-blur-xl border border-white/10 p-6 rounded-3xl shadow-2xl md:h-full md:flex md:flex-col md:overflow-hidden ${mobileTab === 'trips' ? 'block' : 'hidden md:block'}`}>
            <div className="flex items-center justify-between mb-8 md:shrink-0">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-indigo-500/10 text-indigo-500">
                  <RouteIcon size={18} />
                </div>
                <h2 className="text-lg font-bold text-white">HISTORIAL</h2>
              </div>
            </div>

            <div className="space-y-4 md:flex-1 md:min-h-0 md:overflow-y-auto">
              {trips.length > 0 ? (
                trips.map((trip) => {
                  const startDate = new Date(trip.start_time);
                  const date = startDate
                    .toLocaleDateString('es-ES', { day: '2-digit', month: 'short' })
                    .toUpperCase();
                  const startTime = startDate.toLocaleTimeString('es-ES', { hour: '2-digit', minute: '2-digit' });
                  const isSelected = selectedTrip === trip.id;
                  const batteryUsed = trip.consumption ?? (
                    trip.start_battery_level != null && trip.end_battery_level != null
                      ? trip.start_battery_level - trip.end_battery_level
                      : null
                  );

                  return (
                    <div key={trip.id} className="relative group">
                      <button
                        onClick={() => {
                          const next = isSelected ? null : trip.id;
                          setSelectedTrip(next);
                          if (next) setMobileTab('map');
                        }}
                        className={`w-full flex items-center justify-between p-4 pr-12 rounded-2xl border transition-all duration-300 ${
                          isSelected
                            ? 'bg-cyan-500/20 border-cyan-500 shadow-[0_0_20px_rgba(6,182,212,0.2)]'
                            : 'bg-zinc-950/40 border-white/5 hover:border-white/20 hover:bg-zinc-950'
                        }`}
                      >
                        <div className="space-y-1 text-left">
                          <span className={`text-[10px] font-black tracking-wider uppercase transition-colors ${
                            isSelected ? 'text-cyan-400' : 'text-zinc-500'
                          }`}>
                            {date} · {startTime}
                          </span>
                          <div className="flex items-center gap-4">
                            <div className="flex items-center gap-1.5 text-xs font-bold text-white">
                              <Navigation size={12} className={isSelected ? 'text-cyan-400' : 'text-cyan-500'} />
                              {trip.distance} KM
                            </div>
                            <div className="flex items-center gap-1.5 text-xs font-bold text-zinc-400">
                              <Clock size={12} />
                              {trip.duration ?? trip.time ?? 'N/A'}
                            </div>
                            {batteryUsed != null && (
                              <div className="flex items-center gap-1.5 text-xs font-bold text-emerald-500/80">
                                <Battery size={12} />
                                -{batteryUsed}%
                              </div>
                            )}
                          </div>
                        </div>
                        <div className={`p-2 rounded-xl transition-all ${
                          isSelected ? 'bg-cyan-500 text-black scale-110' : 'bg-zinc-900 group-hover:bg-zinc-800 text-cyan-500'
                        }`}>
                          <Zap size={14} fill={isSelected ? 'currentColor' : 'none'} />
                        </div>
                      </button>
                      <button
                        onClick={(e) => { e.stopPropagation(); deleteTrip(trip.id); }}
                        title="Eliminar viaje"
                        className="absolute top-1/2 -translate-y-1/2 right-3 p-1.5 rounded-lg text-zinc-600 hover:text-red-400 hover:bg-red-500/10 transition-colors"
                      >
                        <Trash2 size={14} />
                      </button>
                    </div>
                  );
                })
              ) : (
                <div className="py-12 flex flex-col items-center justify-center text-zinc-600 border border-dashed border-white/5 rounded-3xl">
                  <RouteIcon size={32} className="mb-2 opacity-20" />
                  <p className="text-[10px] font-black tracking-widest uppercase">No data found</p>
                </div>
              )}
            </div>

            {tripCount > TRIPS_PER_PAGE && (
              <div className="flex items-center justify-between mt-4 pt-4 border-t border-white/5 md:shrink-0">
                <button
                  onClick={() => setTripPage((p) => Math.max(0, p - 1))}
                  disabled={tripPage === 0}
                  className="px-3 py-1 rounded-lg bg-zinc-800 text-[10px] font-bold text-zinc-400 hover:text-white transition-colors disabled:opacity-30 disabled:pointer-events-none"
                >
                  ← Anteriores
                </button>
                <span className="text-[10px] font-mono text-zinc-600">
                  {tripPage + 1} / {Math.max(1, Math.ceil(tripCount / TRIPS_PER_PAGE))}
                </span>
                <button
                  onClick={() => setTripPage((p) => p + 1)}
                  disabled={(tripPage + 1) * TRIPS_PER_PAGE >= tripCount}
                  className="px-3 py-1 rounded-lg bg-zinc-800 text-[10px] font-bold text-zinc-400 hover:text-white transition-colors disabled:opacity-30 disabled:pointer-events-none"
                >
                  Siguientes →
                </button>
              </div>
            )}

          </div>
        </div>
      </div>

      {/* Navegación inferior (móvil) — cambia qué sección se muestra arriba */}
      <nav className="fixed bottom-6 left-6 right-6 md:hidden z-50">
        <div className="bg-zinc-900/80 backdrop-blur-2xl border border-white/10 rounded-3xl p-2 flex items-center justify-around shadow-[0_20px_50px_rgba(0,0,0,0.5)]">
          {[
            { key: 'live' as const, icon: Activity, label: 'Live' },
            { key: 'trips' as const, icon: RouteIcon, label: 'Trips' },
            { key: 'map' as const, icon: MapPin, label: 'Map' },
          ].map((item) => (
            <button
              key={item.key}
              type="button"
              onClick={() => setMobileTab(item.key)}
              className={`flex flex-col items-center gap-1 p-3 rounded-2xl transition-all ${mobileTab === item.key ? 'bg-cyan-500/20 text-cyan-400' : 'text-zinc-500'}`}
            >
              <item.icon size={20} />
              <span className="text-[9px] font-black uppercase tracking-widest">{item.label}</span>
            </button>
          ))}
        </div>
      </nav>
    </div>
  );
}
