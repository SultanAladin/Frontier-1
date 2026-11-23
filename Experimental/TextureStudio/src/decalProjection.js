import {
  installSurfaceCoordinates,
  texturePart,
} from "./textureSurfaceModel.js";
import {
  projectArtworkGeometry,
  uvDecalProjector,
  surfaceHitDetails,
} from "./textureUVGeometry.js";
import * as THREE from "three";
import { DecalGeometry } from "three/addons/geometries/DecalGeometry.js";
import { DECAL_LIMIT } from "./decalModel.js";
import { gradientObjectPosition } from "./pointGradientViewport.js";
export function decalQuaternion(decal) {
  return new THREE.Quaternion()
    .fromArray(decal.quaternion)
    .multiply(
      new THREE.Quaternion().setFromAxisAngle(
        new THREE.Vector3(0, 0, 1),
        (decal.roll * Math.PI) / 180,
      ),
    );
}
export function decalLocalProjector(specimen, decal) {
  specimen.geometry.computeBoundingSphere();
  const radius = specimen.geometry.boundingSphere.radius;
  return new THREE.Matrix4().compose(
    gradientObjectPosition(specimen, decal.position),
    decalQuaternion(decal),
    new THREE.Vector3(
      decal.size[0] * radius,
      decal.size[1] * radius,
      decal.depth * radius,
    ),
  );
}
export function projectDecalGeometry(specimen, decal) {
  if (decal.space === "uv" || specimen.geometry.attributes.uvAuthoring)
    return projectArtworkGeometry(
      specimen.geometry,
      decal,
      decal.space === "uv" ? null : decalLocalProjector(specimen, decal),
    );
  specimen.updateWorldMatrix(true, false);
  specimen.geometry.computeBoundingSphere();
  const local = decalLocalProjector(specimen, decal),
    world = specimen.matrixWorld.clone().multiply(local);
  const position = new THREE.Vector3(),
    quaternion = new THREE.Quaternion(),
    size = new THREE.Vector3();
  world.decompose(position, quaternion, size);
  const geometry = new DecalGeometry(
    specimen,
    position,
    new THREE.Euler().setFromQuaternion(quaternion),
    size,
  );
  // DecalGeometry returns world coordinates. Store the projected triangles in
  // the specimen's local frame so orbiting/transforms and object-space fields
  // stay coherent, without assuming unique UVs on the stock teapot.
  geometry.applyMatrix4(specimen.matrixWorld.clone().invert());
  const normal = new THREE.Vector3(0, 0, 1).applyQuaternion(
    decalQuaternion(decal),
  );
  const positions = geometry.attributes.position,
    normals = geometry.attributes.normal,
    uvs = geometry.attributes.uv;
  const p = [],
    n = [],
    uv = [];
  for (let i = 0; i < positions.count; i += 3) {
    const facing = new THREE.Vector3()
      .fromBufferAttribute(normals, i)
      .add(new THREE.Vector3().fromBufferAttribute(normals, i + 1))
      .add(new THREE.Vector3().fromBufferAttribute(normals, i + 2))
      .normalize();
    if (facing.dot(normal) <= 0.05) continue; // Never stamp an opposite-facing/back surface.
    for (let j = i; j < i + 3; j++) {
      p.push(positions.getX(j), positions.getY(j), positions.getZ(j));
      n.push(normals.getX(j), normals.getY(j), normals.getZ(j));
      uv.push(uvs.getX(j), uvs.getY(j));
    }
  }
  geometry.dispose();
  const filtered = new THREE.BufferGeometry();
  filtered.setAttribute("position", new THREE.Float32BufferAttribute(p, 3));
  filtered.setAttribute("normal", new THREE.Float32BufferAttribute(n, 3));
  filtered.setAttribute("uv", new THREE.Float32BufferAttribute(uv, 2));
  filtered.computeBoundingSphere();
  return filtered;
}
export function decalSurfaceHit(engine, x, y) {
  const rect = engine.renderer.domElement.getBoundingClientRect(),
    ray = new THREE.Raycaster();
  engine.specimen.updateWorldMatrix(true, false);
  engine.camera.updateWorldMatrix(true, false);
  engine.specimen.geometry.computeBoundingSphere();
  ray.setFromCamera(
    new THREE.Vector2(
      ((x - rect.left) / rect.width) * 2 - 1,
      (-(y - rect.top) / rect.height) * 2 + 1,
    ),
    engine.camera,
  );
  const hit = ray.intersectObject(engine.specimen, false)[0];
  if (!hit || !hit.face) return null;
  const sphere = engine.specimen.geometry.boundingSphere;
  const position = engine.specimen
    .worldToLocal(hit.point.clone())
    .sub(sphere.center)
    .divideScalar(sphere.radius);
  // Normals and quaternions are object-local, not camera-facing screen rotations.
  const normal = hit.normal?.clone() || hit.face.normal.clone();
  if (
    normal.dot(
      ray.ray.direction
        .clone()
        .transformDirection(engine.specimen.matrixWorld.clone().invert()),
    ) > 0
  )
    normal.negate();
  const quaternion = new THREE.Quaternion().setFromUnitVectors(
    new THREE.Vector3(0, 0, 1),
    normal.normalize(),
  );
  return {
    position: position.toArray(),
    quaternion: quaternion.toArray(),
    ...surfaceHitDetails(engine.specimen, hit),
  };
}
export function createSurfaceMaskUniforms() {
  const data = new Float32Array(7 * DECAL_LIMIT * 4);
  const texture = new THREE.DataTexture(
    data,
    7,
    DECAL_LIMIT,
    THREE.RGBAFormat,
    THREE.FloatType,
  );
  texture.minFilter = texture.magFilter = THREE.NearestFilter;
  texture.generateMipmaps = false;
  texture.needsUpdate = true;
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = 1024;
  const atlas = new THREE.CanvasTexture(canvas);
  atlas.colorSpace = THREE.NoColorSpace;
  atlas.generateMipmaps = false;
  atlas.minFilter = atlas.magFilter = THREE.LinearFilter;
  return {
    uSurfaceStampData: { value: texture },
    uSurfaceStampAtlas: { value: atlas },
    uSurfaceStampCount: { value: 0 },
  };
}
export function updateSurfaceMaskUniforms(
  uniforms,
  records,
  specimen,
  stampOrder,
) {
  const data = uniforms.uSurfaceStampData.value.image.data;
  data.fill(0);
  const masks = records.filter((r) => r.decal.asMask).slice(0, DECAL_LIMIT);
  for (let i = 0; i < masks.length; i++) {
    const { decal, layerIndex, stamp } = masks[i],
      offset = i * 7 * 4;
    data.set(
      (decal.space === "uv"
        ? uvDecalProjector(decal)
        : decalLocalProjector(specimen, decal)
      ).invert().elements,
      offset,
    );
    const index = stampOrder.indexOf(stamp.id),
      row = Math.floor(index / 4),
      col = index % 4;
    data.set(
      [
        col / 4 + 1 / 1024,
        1 - (row + 1) / 4 + 1 / 1024,
        254 / 1024,
        254 / 1024,
      ],
      offset + 16,
    );
    data.set(
      [
        stamp.project.maskMode === "luminance" ? 1 : 0,
        decal.inverted ? 1 : 0,
        decal.opacity,
        (decal.space === "uv" ? -1 : 1) *
          ((texturePart(decal.target?.partId)?.index || 0) + 1),
      ],
      offset + 20,
    );
    data.set(
      [
        ...new THREE.Vector3(0, 0, 1)
          .applyQuaternion(decalQuaternion(decal))
          .toArray(),
        layerIndex + 1,
      ],
      offset + 24,
    );
  }
  uniforms.uSurfaceStampCount.value = masks.length;
  uniforms.uSurfaceStampData.value.needsUpdate = true;
}
export const SURFACE_MASK_GLSL = `
uniform sampler2D uSurfaceStampData;
uniform sampler2D uSurfaceStampAtlas;
uniform int uSurfaceStampCount;
uniform int uSurfaceStampOwnerCount;
uniform int uSurfaceStampOwners[9];
vec4 surfaceStampRow(int i,float column){return texture2D(uSurfaceStampData,vec2((column+.5)/7.,(float(i)+.5)/64.));}
float surfaceStampCoverage(vec3 position,vec3 normal){
 float coverage=1.;
 for(int owner=0;owner<9;owner++){
  if(owner>=uSurfaceStampOwnerCount)break;
  bool hasMask=false;float unionCoverage=0.;
  for(int i=0;i<64;i++){
   if(i>=uSurfaceStampCount)break;
   vec4 facing=surfaceStampRow(i,6.);
   if(int(facing.w+.1)!=uSurfaceStampOwners[owner])continue;
   hasMask=true;vec4 info=surfaceStampRow(i,5.);
   mat4 projector=mat4(surfaceStampRow(i,0.),surfaceStampRow(i,1.),surfaceStampRow(i,2.),surfaceStampRow(i,3.));
   bool uvSpace=info.w<-.5;float targetPart=abs(info.w)-1.;
   bool inPart=targetPart<.5||abs(vSurfacePart-targetPart)<.25;
   vec3 p=(projector*vec4(uvSpace?vec3(vAuthoringUv,0.):position,1.)).xyz;float value=0.;
   if(inPart&&all(lessThanEqual(abs(p),vec3(.5)))&&(uvSpace||dot(normalize(normal),facing.xyz)>.05)){
    vec4 rect=surfaceStampRow(i,4.);vec4 ink=texture2D(uSurfaceStampAtlas,rect.xy+(p.xy+.5)*rect.zw);
    value=ink.a;if(info.x>.5)value*=dot(ink.rgb,vec3(.2126,.7152,.0722));
   }
   if(info.y>.5&&inPart)value=1.-value;
   unionCoverage=max(unionCoverage,value*info.z);
  }
  if(hasMask)coverage*=unionCoverage;
 }return clamp(coverage,0.,1.);
}
`;
export function surfaceMaskOwners(doc, layerId) {
  const owners = new Int32Array(9);
  let count = 0;
  let layer = doc?.layers.find((l) => l.id === layerId);
  while (layer && count < 9) {
    owners[count++] = doc.layers.indexOf(layer) + 1;
    layer = doc.layers.find((l) => l.id === layer.parentId);
  }
  return {
    uSurfaceStampOwners: { value: owners },
    uSurfaceStampOwnerCount: { value: count },
  };
}
export function installSelectedStampMask(material, uniforms, ownerUniforms) {
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, uniforms, ownerUniforms);
    shader.fragmentShader = SURFACE_MASK_GLSL + shader.fragmentShader;
    shader.fragmentShader = shader.fragmentShader.replace(
      "float coverage=uPointGradientOpacity;",
      "float coverage=uPointGradientOpacity*surfaceStampCoverage(vProcPosition,vProcNormal);",
    );
  };
  material.customProgramCacheKey = () => cache() + "|surface-stamp-mask-v2";
  installSurfaceCoordinates(material);
}

export function installPartStampCoverage(material, uniforms, owners) {
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, uniforms, owners);
    shader.fragmentShader = SURFACE_MASK_GLSL + shader.fragmentShader;
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <alphatest_fragment>",
      "diffuseColor.a*=surfaceStampCoverage(vProcPosition,vProcNormal);\n#include <alphatest_fragment>",
    );
  };
  material.customProgramCacheKey = () => cache() + "|part-stamp-coverage-v2";
  installSurfaceCoordinates(material);
}
