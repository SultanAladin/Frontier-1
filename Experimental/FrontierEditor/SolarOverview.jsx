import React from 'react';
import './solar-overview.css';

// Use each editor's own direction values, not a second simulation clock.
export default function SolarOverview({ elevation, azimuth, time }) {
  const bearing = ((Number(azimuth) % 360) + 360) % 360;
  const minutes = ((Math.round(time * 60) % 1440) + 1440) % 1440;
  const clock = [Math.floor(minutes / 60), minutes % 60].map(n => String(n).padStart(2, '0')).join(':');
  return <section className="solar-overview" aria-label="Solar position overview">
    <header><span>Solar telemetry</span><time>{clock} local</time></header>
    <div className="solar-pair">
      <article><span>Elevation</span><strong>{Number(elevation).toFixed(1)}<sup>°</sup></strong><small>{elevation >= 0 ? 'Above horizon' : 'Below horizon'}</small></article>
      <article><span>Azimuth</span><strong>{bearing.toFixed(1)}<sup>°</sup></strong><small>{['North','North-east','East','South-east','South','South-west','West','North-west'][Math.round(bearing / 45) % 8]}</small>
        <svg aria-hidden="true" viewBox="0 0 40 40"><circle cx="20" cy="20" r="17"/><path d="M20 4v3m16 13h-3M20 36v-3M4 20h3"/><path className="needle" d="M20 29V11m-4 5 4-6 4 6" transform={`rotate(${bearing} 20 20)`}/></svg>
      </article>
    </div>
  </section>;
}
