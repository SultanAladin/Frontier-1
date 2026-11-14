/**
 * GLSL 300 es Shaders for the 3D Voxelized Eulerian Pyro Gas Simulation
 * and Physically-Based Volumetric Raymarching (Niagara Fluids-inspired).
 * Supports dynamic world bounds and low-end GPU 16³–128³ grids.
 */

export const FULLSCREEN_VERT = `#version 300 es
precision highp float;

out vec2 vUV;

void main() {
  vec2 pos = vec2(
    float((gl_VertexID & 1) << 2) - 1.0,
    float((gl_VertexID & 2) << 1) - 1.0
  );
  vUV = pos * 0.5 + 0.5;
  gl_Position = vec4(pos, 0.0, 1.0);
}
`;

export const COMMON_VOLUME_HEADER = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

uniform int uGridRes;
uniform int uTilesX;
uniform int uTilesY;
uniform vec2 uAtlasSize;

// Convert 2D atlas pixel coordinate to 3D voxel coordinate [0, uGridRes-1]^3
ivec3 fragToVoxel(ivec2 fragCoord) {
  int res = uGridRes;
  int tileX = fragCoord.x / res;
  int tileY = fragCoord.y / res;
  int vx = fragCoord.x - tileX * res;
  int vy = fragCoord.y - tileY * res;
  int vz = tileY * uTilesX + tileX;
  return ivec3(vx, vy, vz);
}

// Convert 3D voxel coordinate to 2D atlas pixel coordinate
ivec2 voxelToFrag(ivec3 v) {
  int res = uGridRes;
  ivec3 c = clamp(v, ivec3(0), ivec3(res - 1));
  int tileY = c.z / uTilesX;
  int tileX = c.z - tileY * uTilesX;
  return ivec2(tileX * res + c.x, tileY * res + c.y);
}

// Exact integer voxel fetch
vec4 fetchVoxel(sampler2D tex, ivec3 v) {
  return texelFetch(tex, voxelToFrag(v), 0);
}

// Hardware-accelerated 3D trilinear interpolation across the 2D tiled volume atlas
vec4 sampleVolumeTrilinear(sampler2D tex, vec3 uvw) {
  float res = float(uGridRes);
  vec3 c = clamp(uvw, vec3(0.0), vec3(1.0));
  vec2 xy = clamp(c.xy * res, 0.5, res - 0.5);

  float zScaled = clamp(c.z * res - 0.5, 0.0, res - 1.0001);
  int z0 = int(floor(zScaled));
  int z1 = min(z0 + 1, uGridRes - 1);
  float fz = zScaled - float(z0);

  int ty0 = z0 / uTilesX;
  int tx0 = z0 - ty0 * uTilesX;
  int ty1 = z1 / uTilesX;
  int tx1 = z1 - ty1 * uTilesX;

  vec2 uv0 = (vec2(float(tx0) * res, float(ty0) * res) + xy) / uAtlasSize;
  vec2 uv1 = (vec2(float(tx1) * res, float(ty1) * res) + xy) / uAtlasSize;

  vec4 s0 = textureLod(tex, uv0, 0.0);
  vec4 s1 = textureLod(tex, uv1, 0.0);
  return mix(s0, s1, fz);
}

// Sample with adjustable voxel quantization (0 = smooth trilinear, 1 = discrete voxel center)
vec4 sampleVolumeQuantized(sampler2D tex, vec3 uvw, float quantize) {
  if (quantize <= 0.001) {
    return sampleVolumeTrilinear(tex, uvw);
  }
  float res = float(uGridRes);
  vec3 voxelCoord = clamp(uvw, 0.0, 0.9999) * res;
  if (quantize >= 0.999) {
    return fetchVoxel(tex, ivec3(floor(voxelCoord)));
  }
  vec3 fracPart = fract(voxelCoord);
  float sharpness = mix(1.0, 14.0, quantize);
  vec3 terraced = floor(voxelCoord) + clamp((fracPart - 0.5) * sharpness + 0.5, 0.0, 1.0);
  vec3 blendedUVW = mix(uvw, terraced / res, quantize);
  return mix(
    sampleVolumeTrilinear(tex, blendedUVW),
    fetchVoxel(tex, ivec3(floor(voxelCoord))),
    quantize * quantize
  );
}

vec3 hash33(vec3 p3) {
  p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yxz + 33.33);
  return fract((p3.xxy + p3.yxx) * p3.zyx) * 2.0 - 1.0;
}

float valueNoise3D(vec3 p) {
  vec3 i = floor(p);
  vec3 f = fract(p);
  vec3 u = f * f * (3.0 - 2.0 * f);

  float n000 = dot(hash33(i + vec3(0,0,0)), f - vec3(0,0,0));
  float n100 = dot(hash33(i + vec3(1,0,0)), f - vec3(1,0,0));
  float n010 = dot(hash33(i + vec3(0,1,0)), f - vec3(0,1,0));
  float n110 = dot(hash33(i + vec3(1,1,0)), f - vec3(1,1,0));
  float n001 = dot(hash33(i + vec3(0,0,1)), f - vec3(0,0,1));
  float n101 = dot(hash33(i + vec3(1,0,1)), f - vec3(1,0,1));
  float n011 = dot(hash33(i + vec3(0,1,1)), f - vec3(0,1,1));
  float n111 = dot(hash33(i + vec3(1,1,1)), f - vec3(1,1,1));

  return mix(
    mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
    mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y),
    u.z
  );
}

vec3 curlNoise3D(vec3 p) {
  float eps = 0.25;
  float n1 = valueNoise3D(p + vec3(0.0, eps, 0.0)) - valueNoise3D(p - vec3(0.0, eps, 0.0));
  float n2 = valueNoise3D(p + vec3(0.0, 0.0, eps)) - valueNoise3D(p - vec3(0.0, 0.0, eps));
  float n3 = valueNoise3D(p + vec3(eps, 0.0, 0.0)) - valueNoise3D(p - vec3(eps, 0.0, 0.0));
  return vec3(n1 - n2, n2 - n3, n3 - n1) / (2.0 * eps);
}
`;

/**
 * PASS 1: Emitter + Detonation Splat + Interactive 3D Flamethrower Brush + Obstacle Voxelization
 */
export const EMITTER_SPLAT_FRAG = `${COMMON_VOLUME_HEADER}
layout(location = 0) out vec4 outVelocity;
layout(location = 1) out vec4 outThermo;

uniform sampler2D uVelocityTex;
uniform sampler2D uThermoTex;
uniform float uDt;
uniform float uTime;
uniform vec3 uDomainScale; // Relative world bounds scale (so bigger bounds give explosions more room!)

// Continuous emitter
uniform int uEmitterEnabled;
uniform float uEmitterRate;
uniform float uEmitterRadius;
uniform float uEmitterHeight;
uniform float uEmitterUpwardVel;
uniform float uEmitterSwirl;
uniform float uEmitterTemp;
uniform float uEmitterFuel;
uniform float uEmitterSmoke;

// Instant detonation / blast wave trigger
uniform int uBlastActive;
uniform vec3 uBlastCenter;
uniform float uBlastRadius;
uniform float uBlastStrength;
uniform float uBlastTemp;
uniform float uBlastFuel;
uniform float uBlastSmoke;
uniform float uBlastLobes;
uniform float uBlastSeed;
uniform vec3 uBlastDirectionalVel;

// Interactive 3D Flamethrower / Pyro Brush
uniform int uBrushActive;
uniform vec3 uBrushPos;
uniform vec3 uBrushVel;
uniform float uBrushRadius;

// 3D Ballistic Shrapnel / Burning Debris Streamers (up to 6 flying fragments)
uniform int uProjectilesActive;
uniform vec4 uProjectiles[6];    // xyz = UVW position, w = intensity/heat (0 = inactive)
uniform vec4 uProjectileVels[6]; // xyz = velocity vector, w = radius

// Voxelized Solid Obstacle
uniform int uObstacleType;
uniform vec3 uObstaclePos;
uniform float uObstacleRadius;

float computeObstacleMask(vec3 uvw) {
  if (uObstacleType == 0) return 0.0;
  vec3 d = (uvw - uObstaclePos) * uDomainScale;
  float rad = uObstacleRadius;
  if (uObstacleType == 1) {
    return length(d) < rad ? 1.0 : 0.0;
  } else if (uObstacleType == 2) {
    return (length(d.xz) < rad * 0.75 && uvw.y < uObstaclePos.y + (rad * 1.8) / uDomainScale.y) ? 1.0 : 0.0;
  } else if (uObstacleType == 3) {
    return (length(d.yz) < rad * 0.72 && abs(d.x) < rad * 1.9) ? 1.0 : 0.0;
  } else if (uObstacleType == 4) {
    vec3 b = vec3(rad * 1.5, rad * 0.45, rad * 1.5);
    vec3 q = abs(d) - b;
    return max(max(q.x, q.y), q.z) < 0.0 ? 1.0 : 0.0;
  } else if (uObstacleType == 5) {
    return length(vec2(length(d.xy) - rad * 0.76, d.z)) < rad * 0.24 ? 1.0 : 0.0;
  }
  return 0.0;
}

