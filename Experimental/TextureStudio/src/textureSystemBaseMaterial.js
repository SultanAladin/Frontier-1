import * as THREE from "three";
// Do not normalize this through an authored recipe: metal recipes can force
// metalness=1 even if callers pass zero. Layer 0 is genuinely neutral white.
export function createTextureSystemBaseMaterial() {
  const material = new THREE.MeshPhysicalMaterial({
    color: "#ffffff",
    metalness: 0,
    roughness: 0.75,
    clearcoat: 0,
    side: THREE.DoubleSide,
  });
  material.onBeforeCompile = (shader) => {
    shader.vertexShader =
      "varying vec3 vProcPosition,vProcNormal;\n" + shader.vertexShader;
    shader.vertexShader = shader.vertexShader.replace(
      "#include <begin_vertex>",
      "#include <begin_vertex>\nvProcPosition=position;vProcNormal=normal;",
    );
    shader.fragmentShader =
      "varying vec3 vProcPosition,vProcNormal;\n" + shader.fragmentShader;
  };
  material.customProgramCacheKey = () => "system-layer-0-white-v1";
  return material;
}
