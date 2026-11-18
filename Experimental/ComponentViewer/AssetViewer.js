// Component Viewer (formerly Vehicle Lab): a Blueprint-class style editor. Left: the Components tree (scene
// components with transforms, then actor components) and the Content tray of compiled assets. Centre: one tab per
// open blueprint with the Viewport (inspection channels, simulate) or the Construction Script graph, plus a drawer
// with the compiled data of the selected mesh and Vehicle Lab's response curves. Right: Details for the selection.
import { $, el, iconEl, section, panel, rangeField, segmented, segmentedField, toggle, selectField, vec3Field, colorField, textField, rows, metrics, toast, download } from '../Shared/Studio/Ui.js';
import { icon, hydrateIcons } from '../Shared/Studio/Icons.js';
import {
  COMPONENT_CLASSES, SCENE_PROPS, COMMON_PROPS, findComponent, childrenOf, actorComponents, addComponent, removeComponent,
  attachComponent, duplicateComponent, renameComponent, walkScene, touch, validateBlueprint, sampleCurve, stepDrive, createDriveState, VEHICLE_SHEET,
} from './Blueprint.js';
import { STEPS, isCompiled, fmt, formatMs } from '../Shared/GeometryCompile/CompileSteps.js';
import { THREE, Stage, meshObject, collisionObject, assetSlots } from '../Shared/Studio/Render.js';
import { fieldShell, beam, glowSprite, solidArrow, anchorMark, addHalo, cameraModel } from '../Shared/Studio/Visuals.js';
import { createCameraHud } from '../Shared/Studio/Camera.js';
import { SOCKET_KINDS, SOCKET_KIND, SOCKET_FIELDS, SOCKET_PARAMS, DRIVES, socketDefaults, createSocketState, stepSocket, socketPose, socketReadout, rotateVector, dockAccepts, poseLabel } from './Sockets.js';
import { socketVisual } from './SocketVisuals.js';
import { createAssetInspection } from './AssetInspection.js';
import { createGeometryEditor } from './StudioGeometryEditor.js';
import { createUvWorkspace } from './StudioUvWorkspace.js';
import { createSdfWorkspace } from './StudioSdfWorkspace.js';
import { createClusterWorkspace } from './ClusterWorkspace.js';
import { mountWorldMaterial } from './vendor/WorldMaterial.js';
import { layoutStatistics } from '../Shared/GeometryCompile/Engine/UvCharts.js';
import { invalidate, runStep } from '../Shared/GeometryCompile/CompileSteps.js';

const CHANNELS = [
  { id: 'lit', label: 'Lit', key: '1' }, { id: 'unlit', label: 'Unlit', key: '2' }, { id: 'density', label: 'Density', key: '3' },
  { id: 'normals', label: 'Normals', key: '4' }, { id: 'uv', label: 'UV checker', key: '5' }, { id: 'lod', label: 'LOD clusters', key: '6' },
  { id: 'collision', label: 'Collision', key: '7' },
];
const CHANNEL_NOTE = {
  lit: 'Lit · material slots', unlit: 'Unlit · base colour', density: 'Triangle density · red dense, blue sparse', normals: 'Corner normals (compiled)',
  uv: 'UV checker · compiled charts', lod: 'Cluster colours · use the Cluster LOD slider to preview a level', collision: 'Compiled collision field (green) · collision components (amber)',
};
const DEG = Math.PI / 180;
const AMBER = '#ffb238';