void main() {
  ivec2 fc = ivec2(gl_FragCoord.xy);
  ivec3 v = fragToVoxel(fc);
  if (v.z >= uGridRes) {
    outVelocity = vec4(0.0);
    outThermo = vec4(0.0);
    return;
  }

  vec3 uvw = (vec3(v) + 0.5) / float(uGridRes);
  vec4 vel = texelFetch(uVelocityTex, fc, 0);
  vec4 thermo = texelFetch(uThermoTex, fc, 0);

  float isSolid = computeObstacleMask(uvw);
  if (isSolid > 0.5) {
    outVelocity = vec4(0.0, 0.0, 0.0, 1.0);
    outThermo = vec4(0.0, 0.0, 0.0, 0.0);
    return;
  }
  vel.a = 0.0;

  // Scale factor so larger world bounds give plumes & explosions more physical space inside [0,1]^3
  vec3 metricScale = max(vec3(0.75), pow(uDomainScale, vec3(0.55)));

  // 1. Continuous Fire/Smoke Plume Emitter at bottom center
  if (uEmitterEnabled == 1 && uEmitterRate > 0.001) {
    vec3 emitPos = vec3(0.5, uEmitterHeight / metricScale.y, 0.5);
    vec3 diff = (uvw - emitPos) * metricScale;
    float dist = length(vec3(diff.x, diff.y * 1.45, diff.z));
    if (dist < uEmitterRadius * 1.35) {
      float falloff = smoothstep(uEmitterRadius * 1.35, uEmitterRadius * 0.15, dist);
      vec3 np = uvw * uDomainScale * 8.5 + vec3(0.0, -uTime * 5.5, uTime * 1.7);
      float n = 0.55 + 0.45 * valueNoise3D(np);
      float rate = falloff * uEmitterRate * n;

      thermo.r = max(thermo.r, uEmitterSmoke * falloff * (0.6 + 0.4 * n));
      thermo.g = max(thermo.g, uEmitterTemp * rate);
      thermo.b = max(thermo.b, uEmitterFuel * rate);
      thermo.a = max(thermo.a, uEmitterFuel * rate * 0.9);

      vec3 tangent = vec3(-diff.z, 0.0, diff.x) / max(length(diff.xz), 0.02);
      vec3 turb = hash33(np) * 0.65;
      vec3 targetVel = vec3(0.0, uEmitterUpwardVel * (0.75 + 0.5 * n), 0.0)
                     + tangent * uEmitterSwirl * (dist / max(uEmitterRadius, 0.01))
                     + turb;
      vel.xyz = mix(vel.xyz, targetVel, clamp(falloff * uDt * 14.0, 0.0, 0.85));
    }
  }

  // 2. Interactive 3D Flamethrower / Pyro Brush Injection
  if (uBrushActive == 1) {
    vec3 diff = (uvw - uBrushPos) * metricScale;
    float r = length(diff);
    if (r < uBrushRadius) {
      float falloff = smoothstep(uBrushRadius, uBrushRadius * 0.15, r);
      vec3 np = uvw * uDomainScale * 10.0 + vec3(uTime * 4.0);
      float n = 0.6 + 0.4 * valueNoise3D(np);

      thermo.r = max(thermo.r, 2.2 * falloff * n);
      thermo.g = max(thermo.g, 4.8 * falloff * n);
      thermo.b = max(thermo.b, 3.8 * falloff * n);
      thermo.a = max(thermo.a, 4.2 * falloff);

      vec3 curlP = curlNoise3D(np) * 1.5;
      vel.xyz += (uBrushVel + vec3(0.0, 2.5, 0.0) + curlP) * falloff * 0.65;
    }
  }

  // 3. Instantaneous Explosion / Detonation Splat (Multi-Lobe Fireball + Shockwave)
  if (uBlastActive == 1) {
    vec3 diff = (uvw - uBlastCenter) * metricScale;
    float r = length(diff);
    vec3 dir = r > 1e-4 ? diff / r : vec3(0.0, 1.0, 0.0);

    vec3 lobeCoord = dir * uBlastLobes + vec3(uBlastSeed * 7.13, uBlastSeed * 3.71, uBlastSeed * 5.39);
    float lobeNoise = valueNoise3D(lobeCoord) * 0.35 + valueNoise3D(lobeCoord * 2.1) * 0.18;
    float effectiveRadius = uBlastRadius * (1.0 + lobeNoise);

    if (r < effectiveRadius * 1.25) {
      float coreMask = smoothstep(effectiveRadius, effectiveRadius * 0.1, r);
      float shellMask = smoothstep(effectiveRadius * 1.2, effectiveRadius * 0.45, r) *
                        smoothstep(0.0, effectiveRadius * 0.5, r);

      thermo.r += uBlastSmoke * (shellMask * 0.9 + coreMask * 0.45);
      thermo.g = max(thermo.g, uBlastTemp * coreMask * (1.0 + 0.3 * lobeNoise));
      thermo.b = max(thermo.b, uBlastFuel * coreMask);
      thermo.a = max(thermo.a, uBlastTemp * coreMask);

      vec3 curlPerturb = curlNoise3D(uvw * uDomainScale * 7.0 + uBlastSeed * 11.0) * 0.45;
      vec3 blastVel = (dir + vec3(0.0, 0.38, 0.0) + curlPerturb) * uBlastStrength *
                      smoothstep(effectiveRadius * 1.25, effectiveRadius * 0.12, r)
                    + uBlastDirectionalVel * coreMask;
      vel.xyz += blastVel;
    }

    // Ground dust / shockwave ring when detonation is near the floor
    float ringDistXZ = length(diff.xz);
    float ringHeight = abs(uvw.y - max(0.04, uBlastCenter.y * 0.45)) * metricScale.y;
    if (ringDistXZ < effectiveRadius * 1.85 && ringHeight < 0.075) {
      float ringFalloff = smoothstep(effectiveRadius * 1.85, effectiveRadius * 0.4, ringDistXZ)
                        * smoothstep(0.075, 0.01, ringHeight);
      vec2 outXZ = ringDistXZ > 1e-4 ? diff.xz / ringDistXZ : vec2(1.0, 0.0);
      vel.xz += outXZ * uBlastStrength * 0.95 * ringFalloff;
      thermo.r += uBlastSmoke * 0.65 * ringFalloff;
    }
  }

  // 4. Ballistic Shrapnel / Burning Debris Streamers (Arcing Smoke & Fire Fingers)
  if (uProjectilesActive == 1) {
    for (int k = 0; k < 6; k++) {
      vec4 proj = uProjectiles[k];
      if (proj.w <= 0.01) continue;
      vec3 pVel = uProjectileVels[k].xyz;
      float pRad = uProjectileVels[k].w;

      vec3 pDiff = (uvw - proj.xyz) * metricScale;
      float pr = length(pDiff);
      if (pr < pRad) {
        float pFalloff = smoothstep(pRad, pRad * 0.1, pr) * proj.w;
        thermo.r += 2.6 * pFalloff;
        thermo.g = max(thermo.g, 5.8 * pFalloff);
        thermo.b = max(thermo.b, 3.6 * pFalloff);
        thermo.a = max(thermo.a, 4.5 * pFalloff);
        vel.xyz += pVel * pFalloff * 0.55;
      }
    }
  }

  outVelocity = vel;
  outThermo = thermo;
}
`;


/**
 * PASS 2: Semi-Lagrangian + Sharpened MacCormack Advection + Pyro Combustion Chemistry
 */
export const ADVECTION_PYRO_FRAG = `${COMMON_VOLUME_HEADER}
layout(location = 0) out vec4 outVelocity;
layout(location = 1) out vec4 outThermo;

uniform sampler2D uVelocityTex;
uniform sampler2D uThermoTex;
uniform float uDt;
uniform float uTime;
uniform int uMacCormack;
uniform int uEnclosedBox;
uniform vec3 uDomainScale;

uniform float uBurnRate;
uniform float uBurnHeat;
uniform float uSootGen;
uniform float uCoolingRate;
uniform float uSmokeDissipation;
uniform float uVelocityDamping;

