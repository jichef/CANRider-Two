'use client';

import React, { useState, useEffect } from 'react';
import { createClient } from '@/lib/supabase';
import { Zap, Battery, Settings, Clock } from 'lucide-react';

interface CanRule {
  id: number;
  start: number;
  len: number;
  factor: number;
  be: boolean;
  signed?: boolean;
}

interface CanConfig {
  id?: number;
  batA: { v: CanRule; c: CanRule; s: CanRule; t: CanRule };
  batB: { v: CanRule; c: CanRule; s: CanRule; t: CanRule };
  time_tx_id: number;
  time_hour_byte: number;
  time_min_byte: number;
  timezone_offset: number;
  dst_mode: boolean;
}

export default function CanConfigPanel({ motorcycleId }: { motorcycleId: string }) {
  const supabase = createClient();
  const [activeTab, setActiveTab] = useState<'A' | 'B' | 'SYS'>('A');
  const [config, setConfig] = useState<CanConfig>({
    batA: {
      v: { id: 0x504, start: 2, len: 2, factor: 0.01, be: true },
      c: { id: 0x504, start: 4, len: 2, factor: 0.1, be: true, signed: true },
      s: { id: 0x540, start: 0, len: 1, factor: 1, be: true },
      t: { id: 0x540, start: 3, len: 1, factor: 1, be: true }
    },
    batB: {
      v: { id: 0x505, start: 2, len: 2, factor: 0.01, be: true },
      c: { id: 0x505, start: 4, len: 2, factor: 0.1, be: true, signed: true },
      s: { id: 0x541, start: 0, len: 1, factor: 1, be: true },
      t: { id: 0x541, start: 3, len: 1, factor: 1, be: true }
    },
    time_tx_id: 0x510,
    time_hour_byte: 5,
    time_min_byte: 6,
    timezone_offset: 0,
    dst_mode: false
  });
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    fetchConfig();
  }, [motorcycleId]);

  async function fetchConfig() {
    if (!supabase) {
      setLoading(false);
      return;
    }
    try {
      const { data, error } = await supabase
        .from('can_configurations')
        .select('*')
        .eq('motorcycle_id', motorcycleId)
        .maybeSingle();

      if (data) {
        setConfig({
          id: data.id,
          batA: {
            v: { id: data.v_id, start: data.v_start, len: data.v_len, factor: data.v_factor, be: data.v_be },
            c: { id: data.c_id, start: data.c_start, len: data.c_len, factor: data.c_factor, be: data.c_be, signed: data.c_signed },
            s: { id: data.s_id, start: data.s_start, len: data.s_len, factor: data.s_factor, be: data.s_be },
            t: { id: data.t_id, start: data.t_start, len: data.t_len, factor: data.t_factor, be: data.t_be },
          },
          batB: {
            v: { id: data.vb_id, start: data.vb_start, len: data.vb_len, factor: data.vb_factor, be: data.vb_be },
            c: { id: data.cb_id, start: data.cb_start, len: data.cb_len, factor: data.cb_factor, be: data.cb_be, signed: data.cb_signed },
            s: { id: data.sb_id, start: data.sb_start, len: data.sb_len, factor: data.sb_factor, be: data.sb_be },
            t: { id: data.tb_id, start: data.tb_start, len: data.tb_len, factor: data.tb_factor, be: data.tb_be },
          },
          time_tx_id: data.time_tx_id,
          time_hour_byte: data.time_hour_byte ?? 5,
          time_min_byte: data.time_min_byte ?? 6,
          timezone_offset: data.timezone_offset ?? 0,
          dst_mode: data.dst_mode ?? false
        });
      }
    } catch (err) {
      console.error('Error fetching CAN config:', err);
    } finally {
      setLoading(false);
    }
  }

  async function saveConfig() {
    if (!supabase) return;
    setSaving(true);
    try {
      const { data: result, error } = await supabase
        .from('can_configurations')
        .upsert({
          id: config.id, // Si existe, hará UPDATE, si no, INSERT
          motorcycle_id: motorcycleId,
          // Bat A
          v_id: config.batA.v.id, v_start: config.batA.v.start, v_len: config.batA.v.len, v_factor: config.batA.v.factor, v_be: config.batA.v.be,
          c_id: config.batA.c.id, c_start: config.batA.c.start, c_len: config.batA.c.len, c_factor: config.batA.c.factor, c_be: config.batA.c.be, c_signed: config.batA.c.signed,
          s_id: config.batA.s.id, s_start: config.batA.s.start, s_len: config.batA.s.len, s_factor: config.batA.s.factor, s_be: config.batA.s.be,
          t_id: config.batA.t.id, t_start: config.batA.t.start, t_len: config.batA.t.len, t_factor: config.batA.t.factor, t_be: config.batA.t.be,
          // Bat B
          vb_id: config.batB.v.id, vb_start: config.batB.v.start, vb_len: config.batB.v.len, vb_factor: config.batB.v.factor, vb_be: config.batB.v.be,
          cb_id: config.batB.c.id, cb_start: config.batB.c.start, cb_len: config.batB.c.len, cb_factor: config.batB.c.factor, cb_be: config.batB.c.be, cb_signed: config.batB.c.signed,
          sb_id: config.batB.s.id, sb_start: config.batB.s.start, sb_len: config.batB.s.len, sb_factor: config.batB.s.factor, sb_be: config.batB.s.be,
          tb_id: config.batB.t.id, tb_start: config.batB.t.start, tb_len: config.batB.t.len, tb_factor: config.batB.t.factor, tb_be: config.batB.t.be,
          // System
          time_tx_id: config.time_tx_id,
          time_hour_byte: config.time_hour_byte,
          time_min_byte: config.time_min_byte,
          timezone_offset: config.timezone_offset,
          dst_mode: config.dst_mode,
          updated_at: new Date().toISOString()
        }, { onConflict: 'motorcycle_id' })
        .select('id')
        .single();
      
      if (error) throw error;
      if (result) setConfig({ ...config, id: result.id });
      alert('Configuración guardada correctamente');
    } catch (err) {
      console.error('Error saving CAN config:', err);
      alert('Error al guardar: ' + (err as any).message);
    } finally {
      setSaving(false);
    }
  }

  const updateRule = (bat: 'batA' | 'batB', type: keyof CanConfig['batA'], field: keyof CanRule, value: any) => {
    setConfig({
      ...config,
      [bat]: {
        ...config[bat],
        [type]: { ...config[bat][type], [field]: value }
      }
    });
  };

  const renderRuleInputs = (label: string, bat: 'batA' | 'batB', type: keyof CanConfig['batA'], showSigned = false) => {
    const rule = config[bat][type];
    return (
      <div className="border border-white/5 p-4 rounded-2xl mb-4 bg-zinc-950/30">
        <h3 className="font-bold mb-4 text-zinc-400 text-[10px] tracking-widest uppercase">{label}</h3>
        <div className="grid grid-cols-2 md:grid-cols-5 gap-4">
          <div>
            <label className="text-[9px] text-zinc-600 block mb-1 uppercase font-black">ID (Hex)</label>
            <input 
              type="text" 
              className="w-full p-2 bg-zinc-900 border border-white/5 rounded-lg text-white font-mono text-xs focus:border-blue-500 outline-none" 
              value={`0x${rule.id.toString(16).toUpperCase()}`}
              onChange={(e) => {
                const val = parseInt(e.target.value.replace('0x', ''), 16);
                if (!isNaN(val)) updateRule(bat, type, 'id', val);
              }}
            />
          </div>
          <div>
            <label className="text-[9px] text-zinc-600 block mb-1 uppercase font-black">Byte</label>
            <input 
              type="number" 
              className="w-full p-2 bg-zinc-900 border border-white/5 rounded-lg text-white font-mono text-xs focus:border-blue-500 outline-none" 
              value={rule.start}
              onChange={(e) => updateRule(bat, type, 'start', parseInt(e.target.value) || 0)}
            />
          </div>
          <div>
            <label className="text-[9px] text-zinc-600 block mb-1 uppercase font-black">Longitud</label>
            <select 
              className="w-full p-2 bg-zinc-900 border border-white/5 rounded-lg text-white font-mono text-xs focus:border-blue-500 outline-none" 
              value={rule.len}
              onChange={(e) => updateRule(bat, type, 'len', parseInt(e.target.value))}
            >
              <option value={1}>1 B</option>
              <option value={2}>2 B</option>
            </select>
          </div>
          <div>
            <label className="text-[9px] text-zinc-600 block mb-1 uppercase font-black">Factor</label>
            <input 
              type="number" step="0.0001"
              className="w-full p-2 bg-zinc-900 border border-white/5 rounded-lg text-white font-mono text-xs focus:border-blue-500 outline-none" 
              value={rule.factor}
              onChange={(e) => updateRule(bat, type, 'factor', parseFloat(e.target.value) || 0)}
            />
          </div>
          <div className="flex flex-col justify-center gap-1">
            <label className="flex items-center gap-2 cursor-pointer">
              <input 
                type="checkbox" 
                className="w-3 h-3 rounded bg-zinc-900 text-blue-500 border-white/5"
                checked={rule.be}
                onChange={(e) => updateRule(bat, type, 'be', e.target.checked)}
              />
              <span className="text-[8px] text-zinc-500 uppercase font-black">BigEnd</span>
            </label>
            {showSigned && (
              <label className="flex items-center gap-2 cursor-pointer">
                <input 
                  type="checkbox" 
                  className="w-3 h-3 rounded bg-zinc-900 text-blue-500 border-white/5"
                  checked={rule.signed}
                  onChange={(e) => updateRule(bat, type, 'signed', e.target.checked)}
                />
                <span className="text-[8px] text-zinc-500 uppercase font-black">Signed</span>
              </label>
            )}
          </div>
        </div>
      </div>
    );
  };

  return (
    <div className="bg-zinc-900 border border-white/10 shadow-2xl rounded-[2.5rem] p-4 md:p-8 max-w-5xl mx-auto overflow-hidden relative">
      <div className="absolute top-0 right-0 p-12 opacity-5 pointer-events-none">
        <Settings size={200} />
      </div>

      <div className="relative z-10 flex flex-col md:flex-row md:items-center justify-between mb-10 gap-6">
        <div>
          <h2 className="text-2xl font-black text-white flex items-center gap-3 tracking-tighter uppercase">
            <div className="p-3 bg-blue-500/10 rounded-2xl text-blue-400 border border-blue-500/20">
              <Zap size={24} fill="currentColor" />
            </div>
            CAN SELECTOR GRANULAR
          </h2>
          <p className="text-[10px] text-zinc-500 mt-2 font-mono tracking-[0.3em] uppercase">Manual Configuration Node v2.0</p>
        </div>

        <div className="flex bg-zinc-950/50 p-1.5 rounded-2xl border border-white/5">
          <button onClick={() => setActiveTab('A')} className={`px-6 py-2 rounded-xl text-[10px] font-black tracking-widest transition-all ${activeTab === 'A' ? 'bg-blue-600 text-white shadow-lg shadow-blue-600/20' : 'text-zinc-500 hover:text-white'}`}>BAT_A</button>
          <button onClick={() => setActiveTab('B')} className={`px-6 py-2 rounded-xl text-[10px] font-black tracking-widest transition-all ${activeTab === 'B' ? 'bg-orange-600 text-white shadow-lg shadow-orange-600/20' : 'text-zinc-500 hover:text-white'}`}>BAT_B</button>
          <button onClick={() => setActiveTab('SYS')} className={`px-6 py-2 rounded-xl text-[10px] font-black tracking-widest transition-all ${activeTab === 'SYS' ? 'bg-zinc-700 text-white' : 'text-zinc-500 hover:text-white'}`}>SYS_INJECT</button>
        </div>
      </div>
      
      <div className="relative z-10 min-h-[400px]">
        {activeTab === 'A' && (
          <div className="animate-in fade-in slide-in-from-right-4 duration-500">
            <div className="flex items-center gap-2 mb-6 text-blue-400">
              <Battery size={16} />
              <span className="text-[10px] font-black tracking-widest uppercase">Parámetros Batería Principal (A)</span>
            </div>
            {renderRuleInputs('Voltaje de Batería', 'batA', 'v')}
            {renderRuleInputs('Corriente (Amperios)', 'batA', 'c', true)}
            {renderRuleInputs('Estado de Carga (SOC)', 'batA', 's')}
            {renderRuleInputs('Temperatura de Celdas', 'batA', 't')}
          </div>
        )}

        {activeTab === 'B' && (
          <div className="animate-in fade-in slide-in-from-right-4 duration-500">
            <div className="flex items-center gap-2 mb-6 text-orange-400">
              <Battery size={16} />
              <span className="text-[10px] font-black tracking-widest uppercase">Parámetros Batería Secundaria (B)</span>
            </div>
            {renderRuleInputs('Voltaje de Batería', 'batB', 'v')}
            {renderRuleInputs('Corriente (Amperios)', 'batB', 'c', true)}
            {renderRuleInputs('Estado de Carga (SOC)', 'batB', 's')}
            {renderRuleInputs('Temperatura de Celdas', 'batB', 't')}
          </div>
        )}

        {activeTab === 'SYS' && (
          <div className="animate-in fade-in slide-in-from-right-4 duration-500 space-y-6">
            <div className="flex items-center gap-2 mb-6 text-zinc-400">
              <Settings size={16} />
              <span className="text-[10px] font-black tracking-widest uppercase">Configuración de Sistema</span>
            </div>
            
            <div className="border border-white/5 p-6 md:p-8 rounded-3xl bg-zinc-950/30">
              <div className="flex flex-col md:flex-row md:items-center justify-between gap-6 md:gap-8">
                <div className="space-y-2">
                  <div className="flex items-center gap-2 text-white">
                    <Clock size={16} className="text-blue-400" />
                    <h3 className="font-bold text-xs tracking-widest uppercase">Inyección de Hora CAN</h3>
                  </div>
                  <p className="text-[9px] text-zinc-600 font-mono leading-relaxed max-w-md">
                    El ESP32 enviará una trama con la hora sincronizada por GPS/NTP a este ID específico del bus.
                  </p>
                </div>
                <div className="flex flex-wrap md:flex-nowrap gap-4">
                  <div className="flex-1 min-w-[100px]">
                    <label className="text-[9px] text-zinc-600 block mb-2 uppercase font-black">ID Trama (Hex)</label>
                    <input 
                      type="text" 
                      className="w-full p-3 bg-zinc-900 border border-white/5 rounded-xl text-white font-mono text-center text-sm focus:border-blue-500 outline-none" 
                      value={`0x${config.time_tx_id.toString(16).toUpperCase()}`}
                      onChange={(e) => {
                        const val = parseInt(e.target.value.replace('0x', ''), 16);
                        if (!isNaN(val)) setConfig({ ...config, time_tx_id: val });
                      }}
                    />
                  </div>
                  <div className="w-20 md:w-20">
                    <label className="text-[9px] text-zinc-600 block mb-2 uppercase font-black">Hora</label>
                    <input 
                      type="number" 
                      className="w-full p-3 bg-zinc-900 border border-white/5 rounded-xl text-white font-mono text-center text-sm focus:border-blue-500 outline-none" 
                      value={config.time_hour_byte}
                      onChange={(e) => setConfig({ ...config, time_hour_byte: parseInt(e.target.value) || 0 })}
                    />
                  </div>
                  <div className="w-20 md:w-20">
                    <label className="text-[9px] text-zinc-600 block mb-2 uppercase font-black">Min</label>
                    <input 
                      type="number" 
                      className="w-full p-3 bg-zinc-900 border border-white/5 rounded-xl text-white font-mono text-center text-sm focus:border-blue-500 outline-none" 
                      value={config.time_min_byte}
                      onChange={(e) => setConfig({ ...config, time_min_byte: parseInt(e.target.value) || 0 })}
                    />
                  </div>
                </div>
              </div>
            </div>

            <div className="border border-white/5 p-6 md:p-8 rounded-3xl bg-zinc-950/30">
              <div className="flex flex-col md:flex-row md:items-center justify-between gap-6 md:gap-8">
                <div className="space-y-2">
                  <div className="flex items-center gap-2 text-white">
                    <Settings size={16} className="text-orange-400" />
                    <h3 className="font-bold text-xs tracking-widest uppercase">Zona Horaria y Horario de Verano</h3>
                  </div>
                  <p className="text-[9px] text-zinc-600 font-mono leading-relaxed max-w-md">
                    Ajuste el desfase horario (UTC) y active el modo verano si es necesario para la correcta sincronización de la moto.
                  </p>
                </div>
                <div className="flex gap-4 items-center">
                  <div>
                    <label className="text-[9px] text-zinc-600 block mb-2 uppercase font-black">UTC Offset</label>
                    <select 
                      className="p-3 bg-zinc-900 border border-white/5 rounded-xl text-white font-mono text-sm focus:border-blue-500 outline-none"
                      value={config.timezone_offset}
                      onChange={(e) => setConfig({ ...config, timezone_offset: parseInt(e.target.value) })}
                    >
                      {Array.from({ length: 25 }, (_, i) => i - 12).map(offset => (
                        <option key={offset} value={offset}>UTC {offset >= 0 ? '+' : ''}{offset}</option>
                      ))}
                    </select>
                  </div>
                  <div className="flex flex-col items-center">
                    <label className="text-[9px] text-zinc-600 block mb-2 uppercase font-black">Modo Verano</label>
                    <button 
                      onClick={() => setConfig({ ...config, dst_mode: !config.dst_mode })}
                      className={`w-12 h-6 rounded-full transition-all relative ${config.dst_mode ? 'bg-orange-500' : 'bg-zinc-700'}`}
                    >
                      <div className={`absolute top-1 w-4 h-4 bg-white rounded-full transition-all ${config.dst_mode ? 'left-7' : 'left-1'}`} />
                    </button>
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}
      </div>

      <div className="mt-12 flex gap-4">
        <button 
          onClick={saveConfig}
          disabled={saving}
          className="flex-1 bg-blue-600 hover:bg-blue-500 text-white font-black py-5 rounded-2xl transition-all disabled:opacity-50 shadow-[0_0_30px_rgba(37,99,235,0.2)] text-[10px] tracking-[0.3em] uppercase flex items-center justify-center gap-3"
        >
          {saving ? 'PROCESANDO_CAMBIOS...' : 'DESPLEGAR_CONFIGURACIÓN'}
        </button>
      </div>
    </div>
  );
}
