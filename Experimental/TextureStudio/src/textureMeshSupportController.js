import * as THREE from "three";
import { normalizeTextureMeshMaps } from "./textureMeshMaps.js";

const GLSL = `
uniform sampler2D uMeshAO, uMeshNormal, uMeshBevel;
uniform int uMeshSupportBits;
uniform vec2 uMeshNormalY;
vec2 alloyMeshBakeUV(){
 float part=floor(vSurfacePart+.5);float columns=part<2.5?4.:2.;float rows=part<1.5?4.:2.;
 vec2 q=vec2((vAuthoringUv.x-part+1.)*columns,vAuthoringUv.y*rows);
 float local=floor(q.y)*columns+floor(q.x);
 float chartPatch=part<1.5?(local<12.?local:local+16.):part<2.5?local+20.:part<3.5?local+12.:local+16.;
 return (vec2(mod(chartPatch,8.),floor(chartPatch/8.))+fract(q))/vec2(8.,4.);
}
mat3 alloyMeshFrame(vec3 position,vec3 n,vec2 uv){
 vec3 q0=dFdx(position),q1=dFdy(position);vec2 st0=dFdx(uv),st1=dFdy(uv);
 vec3 q1perp=cross(q1,n),q0perp=cross(n,q0);
 vec3 t=q1perp*st0.x+q0perp*st1.x,b=q1perp*st0.y+q0perp*st1.y;
 if(dot(t,t)<1e-16)t=cross(abs(n.y)<.95?vec3(0,1,0):vec3(1,0,0),n);
 t=normalize(t-n*dot(n,t));vec3 ortho=cross(n,t);
 return mat3(t,ortho*(dot(ortho,b)<0.?-1.:1.),n);
}
vec3 alloyMeshRNM(vec3 base,vec3 detail){
 vec3 t=base+vec3(0.,0.,1.),u=detail*vec3(-1.,-1.,1.);
 return normalize(t*dot(t,u)/max(t.z,1e-6)-u);
}
vec3 alloyMeshDetail(vec3 n,mat3 frame){return normalize(vec3(dot(n,frame[0]),dot(n,frame[1]),dot(n,frame[2])));}
vec3 alloyMeshSupportedNormal(vec3 detail,mat3 frame,vec2 uv){
 vec3 base=vec3(0.,0.,1.);
 if(mod(floor(float(uMeshSupportBits)/2.),2.)>.5){base=texture2D(uMeshNormal,uv).xyz*2.-1.;base.y*=uMeshNormalY.x;base=normalize(base);}
 if(uMeshSupportBits>=4){vec3 bevel=texture2D(uMeshBevel,uv).xyz*2.-1.;bevel.y*=uMeshNormalY.y;base=alloyMeshRNM(base,normalize(bevel));}
 return normalize(frame*alloyMeshRNM(base,alloyMeshDetail(detail,frame)));
}
`;
export function installTextureMeshSupport(material, uniforms) {
  if (material.userData.textureMeshSupport) return;
  material.userData.textureMeshSupport = true;
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, uniforms);
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <common>",
      "#include <common>\n" + GLSL,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <normal_fragment_begin>",
      `#include <normal_fragment_begin>
      vec2 meshSupportUV=alloyMeshBakeUV();
      mat3 meshSupportFrame=alloyMeshFrame(-vViewPosition,normal,meshSupportUV);`,
    );
    const supportedNormals = `
      if(uMeshSupportBits>=2){
        normal=alloyMeshSupportedNormal(normal,meshSupportFrame,meshSupportUV);
        #ifdef USE_CLEARCOAT
          clearcoatNormal=alloyMeshSupportedNormal(clearcoatNormal,meshSupportFrame,meshSupportUV);
        #endif
      }
    `;
    // Native createMaterial expands this chunk itself to preserve its physical
    // recipes. Plain decal materials retain the include. Handle both paths,
    // after recipe relief/flakes and before lighting initializes its geometry.
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lights_physical_fragment>",
      supportedNormals + "#include <lights_physical_fragment>",
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "PhysicalMaterial material;",
      supportedNormals + "PhysicalMaterial material;",
    );
    // AO modulates indirect light, not albedo, emission or tangent-normal RGB.
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lights_fragment_end>",
      `#include <lights_fragment_end>
      if(mod(float(uMeshSupportBits),2.)>.5){float meshAO=texture2D(uMeshAO,meshSupportUV).r;
        reflectedLight.indirectDiffuse*=meshAO;reflectedLight.indirectSpecular*=meshAO;
        #ifdef USE_CLEARCOAT
          clearcoatSpecularIndirect*=meshAO;
        #endif
        #ifdef USE_SHEEN
          sheenSpecularIndirect*=meshAO;
        #endif
      }`,
    );
  };
  material.customProgramCacheKey = () =>
    cache() + "|teapot-mesh-support-rnm-v1";
}
function solidTexture(values) {
  const t = new THREE.DataTexture(
    Uint8Array.from(values),
    1,
    1,
    THREE.RGBAFormat,
  );
  t.colorSpace = THREE.NoColorSpace;
  t.needsUpdate = true;
  return t;
}
export function createTextureMeshSupportController(engine, onError) {
  const white = solidTexture([255, 255, 255, 255]),
    flat = solidTexture([128, 128, 255, 255]);
  const uniforms = {
    uMeshAO: { value: white },
    uMeshNormal: { value: flat },
    uMeshBevel: { value: flat },
    uMeshSupportBits: { value: 0 },
    uMeshNormalY: { value: new THREE.Vector2(1, 1) },
  };
  let epoch = 0,
    disposed = false,
    signature = "",
    pendingSignature = null,
    resources = [];
  async function update(raw) {
    const next = JSON.stringify(raw || null);
    if (pendingSignature === next) return;
    const ticket = ++epoch,
      loaded = [];
    // Returning to the already displayed snapshot must invalidate a pending
    // different decode; otherwise it could publish after an undo/import.
    if (signature === next) {
      pendingSignature = null;
      return;
    }
    pendingSignature = next;
    try {
      const snapshot = normalizeTextureMeshMaps(raw);
      for (const [role, source] of Object.entries(snapshot?.maps || {})) {
        const image = new Image();
        image.src = source.dataUrl;
        await image.decode();
        if (disposed || ticket !== epoch) {
          loaded.forEach((r) => r.texture.dispose());
          return;
        }
        if (image.width !== source.width || image.height !== source.height)
          throw new Error(
            "Decoded mesh-map dimensions disagree with the project.",
          );
        const texture = new THREE.Texture(image);
        texture.colorSpace = THREE.NoColorSpace;
        texture.generateMipmaps = false;
        texture.minFilter = texture.magFilter = THREE.LinearFilter;
        texture.needsUpdate = true;
        loaded.push({ role, texture, normalY: source.normalY });
      }
      if (disposed || ticket !== epoch) {
        loaded.forEach((r) => r.texture.dispose());
        return;
      }
      uniforms.uMeshAO.value = white;
      uniforms.uMeshNormal.value = uniforms.uMeshBevel.value = flat;
      uniforms.uMeshSupportBits.value = 0;
      uniforms.uMeshNormalY.value.set(1, 1);
      for (const r of loaded) {
        if (r.role === "occlusion") {
          uniforms.uMeshAO.value = r.texture;
          uniforms.uMeshSupportBits.value |= 1;
        }
        if (r.role === "normal") {
          uniforms.uMeshNormal.value = r.texture;
          uniforms.uMeshSupportBits.value |= 2;
          uniforms.uMeshNormalY.value.x = r.normalY === "-Y" ? -1 : 1;
        }
        if (r.role === "bevel-normal") {
          uniforms.uMeshBevel.value = r.texture;
          uniforms.uMeshSupportBits.value |= 4;
          uniforms.uMeshNormalY.value.y = r.normalY === "-Y" ? -1 : 1;
        }
      }
      resources.forEach((r) => r.texture.dispose());
      resources = loaded;
      signature = next;
      engine.renderer.domElement.dataset.meshSupportMaps = JSON.stringify(
        loaded.map((r) => r.role),
      );
      engine.dirty = true;
    } catch (error) {
      loaded.forEach((r) => r.texture.dispose());
      if (!disposed && ticket === epoch)
        onError?.(`Mesh support: ${error.message}`);
    } finally {
      if (ticket === epoch) pendingSignature = null;
    }
  }
  return {
    uniforms,
    update,
    install: (material) => installTextureMeshSupport(material, uniforms),
    dispose() {
      disposed = true;
      epoch++;
      resources.forEach((r) => r.texture.dispose());
      white.dispose();
      flat.dispose();
    },
  };
}
