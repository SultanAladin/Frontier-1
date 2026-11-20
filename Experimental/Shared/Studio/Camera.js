// Glide camera: the viewport camera rig shared by Geometry Compile and the Component Viewer.
//
//   drag             orbit with inertia; the pivot jumps to the depth of whatever you grabbed (auto depth), so you
//                    always orbit around the surface under the cursor without the view jumping
//   right / middle   pan (shift + drag too)
//   wheel            glide toward the point under the cursor
//   alt + wheel      change lens (14–135 mm) as a dolly zoom: the subject keeps its size, the perspective changes
//   right + WASD/QE  drift through the scene (shift: faster)
//   flyTo / frame    every view change is an eased flight (position, pivot, lens), never a cut
//   turntable        slow automatic orbit, paused while you touch the view
//   lookThrough      fly into a Camera component's pose and lens
//
// State is spherical around a pivot: { target, yaw, pitch, distance, focal }. Input writes goals; update(dt) eases
// the live values toward them with critically damped springs, so every motion settles without overshoot.
import * as THREE from './Vendor/three.esm.min.js';

const SENSOR = 24; // mm, full-frame height: vertical fov = 2·atan(SENSOR / 2f)
export const LENSES = [14, 24, 35, 50, 85, 135];
const fovOf = (focal) => THREE.MathUtils.radToDeg(2 * Math.atan(SENSOR / (2 * focal)));
const ease = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
const wrapAngle = (a) => Math.atan2(Math.sin(a), Math.cos(a));

export class CameraRig {
  constructor(camera, dom, { pick = null } = {}) {
    this.camera = camera;
    this.dom = dom;
    this.pick = pick;                     // (clientX, clientY) → THREE.Vector3 | null (surface under the cursor)
    this.target = new THREE.Vector3(0, 0.5, 0);
    this.yaw = 0.8; this.pitch = 0.35; this.distance = 6; this.focal = 35;
    this.goal = { target: this.target.clone(), yaw: this.yaw, pitch: this.pitch, distance: this.distance, focal: this.focal };
    this.velocity = { yaw: 0, pitch: 0 };
    this.flight = null;
    this.turntable = false;
    this.flying = false;                  // right button held: WASD drift is live, the viewer should ignore those keys
    this.idle = 0;
    this.kick = { fov: 0, offset: new THREE.Vector3() }; // transient effects (simulate speed feel), eased by the caller
    this.listeners = new Set();
    this.keys = new Set();
    this.bind();
    this.apply();
  }

