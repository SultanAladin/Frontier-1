import { litScreenProjection, screenPointer } from "./screenPaintProjection.js";
import * as THREE from "three";
import { digitalBrushTip } from "./digitalBrush.js";
import {
  digitalCursorLabel,
  drawScreenBrushCursor,
} from "./digitalBrushCursor.js";
import { decalSurfaceHit } from "./decalProjection.js";
import { texturePart } from "./textureSurfaceModel.js";
import { uvTopology } from "./textureUVGeometry.js";

const vertexShader = `
attribute vec2 uvAuthoring;
attribute float surfacePart;
attribute float surfaceChart;
varying vec2 vCursorUV;
varying float vCursorPart;
varying float vCursorChart;
void main(){
  vCursorUV=uvAuthoring;vCursorPart=surfacePart;vCursorChart=surfaceChart;
  gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);
}`;
const fragmentShader = `
uniform sampler2D uTip;
uniform vec2 uCenter;
uniform vec2 uRadius;
uniform vec2 uRotation;
uniform float uOpacity;
uniform float uPart;
uniform float uChart;
uniform float uShape;
uniform float uFill;
uniform float uFace;
uniform vec2 uFaceA;
uniform vec2 uFaceB;
uniform vec2 uFaceC;
uniform vec3 uFillColor;
varying vec2 vCursorUV;
varying float vCursorPart;
varying float vCursorChart;
float edge(vec2 a,vec2 b,vec2 p){vec2 d=b-a,q=p-a;return d.x*q.y-d.y*q.x;}
void main(){
  if(abs(vCursorPart-uPart)>0.25)discard;
  if(uChart>=0.0&&abs(vCursorChart-uChart)>0.25)discard;
  if(uFace>0.5){
    vec3 edges=vec3(edge(uFaceA,uFaceB,vCursorUV),edge(uFaceB,uFaceC,vCursorUV),edge(uFaceC,uFaceA,vCursorUV));
    if(!(all(greaterThanEqual(edges,vec3(-1e-7)))||all(lessThanEqual(edges,vec3(1e-7)))))discard;
  }
  vec3 color=uFillColor;float alpha=uOpacity*0.28;
  if(uFill<0.5){
    vec2 p=vCursorUV-uCenter;
    vec2 local=vec2(uRotation.x*p.x+uRotation.y*p.y,-uRotation.y*p.x+uRotation.x*p.y)/uRadius;
    float distance=(uShape>0.5?max(abs(local.x),abs(local.y)):length(local))-31.0/32.0;
    float pixel=max(fwidth(distance),0.00001);
    float black=1.0-smoothstep(pixel*1.1,pixel*2.5,abs(distance));
    float white=1.0-smoothstep(pixel*0.3,pixel*1.1,abs(distance));
    vec2 texUV=local*0.5+0.5;
    vec4 tip=texture2D(uTip,clamp(texUV,0.0,1.0));
    if(any(lessThan(texUV,vec2(0.0)))||any(greaterThan(texUV,vec2(1.0))))tip.a=0.0;
    float textureAlpha=tip.a*uOpacity*0.42;
    alpha=max(textureAlpha,black);
    color=mix(tip.rgb,vec3(0.008,0.013,0.02),black);
    color=mix(color,vec3(1.0),white);
  }
  if(alpha<0.001)discard;
  gl_FragColor=vec4(color,alpha);
  #include <colorspace_fragment>
}`;

