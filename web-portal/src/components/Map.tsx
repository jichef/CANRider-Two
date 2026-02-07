'use client';

import { MapContainer, TileLayer, Marker, Popup, Polyline, useMap } from 'react-leaflet';
import 'leaflet/dist/leaflet.css';
import L from 'leaflet';
import { useEffect } from 'react';

// Icono por defecto (Azul)
const StartIcon = L.icon({
  iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-2x-blue.png',
  shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/0.7.7/images/marker-shadow.png',
  iconSize: [25, 41],
  iconAnchor: [12, 41],
  popupAnchor: [1, -34],
  shadowSize: [41, 41]
});

// Icono de Fin (Rojo)
const EndIcon = L.icon({
  iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-2x-red.png',
  shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/0.7.7/images/marker-shadow.png',
  iconSize: [25, 41],
  iconAnchor: [12, 41],
  popupAnchor: [1, -34],
  shadowSize: [41, 41]
});

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

interface MapProps {
  center: [number, number];
  zoom?: number;
  path?: [number, number][];
}

export default function Map({ center, zoom = 15, path }: MapProps) {
  const bounds = path && path.length > 0 ? L.latLngBounds(path) : undefined;

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
      
      {!path && (
        <>
          <Marker position={center} icon={StartIcon}>
            <Popup>Ubicación actual</Popup>
          </Marker>
          <LiveFollower center={center} />
        </>
      )}

      {path && path.length > 0 && (
        <>
          {/* Marcador de INICIO */}
          <Marker position={path[0]} icon={StartIcon}>
            <Popup>Punto de inicio</Popup>
          </Marker>

          {/* Marcador de FIN */}
          <Marker position={path[path.length - 1]} icon={EndIcon}>
            <Popup>Punto de destino</Popup>
          </Marker>

          <Polyline 
            positions={path} 
            pathOptions={{ 
              color: '#ef4444',
              weight: 6,
              opacity: 0.9,
              lineJoin: 'round',
            }} 
          />
          <MapResizer bounds={bounds} />
        </>
      )}
    </MapContainer>
  );
}
