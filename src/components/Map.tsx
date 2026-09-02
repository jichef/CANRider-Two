'use client';

import { MapContainer, TileLayer, Marker, Popup, Polyline, useMap } from 'react-leaflet';
import 'leaflet/dist/leaflet.css';
import L from 'leaflet';
import { useEffect, useState } from 'react';

// Componente para seguir la posición en tiempo real
function LiveFollower({ center }: { center: [number, number] }) {
  const map = useMap();
  useEffect(() => {
    map.panTo(center);
  }, [center, map]);
  return null;
}

// Componente para ajustar la vista del mapa automáticamente a un recorrido
function MapResizer({ bounds }: { bounds?: L.LatLngBoundsExpression }) {
  const map = useMap();
  useEffect(() => {
    if (bounds) {
      map.fitBounds(bounds, { padding: [50, 50], maxZoom: 18 });
    }
  }, [bounds, map]);
  return null;
}

// Gradiente estilo Garmin/Strava: azul (lento) → verde → amarillo → rojo (rápido).
// El ratio es relativo a la velocidad máxima de ESE viaje, no una escala fija en
// km/h — así funciona igual de bien en un ciclomotor que en una moto más rápida.
function speedColor(ratio: number): string {
  const stops = [
    [59, 130, 246],   // azul
    [34, 197, 94],    // verde
    [250, 204, 21],   // amarillo
    [239, 68, 68],    // rojo
  ];
  const clamped = Math.max(0, Math.min(1, ratio));
  const seg = clamped * (stops.length - 1);
  const idx = Math.min(Math.floor(seg), stops.length - 2);
  const t = seg - idx;
  const [r1, g1, b1] = stops[idx];
  const [r2, g2, b2] = stops[idx + 1];
  const r = Math.round(r1 + (r2 - r1) * t);
  const g = Math.round(g1 + (g2 - g1) * t);
  const b = Math.round(b1 + (b2 - b1) * t);
  return `rgb(${r},${g},${b})`;
}

interface MapProps {
  center: [number, number];
  zoom?: number;
  /** Traza real del viaje: [lat, lon, velocidad_kmh] por punto */
  track?: [number, number, number][];
}

export default function Map({ center, zoom = 15, track }: MapProps) {
  const [icons, setIcons] = useState<{ start: L.Icon, end: L.Icon } | null>(null);

  useEffect(() => {
    // Crear iconos solo en el cliente
    const startIcon = L.icon({
      iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-2x-blue.png',
      shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/0.7.7/images/marker-shadow.png',
      iconSize: [25, 41],
      iconAnchor: [12, 41],
      popupAnchor: [1, -34],
      shadowSize: [41, 41]
    });

    const endIcon = L.icon({
      iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-2x-red.png',
      shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/0.7.7/images/marker-shadow.png',
      iconSize: [25, 41],
      iconAnchor: [12, 41],
      popupAnchor: [1, -34],
      shadowSize: [41, 41]
    });

    setIcons({ start: startIcon, end: endIcon });
  }, []);

  if (!icons) return (
    <div className="h-full w-full bg-zinc-900 flex items-center justify-center">
      <span className="text-zinc-500 font-mono text-[10px] tracking-widest">LOADING_ASSETS...</span>
    </div>
  );

  const hasTrack = track && track.length > 1;
  const positions: [number, number][] = hasTrack ? track!.map(([lat, lon]) => [lat, lon]) : [];
  const bounds = hasTrack ? L.latLngBounds(positions) : undefined;
  const maxSpeed = hasTrack ? Math.max(1, ...track!.map(([, , v]) => v)) : 1;

  return (
    <MapContainer
      center={center}
      zoom={zoom}
      style={{ height: '100%', width: '100%' }}
      scrollWheelZoom={false}
    >
      <TileLayer
        attribution='Tiles &copy; Esri &mdash; Source: Esri, i-cubed, USDA, USGS, AEX, GeoEye, Getmapping, Aerogrid, IGN, IGP, UPR-EGP, and the GIS User Community'
        url="https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"
      />

      {!hasTrack && (
        <>
          <Marker position={center} icon={icons.start}>
            <Popup>Ubicación actual</Popup>
          </Marker>
          <LiveFollower center={center} />
        </>
      )}

      {hasTrack && (
        <>
          {/* Marcador de INICIO */}
          <Marker position={positions[0]} icon={icons.start}>
            <Popup>Punto de inicio</Popup>
          </Marker>

          {/* Marcador de FIN */}
          <Marker position={positions[positions.length - 1]} icon={icons.end}>
            <Popup>Punto de destino</Popup>
          </Marker>

          {/* Un segmento por par de puntos consecutivos, coloreado según la
              velocidad media de ese tramo — efecto de traza tipo Garmin/Strava */}
          {track!.slice(0, -1).map(([lat1, lon1, v1], i) => {
            const [lat2, lon2, v2] = track![i + 1];
            const avgSpeed = (v1 + v2) / 2;
            return (
              <Polyline
                key={i}
                positions={[[lat1, lon1], [lat2, lon2]]}
                pathOptions={{
                  color: speedColor(avgSpeed / maxSpeed),
                  weight: 6,
                  opacity: 0.9,
                  lineJoin: 'round',
                  lineCap: 'round',
                }}
              />
            );
          })}
          <MapResizer bounds={bounds} />
        </>
      )}
    </MapContainer>
  );
}
