//============================================================================================================================================
//                                                              VIEWPORT3D.JSX
//============================================================================================================================================
// 🧩 The 3D viewport: the scene's basic shapes, lit by the Sun and under the Atmosphere's sky, drawn with the Component Viewer's
// three.js renderer (Shared/Studio/Render.js, `Stage`, with its orbit camera). Not the native renderer and not SDF.
//   • Shapes: every geometry object (cube, sphere, cylinder, torus, cone) with its size settings, Transform, base colour, roughness and
//     metallic. The Showcase collection stands on a line 1.8 m apart so its five shapes don't overlap at the origin; Position moves an
//     object from its place on that line. Objects rest on the ground.
//   • Sun: direction from the Sun's date, time and place (or its manual direction); colour is its temperature times the sunlight left
//     after the air (SkyShader.js `Transmittance`), so it reddens and fades at sunset; it casts shadows.
//   • Stars: the Stars object's catalogue (BakedMaps.js) as a point field that comes out after sunset and turns about the celestial
//     pole (StarField).
//   • Atmosphere: the sky-view map (BakedMaps.js, SkyShader.js in equirectangular form) baked whenever the sky changes and drawn as the
//     background (at infinity, so zooming can't clip it); the hemisphere light and the horizon haze take their colours from that map.
//   • Cameras: every scene camera (not the editor camera, which is the view itself) as its editor asset (EditorAssets.js) on a tripod
//     (or, with Tripod off, the body alone at head height, for cameras that move)
//     around the Showcase, aimed at it, with a frustum out to its subject distance from its focal length and sensor.
//   • Vehicles: the Vehicles folder's models (VehicleAssets.js) in their paint, parked in a row behind the Showcase, nose out at an
//     angle; lit by the sun and casting shadows like the shapes. They are not part of the opening framing (the Showcase is).
//   • Lens Flare: the Lens Flare object put together from its baked atlas (LensFlare.js) on a 2D layer over the view, where the sun is on
//     screen and not hidden behind a shape.
// Click an object to select it (Ctrl or Shift adds it). Drag to orbit, right-drag or Shift-drag to pan, wheel to zoom; F frames the
// selection. Frames are drawn only when something changed, so an idle viewport costs nothing.

import React, { useEffect, useRef, useState } from "react";
import { THREE, Stage } from "../Shared/Studio/Render.js";
import { ShapeOf } from "./PrimitiveInspector.jsx";
import { ResolveMaterial } from "./MaterialSpecification.js";
import { Solar } from "./Solar.mjs";
import { SkyMedium, Direction, KelvinTint, SunAngularRadius } from "./SkyShader.js";
import { SkyViewMap, SunBeam, SkyFill, StarCatalogue, StarCount, StarSettings, FlareAtlasMap } from "./BakedMaps.js";
import { FlareSettings, PaintFlare } from "./LensFlare.js";
import { CameraAsset, CameraBodyOf, StudioLit } from "./EditorAssets.js";
import { VehicleAsset } from "./VehicleAssets.js";
import { VehicleAssetPanel, PaintOf } from "./VehicleAssetInspector.jsx";
import { IsEditorCamera } from "./ScenePolicy.js";

export const ShowcaseSpacing = 1.8;
const Rad = Math.PI / 180;
const Accent = "#ffb238";

// Shape, size and lift (half its height, so it rests on the ground) for one geometry object.
export function ShapeGeometry(Shape, Read) {
  const P = (Key, Initial) => (Number.isFinite(+Read(Key)) && Read(Key) !== null && Read(Key) !== "" ? +Read(Key) : Initial);
  const Segments = Math.max(3, Math.min(96, Math.round(P("Geometry Segments", 32))));
  if (Shape === "sphere") {
    const R = P("Geometry Radius", 0.5);
    return { Geometry: new THREE.SphereGeometry(R, Segments, Math.max(2, Math.min(64, Math.round(P("Geometry Rings", 16))))), Lift: R };
  }
  if (Shape === "cylinder") {
    const R = P("Geometry Radius", 0.5), H = P("Geometry Height", 1);
    return { Geometry: new THREE.CylinderGeometry(R, R, H, Segments), Lift: H / 2 };
  }
  if (Shape === "cone") {
    const R = P("Geometry Radius", 0.5), H = P("Geometry Height", 1);
    return { Geometry: new THREE.ConeGeometry(R, H, Segments), Lift: H / 2 };
  }
  if (Shape === "torus") {
    const Major = P("Geometry Major Radius", 0.5), Minor = P("Geometry Minor Radius", 0.15);
    const Geometry = new THREE.TorusGeometry(Major, Minor, Math.max(3, Math.min(48, Math.round(P("Geometry Sides", 16)))), Math.max(3, Math.min(128, Math.round(P("Geometry Segments", 48)))));
    Geometry.rotateX(Math.PI / 2);
    return { Geometry, Lift: Minor };
  }
  const X = P("Geometry Size X", 1), Y = P("Geometry Size Y", 1), Z = P("Geometry Size Z", 1);
  return { Geometry: new THREE.BoxGeometry(X, Y, Z), Lift: Y / 2 };
}

