import * as THREE from "three";
import { leatherSourceSVG } from "./leatherSource.js";
import {
  patternSVG,
  patternDimensions,
  validatePattern,
} from "./patternDocument.js";

export function patternImage(url) {
  return new Promise((resolve, reject) => {
    const i = new Image();
    i.onload = () => resolve(i);
    i.onerror = () =>
      reject(new Error("Could not decode the embedded pattern image."));
    i.src = url;
  });
}
export async function rasterPatternSVG(svg, width, height) {
  const url = URL.createObjectURL(new Blob([svg], { type: "image/svg+xml" }));
  try {
    const image = await patternImage(url),
      canvas = document.createElement("canvas");
    canvas.width = width;
    canvas.height = height;
    canvas.getContext("2d").drawImage(image, 0, 0, width, height);
    return canvas;
  } finally {
    URL.revokeObjectURL(url);
  }
}
export async function rasterLeatherSource() {
  const source = await rasterPatternSVG(leatherSourceSVG(), 2048, 2048),
    atlas = document.createElement("canvas");
  atlas.width = atlas.height = 2056;
  const ctx = atlas.getContext("2d");
  for (let j = 0; j < 2; j++)
    for (let i = 0; i < 2; i++) {
      const patch = document.createElement("canvas");
      patch.width = patch.height = 1024;
      patch
        .getContext("2d")
        .drawImage(source, i * 1024, j * 1024, 1024, 1024, 0, 0, 1024, 1024);
      const x = i * 1028 + 2,
        y = j * 1028 + 2;
      for (let dy = -1; dy <= 1; dy++)
        for (let dx = -1; dx <= 1; dx++) {
          ctx.save();
          ctx.beginPath();
          ctx.rect(i * 1028, j * 1028, 1028, 1028);
          ctx.clip();
          ctx.drawImage(patch, x + dx * 1024, y + dy * 1024);
          ctx.restore();
        }
    }
  return atlas;
}
const patternTextureCache = new Map();
function acquirePatternTextures(key, build) {
  let entry = patternTextureCache.get(key);
  if (!entry) {
    const make = () => {
      const t = new THREE.Texture();
      t.wrapS = t.wrapT = THREE.RepeatWrapping;
      t.minFilter = THREE.LinearMipmapLinearFilter;
      t.magFilter = THREE.LinearFilter;
      t.anisotropy = 4;
      t.flipY = false;
      t.premultiplyAlpha = true;
      return t;
    };
    entry = { refs: 0, textures: [make(), make(), make(), make(), make()], ready: null };
    entry.ready =
      typeof document === "undefined"
        ? Promise.resolve()
        : build().then((canvases) =>
            canvases.forEach((c, i) => {
              if (c.data) {
                entry.textures[i].isDataTexture = true;
                entry.textures[i].premultiplyAlpha = false;
              }
              entry.textures[i].image = c;
              entry.textures[i].needsUpdate = true;
            }),
          );
    patternTextureCache.set(key, entry);
  }
  entry.refs++;
  return {
    ...entry,
    release: () => {
      if (--entry.refs === 0) {
        entry.ready
          .finally(() => entry.textures.forEach((t) => t.dispose()))
          .catch(() => {});
        patternTextureCache.delete(key);
      }
    },
  };
}
export function attachSurfaceSources(material, p) {
  const leather = p.type === 11 || p.type === 30,
    doc = p.pattern ? validatePattern(p.pattern) : null;
  const resources = [];
  let hide, pattern;
  if (leather) {
    hide = acquirePatternTextures("leather-v1", async () => [
      await rasterLeatherSource(),
    ]);
    resources.push(hide);
  }
  if (doc) {
    // Naming, orientation, repeat count and mapping are metadata/uniforms, not
    // raster content. Reuse the same source maps while these are edited live.
    const sourceKey = JSON.stringify({
      background: doc.background,
      backgroundOpacity: doc.backgroundOpacity,
      repeat: doc.repeat,
      tileAxes: doc.tileAxes,
      layers: doc.layers.map(
        ({ name, id, locked, group, EnclosureToken, StructureToken, LinkedDefinition, LinkedSlot, LinkedInstance, ...surface }) => surface,
      ),
    });
    pattern = acquirePatternTextures(sourceKey, async () => {
      const [w, h] = patternDimensions(doc),
        scale = 2;
      const [color, roughness, metalness, height, occlusion, finish, normals] = await Promise.all(
        ["color", "roughness", "metalness", "normalHeight", "occlusion", "finish", "normal"].map(mode => rasterPatternSVG(patternSVG(doc, mode), w * scale, h * scale)),
      );
      const Read = Canvas => Canvas.getContext("2d").getImageData(0, 0, Canvas.width, Canvas.height).data;
      const Roughness = Read(roughness), Metalness = Read(metalness), Height = Read(height), Occlusion = Read(occlusion);
      const Packed = new Uint8Array(Roughness.length), Coverage = new Uint8Array(Roughness.length);
      for (let Offset = 0; Offset < Packed.length; Offset += 4)
      {
        Packed[Offset] = Roughness[Offset]; Packed[Offset + 1] = Metalness[Offset];
        Packed[Offset + 2] = Height[Offset + 3] ? Height[Offset] : 128; Packed[Offset + 3] = Occlusion[Offset + 3] ? Occlusion[Offset] : 255;
        Coverage[Offset] = Roughness[Offset + 3]; Coverage[Offset + 1] = Metalness[Offset + 3]; Coverage[Offset + 2] = Height[Offset + 3]; Coverage[Offset + 3] = Occlusion[Offset + 3];
      }
      return [color, { data: Packed, width: roughness.width, height: roughness.height }, finish, normals, { data: Coverage, width: roughness.width, height: roughness.height }];
    });
    for (const texture of pattern.textures) {
      texture.wrapS = ["y", "none"].includes(doc.tileAxes)
        ? THREE.ClampToEdgeWrapping
        : THREE.RepeatWrapping;
      texture.wrapT = ["x", "none"].includes(doc.tileAxes)
        ? THREE.ClampToEdgeWrapping
        : THREE.RepeatWrapping;
    }
    resources.push(pattern);
  }
  material.userData.ready = Promise.all(resources.map((r) => r.ready));
  material.addEventListener("dispose", () =>
    resources.forEach((r) => r.release()),
  );
  return {
    leather,
    doc,
    uniforms: {
      uHideSource: { value: hide?.textures[0] || null },
      uHidePatch: {
        value: Math.max(0, Math.min(3, Number(p.hideVariant) || 0)),
      },
      uHideVariation: {
        value: Math.max(0, Math.min(1, Number(p.hideVariation ?? 0.65))),
      },
      uPatternBeads: {
        value: new THREE.Vector3(
          doc?.beadwork?.columns || 1,
          doc?.beadwork?.rows || 1,
          doc?.beadwork?.height || 0,
        ),
      },
      uPatternColor: { value: pattern?.textures[0] || null },
      uPatternParams: { value: pattern?.textures[1] || null },
      uPatternFinish: { value: pattern?.textures[2] || null },
      uPatternNormals: { value: pattern?.textures[3] || null },
      uPatternChannelMask: { value: pattern?.textures[4] || null },
      uPatternRepeats: { value: doc?.repeats || 1 },
      uPatternAngle: { value: ((doc?.rotation || 0) * Math.PI) / 180 },
      uPatternAspect: {
        value: new THREE.Vector2(
          ...(doc ? patternDimensions(doc).map((v) => v / 512) : [1, 1]),
        ),
      },
      uPatternObject: {
        value:
          doc?.mapping === "object" ? 1 : doc?.mapping === "cylinder" ? 2 : 0,
      },
    },
  };
}
export function patternGLSL() {
  return `
#ifdef ALLOY_HIDE_SOURCE
uniform sampler2D uHideSource;
uniform float uHidePatch,uHideVariation;
float hideAtlas(vec2 q){
 vec2 origin=vec2(mod(uHidePatch,2.),floor(uHidePatch/2.))*.5;
 // Periodic gutters plus explicit gradients prevent atlas-edge bleed and fract-derivative seams.
 return textureGrad(uHideSource,origin+vec2(2./2056.)+fract(q)*(1024./2056.),dFdx(q)*(1024./2056.),dFdy(q)*(1024./2056.)).r;
}
float vectorHide(vec2 p){
 p+=vec2(uSurfaceSeed*.173,uSurfaceSeed*.317);
 // One coherent height field, warped continuously across all repeat boundaries.
 // Blending offset copies would ghost the furrows, especially on belly scales.
 vec2 warp=vec2(noise3(vec3(p*.43,7.)),noise3(vec3(p*.43,29.)))-.5;
 warp+=.13*(vec2(noise3(vec3(p*1.7,41.)),noise3(vec3(p*1.7,67.)))-.5);
 return hideAtlas(p+warp*uHideVariation*1.1);
}
#endif
#ifdef ALLOY_PATTERN
uniform sampler2D uPatternColor,uPatternParams,uPatternFinish,uPatternNormals,uPatternChannelMask;
uniform float uPatternRepeats,uPatternAngle;
uniform int uPatternObject;
uniform vec2 uPatternAspect;
uniform vec3 uPatternBeads;
vec4 patternColorSample,patternParamsSample,patternNormalSample,patternChannelMaskSample;
vec3 patternFinishSample;
float patternCoverage=0.,patternMaterialCoverage=0.;
vec3 patternLinear(vec3 c){return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));}
#endif
`;
}
export function patternColorGLSL() {
  return `
#ifdef ALLOY_PATTERN
 vec2 patternUV=uPatternObject==1?surfaceUV(pp,weights):uPatternObject==2?vec2(atan(pp.z,pp.x)/6.2831853+.5,pp.y*.33+.5):vProcUv;
 float pc=cos(uPatternAngle),ps=sin(uPatternAngle);
 patternUV=mat2(pc,-ps,ps,pc)*(patternUV-.5)*uPatternRepeats/uPatternAspect+.5;
 patternUV.y=1.-patternUV.y;
 patternColorSample=texture2D(uPatternColor,patternUV);
 patternParamsSample=texture2D(uPatternParams,patternUV);
 patternNormalSample=texture2D(uPatternNormals,patternUV);
 patternChannelMaskSample=texture2D(uPatternChannelMask,patternUV);
 patternNormalSample.rgb/=max(patternNormalSample.a,.00001);
 patternCoverage=patternColorSample.a;
 patternColorSample.rgb/=max(patternCoverage,.00001);

 vec4 finishSample=texture2D(uPatternFinish,patternUV);
 patternMaterialCoverage=finishSample.a;
 patternFinishSample=finishSample.rgb/max(finishSample.a,.00001);
 if(uPatternBeads.z>0.){
   vec2 beadGrid=patternUV*uPatternBeads.xy;
   beadGrid.x-=mod(floor(beadGrid.y),2.)*.5;
   vec2 beadLocal=fract(beadGrid)-.5;
   float beadRadius=length(beadLocal/vec2(.44,.46));
   float beadRound=sqrt(max(0.,1.-beadRadius*beadRadius));
   float beadHole=1.-smoothstep(.07,.14,length(beadLocal/vec2(1.,.65)));
   float beadEdge=1.-smoothstep(.9,1.,beadRadius);
   float beadFilter=1.-smoothstep(.6,1.5,length(fwidth(beadGrid)));
   surfaceHeight+=uPatternBeads.z*(beadRound*.7-beadHole*.42)*beadFilter;
   patternParamsSample.r=mix(.94,patternParamsSample.r,beadEdge*(1.-beadHole*.8));
   patternFinishSample.r*=beadEdge;
 }
 float isWool=patternFinishSample.g;
 float isCotton=patternFinishSample.b;
 vec2 thread=patternUV*420.;
 float weave=(sin(thread.x*6.283)*sin(thread.y*6.283))*.5+.5;
 float filtered=1.-smoothstep(.4,1.5,length(fwidth(thread)));
 float pile=noise3(vec3(patternUV*vec2(190.,250.),31.));
 float pileFilter=1.-smoothstep(.4,1.5,length(fwidth(patternUV*240.)));
 diffuseColor.rgb=mix(diffuseColor.rgb,patternLinear(patternColorSample.rgb)*(1.-isWool*.12+isWool*pile*.2),patternCoverage);
 surfaceHeight+=patternChannelMaskSample.b*((patternParamsSample.b*255.-128.)/2500.+isWool*(pile-.5)*.003*pileFilter+isCotton*(weave-.5)*.00035*filtered);
#endif
`;
}