export function createComponentViewer({ assets, iconRenderer, openCompile, importFiles, samples, worldModels = [], resolveWorldModel, compileContext }) {
  const stage = new Stage($('Viewport'), { background: '#0b0b0d' });
  stage.raycaster.params.Line.threshold = 0.05;
  const blueprints = [];
  let geometryTarget = null, uvTarget = null, sdfTarget = null;
  const clusterWorkspace = createClusterWorkspace({ ctx: compileContext, onClose() { $('Studio').hidden = false; rebuildScene(); renderAll(); }, onAssetUpdated() { touch(bp); } });
  const sdfWorkspace = createSdfWorkspace({
    getGeometry: () => sdfTarget?.out.geometry, getName: () => sdfTarget?.name || '', toast,
    onVolume(volume, geometry) {
      if (!sdfTarget || sdfTarget.out.geometry !== geometry) return;
      sdfTarget.sparseSdf = volume; sdfTarget.sparseSdfSource = geometry;
      sdfTarget.steps.sdf.state = 'done'; sdfTarget.steps.sdf.summary = `${volume.dim}³ sparse`; touch(bp);
      if (!sdfWorkspace.open) renderAll();
    },
    onClose() { $('Studio').hidden = false; renderAll(); },
  });
  const uvWorkspace = createUvWorkspace({
    getGeometry: () => uvTarget?.out.geometry,
    getName: () => uvTarget?.name || '', toast,
    getLayout: () => uvTarget?.steps.uv.state === 'done' ? uvTarget.out.uv.layout : null,
    onChange(layout, uvs) {
      if (!uvTarget) return;
      uvTarget.out.uv = { layout, uvs, stats: layoutStatistics(layout, uvs), seams: layout.seams.length };
      uvTarget.steps.uv.state = 'done'; uvTarget.steps.uv.summary = `${layout.charts.length} charts · ${layout.seams.length} seams`;
      uvTarget.revision++; touch(bp);
    },
    onClose() { $('Studio').hidden = false; renderAll(); },
  });
  const geometryWorkspace = createGeometryEditor({
    getBase: () => geometryTarget?.out.source,
    getTransform: () => geometryTarget.settings.geometry,
    getName: () => geometryTarget?.name || '',
    toast,
    onClose() { $('Studio').hidden = false; },
    async applyTransform(transform) {
      const target = geometryTarget;
      const previousUv = target.out.uv?.authored ? target.out.uv : null;
      const previousIndices = target.out.geometry.indices;
      Object.assign(target.settings.geometry, transform);
      invalidate(target, 'geometry');
      try {
        await runStep(target, 'geometry', compileContext);
        if (previousUv && previousIndices.length === target.out.geometry.indices.length) {
          const uvs = new Float32Array(previousUv.uvs.length), next = target.out.geometry.indices;
          for (let t = 0; t < next.length; t += 3) for (let k = 0; k < 3; k++) {
            let old = 0; while (old < 3 && previousIndices[t + old] !== next[t + k]) old++;
            if (old === 3) old = k;
            uvs[(t + k) * 2] = previousUv.uvs[(t + old) * 2]; uvs[(t + k) * 2 + 1] = previousUv.uvs[(t + old) * 2 + 1];
          }
          target.out.uv = { ...previousUv, uvs }; target.steps.uv.state = 'done'; target.steps.uv.summary = 'Imported UV coordinates'; target.revision++;
        }
        touch(bp); rebuildScene(); renderAll();
      }
      catch (error) { toast(`Geometry: ${error.message || error}`); }
    },
  });
  let inspectorEditor = null, disposeMaterial = null;
  const lodPreviewLevels = new WeakMap();
  let bp = null, selectedId = null, channel = 'lit', centreMode = 'viewport', simulate = false, drawerTab = 'compiled', leftTab = 'components';
  let objects = new Map(), selection = null, drive = createDriveState();
  const graphState = new Map(); // bp.id → { nodes positions, pan }
  let sockets = new Map();             // component id → { c, node, mount, vis } for the current scene
  let socketStates = new Map();        // component id → socket state (survives rebuilds; reset when Simulate stops)

  // ------------------------------------------------------------------------------------------------ blueprints
  function open(blueprint) {
    if (!blueprints.includes(blueprint)) blueprints.push(blueprint);
    bp = blueprint;
    inspectorEditor = null;
    selectedId = bp.rootId;
    drive = createDriveState();
    socketStates = new Map();
    rebuildScene();
    frameAll();
    renderAll();
  }

  function closeTab(blueprint) {
    const i = blueprints.indexOf(blueprint);
    if (i < 0 || blueprints.length === 1) { toast('The last blueprint stays open'); return; }
    blueprints.splice(i, 1);
    if (bp === blueprint) open(blueprints[Math.max(0, i - 1)]);
    else renderTabs();
  }

  const selected = () => (bp ? findComponent(bp, selectedId) : null);

  const SELECT_DRAWN = new Set(['PointLight', 'SpotLight', 'Camera', 'Socket']); // visuals that change with selection
  function select(id) {
    const before = selected();
    if (id !== selectedId) { inspectorEditor = null; const guide = $('AssetIconGuide'); if (guide) guide.hidden = true; }
    selectedId = id;
    const after = selected();
    renderTree();
    renderDetails();
    renderDrawer();
    if (channel === 'lod' || (before && SELECT_DRAWN.has(before.type)) || (after && SELECT_DRAWN.has(after.type))) rebuildScene();
    else updateSelection();
    if (centreMode === 'graph') renderGraph();
  }

  function changed({ scene = true, tree = true, details = false } = {}) {
    touch(bp);
    if (scene) rebuildScene();
    if (tree) renderTree();
    if (details) renderDetails();
    renderHeading();
    renderTabs();
    if (centreMode === 'graph') renderGraph();
  }

  // ----------------------------------------------------------------------------------------------- scene build
  function rebuildScene() {
    objects = new Map();
    sockets = new Map();
    const root = new THREE.Group();
    walkScene(bp, (c) => {
      const node = buildComponent(c);
      objects.set(c.id, node);
      const parent = c.parent ? objects.get(c.parent) : null;
      (parent ? parent.userData.mount || parent : root).add(node); // socket children ride on the joint frame
    });
    stepSockets(0);
    stage.setContent(root);
    stage.xray = sockets.has(selectedId);
    selection = null;
    updateSelection();
  }

  function buildComponent(c) {
    const node = new THREE.Group();
    node.userData.componentId = c.id;
    applyTransform(node, c);
    node.visible = c.props.visible !== false;
    const p = c.props;
    const gizmo = (object, pickRadius = 0.15) => {
      object.traverse((o) => { o.userData.gizmo = true; });
      node.add(object);
      const pick = new THREE.Mesh(new THREE.SphereGeometry(pickRadius, 8, 6), new THREE.MeshBasicMaterial({ visible: false }));
      node.add(pick);
    };
    switch (c.type) {
      case 'StaticMesh': {
        const asset = p.asset && assets.get(p.asset);
        if (asset && asset.out.geometry) {
          const ch = (channel === 'collision' && p.collisionPreset === 'NoCollision') || (channel === 'lod' && asset.steps.lod.state !== 'done') ? 'lit' : channel;
          const object = meshObject(asset, { channel: ch, lod: ch === 'lod' ? (c.id === selectedId ? (lodPreviewLevels.get(c) || 0) : 0) : -1, overrides: p.slots, castShadow: p.castShadow });
          if (ch === 'lit' && p.materialEdits) {
            const mats = object.userData.mesh?.material;
            const slots = assetSlots(asset);
            (Array.isArray(mats) ? mats : [mats]).forEach((mat, i) => {
              const edit = p.materialEdits[slots[i]?.family];
              if (mat && edit) { mat.roughness = edit.Channels.roughness.Fill; mat.metalness = edit.Channels.metallic.Fill; }
            });
          }
          node.add(object);
        } else gizmo(fieldShell({ type: 'box', centre: [0, 0, 0], half: [0.15, 0.15, 0.15] }, '#ff6b5e', { pulse: 1, scan: 0 }), 0.2); // no mesh assigned
        break;
      }
      case 'PointLight': {
        const light = new THREE.PointLight(p.color, p.intensity, p.range, 2);
        node.add(light);
        gizmo(new THREE.Mesh(new THREE.SphereGeometry(0.045, 16, 12), new THREE.MeshBasicMaterial({ color: p.color })));
        gizmo(glowSprite(p.color, 0.55));
        // Influence: a faint additive field the size of the attenuation radius, shown while selected.
        if (c.id === selectedId) gizmo(fieldShell({ type: 'sphere', centre: [0, 0, 0], radius: p.range }, p.color, { base: 0.015, rim: 0.22, scan: 0.06, additive: true }));
        break;
      }
      case 'SpotLight': {
        const light = new THREE.SpotLight(p.color, p.intensity, p.range, p.angle * DEG, 0.35, 2);
        light.target.position.set(1, 0, 0);
        node.add(light, light.target);
        const len = Math.min(p.range, 2.5), r = Math.tan(p.angle * DEG) * len;
        gizmo(beam({ length: len, radius: r, color: p.color, strength: c.id === selectedId ? 0.45 : 0.28 }));
        gizmo(new THREE.Mesh(new THREE.SphereGeometry(0.04, 14, 10), new THREE.MeshBasicMaterial({ color: p.color })));
        gizmo(glowSprite(p.color, 0.45));
        break;
      }
      case 'Camera': {
        const cam = new THREE.PerspectiveCamera(p.fov, 16 / 9, 0.1, 0.9);
        cam.rotation.y = -Math.PI / 2;
        node.add(cam);
        const active = c.id === selectedId;
        const model = cameraModel({ active });
        gizmo(model.group, 0.2);
        const view = frustumBeam(p.fov, active);
        view.position.x = model.lensFront;
        node.add(view);
        break;
      }
      case 'BoxCollision': gizmo(collisionObject({ type: 'box', centre: [0, 0, 0], half: p.extent }, AMBER), 0.1); break;
      case 'SphereCollision': gizmo(collisionObject({ type: 'sphere', centre: [0, 0, 0], radius: p.radius }, AMBER), 0.1); break;
      case 'CapsuleCollision': gizmo(collisionObject({ type: 'capsule', centre: [0, 0, 0], axis: 1, radius: p.radius, halfHeight: p.halfHeight }, AMBER), 0.1); break;
      case 'Arrow': gizmo(solidArrow(p.length, p.color), 0.12); break;
      case 'Origin': gizmo(anchorMark(0.11, '#e8e8e8'), 0.12); break;
      case 'Scene': gizmo(anchorMark(0.07, '#6aa7c7'), 0.1); break;
      case 'Socket': {
        const accepts = p.kind !== 'dock' || childrenOf(bp, c.id).every((k) => dockAccepts(p.key, k.props.tags));
        const vis = socketVisual(p, { selected: c.id === selectedId, accepts });
        gizmo(vis.group, 0.08);
        // The selected socket draws in the x-ray pass, so a spring inside a tyre or a dock inside the body stays visible.
        if (c.id === selectedId) vis.group.traverse((o) => o.layers.set(1));
        const mount = new THREE.Group();
        node.add(mount);
        node.userData.mount = mount;
        sockets.set(c.id, { c, node, mount, vis, accepts });
        break;
      }
    }
    return node;
  }

  // The camera's view as a light volume: a four-sided beam at the lens aspect that fades with distance.
  function frustumBeam(fov, active) {
    const len = active ? 2.4 : 1.1, h = Math.tan((fov * DEG) / 2) * len;
    return beam({ length: len, radius: h * Math.SQRT2, sides: 4, aspect: 16 / 9, color: '#bcd3ff', strength: active ? 0.3 : 0.16 });
  }

  function applyTransform(node, c) {
    if (!COMPONENT_CLASSES[c.type].scene) return;
    const p = c.props;
    node.position.set(...p.location);
    node.rotation.set(p.rotation[0] * DEG, p.rotation[1] * DEG, p.rotation[2] * DEG, 'YZX');
    node.scale.set(...p.scale);
  }

  // Bounds of one component's own geometry (child components excluded, gizmo pick spheres ignored).
  function componentBox(node, recurse = false) {
    const box = new THREE.Box3();
    node.updateMatrixWorld(true);
    const visit = (o) => {
      if (o !== node && o.userData.componentId && !recurse) return;
      if (o.visible === false) return;
      if ((o.isMesh || o.isLineSegments || o.isLine) && o.geometry) {
        const m = Array.isArray(o.material) ? o.material[0] : o.material;
        if (!m || m.visible !== false) {
          if (!o.geometry.boundingBox) o.geometry.computeBoundingBox();
          box.union(o.geometry.boundingBox.clone().applyMatrix4(o.matrixWorld));
        }
      }
      for (const ch of o.children) visit(ch);
    };
    visit(node);
    return box;
  }

  // Selection: the selected surfaces glow amber toward their silhouette (tinted in their own material, no extra mesh).
  function updateSelection() {
    if (selection) selection.restore();
    selection = null;
    const node = objects.get(selectedId);
    if (!node || selectedId === bp.rootId) return;
    selection = { node, restore: addHalo(node) };
  }

  function frameAll() {
    const box = new THREE.Box3();
    for (const [id, node] of objects) {
      const c = findComponent(bp, id);
      if (c && c.type === 'StaticMesh' && node.visible) box.union(componentBox(node));
    }
    if (box.isEmpty()) box.setFromCenterAndSize(new THREE.Vector3(0, 0.5, 0), new THREE.Vector3(2, 1, 2));
    stage.frame(box, { yaw: 48, pitch: 18 });
  }

  function frameSelected() {
    const node = objects.get(selectedId);
    if (!node || selectedId === bp.rootId) return frameAll();
    const box = componentBox(node);
    if (box.isEmpty()) return;
    stage.frame(box.expandByScalar(0.2), { keepAngle: true });
  }

  // ---------------------------------------------------------------------------------------------- simulation
  stage.frameCallbacks.add((dt) => {
    if (!bp) return;
    if (simulate) {
      for (const c of actorComponents(bp)) {
        if (c.type === 'RotatingMovement') {
          const node = objects.get(c.props.target || bp.rootId);
          if (node) node.rotation.y += c.props.rate * DEG * dt;
        }
        if (c.type === 'VehicleMovement') {
          stepDrive(drive, c.props, dt);
          for (const id of c.props.wheels) {
            const node = objects.get(id), comp = findComponent(bp, id);
            if (!node || !comp) continue;
            applyTransform(node, comp);
            const side = Math.cos(comp.props.rotation[1] * DEG) >= 0 ? 1 : -1;
            node.rotateZ(-drive.wheelAngle * side);
          }
          stage.grid.position.x = -(drive.distance % 1);
          speedFeel(dt, drive.speed, drive.rpm / c.props.redlineRpm);
          updateHud(c);
        }
      }
    }
    stepSockets(dt);
    updateReadout();
  });

  // ------------------------------------------------------------------------------------------------- sockets
  // Every frame (editor and Simulate): step each socket, put its joint frame on the mount its children hang from,
  // move the live parts of its debug look, and surface connect / release events as pings in the viewport.
  const tmp = new THREE.Vector3();
  function stepSockets(dt) {
    if (!bp) return;
    const vm = actorComponents(bp).find((x) => x.type === 'VehicleMovement');
    const moving = simulate && vm;
    const downforce = moving ? 0.5 * 1.225 * vm.props.liftArea * drive.speed * drive.speed : 0;
    for (const [id, s] of sockets) {
      let state = socketStates.get(id);
      if (!state) { state = createSocketState(); socketStates.set(id, state); }
      s.node.updateMatrixWorld();
      const along = tmp.setFromMatrixPosition(s.node.matrixWorld).x;
      const events = stepSocket(state, s.c.props, dt, {
        simulate, accel: moving ? drive.acceleration : 0, speed: moving ? drive.speed : 0, distance: moving ? drive.distance : undefined,
        along, downforce: downforce * 0.35, accepts: s.accepts,
      });
      const pose = socketPose(s.c.props, state);
      s.mount.quaternion.fromArray(pose.rotation);
      s.mount.position.fromArray(rotateVector(pose.rotation, pose.offset));
      s.vis.update(state, pose);
      for (const e of events) ping(s, e);
    }
    updateSocketTag();
  }

  const PING_TEXT = { connected: 'connected', released: 'released', rejected: 'rejected · wrong key' };
  function ping(s, e) {
    if (centreMode !== 'viewport') return;
    const at = projectToWell(s.node);
    if (!at) return;
    const kind = e.type === 'connected' ? 'ok' : e.type === 'rejected' ? 'bad' : 'warn';
    const text = `${s.c.name} · ${PING_TEXT[e.type]}${e.load ? ` at ${(e.load / 1000).toFixed(2)} kN` : ''}`;
    const node = el('div', { class: `socket-ping ${kind}`, style: { left: `${at.x}px`, top: `${at.y}px` } }, text);
    $('ViewportWell').append(node);
    setTimeout(() => node.remove(), 1800);
  }

  function projectToWell(node, lift = 0) {
    const well = $('ViewportWell'), rect = well.getBoundingClientRect(), canvasRect = stage.canvas.getBoundingClientRect();
    node.updateMatrixWorld();
    const v = new THREE.Vector3().setFromMatrixPosition(node.matrixWorld);
    v.y += lift;
    v.project(stage.camera);
    if (v.z > 1 || v.z < -1) return null;
    return { x: canvasRect.left - rect.left + ((v.x + 1) / 2) * canvasRect.width, y: canvasRect.top - rect.top + ((1 - v.y) / 2) * canvasRect.height };
  }

  // A floating tag over the selected socket: kind, name and the live value.
  const socketTag = el('div', { class: 'socket-tag', hidden: '' });
  $('ViewportWell').append(socketTag);
  let socketLive = null; // live readout element in Details
  function updateSocketTag() {
    const s = sockets.get(selectedId);
    if (!s || centreMode !== 'viewport') { socketTag.hidden = true; return; }
    const state = socketStates.get(selectedId);
    const kind = SOCKET_KIND[s.c.props.kind] || SOCKET_KIND.fixed;
    const text = state ? socketReadout(s.c.props, state) : '';
    const at = projectToWell(s.node);
    if (!at) { socketTag.hidden = true; return; }
    socketTag.hidden = false;
    socketTag.style.setProperty('--k', kind.color);
    socketTag.style.transform = `translate(${at.x.toFixed(1)}px, ${at.y.toFixed(1)}px)`;
    const html = `<i></i><b>${esc(kind.label)}</b><span>${esc(s.c.name)}</span><em>${esc(text)}</em>`;
    if (socketTag.dataset.html !== html) { socketTag.innerHTML = html; socketTag.dataset.html = html; }
    if (socketLive && socketLive.isConnected) socketLive.textContent = text;
  }

  // While simulating, speed opens the lens a little and high rpm adds a faint engine shimmer to the camera.
  function speedFeel(dt, speed, rpmShare) {
    const kick = stage.rig.kick, k = 1 - Math.exp(-dt * 3);
    kick.fov += (Math.min(9, speed * 0.12) - kick.fov) * k;
    const shake = Math.max(0, rpmShare - 0.75) * 0.012 * stage.rig.distance * 0.2;
    const t = performance.now() / 1000;
    kick.offset.set(Math.sin(t * 61) * shake, Math.sin(t * 47 + 1.3) * shake, Math.sin(t * 53 + 2.1) * shake);
  }

  function setSimulate(on) {
    simulate = on;
    $('SimulateBtn').classList.toggle('active', on);
    $('SimulateBtn').querySelector('.label').textContent = on ? 'Stop' : 'Simulate';
    $('SimulateBtn').querySelector('.ico').innerHTML = icon(on ? 'stop' : 'play', 14);
    const vm = actorComponents(bp).find((c) => c.type === 'VehicleMovement');
    $('SimHud').hidden = !(on && vm);
    if (!on) { drive = createDriveState(); socketStates = new Map(); stage.grid.position.x = 0; stage.rig.kick.fov = 0; stage.rig.kick.offset.set(0, 0, 0); rebuildScene(); }
  }

  function updateHud(vm) {
    $('SimSpeed').textContent = `${(drive.speed * 3.6).toFixed(0)} km/h`;
    $('SimRpm').textContent = `${drive.rpm.toFixed(0)} rpm`;
    $('SimAccel').textContent = `${drive.acceleration.toFixed(1)} m/s²`;
    $('SimTorque').textContent = `${drive.torque.toFixed(0)} N·m`;
    $('SimGear').textContent = `GEAR ${drive.gear + 1}`;
    $('SimRpmBar').style.width = `${(drive.rpm / vm.props.redlineRpm) * 100}%`;
  }

  // ----------------------------------------------------------------------------------------------- readout
  function updateReadout() {
    let tris = 0, meshes = 0;
    for (const c of bp.components) {
      if (c.type !== 'StaticMesh' || !c.props.asset || c.props.visible === false) continue;
      const a = assets.get(c.props.asset);
      if (!a || !a.out.geometry) continue;
      meshes++;
      if (channel === 'lod' && a.steps.lod.state === 'done' && a.out.lod) {
        const L = a.out.lod.levels[Math.max(0, Math.min(a.out.lod.levels.length - 1, c.id === selectedId ? (lodPreviewLevels.get(c) || 0) : 0))];
        tris += L.triangles;
      } else tris += a.out.geometry.indices.length / 3;
    }
    const s = selected();
    $('Readout').textContent = [
      `${stage.stats.fps.toFixed(0).padStart(3)} fps   ${stage.stats.ms.toFixed(1)} ms`,
      `tris     ${fmt(tris)}`,
      `meshes   ${meshes} · ${bp.components.length} components`,
      `select   ${s ? s.name : '—'}`,
    ].join('\n');
  }

  // ------------------------------------------------------------------------------------------------- render
  function renderAll() {
    renderTabs();
    renderHeading();
    renderTree();
    renderDetails();
    renderDrawer();
    if (centreMode === 'graph') renderGraph();
  }

  function renderTabs() {
    const strip = $('BlueprintTabs');
    strip.innerHTML = '';
    for (const b of blueprints) {
      const close = el('span', { class: 'tab-close', title: 'Close', html: icon('x', 10) });
      close.addEventListener('click', (e) => { e.stopPropagation(); closeTab(b); });
      const tab = el('button', { type: 'button', class: `document-tab ${b === bp ? 'active' : ''}` }, b.icon ? el('img', { src: b.icon, class: 'tab-thumb', alt: '' }) : iconEl('cube', 12), el('span', {}, b.name + (b.dirty ? ' •' : '')), close);
      tab.addEventListener('click', () => { if (b !== bp) open(b); });
      strip.append(tab);
    }
  }

  function renderHeading() {
    $('BpTitle').textContent = bp.name;
    $('BpSubtitle').textContent = `Blueprint class · Parent ${bp.parentClass} · ${bp.components.length} components`;
    const issues = validateBlueprint(bp, assets);
    const tag = $('BpState');
    if (issues.some((i) => i.level === 'error')) { tag.textContent = 'ERRORS'; tag.className = 'state-tag bad'; }
    else if (bp.dirty) { tag.textContent = 'DIRTY'; tag.className = 'state-tag'; }
    else { tag.textContent = 'COMPILED'; tag.className = 'state-tag live'; }
    $('CompileBpBtn').classList.toggle('stale', bp.dirty);
    $('Strip').textContent = `COMPONENT VIEWER · ${bp.name.toUpperCase()} · ${CHANNEL_NOTE[channel].toUpperCase()} · ${simulate ? 'SIMULATING' : 'EDITING'}`;
  }

  function compileBlueprint() {
    const issues = validateBlueprint(bp, assets);
    const errors = issues.filter((i) => i.level === 'error');
    if (errors.length) { toast(`${errors.length} error${errors.length > 1 ? 's' : ''}: ${errors[0].text}`); select(bp.rootId); renderHeading(); return; }
    bp.dirty = false;
    bp.compiledRevision = bp.revision;
    bp.icon = bakeBlueprintIcon();
    renderTabs();
    renderHeading();
    renderDetails();
    toast(issues.length ? `Compiled with ${issues.length} warning${issues.length > 1 ? 's' : ''}` : `${bp.name} compiled`);
  }

  function bakeBlueprintIcon() {
    const group = new THREE.Group();
    const nodes = new Map();
    walkScene(bp, (c) => {
      const node = new THREE.Group();
      applyTransform(node, c);
      node.visible = c.props.visible !== false;
      const a = c.type === 'StaticMesh' && c.props.asset && assets.get(c.props.asset);
      if (a && a.out.geometry) node.add(meshObject(a, { channel: 'lit', lod: -1, overrides: c.props.slots }));
      nodes.set(c.id, node);
      (c.parent ? nodes.get(c.parent) : group).add(node);
    });
    return iconRenderer.render(group, { size: 128, yaw: 40, pitch: 20, background: 'transparent' });
  }

  // ---------------------------------------------------------------------------------------------- tree (left)
  function renderTree() {
    const tree = $('ComponentTree');
    const filter = $('ComponentSearch').value.trim().toLowerCase();
    tree.innerHTML = '';
    let count = 0;
    walkScene(bp, (c, depth) => {
      count++;
      if (filter && !c.name.toLowerCase().includes(filter) && !COMPONENT_CLASSES[c.type].label.toLowerCase().includes(filter)) return;
      tree.append(treeRow(c, filter ? 0 : depth));
    });
    const actors = actorComponents(bp);
    tree.append(el('div', { class: 'tree-divider' }, el('span', {}, 'ACTOR COMPONENTS'), el('span', { class: 'count' }, String(actors.length))));
    if (!actors.length) tree.append(el('p', { class: 'tree-empty' }, 'Movement and logic components without a transform appear here.'));
    for (const c of actors) if (!filter || c.name.toLowerCase().includes(filter)) tree.append(treeRow(c, 0, true));
    count += actors.length;
    let tris = 0;
    for (const c of bp.components) if (c.type === 'StaticMesh' && c.props.asset) { const a = assets.get(c.props.asset); if (a && a.out.geometry) tris += a.out.geometry.indices.length / 3; }
    $('StatComponents').textContent = String(count);
    $('StatTriangles').textContent = fmt(tris);
    $('BpCaption').textContent = bp.name;
  }

  function treeRow(c, depth, actor = false) {
    const cls = COMPONENT_CLASSES[c.type];
    const asset = c.type === 'StaticMesh' && c.props.asset ? assets.get(c.props.asset) : null;
    const sk = c.type === 'Socket' ? SOCKET_KIND[c.props.kind] || SOCKET_KIND.fixed : null;
    const sub = c.type === 'StaticMesh' ? (asset ? asset.name : 'No mesh') : sk ? `${sk.label} socket${c.props.kind === 'dock' && c.props.key ? ` · key ${c.props.key}` : ''}` : cls.label;
    const thumb = asset && asset.out.icon ? el('img', { class: 'row-thumb', src: asset.out.icon, alt: '' }) : iconEl(sk ? sk.icon : cls.icon, 16, 'entity-icon');
    if (sk) thumb.style.color = sk.color;
    const kids = childrenOf(bp, c.id).length;
    const eye = !actor && c.id !== bp.rootId ? el('button', { type: 'button', class: `eye ${c.props.visible === false ? 'off' : ''}`, title: 'Toggle visibility', html: icon(c.props.visible === false ? 'eyeOff' : 'eye', 13) }) : el('span');
    if (eye.tagName === 'BUTTON') eye.addEventListener('click', (e) => { e.stopPropagation(); c.props.visible = c.props.visible === false; changed(); if (c.id === selectedId) renderDetails(); });
    const issues = validateBlueprint(bp, assets).filter((i) => i.id === c.id);
    const warn = issues.length ? el('span', { class: `row-warn ${issues.some((i) => i.level === 'error') ? 'bad' : ''}`, title: issues.map((i) => i.text).join('\n'), html: icon('warn', 12) }) : null;
    const row = el('div', {
      class: `entity comp-row ${c.id === selectedId ? 'active' : ''} ${c.id === bp.rootId ? 'root' : ''}`, role: 'treeitem', tabindex: 0,
      draggable: !actor && c.id !== bp.rootId ? 'true' : null, dataset: { id: c.id }, style: { paddingLeft: `${10 + depth * 16}px` },
    },
    el('span', { class: 'tree-caret-sm' }, kids ? '▾' : ''), thumb,
    el('div', {}, el('b', {}, c.name, c.id === bp.rootId ? el('em', { class: 'root-tag' }, 'ROOT') : null), el('small', {}, sub)), warn || el('span'), eye);
    row.addEventListener('click', () => select(c.id));
    row.addEventListener('dblclick', () => { select(c.id); frameSelected(); });
    if (!actor) {
      row.addEventListener('dragstart', (e) => { e.dataTransfer.setData('text/component', c.id); e.dataTransfer.effectAllowed = 'move'; });
      row.addEventListener('dragover', (e) => { if ([...e.dataTransfer.types].includes('text/component')) { e.preventDefault(); row.classList.add('drop'); } });
      row.addEventListener('dragleave', () => row.classList.remove('drop'));
      row.addEventListener('drop', (e) => {
        e.preventDefault(); row.classList.remove('drop');
        const id = e.dataTransfer.getData('text/component');
        if (attachComponent(bp, id, c.id)) { changed(); toast(`${findComponent(bp, id).name} attached to ${c.name}`); }
        else toast('Cannot attach there');
      });
    }
    return row;
  }

  // ------------------------------------------------------------------------------------------- add component
  function renderAddMenu() {
    const menu = $('AddMenu');
    const q = $('AddSearch').value.trim().toLowerCase();
    const list = $('AddList');
    list.innerHTML = '';
    let count = 0;
    const matches = (text) => !q || text.toLowerCase().includes(q);
    function section(title, caption, cards) {
      if (!cards.length) return;
      list.append(el('div', { class: 'add-section-heading' }, el('b', {}, title), el('small', {}, caption)), el('div', { class: 'add-card-grid' }, ...cards));
      count += cards.length;
    }
    function card(iconName, title, description, handler, color = '') {
      const b = el('button', { type: 'button', class: 'menu-item add-card', style: color ? { '--accent': color } : {} },
        iconEl(iconName, 17, 'add-card-icon'), el('span', { class: 'add-card-copy' }, el('b', {}, title), el('small', {}, description)), el('span', { class: 'add-card-plus', 'aria-hidden': 'true' }, '+'));
      b.addEventListener('click', () => { handler(); menu.hidden = true; });
      return b;
    }
    section('Instance', 'Place an asset', matches('instance asset mesh placeholder') ? [
      card('cube', 'Instance', 'An empty asset placement', () => add('StaticMesh', { importPlaceholder: true }, 'Instance'), '#d9a85e'),
    ] : []);
    section('Sockets', 'Connect & move', SOCKET_KINDS.filter((k) => matches(`${k.label} socket ${k.note}`)).map((k) =>
      card(k.icon, `${k.label} socket`, k.note.split('.')[0], () => add('Socket', { kind: k.id, drive: DRIVES[k.id] ? DRIVES[k.id][0][0] : 'hold' }, `${k.label}Socket`), k.color)));
    section('Lights', 'Illuminate the scene', [['PointLight', 'Light in every direction'], ['SpotLight', 'A focused cone of light']].filter(([type]) => matches(`${COMPONENT_CLASSES[type].label} light`)).map(([type, description]) =>
      card(COMPONENT_CLASSES[type].icon, COMPONENT_CLASSES[type].label, description, () => add(type), '#f2c575')));
    if (!count) list.append(el('p', { class: 'add-empty' }, 'No matching items. Try “socket”, “light” or “instance”.'));
  }

  function add(type, props = {}, name = null) {
    const s = selected();
    const parentId = s && COMPONENT_CLASSES[s.type].scene ? s.id : bp.rootId;
    const c = addComponent(bp, type, { parentId, props, name });
    if (type === 'VehicleMovement') c.props.wheels = bp.components.filter((x) => x.type === 'StaticMesh' && /wheel|tyre|tire/i.test(x.name)).map((x) => x.id).slice(0, 4);
    changed();
    select(c.id);
    toast(`${type === 'StaticMesh' && props.importPlaceholder ? 'Instance' : COMPONENT_CLASSES[type].label} added${COMPONENT_CLASSES[type].scene ? ` under ${findComponent(bp, c.parent)?.name}` : ''}`);
    return c;
  }

  function addMesh(asset) {
    return add('StaticMesh', { asset: asset.id }, asset.name.replace(/[^\w]+/g, ''));
  }

  // ------------------------------------------------------------------------------------------- details (right)
  function renderDetails() {
    const box = $('Details');
    const scroll = box.scrollTop;
    if (disposeMaterial) { disposeMaterial(); disposeMaterial = null; }
    box.innerHTML = '';
    const c = selected();
    if (!c) return;
    const cls = COMPONENT_CLASSES[c.type];
    const inspectedAsset = c.type === 'StaticMesh' && assets.get(c.props.asset);
    if (inspectorEditor && inspectedAsset) {
      const inspector = inspectionFor(c, inspectedAsset);
      box.append(inspector.editor(inspectorEditor));
      box.scrollTop = scroll;
      return;
    }
    const path = [];
    for (let x = c; x; x = x.parent ? findComponent(bp, x.parent) : null) path.unshift(x.name);
    const nameInput = el('input', { class: 'name-input', value: c.name, spellcheck: 'false', 'aria-label': 'Component name' });
    nameInput.addEventListener('change', () => { if (!renameComponent(bp, c.id, nameInput.value)) { toast('Name must be unique'); nameInput.value = c.name; } else changed({ scene: false }); });
    const actions = el('div', { class: 'detail-actions' });
    if (c.id !== bp.rootId) {
      const dup = el('button', { type: 'button', class: 'tool round', title: 'Duplicate (Ctrl+D)', html: icon('copy', 14) });
      dup.addEventListener('click', () => { const d = duplicateComponent(bp, c.id); if (d) { changed(); select(d.id); } });
      const del = el('button', { type: 'button', class: 'tool round', title: 'Delete (Del)', html: icon('trash', 14) });
      del.addEventListener('click', () => deleteSelected());
      actions.append(dup, del);
    }
    box.append(
      el('div', { class: 'crumbs' }, `${bp.name} / ${path.join(' / ')}`),
      el('div', { class: 'detail-head' }, el('span', { class: 'detail-glyph', html: icon(cls.icon, 20) }), el('div', { class: 'detail-title' }, nameInput, el('div', { class: 'subtitle' }, `${cls.label} component${cls.scene ? '' : ' · no transform'}`)), actions),
    );
    const search = el('input', { type: 'search', class: 'detail-search', placeholder: 'Search inspector' });
    box.append(el('div', { class: 'search-wrap' }, iconEl('search', 14), search));
    const body = el('div', { class: 'detail-body' });
    box.append(body);

    if (c.id === bp.rootId) renderClassDefaults(body);
    const props = c.props.importPlaceholder && !assets.get(c.props.asset)
      ? [...SCENE_PROPS.slice(0, 3), cls.props.find((p) => p.key === 'asset')]
      : [...(cls.scene ? SCENE_PROPS.slice(0, 3) : []), ...cls.props, ...(cls.scene ? SCENE_PROPS.slice(3) : []), ...COMMON_PROPS];
    const order = [];
    const bySection = new Map();
    for (const p of props) {
      if (['visible', 'hiddenInGame', 'tags', 'collisionPreset', 'castShadow', 'slots', 'simulatePhysics', 'lod'].includes(p.key)) continue;
      if (c.id === bp.rootId && p.section === 'Transform') continue;
      if (!bySection.has(p.section)) { bySection.set(p.section, []); order.push(p.section); }
      bySection.get(p.section).push(p);
    }
    for (const name of order) {
      const pnl = panel();
      for (const p of bySection.get(name)) { const f = propField(c, p); if (f) { f.dataset.search = `${name} ${p.label}`.toLowerCase(); pnl.append(f); } }
      const wrap = el('div', { class: 'detail-section', dataset: { search: name.toLowerCase() } }, section(name.toUpperCase()), pnl);
      body.append(wrap);
    }
    if (inspectedAsset && inspectedAsset.out.geometry) {
      const materialHost = el('div', { class: 'world-material-container', 'aria-label': 'Material inspector' });
      body.append(materialHost, inspectionFor(c, inspectedAsset).cards());
      mountWorldMaterial(materialHost, { component: c, asset: inspectedAsset, slots: assetSlots(inspectedAsset),
        change() { touch(bp); rebuildScene(); renderHeading(); renderTabs(); }
      }).then((dispose) => { if (materialHost.isConnected) disposeMaterial = dispose; else if (dispose) dispose(); })
        .catch((error) => { if (materialHost.isConnected) materialHost.textContent = `Material inspector could not load: ${error.message}`; });
    }
    search.addEventListener('input', () => {
      const q = search.value.trim().toLowerCase();
      for (const s of body.querySelectorAll('.detail-section')) {
        let any = false;
        for (const f of s.querySelectorAll('[data-search]')) { const hit = !q || f.dataset.search.includes(q); f.hidden = !hit; any ||= hit; }
        s.hidden = !any;
      }
    });
    hydrateIcons(box);
    box.scrollTop = scroll;
  }

  function inspectionFor(c, asset) {
    return createAssetInspection({ asset, component: c, ctx: compileContext, miniUv, miniSdf,
      openEditor(id) { if (id === 'geometry') { geometryTarget = asset; $('Studio').hidden = true; geometryWorkspace.show(); } else if (id === 'uv') { uvTarget = asset; $('Studio').hidden = true; uvWorkspace.show(); } else if (id === 'sdf') { sdfTarget = asset; sdfWorkspace.setVolume(asset.sparseSdfSource === asset.out.geometry && asset.steps.sdf.state === 'done' ? asset.sparseSdf : null); $('Studio').hidden = true; sdfWorkspace.show(); } else if (id === 'lod') { $('Studio').hidden = true; clusterWorkspace.open(asset); } else { inspectorEditor = id; renderDetails(); } },
      back() { inspectorEditor = null; renderDetails(); },
      update({ scene = true, channel: nextChannel } = {}) {
        if (scene) touch(bp);
        if (nextChannel) channel = nextChannel;
        if (scene) rebuildScene();
        renderHeading(); renderTree(); renderDetails(); renderDrawer();
      },
      lodPreviewLevel: () => lodPreviewLevels.get(c) || 0,
      previewLod(level) { lodPreviewLevels.set(c, level); setCentre('viewport'); setChannel('lod'); },
      iconCamera(on) {
        let guide = $('AssetIconGuide');
        if (!guide) { guide = el('div', { id: 'AssetIconGuide', class: 'asset-icon-guide', 'aria-hidden': 'true' }, el('span', {}, 'ICON CAMERA · APPROXIMATE FIT')); $('ViewportWell').append(guide); }
        guide.hidden = !on;
        if (on) frameSelected();
      },
      currentView() { return channel; },
      matchIconAngle() { asset.settings.icon.yaw = THREE.MathUtils.radToDeg(stage.rig.yaw); asset.settings.icon.pitch = THREE.MathUtils.radToDeg(stage.rig.pitch); },
    });
  }

  function renderClassDefaults(body) {
    const issues = validateBlueprint(bp, assets);
    const nameInput = textField({ label: 'Blueprint name', value: bp.name, onChange: (v) => { bp.name = v.trim() || bp.name; changed({ scene: false }); } });
    const issueList = issues.length ? el('div', { class: 'issue-list' }, ...issues.map((i) => {
      const b = el('button', { type: 'button', class: `issue ${i.level}` }, iconEl('warn', 13), el('span', {}, i.text));
      b.addEventListener('click', () => select(i.id));
      return b;
    })) : el('p', { class: 'verdict ok' }, 'No issues. The class compiles cleanly.');
    let meshes = 0, lights = 0, coll = 0;
    for (const c of bp.components) { if (c.type === 'StaticMesh') meshes++; if (/Light/.test(c.type)) lights++; if (/Collision/.test(c.type)) coll++; }
    const thumb = el('div', { class: 'bp-thumb' }, bp.icon ? el('img', { src: bp.icon, alt: '' }) : el('span', {}, 'Compile to bake a class thumbnail'));
    body.append(el('div', { class: 'detail-section', dataset: { search: 'class defaults' } }, section('CLASS DEFAULTS'), panel(
      el('div', { class: 'bp-card' }, thumb, metrics([[String(meshes), 'MESHES'], [String(lights), 'LIGHTS'], [String(coll), 'COLLIDERS'], [String(actorComponents(bp).length), 'ACTOR COMPS']])),
      nameInput,
      rows([['Parent class', bp.parentClass], ['Revision', `${bp.revision}${bp.dirty ? ' (uncompiled)' : ''}`]]),
    )));
    body.append(el('div', { class: 'detail-section', dataset: { search: 'compiler results issues' } }, section('COMPILER RESULTS'), issueList));
  }

  function setProp(c, key, value, { rebuild = true, details = false } = {}) {
    c.props[key] = value;
    if (['location', 'rotation', 'scale'].includes(key)) {
      const node = objects.get(c.id);
      if (node) applyTransform(node, c);
      touch(bp); renderHeading(); renderTabs();
      return;
    }
    changed({ scene: rebuild, tree: ['visible', 'asset'].includes(key), details });
    if (key === 'torqueCurve' || key === 'interpolation') renderDrawer();
  }

  function propField(c, p) {
    const v = c.props[p.key];
    switch (p.type) {
      case 'vec3': return vec3Field({ label: p.label, value: v, step: p.step, unit: p.unit, linkable: p.key === 'scale', onChange: (nv) => setProp(c, p.key, nv) });
      case 'range': {
        let max = p.max;
        if (c.type === 'StaticMesh' && p.key === 'lod') { const a = assets.get(c.props.asset); max = a && a.out.lod ? a.out.lod.levels.length - 1 : 0; }
        return rangeField({ label: p.label, value: Math.min(v, max), min: p.min, max, step: p.step, unit: p.unit, note: p.note, format: p.key === 'lod' ? (x) => (x < 0 ? 'Auto' : `LOD ${x}`) : undefined, onInput: c.type === 'VehicleMovement' ? (x) => { c.props[p.key] = x; } : null, onChange: (x) => setProp(c, p.key, x) });
      }
      case 'bool': return toggle({ label: p.label, value: v, onChange: (x) => setProp(c, p.key, x) });
      case 'color': return colorField({ label: p.label, value: v, onChange: (x) => setProp(c, p.key, x) });
      case 'select': return selectField({ label: p.label, options: p.options, value: v, onChange: (x) => setProp(c, p.key, x) });
      case 'text': return textField({ label: p.label, value: v, placeholder: 'comma, separated', onChange: (x) => setProp(c, p.key, x, { rebuild: false }) });
      case 'asset': return assetField(c, p);
      case 'slots': return slotsField(c);
      case 'wheels': return wheelsField(c);
      case 'socket': return socketField(c);
      case 'component': {
        const options = [{ id: '', label: 'Scene root' }, ...bp.components.filter((x) => COMPONENT_CLASSES[x.type].scene && x.id !== bp.rootId).map((x) => ({ id: x.id, label: x.name }))];
        return selectField({ label: p.label, options, value: v || '', onChange: (x) => setProp(c, p.key, x || null, { rebuild: false }) });
      }
    }
    return null;
  }

  // Socket editor: kind tiles (each applies that kind; numbers you set are kept), the kind's own fields, actions and
  // a live readout of the joint.
  function socketField(c) {
    const p = c.props;
    const kind = SOCKET_KIND[p.kind] || SOCKET_KIND.fixed;
    const wrap = el('div', { class: 'socket-editor', style: { '--k': kind.color } });
    const tiles = el('div', { class: 'sock-tiles', role: 'radiogroup', 'aria-label': 'Socket kind' });
    for (const k of SOCKET_KINDS) {
      const t = el('button', { type: 'button', class: `sock-tile ${k.id === p.kind ? 'active' : ''}`, role: 'radio', 'aria-checked': String(k.id === p.kind), title: k.note, style: { '--k': k.color } }, el('span', { class: 'sock-glyph', html: icon(k.icon, 18) }), el('span', {}, k.label));
      t.addEventListener('click', () => {
        if (k.id === p.kind) return;
        p.kind = k.id;
        if (DRIVES[k.id] && !DRIVES[k.id].some(([id]) => id === p.drive)) p.drive = DRIVES[k.id][0][0];
        socketStates.delete(c.id);
        changed({ details: true });
        toast(`${c.name} is now a ${k.label} socket`);
      });
      tiles.append(t);
    }
    wrap.append(tiles, el('p', { class: 'sock-note' }, kind.note));
    const fields = el('div', { class: 'sock-fields' });
    for (const key of SOCKET_FIELDS[p.kind] || []) {
      const spec = SOCKET_PARAMS[key];
      let f = null;
      const set = (x, opts) => setProp(c, key, x, opts);
      if (spec.type === 'axis') f = segmentedField({ label: spec.label, options: ['X', 'Y', 'Z'], value: p.axis, onChange: (x) => { socketStates.delete(c.id); set(x); } });
      else if (spec.type === 'range') f = rangeField({ label: spec.label, value: p[key], min: spec.min, max: spec.max, step: spec.step, unit: spec.unit, note: spec.note, onChange: (x) => set(x) });
      else if (spec.type === 'pose') f = rangeField({ label: spec.label, value: p.pose, min: 0, max: 1, step: 0.01, format: (x) => poseLabel(p, +x), onInput: (x) => { p.pose = x; }, onChange: (x) => set(x, { rebuild: false }) });
      else if (spec.type === 'drive') f = selectField({ label: spec.label, options: DRIVES[p.kind].map(([id, label]) => ({ id, label })), value: p.drive, onChange: (x) => { socketStates.delete(c.id); set(x, { rebuild: false }); } });
      else if (spec.type === 'select') f = selectField({ label: spec.label, options: spec.options.map(([id, label]) => ({ id, label })), value: p[key], onChange: (x) => set(x, { rebuild: false }) });
      else if (spec.type === 'text') f = textField({ label: spec.label, value: p[key], placeholder: spec.placeholder, onChange: (x) => { setProp(c, key, x.trim()); renderTree(); } });
      else if (spec.type === 'bool') f = toggle({ label: spec.label, value: p[key], onChange: (x) => set(x, { rebuild: false }) });
      if (f) { f.dataset.search = `socket ${spec.label}`.toLowerCase(); fields.append(f); }
    }
    wrap.append(fields);
    // Actions
    const actions = el('div', { class: 'sock-actions' });
    if (p.kind === 'dock') {
      const b = el('button', { type: 'button', class: 'tool wide' }, iconEl(p.connected ? 'unlink' : 'link', 14), p.connected ? 'Release' : 'Connect');
      b.addEventListener('click', () => { setProp(c, 'connected', !p.connected, { rebuild: false, details: true }); });
      actions.append(b);
    }
    if (['spring', 'tether'].includes(p.kind)) {
      const b = el('button', { type: 'button', class: 'tool wide' }, iconEl('kick', 14), 'Kick');
      b.addEventListener('click', () => { const st = socketStates.get(c.id); if (!st) return; if (p.kind === 'spring') st.v += 1.2; else st.omega += 3; });
      actions.append(b);
    }
    const reset = el('button', { type: 'button', class: 'tool wide' }, iconEl('rotate', 14), 'Reset motion');
    reset.addEventListener('click', () => socketStates.delete(c.id));
    actions.append(reset);
    socketLive = el('output', { class: 'sock-live' }, '—');
    wrap.append(actions, el('div', { class: 'sock-live-row' }, el('span', {}, 'LIVE'), socketLive));
    return wrap;
  }

  function assetField(c, p) {
    const a = c.props.asset && assets.get(c.props.asset);
    const assign = (asset) => {
      if (!asset || !asset.out.geometry) return toast('That asset has no readable geometry');
      c.props.slots = {};
      setProp(c, 'asset', asset.id, { details: true });
    };
    const options = [{ id: '', label: 'None' }, ...[...assets.values()].filter((x) => x.out.geometry).map((x) => ({ id: x.id, label: x.name }))];
    const pick = selectField({ label: p.label, options, value: c.props.asset || '', onChange: (x) => {
      c.props.slots = {}; setProp(c, 'asset', x || null, { details: true });
    } });
    const card = el('div', { class: 'asset-card asset-drop', tabindex: '0', 'aria-label': 'Drop an asset from the browser here' },
      el('div', { class: 'content-thumb sm' }, a && a.out.icon ? el('img', { src: a.out.icon, alt: '' }) : iconEl('cube', 18)),
      el('div', { class: 'content-meta' }, el('b', {}, a ? a.name : 'Empty import placeholder'),
        el('small', {}, a && a.out.geometry ? `${fmt(a.out.geometry.indices.length / 3)} tris · Drop another asset to replace` : 'Drop an asset from the Asset Browser or import a file')));
    card.addEventListener('dragover', (e) => { if ([...e.dataTransfer.types].includes('text/asset') || [...e.dataTransfer.types].includes('application/x-frontier-world-model') || [...e.dataTransfer.types].includes('Files')) { e.preventDefault(); e.stopPropagation(); card.classList.add('drop'); } });
    card.addEventListener('dragleave', () => card.classList.remove('drop'));
    card.addEventListener('drop', (e) => {
      e.preventDefault(); e.stopPropagation(); card.classList.remove('drop');
      const id = e.dataTransfer.getData('text/asset');
      const worldId = e.dataTransfer.getData('application/x-frontier-world-model');
      if (id) assign(assets.get(id));
      else if (worldId) {
        resolveWorldModel(worldId).then((asset) => { if (!viewerApi.assignImported(c, asset)) toast('Placeholder no longer exists'); }).catch((error) => toast(error.message));
      } else if (!a && e.dataTransfer.files.length) importFiles(e.dataTransfer.files, c);
    });
    const input = el('input', { type: 'file', accept: '.obj,.gltf,.glb,.stl,.ply,.facet,.fbx', hidden: '' });
    input.addEventListener('change', () => { if (input.files.length) importFiles(input.files, c); input.value = ''; });
    const button = el('button', { type: 'button', class: 'tool wide' }, iconEl('upload', 13), 'Import from file…');
    button.addEventListener('click', () => input.click());
    return el('div', { class: 'stack-gap' }, card, c.props.importPlaceholder ? null : pick, a ? null : button, a ? null : input);
  }

  function slotsField(c) {
    const a = c.props.asset && assets.get(c.props.asset);
    if (!a || !a.out.geometry) return el('p', { class: 'muted-note' }, 'Assign a mesh to edit its material slots.');
    const wrap = el('div', { class: 'slot-list' });
    for (const s of assetSlots(a)) {
      const value = c.props.slots[s.family] || s.color;
      const input = el('input', { type: 'color', value });
      input.addEventListener('change', () => setProp(c, 'slots', { ...c.props.slots, [s.family]: input.value }));
      const reset = el('button', { type: 'button', class: 'mini', title: 'Reset to compiled colour', html: icon('reset', 11) });
      reset.addEventListener('click', () => { const next = { ...c.props.slots }; delete next[s.family]; setProp(c, 'slots', next, { details: true }); });
      wrap.append(el('div', { class: `slot-row ${c.props.slots[s.family] ? 'override' : ''}` }, el('span', { class: 'slot-index' }, String(s.family)), el('span', { class: 'slot-name' }, s.name), input, reset));
    }
    return wrap;
  }

  function wheelsField(c) {
    const wrap = el('div', { class: 'wheel-list' });
    const candidates = bp.components.filter((x) => x.type === 'StaticMesh' || x.type === 'Scene');
    for (const x of candidates) {
      const on = c.props.wheels.includes(x.id);
      const b = el('button', { type: 'button', class: `chip-toggle ${on ? 'on' : ''}` }, iconEl(on ? 'check' : 'wheel', 12), x.name);
      b.addEventListener('click', () => {
        const next = on ? c.props.wheels.filter((w) => w !== x.id) : [...c.props.wheels, x.id];
        setProp(c, 'wheels', next, { rebuild: false, details: true });
      });
      wrap.append(b);
    }
    return wrap;
  }

  function deleteSelected() {
    const c = selected();
    if (!c || c.id === bp.rootId) return;
    const parent = c.parent || bp.rootId;
    removeComponent(bp, c.id);
    changed();
    select(parent);
    toast(`${c.name} deleted`);
  }

  // ------------------------------------------------------------------------------------------------- drawer
  function renderDrawer() {
    const body = $('DrawerBody');
    if (!body) return;
    body.innerHTML = '';
    const query = ($('AssetBrowserSearch').value || '').trim().toLowerCase();
    const matches = (name) => name.toLowerCase().includes(query);
    const grid = el('div', { class: 'asset-browser-grid' });
    const addCard = (name, label, mime, id, thumbnail) => {
      if (!matches(name)) return;
      const card = el('div', { class: 'browser-asset', draggable: 'true', title: `Drag ${name} onto an Instance` },
        el('div', { class: 'browser-thumb' }, thumbnail ? el('img', { src: thumbnail, alt: '' }) : iconEl('cube', 25)),
        el('b', {}, name), el('small', {}, label));
      card.addEventListener('dragstart', (e) => { e.dataTransfer.setData(mime, id); e.dataTransfer.effectAllowed = 'copy'; });
      grid.append(card);
    };
    for (const asset of assets.values()) addCard(asset.name, asset.origin === 'world-mirror' ? 'World Mirror · model' : 'Imported · model', 'text/asset', asset.id, asset.steps.icon.state === 'done' ? asset.out.icon : null);
    for (const model of worldModels) addCard(model.Name, `World Mirror · ${model.Category || 'Primitives'}`, 'application/x-frontier-world-model', model.Id);
    if (!grid.children.length) body.append(el('p', { class: 'muted-note' }, 'No matching assets.'));
    else body.append(grid);
  }

  function miniUv(canvas, a) {
    const g = canvas.getContext('2d'), s = canvas.width;
    g.fillStyle = '#0d0d0f'; g.fillRect(0, 0, s, s);
    const { layout, uvs } = a.out.uv;
    if (!layout) {
      g.fillStyle = 'rgba(130, 175, 220, 0.35)'; g.strokeStyle = '#e6e8ea'; g.lineWidth = 0.8;
      for (let o = 0; o + 5 < uvs.length; o += 6) { g.beginPath(); g.moveTo(uvs[o] * s, (1 - uvs[o + 1]) * s); g.lineTo(uvs[o + 2] * s, (1 - uvs[o + 3]) * s); g.lineTo(uvs[o + 4] * s, (1 - uvs[o + 5]) * s); g.closePath(); g.fill(); g.stroke(); }
      return;
    }
    layout.charts.forEach((chart, ci) => {
      g.fillStyle = `hsla(${(ci * 137.508) % 360} 50% 58% / 0.7)`;
      g.beginPath();
      for (const t of chart.triangles) { const o = t * 6; g.moveTo(uvs[o] * s, (1 - uvs[o + 1]) * s); g.lineTo(uvs[o + 2] * s, (1 - uvs[o + 3]) * s); g.lineTo(uvs[o + 4] * s, (1 - uvs[o + 5]) * s); g.closePath(); }
      g.fill();
    });
    g.strokeStyle = '#ffb238'; g.lineWidth = 1.4; g.beginPath();
    for (const slot of layout.seams) { const t = Math.floor(slot / 3), k = slot % 3; const a = (t * 3 + k) * 2, b = (t * 3 + ((k + 1) % 3)) * 2; g.moveTo(uvs[a] * s, (1 - uvs[a + 1]) * s); g.lineTo(uvs[b] * s, (1 - uvs[b + 1]) * s); }
    g.stroke();
  }

  function miniSdf(canvas, sdf) {
    const n = sdf.dims, img = new ImageData(n, n), k = Math.round(n / 2);
    for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
      const v = sdf.values[x + n * (n - 1 - y + n * k)];
      const t = Math.min(1, Math.abs(v) / sdf.band), o = (y * n + x) * 4, f = 0.25 + 0.75 * (1 - t);
      if (v < 0) { img.data[o] = 50 * f; img.data[o + 1] = 150 * f; img.data[o + 2] = 230 * f; } else { img.data[o] = 240 * f; img.data[o + 1] = 170 * f; img.data[o + 2] = 80 * f; }
      img.data[o + 3] = 255;
    }
    const tmp = document.createElement('canvas'); tmp.width = tmp.height = n; tmp.getContext('2d').putImageData(img, 0, 0);
    const g = canvas.getContext('2d'); g.imageSmoothingEnabled = true; g.drawImage(tmp, 0, 0, canvas.width, canvas.height);
  }

  // Vehicle Lab response curve: drag points, double-click adds, right-click removes.
  function renderCurves(body) {
    const vm = actorComponents(bp).find((c) => c.type === 'VehicleMovement');
    $('DrawerTitle').textContent = 'Response curves · Vehicle Movement';
    if (!vm) {
      const addB = el('button', { type: 'button', class: 'tool' }, iconEl('plus', 13), 'Add Vehicle Movement');
      addB.addEventListener('click', () => { add('VehicleMovement'); renderDrawer(); });
      body.append(el('p', { class: 'muted-note' }, 'Response curves belong to a Vehicle Movement component.'), addB);
      return;
    }
    const canvas = el('canvas', { class: 'curve-canvas' });
    const interp = selectField({ label: 'Interpolation', options: [{ id: 'linear', label: 'Linear' }, { id: 'smooth', label: 'Smooth spline' }, { id: 'ease', label: 'Ease in / out' }, { id: 'step', label: 'Step / hold' }], value: vm.props.interpolation, onChange: (x) => { vm.props.interpolation = x; touch(bp); draw(); renderHeading(); } });
    const reset = el('button', { type: 'button', class: 'tool' }, iconEl('reset', 13), 'Reset');
    reset.addEventListener('click', () => { vm.props.torqueCurve = VEHICLE_SHEET.torqueCurve.map((p) => p.slice()); touch(bp); draw(); });
    const csv = el('button', { type: 'button', class: 'tool' }, iconEl('download', 13), 'CSV');
    csv.addEventListener('click', () => download('engine-torque.csv', 'rpm,torque_nm\n' + vm.props.torqueCurve.map((p) => p.join(',')).join('\n'), 'text/csv'));
    const peak = el('small', { class: 'curve-meta' });
    body.append(el('div', { class: 'curve-head' }, el('div', {}, el('span', { class: 'kicker' }, 'PHYSICS RESPONSE'), el('b', {}, 'Engine torque')), interp, reset, csv), el('div', { class: 'curve-wrap' }, canvas, peak));
    let bounds = null, dragIndex = -1;
    const pts = () => vm.props.torqueCurve;
    function draw() {
      const w = canvas.clientWidth || 600, h = canvas.clientHeight || 150, dpr = Math.min(2, devicePixelRatio || 1);
      canvas.width = w * dpr; canvas.height = h * dpr;
      const g = canvas.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, w, h);
      const L = 44, R = 12, T = 10, B = 22;
      const xs = pts().map((p) => p[0]), ys = pts().map((p) => p[1]);
      const xmin = 0, xmax = Math.max(9000, ...xs), ymin = 0, ymax = Math.max(600, ...ys) * 1.1;
      bounds = { L, T, W: w - L - R, H: h - T - B, xmin, xmax, ymin, ymax };
      const X = (x) => L + ((x - xmin) / (xmax - xmin)) * bounds.W, Y = (y) => T + bounds.H - ((y - ymin) / (ymax - ymin)) * bounds.H;
      g.strokeStyle = '#222'; g.lineWidth = 1; g.fillStyle = '#5c5c5c'; g.font = '9px ui-monospace, monospace';
      for (let x = 0; x <= xmax; x += 1000) { g.beginPath(); g.moveTo(X(x), T); g.lineTo(X(x), T + bounds.H); g.stroke(); g.fillText(`${x / 1000}k`, X(x) - 6, h - 6); }
      for (let y = 0; y <= ymax; y += 100) { g.beginPath(); g.moveTo(L, Y(y)); g.lineTo(L + bounds.W, Y(y)); g.stroke(); g.fillText(String(y), 8, Y(y) + 3); }
      g.fillStyle = '#ff6b5e22'; g.fillRect(X(vm.props.redlineRpm), T, L + bounds.W - X(vm.props.redlineRpm), bounds.H);
      g.beginPath();
      const first = pts()[0][0], last = pts()[pts().length - 1][0];
      for (let x = first; x <= last; x += (last - first) / 300) { const y = sampleCurve(pts(), x, vm.props.interpolation); x === first ? g.moveTo(X(x), Y(y)) : g.lineTo(X(x), Y(y)); }
      g.strokeStyle = '#9fe0b5'; g.lineWidth = 1.6; g.stroke();
      g.lineTo(X(last), Y(0)); g.lineTo(X(first), Y(0)); g.closePath();
      const grad = g.createLinearGradient(0, T, 0, T + bounds.H); grad.addColorStop(0, '#9fe0b533'); grad.addColorStop(1, '#9fe0b500');
      g.fillStyle = grad; g.fill();
      for (const [i, p] of pts().entries()) { g.beginPath(); g.arc(X(p[0]), Y(p[1]), i === dragIndex ? 5 : 3.5, 0, Math.PI * 2); g.fillStyle = i === dragIndex ? AMBER : '#e8f6ec'; g.fill(); }
      if (simulate) { g.strokeStyle = AMBER; g.beginPath(); g.moveTo(X(drive.rpm), T); g.lineTo(X(drive.rpm), T + bounds.H); g.stroke(); }
      const pk = pts().reduce((m, p) => (p[1] > m[1] ? p : m), pts()[0]);
      peak.textContent = `${pts().length} points · peak ${pk[1].toFixed(0)} N·m @ ${pk[0].toFixed(0)} rpm · redline ${vm.props.redlineRpm} rpm`;
    }
    const toData = (e) => { const r = canvas.getBoundingClientRect(); const x = e.clientX - r.left, y = e.clientY - r.top; return [bounds.xmin + ((x - bounds.L) / bounds.W) * (bounds.xmax - bounds.xmin), bounds.ymin + ((bounds.T + bounds.H - y) / bounds.H) * (bounds.ymax - bounds.ymin), x, y]; };
    const nearest = (e) => { const [, , x, y] = toData(e); let best = -1, bd = 12; pts().forEach((p, i) => { const px = bounds.L + ((p[0] - bounds.xmin) / (bounds.xmax - bounds.xmin)) * bounds.W, py = bounds.T + bounds.H - ((p[1] - bounds.ymin) / (bounds.ymax - bounds.ymin)) * bounds.H; const d = Math.hypot(px - x, py - y); if (d < bd) { bd = d; best = i; } }); return best; };
    canvas.addEventListener('pointerdown', (e) => { if (e.button !== 0) return; dragIndex = nearest(e); if (dragIndex >= 0) canvas.setPointerCapture(e.pointerId); draw(); });
    canvas.addEventListener('pointermove', (e) => {
      if (dragIndex < 0) return;
      const [x, y] = toData(e), p = pts();
      const lo = dragIndex > 0 ? p[dragIndex - 1][0] + 50 : 0, hi = dragIndex < p.length - 1 ? p[dragIndex + 1][0] - 50 : 12000;
      p[dragIndex] = [Math.round(Math.max(lo, Math.min(hi, x)) / 10) * 10, Math.round(Math.max(0, y))];
      draw();
    });
    canvas.addEventListener('pointerup', () => { if (dragIndex >= 0) { dragIndex = -1; touch(bp); renderHeading(); renderTabs(); draw(); } });
    canvas.addEventListener('dblclick', (e) => { const [x, y] = toData(e); const p = pts(); p.push([Math.round(x / 10) * 10, Math.round(y)]); p.sort((a, b) => a[0] - b[0]); touch(bp); draw(); });
    canvas.addEventListener('contextmenu', (e) => { e.preventDefault(); const i = nearest(e); if (i >= 0 && pts().length > 3) { pts().splice(i, 1); touch(bp); draw(); } });
    requestAnimationFrame(draw);
    curveRedraw = draw;
  }
  let curveRedraw = null;
  setInterval(() => { if (simulate && drawerTab === 'curves' && $('Drawer').classList.contains('open') && curveRedraw) curveRedraw(); }, 120);

  // ------------------------------------------------------------------------------------- construction graph
  function renderGraph() {
    const svg = $('Graph');
    const state = graphState.get(bp.id) || { pos: new Map(), pan: [40, 30], zoom: 1 };
    graphState.set(bp.id, state);
    const nodes = [];
    nodes.push({ id: 'event', kind: 'event', title: 'Construction Script', sub: 'Event', pinsOut: ['exec'] });
    const seq = [];
    for (const c of bp.components) {
      if (c.type === 'StaticMesh') seq.push({ id: `set-${c.id}`, comp: c, kind: 'call', title: 'Set Static Mesh', sub: `Target: ${c.name}`, field: `Mesh: ${c.props.asset ? assets.get(c.props.asset)?.name || 'missing' : 'None'}` });
      if (/Light/.test(c.type)) seq.push({ id: `set-${c.id}`, comp: c, kind: 'call', title: 'Set Intensity', sub: `Target: ${c.name}`, field: `${c.props.intensity} cd` });
      if (c.type === 'VehicleMovement') seq.push({ id: `set-${c.id}`, comp: c, kind: 'call', title: 'Setup Wheels', sub: `Target: ${c.name}`, field: `${c.props.wheels.length} wheels · ${c.props.mass} kg` });
      if (c.type === 'Socket') { const k = SOCKET_KIND[c.props.kind] || SOCKET_KIND.fixed; seq.push({ id: `set-${c.id}`, comp: c, kind: 'call', title: `Make ${k.label} Socket`, sub: `Target: ${c.name}`, field: socketReadout(c.props, socketStates.get(c.id) || createSocketState()).split('  ·  ').pop() }); }
      if (/Collision/.test(c.type)) seq.push({ id: `set-${c.id}`, comp: c, kind: 'call', title: 'Set Collision Profile', sub: `Target: ${c.name}`, field: c.props.trigger ? 'OverlapAll' : 'BlockAll' });
    }
    nodes.push(...seq);
    const getters = seq.map((s) => ({ id: `get-${s.comp.id}`, comp: s.comp, kind: 'get', title: s.comp.name }));
    nodes.push(...getters);
    nodes.forEach((n, i) => {
      if (state.pos.has(n.id)) return;
      if (n.kind === 'event') state.pos.set(n.id, [0, 0]);
      else if (n.kind === 'call') { const k = seq.indexOf(n); state.pos.set(n.id, [260 + k * 250, (k % 2) * 40]); }
      else { const k = getters.indexOf(n); state.pos.set(n.id, [200 + k * 250, 170 + (k % 2) * 40]); }
    });
    const W = 200;
    let html = `<defs><pattern id="gridSmall" width="16" height="16" patternUnits="userSpaceOnUse"><path d="M16 0H0V16" fill="none" stroke="#1a1a1d" stroke-width="1"/></pattern><pattern id="gridBig" width="128" height="128" patternUnits="userSpaceOnUse"><rect width="128" height="128" fill="url(#gridSmall)"/><path d="M128 0H0V128" fill="none" stroke="#232327" stroke-width="1"/></pattern></defs>`;
    html += `<rect class="graph-bg" width="100%" height="100%" fill="url(#gridBig)"/>`;
    html += `<g id="GraphWorld" transform="translate(${state.pan[0]} ${state.pan[1]}) scale(${state.zoom})">`;
    const pinOut = (n) => { const [x, y] = state.pos.get(n.id); return [x + W, y + 34]; };
    const pinIn = (n) => { const [x, y] = state.pos.get(n.id); return [x, y + 34]; };
    const wire = (a, b, color, w = 2.2) => { const dx = Math.max(40, Math.abs(b[0] - a[0]) * 0.5); return `<path d="M${a[0]} ${a[1]} C${a[0] + dx} ${a[1]} ${b[0] - dx} ${b[1]} ${b[0]} ${b[1]}" stroke="${color}" stroke-width="${w}" fill="none"/>`; };
    let prev = nodes[0];
    for (const s of seq) { html += wire(pinOut(prev), pinIn(s), '#e8e8e8'); prev = s; }
    for (const gNode of getters) { const s = seq.find((x) => x.comp === gNode.comp); const [gx, gy] = state.pos.get(gNode.id); const [sx, sy] = state.pos.get(s.id); html += wire([gx + 150, gy + 16], [sx, sy + 58], '#4d9bff', 1.8); }
    for (const n of nodes) {
      const [x, y] = state.pos.get(n.id);
      const sel = n.comp && n.comp.id === selectedId ? ' sel' : '';
      if (n.kind === 'get') {
        html += `<g class="gnode get${sel}" data-node="${n.id}" transform="translate(${x} ${y})"><rect width="150" height="32" rx="16"/><text x="16" y="20">${esc(n.title)}</text><circle cx="138" cy="16" r="5" class="pin-obj"/></g>`;
        continue;
      }
      const h = n.kind === 'event' ? 52 : 80;
      html += `<g class="gnode ${n.kind}${sel}" data-node="${n.id}" transform="translate(${x} ${y})"><rect class="body" width="${W}" height="${h}" rx="10"/><path class="head" d="M10 0H${W - 10}A10 10 0 0 1 ${W} 10V22H0V10A10 10 0 0 1 10 0Z"/><text class="title" x="12" y="15">${esc(n.title)}</text><text class="sub" x="12" y="${n.kind === 'event' ? 40 : 40}">${esc(n.sub)}</text>`;
      if (n.field) html += `<text class="field" x="12" y="62">${esc(n.field)}</text><circle cx="0" cy="58" r="4.5" class="pin-obj"/>`;
      if (n.kind !== 'event') html += `<path class="pin-exec" d="M-1 29 L6 34 L-1 39 Z"/>`;
      html += `<path class="pin-exec" d="M${W - 7} 29 L${W} 34 L${W - 7} 39 Z"/></g>`;
    }
    html += '</g>';
    html += `<text class="graph-hint" x="16" y="${svg.clientHeight - 14}">CONSTRUCTION SCRIPT · generated from the component list · drag nodes · drag background to pan · wheel to zoom</text>`;
    svg.innerHTML = html;
    // interaction
    let drag = null;
    svg.onpointerdown = (e) => {
      const g = e.target.closest('.gnode');
      if (g) {
        const id = g.dataset.node;
        const n = nodes.find((x) => x.id === id);
        if (n && n.comp) { selectedId = n.comp.id; renderTree(); renderDetails(); for (const x of svg.querySelectorAll('.gnode')) x.classList.toggle('sel', x === g || (nodes.find((m) => m.id === x.dataset.node)?.comp === n.comp)); }
        drag = { kind: 'node', id, start: [e.clientX, e.clientY], origin: state.pos.get(id).slice(), g };
      } else drag = { kind: 'pan', start: [e.clientX, e.clientY], origin: state.pan.slice() };
      svg.setPointerCapture(e.pointerId);
    };
    svg.onpointermove = (e) => {
      if (!drag) return;
      const dx = e.clientX - drag.start[0], dy = e.clientY - drag.start[1];
      if (drag.kind === 'pan') { state.pan = [drag.origin[0] + dx, drag.origin[1] + dy]; svg.querySelector('#GraphWorld').setAttribute('transform', `translate(${state.pan[0]} ${state.pan[1]}) scale(${state.zoom})`); }
      else { state.pos.set(drag.id, [drag.origin[0] + dx / state.zoom, drag.origin[1] + dy / state.zoom]); drag.moved = true; drag.g.setAttribute('transform', `translate(${state.pos.get(drag.id)[0]} ${state.pos.get(drag.id)[1]})`); }
    };
    svg.onpointerup = () => { if (drag && drag.kind === 'node' && drag.moved) renderGraph(); drag = null; };
    svg.onwheel = (e) => { e.preventDefault(); state.zoom = Math.max(0.4, Math.min(1.8, state.zoom * (e.deltaY > 0 ? 0.92 : 1.08))); svg.querySelector('#GraphWorld').setAttribute('transform', `translate(${state.pan[0]} ${state.pan[1]}) scale(${state.zoom})`); };
  }

  const esc = (s) => String(s).replace(/[&<>"]/g, (ch) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[ch]));

  function setCentre(mode) {
    centreMode = mode;
    $('GraphPane').hidden = mode !== 'graph';
    $('ViewportTools').classList.toggle('dim', mode === 'graph');
    if (mode === 'graph') renderGraph();
  }

  function setChannel(id) {
    channel = id;
    for (const b of $('ChannelBar').querySelectorAll('button')) b.classList.toggle('active', b.dataset.value === id);
    $('Legend').innerHTML = '';
    $('Legend').append(el('div', {}, CHANNEL_NOTE[id]));
    if (id === 'lod') $('Legend').append(el('div', { class: 'ramp clusters' }));
    if (id === 'density') $('Legend').append(el('div', { class: 'ramp density' }), el('div', { class: 'ramp-labels' }, el('span', {}, 'sparse'), el('span', {}, 'dense')));
    if (id === 'collision') $('Legend').append(el('div', { class: 'legend-keys' }, el('span', { class: 'k green' }, 'compiled'), el('span', { class: 'k amber' }, 'component')));
    if (!bp) return;
    rebuildScene();
    renderHeading();
  }

  // -------------------------------------------------------------------------------------------------- wiring
  const bar = $('ChannelBar');
  bar.append(segmented({ options: CHANNELS.map((c) => ({ id: c.id, label: c.label, title: `${c.label} (${c.key})` })), value: channel, onChange: setChannel }));
  $('CentreModes').append(segmented({ options: [{ id: 'viewport', label: 'Viewport', icon: 'viewport' }, { id: 'graph', label: 'Construction Script', icon: 'graph' }], value: 'viewport', onChange: setCentre }));
  $('SimulateBtn').addEventListener('click', () => setSimulate(!simulate));
  $('FrameBtn').addEventListener('click', () => frameSelected());
  $('GridBtn').addEventListener('click', () => { stage.grid.visible = !stage.grid.visible; $('GridBtn').classList.toggle('active', stage.grid.visible); });
  $('CompileBpBtn').addEventListener('click', () => compileBlueprint());
  $('ComponentSearch').addEventListener('input', renderTree);
  $('AddComponentBtn').addEventListener('click', (e) => { e.stopPropagation(); const m = $('AddMenu'); m.hidden = !m.hidden; if (!m.hidden) { $('AddSearch').value = ''; renderAddMenu(); $('AddSearch').focus(); } });
  $('AddSearch').addEventListener('input', renderAddMenu);
  $('AddSearch').addEventListener('keydown', (e) => { if (e.key === 'Enter') { const first = $('AddList').querySelector('.menu-item'); if (first) first.click(); } if (e.key === 'Escape') $('AddMenu').hidden = true; });
  document.addEventListener('pointerdown', (e) => { if (!e.target.closest('#AddMenu, #AddComponentBtn')) $('AddMenu').hidden = true; });
  $('AssetBrowserSearch').addEventListener('input', renderDrawer);
  $('BrowserImport').addEventListener('click', () => $('FileInput').click());
  $('Drawer').addEventListener('dragover', (e) => { if ([...e.dataTransfer.types].includes('Files')) e.preventDefault(); });
  $('Drawer').addEventListener('drop', (e) => { if (e.dataTransfer.files.length) { e.preventDefault(); e.stopPropagation(); importFiles(e.dataTransfer.files); } });
  $('DrawerHandle').addEventListener('click', () => {
    const d = $('Drawer'); const openNow = !d.classList.contains('open');
    d.classList.toggle('open', openNow); $('DrawerHandle').setAttribute('aria-expanded', String(openNow));
    if (openNow) renderDrawer();
  });
  $('ThrottleInput').addEventListener('input', (e) => { drive.throttle = +e.target.value; $('ThrottleValue').textContent = `${Math.round(drive.throttle * 100)}%`; });
  $('BrakeInput').addEventListener('input', (e) => { drive.brake = +e.target.value; $('BrakeValue').textContent = `${Math.round(drive.brake * 100)}%`; });

  // Camera HUD: orientation dial, lens, turntable, and "look through" for a selected Camera component.
  const lookChip = el('button', { type: 'button', class: 'cam-chip', title: 'Look through the selected Camera (C)', hidden: '' }, 'Look through');
  lookChip.addEventListener('click', () => lookThrough());
  createCameraHud(stage.rig, $('ViewportWell'), { extra: [lookChip] });
  function lookThrough() {
    const c = selected(), node = c && objects.get(c.id);
    if (!c || c.type !== 'Camera' || !node) return;
    node.updateMatrixWorld(true);
    const pos = new THREE.Vector3().setFromMatrixPosition(node.matrixWorld);
    const forward = new THREE.Vector3(1, 0, 0).transformDirection(node.matrixWorld);
    stage.rig.lookThrough(pos, forward, c.props.fov);
  }
  stage.rig.onChange(() => { const c = selected(); lookChip.hidden = !(c && c.type === 'Camera'); });

  // Viewport picking (click without drag) and drag-and-drop of content assets.
  const canvas = $('Viewport');
  let down = null;
  canvas.addEventListener('pointerdown', (e) => { down = [e.clientX, e.clientY]; });
  canvas.addEventListener('pointerup', (e) => {
    if (!down || Math.hypot(e.clientX - down[0], e.clientY - down[1]) > 4 || e.button !== 0) return;
    const hit = stage.pick(e.clientX, e.clientY);
    select(hit ? hit.componentId : bp.rootId);
  });
  canvas.addEventListener('dblclick', () => frameSelected());
  const well = $('ViewportWell');
  well.addEventListener('dragover', (e) => { e.preventDefault(); well.classList.add('drop'); });
  well.addEventListener('dragleave', () => well.classList.remove('drop'));
  well.addEventListener('drop', (e) => {
    e.preventDefault(); well.classList.remove('drop');
    const id = e.dataTransfer.getData('text/asset');
    if (id && assets.get(id)) { addMesh(assets.get(id)); return; }
    if (e.dataTransfer.files.length) importFiles(e.dataTransfer.files);
  });

  addEventListener('keydown', (e) => {
    if (clusterWorkspace.active) { clusterWorkspace.keydown(e); return; }
    if (geometryWorkspace.open) { geometryWorkspace.keydown(e); return; }
    if (uvWorkspace.open) { uvWorkspace.keydown(e); return; }
    if (sdfWorkspace.open) { sdfWorkspace.keydown(e); return; }
    if (!$('Compile').hidden || e.target.matches('input, textarea, select')) return;
    const ch = CHANNELS.find((c) => c.key === e.key);
    if (ch) { setChannel(ch.id); return; }
    if (e.key === 'Delete' || e.key === 'Backspace') { deleteSelected(); e.preventDefault(); }
    else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'd') { e.preventDefault(); const d = duplicateComponent(bp, selectedId); if (d) { changed(); select(d.id); } }
    else if (e.key === 'f' || e.key === 'F') frameSelected();
    else if (e.key === 'g' || e.key === 'G') $('GridBtn').click();
    else if (e.key === 't' || e.key === 'T') document.querySelector('#ViewportWell .cam-chip:nth-child(2)')?.click();
    else if (e.key === 'c' || e.key === 'C') lookThrough();
    else if (e.key === ' ') { e.preventDefault(); setSimulate(!simulate); }
    else if (e.key === 'Escape') select(bp.rootId);
    else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); compileBlueprint(); }
  });

  setChannel('lit');
  hydrateIcons(document);

  const viewerApi = {
    open,
    refresh() { if (!bp) return; rebuildScene(); renderAll(); },
    assignImported(component, asset) {
      const owner = blueprints.find((b) => b.components.includes(component));
      if (!owner) return false;
      component.props.asset = asset.id;
      component.props.slots = {};
      touch(owner);
      if (owner === bp) { rebuildScene(); renderAll(); }
      return true;
    },
    get blueprint() { return bp; },
    blueprints,
  };
  return viewerApi;
}
