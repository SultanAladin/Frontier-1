// Interactive 3D Transform Gizmo for Three.js (Select / Move / Rotate / Scale), rendered in a clean depth-cleared
// overlay pass with capture-phase pointer interception so dragging a handle never fights CameraRig orbit.
import * as THREE from '../Shared/Studio/Vendor/three.esm.min.js';

const AXIS_COLOR = {
  x: 0xe05852,
  y: 0x5fd062,
  z: 0x5288e0,
  xy: 0xe5cb45,
  xz: 0xc84ed0,
  yz: 0x3ec5cc,
  xyz: 0xf0f0f0,
};

const HOVER_COLOR = 0xffb238;
const DEG = Math.PI / 180;
const RAD = 180 / Math.PI;

export class TransformGizmo {
  constructor(camera, canvas, { onChange = null, onDragChange = null } = {}) {
    this.camera = camera;
    this.canvas = canvas;
    this.onChange = onChange;
    this.onDragChange = onDragChange;
    this.mode = 'translate'; // 'off' | 'translate' | 'rotate' | 'scale'
    this.visible = true;
    this.target = null; // { getTransform(), setTransform({ location, rotation, scale }), canMove, canRotate, canScale }

    this.scene = new THREE.Scene();
    this.root = new THREE.Group();
    this.scene.add(this.root);

    // Soft camera-headlight lighting for crisp solid gizmo cones/cubes
    this.scene.add(new THREE.AmbientLight(0xffffff, 0.9));
    const head = new THREE.DirectionalLight(0xffffff, 0.85);
    head.position.set(1, 2, 2);
    this.scene.add(head);
    this.headLight = head;

    this.raycaster = new THREE.Raycaster();
    this.handles = [];
    this.hovered = null;
    this.drag = null;

    this.buildHandles();
    this.bindEvents();
  }

  setMode(mode) {
    this.mode = mode;
    this.rebuildVisibility();
  }

  setTarget(target) {
    this.target = target;
    this.hovered = null;
    this.drag = null;
    this.rebuildVisibility();
  }

  rebuildVisibility() {
    const active = Boolean(this.target && this.visible && this.mode !== 'off');
    this.root.visible = active;
    for (const h of this.handles) {
      const modeMatch = h.mode === this.mode;
      const perm =
        (this.mode === 'translate' && this.target?.canMove !== false) ||
        (this.mode === 'rotate' && this.target?.canRotate !== false) ||
        (this.mode === 'scale' && this.target?.canScale !== false);
      h.group.visible = active && modeMatch && perm;
    }
    this.refreshColors();
  }

  makeMat(hex, opacity = 1) {
    return new THREE.MeshBasicMaterial({
      color: hex,
      transparent: opacity < 1,
      opacity,
      depthTest: true,
      depthWrite: opacity >= 1,
      side: THREE.DoubleSide,
      toneMapped: false,
    });
  }

  registerHandle(group, mode, axis, baseColor) {
    const mats = [];
    group.traverse((o) => {
      if (o.isMesh) {
        o.userData.gizmoAxis = axis;
        o.userData.gizmoMode = mode;
        mats.push({ mat: o.material, base: o.material.color.getHex(), opacity: o.material.opacity });
      }
    });
    this.handles.push({ group, mode, axis, baseColor, mats });
    this.root.add(group);
  }

