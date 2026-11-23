import * as THREE from "three";
import { linearColour, GRADIENT_POINT_LIMIT } from "./pointGradient.js";
export const GRADIENT_MASK_LIMIT = 9;
const FIELDS = GRADIENT_MASK_LIMIT + 1;
export const POINT_GRADIENT_GLSL = `
uniform sampler2D uPointGradientData;
uniform int uPointGradientCounts[10],uPointGradientEnabled,uPointGradientFill,uPointGradientMaskCount;
uniform vec3 uPointGradientBackground[10],uPointGradientCentre,uPointGradientValue;
uniform vec4 uPointGradientMaskInfo[10];
uniform float uPointGradientRadius,uPointGradientOpacity;
vec3 samplePointGradient(vec3 p,int field){
 vec3 colour=vec3(0.);float total=0.;
 for(int i=0;i<12;i++){if(i<uPointGradientCounts[field]){
  vec4 place=texture2D(uPointGradientData,vec2((float(i)+.5)/12.,(float(field*2)+.5)/20.));
  vec4 ink=texture2D(uPointGradientData,vec2((float(i)+.5)/12.,(float(field*2+1)+.5)/20.));
  if(place.w>0. && ink.w>0.){vec3 d=p-place.xyz;float weight=ink.w*exp(-dot(d,d)/(place.w*place.w));colour+=ink.rgb*weight;total+=weight;}
 }}return total>1e-8?colour/total:uPointGradientBackground[field];
}
`;
export function createPointGradientUniforms() {
  const data = new Float32Array(12 * 20 * 4),
    texture = new THREE.DataTexture(
      data,
      12,
      20,
      THREE.RGBAFormat,
      THREE.FloatType,
    );
  texture.minFilter = texture.magFilter = THREE.NearestFilter;
  texture.generateMipmaps = false;
  texture.needsUpdate = true;
  return {
    uPointGradientData: { value: texture },
    uPointGradientCounts: { value: new Int32Array(FIELDS) },
    uPointGradientEnabled: { value: 0 },
    uPointGradientFill: { value: 0 },
    uPointGradientMaskCount: { value: 0 },
    uPointGradientBackground: {
      value: Array.from({ length: FIELDS }, () => new THREE.Vector3()),
    },
    uPointGradientMaskInfo: {
      value: Array.from({ length: FIELDS }, () => new THREE.Vector4()),
    },
    uPointGradientCentre: { value: new THREE.Vector3() },
    uPointGradientRadius: { value: 1 },
    uPointGradientValue: { value: new THREE.Vector3() },
    uPointGradientOpacity: { value: 1 },
  };
}
export function installPointGradient(material, uniforms) {
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, uniforms);
    shader.fragmentShader = POINT_GRADIENT_GLSL + shader.fragmentShader;
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <roughnessmap_fragment>",
      `
 if(uPointGradientEnabled==1){vec3 pointCoordinate=(vProcPosition-uPointGradientCentre)/max(uPointGradientRadius,1e-6);vec3 fill=uPointGradientFill==1?samplePointGradient(pointCoordinate,0):uPointGradientValue;float coverage=uPointGradientOpacity;
 for(int field=1;field<10;field++){if(field<=uPointGradientMaskCount){vec4 info=uPointGradientMaskInfo[field];if(info.x>.5){vec3 maskRGB=samplePointGradient(pointCoordinate,field);float value=clamp(dot(maskRGB,vec3(.2126,.7152,.0722)),0.,1.);if(info.y>.5)value=1.-value;coverage*=mix(1.,value,info.z);}}}
 diffuseColor.rgb=mix(diffuseColor.rgb,fill,clamp(coverage,0.,1.));}
 #include <roughnessmap_fragment>`,
    );
  };
  material.customProgramCacheKey = () => cache() + "|object-point-gradient-v1";
}
export function updatePointGradientUniforms(uniforms, preview, geometry) {
  geometry.computeBoundingSphere();
  uniforms.uPointGradientCentre.value.copy(geometry.boundingSphere.center);
  uniforms.uPointGradientRadius.value = geometry.boundingSphere.radius;
  uniforms.uPointGradientEnabled.value = preview ? 1 : 0;
  uniforms.uPointGradientFill.value = preview?.fill ? 1 : 0;
  uniforms.uPointGradientOpacity.value = preview?.opacity ?? 1;
  uniforms.uPointGradientValue.value.fromArray(
    linearColour(preview?.value || "#b87333"),
  );
  const masks = (preview?.masks || []).slice(0, GRADIENT_MASK_LIMIT),
    fields = [
      preview?.fill,
      ...masks.map((m) => (m.kind === "gradient" ? m.gradient : null)),
    ];
  uniforms.uPointGradientMaskCount.value = masks.length;
  const data = uniforms.uPointGradientData.value.image.data;
  data.fill(0);
  uniforms.uPointGradientCounts.value.fill(0);
  for (let f = 0; f < FIELDS; f++) {
    const g = fields[f],
      mask = masks[f - 1];
    uniforms.uPointGradientBackground.value[f].fromArray(
      g
        ? linearColour(g.background)
        : [mask?.fill ?? 0, mask?.fill ?? 0, mask?.fill ?? 0],
    );
    uniforms.uPointGradientMaskInfo.value[f].set(
      mask?.enabled === false ? 0 : 1,
      mask?.inverted ? 1 : 0,
      (mask?.strength ?? 100) / 100,
      0,
    );
    if (!g) continue;
    uniforms.uPointGradientCounts.value[f] = g.points.length;
    g.points.forEach((p, i) => {
      data.set([...p.position, p.radius], (f * 2 * 12 + i) * 4);
      data.set(
        [...linearColour(p.color), p.weight],
        ((f * 2 + 1) * 12 + i) * 4,
      );
    });
  }
  uniforms.uPointGradientData.value.needsUpdate = true;
}
export function gradientObjectPosition(specimen, normalized) {
  const sphere = specimen.geometry.boundingSphere;
  return new THREE.Vector3()
    .fromArray(normalized)
    .multiplyScalar(sphere.radius)
    .add(sphere.center);
}
export function gradientSurfaceHit(engine, x, y) {
  const rect = engine.renderer.domElement.getBoundingClientRect(),
    ray = new THREE.Raycaster();
  ray.setFromCamera(
    new THREE.Vector2(
      ((x - rect.left) / rect.width) * 2 - 1,
      (-(y - rect.top) / rect.height) * 2 + 1,
    ),
    engine.camera,
  );
  const hit = ray.intersectObject(engine.specimen, false)[0];
  if (!hit) return null;
  const sphere = engine.specimen.geometry.boundingSphere,
    position = engine.specimen
      .worldToLocal(hit.point.clone())
      .sub(sphere.center)
      .divideScalar(sphere.radius);
  return position.toArray();
}

