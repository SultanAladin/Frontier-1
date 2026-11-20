// Procedural offline CanvasTextures for architectural stone slabs, concrete walls, epoxy garage floors, and timber slats.
import * as THREE from '../Shared/Studio/Vendor/three.esm.min.js';

const cache = new Map();

function makeTexture(key, size, drawFn, repeat = [1, 1], isColor = true) {
  const fullKey = `${key}:${size}:${repeat.join('x')}`;
  if (cache.has(fullKey)) return cache.get(fullKey);
  if (typeof document === 'undefined') {
    const tex = new THREE.DataTexture(new Uint8Array([200, 205, 212, 255]), 1, 1);
    tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(repeat[0], repeat[1]);
    cache.set(fullKey, tex);
    return tex;
  }
  const c = document.createElement('canvas');
  c.width = c.height = size;
  const g = c.getContext('2d');
  drawFn(g, size);
  const tex = new THREE.CanvasTexture(c);
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.repeat.set(repeat[0], repeat[1]);
  tex.anisotropy = 4;
  if (isColor) tex.colorSpace = THREE.SRGBColorSpace;
  cache.set(fullKey, tex);
  return tex;
}

// Deterministic pseudo-random helper for repeatable procedural textures
function rng(seed) {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296;
  };
}

export function stoneSlabTextures(tone = '#cfd5dc', repeat = [16, 16]) {
  const col = new THREE.Color(tone);
  const baseR = Math.round(col.r * 255);
  const baseG = Math.round(col.g * 255);
  const baseB = Math.round(col.b * 255);

  const map = makeTexture(`stone-${tone}`, 512, (g, s) => {
    const rand = rng(42);
    g.fillStyle = `rgb(${baseR},${baseG},${baseB})`;
    g.fillRect(0, 0, s, s);

    // Subtle per-tile tonal variation (2x2 large architectural slabs per tile)
    const half = s / 2;
    for (let j = 0; j < 2; j++) {
      for (let i = 0; i < 2; i++) {
        const delta = Math.round((rand() - 0.5) * 8);
        g.fillStyle = `rgb(${Math.max(0, Math.min(255, baseR + delta))},${Math.max(0, Math.min(255, baseG + delta))},${Math.max(0, Math.min(255, baseB + delta))})`;
        g.fillRect(i * half, j * half, half, half);
      }
    }

    // Fine mineral terrazzo / concrete grain
    for (let k = 0; k < 2800; k++) {
      const x = rand() * s;
      const y = rand() * s;
      const a = 0.02 + rand() * 0.04;
      g.fillStyle = rand() > 0.5 ? `rgba(255,255,255,${a})` : `rgba(15,20,28,${a})`;
      g.fillRect(x, y, 1.5 + rand() * 2, 1.5 + rand() * 2);
    }

    // Hairline architectural expansion joints
    g.strokeStyle = 'rgba(20, 24, 30, 0.14)';
    g.lineWidth = 2;
    g.beginPath();
    g.moveTo(0, 0); g.lineTo(s, 0);
    g.moveTo(0, half); g.lineTo(s, half);
    g.moveTo(0, 0); g.lineTo(0, s);
    g.moveTo(half, 0); g.lineTo(half, s);
    g.stroke();
  }, repeat, true);

  return { map };
}

export function epoxyGarageTextures(repeat = [12, 12]) {
  const map = makeTexture('epoxy-garage', 512, (g, s) => {
    const rand = rng(109);
    g.fillStyle = '#252930';
    g.fillRect(0, 0, s, s);

    // Subtle satin epoxy mottling & micro-flakes
    for (let k = 0; k < 3400; k++) {
      const x = rand() * s;
      const y = rand() * s;
      const v = rand();
      g.fillStyle = v > 0.72
        ? 'rgba(195,205,220,0.06)'
        : v > 0.35
          ? 'rgba(120,130,145,0.05)'
          : 'rgba(10,12,16,0.08)';
      g.fillRect(x, y, 1.5, 1.5);
    }

    // Crisp bay tile seams
    const half = s / 2;
    g.strokeStyle = 'rgba(8, 10, 14, 0.45)';
    g.lineWidth = 2;
    g.beginPath();
    g.moveTo(0, 0); g.lineTo(s, 0);
    g.moveTo(0, half); g.lineTo(s, half);
    g.moveTo(0, 0); g.lineTo(0, s);
    g.moveTo(half, 0); g.lineTo(half, s);
    g.stroke();
  }, repeat, true);

  return { map };
}

export function plasterWallTexture(tone = '#d9dfe6', repeat = [6, 2]) {
  const col = new THREE.Color(tone);
  const r = Math.round(col.r * 255);
  const gr = Math.round(col.g * 255);
  const b = Math.round(col.b * 255);

  return makeTexture(`plaster-${tone}`, 512, (g, s) => {
    const rand = rng(77);
    g.fillStyle = `rgb(${r},${gr},${b})`;
    g.fillRect(0, 0, s, s);

    // Subtle troweled mineral variation
    for (let k = 0; k < 2400; k++) {
      const x = rand() * s;
      const y = rand() * s;
      const w = 4 + rand() * 14;
      const h = 2 + rand() * 6;
      const a = 0.014 + rand() * 0.025;
      g.fillStyle = rand() > 0.5 ? `rgba(255,255,255,${a})` : `rgba(30,36,46,${a})`;
      g.fillRect(x, y, w, h);
    }

    // Architectural panel reveal shadow lines
    g.strokeStyle = 'rgba(18, 24, 34, 0.10)';
    g.lineWidth = 2;
    g.beginPath();
    g.moveTo(0, s * 0.5); g.lineTo(s, s * 0.5);
    g.moveTo(s * 0.5, 0); g.lineTo(s * 0.5, s);
    g.stroke();
  }, repeat, true);
}

export function timberSlatTexture(repeat = [8, 4]) {
  return makeTexture('timber-slat', 256, (g, s) => {
    const rand = rng(313);
    g.fillStyle = '#4e3827';
    g.fillRect(0, 0, s, s);
    const slats = 8;
    const w = s / slats;
    for (let i = 0; i < slats; i++) {
      const d = Math.round((rand() - 0.5) * 16);
      g.fillStyle = `rgb(${84 + d},${58 + d},${38 + d})`;
      g.fillRect(i * w + 2, 0, w - 4, s);
      g.fillStyle = '#181412';
      g.fillRect(i * w, 0, 2, s);
      g.fillRect((i + 1) * w - 2, 0, 2, s);
    }
  }, repeat, true);
}