  buildHandles() {
    const dirs = { x: new THREE.Vector3(1, 0, 0), y: new THREE.Vector3(0, 1, 0), z: new THREE.Vector3(0, 0, 1) };

    // ---------------- TRANSLATE ----------------
    for (const axis of ['x', 'y', 'z']) {
      const g = new THREE.Group();
      const mat = this.makeMat(AXIS_COLOR[axis]);
      const pickMat = new THREE.MeshBasicMaterial({ visible: false });
      const shaft = new THREE.Mesh(new THREE.CylinderGeometry(0.022, 0.022, 0.82, 10), mat);
      shaft.position.y = 0.45;
      const cone = new THREE.Mesh(new THREE.ConeGeometry(0.072, 0.22, 16), mat);
      cone.position.y = 0.96;
      const hit = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.11, 1.08, 8), pickMat);
      hit.position.y = 0.56;
      g.add(shaft, cone, hit);
      g.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dirs[axis]);
      this.registerHandle(g, 'translate', axis, AXIS_COLOR[axis]);
    }

    // Translate planes (XY, XZ, YZ)
    const planes = [
      { axis: 'xy', rot: [0, 0, 0], pos: [0.28, 0.28, 0] },
      { axis: 'xz', rot: [Math.PI / 2, 0, 0], pos: [0.28, 0, 0.28] },
      { axis: 'yz', rot: [0, Math.PI / 2, 0], pos: [0, 0.28, 0.28] },
    ];
    for (const pl of planes) {
      const g = new THREE.Group();
      const fill = new THREE.Mesh(new THREE.PlaneGeometry(0.24, 0.24), this.makeMat(AXIS_COLOR[pl.axis], 0.32));
      const borderGeo = new THREE.EdgesGeometry(new THREE.PlaneGeometry(0.24, 0.24));
      const border = new THREE.LineSegments(borderGeo, new THREE.LineBasicMaterial({ color: AXIS_COLOR[pl.axis], toneMapped: false }));
      g.add(fill, border);
      g.rotation.set(...pl.rot);
      g.position.set(...pl.pos);
      this.registerHandle(g, 'translate', pl.axis, AXIS_COLOR[pl.axis]);
    }

    // Center free-move ring
    {
      const g = new THREE.Group();
      const center = new THREE.Mesh(new THREE.SphereGeometry(0.075, 16, 12), this.makeMat(AXIS_COLOR.xyz, 0.95));
      g.add(center);
      this.registerHandle(g, 'translate', 'xyz', AXIS_COLOR.xyz);
    }

    // ---------------- ROTATE ----------------
    const ringRots = {
      x: [0, Math.PI / 2, 0],
      y: [Math.PI / 2, 0, 0],
      z: [0, 0, 0],
    };
    for (const axis of ['x', 'y', 'z']) {
      const g = new THREE.Group();
      const torus = new THREE.Mesh(new THREE.TorusGeometry(0.92, 0.024, 12, 56), this.makeMat(AXIS_COLOR[axis]));
      const pick = new THREE.Mesh(new THREE.TorusGeometry(0.92, 0.10, 8, 28), new THREE.MeshBasicMaterial({ visible: false }));
      g.add(torus, pick);
      g.rotation.set(...ringRots[axis]);
      this.registerHandle(g, 'rotate', axis, AXIS_COLOR[axis]);
    }

    // ---------------- SCALE ----------------
    for (const axis of ['x', 'y', 'z']) {
      const g = new THREE.Group();
      const mat = this.makeMat(AXIS_COLOR[axis]);
      const shaft = new THREE.Mesh(new THREE.CylinderGeometry(0.022, 0.022, 0.82, 10), mat);
      shaft.position.y = 0.45;
      const box = new THREE.Mesh(new THREE.BoxGeometry(0.13, 0.13, 0.13), mat);
      box.position.y = 0.92;
      const hit = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.11, 1.05, 8), new THREE.MeshBasicMaterial({ visible: false }));
      hit.position.y = 0.54;
      g.add(shaft, box, hit);
      g.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dirs[axis]);
      this.registerHandle(g, 'scale', axis, AXIS_COLOR[axis]);
    }
    {
      const g = new THREE.Group();
      const box = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.16, 0.16), this.makeMat(AXIS_COLOR.xyz));
      g.add(box);
      this.registerHandle(g, 'scale', 'xyz', AXIS_COLOR.xyz);
    }
  }

  refreshColors() {
    const activeAxis = this.drag ? this.drag.axis : this.hovered;
    for (const h of this.handles) {
      const isHot = h.mode === this.mode && h.axis === activeAxis;
      for (const item of h.mats) {
        item.mat.color.setHex(isHot ? HOVER_COLOR : item.base);
        if (item.opacity < 1) item.mat.opacity = isHot ? 0.65 : item.opacity;
      }
    }
  }

  syncPose() {
    if (!this.target || !this.visible || this.mode === 'off') {
      this.root.visible = false;
      return;
    }
    this.root.visible = true;
    const t = this.target.getTransform();
    const pos = new THREE.Vector3(...t.location);
    this.root.position.copy(pos);
    // Keep gizmo screen size constant relative to distance
    const dist = Math.max(0.4, this.camera.position.distanceTo(pos));
    const fovRad = THREE.MathUtils.degToRad(this.camera.fov || 40);
    const scale = dist * Math.tan(fovRad * 0.5) * 0.36;
    this.root.scale.setScalar(scale);
    this.root.updateMatrixWorld(true);
    this.headLight.position.copy(this.camera.position);
  }

  ndcOf(clientX, clientY) {
    const rect = this.canvas.getBoundingClientRect();
    return new THREE.Vector2(
      ((clientX - rect.left) / Math.max(1, rect.width)) * 2 - 1,
      -((clientY - rect.top) / Math.max(1, rect.height)) * 2 + 1
    );
  }

  pickHandle(clientX, clientY) {
    if (!this.root.visible) return null;
    this.syncPose();
    this.raycaster.setFromCamera(this.ndcOf(clientX, clientY), this.camera);
    const visibleRoots = this.handles.filter((h) => h.group.visible).map((h) => h.group);
    const hits = this.raycaster.intersectObjects(visibleRoots, true);
    for (const hit of hits) {
      if (hit.object.userData.gizmoAxis) {
        return { axis: hit.object.userData.gizmoAxis, point: hit.point.clone() };
      }
    }
    return null;
  }

  rayPlanePoint(clientX, clientY, plane) {
    this.raycaster.setFromCamera(this.ndcOf(clientX, clientY), this.camera);
    const out = new THREE.Vector3();
    return this.raycaster.ray.intersectPlane(plane, out) ? out : null;
  }

  axisDir(axis) {
    if (axis === 'x') return new THREE.Vector3(1, 0, 0);
    if (axis === 'y') return new THREE.Vector3(0, 1, 0);
    if (axis === 'z') return new THREE.Vector3(0, 0, 1);
    return null;
  }

  planeNormal(axis) {
    if (axis === 'xy') return new THREE.Vector3(0, 0, 1);
    if (axis === 'xz') return new THREE.Vector3(0, 1, 0);
    if (axis === 'yz') return new THREE.Vector3(1, 0, 0);
    return null;
  }

  bindEvents() {
    this.canvas.addEventListener('pointerdown', (e) => {
      if (e.button !== 0 || !this.root.visible) return;
      const hit = this.pickHandle(e.clientX, e.clientY);
      if (!hit) return;

      e.stopImmediatePropagation();
      e.preventDefault();
      this.canvas.setPointerCapture?.(e.pointerId);

      const startTransform = this.target.getTransform();
      const pivot = new THREE.Vector3(...startTransform.location);
      const camDir = new THREE.Vector3().subVectors(this.camera.position, pivot).normalize();

      let dragPlane;
      if (this.mode === 'translate' || this.mode === 'scale') {
        const ax = this.axisDir(hit.axis);
        const pn = this.planeNormal(hit.axis);
        if (pn) {
          dragPlane = new THREE.Plane().setFromNormalAndCoplanarPoint(pn, pivot);
        } else if (ax) {
          const perp = new THREE.Vector3().crossVectors(ax, camDir);
          const normal = perp.lengthSq() > 1e-4 ? new THREE.Vector3().crossVectors(perp, ax).normalize() : camDir;
          dragPlane = new THREE.Plane().setFromNormalAndCoplanarPoint(normal, pivot);
        } else {
          dragPlane = new THREE.Plane().setFromNormalAndCoplanarPoint(camDir, pivot);
        }
      } else {
        const rotN = this.axisDir(hit.axis) || camDir;
        dragPlane = new THREE.Plane().setFromNormalAndCoplanarPoint(rotN, pivot);
      }

      const startPoint = this.rayPlanePoint(e.clientX, e.clientY, dragPlane) || hit.point.clone();
      this.drag = {
        pointerId: e.pointerId,
        axis: hit.axis,
        mode: this.mode,
        pivot,
        dragPlane,
        startPoint,
        startClient: [e.clientX, e.clientY],
        startTransform: {
          location: startTransform.location.slice(),
          rotation: startTransform.rotation.slice(),
          scale: startTransform.scale.slice(),
        },
      };
      this.refreshColors();
      this.onDragChange?.(true);
    }, { capture: true });

    this.canvas.addEventListener('pointermove', (e) => {
      if (this.drag) {
        if (e.pointerId !== this.drag.pointerId) return;
        e.stopImmediatePropagation();
        e.preventDefault();
        this.applyDrag(e);
        return;
      }
      if (!this.root.visible) {
        if (this.hovered) { this.hovered = null; this.refreshColors(); }
        return;
      }
      const hit = this.pickHandle(e.clientX, e.clientY);
      const next = hit ? hit.axis : null;
      if (next !== this.hovered) {
        this.hovered = next;
        this.refreshColors();
        this.canvas.style.cursor = next ? 'grab' : '';
      }
    }, { capture: true });

    const endDrag = (e) => {
      if (!this.drag || e.pointerId !== this.drag.pointerId) return;
      e.stopImmediatePropagation();
      this.drag = null;
      this.canvas.style.cursor = this.hovered ? 'grab' : '';
      this.refreshColors();
      this.onDragChange?.(false);
    };
    this.canvas.addEventListener('pointerup', endDrag, { capture: true });
    this.canvas.addEventListener('pointercancel', endDrag, { capture: true });
  }

  applyDrag(e) {
    const d = this.drag;
    const snap = e.ctrlKey || e.metaKey;
    const next = {
      location: d.startTransform.location.slice(),
      rotation: d.startTransform.rotation.slice(),
      scale: d.startTransform.scale.slice(),
    };

    if (d.mode === 'translate') {
      const cur = this.rayPlanePoint(e.clientX, e.clientY, d.dragPlane);
      if (!cur) return;
      const delta = cur.sub(d.startPoint);
      const ax = this.axisDir(d.axis);
      if (ax) delta.copy(ax).multiplyScalar(delta.dot(ax));
      if (snap) {
        delta.x = Math.round(delta.x * 4) / 4;
        delta.y = Math.round(delta.y * 4) / 4;
        delta.z = Math.round(delta.z * 4) / 4;
      }
      next.location = [
        +(d.startTransform.location[0] + delta.x).toFixed(4),
        +(d.startTransform.location[1] + delta.y).toFixed(4),
        +(d.startTransform.location[2] + delta.z).toFixed(4),
      ];
    } else if (d.mode === 'rotate') {
      const cur = this.rayPlanePoint(e.clientX, e.clientY, d.dragPlane);
      if (!cur) return;
      const v0 = d.startPoint.clone().sub(d.pivot);
      const v1 = cur.clone().sub(d.pivot);
      if (v0.lengthSq() < 1e-6 || v1.lengthSq() < 1e-6) return;
      v0.normalize();
      v1.normalize();
      const n = d.dragPlane.normal;
      const angle = Math.atan2(new THREE.Vector3().crossVectors(v0, v1).dot(n), v0.dot(v1));
      let deg = angle * RAD;
      if (snap) deg = Math.round(deg / 15) * 15;
      const idx = d.axis === 'x' ? 0 : d.axis === 'y' ? 1 : 2;
      next.rotation[idx] = +(d.startTransform.rotation[idx] + deg).toFixed(2);
    } else if (d.mode === 'scale') {
      const cur = this.rayPlanePoint(e.clientX, e.clientY, d.dragPlane);
      let factorDelta = 0;
      if (cur && d.axis !== 'xyz') {
        const ax = this.axisDir(d.axis);
        factorDelta = cur.sub(d.startPoint).dot(ax) * 0.8;
      } else {
        factorDelta = ((e.clientX - d.startClient[0]) - (e.clientY - d.startClient[1])) * 0.008;
      }
      const mult = Math.max(0.05, 1 + factorDelta);
      const snappedMult = snap ? Math.max(0.1, Math.round(mult * 10) / 10) : mult;
      if (d.axis === 'xyz') {
        next.scale = d.startTransform.scale.map((s) => +(s * snappedMult).toFixed(4));
      } else {
        const idx = d.axis === 'x' ? 0 : d.axis === 'y' ? 1 : 2;
        next.scale[idx] = +(d.startTransform.scale[idx] * snappedMult).toFixed(4);
      }
    }

    this.target.setTransform(next);
    this.onChange?.(next);
  }

  render(renderer) {
    this.syncPose();
    if (!this.root.visible) return;
    const prevAuto = renderer.autoClear;
    renderer.autoClear = false;
    renderer.clearDepth();
    renderer.render(this.scene, this.camera);
    renderer.autoClear = prevAuto;
  }
}
