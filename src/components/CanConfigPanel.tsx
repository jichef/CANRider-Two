'use client';

import React, { useState, useEffect } from 'react';
import { createClient } from '@/lib/supabase';
import { Zap, X } from 'lucide-react';

interface CanRule {
  id: number;
  start: number;
  len: number;
  factor: number;
  be: boolean;
  signed?: boolean;
}

interface CanConfig {
  v: CanRule;
  c: CanRule;
  s: CanRule;
  t: CanRule;
  bat_b_offset: number;
}

export default function CanConfigPanel({ motorcycleId }: { motorcycleId: string }) {
  const supabase = createClient();
  const [config, setConfig] = useState<CanConfig>({
    v: { id: 0x504, start: 2, len: 2, factor: 0.01, be: true },
    c: { id: 0x504, start: 4, len: 2, factor: 0.1, be: true, signed: true },
    s: { id: 0x540, start: 0, len: 1, factor: 1, be: true },
    t: { id: 0x540, start: 3, len: 1, factor: 1, be: true },
    bat_b_offset: 1
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
          v: { id: data.v_id, start: data.v_start, len: data.v_len, factor: data.v_factor, be: data.v_be },
          c: { id: data.c_id, start: data.c_start, len: data.c_len, factor: data.c_factor, be: data.c_be, signed: data.c_signed },
          s: { id: data.s_id, start: data.s_start, len: data.s_len, factor: data.s_factor, be: data.s_be },
          t: { id: data.t_id, start: data.t_start, len: data.t_len, factor: data.t_factor, be: data.t_be },
          bat_b_offset: data.bat_b_offset
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
      const { error } = await supabase
        .from('can_configurations')
        .upsert({
          motorcycle_id: motorcycleId,
          v_id: config.v.id, v_start: config.v.start, v_len: config.v.len, v_factor: config.v.factor, v_be: config.v.be,
          c_id: config.c.id, c_start: config.c.start, c_len: config.c.len, c_factor: config.c.factor, c_be: config.c.be, c_signed: config.c.signed,
          s_id: config.s.id, s_start: config.s.start, s_len: config.s.len, s_factor: config.s.factor, s_be: config.s.be,
          t_id: config.t.id, t_start: config.t.start, t_len: config.t.len, t_factor: config.t.factor, t_be: config.t.be,
          bat_b_offset: config.bat_b_offset,
          updated_at: new Date().toISOString()
        });
      if (error) throw error;
      alert('Configuración guardada correctamente');
    } catch (err) {
      console.error('Error saving CAN config:', err);
      alert('Error al guardar: ' + (err as any).message);
    } finally {
      setSaving(false);
    }
  }

  const renderRuleInputs = (label: string, key: keyof CanConfig, showSigned = false) => {
    const rule = config[key] as CanRule;
    return (
      <div className="border border-white/10 p-4 rounded-2xl mb-4 bg-zinc-950/50">
        <h3 className="font-bold mb-4 text-blue-400 text-xs tracking-widest uppercase">{label}</h3>
        <div className="grid grid-cols-2 md:grid-cols-5 gap-4">
          <div>
            <label className="text-[10px] text-zinc-500 block mb-1 uppercase font-bold">ID (Hex)</label>
            <input 
              type="text" 
              className="w-full p-2 bg-zinc-900 border border-white/10 rounded-lg text-white font-mono text-sm focus:border-blue-500 outline-none" 
              value={`0x${rule.id.toString(16).toUpperCase()}`}
              onChange={(e) => {
                const val = parseInt(e.target.value.replace('0x', ''), 16);
                if (!isNaN(val)) setConfig({ ...config, [key]: { ...rule, id: val } });
              }}
            />
          </div>
          <div>
            <label className="text-[10px] text-zinc-500 block mb-1 uppercase font-bold">Byte Inicio</label>
            <input 
              type="number" 
              className="w-full p-2 bg-zinc-900 border border-white/10 rounded-lg text-white font-mono text-sm focus:border-blue-500 outline-none" 
              value={rule.start}
              onChange={(e) => setConfig({ ...config, [key]: { ...rule, start: parseInt(e.target.value) || 0 } })}
            />
          </div>
          <div>
            <label className="text-[10px] text-zinc-500 block mb-1 uppercase font-bold">Longitud</label>
            <select 
              className="w-full p-2 bg-zinc-900 border border-white/10 rounded-lg text-white font-mono text-sm focus:border-blue-500 outline-none" 
              value={rule.len}
              onChange={(e) => setConfig({ ...config, [key]: { ...rule, len: parseInt(e.target.value) } })}
            >
              <option value={1}>1 Byte</option>
              <option value={2}>2 Bytes</option>
            </select>
          </div>
          <div>
            <label className="text-[10px] text-zinc-500 block mb-1 uppercase font-bold">Factor</label>
            <input 
              type="number" step="0.0001"
              className="w-full p-2 bg-zinc-900 border border-white/10 rounded-lg text-white font-mono text-sm focus:border-blue-500 outline-none" 
              value={rule.factor}
              onChange={(e) => setConfig({ ...config, [key]: { ...rule, factor: parseFloat(e.target.value) || 0 } })}
            />
          </div>
          <div className="flex flex-col justify-center gap-2">
            <label className="flex items-center gap-2 cursor-pointer group">
              <input 
                type="checkbox" 
                className="w-4 h-4 rounded border-white/10 bg-zinc-900 text-blue-500"
                checked={rule.be}
                onChange={(e) => setConfig({ ...config, [key]: { ...rule, be: e.target.checked } })}
              />
              <span className="text-[10px] text-zinc-400 group-hover:text-white uppercase font-bold">Big Endian</span>
            </label>
            {showSigned && (
              <label className="flex items-center gap-2 cursor-pointer group">
                <input 
                  type="checkbox" 
                  className="w-4 h-4 rounded border-white/10 bg-zinc-900 text-blue-500"
                  checked={rule.signed}
                  onChange={(e) => setConfig({ ...config, [key]: { ...rule, signed: e.target.checked } })}
                />
                <span className="text-[10px] text-zinc-400 group-hover:text-white uppercase font-bold">Signed</span>
              </label>
            )}
          </div>
        </div>
      </div>
    );
  };

  return (
    <div className="bg-zinc-900 border border-white/10 shadow-2xl rounded-3xl p-8 max-w-5xl mx-auto">
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-xl font-black text-white flex items-center gap-3 tracking-tighter uppercase">
            <div className="p-2 bg-blue-500/10 rounded-xl text-blue-400 border border-blue-500/20">
              <Zap size={20} fill="currentColor" />
            </div>
            SELECTOR CAN MANUAL
          </h2>
          <p className="text-[10px] text-zinc-500 mt-1 font-mono tracking-widest">DEFINICIÓN DINÁMICA DE TRAMAS</p>
        </div>
      </div>
      
      <div className="space-y-2">
        {renderRuleInputs('Voltaje de Batería', 'v')}
        {renderRuleInputs('Corriente (Amperios)', 'c', true)}
        {renderRuleInputs('Estado de Carga (SOC)', 's')}
        {renderRuleInputs('Temperatura de Celdas', 't')}
      </div>

      <div className="border border-white/10 p-6 rounded-2xl mb-8 bg-zinc-950/50 flex items-center justify-between">
        <div>
          <h3 className="font-bold text-white text-xs tracking-widest uppercase mb-1">Dual Battery Offset</h3>
          <p className="text-[10px] text-zinc-500 font-mono">ID_BAT_B = ID_BAT_A + OFFSET</p>
        </div>
        <input 
          type="number" 
          className="w-24 p-3 bg-zinc-900 border border-white/10 rounded-xl text-white font-mono text-center focus:border-blue-500 outline-none" 
          value={config.bat_b_offset}
          onChange={(e) => setConfig({ ...config, bat_b_offset: parseInt(e.target.value) || 0 })}
        />
      </div>

      <button 
        onClick={saveConfig}
        disabled={saving}
        className="w-full bg-blue-600 hover:bg-blue-500 text-white font-black py-4 rounded-2xl transition-all disabled:opacity-50 shadow-[0_0_20px_rgba(37,99,235,0.2)] text-xs tracking-[0.2em] uppercase"
      >
        {saving ? 'GUARDANDO_CAMBIOS...' : 'GUARDAR_CONFIGURACIÓN'}
      </button>
    </div>
  );
}
