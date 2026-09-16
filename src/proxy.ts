import { createServerClient } from '@supabase/ssr';
import { NextResponse, type NextRequest } from 'next/server';

// Protege todo el portal detrás de un login de Supabase Auth — sin sesión,
// redirige a /login. El firmware NUNCA pasa por aquí (habla directo con
// la API REST de Supabase con la anon key, no visita esta web), así que
// esto no le afecta: solo protege quién puede VER el dashboard. Lo que de
// verdad protege los datos son las políticas RLS de supabase/schema.sql
// (select anon -> authenticated en telemetry/trips) — este proxy es el
// complemento de UX (una pantalla de login en vez de un dashboard
// vacío/roto por RLS).
//
// "proxy" (no "middleware"): convención renombrada en Next.js 16, mismo
// sitio/firma que antes — ver https://nextjs.org/docs/messages/middleware-to-proxy
export async function proxy(request: NextRequest) {
  const url = process.env.NEXT_PUBLIC_SUPABASE_URL;
  const key = process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY;

  // Sin credenciales de Supabase configuradas (p.ej. en local sin
  // .env.local todavía) no hay sesión que comprobar — se deja pasar para
  // no bloquear el propio desarrollo/primer arranque; DashboardContent ya
  // muestra su aviso de "Database Offline" en ese caso.
  if (!url || !key) return NextResponse.next();

  let response = NextResponse.next({ request });

  const supabase = createServerClient(url, key, {
    cookies: {
      getAll() {
        return request.cookies.getAll();
      },
      setAll(cookiesToSet) {
        cookiesToSet.forEach(({ name, value }) => request.cookies.set(name, value));
        response = NextResponse.next({ request });
        cookiesToSet.forEach(({ name, value, options }) =>
          response.cookies.set(name, value, options)
        );
      },
    },
  });

  const { data: { user } } = await supabase.auth.getUser();

  if (!user && !request.nextUrl.pathname.startsWith('/login')) {
    const loginUrl = request.nextUrl.clone();
    loginUrl.pathname = '/login';
    return NextResponse.redirect(loginUrl);
  }

  return response;
}

export const config = {
  matcher: [
    '/((?!_next/static|_next/image|favicon.ico|.*\\.(?:svg|png|jpg|jpeg|gif|webp)$).*)',
  ],
};
