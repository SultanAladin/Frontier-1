// Inspector cluster preview: draws one LOD level of the built hierarchy in 3D, one colour per cluster. The level
// slider picks the level; drag orbits. Triangles come from the hierarchy's own corner stream, so this is the
// simplified mesh a level really contains, not a re-simplification.
const $ = (id) => document.getElementById(id);
const MAX_TRIANGLES = 40000;
const hue = (i) => (i * 137.508) % 360;

export function createClusterPreview({ getGeometry, getHierarchy }) {
  let level = 0, yaw = 0.6, pitch = 0.35, drag = null, redraw = 0;

  function clustersAt(h, L) {
    const list = [];
    for (let c = 0; c < h.clusterFirst.length; c++) if (h.clusterLevel[c] === L) list.push(c);
    return list;
  }

  function refresh() {
    const h = getHierarchy();
    if (!h) return;
    const top = h.levels.length - 1;
    if (level > top) level = top;
    const slider = $('ClusterLevel');
    slider.max = String(top);
    slider.value = String(level);
    slider.style.setProperty('--fill', (top ? (level / top) * 100 : 100) + '%');
    $('ClusterLevelOut').textContent = `LOD ${level} of ${top}`;
    const info = h.levels[level];
    $('ClusterLevelStats').textContent = `${info.clusters.toLocaleString('en-US')} clusters · ${info.triangles.toLocaleString('en-US')} triangles`;
    schedule();
  }

  function schedule() {
    if (redraw) return;
    redraw = requestAnimationFrame(() => { redraw = 0; draw(); });
  }

  function draw() {
    const canvas = $('ClusterPreview');
    const size = Math.max(200, Math.round(canvas.clientWidth * Math.min(devicePixelRatio || 1, 2)));
    if (canvas.width !== size || canvas.height !== size) { canvas.width = size; canvas.height = size; }
    const g = canvas.getContext('2d');
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.fillStyle = '#101010';
    g.fillRect(0, 0, size, size);
    const geometry = getGeometry(), h = getHierarchy();
    if (!geometry || !h) return;
    const p = geometry.positions;
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < p.length; i += 3) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], p[i + k]); hi[k] = Math.max(hi[k], p[i + k]); }
    const centre = [0, 1, 2].map((k) => (lo[k] + hi[k]) / 2);
    const radius = Math.max(1e-6, Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) / 2);

    const clusters = clustersAt(h, level);
    let triangles = 0;
    for (const c of clusters) triangles += h.clusterSize[c] / 3;
    const stride = Math.max(1, Math.ceil(triangles / MAX_TRIANGLES));
    const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
    const view = (x, y, z) => {
      const X = x * cy + z * sy, Z0 = -x * sy + z * cy;
      return [X, y * cp - Z0 * sp, y * sp + Z0 * cp];
    };
    const scale = (size * 0.42) / radius;
    const tris = [];
    let seen = 0;
    for (const c of clusters) {
      const first = h.clusterFirst[c], corners = h.clusterSize[c];
      const colour = hue(c);
      for (let t = 0; t < corners; t += 3, seen++) {
        if (seen % stride) continue;
        const pts = [];
        for (let k = 0; k < 3; k++) {
          const v = h.cornerVertex[first + t + k] * 3;
          const q = view(p[v] - centre[0], p[v + 1] - centre[1], p[v + 2] - centre[2]);
          pts.push(q);
        }
        // View-space normal; depth is view Z, the same ordering the Geometry Editor uses.
        const [a, b, c] = pts;
        const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const len = Math.hypot(nx, ny, nz) || 1;
        const lit = Math.abs((nx * 0.4 + ny * 0.6 + nz * 0.7) / len);
        tris.push({ pts, depth: (a[2] + b[2] + c[2]) / 3, colour, shade: 0.35 + 0.65 * lit });
      }
    }
    tris.sort((a, b) => a.depth - b.depth);
    const to = (x, y) => [size / 2 + x * scale, size / 2 - y * scale];
    g.lineJoin = 'round';
    for (const t of tris) {
      const [a, b, c] = t.pts.map((q) => to(q[0], q[1]));
      g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.lineTo(c[0], c[1]); g.closePath();
      g.fillStyle = `hsl(${t.colour} 62% ${Math.round(18 + 52 * t.shade)}%)`;
      g.fill();
    }
    $('ClusterPreviewStats').textContent = `${clusters.length.toLocaleString('en-US')} clusters · ${triangles.toLocaleString('en-US')} tris${stride > 1 ? ` · drawing 1 in ${stride}` : ''}`;
  }

  function bind() {
    $('ClusterLevel').addEventListener('input', (event) => { level = +event.target.value; refresh(); });
    const canvas = $('ClusterPreview');
    canvas.addEventListener('pointerdown', (event) => { drag = { x: event.clientX, y: event.clientY, yaw, pitch }; canvas.setPointerCapture(event.pointerId); });
    canvas.addEventListener('pointermove', (event) => {
      if (!drag) return;
      yaw = drag.yaw + (event.clientX - drag.x) * 0.01;
      pitch = Math.max(-1.4, Math.min(1.4, drag.pitch + (event.clientY - drag.y) * 0.01));
      schedule();
    });
    const end = () => { drag = null; };
    canvas.addEventListener('pointerup', end);
    canvas.addEventListener('pointercancel', end);
  }
  bind();
  return { refresh, setLevel(next) { level = next; refresh(); }, get level() { return level; } };
}