  // ------------------------------------------------------------------------------------------------------ input
  bind() {
    const dom = this.dom;
    let drag = null;
    dom.addEventListener('contextmenu', (e) => e.preventDefault());
    dom.addEventListener('pointerdown', (e) => {
      dom.focus?.({ preventScroll: true });
      this.cancelFlight();
      this.idle = 0;
      const pan = e.button === 1 || e.button === 2 || e.shiftKey;
      drag = { id: e.pointerId, x: e.clientX, y: e.clientY, pan, button: e.button, moved: false };
      if (e.button === 2) { this.flying = true; this.keys.clear(); }
      if (!pan && e.button === 0) this.anchorDepthAt(e.clientX, e.clientY);
      this.velocity.yaw = this.velocity.pitch = 0;
      dom.setPointerCapture?.(e.pointerId);
    });
    dom.addEventListener('pointermove', (e) => {
      if (!drag || drag.id !== e.pointerId) return;
      const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
      drag.x = e.clientX; drag.y = e.clientY;
      if (Math.abs(dx) + Math.abs(dy) > 0) drag.moved = true;
      const h = Math.max(1, dom.clientHeight);
      if (drag.pan) {
        const worldPerPixel = (2 * this.distance * Math.tan(THREE.MathUtils.degToRad(fovOf(this.focal)) / 2)) / h;
        const right = new THREE.Vector3().setFromMatrixColumn(this.camera.matrixWorld, 0);
        const up = new THREE.Vector3().setFromMatrixColumn(this.camera.matrixWorld, 1);
        this.goal.target.addScaledVector(right, -dx * worldPerPixel).addScaledVector(up, dy * worldPerPixel);
      } else {
        const k = 5.2 / h;
        this.goal.yaw -= dx * k; this.goal.pitch += dy * k;
        this.velocity.yaw = -dx * k * 60; this.velocity.pitch = dy * k * 60;
        this.clampPitch();
      }
    });
    const end = (e) => {
      if (!drag || drag.id !== e.pointerId) return;
      if (drag.button === 2) { this.flying = false; this.keys.clear(); }
      if (drag.pan || !drag.moved) this.velocity.yaw = this.velocity.pitch = 0;
      drag = null;
    };
    dom.addEventListener('pointerup', end);
    dom.addEventListener('pointercancel', end);
    dom.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.cancelFlight();
      this.idle = 0;
      const step = Math.sign(e.deltaY) * Math.min(1, Math.abs(e.deltaY) / 100);
      if (e.altKey) { this.dollyZoom(this.goal.focal * Math.pow(1.12, -step)); return; }
      const factor = Math.pow(1.18, step);
      const hit = this.pick && this.pick(e.clientX, e.clientY);
      if (hit && factor < 1) this.goal.target.lerp(hit, 1 - factor);   // glide toward the cursor when moving in
      this.goal.distance = THREE.MathUtils.clamp(this.goal.distance * factor, 0.05, 400);
    }, { passive: false });
    addEventListener('keydown', (e) => {
      if (!this.flying) return;
      const k = e.key.toLowerCase();
      if ('wasdqe'.includes(k) && k.length === 1) { this.keys.add(k); e.preventDefault(); e.stopImmediatePropagation(); }
      if (e.key === 'Shift') this.keys.add('shift');
    }, true);
    addEventListener('keyup', (e) => { this.keys.delete(e.key.toLowerCase()); if (e.key === 'Shift') this.keys.delete('shift'); }, true);
  }

  // Move the pivot along the view axis to the depth of the surface under the cursor, keeping the camera where it is.
  anchorDepthAt(x, y) {
    const hit = this.pick && this.pick(x, y);
    if (!hit) return;
    const eye = this.camera.position.clone();
    const forward = this.target.clone().sub(eye).normalize();
    const depth = hit.clone().sub(eye).dot(forward);
    if (!(depth > 0.02)) return;
    const pivot = eye.clone().addScaledVector(forward, depth);
    this.target.copy(pivot); this.goal.target.copy(pivot);
    this.distance = this.goal.distance = depth;
  }

  clampPitch() { this.goal.pitch = THREE.MathUtils.clamp(this.goal.pitch, -1.45, 1.5); }

  // Lens change that keeps the subject at the pivot the same size: distance scales with focal length.
  dollyZoom(focal, { animate = false } = {}) {
    const f = THREE.MathUtils.clamp(focal, 14, 135);
    const distance = this.goal.distance * (f / this.goal.focal);
    if (animate) this.flyTo({ focal: f, distance }, { duration: 0.9 });
    else { this.goal.focal = f; this.goal.distance = distance; }
  }

  // ---------------------------------------------------------------------------------------------------- flights
  flyTo({ target = null, yaw = null, pitch = null, distance = null, focal = null }, { duration = 0.75 } = {}) {
    const from = { target: this.target.clone(), yaw: this.yaw, pitch: this.pitch, distance: this.distance, focal: this.focal };
    const to = {
      target: target ? target.clone() : this.goal.target.clone(),
      yaw: yaw ?? this.goal.yaw, pitch: pitch ?? this.goal.pitch,
      distance: distance ?? this.goal.distance, focal: focal ?? this.goal.focal,
    };
    to.yaw = from.yaw + wrapAngle(to.yaw - from.yaw); // shortest way round
    this.velocity.yaw = this.velocity.pitch = 0;
    this.goal.target.copy(to.target); // goals hold the destination, so a flight chained onto this one starts from it
    Object.assign(this.goal, { yaw: to.yaw, pitch: to.pitch, distance: to.distance, focal: to.focal });
    if (duration <= 0) { this.setNow(to); return; }
    this.flight = { from, to, t: 0, duration };
  }

  setNow(s) {
    this.target.copy(s.target); this.goal.target.copy(s.target);
    this.yaw = this.goal.yaw = s.yaw; this.pitch = this.goal.pitch = s.pitch;
    this.distance = this.goal.distance = s.distance; this.focal = this.goal.focal = s.focal;
    this.flight = null;
    this.apply();
  }

  cancelFlight() {
    if (!this.flight) return;
    this.flight = null;
    this.goal.target.copy(this.target);
    Object.assign(this.goal, { yaw: this.yaw, pitch: this.pitch, distance: this.distance, focal: this.focal });
  }

  // Fit a box. keepAngle keeps the current direction; otherwise yaw/pitch in degrees.
  frame(box, { yaw = 38, pitch = 22, keepAngle = false, animate = true, fill = 1.12 } = {}) {
    if (!box || box.isEmpty()) return;
    const sphere = box.getBoundingSphere(new THREE.Sphere());
    const r = Math.max(sphere.radius, 0.05);
    const focal = this.goal.focal;
    const half = Math.min(THREE.MathUtils.degToRad(fovOf(focal)) / 2, Math.atan(Math.tan(THREE.MathUtils.degToRad(fovOf(focal)) / 2) * (this.camera.aspect || 1)));
    const distance = (r / Math.sin(half)) * fill;
    const pose = { target: sphere.center, distance, focal };
    if (!keepAngle) { pose.yaw = THREE.MathUtils.degToRad(yaw); pose.pitch = THREE.MathUtils.degToRad(pitch); }
    this.flyTo(pose, { duration: animate ? 0.85 : 0 });
  }

  // Named views around the current pivot, in degrees.
  view(name) {
    const views = { hero: [38, 20], front: [90, 4], back: [-90, 4], side: [0, 4], top: [0, 89], under: [0, -60] };
    const v = views[name] || views.hero;
    this.flyTo({ yaw: THREE.MathUtils.degToRad(v[0]), pitch: THREE.MathUtils.degToRad(v[1]) }, { duration: 0.9 });
  }

  // Fly into a world pose (position + forward direction) with a given vertical fov.
  lookThrough(position, forward, fovDeg, { duration = 1.1 } = {}) {
    const dir = forward.clone().normalize();
    const distance = Math.max(0.6, this.distance * 0.25);
    const target = position.clone().addScaledVector(dir, distance);
    const back = dir.clone().negate();
    const focal = THREE.MathUtils.clamp(SENSOR / (2 * Math.tan(THREE.MathUtils.degToRad(fovDeg) / 2)), 14, 135);
    this.flyTo({ target, distance, yaw: Math.atan2(back.x, back.z), pitch: Math.asin(THREE.MathUtils.clamp(back.y, -1, 1)), focal }, { duration });
  }

  onChange(fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); }

  // ----------------------------------------------------------------------------------------------------- update
  update(dt) {
    this.idle += dt;
    if (this.flight) {
      const f = this.flight;
      f.t = Math.min(1, f.t + dt / f.duration);
      const k = ease(f.t);
      this.target.lerpVectors(f.from.target, f.to.target, k);
      this.yaw = f.from.yaw + (f.to.yaw - f.from.yaw) * k;
      this.pitch = f.from.pitch + (f.to.pitch - f.from.pitch) * k;
      // Distance and lens interpolate in log space so long flights feel even.
      this.distance = Math.exp(Math.log(f.from.distance) + (Math.log(f.to.distance) - Math.log(f.from.distance)) * k);
      this.focal = Math.exp(Math.log(f.from.focal) + (Math.log(f.to.focal) - Math.log(f.from.focal)) * k);
      if (f.t >= 1) { this.flight = null; this.goal.target.copy(this.target); Object.assign(this.goal, { yaw: this.yaw, pitch: this.pitch, distance: this.distance, focal: this.focal }); }
    } else {
      // Inertia after a flick, then springs toward the goals.
      if (Math.abs(this.velocity.yaw) + Math.abs(this.velocity.pitch) > 1e-4) {
        this.goal.yaw += this.velocity.yaw * dt; this.goal.pitch += this.velocity.pitch * dt;
        const decay = Math.exp(-dt * 5);
        this.velocity.yaw *= decay; this.velocity.pitch *= decay;
        this.clampPitch();
      }
      if (this.turntable && this.idle > 1.2 && !this.flying) this.goal.yaw += dt * 0.25;
      if (this.flying && this.keys.size) this.drift(dt);
      const s = 1 - Math.exp(-dt * 14);
      this.target.lerp(this.goal.target, s);
      this.yaw += (this.goal.yaw - this.yaw) * s;
      this.pitch += (this.goal.pitch - this.pitch) * s;
      this.distance += (this.goal.distance - this.distance) * s;
      this.focal += (this.goal.focal - this.focal) * s;
    }
    this.apply();
  }

  drift(dt) {
    const speed = this.distance * (this.keys.has('shift') ? 2.4 : 0.8) * dt;
    const forward = new THREE.Vector3().setFromMatrixColumn(this.camera.matrixWorld, 2).negate();
    const right = new THREE.Vector3().setFromMatrixColumn(this.camera.matrixWorld, 0);
    const move = new THREE.Vector3();
    if (this.keys.has('w')) move.add(forward);
    if (this.keys.has('s')) move.sub(forward);
    if (this.keys.has('d')) move.add(right);
    if (this.keys.has('a')) move.sub(right);
    if (this.keys.has('e')) move.y += 1;
    if (this.keys.has('q')) move.y -= 1;
    if (move.lengthSq()) this.goal.target.addScaledVector(move.normalize(), speed);
    this.idle = 0;
  }

  apply() {
    const cp = Math.cos(this.pitch);
    const dir = new THREE.Vector3(Math.sin(this.yaw) * cp, Math.sin(this.pitch), Math.cos(this.yaw) * cp);
    const cam = this.camera;
    cam.position.copy(this.target).addScaledVector(dir, this.distance).add(this.kick.offset);
    cam.up.set(0, 1, 0);
    cam.lookAt(this.target.clone().add(this.kick.offset));
    const fov = fovOf(this.focal) + this.kick.fov;
    if (Math.abs(cam.fov - fov) > 1e-4 || cam.userData.distance !== this.distance) {
      cam.fov = fov;
      cam.near = Math.max(0.005, this.distance / 300);
      cam.far = this.distance * 60 + 80;
      cam.userData.distance = this.distance;
      cam.updateProjectionMatrix();
    }
    cam.updateMatrixWorld();
    for (const fn of this.listeners) fn(this);
  }
}

