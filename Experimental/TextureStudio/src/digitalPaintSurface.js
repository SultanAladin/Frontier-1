import * as THREE from "three";
import { createDigitalPaintSession } from "./digitalPaintGpu.js";
import {
  DIGITAL_PAINT_CHANNELS,
  paintedChannelIds,
} from "./textureDigitalPaint.js";
import {
  textureLayerRenderOrder,
  textureMaterialRecipe,
} from "./textureStackEvaluation.js";

// The brush atlas is a texture array, so thirteen channel targets cost ONE
// sampler, not thirteen. Colour slices are converted to linear, premultiplied RGB for filtering;
// scalar slices are linear and premultiplied too. Alpha is independent per channel. The final slice is their union.
const channelIndex = (id) =>
  DIGITAL_PAINT_CHANNELS.findIndex((c) => c.id === id);
const I = Object.fromEntries(
  DIGITAL_PAINT_CHANNELS.map((c) => [c.id, channelIndex(c.id)]),
);
const GLSL = `
uniform highp sampler2DArray uDigitalAtlas, uDigitalUnderlay;
uniform int uDigitalSlots[13], uDigitalUnion, uDigitalEnabled, uDigitalSparse, uDigitalInherited, uDigitalNormal, uDigitalBlend, uDigitalCapture;
uniform float uDigitalHeightStrength;
uniform mat3 uDigitalObjectToView, uDigitalViewToObject;
vec2 digitalUV(){return vec2(vAuthoringUv.x/4.,vAuthoringUv.y);}
vec4 digitalFieldAt(int i,vec2 uv){int s=uDigitalSlots[i];vec4 p=uDigitalEnabled==1&&s>=0?texture(uDigitalAtlas,vec3(uv,float(s))):vec4(0.);p.rgb/=max(p.a,1e-6);
// On a sparse pass coverage is applied once by its union alpha. Channel alpha
// is relative coverage inside that pass; native-owner overrides keep raw alpha.
if(uDigitalSparse==1&&p.a>0.){float coverage=texture(uDigitalAtlas,vec3(uv,float(uDigitalUnion))).r;p.a=clamp(p.a/max(coverage,1e-6),0.,1.);}
return p;}
vec4 digitalField(int i){return digitalFieldAt(i,digitalUV());}
float digitalScalar(int i,float original,float lo,float hi){vec4 p=digitalField(i);return mix(original,lo+p.r*(hi-lo),p.a);}
vec3 digitalBlend(vec3 a,vec3 b){
 if(uDigitalBlend==1)return a*b;
 if(uDigitalBlend==2)return 1.-(1.-a)*(1.-b);
 if(uDigitalBlend==3)return mix(2.*a*b,1.-2.*(1.-a)*(1.-b),step(vec3(.5),a));
 if(uDigitalBlend==4)return min(a+b,vec3(1.));
 if(uDigitalBlend==5)return min(a,b);
 return b;
}
vec3 digitalColour(int i,vec3 original){vec4 p=digitalField(i);return mix(original,digitalBlend(original,p.rgb),p.a);}
float digitalHeight(vec2 uv){vec4 p=digitalFieldAt(${I.height},uv);return (p.r-.5)*p.a;}
mat3 digitalFrame(vec3 position,vec3 n,vec2 uv){
 vec3 q0=dFdx(position),q1=dFdy(position);vec2 s0=dFdx(uv),s1=dFdy(uv);
 vec3 p0=cross(q1,n),p1=cross(n,q0),t=p0*s0.x+p1*s1.x,b=p0*s0.y+p1*s1.y;
 if(dot(t,t)<1e-16)t=cross(abs(n.y)<.95?vec3(0,1,0):vec3(1,0,0),n);
 t=normalize(t-n*dot(n,t));vec3 o=cross(n,t);return mat3(t,o*(dot(o,b)<0.?-1.:1.),n);
}
vec3 digitalRelief(vec3 n,vec3 geometryNormal){
 if(uDigitalNormal!=1||uDigitalEnabled!=1||uDigitalSlots[${I.height}]<0)return n;
 vec2 uv=digitalUV(),texel=vec2(1./1024.,1./256.);
 float x=digitalHeight(uv+vec2(texel.x,0.))-digitalHeight(uv-vec2(texel.x,0.));
 float y=digitalHeight(uv+vec2(0.,texel.y))-digitalHeight(uv-vec2(0.,texel.y));
 vec3 detail=normalize(vec3(-x*uDigitalHeightStrength*4.,-y*uDigitalHeightStrength*4.,1.));
 mat3 f=digitalFrame(-vViewPosition,geometryNormal,vAuthoringUv);
 vec3 base=normalize(vec3(dot(n,f[0]),dot(n,f[1]),dot(n,f[2]))),t=base+vec3(0,0,1),u=detail*vec3(-1,-1,1);
 return normalize(f*normalize(t*dot(t,u)/max(t.z,1e-6)-u));
}
`;
const BEFORE_PHYSICAL = `
vec4 digitalPbr=vec4(roughnessFactor,metalnessFactor,1.,0.);
vec4 digitalRadiance=vec4(totalEmissiveRadiance,.25);
vec4 digitalSheen=vec4(0.);vec4 digitalScattering=vec4(0.);vec4 digitalReflectance=vec4(0.);vec4 digitalDetails=vec4(0.);float digitalInheritedAngle=0.;
if(uDigitalInherited==1){
 diffuseColor.rgb=texture(uDigitalUnderlay,vec3(digitalUV(),0.)).rgb;
 digitalPbr=texture(uDigitalUnderlay,vec3(digitalUV(),1.));
 digitalRadiance=texture(uDigitalUnderlay,vec3(digitalUV(),2.));
 digitalSheen=texture(uDigitalUnderlay,vec3(digitalUV(),3.));
 digitalScattering=texture(uDigitalUnderlay,vec3(digitalUV(),4.));
 digitalReflectance=texture(uDigitalUnderlay,vec3(digitalUV(),6.));
 digitalDetails=texture(uDigitalUnderlay,vec3(digitalUV(),7.));
 digitalInheritedAngle=digitalDetails.r*360.;
 normal=normalize(uDigitalObjectToView*(texture(uDigitalUnderlay,vec3(digitalUV(),5.)).rgb*2.-1.));
 #ifdef USE_CLEARCOAT
  clearcoatNormal=normal;
 #endif
}
diffuseColor.rgb=digitalColour(${I.baseColor},diffuseColor.rgb);
roughnessFactor=digitalScalar(${I.roughness},digitalPbr.r,0.,1.);
metalnessFactor=digitalScalar(${I.metalness},digitalPbr.g,0.,1.);
totalEmissiveRadiance=digitalColour(${I.emission},digitalRadiance.rgb);
float digitalAo=digitalScalar(${I.occlusion},digitalPbr.b,0.,1.);
vec4 digitalSS=digitalField(${I.subsurface});
// Digital subsurface target is a bounded diffuse tint, not a volume solver.
diffuseColor.rgb=mix(diffuseColor.rgb,digitalSS.rgb,digitalSS.a*.25);
normal=digitalRelief(normal,nonPerturbedNormal);
#ifdef USE_CLEARCOAT
 clearcoatNormal=digitalRelief(clearcoatNormal,nonPerturbedNormal);
#endif
`;
export function installDigitalPaintMaterial(
  material,
  uniforms,
  captureUniforms,
) {
  if (material.userData.digitalPaint) return;
  material.userData.digitalPaint = uniforms;
  // Compile capabilities without changing ANY native parameter. Uniforms for
  // absent features remain zero; no transmission/IOR recipe is rewritten.
  material.defines = {
    ...material.defines,
    USE_CLEARCOAT: "",
    USE_SHEEN: "",
    USE_ANISOTROPY: "",
  };
  const original = material.onBeforeCompile,
    key = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, uniforms, captureUniforms);
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <common>",
      "#include <common>\n" + GLSL,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <alphatest_fragment>",
      `
      if(uDigitalSparse==1){float unionAlpha=uDigitalEnabled==1?texture(uDigitalAtlas,vec3(digitalUV(),float(uDigitalUnion))).r:0.;diffuseColor.a*=unionAlpha;}
      diffuseColor.a*=digitalScalar(${I.opacity},1.,0.,1.);
      #include <alphatest_fragment>`,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lights_physical_fragment>",
      THREE.ShaderChunk.lights_physical_fragment,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "PhysicalMaterial material;",
      BEFORE_PHYSICAL + "\nPhysicalMaterial material;",
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "material.ior = ior;",
      `material.ior=digitalScalar(${I.refractionIndex},uDigitalInherited==1?1.+2.*digitalReflectance.g:ior,1.,3.);`,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "material.clearcoat = clearcoat;",
      `material.clearcoat=digitalScalar(${I.clearcoat},uDigitalInherited==1?digitalReflectance.r:clearcoat,0.,1.);`,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "material.clearcoatRoughness = clearcoatRoughness;",
      "material.clearcoatRoughness=uDigitalInherited==1?digitalDetails.g:clearcoatRoughness;",
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "material.sheenRoughness = clamp( sheenRoughness, 0.07, 1.0 );",
      "material.sheenRoughness=clamp(uDigitalInherited==1?digitalDetails.b:sheenRoughness,.07,1.);",
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "material.sheenColor = sheenColor;",
      `material.sheenColor=digitalColour(${I.sheen},uDigitalInherited==1?digitalSheen.rgb:sheenColor);`,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "material.anisotropy = length( anisotropyV );",
      `
      // Preserve the native vector bit-for-bit when neither target is painted,
      // and avoid undefined atan(0,0) on devices with strict shader arithmetic.
      if(uDigitalInherited==1||digitalField(${I.anisotropy}).a>0.||digitalField(${I.anisotropyAngle}).a>0.){
        float nativeAniso=length(anisotropyV);
        float nativeAngle=nativeAniso>1e-6?degrees(atan(anisotropyV.y,anisotropyV.x)):0.;
        float digitalAniso=digitalScalar(${I.anisotropy},uDigitalInherited==1?digitalReflectance.b:nativeAniso,0.,1.);
        float digitalAngle=digitalScalar(${I.anisotropyAngle},uDigitalInherited==1?digitalInheritedAngle:nativeAngle,0.,360.);
        anisotropyV=digitalAniso*vec2(cos(radians(digitalAngle)),sin(radians(digitalAngle)));
      }
      material.anisotropy = length( anisotropyV );`,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lights_fragment_end>",
      `#include <lights_fragment_end>
      reflectedLight.indirectDiffuse*=digitalAo;reflectedLight.indirectSpecular*=digitalAo;
      #ifdef USE_CLEARCOAT
        clearcoatSpecularIndirect*=digitalAo;
      #endif
      #ifdef USE_SHEEN
        sheenSpecularIndirect*=digitalAo;
      #endif`,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lights_fragment_begin>",
      `if(uDigitalCapture>0){
      // Raw, linear PBR fields of LOWER rows only. Never read lit RGB back as albedo.
      if(uDigitalCapture==1)gl_FragColor=vec4(diffuseColor.rgb,diffuseColor.a);
      if(uDigitalCapture==2)gl_FragColor=vec4(roughnessFactor,metalnessFactor,digitalAo,diffuseColor.a);
      if(uDigitalCapture==3)gl_FragColor=vec4(totalEmissiveRadiance,diffuseColor.a);
      if(uDigitalCapture==4)gl_FragColor=vec4(material.sheenColor,diffuseColor.a);
      if(uDigitalCapture==5)gl_FragColor=vec4(uDigitalInherited==1?digitalScattering.rgb:vec3(0.),diffuseColor.a);
      if(uDigitalCapture==6)gl_FragColor=vec4(normalize(uDigitalViewToObject*normal)*.5+.5,diffuseColor.a);
      if(uDigitalCapture==7)gl_FragColor=vec4(material.clearcoat,(material.ior-1.)/2.,material.anisotropy,diffuseColor.a);
      if(uDigitalCapture==8){float resolvedAngle=material.anisotropy>1e-6?fract(atan(anisotropyV.y,anisotropyV.x)/6.28318530718+1.):0.;gl_FragColor=vec4(resolvedAngle,material.clearcoatRoughness,material.sheenRoughness,diffuseColor.a);}
      return;}
      #include <lights_fragment_begin>
    `,
    );
    // Every capture keeps source-over coverage in alpha, including scalar
    // slices. Never overload alpha with a fourth PBR value.
  };
  material.customProgramCacheKey = () => key() + "|digital-pbr-fields-v2";
  material.needsUpdate = true;
}
const BLENDS = ["Normal", "Multiply", "Screen", "Overlay", "Add", "Darken"];
export const DIGITAL_UNDERLAY_TILE_EDGE = 128;
const FIELD_CLEAR = [
  [1, 1, 1, 1],
  [0.75, 0, 1, 1],
  [0, 0, 0, 1],
  [0, 0, 0, 1],
  [0, 0, 0, 1],
  [0.5, 0.5, 1, 1],
  [0, 0.25, 0, 1],
  [0, 0, 0, 1],
];
export function createDigitalPaintSurfaceController(engine, onError) {
  const dummy = new THREE.DataArrayTexture(new Uint8Array(4), 1, 1, 1);
  dummy.needsUpdate = true;
  const capture = {
    uDigitalCapture: { value: 0 },
    uDigitalObjectToView: { value: new THREE.Matrix3() },
    uDigitalViewToObject: { value: new THREE.Matrix3() },
  };
  const entries = new Map(),
    bindings = new Map();
  let doc = null,
    disposed = false;
  function makeEntry(id, sparse) {
    const uniforms = bindings.get(id) || {
      uDigitalAtlas: { value: dummy },
      uDigitalUnderlay: { value: dummy },
      uDigitalSlots: { value: new Int32Array(13).fill(-1) },
      uDigitalUnion: { value: 0 },
      uDigitalEnabled: { value: 0 },
      uDigitalSparse: { value: sparse ? 1 : 0 },
      uDigitalInherited: { value: 0 },
      uDigitalNormal: { value: 0 },
      uDigitalHeightStrength: { value: 0.4 },
      uDigitalBlend: { value: 0 },
    };
    bindings.set(id, uniforms);
    return {
      id,
      uniforms,
      session: null,
      sessionGeometry: null,
      atlas: null,
      underlay: null,
      lowerSignature: "",
    };
  }
  const empty = makeEntry("system", false);
  function update(next) {
    doc = next;
    const keep = new Set();
    for (const layer of doc.layers) {
      if (!paintedChannelIds(layer).length) continue;
      keep.add(layer.id);
      let entry = entries.get(layer.id);
      if (!entry) {
        entry = makeEntry(layer.id, !textureMaterialRecipe(layer));
        entries.set(layer.id, entry);
      }
      entry.uniforms.uDigitalSparse.value = textureMaterialRecipe(layer)
        ? 0
        : 1;
      // 💡 The atlas session owns change detection: it deposits only the
      //    growing stroke's new dabs and re-uploads only dirty texture
      //    slices. Geometry replacement still forces a fresh session — a
      //    frozen screen projection must be replayed on the final geometry,
      //    not keep the placeholder mesh's atlas after reload.
      if (
        !entry.session ||
        entry.sessionGeometry !== engine.specimen.geometry
      ) {
        entry.session?.dispose();
        // 💡 GPU deposition when WebGL2 is available (dabs, composites and
        //    the slice atlas never leave the GPU); the factory and the
        //    session itself fall back to the CPU session on any failure.
        entry.session = createDigitalPaintSession(
          engine.renderer,
          engine.specimen.geometry,
        );
        entry.sessionGeometry = engine.specimen.geometry;
      }
      const atlas = entry.session.update(layer);
      entry.atlas = atlas;
      entry.uniforms.uDigitalAtlas.value = atlas.texture;
      entry.uniforms.uDigitalSlots.value = atlas.slots;
      entry.uniforms.uDigitalUnion.value = atlas.unionSlot;
      entry.uniforms.uDigitalEnabled.value = 1;
      entry.uniforms.uDigitalNormal.value = layer.channels.includes("normal")
        ? 1
        : 0;
      entry.uniforms.uDigitalHeightStrength.value =
        layer.paint.operations
          .filter((op) => Object.hasOwn(op.channels, "height"))
          .at(-1)?.brush.heightStrength ?? 0.4;
      entry.uniforms.uDigitalBlend.value = BLENDS.indexOf(layer.blend);
      engine.dirty = true;
    }
    for (const [id, entry] of entries)
      if (!keep.has(id)) {
        entry.uniforms.uDigitalEnabled.value = 0;
        entry.uniforms.uDigitalInherited.value = 0;
        entry.uniforms.uDigitalAtlas.value =
          entry.uniforms.uDigitalUnderlay.value = dummy;
        entry.session?.dispose();
        entry.session = null;
        entry.underlay?.dispose();
        entries.delete(id);
        engine.dirty = true;
      }
    for (const id of bindings.keys())
      if (id !== "system" && !doc.layers.some((l) => l.id === id))
        bindings.delete(id);
    const canvas = engine.renderer.domElement;
    canvas.dataset.digitalPaintLayers = JSON.stringify(
      [...entries.values()].map((e) => ({
        layerId: e.id,
        channels: e.atlas.ids,
      })),
    );
    canvas.dataset.digitalPaintSlices = String(
      [...entries.values()].reduce((n, e) => n + e.atlas.ids.length + 1, 0),
    );
    canvas.dataset.digitalPaintBackend = [...entries.values()]
      .map((e) => (e.session?.gpu ? "gpu" : "cpu"))
      .join(",");
  }
  function prepare() {
    if (disposed || !doc) return;
    engine.specimen.updateWorldMatrix(true, false);
    engine.camera.updateWorldMatrix(true, false);
    capture.uDigitalObjectToView.value.getNormalMatrix(
      new THREE.Matrix4().multiplyMatrices(
        engine.camera.matrixWorldInverse,
        engine.specimen.matrixWorld,
      ),
    );
    capture.uDigitalViewToObject.value
      .copy(capture.uDigitalObjectToView.value)
      .invert();
    const r = engine.renderer;
    let succeeded = true;
    for (const entry of [...entries.values()].sort(
      (a, b) =>
        doc.layers.findIndex((l) => l.id === b.id) -
        doc.layers.findIndex((l) => l.id === a.id),
    )) {
      if (!entry.uniforms.uDigitalSparse.value) continue;
      const index = doc.layers.findIndex((l) => l.id === entry.id),
        order = textureLayerRenderOrder(doc, entry.id);
      const prefix = [];
      engine.scene.traverse((object) => {
        if (
          object.isMesh &&
          object.userData.textureStackPass &&
          object.renderOrder < order
        )
          prefix.push([
            object.uuid,
            object.material.uuid,
            object.geometry.uuid,
          ]);
      });
      const signature = JSON.stringify([
        doc.layers.slice(index + 1),
        doc.meshMaps,
        prefix,
      ]);
      if (signature === entry.lowerSignature) continue;
      if (!entry.underlay)
        entry.underlay = new THREE.WebGLArrayRenderTarget(
          DIGITAL_UNDERLAY_TILE_EDGE * 4,
          DIGITAL_UNDERLAY_TILE_EDGE,
          8,
          {
            depthBuffer: false,
            stencilBuffer: false,
            minFilter: THREE.LinearFilter,
            magFilter: THREE.LinearFilter,
          },
        );
      const target = entry.underlay;
      const visibility = new Map();
      try {
        // The UV helper restores renderer/scene/material state even on errors.
        engine.scene.traverse((object) => {
          if (!object.isMesh) return;
          visibility.set(object, object.visible);
          if (object.userData.textureStackPass && object.renderOrder >= order)
            object.visible = false;
        });
        engine.stackUV.withState(target, () => {
          for (let slice = 0; slice < 8; slice++) {
            const [red, green, blue, alpha] = FIELD_CLEAR[slice];
            capture.uDigitalCapture.value = slice + 1;
            r.setRenderTarget(target, slice);
            r.setClearColor(new THREE.Color(red, green, blue), alpha);
            r.clear();
            r.render(engine.scene, engine.camera);
          }
        });
        entry.uniforms.uDigitalUnderlay.value = target.texture;
        entry.uniforms.uDigitalInherited.value = 1;
        entry.lowerSignature = signature;
      } catch (error) {
        succeeded = false;
        entry.uniforms.uDigitalInherited.value = 0;
        onError?.(`Digital channel underlay: ${error.message}`);
      } finally {
        capture.uDigitalCapture.value = 0;
        visibility.forEach((visible, object) => {
          object.visible = visible;
        });
      }
    }
    r.domElement.dataset.digitalPaintReady = succeeded ? "true" : "false";
  }
  return {
    update,
    prepare,
    install(material, layerId = null) {
      installDigitalPaintMaterial(
        material,
        layerId
          ? bindings.get(layerId) || makeEntry(layerId, false).uniforms
          : empty.uniforms,
        capture,
      );
    },
    dispose() {
      disposed = true;
      for (const entry of entries.values()) {
        entry.session?.dispose();
        entry.session = null;
        entry.underlay?.dispose();
      }
      entries.clear();
      bindings.clear();
      dummy.dispose();
    },
  };
}