// Where each geometry object stands: its slot on the Showcase line (if it is in the Showcase) plus its Position.
// Whether a row or any row above it is hidden.
export function HiddenUp(Rows, Hidden, Row) {
  const Seen = new Set();
  for (let At = Row; At && !Seen.has(At.Id); At = Rows.find((Each) => Each.Id === At.Parent)) {
    if (Hidden[At.Id]) return true;
    Seen.add(At.Id);
  }
  return false;
}
export function Placements(Rows, Values, Hidden) {
  const Shapes = Rows.filter((Row) => Row.Panel === "geometry" && ShapeOf(Row));
  const Showcase = Shapes.filter((Row) => Row.Parent === "showcase");
  return Shapes.filter((Row) => !HiddenUp(Rows, Hidden, Row)).map((Row) => {
    const Slot = Showcase.indexOf(Row);
    const Along = Slot >= 0 ? (Slot - (Showcase.length - 1) / 2) * ShowcaseSpacing : 0;
    const Own = Values[Row.Id] || {};
    const Triple = (Key, Initial) => (Array.isArray(Own[Key]) && Own[Key].length === 3 ? Own[Key].map(Number) : Initial);
    return { Row, Shape: ShapeOf(Row), Along, Position: Triple("Position", [0, 0, 0]), Rotation: Triple("Rotation", [0, 0, 0]), Scale: Triple("Scale", [1, 1, 1]) };
  });
}

// Scene cameras: their bodies and optics, and Turn, where each stands on an arc in front and left of the Showcase (the viewport places
// them from the Showcase's bounds, aimed at its middle).
const LensFront = { film: [0, 0.13, -0.33], digital: [0, 0.08, -0.3], mirrorless: [0, 0.043, -0.13] };
export function CameraPlacements(Rows, Values, Hidden) {
  return Rows.filter((Row) => Row.Panel === "camera" && !IsEditorCamera(Row) && !HiddenUp(Rows, Hidden, Row)).map((Row, Index) => {
    const Own = Values[Row.Id] || {};
    const Read = (Name, Fallback) => (Number.isFinite(+Own[Name]) && Own[Name] !== null && Own[Name] !== "" ? +Own[Name] : Fallback);
    return {
      Row,
      Body: CameraBodyOf(Row.Id, Own["Camera body"]),
      Tripod: Own.Tripod !== false,
      Turn: (-34 - Index * 22) * Rad,
      Focal: Read("Focal Length", 35),
      Sensor: Read("Sensor Width", 36),
      Subject: Read("Subject Distance", 10),
      Aspect: Row.Id === "cine" ? 1.5 : 16 / 9,
    };
  });
}

// Level vehicles: where each parks, in a row 5 m apart and 9.5 m behind the Showcase line (shifted left, where the default view looks), turned to show its nose.
export const VehicleSpacing = 5;
export function VehiclePlacements(Rows, Values, Hidden) {
  const Shown = Rows.filter((Row) => Row.Panel === VehicleAssetPanel);
  return Shown.filter((Row) => !HiddenUp(Rows, Hidden, Row)).map((Row) => {
    const Slot = Shown.indexOf(Row);
    return { Row, Model: Row.Model, Paint: PaintOf(Row, Values[Row.Id] || {}), At: [(Slot - (Shown.length - 1) / 2) * VehicleSpacing - 3, 0, -9.5], Turn: Math.PI - 0.55 };
  });
}

// The sun for the viewport: direction, colour and strength.
export function SunLight(Sun, Medium) {
  const Manual = !!Sun("Manual direction");
  const Solved = Solar({ Month: Sun("Month"), Day: Sun("Day of Month"), Latitude: Sun("Latitude"), Longitude: Sun("Longitude"), LocalHours: Sun("Local Hours") ?? 12 });
  const Elevation = Manual ? Sun("Manual elevation") ?? Solved.Elevation : Solved.Elevation;
  const Azimuth = Manual ? Sun("Manual azimuth") ?? Solved.Azimuth : Solved.Azimuth;
  const Tint = KelvinTint(Sun("Temperature") ?? 6500);
  const Beam = SunBeam(Medium, Tint, Elevation);
  return { Elevation, Azimuth, Direction: Direction(Elevation, Azimuth), Tint, Colour: Beam.Colour, Strength: Beam.Strength, Beam: Beam.Intensity, Latitude: +Sun("Latitude") || 0, Hours: +(Sun("Local Hours") ?? 12) };
}