// Paint/Erase use an independent CSS-pixel canvas. The mesh shader remains for
// region Fill/legacy UV previews only. Neither enters document/optical passes.
export function createDigitalBrushCursorController(
  engine,
  getEditor,
  onChange = () => {},
) {
  let screenCanvas = null;
  let pointer = null,
    pending = true,
    mesh = null,
    texture = null,
    material = null,
    signature = "",
    disposed = false;
  const uniforms = {
    uTip: { value: null },
    uCenter: { value: new THREE.Vector2() },
    uRadius: { value: new THREE.Vector2() },
    uRotation: { value: new THREE.Vector2(1, 0) },
    uOpacity: { value: 1 },
    uPart: { value: 1 },
    uChart: { value: -1 },
    uShape: { value: 0 },
    uFill: { value: 0 },
    uFace: { value: 0 },
    uFaceA: { value: new THREE.Vector2() },
    uFaceB: { value: new THREE.Vector2() },
    uFaceC: { value: new THREE.Vector2() },
    uFillColor: { value: new THREE.Color() },
  };
  function allocate() {
    if (mesh) return;
    texture = new THREE.CanvasTexture(
      digitalBrushTip({ shape: "round", hardness: 1 }, "#ffffff"),
    );
    texture.flipY = false;
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.generateMipmaps = false;
    texture.minFilter = texture.magFilter = THREE.LinearFilter;
    uniforms.uTip.value = texture;
    material = new THREE.ShaderMaterial({
      uniforms,
      vertexShader,
      fragmentShader,
      transparent: true,
      depthTest: true,
      depthWrite: false,
      side: THREE.DoubleSide,
      polygonOffset: true,
      polygonOffsetFactor: -4,
      polygonOffsetUnits: -4,
      toneMapped: false,
    });
    mesh = new THREE.Mesh(engine.specimen.geometry, material);
    mesh.name = "digital-brush-footprint";
    mesh.visible = false;
    mesh.renderOrder = 30000;
    mesh.frustumCulled = false;
    mesh.userData.textureStackOverlay = true;
    mesh.raycast = () => {};
    engine.specimen.add(mesh);
  }
  function update(cameraChanged = false) {
    if (disposed || (!pending && !cameraChanged)) return false;
    pending = false;
    const editor = getEditor(),
      rect = engine.renderer.domElement.getBoundingClientRect();
    let cursor = null;
    if (
      pointer &&
      editor?.active &&
      editor.digital &&
      rect.width > 0 &&
      rect.height > 0 &&
      pointer.clientX >= rect.left &&
      pointer.clientX <= rect.right &&
      pointer.clientY >= rect.top &&
      pointer.clientY <= rect.bottom &&
      !pointer.altKey &&
      !pointer.ctrlKey &&
      !pointer.metaKey
    ) {
      const hit = decalSurfaceHit(engine, pointer.clientX, pointer.clientY);
      cursor = editor.getCursor(
        hit,
        screenPointer(pointer, litScreenProjection(engine, rect), rect),
      );
    }
    const next = cursor ? JSON.stringify(cursor) : "";
    if (
      next === signature &&
      (!mesh || mesh.geometry === engine.specimen.geometry)
    ) {
      if (cameraChanged) onChange(cursor, pointer);
      return false;
    }
    signature = next;
    engine.renderer.domElement.dataset.brushCursor = next;
    engine.renderer.domElement.dataset.brushCursorLabel =
      digitalCursorLabel(cursor);
    onChange(cursor, pointer);
    if (cursor?.space === "screen") {
      if (!screenCanvas) {
        screenCanvas = document.createElement("canvas");
        screenCanvas.className = "digital-screen-cursor";
        screenCanvas.setAttribute(
          "aria-label",
          "Screen-space brush footprint preview",
        );
        screenCanvas.setAttribute("aria-hidden", "true");
        engine.renderer.domElement.parentElement?.appendChild(screenCanvas);
      }
      screenCanvas.hidden = false;
      drawScreenBrushCursor(
        screenCanvas,
        { width: rect.width, height: rect.height },
        cursor,
      );
      const changedGL = !!mesh?.visible;
      if (mesh) mesh.visible = false;
      return changedGL;
    }
    if (screenCanvas) {
      screenCanvas.hidden = true;
      drawScreenBrushCursor(
        screenCanvas,
        { width: rect.width, height: rect.height },
        null,
      );
    }
    if (!cursor) {
      if (mesh) mesh.visible = false;
      return true;
    }
    allocate();
    mesh.geometry = engine.specimen.geometry;
    mesh.visible = true;
    const { brush, region, dab } = cursor;
    uniforms.uPart.value = texturePart(region.partId).index;
    uniforms.uChart.value = region.kind === "island" ? region.id : -1;
    uniforms.uFace.value = region.kind === "face" ? 1 : 0;
    if (region.kind === "face") {
      const points = uvTopology(engine.specimen.geometry).triangles[region.id]
        .points;
      [uniforms.uFaceA, uniforms.uFaceB, uniforms.uFaceC].forEach((u, i) =>
        u.value.fromArray(points[i]),
      );
    }
    uniforms.uFill.value = dab ? 0 : 1;
    uniforms.uFillColor.value.set(cursor.color || brush.color);
    uniforms.uShape.value = brush.shape === "square" ? 1 : 0;
    uniforms.uOpacity.value = brush.opacity * (dab?.opacity ?? 1);
    if (dab) {
      uniforms.uCenter.value.set(dab.x / 256, dab.y / 256);
      uniforms.uRadius.value.set(
        dab.radius / 256,
        (dab.radius * brush.roundness) / 256,
      );
      uniforms.uRotation.value.set(Math.cos(dab.angle), Math.sin(dab.angle));
      const tip = digitalBrushTip(brush, dab.color);
      if (texture.image !== tip) {
        texture.image = tip;
        texture.needsUpdate = true;
      }
      const filter =
        brush.preset === "pixel" ? THREE.NearestFilter : THREE.LinearFilter;
      if (texture.magFilter !== filter) {
        texture.minFilter = texture.magFilter = filter;
        texture.needsUpdate = true;
      }
    }
    return true;
  }
  return {
    get pending() {
      return pending;
    },
    move(event) {
      if (event.isPrimary === false) return;
      pointer = {
        clientX: event.clientX,
        clientY: event.clientY,
        pointerType: event.pointerType,
        pressure: event.pressure,
        buttons: event.buttons,
        altKey: event.altKey,
        ctrlKey: event.ctrlKey,
        metaKey: event.metaKey,
      };
      pending = true;
    },
    clear() {
      pointer = null;
      pending = true;
    },
    invalidate() {
      pending = true;
    },
    update,
    dispose() {
      disposed = true;
      screenCanvas?.remove();
      mesh?.removeFromParent();
      material?.dispose();
      texture?.dispose();
      onChange(null, null);
    },
  };
}
