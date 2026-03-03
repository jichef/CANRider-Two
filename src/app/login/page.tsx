'use client';

import { useState } from 'react';
import { createClient } from '@/lib/supabase';
import { useRouter } from 'next/navigation';
import { ShieldCheck, Zap, Lock, Mail } from 'lucide-react';

export default function LoginPage() {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const router = useRouter();
  const supabase = createClient();

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    setLoading(true);
    setError(null);

    if (!supabase) {
      setError('Error al conectar con Supabase');
      setLoading(false);
      return;
    }

    const { error: authError } = await supabase.auth.signInWithPassword({
      email,
      password,
    });

    if (authError) {
      setError(authError.message === 'Invalid login credentials' ? 'Credenciales incorrectas' : authError.message);
      setLoading(false);
    } else {
      router.refresh();
      router.push('/');
    }
  };

  return (
    <div className="min-h-screen bg-black flex items-center justify-center p-4">
      {/* Background decoration */}
      <div className="absolute inset-0 overflow-hidden pointer-events-none">
        <div className="absolute top-1/4 left-1/4 w-96 h-96 bg-blue-600/10 rounded-full blur-[120px] animate-pulse" />
        <div className="absolute bottom-1/4 right-1/4 w-96 h-96 bg-cyan-600/10 rounded-full blur-[120px] animate-pulse delay-700" />
      </div>

      <div className="w-full max-w-md relative animate-in fade-in slide-in-from-bottom-4 duration-1000">
        <div className="bg-zinc-900/40 backdrop-blur-2xl border border-white/10 rounded-[2.5rem] p-10 shadow-2xl overflow-hidden relative group">
          
          {/* Scanline effect */}
          <div className="absolute inset-0 pointer-events-none bg-[linear-gradient(rgba(18,16,16,0)_50%,rgba(0,0,0,0.1)_50%)] bg-[length:100%_4px] z-10 opacity-20" />
          
          <div className="relative z-20 text-center mb-10">
            <div className="inline-flex p-4 rounded-3xl bg-blue-600/10 text-blue-400 border border-blue-500/20 mb-6 shadow-[0_0_30px_rgba(37,99,235,0.2)]">
              <ShieldCheck size={40} />
            </div>
            <h1 className="text-3xl font-black text-white tracking-tighter uppercase mb-2">Acceso Restringido</h1>
            <p className="text-[10px] text-zinc-500 font-mono tracking-[0.3em] uppercase">CANRIDER_SECURE_PORTAL_V2</p>
          </div>

          <form onSubmit={handleLogin} className="space-y-6 relative z-20">
            <div className="space-y-2">
              <label className="text-[10px] font-black text-zinc-500 tracking-widest uppercase ml-1">Email</label>
              <div className="relative">
                <div className="absolute left-4 top-1/2 -translate-y-1/2 text-zinc-500">
                  <Mail size={18} />
                </div>
                <input
                  type="email"
                  value={email}
                  onChange={(e) => setEmail(e.target.value)}
                  className="w-full bg-zinc-950/50 border border-white/10 rounded-2xl py-4 pl-12 pr-4 text-white placeholder:text-zinc-700 focus:border-blue-500/50 focus:bg-zinc-900/80 outline-none transition-all font-mono text-sm"
                  placeholder="admin@canrider.io"
                  required
                />
              </div>
            </div>

            <div className="space-y-2">
              <label className="text-[10px] font-black text-zinc-500 tracking-widest uppercase ml-1">Contraseña</label>
              <div className="relative">
                <div className="absolute left-4 top-1/2 -translate-y-1/2 text-zinc-500">
                  <Lock size={18} />
                </div>
                <input
                  type="password"
                  value={password}
                  onChange={(e) => setPassword(e.target.value)}
                  className="w-full bg-zinc-950/50 border border-white/10 rounded-2xl py-4 pl-12 pr-4 text-white placeholder:text-zinc-700 focus:border-blue-500/50 focus:bg-zinc-900/80 outline-none transition-all font-mono text-sm"
                  placeholder="••••••••"
                  required
                />
              </div>
            </div>

            {error && (
              <div className="p-4 rounded-2xl bg-red-500/10 border border-red-500/20 text-red-500 text-[10px] font-black tracking-widest uppercase text-center animate-shake">
                {error}
              </div>
            )}

            <button
              type="submit"
              disabled={loading}
              className="w-full bg-blue-600 hover:bg-blue-500 text-white font-black py-5 rounded-2xl transition-all disabled:opacity-50 shadow-[0_0_20px_rgba(37,99,235,0.2)] text-xs tracking-[0.2em] uppercase flex items-center justify-center gap-2 group"
            >
              {loading ? (
                <>
                  <Zap size={16} className="animate-spin" />
                  AUTENTICANDO...
                </>
              ) : (
                <>
                  <Zap size={16} className="group-hover:animate-pulse" />
                  ACCEDER_AL_SISTEMA
                </>
              )}
            </button>

            {/* Botón temporal de acceso directo */}
            <button
              type="button"
              onClick={() => router.push('/')}
              className="w-full bg-zinc-800 hover:bg-zinc-700 text-zinc-400 font-bold py-4 rounded-2xl transition-all text-[10px] tracking-[0.2em] uppercase border border-white/5"
            >
              ACCESO_TEMPORAL_SIN_PASSWORD
            </button>
          </form>

          <div className="mt-10 pt-8 border-t border-white/5 text-center relative z-20">
            <p className="text-[9px] text-zinc-600 font-mono tracking-widest leading-loose uppercase">
              Solo personal autorizado.<br />
              Cualquier intento de acceso no autorizado será registrado.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}