// Star field: the Stars object's catalogue (BakedMaps.js, the same stars as its star map) as points drawn at a fixed size in pixels (so
// they stay sharp at any zoom, unlike stars baked into the sky image), kept around the camera at a fixed fraction of its far plane. A
// star's size and gain follow its magnitude under the limiting magnitude (StarLook), times Brightness and Point Size; they fade towards
// the horizon where the air is thickest, come out as the sun goes down (none above −4°, all by −14°) and are drawn behind the shapes.
// The field turns about the celestial pole (up at the Sun's latitude, due north) by Celestial rotation plus 15° an hour of the Sun's
// local time, so stars rise in the east. Twinkle is not drawn (it would keep the idle viewport redrawing).
export { StarCount };
function StarField() {
  const { Positions, Colours, Magnitudes } = StarCatalogue();
  const Geometry = new THREE.BufferGeometry();
  Geometry.setAttribute("position", new THREE.BufferAttribute(Positions, 3));
  Geometry.setAttribute("colour", new THREE.BufferAttribute(Colours, 3));
  Geometry.setAttribute("magnitude", new THREE.BufferAttribute(Magnitudes, 1));
  const Material = new THREE.ShaderMaterial({
    uniforms: { uRadius: { value: 100 }, uShown: { value: 0 }, uScale: { value: 1 }, uLimit: { value: 5.5 }, uGain: { value: 1 }, uSize: { value: 1 } },
    vertexShader: `
      attribute vec3 colour; attribute float magnitude;
      uniform float uRadius, uScale, uLimit, uGain, uSize; varying vec3 vColour; varying float vUp;
      void main() {
        float Lead = clamp((uLimit - magnitude) / (uLimit + 1.5), 0.0, 1.0), Bright = pow(Lead, 2.2);
        float Seen = smoothstep(0.0, 1.0, clamp((uLimit + 0.6 - magnitude) / 0.8, 0.0, 1.0));
        vColour = colour * (0.3 + 0.7 * Bright) * uGain * Seen;
        vUp = (modelMatrix * vec4(position, 0.0)).y;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position * uRadius, 1.0);
        gl_PointSize = Seen > 0.0 ? (1.1 + Bright * 2.6) * uSize * uScale : 0.0;
      }`,
    fragmentShader: `
      uniform float uShown; varying vec3 vColour; varying float vUp;
      void main() {
        float Fall = 1.0 - smoothstep(0.15, 0.5, length(gl_PointCoord - 0.5));
        float Air = smoothstep(0.0, 0.18, vUp);
        gl_FragColor = vec4(vColour * Fall * Air * uShown, 1.0);
      }`,
    blending: THREE.AdditiveBlending,
    transparent: true,
    depthWrite: false,
    fog: false,
    toneMapped: false,
  });
  const Stars = new THREE.Points(Geometry, Material);
  Stars.frustumCulled = false;
  Stars.renderOrder = -5;
  Stars.raycast = () => {};
  return Stars;
}

