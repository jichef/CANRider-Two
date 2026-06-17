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
  Cpu,
} from 'lucide-react';
import Link from 'next/link';
import dynamic from 'next/dynamic';
import { useEffect, useState } from 'react';
import { createClient } from '@/lib/supabase';

const Map = dynamic(() => import('@/components/Map'), {
  ssr: false,
  loading: () => (
    <div className="h-full w-full bg-zinc-900 animate-pulse flex items-center justify-center min-h-[400px]">
      <span className="text-zinc-500 font-mono text-[10px] tracking-widest">INITIALIZING_GPS...</span>
    </div>
  )
});

export default function DashboardContent() {
  const supabase = createClient();
  const [telemetry, setTelemetry] = useState<any>(null);
  const [trips, setTrips] = useState<any[]>([]);
  const [selectedTrip, setSelectedTrip] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [isConfigured] = useState(!!supabase);
  const [isStale, setIsStale] = useState(false);

  const [currentPosition, setCurrentPosition] = useState<[number, number]>([40.41678, -3.70379]);

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
      const { data: telData } = await supabase
        .from('telemetry')
        .select('*')
        .order('timestamp', { ascending: false })
        .limit(1)
        .single();

      if (telData) {
        setTelemetry(telData);
        if (telData.latitude && telData.longitude) {
          setCurrentPosition([telData.latitude, telData.longitude]);
        }
      }

      const { data: tripData } = await supabase
        .from('trips')
        .select('*')
        .order('start_time', { ascending: false })
        .limit(5);

      if (tripData) setTrips(tripData);
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
          setTelemetry(newData);
          if (newData.latitude && newData.longitude) {
            setCurrentPosition([newData.latitude, newData.longitude]);
          }
        }
      )
      .subscribe();

    return () => { supabase.removeChannel(channel); };
  }, [supabase]);

  // SoC: CAN tiene prioridad sobre AT+CBC
  const socValue = telemetry?.soc ?? telemetry?.battery_level;
  // Carga: BMS CAN tiene prioridad sobre AT+CBC
  const isCharging = !!(telemetry?.bms_charging || telemetry?.is_charging);
  // Temperatura media de celdas disponibles
  const temps = [telemetry?.temp1, telemetry?.temp2, telemetry?.temp3, telemetry?.temp4]
    .filter((v): v is number => v != null);
  const avgTemp = temps.length > 0 ? temps.reduce((a, b) => a + b, 0) / temps.length : undefined;

  const hasCAN = telemetry?.pack_voltage != null
    || telemetry?.battery_current != null
    || avgTemp != null
    || telemetry?.charge_current != null
    || telemetry?.cell_voltage != null;

  const stats = [
    {
      label: 'SOC',
      value: socValue != null ? `${Math.round(socValue)}` : '---',
      unit: socValue != null ? '%' : '',
      icon: Battery,
      color: 'text-emerald-400',
      glow: 'shadow-[0_0_15px_rgba(52,211,153,0.3)]',
      border: 'border-emerald-500/20'
    },
    {
      label: 'VELOCIDAD',
      value: telemetry?.speed != null ? Math.round(telemetry.speed) : '---',
      unit: telemetry?.speed != null ? 'km/h' : '',
      icon: Navigation,
      color: 'text-cyan-400',
      glow: 'shadow-[0_0_15px_rgba(34,211,238,0.3)]',
      border: 'border-cyan-500/20'
    },
    {
      label: 'TENSIÓN',
      value: telemetry?.pack_voltage != null ? telemetry.pack_voltage.toFixed(1) : '---',
      unit: telemetry?.pack_voltage != null ? 'V' : '',
      icon: Zap,
      color: 'text-amber-400',
      glow: 'shadow-[0_0_15px_rgba(251,191,36,0.3)]',
      border: 'border-amber-500/20'
    },
    {
      label: 'SISTEMA',
      value: telemetry ? (isCharging ? 'CHARGING' : 'READY') : '---',
      unit: '',
      icon: ShieldCheck,
      color: isCharging ? 'text-amber-400' : (telemetry ? 'text-indigo-400' : 'text-zinc-600'),
      glow: isCharging ? 'shadow-[0_0_15px_rgba(251,191,36,0.3)]' : 'shadow-[0_0_15px_rgba(129,140,248,0.3)]',
      border: 'border-indigo-500/20'
    },
    {
      label: 'SEÑAL',
      value: telemetry?.signal_strength != null ? String(telemetry.signal_strength) : '---',
      unit: telemetry?.signal_strength != null ? 'dBm' : '',
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
    <div className={`group bg-zinc-900/40 backdrop-blur-xl border ${stat.border} ${stat.glow} p-6 rounded-3xl transition-all hover:scale-[1.02] hover:bg-zinc-900/60`}>
      <div className="flex items-center justify-between mb-6">
        <div className={`p-3 rounded-2xl bg-zinc-950/50 ${stat.color} border border-white/5`}>
          <stat.icon size={22} />
        </div>
        <div className="h-1 w-12 bg-zinc-800 rounded-full overflow-hidden">
          <div className={`h-full bg-current ${stat.color} w-2/3 opacity-50`} />
        </div>
      </div>
      <p className="text-[10px] font-black tracking-[0.2em] text-zinc-500 mb-1 uppercase">{stat.label}</p>
      <div className="flex items-baseline gap-1">
        <h3 className="text-3xl font-black text-white font-mono">{stat.value}</h3>
        {stat.unit && <span className="text-xs font-bold text-zinc-600">{stat.unit}</span>}
      </div>
    </div>
  );

  void loading; // usado implícitamente via isConfigured + telemetry===null

  return (
    <div className="min-h-screen bg-black text-zinc-300 font-sans selection:bg-cyan-500/30 pb-24 md:pb-8">
      <div className="fixed inset-0 bg-[radial-gradient(circle_at_50%_-20%,_#1e1b4b_0%,_#000_80%)] pointer-events-none" />

      <div className="relative max-w-7xl mx-auto p-4 md:p-8">
        {!isConfigured && (
          <div className="mb-8 p-4 bg-amber-500/10 border border-amber-500/20 rounded-2xl flex items-center gap-4 animate-pulse">
            <div className="p-2 bg-amber-500/20 rounded-lg text-amber-500">
              <ShieldCheck size={20} />
            </div>
            <div>
              <p className="text-xs font-black tracking-widest text-amber-500 uppercase">System Alert: Database Offline</p>
              <p className="text-[10px] text-amber-500/70 uppercase">Faltan las credenciales de Supabase en .env.local</p>
            </div>
          </div>
        )}

        <header className="flex flex-col md:flex-row md:items-center justify-between mb-12 gap-6">
          <div className="flex items-center gap-2 text-cyan-500">
            <Activity size={18} className="animate-pulse" />
            <span className="text-xs font-black tracking-[0.2em] uppercase">Telemetry Link Active</span>
          </div>

          <div className="flex items-center gap-3">
            <Link
              href="/signals"
              className="p-2 rounded-xl bg-zinc-900 border border-white/10 text-zinc-400 hover:text-cyan-400 hover:border-cyan-500/30 transition-colors"
              title="Configurar señales CAN"
            >
              <Cpu size={18} />
            </Link>

          <div className="flex items-center gap-4 bg-zinc-900/50 backdrop-blur-md border border-white/10 p-1 rounded-2xl">
            <div className={`flex items-center gap-2 px-4 py-2 rounded-xl border transition-all ${
              isConfigured && telemetry && !isStale
                ? 'bg-emerald-500/10 text-emerald-400 border-emerald-500/20'
                : 'bg-red-500/10 text-red-400 border-red-500/20'
            }`}>
              <div className={`w-2 h-2 rounded-full ${
                isConfigured && telemetry && !isStale ? 'bg-emerald-500 animate-ping' : 'bg-red-500'
              }`} />
              <span className="text-xs font-bold uppercase tracking-wider">
                {isConfigured && telemetry && !isStale ? 'Online' : 'Offline'}
              </span>
            </div>

            {/* Batería interna ESP32 (AT+CBC) */}
            {telemetry?.battery_level != null && (
              <div className={`flex items-center gap-1.5 px-3 py-2 rounded-xl border transition-all ${
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

            <div className="pr-4 text-xs font-mono text-zinc-500">
              {telemetry?.motorcycle_id || 'ESP32_NODE_01'}
            </div>
          </div>
          </div>
        </header>

        {/* Stats principales */}
        <div className="grid grid-cols-2 lg:grid-cols-5 gap-6 mb-6">
          {stats.map((stat) => <StatCard key={stat.label} stat={stat} />)}
        </div>

        {/* Stats CAN — solo visibles cuando el bus ha enviado datos */}
        {hasCAN && (
          <div className="grid grid-cols-2 lg:grid-cols-4 gap-4 mb-10">
            {canStats.map((stat) => stat.value != null && (
              <div key={stat.label} className={`bg-zinc-900/30 backdrop-blur-xl border ${stat.border} ${stat.glow} px-5 py-4 rounded-2xl transition-all hover:bg-zinc-900/50`}>
                <div className="flex items-center gap-2 mb-2">
                  <stat.icon size={14} className={stat.color} />
                  <p className="text-[9px] font-black tracking-[0.2em] text-zinc-500 uppercase">{stat.label}</p>
                </div>
                <div className="flex items-baseline gap-1">
                  <span className={`text-xl font-black font-mono ${stat.color}`}>{stat.value}</span>
                  <span className="text-[10px] font-bold text-zinc-600">{stat.unit}</span>
                </div>
              </div>
            ))}
          </div>
        )}

        <div className="grid lg:grid-cols-3 gap-8">
          {/* Mapa */}
          <div className="lg:col-span-2 group bg-zinc-900/40 backdrop-blur-xl border border-white/10 rounded-3xl overflow-hidden shadow-2xl flex flex-col transition-all hover:border-white/20">
            <div className="p-5 border-b border-white/5 flex items-center justify-between bg-zinc-950/20">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-red-500/10 text-red-500">
                  <MapPin size={18} />
                </div>
                <div>
                  <span className="text-sm font-bold text-white block">
                    {selectedTrip ? 'ROUTE_ANALYSIS' : 'LIVE_LOCATION'}
                  </span>
                  <span className="text-[10px] text-zinc-500 font-mono uppercase">
                    {selectedTrip ? `Trip ID: ${selectedTrip.slice(0, 8)}` : 'Madrid, Spain • 40.4168° N, 3.7038° W'}
                  </span>
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
            <div className="flex-1 relative min-h-[450px]">
              <Map
                center={currentPosition}
              />
              <div className="absolute inset-0 pointer-events-none bg-[linear-gradient(rgba(18,16,16,0)_50%,rgba(0,0,0,0.1)_50%),linear-gradient(90deg,rgba(255,0,0,0.03),rgba(0,255,0,0.01),rgba(0,0,255,0.03))] bg-[length:100%_2px,3px_100%] z-20 opacity-20" />
            </div>
          </div>

          {/* Historial de viajes */}
          <div className="bg-zinc-900/40 backdrop-blur-xl border border-white/10 p-6 rounded-3xl shadow-2xl">
            <div className="flex items-center justify-between mb-8">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-indigo-500/10 text-indigo-500">
                  <RouteIcon size={18} />
                </div>
                <h2 className="text-lg font-bold text-white">HISTORIAL</h2>
              </div>
            </div>

            <div className="space-y-4">
              {trips.length > 0 ? (
                trips.map((trip) => {
                  const date = new Date(trip.start_time)
                    .toLocaleDateString('es-ES', { day: '2-digit', month: 'short' })
                    .toUpperCase();
                  const isSelected = selectedTrip === trip.id;
                  const batteryUsed = trip.consumption ?? (
                    trip.start_battery_level != null && trip.end_battery_level != null
                      ? trip.start_battery_level - trip.end_battery_level
                      : null
                  );

                  return (
                    <button
                      key={trip.id}
                      onClick={() => setSelectedTrip(isSelected ? null : trip.id)}
                      className={`w-full group flex items-center justify-between p-4 rounded-2xl border transition-all duration-300 ${
                        isSelected
                          ? 'bg-cyan-500/20 border-cyan-500 shadow-[0_0_20px_rgba(6,182,212,0.2)]'
                          : 'bg-zinc-950/40 border-white/5 hover:border-white/20 hover:bg-zinc-950'
                      }`}
                    >
                      <div className="space-y-1 text-left">
                        <span className={`text-[10px] font-black tracking-wider uppercase transition-colors ${
                          isSelected ? 'text-cyan-400' : 'text-zinc-500'
                        }`}>
                          {date}
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
                  );
                })
              ) : (
                <div className="py-12 flex flex-col items-center justify-center text-zinc-600 border border-dashed border-white/5 rounded-3xl">
                  <RouteIcon size={32} className="mb-2 opacity-20" />
                  <p className="text-[10px] font-black tracking-widest uppercase">No data found</p>
                </div>
              )}
            </div>

          </div>
        </div>
      </div>

      {/* Navegación inferior (móvil) */}
      <nav className="fixed bottom-6 left-6 right-6 md:hidden z-50">
        <div className="bg-zinc-900/80 backdrop-blur-2xl border border-white/10 rounded-3xl p-2 flex items-center justify-around shadow-[0_20px_50px_rgba(0,0,0,0.5)]">
          {[
            { icon: Activity, label: 'Live', href: '/' },
            { icon: RouteIcon, label: 'Trips', href: '/' },
            { icon: MapPin, label: 'Map', href: '/' },
            { icon: Cpu, label: 'CAN', href: '/signals' },
          ].map((item, i) => (
            <Link
              key={item.label}
              href={item.href}
              className={`flex flex-col items-center gap-1 p-3 rounded-2xl transition-all ${i === 0 ? 'bg-cyan-500/20 text-cyan-400' : 'text-zinc-500'}`}
            >
              <item.icon size={20} />
              <span className="text-[9px] font-black uppercase tracking-widest">{item.label}</span>
            </Link>
          ))}
        </div>
      </nav>
    </div>
  );
}