// A decal's coverage can inherit the same bounded object-space Fill/Gradient
// masks without changing its artwork colour or allocating a mesh UV atlas.
export function installPointGradientAlpha(material, uniforms) {
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  const named = Object.fromEntries(
    Object.entries(uniforms).map(([key, value]) => [
      key.replace("uPointGradient", "uDecalPointGradient"),
      value,
    ]),
  );
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, named);
    shader.fragmentShader =
      POINT_GRADIENT_GLSL.replaceAll(
        "uPointGradient",
        "uDecalPointGradient",
      ).replaceAll("samplePointGradient", "sampleDecalPointGradient") +
      shader.fragmentShader;
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <alphatest_fragment>",
      `
      vec3 decalPointCoordinate=(vProcPosition-uDecalPointGradientCentre)/max(uDecalPointGradientRadius,1e-6);
      for(int field=1;field<10;field++){if(field<=uDecalPointGradientMaskCount){vec4 info=uDecalPointGradientMaskInfo[field];if(info.x>.5){float value=clamp(dot(sampleDecalPointGradient(decalPointCoordinate,field),vec3(.2126,.7152,.0722)),0.,1.);if(info.y>.5)value=1.-value;diffuseColor.a*=mix(1.,value,info.z);}}}
      #include <alphatest_fragment>`,
    );
  };
  material.customProgramCacheKey = () =>
    cache() + "|decal-gradient-coverage-v1";
}
