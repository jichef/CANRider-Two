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
  Smartphone,
  Trash2,
  Calendar
} from 'lucide-react';
import dynamic from 'next/dynamic';
import { useEffect, useState, useMemo } from 'react';
import { createClient } from '@/lib/supabase';
import { 
  AreaChart, 
  Area, 
  XAxis, 
  YAxis, 
  CartesianGrid, 
  Tooltip, 
  ResponsiveContainer 
} from 'recharts';

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
  const [historyData, setHistoryData] = useState<any[]>([]);
  const [timeRange, setTimeRange] = useState('24h');
  const [selectedTrip, setSelectedTrip] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [isDeleting, setIsDeleting] = useState(false);
  const [isConfigured, setIsConfigured] = useState(true);
  const [isStale, setIsStale] = useState(false);
  const [showHistory, setShowHistory] = useState(false);

  // Madrid por defecto si no hay datos
  const [currentPosition, setCurrentPosition] = useState<[number, number]>([40.41678, -3.70379]);

  // Cargar histórico cuando cambia el rango
  useEffect(() => {
    if (!supabase) return;

    const fetchHistory = async () => {
      const now = new Date();
      let startTime = new Date();
      
      if (timeRange === '1h') startTime.setHours(now.getHours() - 1);
      else if (timeRange === '6h') startTime.setHours(now.getHours() - 6);
      else if (timeRange === '24h') startTime.setHours(now.getHours() - 24);
      else if (timeRange === '7d') startTime.setDate(now.getDate() - 7);

      const { data } = await supabase
        .from('telemetry')
        .select('timestamp, battery_level, moto_battery, moto_battery_b, signal_strength, speed, bat_a_volts, bat_a_amps, bat_a_temp, bat_b_volts, bat_b_amps, bat_b_temp')
        .gt('timestamp', startTime.toISOString())
        .order('timestamp', { ascending: true });

      if (data) {
        setHistoryData(data.map((d: any) => ({
          ...d,
          time: new Date(d.timestamp).toLocaleTimeString('es-ES', { 
            hour: '2-digit', 
            minute: '2-digit',
            ...(timeRange === '7d' ? { day: '2-digit', month: '2-digit' } : {})
          })
        })));
      }
    };

    fetchHistory();
  }, [supabase, timeRange]);

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
        if (typeof telData.latitude === 'number' && typeof telData.longitude === 'number') {
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
          if (typeof newData.latitude === 'number' && typeof newData.longitude === 'number') {
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

  const handleClearData = async () => {
    if (!confirm('¿Estás seguro de que quieres borrar todo el historial de telemetría y viajes? Los dispositivos registrados se mantendrán.')) {
      return;
    }

    setIsDeleting(true);
    try {
      const response = await fetch('/api/admin/clear-data', {
        method: 'POST',
      });

      if (!response.ok) throw new Error('Error al borrar datos');

      alert('Datos borrados correctamente. La página se recargará.');
      window.location.reload();
    } catch (error) {
      console.error(error);
      alert('Error al intentar borrar los datos.');
    } finally {
      setIsDeleting(false);
    }
  };

  const stats = [
    { 
      label: 'BATERÍA CANRIDER', 
      value: (telemetry?.battery_level !== undefined && telemetry?.battery_level !== null) ? `${telemetry.battery_level}%` : '--%', 
      percent: telemetry?.battery_level || 0,
      icon: Battery, 
      color: (telemetry?.battery_level ?? 100) < 20 ? 'text-red-500' : 'text-emerald-400', 
      glow: (telemetry?.battery_level ?? 100) < 20 ? 'shadow-[0_0_15px_rgba(239,68,68,0.3)]' : 'shadow-[0_0_15px_rgba(52,211,153,0.3)]',
      border: (telemetry?.battery_level ?? 100) < 20 ? 'border-red-500/20' : 'border-emerald-500/20'
    },
    { 
      label: 'BATERÍA VEHÍCULO A', 
      value: (telemetry?.moto_battery !== undefined && telemetry?.moto_battery !== null) ? `${telemetry.moto_battery}%` : '--%', 
      percent: telemetry?.moto_battery || 0,
      icon: Zap, 
      is_charging: telemetry?.is_charging,
      color: telemetry?.is_charging ? 'text-yellow-400 animate-pulse' : 'text-yellow-400', 
      glow: 'shadow-[0_0_15px_rgba(250,204,21,0.3)]',
      border: 'border-yellow-500/20'
    },
    { 
      label: 'BATERÍA VEHÍCULO B', 
      value: (telemetry?.moto_battery_b !== undefined && telemetry?.moto_battery_b !== null) ? `${telemetry.moto_battery_b}%` : '--%', 
      percent: telemetry?.moto_battery_b || 0,
      icon: Zap, 
      is_charging: telemetry?.is_charging_b,
      color: telemetry?.is_charging_b ? 'text-orange-400 animate-pulse' : 'text-orange-400', 
      glow: 'shadow-[0_0_15px_rgba(251,146,60,0.3)]',
      border: 'border-orange-500/20'
    },
    { 
      label: 'VELOCIDAD', 
      value: (telemetry?.speed !== undefined && telemetry?.speed !== null) ? Math.round(telemetry.speed) : '--', 
      percent: Math.min((telemetry?.speed || 0) * 0.8, 100), // Max 120km/h aprox
      unit: (telemetry?.speed !== undefined && telemetry?.speed !== null) ? 'km/h' : '',
      icon: Navigation, 
      color: 'text-cyan-400', 
      glow: 'shadow-[0_0_15px_rgba(34,211,238,0.3)]',
      border: 'border-cyan-500/20'
    },
    { 
      label: 'ESTADO', 
      value: !isStale && telemetry 
        ? 'ONLINE' 
        : ((telemetry?.moto_battery !== null && telemetry?.moto_battery <= 10) || (telemetry?.moto_battery_b !== null && telemetry?.moto_battery_b <= 10))
          ? 'REPOSO' 
          : 'OFFLINE', 
      percent: !isStale && telemetry ? 100 : 0,
      icon: ShieldCheck, 
      color: !isStale && telemetry 
        ? 'text-indigo-400' 
        : ((telemetry?.moto_battery !== null && telemetry?.moto_battery <= 10) || (telemetry?.moto_battery_b !== null && telemetry?.moto_battery_b <= 10))
          ? 'text-amber-500' 
          : 'text-zinc-600', 
      glow: !isStale && telemetry 
        ? 'shadow-[0_0_15px_rgba(129,140,248,0.3)]' 
        : ((telemetry?.moto_battery !== null && telemetry?.moto_battery <= 10) || (telemetry?.moto_battery_b !== null && telemetry?.moto_battery_b <= 10))
          ? 'shadow-[0_0_15px_rgba(245,158,11,0.3)]'
          : '',
      border: 'border-indigo-500/20'
    },
    { 
      label: 'SEÑAL', 
      value: (telemetry?.signal_strength !== undefined && telemetry?.signal_strength !== null) ? telemetry.signal_strength : '--', 
      percent: Math.min(((telemetry?.signal_strength || 0) / 31) * 100, 100), // CSQ max es 31
      unit: (telemetry?.signal_strength !== undefined && telemetry?.signal_strength !== null) ? 'RSSI' : '',
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
              <span className="text-xs font-black tracking-[0.2em] uppercase">
                {telemetry?.location_type === 'lbs' ? 'GSM Cell Location (Approx)' : 'GPS Link Active'}
              </span>
            </div>
            {telemetry?.timestamp && (
              <div className="flex items-center gap-2 text-zinc-500 font-mono text-[10px]">
                <Clock size={12} />
                <span>ÚLTIMA ACTUALIZACIÓN: {new Date(telemetry.timestamp).toLocaleTimeString('es-ES')}</span>
              </div>
            )}
          </div>
          
          <div className="flex flex-wrap items-center gap-4">
            <button 
              onClick={() => setShowHistory(!showHistory)}
              className={`flex items-center gap-2 px-6 py-2 rounded-xl border transition-all duration-500 font-bold tracking-widest text-[10px] uppercase ${
                showHistory 
                  ? 'bg-cyan-500 text-black border-cyan-400 shadow-[0_0_20px_rgba(6,182,212,0.3)]' 
                  : 'bg-zinc-900/50 text-cyan-500 border-white/10 hover:border-cyan-500/50 hover:bg-zinc-900'
              }`}
            >
              <Activity size={14} className={showHistory ? 'animate-pulse' : ''} />
              {showHistory ? 'Cerrar Análisis' : 'Ver Análisis'}
            </button>

            <button 
              onClick={handleClearData}
              disabled={isDeleting}
              className={`flex items-center gap-2 px-6 py-2 rounded-xl border transition-all duration-500 font-bold tracking-widest text-[10px] uppercase ${
                isDeleting 
                  ? 'bg-red-500/20 text-red-500/50 border-red-500/20 cursor-not-allowed' 
                  : 'bg-zinc-900/50 text-red-500/70 border-white/10 hover:border-red-500/50 hover:bg-red-500/10 hover:text-red-500'
              }`}
            >
              <Trash2 size={14} className={isDeleting ? 'animate-spin' : ''} />
              {isDeleting ? 'Borrando...' : 'Vaciar Datos'}
            </button>

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
          </div>
        </header>

        {/* Grid de Estadísticas */}
        <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-6 gap-6 mb-10">
          {stats.map((stat) => (
            <div key={stat.label} className={`group bg-zinc-900/40 backdrop-blur-xl border ${stat.border} ${stat.glow} p-6 rounded-3xl transition-all hover:scale-[1.02] hover:bg-zinc-900/60`}>
              <div className="flex items-center justify-between mb-6">
                <div className={`p-3 rounded-2xl bg-zinc-950/50 ${stat.color} border border-white/5`}>
                  <stat.icon size={22} />
                </div>
                {stat.label !== 'ESTADO' && (
                  <div className="h-1 w-12 bg-zinc-800 rounded-full overflow-hidden">
                    <div 
                      className={`h-full bg-current ${stat.color} transition-all duration-500`} 
                      style={{ width: `${stat.percent}%`, opacity: 0.5 + (stat.percent / 200) }}
                    />
                  </div>
                )}
                {stat.label === 'ESTADO' && (
                  <div className={`w-2 h-2 rounded-full ${stat.percent > 0 ? 'bg-indigo-400 animate-ping' : 'bg-zinc-600'}`} />
                )}
              </div>
              <p className="text-[10px] font-black tracking-[0.2em] text-zinc-500 mb-1 uppercase">{stat.label}</p>
              <div className="flex items-baseline gap-2">
                <h3 className="text-3xl font-black text-white font-mono">{stat.value}</h3>
                {stat.unit && <span className="text-xs font-bold text-zinc-600">{stat.unit}</span>}
                {stat.is_charging && (
                  <span className="flex items-center gap-1 text-[8px] font-black text-emerald-400 animate-pulse tracking-widest border border-emerald-500/30 px-2 py-0.5 rounded-full">
                    <Zap size={8} fill="currentColor" />
                    CHARGING
                  </span>
                )}
              </div>
            </div>
          ))}
        </div>

        {/* Histórico y Gráficas */}
        {showHistory && (
          <div className="mb-10 bg-zinc-900/40 backdrop-blur-xl border border-white/10 rounded-3xl p-6 shadow-2xl animate-in fade-in slide-in-from-bottom-4 duration-700">
            <div className="flex flex-col md:flex-row md:items-center justify-between gap-6 mb-8">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-cyan-500/10 text-cyan-500">
                  <Activity size={18} />
                </div>
                <div>
                  <h2 className="text-lg font-bold text-white uppercase tracking-wider">Histórico de Telemetría</h2>
                  <p className="text-[10px] text-zinc-500 font-mono">EVOLUCIÓN TEMPORAL DE LOS DATOS</p>
                </div>
              </div>

              <div className="flex bg-zinc-950/50 p-1 rounded-xl border border-white/5">
                {[
                  { id: '1h', label: '1H' },
                  { id: '6h', label: '6H' },
                  { id: '24h', label: '24H' },
                  { id: '7d', label: '7D' },
                ].map((range) => (
                  <button
                    key={range.id}
                    onClick={() => setTimeRange(range.id)}
                    className={`px-4 py-1.5 rounded-lg text-[10px] font-bold tracking-widest transition-all ${
                      timeRange === range.id 
                        ? 'bg-cyan-500 text-black shadow-[0_0_15px_rgba(6,182,212,0.3)]' 
                        : 'text-zinc-500 hover:text-white'
                    }`}
                  >
                    {range.label}
                  </button>
                ))}
              </div>
            </div>

            <div className="grid md:grid-cols-2 gap-8 h-[300px]">
              {/* Gráfica de Batería */}
              <div className="relative">
                <div className="absolute top-0 left-0 text-[10px] font-bold text-emerald-400/50 tracking-widest uppercase mb-4">
                  Nivel de baterías CanRider y Vehículo A/B (%)
                </div>
                <ResponsiveContainer width="100%" height="100%">
                  <AreaChart data={historyData}>
                    <defs>
                      <linearGradient id="colorBat" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#10b981" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#10b981" stopOpacity={0}/>
                      </linearGradient>
                      <linearGradient id="colorMotoA" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#facc15" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#facc15" stopOpacity={0}/>
                      </linearGradient>
                      <linearGradient id="colorMotoB" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#fb923c" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#fb923c" stopOpacity={0}/>
                      </linearGradient>
                    </defs>
                    <CartesianGrid strokeDasharray="3 3" stroke="#ffffff05" vertical={false} />
                    <XAxis 
                      dataKey="time" 
                      stroke="#ffffff20" 
                      fontSize={10} 
                      tickLine={false} 
                      axisLine={false}
                      interval="preserveStartEnd"
                    />
                    <YAxis 
                      domain={[0, 100]} 
                      stroke="#ffffff20" 
                      fontSize={10} 
                      tickLine={false} 
                      axisLine={false}
                    />
                    <Tooltip 
                      contentStyle={{ backgroundColor: '#09090b', border: '1px solid #ffffff10', borderRadius: '12px', fontSize: '10px' }}
                      itemStyle={{ fontSize: '10px' }}
                    />
                    <Area 
                      type="monotone" 
                      dataKey="battery_level" 
                      name="CanRider"
                      stroke="#10b981" 
                      fillOpacity={1} 
                      fill="url(#colorBat)" 
                      strokeWidth={2}
                    />
                    <Area 
                      type="monotone" 
                      dataKey="moto_battery" 
                      name="Vehículo A"
                      stroke="#facc15" 
                      fillOpacity={1} 
                      fill="url(#colorMotoA)" 
                      strokeWidth={2}
                    />
                    <Area 
                      type="monotone" 
                      dataKey="moto_battery_b" 
                      name="Vehículo B"
                      stroke="#fb923c" 
                      fillOpacity={1} 
                      fill="url(#colorMotoB)" 
                      strokeWidth={2}
                    />
                  </AreaChart>
                </ResponsiveContainer>
              </div>

              {/* Gráfica de Señal y Velocidad */}
              <div className="relative">
                <div className="absolute top-0 left-0 text-[10px] font-bold text-cyan-400/50 tracking-widest uppercase mb-4">
                  Señal (RSSI) y Velocidad (km/h)
                </div>
                <ResponsiveContainer width="100%" height="100%">
                  <AreaChart data={historyData}>
                    <defs>
                      <linearGradient id="colorSignal" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#22d3ee" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#22d3ee" stopOpacity={0}/>
                      </linearGradient>
                      <linearGradient id="colorSpeed" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#818cf8" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#818cf8" stopOpacity={0}/>
                      </linearGradient>
                    </defs>
                    <CartesianGrid strokeDasharray="3 3" stroke="#ffffff05" vertical={false} />
                    <XAxis 
                      dataKey="time" 
                      stroke="#ffffff20" 
                      fontSize={10} 
                      tickLine={false} 
                      axisLine={false}
                      interval="preserveStartEnd"
                    />
                    <YAxis 
                      stroke="#ffffff20" 
                      fontSize={10} 
                      tickLine={false} 
                      axisLine={false}
                    />
                    <Tooltip 
                      contentStyle={{ backgroundColor: '#09090b', border: '1px solid #ffffff10', borderRadius: '12px', fontSize: '10px' }}
                    />
                    <Area 
                      type="monotone" 
                      dataKey="signal_strength" 
                      name="Señal"
                      stroke="#22d3ee" 
                      fillOpacity={1} 
                      fill="url(#colorSignal)" 
                      strokeWidth={2}
                    />
                    <Area 
                      type="monotone" 
                      dataKey="speed" 
                      name="Velocidad"
                      stroke="#818cf8" 
                      fillOpacity={1} 
                      fill="url(#colorSpeed)" 
                      strokeWidth={2}
                    />
                  </AreaChart>
                </ResponsiveContainer>
              </div>
            </div>

            <div className="grid md:grid-cols-2 gap-8 h-[300px] mt-12 pt-8 border-t border-white/5">
              {/* Gráfica de Corriente */}
              <div className="relative">
                <div className="absolute top-0 left-0 text-[10px] font-bold text-orange-400/50 tracking-widest uppercase mb-4">
                  Corriente de Baterías (Amperios)
                </div>
                <ResponsiveContainer width="100%" height="100%">
                  <AreaChart data={historyData}>
                    <defs>
                      <linearGradient id="colorAmpsA" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#facc15" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#facc15" stopOpacity={0}/>
                      </linearGradient>
                      <linearGradient id="colorAmpsB" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#fb923c" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#fb923c" stopOpacity={0}/>
                      </linearGradient>
                    </defs>
                    <CartesianGrid strokeDasharray="3 3" stroke="#ffffff05" vertical={false} />
                    <XAxis dataKey="time" stroke="#ffffff20" fontSize={10} tickLine={false} axisLine={false} interval="preserveStartEnd" />
                    <YAxis stroke="#ffffff20" fontSize={10} tickLine={false} axisLine={false} />
                    <Tooltip contentStyle={{ backgroundColor: '#09090b', border: '1px solid #ffffff10', borderRadius: '12px', fontSize: '10px' }} />
                    <Area type="monotone" dataKey="bat_a_amps" name="Corriente A" stroke="#facc15" fill="url(#colorAmpsA)" strokeWidth={2} />
                    <Area type="monotone" dataKey="bat_b_amps" name="Corriente B" stroke="#fb923c" fill="url(#colorAmpsB)" strokeWidth={2} />
                  </AreaChart>
                </ResponsiveContainer>
              </div>

              {/* Gráfica de Temperatura */}
              <div className="relative">
                <div className="absolute top-0 left-0 text-[10px] font-bold text-red-400/50 tracking-widest uppercase mb-4">
                  Temperatura (°C)
                </div>
                <ResponsiveContainer width="100%" height="100%">
                  <AreaChart data={historyData}>
                    <defs>
                      <linearGradient id="colorTempA" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="5%" stopColor="#f87171" stopOpacity={0.3}/>
                        <stop offset="95%" stopColor="#f87171" stopOpacity={0}/>
                      </linearGradient>
                    </defs>
                    <CartesianGrid strokeDasharray="3 3" stroke="#ffffff05" vertical={false} />
                    <XAxis dataKey="time" stroke="#ffffff20" fontSize={10} tickLine={false} axisLine={false} interval="preserveStartEnd" />
                    <YAxis stroke="#ffffff20" fontSize={10} tickLine={false} axisLine={false} />
                    <Tooltip contentStyle={{ backgroundColor: '#09090b', border: '1px solid #ffffff10', borderRadius: '12px', fontSize: '10px' }} />
                    <Area type="monotone" dataKey="bat_a_temp" name="Temp A" stroke="#f87171" fill="url(#colorTempA)" strokeWidth={2} />
                    <Area type="monotone" dataKey="bat_b_temp" name="Temp B" stroke="#ef4444" fillOpacity={0} strokeWidth={2} />
                  </AreaChart>
                </ResponsiveContainer>
              </div>
            </div>
          </div>
        )}

        <div className="grid lg:grid-cols-3 gap-8">
          {/* Contenedor del Mapa */}
          <div className="lg:col-span-2 group bg-zinc-900/40 backdrop-blur-xl border border-white/10 rounded-3xl overflow-hidden shadow-2xl flex flex-col transition-all hover:border-white/20">
            <div className="p-5 border-b border-white/5 flex items-center justify-between bg-zinc-950/20">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-red-500/10 text-red-500">
                  <MapPin size={18} />
                </div>
                <div>
                  <span className="text-sm font-bold text-white block uppercase">
                    {selectedTrip ? 'Análisis de ruta' : 'Ubicación actual'}
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
                locationType={telemetry?.location_type}
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
