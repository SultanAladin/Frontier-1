// Socket debug looks, one per kind, in the same language as Visuals.js (shaded solids, light fans, glows; no line
// helpers). Everything that shows the *range* of a socket sits on the socket itself; the parts that show the *current
// state* (needle, carriage, stem, coil, rope, plug) are moved by update() every frame from the socket state.
import * as THREE from '../Shared/Studio/Vendor/three.esm.min.js';
import { clock, beam, glowSprite, fieldShell, shellMaterial, markDebug } from '../Shared/Studio/Visuals.js';
import { SOCKET_KIND, DOCK_COLORS, axisVector, axisBasis, rotateVector } from './Sockets.js';

const DEG = Math.PI / 180;
const Y = new THREE.Vector3(0, 1, 0), Z = new THREE.Vector3(0, 0, 1), X = new THREE.Vector3(1, 0, 0);
const satin = (color, metal = 0.6, rough = 0.32) => new THREE.MeshStandardMaterial({ color, roughness: rough, metalness: metal });
const lit = (color, intensity = 0.9) => new THREE.MeshStandardMaterial({ color, emissive: color, emissiveIntensity: intensity, roughness: 0.4, metalness: 0.1 });
const GRAPHITE = '#2a2d34', STEEL = '#8d939d';

// ------------------------------------------------------------------------------------------------- light fans
const FAN_VERTEX = /* glsl */`
  varying vec2 vUv;
  void main() { vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`;

// mode 0: range fan (soft fill, bright limit edges, a glow at the current value)
// mode 1: comet band (three trailing arcs; the band itself is rotated by the spin)
const FAN_FRAGMENT = /* glsl */`
  uniform vec3 uColor;
  uniform float uCurrent, uMode, uIntensity, uTime;
  varying vec2 vUv;
  void main() {
    float radial = smoothstep(0.0, 0.3, vUv.y) * (1.0 - smoothstep(0.8, 1.0, vUv.y));
    float a;
    if (uMode < 0.5) {
      float edge = max(1.0 - smoothstep(0.0, 0.035, vUv.x), 1.0 - smoothstep(0.0, 0.035, 1.0 - vUv.x));
      float cur = uCurrent >= 0.0 ? exp(-pow((vUv.x - uCurrent) / 0.035, 2.0)) : 0.0;
      float shimmer = 0.85 + 0.15 * sin(vUv.y * 18.0 - uTime * 3.0);
      a = (0.07 + 0.22 * vUv.y) * radial * shimmer + edge * 0.75 * radial + cur * 0.9 * radial;
    } else {
      float comet = pow(fract(vUv.x * 3.0), 4.0);
      a = comet * radial * uIntensity;
    }
    gl_FragColor = vec4(uColor, clamp(a, 0.0, 1.0) * uIntensity);
  }`;

function fanMaterial(color, mode = 0) {
  return new THREE.ShaderMaterial({
    uniforms: { uColor: { value: new THREE.Color(color) }, uCurrent: { value: -1 }, uMode: { value: mode }, uIntensity: { value: 1 }, uTime: clock },
    vertexShader: FAN_VERTEX, fragmentShader: FAN_FRAGMENT, transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
  });
}