void main() {
  ivec2 fc = ivec2(gl_FragCoord.xy);
  ivec3 v = fragToVoxel(fc);
  if (v.z >= uGridRes) {
    outVelocity = vec4(0.0);
    outThermo = vec4(0.0);
    return;
  }

  vec4 curVel = texelFetch(uVelocityTex, fc, 0);
  if (curVel.a > 0.5) {
    outVelocity = vec4(0.0, 0.0, 0.0, 1.0);
    outThermo = vec4(0.0);
    return;
  }

  float res = float(uGridRes);
  vec3 uvw = (vec3(v) + 0.5) / res;

  // Scale advection step inversely with world domain size so larger bounds give physical travel room
  vec3 velScale = (uDt * 0.34) / max(vec3(0.75), pow(uDomainScale, vec3(0.65)));
  vec3 backUVW = uvw - curVel.xyz * velScale;

  vec4 advVel = sampleVolumeTrilinear(uVelocityTex, backUVW);
  vec4 advThermo = sampleVolumeTrilinear(uThermoTex, backUVW);

  if (uMacCormack == 1) {
    vec3 fwdUVW = backUVW + advVel.xyz * velScale;
    vec4 backFwdThermo = sampleVolumeTrilinear(uThermoTex, fwdUVW);
    vec4 curThermo = texelFetch(uThermoTex, fc, 0);
    vec4 corrThermo = advThermo + 0.45 * (curThermo - backFwdThermo);

    vec4 nL = fetchVoxel(uThermoTex, v + ivec3(-1, 0, 0));
    vec4 nR = fetchVoxel(uThermoTex, v + ivec3( 1, 0, 0));
    vec4 nD = fetchVoxel(uThermoTex, v + ivec3( 0,-1, 0));
    vec4 nU = fetchVoxel(uThermoTex, v + ivec3( 0, 1, 0));
    vec4 nB = fetchVoxel(uThermoTex, v + ivec3( 0, 0,-1));
    vec4 nF = fetchVoxel(uThermoTex, v + ivec3( 0, 0, 1));

    vec4 minVal = min(curThermo, min(min(nL, nR), min(min(nD, nU), min(nB, nF))));
    vec4 maxVal = max(curThermo, max(max(nL, nR), max(max(nD, nU), max(nB, nF))));
    advThermo = clamp(corrThermo, minVal, maxVal);
  }

  float smoke = max(0.0, advThermo.r);
  float temp  = max(0.0, advThermo.g);
  float fuel  = max(0.0, advThermo.b);

  float ignitionFactor = smoothstep(0.08, 0.65, temp + fuel * 0.5);
  float burnedFuel = min(fuel, fuel * uBurnRate * (0.65 + 0.65 * ignitionFactor) * uDt);
  fuel = max(0.0, fuel - burnedFuel);

  temp += burnedFuel * uBurnHeat;
  smoke += burnedFuel * uSootGen;

  float radiativeCool = uCoolingRate * (0.55 * temp + 0.14 * temp * temp) * uDt;
  temp = max(0.0, temp - radiativeCool);

  smoke = max(0.0, smoke * exp(-uSmokeDissipation * uDt));
  fuel  = max(0.0, fuel  * exp(-0.15 * uDt));

  float reaction = burnedFuel / max(uDt, 1e-4);

  if (uEnclosedBox == 0) {
    float edgeDistX = min(uvw.x, 1.0 - uvw.x);
    float edgeDistZ = min(uvw.z, 1.0 - uvw.z);
    float topDist = 1.0 - uvw.y;
    float sponge = smoothstep(0.0, 0.045, min(edgeDistX, edgeDistZ)) * smoothstep(0.0, 0.04, topDist);
    smoke *= sponge;
    temp  *= sponge;
    fuel  *= sponge;
  }

  vec3 nextVel = advVel.xyz * exp(-uVelocityDamping * uDt);

  if (v.y <= 1 && nextVel.y < 0.0) {
    nextVel.y = 0.0;
  }
  if (uEnclosedBox == 1) {
    if ((v.x <= 1 && nextVel.x < 0.0) || (v.x >= uGridRes - 2 && nextVel.x > 0.0)) nextVel.x = 0.0;
    if ((v.y >= uGridRes - 2 && nextVel.y > 0.0)) nextVel.y = 0.0;
    if ((v.z <= 1 && nextVel.z < 0.0) || (v.z >= uGridRes - 2 && nextVel.z > 0.0)) nextVel.z = 0.0;
  }

  outVelocity = vec4(nextVel, 0.0);
  outThermo = vec4(smoke, temp, fuel, reaction);
}
`;

/**
 * PASS 3: 3D Curl (Vorticity Vector) Computation
 */
export const CURL_FRAG = `${COMMON_VOLUME_HEADER}
layout(location = 0) out vec4 outCurl;

uniform sampler2D uVelocityTex;

void main() {
  ivec2 fc = ivec2(gl_FragCoord.xy);
  ivec3 v = fragToVoxel(fc);
  if (v.z >= uGridRes) {
    outCurl = vec4(0.0);
    return;
  }

  vec3 vL = fetchVoxel(uVelocityTex, v + ivec3(-1, 0, 0)).xyz;
  vec3 vR = fetchVoxel(uVelocityTex, v + ivec3( 1, 0, 0)).xyz;
  vec3 vD = fetchVoxel(uVelocityTex, v + ivec3( 0,-1, 0)).xyz;
  vec3 vU = fetchVoxel(uVelocityTex, v + ivec3( 0, 1, 0)).xyz;
  vec3 vB = fetchVoxel(uVelocityTex, v + ivec3( 0, 0,-1)).xyz;
  vec3 vF = fetchVoxel(uVelocityTex, v + ivec3( 0, 0, 1)).xyz;

  float dw_dy = 0.5 * (vU.z - vD.z);
  float dv_dz = 0.5 * (vF.y - vB.y);
  float du_dz = 0.5 * (vF.x - vB.x);
  float dw_dx = 0.5 * (vR.z - vL.z);
  float dv_dx = 0.5 * (vR.y - vL.y);
  float du_dy = 0.5 * (vU.x - vD.x);

  vec3 curl = vec3(dw_dy - dv_dz, du_dz - dw_dx, dv_dx - du_dy);
  outCurl = vec4(curl, length(curl));
}
`;

/**
 * PASS 4: Vorticity Confinement + Buoyancy + Baroclinic Torque + Sub-Grid Turbulence + Wind
 */
export const FORCES_FRAG = `${COMMON_VOLUME_HEADER}
layout(location = 0) out vec4 outVelocity;

uniform sampler2D uVelocityTex;
uniform sampler2D uThermoTex;
uniform sampler2D uCurlTex;
uniform float uDt;
uniform float uTime;
uniform float uVorticityConfinement;
uniform float uBuoyancy;
uniform float uSmokeWeight;
uniform float uTurbulenceStrength;
uniform float uTurbulenceScale;
uniform vec2 uWindXZ;