// --------------------------------------------------------------------------------------------------------- HUD
// Orientation dial (axis beads you can click to fly to that side), lens wheel and turntable switch, drawn into host.
export function createCameraHud(rig, host, { extra = [] } = {}) {
  const NS = 'http://www.w3.org/2000/svg';
  const wrap = document.createElement('div');
  wrap.className = 'cam-hud';
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', '-40 -40 80 80');
  svg.setAttribute('class', 'cam-dial');
  const ring = document.createElementNS(NS, 'circle');
  ring.setAttribute('r', '34'); ring.setAttribute('class', 'dial-ring');
  const horizon = document.createElementNS(NS, 'path');
  horizon.setAttribute('class', 'dial-horizon');
  svg.append(ring, horizon);
  const axes = [
    { id: 'x', label: 'X', color: '#e0605a', dir: [1, 0, 0], view: [90, 0] }, { id: '-x', color: '#e0605a', dir: [-1, 0, 0], view: [-90, 0] },
    { id: 'y', label: 'Y', color: '#6fcf6a', dir: [0, 1, 0], view: [0, 89] }, { id: '-y', color: '#6fcf6a', dir: [0, -1, 0], view: [0, -89] },
    { id: 'z', label: 'Z', color: '#5a8de0', dir: [0, 0, 1], view: [0, 0] }, { id: '-z', color: '#5a8de0', dir: [0, 0, -1], view: [180, 0] },
  ];
  for (const a of axes) {
    const g = document.createElementNS(NS, 'g');
    g.setAttribute('class', `dial-bead ${a.label ? 'pos' : 'neg'}`);
    const c = document.createElementNS(NS, 'circle');
    c.setAttribute('r', a.label ? '7.5' : '5');
    c.setAttribute('fill', a.label ? a.color : 'transparent');
    c.setAttribute('stroke', a.color);
    g.append(c);
    if (a.label) { const t = document.createElementNS(NS, 'text'); t.textContent = a.label; t.setAttribute('dy', '3'); g.append(t); }
    g.addEventListener('click', (e) => { e.stopPropagation(); rig.flyTo({ yaw: THREE.MathUtils.degToRad(a.view[0]), pitch: THREE.MathUtils.degToRad(a.view[1]) }, { duration: 0.8 }); });
    const title = document.createElementNS(NS, 'title'); title.textContent = `Look from ${a.id.startsWith('-') ? '−' : '+'}${a.id.replace('-', '').toUpperCase()}`;
    g.append(title);
    a.node = g;
    svg.append(g);
  }
  const lens = document.createElement('button');
  lens.type = 'button'; lens.className = 'cam-chip lens';
  lens.title = 'Lens (dolly zoom). Alt + wheel for any focal length';
  lens.addEventListener('click', () => {
    const i = LENSES.findIndex((f) => f > rig.goal.focal + 0.5);
    rig.dollyZoom(LENSES[i < 0 ? 0 : i], { animate: true });
  });
  const spin = document.createElement('button');
  spin.type = 'button'; spin.className = 'cam-chip';
  spin.title = 'Turntable';
  spin.textContent = '↻';
  spin.addEventListener('click', () => { rig.turntable = !rig.turntable; rig.idle = 2; spin.classList.toggle('on', rig.turntable); });
  const chips = document.createElement('div');
  chips.className = 'cam-chips';
  chips.append(lens, spin, ...extra);
  wrap.append(svg, chips);
  host.append(wrap);

  const v = new THREE.Vector3();
  const update = () => {
    const m = rig.camera.matrixWorldInverse;
    const placed = axes.map((a) => {
      v.set(...a.dir).transformDirection(m);
      return { a, x: v.x * 24, y: -v.y * 24, z: v.z };
    }).sort((p, q) => p.z - q.z);
    for (const p of placed) { p.a.node.setAttribute('transform', `translate(${p.x.toFixed(2)} ${p.y.toFixed(2)})`); p.a.node.style.opacity = String(0.55 + 0.45 * (1 + p.z) / 2); svg.append(p.a.node); }
    // Horizon: the ground plane's tilt as an arc across the dial.
    const tilt = Math.sin(rig.pitch) * 30;
    horizon.setAttribute('d', `M -30 ${tilt.toFixed(1)} Q 0 ${(tilt * 1.6).toFixed(1)} 30 ${tilt.toFixed(1)}`);
    lens.textContent = `${Math.round(rig.focal)} mm`;
  };
  rig.onChange(update);
  update();
  return { element: wrap, update };
}