// Annular sector p = r(u·cosφ + v·sinφ), φ ∈ [start, start + length], r ∈ [r0, r1]. uv = (angle share, radius share).
function sectorGeometry(u, v, r0, r1, start, length, segments = 64) {
  const pos = [], uvs = [], idx = [];
  const n = Math.max(2, Math.ceil(segments * Math.abs(length) / (2 * Math.PI)) + 1);
  for (let i = 0; i <= n; i++) {
    const t = i / n, phi = start + length * t, c = Math.cos(phi), s = Math.sin(phi);
    for (let j = 0; j <= 1; j++) {
      const r = j ? r1 : r0;
      pos.push(r * (u[0] * c + v[0] * s), r * (u[1] * c + v[1] * s), r * (u[2] * c + v[2] * s));
      uvs.push(t, j);
    }
    if (i < n) { const k = i * 2; idx.push(k, k + 1, k + 2, k + 1, k + 3, k + 2); }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  g.setIndex(idx);
  return g;
}

// ------------------------------------------------------------------------------------------------- dock iris
const IRIS_FRAGMENT = /* glsl */`
  uniform vec3 uColor;
  uniform float uTime, uFlow, uSteady;
  varying vec2 vUv;
  void main() {
    vec2 p = vUv * 2.0 - 1.0;
    float r = length(p);
    if (r > 1.0) discard;
    float rings = pow(0.5 + 0.5 * sin(r * 22.0 + uTime * 7.0 * uFlow), 6.0);
    float core = exp(-r * r * 6.0);
    float a = mix(rings * (1.0 - r) * 0.9, core * 0.9 + 0.15 * (1.0 - r), uSteady);
    gl_FragColor = vec4(uColor, a);
  }`;

function irisMaterial() {
  return new THREE.ShaderMaterial({
    uniforms: { uColor: { value: new THREE.Color(DOCK_COLORS.connected) }, uTime: clock, uFlow: { value: 1 }, uSteady: { value: 1 } },
    vertexShader: FAN_VERTEX, fragmentShader: IRIS_FRAGMENT, transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
  });
}

// ------------------------------------------------------------------------------------------------------ build
export function socketVisual(p, { selected = false, accepts = true } = {}) {
  const kind = SOCKET_KIND[p.kind] || SOCKET_KIND.fixed;
  const color = kind.color;
  const a = new THREE.Vector3(...axisVector(p.axis));
  const { u, v } = axisBasis(p.axis);
  const U = new THREE.Vector3(...u), V = new THREE.Vector3(...v);
  const alongA = new THREE.Quaternion().setFromUnitVectors(Y, a); // Y-built parts → axis
  const faceA = new THREE.Quaternion().setFromUnitVectors(Z, a);  // Z-facing parts (rings, discs) → axis
  const g = new THREE.Group();
  const add = (geo, mat, parent = g) => { const m = new THREE.Mesh(geo, mat); parent.add(m); return m; };
  let update = () => {};

  const halo = glowSprite(color, selected ? 0.3 : 0.2, selected ? 0.55 : 0.3);
  g.add(halo);

  switch (p.kind) {
    case 'fixed': {
      const puck = add(new THREE.CylinderGeometry(0.05, 0.056, 0.018, 6), satin(GRAPHITE, 0.5));
      puck.quaternion.copy(alongA);
      const face = add(new THREE.CylinderGeometry(0.03, 0.03, 0.004, 6), lit(color, 1.1));
      face.quaternion.copy(alongA); face.position.copy(a).multiplyScalar(0.011);
      for (let i = 0; i < 3; i++) {
        const phi = (i / 3) * Math.PI * 2 + Math.PI / 6;
        const bolt = add(new THREE.SphereGeometry(0.0065, 10, 8), satin(STEEL, 1, 0.2));
        bolt.position.copy(U).multiplyScalar(Math.cos(phi) * 0.041).addScaledVector(V, Math.sin(phi) * 0.041).addScaledVector(a, 0.01);
      }
      break;
    }

    case 'hinge': {
      const lo = Math.min(p.angleMin, p.angleMax) * DEG, hi = Math.max(p.angleMin, p.angleMax) * DEG;
      const pin = add(new THREE.CylinderGeometry(0.009, 0.009, 0.16, 16), satin(STEEL, 1, 0.18));
      pin.quaternion.copy(alongA);
      for (const s of [-1, 1]) {
        const knuckle = add(new THREE.CylinderGeometry(0.02, 0.02, 0.035, 20), satin(GRAPHITE, 0.5));
        knuckle.quaternion.copy(alongA); knuckle.position.copy(a).multiplyScalar(s * 0.045);
        const cap = add(new THREE.SphereGeometry(0.011, 12, 8), lit(color, 1.2));
        cap.position.copy(a).multiplyScalar(s * 0.08);
      }
      const fanMat = fanMaterial(color);
      add(sectorGeometry(u, v, 0.03, 0.2, lo, Math.max(hi - lo, 0.01)), fanMat);
      const needle = new THREE.Group();
      const blade = add(new THREE.BoxGeometry(0.17, 0.006, 0.014), lit(color, 0.7), needle);
      blade.quaternion.setFromUnitVectors(X, U); blade.position.copy(U).multiplyScalar(0.115);
      const tip = glowSprite(color, 0.09, 0.9); tip.position.copy(U).multiplyScalar(0.205); needle.add(tip);
      g.add(needle);
      update = (s) => {
        needle.quaternion.setFromAxisAngle(a, s.angle * DEG);
        fanMat.uniforms.uCurrent.value = (s.angle * DEG - lo) / Math.max(hi - lo, 1e-6);
      };
      break;
    }

    case 'slider': {
      const lo = Math.min(p.travelMin, p.travelMax), hi = Math.max(p.travelMin, p.travelMax), len = Math.max(hi - lo, 0.01);
      const track = add(new THREE.CylinderGeometry(0.014, 0.014, len, 20, 1, true), fanlessShell(color));
      track.quaternion.copy(alongA); track.position.copy(a).multiplyScalar((lo + hi) / 2);
      const rail = add(new THREE.CylinderGeometry(0.005, 0.005, len, 10), satin(STEEL, 1, 0.2));
      rail.quaternion.copy(alongA); rail.position.copy(track.position);
      for (const at of [lo, hi]) {
        const stop = add(new THREE.CylinderGeometry(0.034, 0.034, 0.008, 28), satin(GRAPHITE, 0.5));
        stop.quaternion.copy(alongA); stop.position.copy(a).multiplyScalar(at);
        const rim = add(new THREE.TorusGeometry(0.034, 0.0035, 8, 32), lit(color, 1));
        rim.quaternion.copy(faceA); rim.position.copy(stop.position);
      }
      const carriage = new THREE.Group();
      const ring = add(new THREE.TorusGeometry(0.028, 0.009, 12, 36), lit(color, 0.9), carriage);
      ring.quaternion.copy(faceA);
      carriage.add(glowSprite(color, 0.12, 0.8));
      g.add(carriage);
      update = (s) => carriage.position.copy(a).multiplyScalar(s.travel);
      break;
    }

    case 'ball': {
      add(new THREE.SphereGeometry(0.03, 24, 16), satin(STEEL, 1, 0.15));
      const cup = add(new THREE.SphereGeometry(0.04, 28, 12, 0, Math.PI * 2, Math.PI / 2, Math.PI / 2), new THREE.MeshStandardMaterial({ color: GRAPHITE, roughness: 0.45, metalness: 0.5, side: THREE.DoubleSide }));
      cup.quaternion.copy(alongA);
      const L = 0.24;
      const cone = beam({ length: L, radius: Math.tan(Math.min(p.cone, 85) * DEG) * L, color, strength: selected ? 0.42 : 0.3 });
      cone.quaternion.setFromUnitVectors(X, a);
      g.add(cone);
      const stem = new THREE.Group();
      const rod = add(new THREE.CylinderGeometry(0.006, 0.006, L, 10), lit(color, 0.5), stem);
      rod.position.y = L / 2;
      const knob = add(new THREE.SphereGeometry(0.012, 12, 10), lit(color, 1.2), stem);
      knob.position.y = L;
      const tip = glowSprite(color, 0.1, 0.9); tip.position.y = L; stem.add(tip);
      g.add(stem);
      update = (s, pose) => stem.quaternion.fromArray(pose.rotation).multiply(alongA);
      break;
    }

    case 'spring': {
      const L0 = 0.26, base = a.clone().multiplyScalar(L0); // fixed top mount above; the moving seat is at the socket
      const pts = [];
      const turns = 7;
      for (let i = 0; i <= 200; i++) { const t = i / 200, phi = t * turns * Math.PI * 2; pts.push(new THREE.Vector3(Math.cos(phi) * 0.034, t, Math.sin(phi) * 0.034)); }
      const coilMat = new THREE.MeshStandardMaterial({ color, emissive: color, emissiveIntensity: 0.3, roughness: 0.3, metalness: 0.55 });
      const coil = add(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts), 260, 0.0052, 8), coilMat);
      coil.quaternion.copy(alongA);
      const plate = (at) => { const m = add(new THREE.CylinderGeometry(0.048, 0.048, 0.01, 28), satin(GRAPHITE, 0.5)); m.quaternion.copy(alongA); m.position.copy(at); return m; };
      plate(base);
      const top = plate(new THREE.Vector3());
      const body = add(new THREE.CylinderGeometry(0.015, 0.015, 0.15, 16), satin(STEEL, 1, 0.25));
      body.quaternion.copy(alongA); body.position.copy(base).addScaledVector(a, -0.075);
      const rod = add(new THREE.CylinderGeometry(0.0075, 0.0075, 0.16, 12), satin('#c9ced6', 1, 0.1));
      rod.quaternion.copy(alongA);
      const ring = add(new THREE.TorusGeometry(0.048, 0.003, 8, 32), lit(color, 1));
      ring.quaternion.copy(faceA);
      update = (s) => {
        const at = a.clone().multiplyScalar(s.x);
        coil.position.copy(at); coil.scale.set(1, Math.max(0.03, L0 - s.x), 1);
        top.position.copy(at); ring.position.copy(at);
        rod.position.copy(at).addScaledVector(a, 0.08);
        coilMat.emissiveIntensity = 0.3 + Math.min(1.2, Math.abs(s.v) * 1.5);
      };
      break;
    }

    case 'spin': {
      const hub = add(new THREE.CylinderGeometry(0.028, 0.028, 0.04, 24), satin(GRAPHITE, 0.6));
      hub.quaternion.copy(alongA);
      const cap = add(new THREE.CylinderGeometry(0.016, 0.016, 0.044, 6), lit(color, 0.9));
      cap.quaternion.copy(alongA);
      const bandMat = fanMaterial(color, 1);
      const band = new THREE.Group();
      add(sectorGeometry(u, v, 0.055, 0.13, 0, Math.PI * 2, 96), bandMat, band);
      g.add(band);
      update = (s) => {
        band.quaternion.setFromAxisAngle(a, s.spin);
        const rpm = Math.abs(((s.rate || 0) * 60) / (2 * Math.PI));
        bandMat.uniforms.uIntensity.value = 0.35 + 0.65 * Math.min(1, rpm / 600);
      };
      break;
    }

    case 'tether': {
      const eye = add(new THREE.TorusGeometry(0.018, 0.0055, 10, 24), satin(STEEL, 1, 0.2));
      eye.quaternion.setFromUnitVectors(Z, V);
      const ropeMat = new THREE.MeshStandardMaterial({ color, emissive: color, emissiveIntensity: 0.35, roughness: 0.7, metalness: 0 });
      const ropeGeo = new THREE.CylinderGeometry(0.0065, 0.0065, 1, 8); ropeGeo.translate(0, 0.5, 0);
      const rope = add(ropeGeo, ropeMat);
      const end = new THREE.Group();
      const ring = add(new THREE.TorusGeometry(0.024, 0.006, 10, 28), lit(color, 0.8), end);
      ring.quaternion.setFromUnitVectors(Z, V);
      end.add(glowSprite(color, 0.11, 0.85));
      g.add(end);
      let fanMat = null;
      if (selected) {
        const hang = a.clone().negate(), side = new THREE.Vector3().crossVectors(V, hang); // swing plane: θ about V
        fanMat = fanMaterial(color);
        fanMat.uniforms.uIntensity.value = 0.55;
        add(sectorGeometry(hang.toArray(), side.toArray(), p.length * 0.55, p.length, -70 * DEG, 140 * DEG), fanMat);
      }
      const down = [-a.x, -a.y, -a.z];
      update = (s, pose) => {
        const dir = new THREE.Vector3(...rotateVector(pose.rotation, down));
        rope.quaternion.setFromUnitVectors(Y, dir); rope.scale.set(1, p.length, 1);
        end.position.copy(dir).multiplyScalar(p.length);
        if (fanMat) fanMat.uniforms.uCurrent.value = (s.theta + 70 * DEG) / (140 * DEG);
      };
      break;
    }

    case 'dock': {
      const collar = add(new THREE.TorusGeometry(0.062, 0.013, 14, 48), satin(GRAPHITE, 0.55));
      collar.quaternion.copy(faceA);
      const notches = [];
      for (let i = 0; i < 6; i++) {
        const phi = (i / 6) * Math.PI * 2;
        const n = add(new THREE.SphereGeometry(0.0075, 10, 8), lit(DOCK_COLORS.connected, 1.2));
        n.position.copy(U).multiplyScalar(Math.cos(phi) * 0.062).addScaledVector(V, Math.sin(phi) * 0.062).addScaledVector(a, 0.012);
        notches.push(n);
      }
      const irisMat = irisMaterial();
      const iris = add(new THREE.CircleGeometry(0.05, 48), irisMat);
      iris.quaternion.copy(faceA);
      const field = fieldShell({ type: 'sphere', centre: [0, 0, 0], radius: Math.max(0.05, p.snapRadius) }, DOCK_COLORS.connected, { base: 0.01, rim: selected ? 0.3 : 0.16, scan: 0.06, additive: true });
      g.add(field);
      const fieldMat = field.children[0].material;
      const flux = beam({ length: 1, radius: 0.045, color: DOCK_COLORS.seeking, strength: 0.5 });
      const fluxRoot = new THREE.Group(); fluxRoot.add(flux); fluxRoot.quaternion.setFromUnitVectors(X, a);
      // the beam's apex sits at the plug: flip it so it fans out from the dock toward the plug
      flux.rotation.set(0, Math.PI, 0); flux.position.x = 1;
      g.add(fluxRoot);
      const plug = new THREE.Group();
      const plugBody = add(new THREE.CylinderGeometry(0.022, 0.034, 0.05, 24), satin(STEEL, 1, 0.2), plug);
      plugBody.quaternion.copy(alongA); plugBody.position.copy(a).multiplyScalar(0.03);
      const plugTip = add(new THREE.SphereGeometry(0.012, 12, 10), lit(DOCK_COLORS.connected, 1.1), plug);
      g.add(plug);
      const rippleMat = new THREE.MeshBasicMaterial({ color: DOCK_COLORS.connected, transparent: true, opacity: 0, depthWrite: false, blending: THREE.AdditiveBlending });
      const ripple = add(new THREE.TorusGeometry(0.062, 0.006, 8, 48), rippleMat);
      ripple.quaternion.copy(faceA);
      const c = new THREE.Color(), green = new THREE.Color(DOCK_COLORS.connected), amber = new THREE.Color(DOCK_COLORS.seeking), red = new THREE.Color(DOCK_COLORS.rejected);
      update = (s) => {
        const d = s.dock;
        const out = Math.max(0.05, p.snapRadius) * 1.8;
        if (d.phase === 'connected') {
          const share = Math.min(1, d.load / Math.max(1, p.breakForce)); // green → amber → red as the load nears the break force
          if (share < 0.6) c.copy(green).lerp(amber, share / 0.6);
          else c.copy(amber).lerp(red, (share - 0.6) / 0.4);
        } else c.copy(d.phase === 'rejected' || !accepts ? red : amber);
        for (const n of notches) { n.material.color.copy(c); n.material.emissive.copy(c); }
        irisMat.uniforms.uColor.value.copy(c);
        irisMat.uniforms.uSteady.value = d.phase === 'connected' ? 1 : 0;
        irisMat.uniforms.uFlow.value = d.phase === 'rejected' ? -1 : 1;
        fieldMat.uniforms.uColor.value.copy(c);
        fieldMat.uniforms.uPulse.value = d.phase === 'connected' ? 0 : 1;
        plug.position.copy(a).multiplyScalar(d.gap);
        plugTip.material.color.copy(c); plugTip.material.emissive.copy(c);
        const showFlux = d.phase !== 'connected' && d.gap > 0.01;
        fluxRoot.visible = showFlux;
        if (showFlux) {
          fluxRoot.scale.set(d.gap, 1, 1);
          flux.material.uniforms.uColor.value.copy(c);
          flux.material.uniforms.uStrength.value = 0.25 + 0.6 * (1 - d.gap / out);
        }
        const k = d.ripple / 0.6;
        ripple.visible = k > 0;
        if (k > 0) { ripple.scale.setScalar(1 + (1 - k) * 3); rippleMat.opacity = k; }
      };
      break;
    }
  }

  markDebug(g);
  return { group: g, update };
}

// Translucent tube for slider tracks: fresnel shell without scan bands.
const fanlessShell = (color) => shellMaterial(color, { base: 0.08, rim: 0.55, scan: 0 });