void main() {
  ivec2 fc = ivec2(gl_FragCoord.xy);
  ivec3 v = fragToVoxel(fc);
  if (v.z >= uGridRes) {
    outVelocity = vec4(0.0);
    return;
  }

  vec4 vel = texelFetch(uVelocityTex, fc, 0);
  if (vel.a > 0.5) {
    outVelocity = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec4 thermo = texelFetch(uThermoTex, fc, 0);
  float smoke = thermo.r;
  float temp  = thermo.g;
  float fuel  = thermo.b;

  float cL = fetchVoxel(uCurlTex, v + ivec3(-1, 0, 0)).a;
  float cR = fetchVoxel(uCurlTex, v + ivec3( 1, 0, 0)).a;
  float cD = fetchVoxel(uCurlTex, v + ivec3( 0,-1, 0)).a;
  float cU = fetchVoxel(uCurlTex, v + ivec3( 0, 1, 0)).a;
  float cB = fetchVoxel(uCurlTex, v + ivec3( 0, 0,-1)).a;
  float cF = fetchVoxel(uCurlTex, v + ivec3( 0, 0, 1)).a;

  vec3 gradMag = 0.5 * vec3(cR - cL, cU - cD, cF - cB);
  float gradLen = length(gradMag);
  vec3 N = gradLen > 1e-5 ? gradMag / gradLen : vec3(0.0);
  vec3 omega = texelFetch(uCurlTex, fc, 0).xyz;
  vec3 vortForce = uVorticityConfinement * cross(N, omega);

  float buoyForceY = uBuoyancy * temp - uSmokeWeight * smoke * 0.45;

  float tL = fetchVoxel(uThermoTex, v + ivec3(-1, 0, 0)).g;
  float tR = fetchVoxel(uThermoTex, v + ivec3( 1, 0, 0)).g;
  float tB = fetchVoxel(uThermoTex, v + ivec3( 0, 0,-1)).g;
  float tF = fetchVoxel(uThermoTex, v + ivec3( 0, 0, 1)).g;
  vec3 baroclinic = vec3(tR - tL, 0.0, tF - tB) * (0.35 * uBuoyancy);

  vec3 uvw = (vec3(v) + 0.5) / float(uGridRes);
  float activity = clamp(smoke * 0.6 + temp * 0.8 + fuel * 0.5, 0.0, 1.5);
  vec3 noiseCoord = uvw * uTurbulenceScale + vec3(uTime * 0.65, -uTime * 1.35, uTime * 0.45);
  vec3 subGridTurb = curlNoise3D(noiseCoord) * uTurbulenceStrength * activity;

  vec3 windForce = vec3(uWindXZ.x, 0.0, uWindXZ.y) * activity * smoothstep(0.05, 0.65, uvw.y);

  vec3 totalForce = vortForce + vec3(0.0, buoyForceY, 0.0) + baroclinic + subGridTurb + windForce;
  vel.xyz += totalForce * uDt;

  float speed = length(vel.xyz);
  if (speed > 20.0) {
    vel.xyz = (vel.xyz / speed) * 20.0;
  }

  outVelocity = vel;
}
`;

/**
 * PASS 5: Velocity Divergence + Combustion Gas Expansion
 */
export const DIVERGENCE_FRAG = `${COMMON_VOLUME_HEADER}
layout(location = 0) out vec4 outDiv;

uniform sampler2D uVelocityTex;
uniform sampler2D uThermoTex;
uniform float uCombustionExpansion;

void main() {
  ivec2 fc = ivec2(gl_FragCoord.xy);
  ivec3 v = fragToVoxel(fc);
  if (v.z >= uGridRes) {
    outDiv = vec4(0.0);
    return;
  }

  vec4 vL = fetchVoxel(uVelocityTex, v + ivec3(-1, 0, 0));
  vec4 vR = fetchVoxel(uVelocityTex, v + ivec3( 1, 0, 0));
  vec4 vD = fetchVoxel(uVelocityTex, v + ivec3( 0,-1, 0));
  vec4 vU = fetchVoxel(uVelocityTex, v + ivec3( 0, 1, 0));
  vec4 vB = fetchVoxel(uVelocityTex, v + ivec3( 0, 0,-1));
  vec4 vF = fetchVoxel(uVelocityTex, v + ivec3( 0, 0, 1));

  vec3 uL = vL.a > 0.5 ? vec3(0.0) : vL.xyz;
  vec3 uR = vR.a > 0.5 ? vec3(0.0) : vR.xyz;
  vec3 uD = (vD.a > 0.5 || v.y == 0) ? vec3(0.0) : vD.xyz;
  vec3 uU = vU.a > 0.5 ? vec3(0.0) : vU.xyz;
  vec3 uB = vB.a > 0.5 ? vec3(0.0) : vB.xyz;
  vec3 uF = vF.a > 0.5 ? vec3(0.0) : vF.xyz;

  float div = 0.5 * ((uR.x - uL.x) + (uU.y - uD.y) + (uF.z - uB.z));

  vec4 thermo = texelFetch(uThermoTex, fc, 0);
  float reaction = thermo.a;
  float expansion = reaction * uCombustionExpansion * 0.08;

  float fireEmissionSource = clamp(thermo.g * 0.45 + thermo.b * 0.35, 0.0, 6.0);
  outDiv = vec4(div - expansion, fireEmissionSource, 0.0, 0.0);
}
`;

/**
 * PASS 6: Jacobi Poisson Pressure Solver + Simultaneous 3D Fire Irradiance Diffusion
 */
export const PRESSURE_JACOBI_FRAG = `${COMMON_VOLUME_HEADER}
layout(location = 0) out vec4 outPressure;

uniform sampler2D uPressureTex;
uniform sampler2D uDivergenceTex;
uniform sampler2D uVelocityTex;

void main() {
  ivec2 fc = ivec2(gl_FragCoord.xy);
  ivec3 v = fragToVoxel(fc);
  if (v.z >= uGridRes) {
    outPressure = vec4(0.0);
    return;
  }

  vec4 centerP = texelFetch(uPressureTex, fc, 0);
  vec4 divSample = texelFetch(uDivergenceTex, fc, 0);
  float div = divSample.r;
  float fireSource = divSample.g;

  ivec3 iL = v + ivec3(-1, 0, 0);
  ivec3 iR = v + ivec3( 1, 0, 0);
  ivec3 iD = v + ivec3( 0,-1, 0);
  ivec3 iU = v + ivec3( 0, 1, 0);
  ivec3 iB = v + ivec3( 0, 0,-1);
  ivec3 iF = v + ivec3( 0, 0, 1);

  float sL = fetchVoxel(uVelocityTex, iL).a;
  float sR = fetchVoxel(uVelocityTex, iR).a;
  float sD = fetchVoxel(uVelocityTex, iD).a;
  float sU = fetchVoxel(uVelocityTex, iU).a;
  float sB = fetchVoxel(uVelocityTex, iB).a;
  float sF = fetchVoxel(uVelocityTex, iF).a;

  vec2 pL = sL > 0.5 ? centerP.rg : fetchVoxel(uPressureTex, iL).rg;
  vec2 pR = sR > 0.5 ? centerP.rg : fetchVoxel(uPressureTex, iR).rg;
  vec2 pD = (sD > 0.5 || v.y == 0) ? centerP.rg : fetchVoxel(uPressureTex, iD).rg;
  vec2 pU = sU > 0.5 ? centerP.rg : fetchVoxel(uPressureTex, iU).rg;
  vec2 pB = sB > 0.5 ? centerP.rg : fetchVoxel(uPressureTex, iB).rg;
  vec2 pF = sF > 0.5 ? centerP.rg : fetchVoxel(uPressureTex, iF).rg;

  float pNew = (pL.r + pR.r + pD.r + pU.r + pB.r + pF.r - div) / 6.0;
  float irrNew = (pL.g + pR.g + pD.g + pU.g + pB.g + pF.g + fireSource * 0.55) / 6.28;

  outPressure = vec4(pNew, irrNew, 0.0, 1.0);
}
`;

/**
 * PASS 7: Pressure Gradient Subtraction
 */
export const GRADIENT_SUBTRACT_FRAG = `${COMMON_VOLUME_HEADER}
layout(location = 0) out vec4 outVelocity;

uniform sampler2D uVelocityTex;
uniform sampler2D uPressureTex;

void main() {
  ivec2 fc = ivec2(gl_FragCoord.xy);
  ivec3 v = fragToVoxel(fc);
  if (v.z >= uGridRes) {
    outVelocity = vec4(0.0);
    return;
  }

  vec4 vel = texelFetch(uVelocityTex, fc, 0);
  if (vel.a > 0.5) {
    outVelocity = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  float pC = texelFetch(uPressureTex, fc, 0).r;

  ivec3 iL = v + ivec3(-1, 0, 0);
  ivec3 iR = v + ivec3( 1, 0, 0);
  ivec3 iD = v + ivec3( 0,-1, 0);
  ivec3 iU = v + ivec3( 0, 1, 0);
  ivec3 iB = v + ivec3( 0, 0,-1);
  ivec3 iF = v + ivec3( 0, 0, 1);

  float sL = fetchVoxel(uVelocityTex, iL).a;
  float sR = fetchVoxel(uVelocityTex, iR).a;
  float sD = fetchVoxel(uVelocityTex, iD).a;
  float sU = fetchVoxel(uVelocityTex, iU).a;
  float sB = fetchVoxel(uVelocityTex, iB).a;
  float sF = fetchVoxel(uVelocityTex, iF).a;

  float pL = sL > 0.5 ? pC : fetchVoxel(uPressureTex, iL).r;
  float pR = sR > 0.5 ? pC : fetchVoxel(uPressureTex, iR).r;
  float pD = (sD > 0.5 || v.y == 0) ? pC : fetchVoxel(uPressureTex, iD).r;
  float pU = sU > 0.5 ? pC : fetchVoxel(uPressureTex, iU).r;
  float pB = sB > 0.5 ? pC : fetchVoxel(uPressureTex, iB).r;
  float pF = sF > 0.5 ? pC : fetchVoxel(uPressureTex, iF).r;

  vec3 gradP = 0.5 * vec3(pR - pL, pU - pD, pF - pB);
  vel.xyz -= gradP;

  if ((sL > 0.5 && vel.x < 0.0) || (sR > 0.5 && vel.x > 0.0)) vel.x = 0.0;
  if ((sD > 0.5 && vel.y < 0.0) || (sU > 0.5 && vel.y > 0.0)) vel.y = 0.0;
  if ((sB > 0.5 && vel.z < 0.0) || (sF > 0.5 && vel.z > 0.0)) vel.z = 0.0;

  outVelocity = vel;
}
`;

/**
 * PASS 8: Full-Viewport Volumetric Raymarcher + Dynamic World Bounds + Voxel DDA + Sparse Octree
 */
export const VOLUMETRIC_RAYMARCH_FRAG = `${COMMON_VOLUME_HEADER}
in vec2 vUV;
layout(location = 0) out vec4 outColor;

uniform sampler2D uThermoTex;
uniform sampler2D uVelocityTex;
uniform sampler2D uCurlTex;
uniform sampler2D uPressureTex;

// Dynamic World-Space Bounding Box
uniform vec3 uBoxMin;
uniform vec3 uBoxMax;
uniform vec3 uDomainScale;

// Camera & Viewport
uniform vec3 uCamPos;
uniform vec3 uCamForward;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uTanHalfFov;
uniform float uAspect;
uniform vec2 uResolution;
uniform float uTime;

// Raymarching & Shading
uniform int uRenderChannel;
uniform int uColorPalette;
uniform int uRaymarchSteps;
uniform int uShadowSteps;
uniform float uVoxelQuantization;
uniform float uDensityExtinction;
uniform float uSmokeAlbedo;
uniform float uShadowDensity;
uniform float uFireIntensity;
uniform float uTemperatureScale;
uniform float uInternalScattering;
uniform float uPhaseAnisotropy;
uniform float uAmbientIntensity;
uniform float uSunIntensity;
uniform vec3 uSunDir;
uniform float uExposure;
uniform float uBloomIntensity;
uniform float uGodRaysIntensity;

// Supersonic Shockwave Shell
uniform float uShockwaveAge;
uniform vec3 uShockwaveCenter;
uniform float uShockwaveStrength;

// Slice Inspector
uniform int uSliceAxis;
uniform float uSlicePos;

// Obstacle & Overlays
uniform int uObstacleType;
uniform vec3 uObstaclePos;
uniform float uObstacleRadius;
uniform int uTransparentExport;
uniform int uShowVoxelGridLines;
uniform int uShowActiveVoxelCells;
uniform int uShowFloorGrid;

vec3 worldToUVW(vec3 p) {
  return (p - uBoxMin) / (uBoxMax - uBoxMin);
}

vec2 intersectBox(vec3 ro, vec3 rd, vec3 bMin, vec3 bMax) {
  vec3 safeDirection = mix(vec3(-1.0), vec3(1.0), greaterThanEqual(rd, vec3(0.0))) * max(abs(rd), vec3(1e-7));
  vec3 invRd = 1.0 / safeDirection;
  vec3 t0 = (bMin - ro) * invRd;
  vec3 t1 = (bMax - ro) * invRd;
  vec3 tSmaller = min(t0, t1);
  vec3 tBigger  = max(t0, t1);
  float tNear = max(max(tSmaller.x, tSmaller.y), tSmaller.z);
  float tFar  = min(min(tBigger.x, tBigger.y), tBigger.z);
  return vec2(tNear, tFar);
}

float interleavedGradientNoise(vec2 pixel) {
  return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float hgPhase(float cosTheta, float g) {
  float g2 = g * g;
  float denom = max(1e-4, pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
  return (1.0 - g2) / (4.0 * 3.14159265 * denom);
}

float dualHenyeyGreenstein(float cosTheta, float g) {
  return mix(hgPhase(cosTheta, -0.25), hgPhase(cosTheta, g), 0.72);
}

vec3 evaluateBlackbodyPalette(float tempRaw, int palette) {
  float t = clamp(tempRaw * uTemperatureScale * 0.28, 0.0, 1.6);
  if (t <= 0.01) return vec3(0.0);

  if (palette == 0) {
    vec3 c0 = vec3(0.0);
    vec3 c1 = vec3(0.55, 0.03, 0.005);
    vec3 c2 = vec3(1.00, 0.24, 0.01);
    vec3 c3 = vec3(1.00, 0.68, 0.10);
    vec3 c4 = vec3(1.00, 0.96, 0.78);
    vec3 col = mix(c0, c1, smoothstep(0.0, 0.22, t));
    col = mix(col, c2, smoothstep(0.18, 0.52, t));
    col = mix(col, c3, smoothstep(0.48, 0.88, t));
    col = mix(col, c4, smoothstep(0.82, 1.35, t));
    return col * (t * t * 1.65);
  } else if (palette == 1) {
    vec3 col = vec3(
      smoothstep(0.02, 0.32, t),
      pow(clamp((t - 0.18) / 0.85, 0.0, 1.2), 1.75),
      pow(clamp((t - 0.58) / 0.75, 0.0, 1.2), 2.4)
    );
    return col * (t * t * 1.8);
  } else if (palette == 2) {
    vec3 col = vec3(
      smoothstep(0.01, 0.22, t),
      smoothstep(0.10, 0.55, t),
      smoothstep(0.28, 0.95, t) * 1.15
    );
    return col * (t * t * 2.1);
  } else if (palette == 3) {
    vec3 c1 = vec3(0.02, 0.38, 0.08);
    vec3 c2 = vec3(0.14, 0.95, 0.28);
    vec3 c3 = vec3(0.72, 1.00, 0.45);
    vec3 c4 = vec3(0.92, 1.00, 0.90);
    vec3 col = mix(vec3(0.0), c1, smoothstep(0.0, 0.25, t));
    col = mix(col, c2, smoothstep(0.22, 0.58, t));
    col = mix(col, c3, smoothstep(0.55, 0.92, t));
    col = mix(col, c4, smoothstep(0.88, 1.35, t));
    return col * (t * t * 1.7);
  } else if (palette == 4) {
    vec3 c1 = vec3(0.04, 0.12, 0.65);
    vec3 c2 = vec3(0.08, 0.58, 1.00);
    vec3 c3 = vec3(0.45, 0.92, 1.00);
    vec3 c4 = vec3(0.95, 0.99, 1.00);
    vec3 col = mix(vec3(0.0), c1, smoothstep(0.0, 0.25, t));
    col = mix(col, c2, smoothstep(0.22, 0.58, t));
    col = mix(col, c3, smoothstep(0.55, 0.92, t));
    col = mix(col, c4, smoothstep(0.88, 1.35, t));
    return col * (t * t * 1.85);
  } else {
    vec3 c1 = vec3(0.32, 0.02, 0.48);
    vec3 c2 = vec3(0.88, 0.08, 0.65);
    vec3 c3 = vec3(1.00, 0.48, 0.88);
    vec3 c4 = vec3(1.00, 0.92, 1.00);
    vec3 col = mix(vec3(0.0), c1, smoothstep(0.0, 0.25, t));
    col = mix(col, c2, smoothstep(0.22, 0.58, t));
    col = mix(col, c3, smoothstep(0.55, 0.92, t));
    col = mix(col, c4, smoothstep(0.88, 1.35, t));
    return col * (t * t * 1.75);
  }
}

float sdObstacle(vec3 uvw) {
  if (uObstacleType == 0) return 1e5;
  vec3 d = (uvw - uObstaclePos) * uDomainScale;
  float rad = uObstacleRadius;
  if (uObstacleType == 1) {
    return length(d) - rad;
  } else if (uObstacleType == 2) {
    vec2 w = vec2(length(d.xz) - rad * 0.75, abs(uvw.y - (uObstaclePos.y * 0.5)) * uDomainScale.y - rad * 1.4);
    return min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
  } else if (uObstacleType == 3) {
    vec2 w = vec2(length(d.yz) - rad * 0.72, abs(d.x) - rad * 1.9);
    return min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
  } else if (uObstacleType == 4) {
    vec3 b = vec3(rad * 1.5, rad * 0.45, rad * 1.5);
    vec3 q = abs(d) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
  } else if (uObstacleType == 5) {
    // Rounded treaded tyre: ring in the X/Y plane, axle along Z.
    return length(vec2(length(d.xy) - rad * 0.76, d.z)) - rad * 0.24;
  }
  return 1e5;
}

float marchSunTransmittance(vec3 startUVW, float quantize) {
  int steps = uShadowSteps;
  float stepLen = 0.065;
  float opticalDepth = 0.0;
  vec3 pos = startUVW;

  for (int i = 0; i < 12; i++) {
    if (i >= steps) break;
    pos += uSunDir * stepLen;
    if (any(lessThan(pos, vec3(0.0))) || any(greaterThan(pos, vec3(1.0)))) break;
    float d = sampleVolumeQuantized(uThermoTex, pos, quantize).r;
    opticalDepth += max(0.0, d) * stepLen * uShadowDensity;
    stepLen *= 1.25;
  }

  float primary = exp(-opticalDepth);
  float secondary = exp(-opticalDepth * 0.28) * 0.65;
  return mix(primary, max(primary, secondary), 0.48);
}

vec3 acesToneMap(vec3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec4 encodeFlipbookPixel(vec3 radiance, float transmittance) {
  vec3 colour = pow(acesToneMap(max(radiance, vec3(0.0)) * uExposure), vec3(1.0 / 2.2));
  float alpha = clamp(max(1.0 - transmittance, max(colour.r, max(colour.g, colour.b))), 0.0, 1.0);
  return vec4(colour, alpha);
}

vec3 evaluateSparseVoxelOverlay(vec3 uvw, float smoke, float temp) {
  if (uShowActiveVoxelCells == 0) return vec3(0.0);
  if (smoke < 0.04 && temp < 0.08) return vec3(0.0);

  float res = float(uGridRes);
  vec3 vf = abs(fract(uvw * res) - 0.5);
  float vMax = max(vf.x, max(vf.y, vf.z));
  float vMid = vf.x + vf.y + vf.z - min(vf.x, min(vf.y, vf.z)) - vMax;
  float fineWire = smoothstep(0.45, 0.49, min(vMax, vMid));

  vec3 mf = abs(fract(uvw * max(4.0, res * 0.25)) - 0.5);
  float mMax = max(mf.x, max(mf.y, mf.z));
  float mMid = mf.x + mf.y + mf.z - min(mf.x, min(mf.y, mf.z)) - mMax;
  float macroWire = smoothstep(0.465, 0.495, min(mMax, mMid));

  vec3 wireCol = mix(vec3(0.15, 0.78, 1.0), vec3(1.0, 0.65, 0.15), clamp(temp * 0.6, 0.0, 1.0));
  return wireCol * (fineWire * 0.35 + macroWire * 0.85);
}

void main() {
  vec2 ndc = vUV * 2.0 - 1.0;
  vec3 rayOrigin = uCamPos;
  vec3 rayDir = normalize(
    uCamForward +
    ndc.x * uAspect * uTanHalfFov * uCamRight +
    ndc.y * uTanHalfFov * uCamUp
  );

  // Supersonic Shockwave Ray Refraction when an explosion just detonated
  float shockGlow = 0.0;
  if (uShockwaveAge > 0.0 && uShockwaveAge < 1.0 && uShockwaveStrength > 0.01) {
    vec3 shockWorld = uBoxMin + uShockwaveCenter * (uBoxMax - uBoxMin);
    float shockRad = uShockwaveAge * length(uBoxMax - uBoxMin) * 0.55;
    vec3 oc = rayOrigin - shockWorld;
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - shockRad * shockRad;
    float h = b * b - c;
    if (h > 0.0) {
      float edgeRing = exp(-h * 18.0 / max(shockRad * shockRad, 0.02));
      float fade = (1.0 - uShockwaveAge) * (1.0 - uShockwaveAge) * uShockwaveStrength;
      vec3 perp = normalize(oc - b * rayDir);
      rayDir = normalize(rayDir + perp * edgeRing * fade * 0.032);
      shockGlow = edgeRing * fade * 0.38;
    }
  }

  float skyGrad = clamp(rayDir.y * 0.5 + 0.5, 0.0, 1.0);
  vec3 bgSky = mix(vec3(0.035, 0.04, 0.052), vec3(0.012, 0.015, 0.022), pow(skyGrad, 0.7));
  float sunHalo = pow(max(0.0, dot(rayDir, uSunDir)), 24.0) * 0.09;
  bgSky += vec3(1.0, 0.78, 0.55) * sunHalo;

  // Studio Ground Plane at y = uBoxMin.y (-0.6)
  float tGround = 1e9;
  bool hitGround = false;
  if (uShowFloorGrid == 1 && rayDir.y < -1e-4) {
    float tg = (uBoxMin.y - rayOrigin.y) / rayDir.y;
    if (tg > 0.0 && tg < 28.0) {
      vec3 gp = rayOrigin + rayDir * tg;
      if (max(abs(gp.x), abs(gp.z)) < 7.5) {
        tGround = tg;
        hitGround = true;
      }
    }
  }

  if (hitGround) {
    vec3 gp = rayOrigin + rayDir * tGround;
    float radialFade = 1.0 - smoothstep(2.2, 7.2, length(gp.xz));

    vec2 gridMajor = abs(fract(gp.xz * 2.0 + 0.5) - 0.5) / max(fwidth(gp.xz * 2.0), vec2(1e-6));
    vec2 gridVoxel = abs(fract(gp.xz * float(uGridRes) * 0.15) - 0.5) / max(fwidth(gp.xz * float(uGridRes) * 0.15), vec2(1e-6));
    float majorLine = 1.0 - min(min(gridMajor.x, gridMajor.y), 1.0);
    float minorLine = (1.0 - min(min(gridVoxel.x, gridVoxel.y), 1.0)) * float(uShowVoxelGridLines);

    // Highlight live dynamic simulation footprint [uBoxMin.xz, uBoxMax.xz] on the studio floor!
    float inDomainFootprint = step(abs(gp.x), uBoxMax.x) * step(abs(gp.z), uBoxMax.z);
    vec3 floorBase = mix(vec3(0.026, 0.030, 0.038), vec3(0.044, 0.050, 0.064), inDomainFootprint);
    floorBase = mix(floorBase, vec3(0.09, 0.105, 0.13), majorLine * 0.55);
    floorBase = mix(floorBase, vec3(0.18, 0.42, 0.65) * 0.35, minorLine * inDomainFootprint * 0.45);

    float axisX = 1.0 - smoothstep(0.0, 0.01, abs(gp.z));
    float axisZ = 1.0 - smoothstep(0.0, 0.01, abs(gp.x));
    floorBase = mix(floorBase, vec3(0.65, 0.16, 0.16), axisX * 0.5);
    floorBase = mix(floorBase, vec3(0.16, 0.32, 0.72), axisZ * 0.5);

    if (uShockwaveAge > 0.0 && uShockwaveAge < 1.0 && uShockwaveStrength > 0.01) {
      vec2 shockXZ = uBoxMin.xz + uShockwaveCenter.xz * (uBoxMax.xz - uBoxMin.xz);
      float dGround = length(gp.xz - shockXZ);
      float waveR = uShockwaveAge * (uBoxMax.x - uBoxMin.x) * 0.85;
      float groundRing = exp(-pow((dGround - waveR) * 12.0, 2.0)) * (1.0 - uShockwaveAge) * uShockwaveStrength;
      floorBase += vec3(1.0, 0.72, 0.38) * groundRing * 0.55;
    }

    float floorShadow = 1.0;
    vec2 shadowBoxHit = intersectBox(gp + uSunDir * 0.005, uSunDir, uBoxMin, uBoxMax);
    if (shadowBoxHit.y > max(shadowBoxHit.x, 0.0)) {
      float tS = max(shadowBoxHit.x, 0.02);
      float dtS = (shadowBoxHit.y - tS) / 6.0;
      float tau = 0.0;
      for (int s = 0; s < 6; s++) {
        vec3 sp = worldToUVW(gp + uSunDir * (tS + (float(s) + 0.5) * dtS));
        tau += sampleVolumeTrilinear(uThermoTex, sp).r * dtS * uShadowDensity * 0.85;
      }
      floorShadow = exp(-tau);
    }

    vec3 sampleAboveUVW = clamp(worldToUVW(vec3(gp.x, uBoxMin.y + 0.18, gp.z)), 0.0, 1.0);
    float distFromDomain = length(max(abs(gp.xz) - uBoxMax.xz, 0.0));
    float bounceIrr = sampleVolumeTrilinear(uPressureTex, sampleAboveUVW).g * exp(-distFromDomain * 2.5);
    vec3 bounceColor = evaluateBlackbodyPalette(max(0.85, bounceIrr * 1.4), uColorPalette) * bounceIrr * 0.55;
    vec3 floorLit = floorBase * (0.35 + 0.65 * floorShadow * uSunIntensity * 0.5) + bounceColor;
    bgSky = mix(bgSky, floorLit, radialFade);
  }

  vec2 boxHit = intersectBox(rayOrigin, rayDir, uBoxMin, uBoxMax);
  float tEnter = max(boxHit.x, 0.0);
  float tExit  = min(boxHit.y, tGround);

  if (tExit <= tEnter) {
    if (uTransparentExport == 1) { outColor = encodeFlipbookPixel(vec3(1.0, 0.85, 0.65) * shockGlow, 1.0); return; }
    vec3 finalBg = acesToneMap((bgSky + vec3(1.0, 0.85, 0.65) * shockGlow) * uExposure);
    outColor = vec4(pow(finalBg, vec3(1.0 / 2.2)), 1.0);
    return;
  }

  // --- MODE 8: 2D Axis Cross-Section Slice Inspector ---
  if (uRenderChannel == 8) {
    vec3 accum = bgSky;
    vec3 sliceWorld = mix(uBoxMin, uBoxMax, vec3(uSlicePos));
    float tSlice = -1.0;
    if (uSliceAxis == 0 && abs(rayDir.x) > 1e-5) tSlice = (sliceWorld.x - rayOrigin.x) / rayDir.x;
    if (uSliceAxis == 1 && abs(rayDir.y) > 1e-5) tSlice = (sliceWorld.y - rayOrigin.y) / rayDir.y;
    if (uSliceAxis == 2 && abs(rayDir.z) > 1e-5) tSlice = (sliceWorld.z - rayOrigin.z) / rayDir.z;

    if (tSlice >= tEnter && tSlice <= tExit) {
      vec3 pHit = rayOrigin + rayDir * tSlice;
      vec3 uvw = worldToUVW(pHit);
      vec4 th = sampleVolumeQuantized(uThermoTex, uvw, uVoxelQuantization);
      vec3 vel = sampleVolumeQuantized(uVelocityTex, uvw, uVoxelQuantization).xyz;
      float res = float(uGridRes);
      vec3 cellFrac = abs(fract(uvw * res) - 0.5);
      float voxelGrid = smoothstep(0.47, 0.495, max(cellFrac.x, max(cellFrac.y, cellFrac.z)));

      vec3 fireCol = evaluateBlackbodyPalette(th.g, uColorPalette);
      vec3 smokeCol = vec3(th.r * 0.45);
      vec3 velCol = abs(vel) * 0.12;
      vec3 sliceCol = vec3(0.05, 0.06, 0.08) + smokeCol + fireCol + velCol;
      sliceCol = mix(sliceCol, vec3(0.25, 0.65, 1.0), voxelGrid * 0.45);
      accum = mix(bgSky, sliceCol, 0.92);
    }
    accum = acesToneMap(accum * uExposure);
    outColor = vec4(pow(accum, vec3(1.0 / 2.2)), 1.0);
    return;
  }

  // --- MODE 1: Explicit 3D Voxel DDA Raymarching (Discrete Voxel Grid Cubes) ---
  if (uRenderChannel == 1) {
    float res = float(uGridRes);
    vec3 startP = worldToUVW(rayOrigin + rayDir * (tEnter + 0.0002)) * res;
    vec3 rdGrid = normalize(rayDir / (uBoxMax - uBoxMin));

    ivec3 mapPos = clamp(ivec3(floor(startP)), ivec3(0), ivec3(uGridRes - 1));
    vec3 deltaDist = abs(vec3(1.0) / max(abs(rdGrid), vec3(1e-5)));
    ivec3 rayStep = ivec3(sign(rdGrid));
    vec3 sideDist = (sign(rdGrid) * (vec3(mapPos) - startP) + (sign(rdGrid) * 0.5) + 0.5) * deltaDist;
    bvec3 mask = bvec3(false);

    vec3 accumLight = vec3(0.0);
    float transmittance = 1.0;
    float stepWorldScale = 1.0 / res;

    int maxDDASteps = min(uGridRes * 2, 180);
    for (int i = 0; i < 180; i++) {
      if (i >= maxDDASteps) break;
      if (any(lessThan(mapPos, ivec3(0))) || any(greaterThanEqual(mapPos, ivec3(uGridRes)))) break;

      vec4 th = fetchVoxel(uThermoTex, mapPos);
      vec4 velAndSolid = fetchVoxel(uVelocityTex, mapPos);
      float irr = fetchVoxel(uPressureTex, mapPos).g;

      if (velAndSolid.a > 0.5 && uTransparentExport == 0) {
        vec3 n = vec3(mask) * (-sign(rdGrid));
        if (length(n) < 0.1) n = -rayDir;
        float diff = max(0.18, dot(n, uSunDir));
        vec3 solidCol = vec3(0.28, 0.32, 0.38) * diff + evaluateBlackbodyPalette(irr, uColorPalette) * 0.35;
        accumLight += transmittance * solidCol;
        transmittance = 0.0;
        break;
      }

      float smoke = th.r;
      float temp  = th.g;

      if (smoke > 0.015 || temp > 0.02) {
        vec3 uvwCenter = (vec3(mapPos) + 0.5) / res;
        vec3 voxelNormal = vec3(mask) * (-sign(rdGrid));
        float facetShade = 0.78 + 0.22 * max(0.0, dot(voxelNormal, uSunDir));

        float sunTrans = marchSunTransmittance(uvwCenter, 1.0);
        vec3 sunColor = vec3(1.0, 0.92, 0.80) * uSunIntensity;
        vec3 ambientColor = vec3(0.32, 0.40, 0.52) * uAmbientIntensity;

        vec3 internalGlow = evaluateBlackbodyPalette(max(0.7, irr * 1.3), uColorPalette)
                          * irr * uInternalScattering * 0.55;

        vec3 smokeScattering = uSmokeAlbedo * facetShade * (sunColor * sunTrans + ambientColor) + internalGlow;
        vec3 fireEmission = evaluateBlackbodyPalette(temp, uColorPalette) * uFireIntensity;

        float ext = max(0.02, smoke * uDensityExtinction + temp * 1.5) * stepWorldScale * 1.15;
        float stepTrans = exp(-ext);
        float weight = (1.0 - stepTrans);

        accumLight += transmittance * (smokeScattering * clamp(smoke * 2.5, 0.0, 1.0) + fireEmission * 0.32) * weight;
        transmittance *= stepTrans;

        if (transmittance < 0.01) {
          transmittance = 0.0;
          break;
        }
      }

      mask = lessThanEqual(sideDist.xyz, min(sideDist.yzx, sideDist.zxy));
      sideDist += vec3(mask) * deltaDist;
      mapPos += ivec3(vec3(mask)) * rayStep;
    }

    if (uTransparentExport == 1) { outColor = encodeFlipbookPixel(accumLight, transmittance); return; }
    vec3 finalCol = accumLight + bgSky * transmittance;
    finalCol = acesToneMap(finalCol * uExposure);
    outColor = vec4(pow(finalCol, vec3(1.0 / 2.2)), 1.0);
    return;
  }

  // --- STANDARD CONTINUOUS / QUANTIZED VOLUMETRIC RAYMARCHING ---
  int numSteps = uRaymarchSteps;
  float boxDiag = length(uBoxMax - uBoxMin);
  float baseStepSize = (boxDiag * 0.82) / float(numSteps);
  // Normalize optical density relative to box size so expanding bounds keeps physical opacity consistent
  float opticalScale = 1.75 / max(1.0, boxDiag);

  float jitter = interleavedGradientNoise(gl_FragCoord.xy);
  float t = tEnter + jitter * baseStepSize;

  vec3 accumLight = vec3(0.0);
  vec3 accumBloom = vec3(0.0);
  float transmittance = 1.0;

  float cosTheta = dot(rayDir, uSunDir);
  float phaseVal = dualHenyeyGreenstein(cosTheta, uPhaseAnisotropy);
  vec3 sunLightColor = vec3(1.0, 0.93, 0.82) * uSunIntensity;
  vec3 skyAmbientColor = vec3(0.30, 0.38, 0.52) * uAmbientIntensity;

  for (int i = 0; i < 160; i++) {
    if (i >= numSteps || t >= tExit) break;

    vec3 pWorld = rayOrigin + rayDir * t;
    vec3 uvw = worldToUVW(pWorld);

    if (uObstacleType != 0 && uTransparentExport == 0) {
      float obsDist = sdObstacle(uvw);
      if (obsDist < 0.0) {
        vec2 eps = vec2(0.008, 0.0);
        vec3 n = normalize(vec3(
          sdObstacle(uvw + eps.xyy) - sdObstacle(uvw - eps.xyy),
          sdObstacle(uvw + eps.yxy) - sdObstacle(uvw - eps.yxy),
          sdObstacle(uvw + eps.yyx) - sdObstacle(uvw - eps.yyx)
        ));
        float irr = sampleVolumeTrilinear(uPressureTex, uvw + n * 0.03).g;
        float diff = max(0.15, dot(n, uSunDir));
        float rim = pow(1.0 - max(0.0, dot(n, -rayDir)), 3.0) * 0.25;

        float res = float(uGridRes);
        vec3 vCell = abs(fract(uvw * res) - 0.5);
        float vEdge = smoothstep(0.44, 0.49, max(vCell.x, max(vCell.y, vCell.z)));

        vec3 obsCol = vec3(0.22, 0.25, 0.30) * diff
                    + vec3(0.45, 0.55, 0.70) * rim
                    + vec3(1.0, 0.52, 0.15) * vEdge * 0.35
                    + evaluateBlackbodyPalette(max(0.8, irr * 1.3), uColorPalette) * irr * 0.65;
        accumLight += transmittance * obsCol;
        transmittance = 0.0;
        break;
      }
    }

    vec4 thermo = sampleVolumeQuantized(uThermoTex, uvw, uVoxelQuantization);
    float smoke = thermo.r;
    float temp  = thermo.g;
    float fuel  = thermo.b;
    float react = thermo.a;

    if (uRenderChannel >= 2) {
      vec3 debugEmission = vec3(0.0);
      float debugDensity = 0.0;

      if (uRenderChannel == 2) {
        debugDensity = smoke * 14.0;
        float sunT = marchSunTransmittance(uvw, uVoxelQuantization);
        debugEmission = vec3(0.85, 0.88, 0.94) * (0.25 + 0.75 * sunT) * debugDensity;
      } else if (uRenderChannel == 3) {
        debugDensity = temp * 8.0;
        debugEmission = evaluateBlackbodyPalette(temp * 1.15, uColorPalette) * 4.5;
      } else if (uRenderChannel == 4) {
        debugDensity = (fuel + react * 0.5) * 9.0;
        debugEmission = mix(vec3(0.1, 0.65, 1.0), vec3(1.0, 0.85, 0.2), clamp(react * 0.4, 0.0, 1.0)) * debugDensity * 1.8;
      } else if (uRenderChannel == 5) {
        vec3 vel = sampleVolumeQuantized(uVelocityTex, uvw, uVoxelQuantization).xyz;
        float speed = length(vel);
        debugDensity = clamp(speed * 1.6, 0.0, 12.0);
        debugEmission = (abs(vel) / max(speed, 0.001)) * debugDensity * 1.6;
      } else if (uRenderChannel == 6) {
        vec4 curlS = sampleVolumeQuantized(uCurlTex, uvw, uVoxelQuantization);
        debugDensity = curlS.a * 4.5;
        debugEmission = mix(vec3(0.15, 0.3, 0.95), vec3(1.0, 0.35, 0.1), clamp(curlS.a * 0.35, 0.0, 1.0)) * debugDensity * 1.5;
      } else if (uRenderChannel == 7) {
        float pVal = sampleVolumeQuantized(uPressureTex, uvw, uVoxelQuantization).r;
        debugDensity = abs(pVal) * 22.0;
        vec3 pCol = pVal >= 0.0 ? vec3(1.0, 0.35, 0.15) : vec3(0.15, 0.55, 1.0);
        debugEmission = pCol * debugDensity * 2.2;
      }

      if (debugDensity > 0.01) {
        float stepTrans = exp(-debugDensity * baseStepSize * opticalScale);
        accumLight += transmittance * debugEmission * baseStepSize * opticalScale;
        transmittance *= stepTrans;
      }
    } else {
      if (smoke > 0.004 || temp > 0.015) {
        float effStep = baseStepSize * opticalScale;
        float extinction = max(0.001, smoke * uDensityExtinction + temp * 0.65);
        float stepTrans = exp(-extinction * effStep);

        float sunTrans = marchSunTransmittance(uvw, uVoxelQuantization);
        float powder = 1.0 - 0.45 * exp(-smoke * uDensityExtinction * 2.2);
        vec3 directSunScatter = sunLightColor * sunTrans * phaseVal * powder * 2.4;

        float heightAO = mix(0.45, 1.15, uvw.y);
        vec3 ambientScatter = skyAmbientColor * heightAO;

        float fireIrradiance = sampleVolumeTrilinear(uPressureTex, uvw).g;
        vec3 internalFireLight = evaluateBlackbodyPalette(max(0.75, fireIrradiance * 1.25), uColorPalette)
                               * fireIrradiance * uInternalScattering * (0.45 + 0.55 * exp(-smoke * 0.9));

        vec3 smokeScatteredRadiance = uSmokeAlbedo * (directSunScatter + ambientScatter) + internalFireLight;
        vec3 fireEmission = evaluateBlackbodyPalette(temp, uColorPalette) * uFireIntensity;

        vec3 sparseOverlay = evaluateSparseVoxelOverlay(uvw, smoke, temp);

        vec3 totalSource = smokeScatteredRadiance * (smoke * uDensityExtinction)
                         + fireEmission * (1.0 + react * 0.25)
                         + sparseOverlay * 8.0;
        vec3 stepIntegral = totalSource * ((1.0 - stepTrans) / extinction);

        accumLight += transmittance * stepIntegral;
        accumBloom += transmittance * internalFireLight * effStep * 0.65;
        transmittance *= stepTrans;

        if (transmittance < 0.008) {
          transmittance = 0.0;
          break;
        }
      } else if (uBloomIntensity > 0.01 || uGodRaysIntensity > 0.01) {
        float effStep = baseStepSize * opticalScale;
        float airIrr = sampleVolumeTrilinear(uPressureTex, uvw).g;
        if (airIrr > 0.02 && uBloomIntensity > 0.01) {
          vec3 haloCol = evaluateBlackbodyPalette(max(0.7, airIrr * 1.1), uColorPalette) * airIrr;
          accumBloom += transmittance * haloCol * effStep * 0.55;
        }
        // Crepuscular Volumetric God-Ray Sun Shafts & Fire Shafts through smoke gaps (every 2nd step)
        if (uGodRaysIntensity > 0.01 && (i & 1) == 0) {
          float sunShaft = marchSunTransmittance(uvw, 0.0);
          float radialMask = (1.0 - smoothstep(0.05, 0.55, length(uvw.xz - vec2(0.5)))) * (1.0 - smoothstep(0.15, 0.95, uvw.y));
          vec3 shaftLight = sunLightColor * sunShaft * (0.25 + 1.45 * phaseVal) * radialMask * 0.09;
          accumBloom += transmittance * shaftLight * effStep * 2.0 * (uGodRaysIntensity / max(uBloomIntensity, 0.25));
        }
      }
    }

    t += baseStepSize;
  }

  if (uTransparentExport == 1) { outColor = encodeFlipbookPixel(accumLight + accumBloom * uBloomIntensity + vec3(1.0, 0.85, 0.65) * shockGlow, transmittance); return; }
  vec3 finalColor = accumLight
                  + accumBloom * uBloomIntensity
                  + bgSky * transmittance
                  + vec3(1.0, 0.85, 0.65) * shockGlow;
  finalColor = acesToneMap(finalColor * uExposure);
  outColor = vec4(pow(finalColor, vec3(1.0 / 2.2)), 1.0);
}
`;

/**
 * PASS 9: GPU Advected Hot Ember / Spark Particles ("Fireflies") Shader
 * Supports dynamic world bounding box [uBoxMin, uBoxMax]
 */
export const EMBER_PARTICLES_VERT = `${COMMON_VOLUME_HEADER}
layout(location = 0) in vec4 aSeed;

uniform sampler2D uVelocityTex;
uniform sampler2D uThermoTex;
uniform float uTime;
uniform vec3 uBoxMin;
uniform vec3 uBoxMax;
uniform vec3 uCamPos;
uniform vec3 uCamForward;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uEmberSize;
uniform float uEmberLifetime;

out float vHeat;
out float vAlpha;

void main() {
  float life = fract(aSeed.w + uTime * (0.28 + 0.22 * aSeed.x) / max(uEmberLifetime, 0.15));
  float angle = aSeed.y * 6.2831853 + uTime * 1.4;
  float rad = sqrt(aSeed.z) * 0.18;

  vec3 baseUVW = vec3(
    0.5 + cos(angle) * rad * (1.0 + life * 1.4),
    0.06 + life * 0.88,
    0.5 + sin(angle) * rad * (1.0 + life * 1.4)
  );

  vec3 flowVel = sampleVolumeTrilinear(uVelocityTex, baseUVW).xyz;
  vec4 thermo = sampleVolumeTrilinear(uThermoTex, baseUVW);

  vec3 displacedUVW = clamp(baseUVW + flowVel * (life * 0.085), 0.02, 0.98);
  vec3 worldPos = uBoxMin + displacedUVW * (uBoxMax - uBoxMin);

  vec3 toCam = worldPos - uCamPos;
  float zCam = dot(toCam, uCamForward);
  if (zCam <= 0.1) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    gl_PointSize = 0.0;
    vAlpha = 0.0;
    vHeat = 0.0;
    return;
  }

  float xCam = dot(toCam, uCamRight);
  float yCam = dot(toCam, uCamUp);

  float ndcX = xCam / (zCam * uAspect * uTanHalfFov);
  float ndcY = yCam / (zCam * uTanHalfFov);

  float localActivity = clamp(thermo.g * 0.7 + thermo.r * 0.5 + length(flowVel) * 0.15, 0.0, 1.5);
  vHeat = clamp((1.0 - life * 0.75) * (0.65 + 0.6 * aSeed.x) + thermo.g * 0.25, 0.0, 1.5);
  vAlpha = sin(life * 3.14159265) * smoothstep(0.05, 0.35, localActivity);

  gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
  gl_PointSize = clamp((4.8 + 5.8 * aSeed.x) * uEmberSize / zCam, 1.5, 14.0);
}
`;

export const EMBER_PARTICLES_FRAG = `#version 300 es
precision highp float;

in float vHeat;
in float vAlpha;
uniform float uEmberIntensity;
uniform float uEmberAshiness;
layout(location = 0) out vec4 outColor;

void main() {
  if (vAlpha <= 0.01) discard;
  vec2 c = gl_PointCoord * 2.0 - 1.0;
  float r2 = dot(c, c);
  if (r2 > 1.0) discard;

  float core = exp(-r2 * 3.2);
  vec3 hotCol = mix(vec3(1.0, 0.22, 0.02), vec3(1.0, 0.88, 0.45), clamp(vHeat, 0.0, 1.0));
  vec3 ashCol = mix(vec3(0.20, 0.22, 0.24), vec3(0.72, 0.75, 0.78), clamp(vHeat * 0.65, 0.0, 1.0));
  vec3 emberCol = mix(hotCol, ashCol, clamp(uEmberAshiness, 0.0, 1.0));
  float glow = mix(1.4, 0.65, clamp(uEmberAshiness, 0.0, 1.0)) * uEmberIntensity;
  outColor = vec4(emberCol * core * glow, core * vAlpha * 0.85);
}
`;

/**
 * PASS 10: 2D Tiled 3D Voxel Atlas Picture-in-Picture (PiP) Minimap Visualizer
 */
export const ATLAS_MINIMAP_FRAG = `#version 300 es
precision highp float;

in vec2 vUV;
layout(location = 0) out vec4 outColor;

uniform sampler2D uAtlasTex;
uniform int uFieldMode;
uniform int uTilesX;
uniform int uTilesY;

void main() {
  vec4 s = texture(uAtlasTex, vUV);
  vec3 col = vec3(0.03, 0.04, 0.055);

  if (uFieldMode == 0) {
    vec3 fire = vec3(1.0, 0.38, 0.05) * s.g * 0.45 + vec3(1.0, 0.88, 0.35) * s.b * 0.35;
    vec3 smoke = vec3(0.55, 0.62, 0.72) * s.r * 0.45;
    col += smoke + fire;
  } else if (uFieldMode == 1) {
    col += abs(s.xyz) * 0.28;
  } else if (uFieldMode == 2) {
    col += mix(vec3(0.05, 0.25, 0.85), vec3(1.0, 0.42, 0.08), clamp(s.a * 0.25, 0.0, 1.0)) * clamp(s.a * 0.35, 0.0, 1.2);
  } else {
    vec3 presCol = s.r >= 0.0 ? vec3(0.2, 0.7, 1.0) * s.r * 2.5 : vec3(0.9, 0.2, 0.4) * (-s.r) * 2.5;
    vec3 irrCol = vec3(1.0, 0.52, 0.12) * s.g * 0.55;
    col += presCol + irrCol;
  }

  vec2 tileCoord = vUV * vec2(float(uTilesX), float(uTilesY));
  vec2 tileFrac = abs(fract(tileCoord) - 0.5);
  float tileBorder = smoothstep(0.47, 0.495, max(tileFrac.x, tileFrac.y));
  col = mix(col, vec3(0.25, 0.34, 0.48), tileBorder * 0.65);

  vec2 outerFrac = abs(vUV - 0.5);
  float outerBorder = smoothstep(0.488, 0.498, max(outerFrac.x, outerFrac.y));
  col = mix(col, vec3(1.0, 0.45, 0.12), outerBorder);

  outColor = vec4(clamp(col, 0.0, 1.0), 0.94);
}
`;
