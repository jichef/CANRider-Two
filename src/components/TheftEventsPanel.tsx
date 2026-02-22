'use client';

import React, { useState, useEffect } from 'react';
import { createClient } from '@/lib/supabase';
import { AlertTriangle, MapPin, Clock, Zap } from 'lucide-react';

interface TheftEvent {
  id: string;
  motorcycle_id: string;
  start_time: string;
  end_time: string;
  status: string;
  start_latitude: number;
  start_longitude: number;
  end_latitude: number;
  end_longitude: number;
  distance_km: number;
  max_speed: number;
  battery_level_start: number;
  battery_level_end: number;
}

export default function TheftEventsPanel({ motorcycleId }: { motorcycleId: string }) {
  const supabase = createClient();
  const [events, setEvents] = useState<TheftEvent[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetchTheftEvents();
    const interval = setInterval(fetchTheftEvents, 10000);
    return () => clearInterval(interval);
  }, [motorcycleId]);

  async function fetchTheftEvents() {
    if (!supabase) {
      console.warn('[TheftEvents] Supabase client not available');
      setLoading(false);
      return;
    }

    try {
      console.log('[TheftEvents] Fetching events for:', motorcycleId);
      
      const { data, error } = await supabase
        .from('theft_events')
        .select('*')
        .eq('motorcycle_id', motorcycleId)
        .order('start_time', { ascending: false })
        .limit(20);

      if (error) {
        console.error('[TheftEvents] Supabase error:', error);
      } else {
        console.log('[TheftEvents] Fetched events:', data?.length || 0);
        if (data) {
          setEvents(data);
        }
      }
    } catch (err) {
      console.error('[TheftEvents] Exception:', err);
    } finally {
      setLoading(false);
    }
  }

  const formatDate = (dateStr: string) => {
    const date = new Date(dateStr);
    return date.toLocaleString('es-ES', {
      year: '2-digit',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit'
    });
  };

  const getDuration = (start: string, end: string) => {
    const startTime = new Date(start).getTime();
    const endTime = new Date(end).getTime();
    const diffMs = endTime - startTime;
    const diffMins = Math.floor(diffMs / 60000);
    return `${diffMins} min`;
  };

  return (
    <div className="bg-red-950/30 border border-red-500/30 rounded-3xl p-8 max-w-5xl mx-auto">
      <div className="flex items-center gap-3 mb-8">
        <div className="p-3 bg-red-500/20 rounded-2xl text-red-500">
          <AlertTriangle size={28} />
        </div>
        <div>
          <h2 className="text-2xl font-black text-red-400 uppercase">Eventos de Robo</h2>
          <p className="text-xs text-red-600/70 mt-1 font-mono">Movimiento sin motor activo</p>
        </div>
      </div>

      {loading ? (
        <div className="text-center py-12 text-zinc-500">Cargando eventos...</div>
      ) : events.length === 0 ? (
        <div className="text-center py-12">
          <p className="text-zinc-500">✓ Sin eventos de robo detectados</p>
        </div>
      ) : (
        <div className="space-y-4">
          {events.map((event) => (
            <div key={event.id} className="border border-red-500/20 rounded-2xl p-5 bg-zinc-950/40">
              <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                {/* Tiempo */}
                <div className="flex items-start gap-3">
                  <Clock size={16} className="text-red-500 mt-1 flex-shrink-0" />
                  <div>
                    <p className="text-[9px] text-red-600/70 font-mono uppercase mb-1">Inicio</p>
                    <p className="text-sm text-white font-mono">{formatDate(event.start_time)}</p>
                    <p className="text-xs text-zinc-500 mt-1">
                      Duración: {getDuration(event.start_time, event.end_time)}
                    </p>
                  </div>
                </div>

                {/* Distancia */}
                <div className="flex items-start gap-3">
                  <MapPin size={16} className="text-red-500 mt-1 flex-shrink-0" />
                  <div>
                    <p className="text-[9px] text-red-600/70 font-mono uppercase mb-1">Desplazamiento</p>
                    <p className="text-sm text-white font-mono">{event.distance_km.toFixed(2)} km</p>
                    <p className="text-xs text-zinc-500 mt-1">
                      Max: {event.max_speed.toFixed(1)} km/h
                    </p>
                  </div>
                </div>

                {/* Batería */}
                <div className="flex items-start gap-3">
                  <Zap size={16} className="text-orange-500 mt-1 flex-shrink-0" />
                  <div>
                    <p className="text-[9px] text-orange-600/70 font-mono uppercase mb-1">Batería</p>
                    <p className="text-sm text-white font-mono">
                      {event.battery_level_start}% → {event.battery_level_end}%
                    </p>
                    <p className="text-xs text-zinc-500 mt-1">
                      Consumo: {(event.battery_level_start - event.battery_level_end).toFixed(0)}%
                    </p>
                  </div>
                </div>

                {/* Ubicación */}
                <div className="flex items-start gap-3">
                  <MapPin size={16} className="text-blue-500 mt-1 flex-shrink-0" />
                  <div>
                    <p className="text-[9px] text-blue-600/70 font-mono uppercase mb-1">Inicio → Fin</p>
                    <p className="text-[10px] text-white font-mono">
                      {event.start_latitude.toFixed(4)}, {event.start_longitude.toFixed(4)}
                    </p>
                    <p className="text-[10px] text-zinc-500 mt-1">
                      → {event.end_latitude.toFixed(4)}, {event.end_longitude.toFixed(4)}
                    </p>
                  </div>
                </div>
              </div>

              {/* Link a Google Maps */}
              <div className="mt-4">
                <a
                  href={`https://www.google.com/maps?q=${event.start_latitude},${event.start_longitude}&z=14`}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="text-xs text-red-400 hover:text-red-300 font-mono underline"
                >
                  Ver en Google Maps
                </a>
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
