'use client';

import { useEffect, useState, useTransition } from 'react';
import { useRouter } from 'next/navigation';
import { Plus, Pencil, Trash2, X, ArrowLeft, Cpu, AlertTriangle, Radio, Send } from 'lucide-react';
import Link from 'next/link';
import {
  CANSignalRow,
  addSignal,
  updateSignal,
  deleteSignal,
} from '@/app/actions/signals';
import { KNOWN_SIGNALS } from '@/lib/known-signals';

// ── helpers ──────────────────────────────────────────────────────────────────

const toHex = (n: number, pad = 3) =>
  `0x${n.toString(16).toUpperCase().padStart(pad, '0')}`;

const parseHex = (s: string): number => {
  const n = parseInt(s.replace(/^0[xX]/, ''), 16);
  return isNaN(n) ? 0 : n;
};

type FormState = {
  direction: 'rx' | 'tx';
  signal_name: string;
  frame_id: string;
  tx_interval_ms: string;
  dual_mode: boolean;
  byte_start: string;
  byte_length: string;
  bit_mask: string;
  big_endian: boolean;
  is_signed: boolean;
  scale: string;
  offset_val: string;
};

const DEFAULT_FORM: FormState = {
  direction: 'rx',
  signal_name: '',
  frame_id: '',
  tx_interval_ms: '200',
  dual_mode: false,
  byte_start: '0',
  byte_length: '1',
  bit_mask: '0',
  big_endian: true,
  is_signed: false,
  scale: '1',
  offset_val: '0',
};

const rowToForm = (row: CANSignalRow): FormState => ({
  direction: row.direction,
  signal_name: row.signal_name,
  frame_id: toHex(row.frame_id),
  tx_interval_ms: String(row.tx_interval_ms ?? 200),
  dual_mode: row.dual_mode,
  byte_start: String(row.byte_start),
  byte_length: String(row.byte_length),
  bit_mask: row.bit_mask ? toHex(row.bit_mask, 2) : '0',
  big_endian: row.big_endian,
  is_signed: row.is_signed,
  scale: String(row.scale),
  offset_val: String(row.offset_val),
});

// ── sub-components ────────────────────────────────────────────────────────────

function Field({
  label, hint, children,
}: { label: string; hint?: string; children: React.ReactNode }) {
  return (
    <div>
      <label className="block text-[10px] font-black tracking-widest text-zinc-500 uppercase mb-1">
        {label}
        {hint && <span className="ml-2 normal-case font-normal text-zinc-600">{hint}</span>}
      </label>
      {children}
    </div>
  );
}

const inputCls =
  'w-full bg-zinc-950/60 border border-white/10 rounded-xl px-3 py-2 text-sm text-white font-mono placeholder-zinc-600 focus:outline-none focus:border-cyan-500/50 transition-colors';

const checkCls =
  'w-4 h-4 rounded bg-zinc-950 border border-white/20 accent-cyan-500 cursor-pointer';

// ── main component ────────────────────────────────────────────────────────────

interface Props {
  vehicleId: string | undefined;
  initialSignals: CANSignalRow[];
}

