'use client';

import { 
  Battery, 
  MapPin, 
  Navigation, 
  Zap, 
  Signal, 
  Clock, 
  Route as RouteIcon,
  ShieldCheck,
  Activity,
  Smartphone
} from 'lucide-react';
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
  const [isConfigured, setIsConfigured] = useState(true);
  const [isStale, setIsStale] = useState(false);

  // Madrid por defecto si no hay datos
  const [currentPosition, setCurrentPosition] = useState<[number, number]>([40.41678, -3.70379]);

  useEffect(() => {
    // Comprobar si los datos son antiguos (más de 2 minutos)
    const checkStale = () => {
      if (telemetry?.timestamp) {
        const diff = Date.now() - new Date(telemetry.timestamp).getTime();
        setIsStale(diff > 120000); // 2 minutos
      } else {
        setIsStale(true);
      }
    };

    const interval = setInterval(checkStale, 30000);
    checkStale();
    return () => clearInterval(interval);
  }, [telemetry]);

  useEffect(() => {
    // Si el cliente no se pudo crear, estamos en modo offline
    if (!supabase) {
      setIsConfigured(false);
      setLoading(false);
      return;
    }

    // 1. Cargar último dato y últimos viajes
    const fetchData = async () => {
      // Cargar telemetría
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

      // Cargar viajes
      const { data: tripData } = await supabase
        .from('trips')
        .select('*')
        .order('start_time', { ascending: false })
        .limit(5);

      if (tripData) {
        setTrips(tripData);
      }

      setLoading(false);
    };

    fetchData();

    // 2. Suscribirse a cambios en tiempo real
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

    return () => {
      if (supabase && channel) {
        supabase.removeChannel(channel);
      }
    };
  }, [supabase]);

  const stats = [
    { 
      label: 'BATERÍA', 
      value: telemetry?.battery_level !== undefined ? `${telemetry.battery_level}%` : '---', 
      percent: telemetry?.battery_level || 0,
      icon: Battery, 
      color: 'text-emerald-400', 
      glow: 'shadow-[0_0_15px_rgba(52,211,153,0.3)]',
      border: 'border-emerald-500/20'
    },
    { 
      label: 'VELOCIDAD', 
      value: telemetry?.speed !== undefined ? Math.round(telemetry.speed) : '---', 
      percent: Math.min((telemetry?.speed || 0) * 0.8, 100), // Max 120km/h aprox
      unit: telemetry?.speed !== undefined ? 'km/h' : '',
      icon: Navigation, 
      color: 'text-cyan-400', 
      glow: 'shadow-[0_0_15px_rgba(34,211,238,0.3)]',
      border: 'border-cyan-500/20'
    },
    { 
      label: 'ESTADO', 
      value: !isStale && telemetry ? 'ONLINE' : 'OFFLINE', 
      percent: !isStale && telemetry ? 100 : 0,
      icon: ShieldCheck, 
      color: !isStale && telemetry ? 'text-indigo-400' : 'text-zinc-600', 
      glow: !isStale && telemetry ? 'shadow-[0_0_15px_rgba(129,140,248,0.3)]' : '',
      border: 'border-indigo-500/20'
    },
    { 
      label: 'SEÑAL', 
      value: telemetry?.signal_strength !== undefined ? telemetry.signal_strength : '---', 
      percent: Math.min(((telemetry?.signal_strength || 0) / 31) * 100, 100), // CSQ max es 31
      unit: telemetry?.signal_strength !== undefined ? 'RSSI' : '',
      icon: Signal, 
      color: 'text-fuchsia-400', 
      glow: 'shadow-[0_0_15px_rgba(232,121,249,0.3)]',
      border: 'border-fuchsia-500/20'
    },
  ];

  return (
    <div className="min-h-screen bg-black text-zinc-300 font-sans selection:bg-cyan-500/30 pb-24 md:pb-8">
      {/* Fondo con resplandor radial */}
      <div className="fixed inset-0 bg-[radial-gradient(circle_at_50%_-20%,_#1e1b4b_0%,_#000_80%)] pointer-events-none" />

      <div className="relative max-w-7xl mx-auto p-4 md:p-8">
        {/* Aviso de Conexión Pendiente */}
        {!isConfigured && (
          <div className="mb-8 p-4 bg-amber-500/10 border border-amber-500/20 rounded-2xl flex items-center gap-4 animate-pulse">
            <div className="p-2 bg-amber-500/20 rounded-lg text-amber-500">
              <Signal size={20} />
            </div>
            <div>
              <p className="text-xs font-black tracking-widest text-amber-500 uppercase">System Alert: Database Offline</p>
              <p className="text-[10px] text-amber-500/70 uppercase">Faltan las credenciales de Supabase en .env.local. El portal está mostrando datos de simulación.</p>
            </div>
          </div>
        )}

        {/* Header */}
        <header className="flex flex-col md:flex-row md:items-center justify-between mb-12 gap-6">
          <div className="space-y-1">
            <div className="flex items-center gap-2 text-cyan-500 mb-1">
              <Activity size={18} className="animate-pulse" />
              <span className="text-xs font-black tracking-[0.2em] uppercase">Telemetry Link Active</span>
            </div>
          </div>
          
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
            <div className="pr-4 text-xs font-mono text-zinc-500">
              {telemetry?.motorcycle_id || 'ESP32_NODE_01'}
            </div>
          </div>
        </header>

        {/* Grid de Estadísticas */}
        <div className="grid grid-cols-2 lg:grid-cols-4 gap-6 mb-10">
          {stats.map((stat) => (
            <div key={stat.label} className={`group bg-zinc-900/40 backdrop-blur-xl border ${stat.border} ${stat.glow} p-6 rounded-3xl transition-all hover:scale-[1.02] hover:bg-zinc-900/60`}>
              <div className="flex items-center justify-between mb-6">
                <div className={`p-3 rounded-2xl bg-zinc-950/50 ${stat.color} border border-white/5`}>
                  <stat.icon size={22} />
                </div>
                <div className="h-1 w-12 bg-zinc-800 rounded-full overflow-hidden">
                  <div 
                    className={`h-full bg-current ${stat.color} transition-all duration-500`} 
                    style={{ width: `${stat.percent}%`, opacity: 0.5 + (stat.percent / 200) }}
                  />
                </div>
              </div>
              <p className="text-[10px] font-black tracking-[0.2em] text-zinc-500 mb-1 uppercase">{stat.label}</p>
              <div className="flex items-baseline gap-1">
                <h3 className="text-3xl font-black text-white font-mono">{stat.value}</h3>
                {stat.unit && <span className="text-xs font-bold text-zinc-600">{stat.unit}</span>}
              </div>
            </div>
          ))}
        </div>

        <div className="grid lg:grid-cols-3 gap-8">
          {/* Contenedor del Mapa */}
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
                    {selectedTrip ? `Trip ID: ${selectedTrip.slice(0,8)}` : 
                      telemetry ? `LAT: ${telemetry.latitude?.toFixed(4)}° • LON: ${telemetry.longitude?.toFixed(4)}°` : 
                      'SEARCHING_SATELLITES...'}
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
                path={trips.find(t => t.id === selectedTrip)?.path || (selectedTrip === '1' ? [[40.41678, -3.70379], [40.42, -3.71], [40.43, -3.72]] : undefined)} 
              />
              <div className="absolute inset-0 pointer-events-none bg-[linear-gradient(rgba(18,16,16,0)_50%,rgba(0,0,0,0.1)_50%),linear-gradient(90deg,rgba(255,0,0,0.03),rgba(0,255,0,0.01),rgba(0,0,255,0.03))] bg-[length:100%_2px,3px_100%] z-20 opacity-20" />
            </div>
          </div>

          {/* Historial de Viajes */}
          <div className="bg-zinc-900/40 backdrop-blur-xl border border-white/10 p-6 rounded-3xl shadow-2xl">
            <div className="flex items-center justify-between mb-8">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-indigo-500/10 text-indigo-500">
                  <RouteIcon size={18} />
                </div>
                <h2 className="text-lg font-bold text-white">HISTORIAL</h2>
              </div>
              <button className="text-[10px] font-bold text-zinc-500 hover:text-white transition-colors tracking-widest uppercase">
                View All
              </button>
            </div>
            
            <div className="space-y-4">
              {trips.length > 0 ? (
                trips.map((trip) => {
                  const date = new Date(trip.start_time).toLocaleDateString('es-ES', { day: '2-digit', month: 'short' }).toUpperCase();
                  const isSelected = selectedTrip === trip.id;
                  const batteryUsed = trip.consumption || (trip.start_battery_level && trip.end_battery_level ? trip.start_battery_level - trip.end_battery_level : null);
                  
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
                            {trip.duration || trip.time || 'N/A'}
                          </div>
                          {batteryUsed && (
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
                        <Zap size={14} fill={isSelected ? "currentColor" : "none"} />
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

            {telemetry?.battery_health && (
              <div className="mt-8 p-4 rounded-2xl bg-gradient-to-br from-indigo-500/10 to-transparent border border-indigo-500/20">
                <p className="text-[10px] font-bold text-indigo-400 mb-1 tracking-widest uppercase">Battery Health</p>
                <div className="flex items-end justify-between">
                  <span className="text-2xl font-black text-white font-mono">{telemetry.battery_health}%</span>
                  <span className="text-[10px] text-zinc-500 mb-1 italic">Optimal Performance</span>
                </div>
              </div>
            )}
          </div>
        </div>
      </div>

      {/* Navegación Inferior (Móvil) */}
      <nav className="fixed bottom-6 left-6 right-6 md:hidden z-50">
        <div className="bg-zinc-900/80 backdrop-blur-2xl border border-white/10 rounded-3xl p-2 flex items-center justify-around shadow-[0_20px_50px_rgba(0,0,0,0.5)]">
          {[
            { icon: Activity, label: 'Live' },
            { icon: RouteIcon, label: 'Trips' },
            { icon: MapPin, label: 'Map' },
            { icon: Smartphone, label: 'Device' }
          ].map((item, i) => (
            <button key={item.label} className={`flex flex-col items-center gap-1 p-3 rounded-2xl transition-all ${i === 0 ? 'bg-cyan-500/20 text-cyan-400' : 'text-zinc-500'}`}>
              <item.icon size={20} />
              <span className="text-[9px] font-black uppercase tracking-widest">{item.label}</span>
            </button>
          ))}
        </div>
      </nav>
    </div>
  );
}