export default function Viewport3D({ Rows, Values, Hidden, Selected, Picked = [], Select, Air, Sun, Stars = () => undefined, Flare = () => undefined, StarsOff = false, FlareOff = false, FrameToken = 0 }) {
  const Host = useRef(null), Scene = useRef(null), Latest = useRef({});
  const [Failed, Fail] = useState("");
  const Medium = SkyMedium(Air);
  const Light = SunLight(Sun, Medium);
  const Field = StarSettings(Stars);
  const Lens = FlareSettings(Flare);
  const Chosen = new Set([Selected, ...Picked]);
  const Placed = Placements(Rows, Values, Hidden);
  const Posed = CameraPlacements(Rows, Values, Hidden);
  const Parked = VehiclePlacements(Rows, Values, Hidden);

  // One renderer for the viewport's life.
  useEffect(() => {
    let Built;
    try {
      const Canvas = document.createElement("canvas");
      Canvas.className = "viewport-3d-canvas";
      Canvas.tabIndex = 0;
      Canvas.setAttribute("aria-label", "3D viewport");
      Host.current.appendChild(Canvas);
      // Lens flare: a 2D layer over the view, redrawn with every frame.
      const Layer = document.createElement("canvas");
      Layer.className = "viewport-3d-flare";
      Layer.setAttribute("aria-hidden", "true");
      Host.current.appendChild(Layer);
      const View = new Stage(Canvas, { background: "#0b0b0d", grid: true, shadows: true });
      // Sky: the sky-view map (BakedMaps.js, with the sun's disc in it) copied in and used as the scene background, which three.js
      // draws at infinity. It doesn't depend on the camera's position or its near and far planes, so zooming never cuts it.
      const SkyImage = document.createElement("canvas");
      SkyImage.width = 1024;
      SkyImage.height = 512;
      const SkyTexture = new THREE.CanvasTexture(SkyImage);
      SkyTexture.colorSpace = THREE.SRGBColorSpace;
      SkyTexture.mapping = THREE.EquirectangularReflectionMapping;
      View.scene.background = SkyTexture;
      const Stars = StarField();
      View.scene.add(Stars);
      const Lights = View.scene.children.filter((Each) => Each.isLight);
      const Hemisphere = Lights.find((Each) => Each.isHemisphereLight);
      const Fill = Lights.find((Each) => Each.isDirectionalLight && Each !== View.sun);
      if (Fill) Fill.intensity = 0;
      View.sun.shadow.mapSize.set(1024, 1024);
      // Ground: the Atmosphere's ground colour. It follows the camera across and reaches just short of the far plane (which the camera
      // rig moves with the zoom), fading into haze the colour of the sky's own ground at the horizon, so it has no visible edge.
      View.ground.visible = false;
      const Ground = new THREE.Mesh(new THREE.CircleGeometry(1, 128), new THREE.MeshStandardMaterial({ color: "#808080", roughness: 0.95 }));
      Ground.rotation.x = -Math.PI / 2;
      Ground.position.y = -0.002;
      Ground.receiveShadow = true;
      Ground.raycast = () => {};
      View.scene.add(Ground);
      // The sun light follows the sun direction around whatever is out (frame() would put it back over the camera's shoulder).
      let Around = new THREE.Sphere(new THREE.Vector3(), 4);
      const AimSun = (Sphere = Around) => {
        Around = Sphere;
        const Toward = new THREE.Vector3(...(Latest.current.SunDirection || [0, 1, 0]));
        View.sun.position.copy(Around.center).addScaledVector(Toward, Around.radius + 30);
        View.sun.target.position.copy(Around.center);
        const Reach = Around.radius + 2;
        Object.assign(View.sun.shadow.camera, { left: -Reach, right: Reach, top: Reach, bottom: -Reach, near: 0.1, far: Around.radius * 2 + 60 });
        View.sun.shadow.camera.updateProjectionMatrix();
      };
      // The flare: where the sun lands on screen, how much of its disc the shapes leave showing (five rays: its centre and four points
      // on its drawn edge), faded as it nears the frame's edge and as it sets.
      const Caster = new THREE.Raycaster();
      const DrawFlare = () => {
        const Width = Canvas.clientWidth, Height = Canvas.clientHeight, Ratio = View.renderer.getPixelRatio();
        if (Layer.width !== Math.round(Width * Ratio) || Layer.height !== Math.round(Height * Ratio)) {
          Layer.width = Math.round(Width * Ratio);
          Layer.height = Math.round(Height * Ratio);
        }
        const Brush = Layer.getContext("2d");
        Brush.setTransform(1, 0, 0, 1, 0, 0);
        Brush.clearRect(0, 0, Layer.width, Layer.height);
        const Now = Latest.current;
        let Strength = 0, Drawn = 0;
        const Toward = new THREE.Vector3(...(Now.SunDirection || [0, 1, 0])).normalize();
        const Ahead = View.camera.getWorldDirection(new THREE.Vector3());
        if (Now.Lens?.Enabled && !Now.FlareOff && Now.FlareAtlas && Toward.dot(Ahead) > 0.05) {
          const At = View.camera.position.clone().addScaledVector(Toward, View.camera.near * 4 + 1).project(View.camera);
          const Edge = Math.max(0, Math.min(1, (1.2 - Math.max(Math.abs(At.x), Math.abs(At.y))) / 0.3));
          const Setting = Math.max(0, Math.min(1, (Now.SunElevation + 0.6) / 2));
          let Clear = 0;
          if (Edge > 0 && Setting > 0) {
            const Side = new THREE.Vector3().crossVectors(Toward, Math.abs(Toward.y) > 0.98 ? new THREE.Vector3(1, 0, 0) : new THREE.Vector3(0, 1, 0)).normalize();
            const Lift = new THREE.Vector3().crossVectors(Side, Toward);
            const Reach = Math.tan(SunAngularRadius * 4);
            const Targets = Built?.Group ? Built.Group.children : [];
            for (const [A, B] of [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1]]) {
              const Ray = Toward.clone().addScaledVector(Side, A * Reach).addScaledVector(Lift, B * Reach).normalize();
              Caster.set(View.camera.position, Ray);
              if (!Caster.intersectObjects(Targets, true).length) Clear += 0.2;
            }
          }
          Strength = Clear * Edge * Setting * (0.35 + 0.65 * Math.min(1, Now.SunStrength || 0));
          Brush.setTransform(Ratio, 0, 0, Ratio, 0, 0);
          Drawn = PaintFlare(Brush, Now.FlareAtlas, Now.Lens, { X: ((At.x + 1) / 2) * Width, Y: ((1 - At.y) / 2) * Height, Width, Height, Strength });
        }
        // Where the sun is on the canvas (fractions; off the canvas or behind the camera is "off"), for checks.
        const SunAt = View.camera.position.clone().addScaledVector(Toward, View.camera.near * 4 + 1).project(View.camera);
        Canvas.dataset.sunAt = Toward.dot(Ahead) > 0 && Math.abs(SunAt.x) <= 1 && Math.abs(SunAt.y) <= 1 ? [((SunAt.x + 1) / 2).toFixed(3), ((1 - SunAt.y) / 2).toFixed(3)].join(" ") : "off";
        Canvas.dataset.flare = (Strength * (Now.Lens?.Enabled && !Now.FlareOff ? Now.Lens.Intensity : 0)).toFixed(2);
        Canvas.dataset.flareSprites = String(Drawn);
      };
      // Draw only when something changed: the camera moved, or the scene was touched.
      let Dirty = true, Last = "";
          View.renderer.setAnimationLoop(() => {
        if (!Canvas.offsetParent) return;
        const Now = performance.now();
        const Step = Math.min(0.05, (Now - View.lastTime) / 1000);
        View.lastTime = Now;
        // Zoom stays between 0.6 m (closer, the wheel's glide pushes the camera into a surface) and 150 m (farther, the scene is a dot).
        View.rig.goal.distance = Math.min(150, Math.max(0.6, View.rig.goal.distance));
        // The camera stays above the ground (8 cm up at least): from below, the ground vanishes and the shapes turn to silhouettes.
        const Goal = View.rig.goal;
        Goal.target.y = Math.max(0, Goal.target.y);
        Goal.pitch = Math.max(Goal.pitch, Math.asin(Math.max(-1, Math.min(1, (0.08 - Goal.target.y) / Goal.distance))));
        View.rig.update(Step);
        if (View.camera.position.y < 0.08) {
          View.rig.pitch = Math.max(View.rig.pitch, Math.asin(Math.max(-1, Math.min(1, (0.08 - View.rig.target.y) / View.rig.distance))));
          View.rig.apply();
        }
        const Seen = View.camera.matrixWorld.elements.map((Each) => Each.toFixed(5)).join(",") + View.camera.fov.toFixed(3) + View.canvas.width + "x" + View.canvas.height;
        if (!Dirty && Seen === Last) return;
        Last = Seen;
        Dirty = false;
        const Reach = View.camera.far * 0.92;
        Stars.position.copy(View.camera.position);
        Stars.material.uniforms.uRadius.value = View.camera.far * 0.6;
        Stars.material.uniforms.uScale.value = View.renderer.getPixelRatio();
        Ground.position.set(View.camera.position.x, -0.002, View.camera.position.z);
        Ground.scale.setScalar(Reach);
        if (View.scene.fog) {
          // Haze starts well past the shapes at any zoom and is complete where the ground ends.
          View.scene.fog.near = Math.min(Reach * 0.5, View.rig.distance * 4 + 40);
          View.scene.fog.far = Reach;
        }
        // The dot grid only reads when looking down; edge-on its dots smear into streaks.
        View.grid.visible = View.camera.position.y - View.rig.target.y > View.rig.distance * 0.2;
        const Floor = View.grid.material.uniforms;
        Floor.uFocus.value.copy(View.rig.target);
        Floor.uRadius.value = Math.max(6, View.rig.distance * 2.6);
        View.renderer.render(View.scene, View.camera);
        DrawFlare();
        Canvas.dataset.frames = String(+(Canvas.dataset.frames || 0) + 1);
        // Where each object's centre lands on the canvas (fractions), for checks and tools that aim at objects.
        const Centres = {};
        for (const Each of View.content.children[0]?.children || []) {
          if (Each.userData.asset) continue;
          const At = Each.position.clone().project(View.camera);
          Centres[Each.userData.componentId] = [+((At.x + 1) / 2).toFixed(4), +((1 - At.y) / 2).toFixed(4)];
        }
        Canvas.dataset.centres = JSON.stringify(Centres);
      });
      // Click (no drag) picks.
      let Down = null;
      Canvas.addEventListener("pointerdown", (Event) => (Down = { X: Event.clientX, Y: Event.clientY, Button: Event.button }));
      Canvas.addEventListener("pointerup", (Event) => {
        if (!Down || Down.Button !== 0 || Math.hypot(Event.clientX - Down.X, Event.clientY - Down.Y) > 4) return;
        const Hit = View.pick(Event.clientX, Event.clientY);
        if (Hit) Latest.current.Select?.(Hit.componentId, { Add: Event.ctrlKey || Event.metaKey || Event.shiftKey });
      });
      Canvas.addEventListener("keydown", (Event) => {
        if (Event.key.toLowerCase() === "f") Scene.current?.FrameChosen?.();
      });
      Built = { View, Canvas, Layer, SkyImage, SkyTexture, Hemisphere, Ground, AimSun, Stars, Touch: () => (Dirty = true), Framed: false, Moved: false };
      Scene.current = Built;
      // Until the camera is moved by hand, keep the shapes framed when the viewport changes shape (it opens before the panels settle).
      for (const Name of ["pointerdown", "wheel"]) Canvas.addEventListener(Name, () => (Built.Moved = true), { passive: true });
      const Refit = new ResizeObserver(() => {
        if (Built.Moved || !Built.Bounds) return;
        View.resize();
        View.frame(Built.Bounds, { yaw: 24, pitch: 9, animate: false });
        Built.AimSun();
        Built.Touch();
      });
      Refit.observe(Host.current);
      Built.Refit = Refit;
    } catch (Problem) {
      Fail(String(Problem?.message || Problem));
    }
    return () => {
      if (!Built) return;
      Built.View.resize = () => {}; // its ResizeObserver outlives the canvas
      Built.Refit.disconnect();
      Built.View.renderer.setAnimationLoop(null);
      Built.View.setContent(null);
      Built.View.renderer.dispose();
      Built.View.renderer.forceContextLoss();
      Built.Canvas.remove();
      Built.Layer.remove();
      Scene.current = null;
    };
  }, []);

  // The renderer's listeners read the latest props from here (the renderer itself is built once).
  Latest.current.Select = Select;
  Latest.current.SunDirection = Light.Direction;
  Latest.current.SunElevation = Light.Elevation;
  Latest.current.SunStrength = Light.Strength;
  Latest.current.Lens = Lens;
  Latest.current.FlareOff = FlareOff;
  Latest.current.Chosen = Chosen;

  // Sky and sun.
  const SkyKey = JSON.stringify([Medium, Light.Elevation, Light.Azimuth, Light.Tint, Light.Latitude, Light.Hours, Field, StarsOff]);
  useEffect(() => {
    const Built = Scene.current;
    if (!Built) return;
    // The sky-view map (shared with the Atmosphere's BAKING section): the disc is drawn larger than life (×4) so it reads at viewport
    // sizes; below the horizon it carries on with the colour just above it (also the haze colour), so the thin sliver between the
    // ground's far edge and the true horizon doesn't show a seam; after dusk a faint airglow keeps the sky from going black.
    const SkyMap = SkyViewMap(Medium, Light);
    const Brush = Built.SkyImage.getContext("2d");
    Brush.drawImage(SkyMap.Canvas, 0, 0, Built.SkyImage.width, Built.SkyImage.height);
    Built.SkyTexture.needsUpdate = true;
    // Hemisphere light: the sky's colour 30–50° up, the ground bouncing the albedo; after dusk a moonlit blue fill takes over, so the
    // shapes stay readable at night (BakedMaps.js SkyFill, the same sums as the Sun's lighting LUT).
    const Fill = SkyFill(SkyMap.Band(50, 30), Light.Elevation, Medium, Light.Strength);
    Built.Hemisphere.color.setRGB(...Fill.Sky);
    Built.Hemisphere.groundColor.setRGB(...Fill.Ground);
    Built.Hemisphere.intensity = Fill.Intensity;
    // Haze: the sky's colour at the horizon, so distant ground fades into it the way it does outdoors.
    Built.View.scene.fog = new THREE.Fog(new THREE.Color().setRGB(...SkyMap.Band(1.4, 0.4)), 40, 300);
    const Beam = Built.View.sun;
    Beam.color.setRGB(...Light.Colour);
    Beam.intensity = Light.Beam;
    Built.AimSun();
    // Stars come out between −4° and −14° of sun, with the Stars object's settings, turned about the celestial pole.
    const Star = Built.Stars.material.uniforms;
    Star.uShown.value = Field.Shown && !StarsOff ? Math.max(0, Math.min(1, (-Light.Elevation - 4) / 10)) : 0;
    Star.uLimit.value = Field.Limit;
    Star.uGain.value = Field.Brightness;
    Star.uSize.value = Field.Size;
    Built.Stars.quaternion
      .setFromAxisAngle(new THREE.Vector3(1, 0, 0), (90 - Light.Latitude) * Rad)
      .multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), (Field.Rotation + Light.Hours * 15) * Rad));
    Built.Stars.visible = Star.uShown.value > 0;
    Built.Canvas.dataset.stars = Star.uShown.value.toFixed(2);
    Built.Ground.material.color.setRGB(...Medium.Ground);
    Built.Touch();
    Built.Canvas.dataset.sky = SkyKey;
  }, [SkyKey]);

  // Lens flare: its atlas (shared with the Lens Flare's BAKING section) is baked again when its shapes or the sun's colour change.
  const FlareKey = JSON.stringify([Lens, FlareOff, Light.Colour.map((C) => +C.toFixed(2))]);
  useEffect(() => {
    const Built = Scene.current;
    if (!Built) return;
    Latest.current.FlareAtlas = Lens.Enabled && !FlareOff ? FlareAtlasMap(Lens, Light.Colour).Canvas : null;
    Built.Touch();
  }, [FlareKey]);

  // Shapes.
  const ShapeKey = JSON.stringify(
    Placed.map(({ Row, Along, Position, Rotation, Scale }) => {
      const Own = Values[Row.Id] || {};
      const Material = ResolveMaterial(Own).Channels;
      return [Row.Id, Along, Position, Rotation, Scale, Object.entries(Own).filter(([Key]) => Key.startsWith("Geometry ")), Material.colour?.Fill, Material.roughness?.Fill, Material.metallic?.Fill];
    }).concat(Posed.map(({ Row, ...Pose }) => [Row.Id, Pose]), Parked.map(({ Row, ...Pose }) => [Row.Id, Pose])),
  );
  useEffect(() => {
    const Built = Scene.current;
    if (!Built) return;
    const Group = new THREE.Group();
    const Bounds = new THREE.Box3();
    for (const { Row, Shape, Along, Position, Rotation, Scale } of Placed) {
      const Own = Values[Row.Id] || {};
      const { Geometry, Lift } = ShapeGeometry(Shape, (Key) => Own[Key]);
      const Material = ResolveMaterial(Own).Channels;
      const Colour = /^#[\da-f]{6}$/i.test(Material.colour?.Fill || "") ? Material.colour.Fill : "#c7ada0";
      const Mesh = new THREE.Mesh(
        Geometry,
        new THREE.MeshStandardMaterial({
          color: new THREE.Color(Colour),
          roughness: Math.max(0.04, Math.min(1, +Material.roughness?.Fill || 0.32)),
          metalness: Math.max(0, Math.min(1, +Material.metallic?.Fill || 0)),
        }),
      );
      Mesh.castShadow = Mesh.receiveShadow = true;
      Mesh.position.set(Along + Position[0], Lift * Scale[1] + Position[1], Position[2]);
      Mesh.rotation.set(Rotation[0] * Rad, Rotation[1] * Rad, Rotation[2] * Rad);
      Mesh.scale.set(...Scale);
      Mesh.userData.componentId = Row.Id;
      Group.add(Mesh);
      Mesh.updateMatrixWorld(true);
      Bounds.expandByObject(Mesh);
    }
    // Cameras on tripods, each aimed at the Showcase's middle, with a frustum to its subject distance.
    const Aim = Bounds.isEmpty() ? new THREE.Vector3(0, 0.5, 0) : Bounds.getCenter(new THREE.Vector3());
    const Radius = Bounds.isEmpty() ? 1 : Bounds.getSize(new THREE.Vector3()).length() / 2;
    for (const { Row, Body, Tripod, Turn, Focal, Sensor, Subject, Aspect } of Posed) {
      const At = [Aim.x + Math.sin(Turn) * (Radius * 0.75 + 1.2), 0, Aim.z + Math.cos(Turn) * (Radius * 0.75 + 1.2)];
      const Asset = CameraAsset(Body, { Stand: Tripod, Raised: !Tripod, Height: Body === "mirrorless" ? 1.45 : 1.3 });
      Asset.position.set(...At);
      Asset.rotation.y = Math.atan2(At[0] - Aim.x, At[2] - Aim.z);
      const Head = Asset.userData.body;
      const Lens = new THREE.Vector3(...LensFront[Body]);
      const Down = Math.atan2(Head.position.y + Lens.y - Aim.y, Math.hypot(At[0] - Aim.x, At[2] - Aim.z));
      Head.rotation.x = -Down;
      const Length = Math.min(Subject, 2.4), Wide = (Sensor / 2 / Focal) * Length, Tall = Wide / Aspect;
      const Far = [[-Wide, -Tall], [Wide, -Tall], [Wide, Tall], [-Wide, Tall]].map(([X, Y]) => new THREE.Vector3(Lens.x + X, Lens.y + Y, Lens.z - Length));
      const Lines = [];
      for (let Corner = 0; Corner < 4; Corner++) Lines.push(Lens, Far[Corner], Far[Corner], Far[(Corner + 1) % 4]);
      const Frustum = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(Lines), new THREE.LineBasicMaterial({ color: "#ffd38a", transparent: true, opacity: 0.45, depthWrite: false }));
      Frustum.raycast = () => {};
      Head.add(Frustum);
      // Studio-lit (a matcap) so its metal and glass read in any scene light.
      StudioLit(Asset);
      Asset.userData.componentId = Row.Id;
      Asset.userData.asset = true;
      Group.add(Asset);
    }
    // Vehicles, parked behind the Showcase.
    for (const { Row, Model, Paint, At, Turn } of Parked) {
      const Asset = VehicleAsset(Model, Paint);
      Asset.position.set(...At);
      Asset.rotation.y = Turn;
      Asset.userData.componentId = Row.Id;
      Asset.userData.asset = true;
      Group.add(Asset);
    }
    Built.View.setContent(Group);
    if (!Built.Framed && !Bounds.isEmpty()) {
      Built.Bounds = Bounds.clone().expandByScalar(0.45); // room for the workspace rail on the left
      Built.View.frame(Built.Bounds, { yaw: 24, pitch: 9, animate: false });
      Built.Framed = true;
    }
    // Shadows cover everything that's out (the parked vehicles too).
    const Lit = Bounds.clone();
    for (const Each of Group.children) if (Each.userData.asset) Lit.expandByObject(Each);
    const Sphere = Lit.isEmpty() ? new THREE.Sphere(new THREE.Vector3(), 4) : Lit.getBoundingSphere(new THREE.Sphere());
    Built.AimSun(Sphere);
    Built.Group = Group;
    Built.FrameChosen = () => {
      const Picks = new THREE.Box3();
      for (const Mesh of Built.Group.children) if (Latest.current.Chosen?.has(Mesh.userData.componentId)) Picks.expandByObject(Mesh);
      if (Picks.isEmpty()) return;
      Built.View.frame(Picks.expandByScalar(0.3), { keepAngle: true });
      Built.AimSun(Sphere);
    };
    Built.Touch();
    Built.Canvas.dataset.objects = String(Placed.length);
    Built.Canvas.dataset.vehicles = Parked.map(({ Row, Model }) => Row.Id + ":" + Model).join(" ");
    Built.Canvas.dataset.cameras = Posed.map(({ Row, Body, Tripod }) => Row.Id + ":" + Body + (Tripod ? "" : ":free")).join(" ");
  }, [ShapeKey, SkyKey]);

  // Selection outlines: only these change when the selection does, so picking doesn't rebuild the shapes.
  const ChosenKey = [Selected, ...Chosen].join(" ") + "|" + ShapeKey.length;
  useEffect(() => {
    const Built = Scene.current;
    if (!Built?.Group) return;
    for (const Mesh of Built.Group.children) {
      for (const Old of Mesh.children.filter((Each) => Each.userData.outline)) {
        Mesh.remove(Old);
        Old.geometry.dispose();
        Old.material.dispose();
      }
      const Id = Mesh.userData.componentId;
      if (!Chosen.has(Id)) continue;
      // An asset (several parts) gets a box around it; a shape its own edges.
      let Outline = Mesh.geometry;
      if (Mesh.userData.asset) {
        const Box = new THREE.Box3();
        Mesh.updateMatrixWorld(true);
        const Inverse = Mesh.matrixWorld.clone().invert();
        Mesh.traverse((Part) => {
          if (Part.isMesh) Box.union(new THREE.Box3().setFromObject(Part).applyMatrix4(Inverse));
        });
        Outline = new THREE.BoxGeometry(...Box.getSize(new THREE.Vector3()).toArray());
        Outline.translate(...Box.getCenter(new THREE.Vector3()).toArray());
      }
      const Edges = new THREE.LineSegments(new THREE.EdgesGeometry(Outline, 30), new THREE.LineBasicMaterial({ color: Id === Selected ? Accent : "#ffd98a", depthTest: false, transparent: true, opacity: 0.95 }));
      Edges.renderOrder = 5;
      Edges.raycast = () => {};
      Edges.userData.outline = true;
      Mesh.add(Edges);
    }
    Built.Touch();
    Built.Canvas.dataset.chosen = Built.Group.children.map((Mesh) => Mesh.userData.componentId).filter((Id) => Chosen.has(Id)).join(" ");
  }, [ChosenKey, ShapeKey, SkyKey]);

  useEffect(() => {
    if (FrameToken) Scene.current?.FrameChosen?.();
  }, [FrameToken]);

  return (
    <div className="viewport-3d" ref={Host} role="region" aria-label="3D viewport region">
      {Failed && <p className="viewport-3d-failed">The 3D viewport could not start: {Failed}</p>}
      <span className="viewport-3d-caption">3D PREVIEW · THREE.JS · SHOWCASE LINE {ShowcaseSpacing} M APART</span>
    </div>
  );
}