export default function SignalsConfig({ vehicleId, initialSignals }: Props) {
  const router = useRouter();
  const [signals, setSignals] = useState(initialSignals);
  const [showModal, setShowModal] = useState(false);
  const [editTarget, setEditTarget] = useState<CANSignalRow | null>(null);
  const [form, setForm] = useState<FormState>(DEFAULT_FORM);
  const [formError, setFormError] = useState<string | null>(null);
  const [isPending, startTransition] = useTransition();

  // Sync with server after router.refresh()
  useEffect(() => { setSignals(initialSignals); }, [initialSignals]);

  const set = (field: keyof FormState) =>
    (e: React.ChangeEvent<HTMLInputElement>) =>
      setForm(prev => ({ ...prev, [field]: e.target.type === 'checkbox' ? e.target.checked : e.target.value }));

  const openAdd = () => {
    setEditTarget(null);
    setForm(DEFAULT_FORM);
    setFormError(null);
    setShowModal(true);
  };

  const openEdit = (row: CANSignalRow) => {
    setEditTarget(row);
    setForm(rowToForm(row));
    setFormError(null);
    setShowModal(true);
  };

  const handleDelete = (row: CANSignalRow) => {
    if (!confirm(`¿Borrar señal "${row.signal_name}"?`)) return;
    startTransition(async () => {
      await deleteSignal(row.id);
      setSignals(prev => prev.filter(s => s.id !== row.id));
      router.refresh();
    });
  };

  const handleSubmit = () => {
    if (!vehicleId) return;
    if (!form.signal_name.trim()) { setFormError('signal_name es obligatorio'); return; }
    const frameId = parseHex(form.frame_id);
    if (!frameId) { setFormError('Frame ID inválido (ejemplo: 0x504)'); return; }

    const signal: Omit<CANSignalRow, 'id'> = {
      vehicle_id: vehicleId,
      direction: form.direction,
      signal_name: form.signal_name.trim(),
      frame_id: frameId,
      tx_interval_ms: parseInt(form.tx_interval_ms) || 200,
      dual_mode: form.direction === 'tx' ? false : form.dual_mode,
      byte_start: parseInt(form.byte_start) || 0,
      byte_length: parseInt(form.byte_length) || 1,
      bit_mask: parseHex(form.bit_mask),
      big_endian: form.big_endian,
      is_signed: form.is_signed,
      scale: parseFloat(form.scale) || 1,
      offset_val: parseFloat(form.offset_val) || 0,
    };

    startTransition(async () => {
      try {
        if (editTarget) {
          await updateSignal(editTarget.id, signal);
          setSignals(prev => prev.map(s => s.id === editTarget.id ? { ...signal, id: editTarget.id } : s));
        } else {
          await addSignal(signal);
        }
        setShowModal(false);
        router.refresh();
      } catch (e: unknown) {
        setFormError(e instanceof Error ? e.message : 'Error desconocido');
      }
    });
  };

  // ── render ──────────────────────────────────────────────────────────────────

  return (
    <div className="min-h-screen bg-black text-zinc-300 font-sans pb-12">
      <div className="fixed inset-0 bg-[radial-gradient(circle_at_50%_-20%,_#1e1b4b_0%,_#000_80%)] pointer-events-none" />

      <div className="relative max-w-6xl mx-auto p-4 md:p-8">

        {/* Header */}
        <header className="flex items-center justify-between mb-10 gap-4">
          <div className="flex items-center gap-4">
            <Link href="/" className="p-2 rounded-xl bg-zinc-900 border border-white/10 text-zinc-400 hover:text-white transition-colors">
              <ArrowLeft size={18} />
            </Link>
            <div className="flex items-center gap-3">
              <div className="p-2 rounded-xl bg-cyan-500/10 text-cyan-400">
                <Cpu size={20} />
              </div>
              <div>
                <h1 className="text-lg font-black text-white tracking-tight">CAN SIGNALS</h1>
                <p className="text-[10px] font-mono text-zinc-500 uppercase">
                  {vehicleId ? `vehicle: ${vehicleId.slice(0, 8)}…` : 'sin vehicle_id configurado'}
                </p>
              </div>
            </div>
          </div>

          {vehicleId && (
            <button
              onClick={openAdd}
              className="flex items-center gap-2 px-4 py-2 bg-cyan-500/20 hover:bg-cyan-500/30 border border-cyan-500/30 text-cyan-400 rounded-2xl text-sm font-bold transition-colors"
            >
              <Plus size={16} />
              <span className="hidden sm:inline">Nueva señal</span>
            </button>
          )}
        </header>

        {/* Sin vehicle_id */}
        {!vehicleId && (
          <div className="p-6 bg-amber-500/10 border border-amber-500/20 rounded-3xl flex items-start gap-4 mb-8">
            <AlertTriangle size={20} className="text-amber-400 mt-0.5 shrink-0" />
            <div>
              <p className="text-sm font-bold text-amber-400 mb-1">NEXT_PUBLIC_VEHICLE_ID no configurado</p>
              <p className="text-xs text-amber-400/70">
                Añade{' '}
                <code className="bg-amber-500/10 px-1 rounded font-mono">NEXT_PUBLIC_VEHICLE_ID=&lt;tu-uuid&gt;</code>
                {' '}a <code className="bg-amber-500/10 px-1 rounded font-mono">.env.local</code> y reinicia el servidor.
              </p>
              <p className="text-[10px] text-amber-400/50 mt-1">El UUID es el mismo que VEHICLE_ID en config.h del firmware.</p>
            </div>
          </div>
        )}


        {/* Tabla */}
        {vehicleId && (
          <div className="bg-zinc-900/40 backdrop-blur-xl border border-white/10 rounded-3xl overflow-hidden shadow-2xl">
            <div className="p-5 border-b border-white/5 flex items-center justify-between">
              <span className="text-sm font-bold text-white">
                {signals.length} señal{signals.length !== 1 ? 'es' : ''} configurada{signals.length !== 1 ? 's' : ''}
              </span>
              <span className="text-[10px] font-mono text-zinc-600 uppercase">El firmware descarga esta tabla al arrancar</span>
            </div>

            {signals.length === 0 ? (
              <div className="py-20 flex flex-col items-center justify-center text-zinc-600">
                <Cpu size={40} className="mb-3 opacity-20" />
                <p className="text-[10px] font-black tracking-widest uppercase">Sin señales</p>
                <p className="text-[10px] text-zinc-700 mt-1">Pulsa "Nueva señal" para añadir la primera</p>
              </div>
            ) : (
              <div className="overflow-x-auto">
                <table className="w-full text-sm">
                  <thead>
                    <tr className="border-b border-white/5 text-[9px] font-black tracking-widest text-zinc-600 uppercase">
                      <th className="px-4 py-3 text-left">Dirección</th>
                      <th className="px-4 py-3 text-left">Señal</th>
                      <th className="px-4 py-3 text-left">Frame ID</th>
                      <th className="px-4 py-3 text-left">Byte/Mask</th>
                      <th className="px-4 py-3 text-left">Flags</th>
                      <th className="px-4 py-3 text-left">Scale × + Offset</th>
                      <th className="px-4 py-3 text-right"></th>
                    </tr>
                  </thead>
                  <tbody>
                    {signals.map((sig, i) => {
                      const byteInfo = sig.bit_mask
                        ? `b${sig.byte_start} & ${toHex(sig.bit_mask, 2)}`
                        : sig.byte_length === 1
                          ? `b${sig.byte_start}`
                          : `b${sig.byte_start}–b${sig.byte_start + sig.byte_length - 1}`;
                      const isTx = sig.direction === 'tx';
                      return (
                        <tr
                          key={sig.id}
                          className={`border-b border-white/5 hover:bg-zinc-800/30 transition-colors ${i % 2 === 0 ? '' : 'bg-zinc-900/20'}`}
                        >
                          <td className="px-4 py-3">
                            {isTx ? (
                              <span className="inline-flex items-center gap-1 text-[9px] bg-orange-500/15 text-orange-400 border border-orange-500/30 px-2 py-0.5 rounded-full font-black">
                                <Send size={9} />TX
                              </span>
                            ) : (
                              <span className="inline-flex items-center gap-1 text-[9px] bg-cyan-500/10 text-cyan-500 border border-cyan-500/20 px-2 py-0.5 rounded-full font-black">
                                <Radio size={9} />RX
                              </span>
                            )}
                          </td>
                          <td className="px-4 py-3 font-mono text-cyan-400 font-bold text-xs">
                            {sig.signal_name}
                          </td>
                          <td className="px-4 py-3 font-mono text-zinc-300 text-xs">
                            {toHex(sig.frame_id)}
                            {sig.dual_mode && (
                              <span className="ml-1.5 text-[9px] bg-violet-500/20 text-violet-400 border border-violet-500/30 px-1.5 py-0.5 rounded-full font-bold">DUAL</span>
                            )}
                            {isTx && (
                              <span className="ml-1.5 text-[9px] text-zinc-600">
                                /{sig.tx_interval_ms}ms
                              </span>
                            )}
                          </td>
                          <td className="px-4 py-3 font-mono text-zinc-400 text-xs">{byteInfo}</td>
                          <td className="px-4 py-3 text-xs">
                            <div className="flex gap-1 flex-wrap">
                              {sig.big_endian && <span className="text-[9px] bg-zinc-800 text-zinc-500 px-1.5 py-0.5 rounded-full">BE</span>}
                              {sig.is_signed && <span className="text-[9px] bg-zinc-800 text-zinc-500 px-1.5 py-0.5 rounded-full">±</span>}
                            </div>
                          </td>
                          <td className="px-4 py-3 font-mono text-zinc-500 text-xs">
                            ×{sig.scale}{sig.offset_val !== 0 ? ` ${sig.offset_val >= 0 ? '+' : ''}${sig.offset_val}` : ''}
                          </td>
                          <td className="px-4 py-3">
                            <div className="flex items-center justify-end gap-2">
                              <button
                                onClick={() => openEdit(sig)}
                                className="p-1.5 rounded-lg text-zinc-500 hover:text-cyan-400 hover:bg-cyan-500/10 transition-colors"
                              >
                                <Pencil size={14} />
                              </button>
                              <button
                                onClick={() => handleDelete(sig)}
                                disabled={isPending}
                                className="p-1.5 rounded-lg text-zinc-500 hover:text-red-400 hover:bg-red-500/10 transition-colors disabled:opacity-30"
                              >
                                <Trash2 size={14} />
                              </button>
                            </div>
                          </td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              </div>
            )}
          </div>
        )}

        {/* Nota firmware */}
        {vehicleId && signals.length > 0 && (
          <p className="mt-4 text-[10px] text-zinc-700 text-center">
            Los cambios se aplican en el próximo arranque del firmware (o al pulsar reset en el ESP32).
          </p>
        )}
      </div>

      {/* Modal add / edit */}
      {showModal && (
        <div className="fixed inset-0 z-50 flex items-center justify-center p-4">
          <div className="absolute inset-0 bg-black/70 backdrop-blur-sm" onClick={() => !isPending && setShowModal(false)} />
          <div className="relative w-full max-w-lg bg-zinc-950 border border-white/10 rounded-3xl shadow-2xl overflow-y-auto max-h-[90vh]">

            {/* Modal header */}
            <div className="sticky top-0 bg-zinc-950 border-b border-white/5 px-6 py-4 flex items-center justify-between z-10">
              <h2 className="text-sm font-black text-white uppercase tracking-widest">
                {editTarget ? 'Editar señal' : 'Nueva señal'}
              </h2>
              <button onClick={() => !isPending && setShowModal(false)} className="p-1 rounded-lg text-zinc-500 hover:text-white transition-colors">
                <X size={18} />
              </button>
            </div>

            <div className="p-6 space-y-5">

              {/* Dirección RX / TX */}
              <Field label="Dirección">
                <div className="flex gap-2 mt-0.5">
                  <button
                    type="button"
                    onClick={() => setForm(prev => ({ ...prev, direction: 'rx' }))}
                    className={`flex-1 flex items-center justify-center gap-2 py-2.5 rounded-xl text-xs font-bold border transition-colors ${
                      form.direction === 'rx'
                        ? 'bg-cyan-500/20 border-cyan-500/30 text-cyan-400'
                        : 'bg-zinc-950/60 border-white/10 text-zinc-500 hover:text-zinc-300'
                    }`}
                  >
                    <Radio size={12} />
                    RX — Recibir
                  </button>
                  <button
                    type="button"
                    onClick={() => setForm(prev => ({ ...prev, direction: 'tx' }))}
                    className={`flex-1 flex items-center justify-center gap-2 py-2.5 rounded-xl text-xs font-bold border transition-colors ${
                      form.direction === 'tx'
                        ? 'bg-orange-500/20 border-orange-500/30 text-orange-400'
                        : 'bg-zinc-950/60 border-white/10 text-zinc-500 hover:text-zinc-300'
                    }`}
                  >
                    <Send size={12} />
                    TX — Enviar
                  </button>
                </div>
                <p className="text-[9px] text-zinc-600 mt-1.5">
                  {form.direction === 'rx'
                    ? 'El ESP32 escucha este frame e interpreta el valor.'
                    : 'El ESP32 construye y emite este frame periódicamente. El valor del byte lo determina el firmware según el signal_name (ej: clock_hours → hora NITZ).'}
                </p>
              </Field>

              {/* TX: nota de soporte de firmware */}
              {form.direction === 'tx' && (
                <div className="p-3 bg-orange-500/10 border border-orange-500/20 rounded-xl space-y-1.5">
                  <p className="text-[10px] font-bold text-orange-400">Variables dinámicas soportadas en TX:</p>
                  <div className="flex flex-wrap gap-1">
                    {['clock_hours','clock_minutes'].map(n => (
                      <code key={n} className="text-[9px] font-mono bg-orange-500/10 text-orange-300 px-1.5 py-0.5 rounded border border-orange-500/20">{n}</code>
                    ))}
                  </div>
                  <p className="text-[9px] text-zinc-500 leading-relaxed">
                    Cualquier otro nombre envía un byte fijo cuyo valor es el campo <code className="font-mono">Offset</code>.
                    Cada señal define un byte dentro de la trama; con el mismo Frame ID se construyen todas juntas.
                  </p>
                </div>
              )}

              {/* Nombre de señal con sugerencias de variables conocidas */}
              <div className="grid grid-cols-2 gap-4">
                <Field label="Variable del portal" hint="elige o escribe">
                  <input
                    list="known-signals-list"
                    className={inputCls}
                    value={form.signal_name}
                    onChange={set('signal_name')}
                    placeholder="soc, clock_hours…"
                    autoFocus
                  />
                  <datalist id="known-signals-list">
                    {KNOWN_SIGNALS.map(s => (
                      <option key={s.name} value={s.name} />
                    ))}
                  </datalist>
                  {(() => {
                    const known = KNOWN_SIGNALS.find(s => s.name === form.signal_name);
                    return known ? (
                      <div className="mt-1.5 bg-cyan-500/5 border border-cyan-500/10 rounded-lg px-3 py-2 space-y-1">
                        <p className="text-[10px] font-bold text-cyan-400">
                          {known.summary}{known.unit ? ` — ${known.unit}` : ''}
                        </p>
                        <p className="text-[10px] text-zinc-400 leading-relaxed">{known.hint}</p>
                      </div>
                    ) : null;
                  })()}
                </Field>
                <Field label="Frame ID" hint="hex">
                  <input className={inputCls} value={form.frame_id} onChange={set('frame_id')}
                    placeholder="0x…" />
                </Field>
              </div>

              {/* TX interval (solo TX) */}
              {form.direction === 'tx' && (
                <Field label="Intervalo TX" hint="ms">
                  <input
                    type="number"
                    min={10}
                    className={inputCls}
                    value={form.tx_interval_ms}
                    onChange={set('tx_interval_ms')}
                    placeholder="200"
                  />
                </Field>
              )}

              {/* Dual mode (solo RX) */}
              {form.direction === 'rx' && (
                <Field label="Modo dual" hint="para vehículos que cambian el ID según el modo de batería">
                  <label className="flex items-center gap-2 cursor-pointer">
                    <input type="checkbox" className={checkCls} checked={form.dual_mode} onChange={set('dual_mode')} />
                    <span className="text-sm text-zinc-400">
                      Escuchar también <span className="font-mono text-zinc-300">frame_id + 1</span>
                    </span>
                  </label>
                  <p className="text-[9px] text-zinc-600 mt-1">
                    Actívalo si tu vehículo envía la misma señal alternando entre dos IDs consecutivos según el estado de la batería. En la mayoría de vehículos no es necesario.
                  </p>
                </Field>
              )}

              <hr className="border-white/5" />

              {/* Extracción / Posición de bytes */}
              <p className="text-[9px] font-black tracking-widest text-zinc-600 uppercase">
                {form.direction === 'rx' ? 'Extracción de datos' : 'Posición en la trama TX'}
              </p>

              <div className="grid grid-cols-3 gap-4">
                <Field label="Byte inicio" hint="0–7">
                  <input type="number" min={0} max={7} className={inputCls} value={form.byte_start} onChange={set('byte_start')} />
                </Field>
                <Field label="Nº bytes" hint="1–8">
                  <input type="number" min={1} max={8} className={inputCls} value={form.byte_length} onChange={set('byte_length')} />
                </Field>
                {form.direction === 'rx' && (
                  <Field label="Bit mask" hint="0=desact.">
                    <input className={inputCls} value={form.bit_mask} onChange={set('bit_mask')}
                      placeholder="0x10" />
                  </Field>
                )}
              </div>

              <div className="flex gap-6">
                <Field label="Big endian">
                  <label className="flex items-center gap-2 cursor-pointer mt-1">
                    <input type="checkbox" className={checkCls} checked={form.big_endian} onChange={set('big_endian')} />
                    <span className="text-sm text-zinc-400">MSB primero</span>
                  </label>
                </Field>
                {form.direction === 'rx' && (
                  <Field label="Con signo">
                    <label className="flex items-center gap-2 cursor-pointer mt-1">
                      <input type="checkbox" className={checkCls} checked={form.is_signed} onChange={set('is_signed')} />
                      <span className="text-sm text-zinc-400">Entero con signo</span>
                    </label>
                  </Field>
                )}
              </div>

              <hr className="border-white/5" />

              {/* Conversión */}
              <p className="text-[9px] font-black tracking-widest text-zinc-600 uppercase">Conversión  →  valor = raw × scale + offset</p>

              <div className="grid grid-cols-2 gap-4">
                <Field label="Scale" hint="ej: 0.1 para ×0.1">
                  <input className={inputCls} value={form.scale} onChange={set('scale')} placeholder="1" />
                </Field>
                <Field label="Offset">
                  <input className={inputCls} value={form.offset_val} onChange={set('offset_val')} placeholder="0" />
                </Field>
              </div>

              {formError && (
                <div className="flex items-center gap-2 p-3 bg-red-500/10 border border-red-500/20 rounded-xl text-xs text-red-400">
                  <AlertTriangle size={14} className="shrink-0" />
                  {formError}
                </div>
              )}

              <div className="flex gap-3 pt-2">
                <button
                  onClick={() => !isPending && setShowModal(false)}
                  className="flex-1 py-3 rounded-2xl border border-white/10 text-zinc-400 hover:text-white text-sm font-bold transition-colors"
                >
                  Cancelar
                </button>
                <button
                  onClick={handleSubmit}
                  disabled={isPending}
                  className="flex-1 py-3 rounded-2xl bg-cyan-500/20 hover:bg-cyan-500/30 border border-cyan-500/30 text-cyan-400 text-sm font-bold transition-colors disabled:opacity-40"
                >
                  {isPending ? 'Guardando…' : editTarget ? 'Guardar cambios' : 'Añadir señal'}
                </button>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
