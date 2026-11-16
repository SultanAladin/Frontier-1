/**
 * WGSL Compute & Volumetric Raymarching Shaders for WebGPU Backend
 * Executes 3D Eulerian Pyro via @compute @workgroup_size(4, 4, 4)
 * and renders via full-viewport WGSL volumetric raymarching.
 */

export const WGSL_SIM_COMMON = /* wgsl */ `
struct SimUniforms {
  gridRes: u32,
  macCormack: u32,
  enclosedBox: u32,
  emitterEnabled: u32,

  dt: f32,
  time: f32,
  emitterRate: f32,
  emitterRadius: f32,

  emitterHeight: f32,
  emitterUpwardVel: f32,
  emitterSwirl: f32,
  emitterTemp: f32,

  emitterFuel: f32,
  emitterSmoke: f32,
  blastActive: u32,
  obstacleType: u32,

  blastCenterX: f32,
  blastCenterY: f32,
  blastCenterZ: f32,
  blastRadius: f32,

  blastStrength: f32,
  blastTemp: f32,
  blastFuel: f32,
  blastSmoke: f32,

  blastLobes: f32,
  blastSeed: f32,
  burnRate: f32,
  burnHeat: f32,

  sootGen: f32,
  coolingRate: f32,
  smokeDissipation: f32,
  velocityDamping: f32,

  vorticityConfinement: f32,
  buoyancy: f32,
  smokeWeight: f32,
  combustionExpansion: f32,

  turbulenceStrength: f32,
  turbulenceScale: f32,
  windX: f32,
  windZ: f32,

  obstacleX: f32,
  obstacleY: f32,
  obstacleZ: f32,
  obstacleRadius: f32,

};

fn voxelIndex(v: vec3<i32>, res: i32) -> u32 {
  let c = clamp(v, vec3<i32>(0), vec3<i32>(res - 1));
  return u32(c.x + c.y * res + c.z * res * res);
}

fn hash33(p3_in: vec3<f32>) -> vec3<f32> {
  var p3 = fract(p3_in * vec3<f32>(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yxz + vec3<f32>(33.33));
  return fract((p3.xxy + p3.yxx) * p3.zyx) * 2.0 - vec3<f32>(1.0);
}

fn valueNoise3D(p: vec3<f32>) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (vec3<f32>(3.0) - 2.0 * f);

  let n000 = dot(hash33(i + vec3<f32>(0.0, 0.0, 0.0)), f - vec3<f32>(0.0, 0.0, 0.0));
  let n100 = dot(hash33(i + vec3<f32>(1.0, 0.0, 0.0)), f - vec3<f32>(1.0, 0.0, 0.0));
  let n010 = dot(hash33(i + vec3<f32>(0.0, 1.0, 0.0)), f - vec3<f32>(0.0, 1.0, 0.0));
  let n110 = dot(hash33(i + vec3<f32>(1.0, 1.0, 0.0)), f - vec3<f32>(1.0, 1.0, 0.0));
  let n001 = dot(hash33(i + vec3<f32>(0.0, 0.0, 1.0)), f - vec3<f32>(0.0, 0.0, 1.0));
  let n101 = dot(hash33(i + vec3<f32>(1.0, 0.0, 1.0)), f - vec3<f32>(1.0, 0.0, 1.0));
  let n011 = dot(hash33(i + vec3<f32>(0.0, 1.0, 1.0)), f - vec3<f32>(0.0, 1.0, 1.0));
  let n111 = dot(hash33(i + vec3<f32>(1.0, 1.0, 1.0)), f - vec3<f32>(1.0, 1.0, 1.0));

  return mix(
    mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
    mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y),
    u.z
  );
}

fn curlNoise3D(p: vec3<f32>) -> vec3<f32> {
  let eps = 0.25;
  let n1 = valueNoise3D(p + vec3<f32>(0.0, eps, 0.0)) - valueNoise3D(p - vec3<f32>(0.0, eps, 0.0));
  let n2 = valueNoise3D(p + vec3<f32>(0.0, 0.0, eps)) - valueNoise3D(p - vec3<f32>(0.0, 0.0, eps));
  let n3 = valueNoise3D(p + vec3<f32>(eps, 0.0, 0.0)) - valueNoise3D(p - vec3<f32>(eps, 0.0, 0.0));
  return vec3<f32>(n1 - n2, n2 - n3, n3 - n1) / (2.0 * eps);
}
`;

export const WGSL_COMPUTE_SHADER = /* wgsl */ `
${WGSL_SIM_COMMON}

@group(0) @binding(0) var<uniform> u: SimUniforms;
@group(0) @binding(1) var<storage, read> velIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> thermoIn: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read> auxIn: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> outBuf0: array<vec4<f32>>;
@group(0) @binding(5) var<storage, read_write> outBuf1: array<vec4<f32>>;

fn sampleVelTrilinear(uvw: vec3<f32>, res: i32) -> vec4<f32> {
  let fRes = f32(res);
  let pos = clamp(uvw * fRes - vec3<f32>(0.5), vec3<f32>(0.0), vec3<f32>(fRes - 1.0001));
  let i0 = vec3<i32>(floor(pos));
  let i1 = min(i0 + vec3<i32>(1), vec3<i32>(res - 1));
  let f = pos - vec3<f32>(i0);

  let c000 = velIn[voxelIndex(vec3<i32>(i0.x, i0.y, i0.z), res)];
  let c100 = velIn[voxelIndex(vec3<i32>(i1.x, i0.y, i0.z), res)];
  let c010 = velIn[voxelIndex(vec3<i32>(i0.x, i1.y, i0.z), res)];
  let c110 = velIn[voxelIndex(vec3<i32>(i1.x, i1.y, i0.z), res)];
  let c001 = velIn[voxelIndex(vec3<i32>(i0.x, i0.y, i1.z), res)];
  let c101 = velIn[voxelIndex(vec3<i32>(i1.x, i0.y, i1.z), res)];
  let c011 = velIn[voxelIndex(vec3<i32>(i0.x, i1.y, i1.z), res)];
  let c111 = velIn[voxelIndex(vec3<i32>(i1.x, i1.y, i1.z), res)];

  return mix(
    mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
    mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y),
    f.z
  );
}

fn sampleThermoTrilinear(uvw: vec3<f32>, res: i32) -> vec4<f32> {
  let fRes = f32(res);
  let pos = clamp(uvw * fRes - vec3<f32>(0.5), vec3<f32>(0.0), vec3<f32>(fRes - 1.0001));
  let i0 = vec3<i32>(floor(pos));
  let i1 = min(i0 + vec3<i32>(1), vec3<i32>(res - 1));
  let f = pos - vec3<f32>(i0);

  let c000 = thermoIn[voxelIndex(vec3<i32>(i0.x, i0.y, i0.z), res)];
  let c100 = thermoIn[voxelIndex(vec3<i32>(i1.x, i0.y, i0.z), res)];
  let c010 = thermoIn[voxelIndex(vec3<i32>(i0.x, i1.y, i0.z), res)];
  let c110 = thermoIn[voxelIndex(vec3<i32>(i1.x, i1.y, i0.z), res)];
  let c001 = thermoIn[voxelIndex(vec3<i32>(i0.x, i0.y, i1.z), res)];
  let c101 = thermoIn[voxelIndex(vec3<i32>(i1.x, i0.y, i1.z), res)];
  let c011 = thermoIn[voxelIndex(vec3<i32>(i0.x, i1.y, i1.z), res)];
  let c111 = thermoIn[voxelIndex(vec3<i32>(i1.x, i1.y, i1.z), res)];

  return mix(
    mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
    mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y),
    f.z
  );
}

fn computeObstacleMask(uvw: vec3<f32>) -> f32 {
  if (u.obstacleType == 0u) { return 0.0; }
  let obsPos = vec3<f32>(u.obstacleX, u.obstacleY, u.obstacleZ);
  let d = uvw - obsPos;
  let rad = u.obstacleRadius;
  if (u.obstacleType == 1u) {
    return select(0.0, 1.0, length(d) < rad);
  } else if (u.obstacleType == 2u) {
    return select(0.0, 1.0, length(d.xz) < rad * 0.75 && uvw.y < obsPos.y + rad * 1.8);
  } else if (u.obstacleType == 3u) {
    return select(0.0, 1.0, length(d.yz) < rad * 0.72 && abs(d.x) < rad * 1.9);
  } else if (u.obstacleType == 4u) {
    let b = vec3<f32>(rad * 1.5, rad * 0.45, rad * 1.5);
    let q = abs(d) - b;
    return select(0.0, 1.0, max(max(q.x, q.y), q.z) < 0.0);
  }
  return 0.0;
}

@compute @workgroup_size(4, 4, 4)
fn csSplat(@builtin(global_invocation_id) gid: vec3<u32>) {
  let res = i32(u.gridRes);
  let v = vec3<i32>(gid);
  if (v.x >= res || v.y >= res || v.z >= res) { return; }

  let idx = voxelIndex(v, res);
  let uvw = (vec3<f32>(v) + vec3<f32>(0.5)) / f32(res);

  var vel = velIn[idx];
  var thermo = thermoIn[idx];

  if (computeObstacleMask(uvw) > 0.5) {
    outBuf0[idx] = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    outBuf1[idx] = vec4<f32>(0.0);
    return;
  }
  vel.a = 0.0;

  if (u.emitterEnabled == 1u && u.emitterRate > 0.001) {
    let emitPos = vec3<f32>(0.5, u.emitterHeight, 0.5);
    let diff = uvw - emitPos;
    let dist = length(vec3<f32>(diff.x, diff.y * 1.45, diff.z));
    if (dist < u.emitterRadius * 1.35) {
      let falloff = smoothstep(u.emitterRadius * 1.35, u.emitterRadius * 0.15, dist);
      let np = uvw * 11.0 + vec3<f32>(0.0, -u.time * 5.5, u.time * 1.7);
      let n = 0.55 + 0.45 * valueNoise3D(np);
      let rate = falloff * u.emitterRate * n;

      thermo.r = max(thermo.r, u.emitterSmoke * falloff * (0.6 + 0.4 * n));
      thermo.g = max(thermo.g, u.emitterTemp * rate);
      thermo.b = max(thermo.b, u.emitterFuel * rate);
      thermo.a = max(thermo.a, u.emitterFuel * rate * 0.9);

      let tangent = vec3<f32>(-diff.z, 0.0, diff.x) / max(length(diff.xz), 0.02);
      let turb = hash33(np) * 0.65;
      let targetVel = vec3<f32>(0.0, u.emitterUpwardVel * (0.75 + 0.5 * n), 0.0)
                    + tangent * u.emitterSwirl * (dist / max(u.emitterRadius, 0.01))
                    + turb;
      let blend = clamp(falloff * u.dt * 14.0, 0.0, 0.85);
      vel = vec4<f32>(mix(vel.xyz, targetVel, blend), 0.0);
    }
  }

  if (u.blastActive == 1u) {
    let blastCenter = vec3<f32>(u.blastCenterX, u.blastCenterY, u.blastCenterZ);
    let diff = uvw - blastCenter;
    let r = length(diff);
    let dir = select(vec3<f32>(0.0, 1.0, 0.0), diff / r, r > 1e-4);

    let lobeCoord = dir * u.blastLobes + vec3<f32>(u.blastSeed * 7.13, u.blastSeed * 3.71, u.blastSeed * 5.39);
    let lobeNoise = valueNoise3D(lobeCoord) * 0.35 + valueNoise3D(lobeCoord * 2.1) * 0.18;
    let effectiveRadius = u.blastRadius * (1.0 + lobeNoise);

    if (r < effectiveRadius * 1.25) {
      let coreMask = smoothstep(effectiveRadius, effectiveRadius * 0.1, r);
      let shellMask = smoothstep(effectiveRadius * 1.2, effectiveRadius * 0.45, r) *
                      smoothstep(0.0, effectiveRadius * 0.5, r);

      thermo.r += u.blastSmoke * (shellMask * 0.9 + coreMask * 0.45);
      thermo.g = max(thermo.g, u.blastTemp * coreMask * (1.0 + 0.3 * lobeNoise));
      thermo.b = max(thermo.b, u.blastFuel * coreMask);
      thermo.a = max(thermo.a, u.blastTemp * coreMask);

      let curlPerturb = curlNoise3D(uvw * 9.0 + vec3<f32>(u.blastSeed * 11.0)) * 0.45;
      let blastVel = (dir + vec3<f32>(0.0, 0.38, 0.0) + curlPerturb) * u.blastStrength *
                     smoothstep(effectiveRadius * 1.25, effectiveRadius * 0.12, r);
      vel = vec4<f32>(vel.xyz + blastVel, 0.0);
    }
  }

  outBuf0[idx] = vel;
  outBuf1[idx] = thermo;
}

@compute @workgroup_size(4, 4, 4)
fn csAdvect(@builtin(global_invocation_id) gid: vec3<u32>) {
  let res = i32(u.gridRes);
  let v = vec3<i32>(gid);
  if (v.x >= res || v.y >= res || v.z >= res) { return; }

  let idx = voxelIndex(v, res);
  let curVel = velIn[idx];
  if (curVel.a > 0.5) {
    outBuf0[idx] = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    outBuf1[idx] = vec4<f32>(0.0);
    return;
  }

  let uvw = (vec3<f32>(v) + vec3<f32>(0.5)) / f32(res);
  let backUVW = uvw - curVel.xyz * (u.dt * 0.28);

  let advVel = sampleVelTrilinear(backUVW, res);
  var advThermo = sampleThermoTrilinear(backUVW, res);

  if (u.macCormack == 1u) {
    let fwdUVW = backUVW + advVel.xyz * (u.dt * 0.28);
    let backFwdThermo = sampleThermoTrilinear(fwdUVW, res);
    let curThermo = thermoIn[idx];
    let corrThermo = advThermo + 0.45 * (curThermo - backFwdThermo);

    let nL = thermoIn[voxelIndex(v + vec3<i32>(-1, 0, 0), res)];
    let nR = thermoIn[voxelIndex(v + vec3<i32>( 1, 0, 0), res)];
    let nD = thermoIn[voxelIndex(v + vec3<i32>( 0,-1, 0), res)];
    let nU = thermoIn[voxelIndex(v + vec3<i32>( 0, 1, 0), res)];
    let nB = thermoIn[voxelIndex(v + vec3<i32>( 0, 0,-1), res)];
    let nF = thermoIn[voxelIndex(v + vec3<i32>( 0, 0, 1), res)];

    let minVal = min(curThermo, min(min(nL, nR), min(min(nD, nU), min(nB, nF))));
    let maxVal = max(curThermo, max(max(nL, nR), max(max(nD, nU), max(nB, nF))));
    advThermo = clamp(corrThermo, minVal, maxVal);
  }

  var smoke = max(0.0, advThermo.r);
  var temp  = max(0.0, advThermo.g);
  var fuel  = max(0.0, advThermo.b);

  let ignitionFactor = smoothstep(0.08, 0.65, temp + fuel * 0.5);
  let burnedFuel = min(fuel, fuel * u.burnRate * (0.65 + 0.65 * ignitionFactor) * u.dt);
  fuel = max(0.0, fuel - burnedFuel);

  temp += burnedFuel * u.burnHeat;
  smoke += burnedFuel * u.sootGen;

  let radiativeCool = u.coolingRate * (0.55 * temp + 0.14 * temp * temp) * u.dt;
  temp = max(0.0, temp - radiativeCool);

  smoke = max(0.0, smoke * exp(-u.smokeDissipation * u.dt));
  fuel  = max(0.0, fuel  * exp(-0.15 * u.dt));

  let reaction = burnedFuel / max(u.dt, 1e-4);

  if (u.enclosedBox == 0u) {
    let edgeDistX = min(uvw.x, 1.0 - uvw.x);
    let edgeDistZ = min(uvw.z, 1.0 - uvw.z);
    let topDist = 1.0 - uvw.y;
    let sponge = smoothstep(0.0, 0.055, min(edgeDistX, edgeDistZ)) * smoothstep(0.0, 0.045, topDist);
    smoke *= sponge;
    temp  *= sponge;
    fuel  *= sponge;
  }

  var nextVel = advVel.xyz * exp(-u.velocityDamping * u.dt);
  if (v.y <= 1 && nextVel.y < 0.0) { nextVel.y = 0.0; }

  outBuf0[idx] = vec4<f32>(nextVel, 0.0);
  outBuf1[idx] = vec4<f32>(smoke, temp, fuel, reaction);
}

@compute @workgroup_size(4, 4, 4)
fn csCurl(@builtin(global_invocation_id) gid: vec3<u32>) {
  let res = i32(u.gridRes);
  let v = vec3<i32>(gid);
  if (v.x >= res || v.y >= res || v.z >= res) { return; }

  let vL = velIn[voxelIndex(v + vec3<i32>(-1, 0, 0), res)].xyz;
  let vR = velIn[voxelIndex(v + vec3<i32>( 1, 0, 0), res)].xyz;
  let vD = velIn[voxelIndex(v + vec3<i32>( 0,-1, 0), res)].xyz;
  let vU = velIn[voxelIndex(v + vec3<i32>( 0, 1, 0), res)].xyz;
  let vB = velIn[voxelIndex(v + vec3<i32>( 0, 0,-1), res)].xyz;
  let vF = velIn[voxelIndex(v + vec3<i32>( 0, 0, 1), res)].xyz;

  let curl = 0.5 * vec3<f32>(
    (vU.z - vD.z) - (vF.y - vB.y),
    (vF.x - vB.x) - (vR.z - vL.z),
    (vR.y - vL.y) - (vU.x - vD.x)
  );
  outBuf0[voxelIndex(v, res)] = vec4<f32>(curl, length(curl));
}

@compute @workgroup_size(4, 4, 4)
fn csForces(@builtin(global_invocation_id) gid: vec3<u32>) {
  let res = i32(u.gridRes);
  let v = vec3<i32>(gid);
  if (v.x >= res || v.y >= res || v.z >= res) { return; }

  let idx = voxelIndex(v, res);
  let vel = velIn[idx];
  if (vel.a > 0.5) {
    outBuf0[idx] = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    return;
  }

  let thermo = thermoIn[idx];
  let smoke = thermo.r;
  let temp  = thermo.g;
  let fuel  = thermo.b;

  let cL = auxIn[voxelIndex(v + vec3<i32>(-1, 0, 0), res)].a;
  let cR = auxIn[voxelIndex(v + vec3<i32>( 1, 0, 0), res)].a;
  let cD = auxIn[voxelIndex(v + vec3<i32>( 0,-1, 0), res)].a;
  let cU = auxIn[voxelIndex(v + vec3<i32>( 0, 1, 0), res)].a;
  let cB = auxIn[voxelIndex(v + vec3<i32>( 0, 0,-1), res)].a;
  let cF = auxIn[voxelIndex(v + vec3<i32>( 0, 0, 1), res)].a;

  let gradMag = 0.5 * vec3<f32>(cR - cL, cU - cD, cF - cB);
  let gradLen = length(gradMag);
  let N = select(vec3<f32>(0.0), gradMag / gradLen, gradLen > 1e-5);
  let omega = auxIn[idx].xyz;
  let vortForce = u.vorticityConfinement * cross(N, omega);

  let buoyForceY = u.buoyancy * temp - u.smokeWeight * smoke * 0.45;

  let tL = thermoIn[voxelIndex(v + vec3<i32>(-1, 0, 0), res)].g;
  let tR = thermoIn[voxelIndex(v + vec3<i32>( 1, 0, 0), res)].g;
  let tB = thermoIn[voxelIndex(v + vec3<i32>( 0, 0,-1), res)].g;
  let tF = thermoIn[voxelIndex(v + vec3<i32>( 0, 0, 1), res)].g;
  let baroclinic = vec3<f32>(tR - tL, 0.0, tF - tB) * (0.35 * u.buoyancy);

  let uvw = (vec3<f32>(v) + vec3<f32>(0.5)) / f32(res);
  let activity = clamp(smoke * 0.6 + temp * 0.8 + fuel * 0.5, 0.0, 1.5);
  let noiseCoord = uvw * u.turbulenceScale + vec3<f32>(u.time * 0.65, -u.time * 1.35, u.time * 0.45);
  let subGridTurb = curlNoise3D(noiseCoord) * u.turbulenceStrength * activity;
  let windForce = vec3<f32>(u.windX, 0.0, u.windZ) * activity * smoothstep(0.05, 0.65, uvw.y);

  var nextVel = vel.xyz + (vortForce + vec3<f32>(0.0, buoyForceY, 0.0) + baroclinic + subGridTurb + windForce) * u.dt;
  let speed = length(nextVel);
  if (speed > 18.0) { nextVel = (nextVel / speed) * 18.0; }

  outBuf0[idx] = vec4<f32>(nextVel, 0.0);
}

@compute @workgroup_size(4, 4, 4)
fn csDivergence(@builtin(global_invocation_id) gid: vec3<u32>) {
  let res = i32(u.gridRes);
  let v = vec3<i32>(gid);
  if (v.x >= res || v.y >= res || v.z >= res) { return; }

  let idx = voxelIndex(v, res);
  let vL = velIn[voxelIndex(v + vec3<i32>(-1, 0, 0), res)];
  let vR = velIn[voxelIndex(v + vec3<i32>( 1, 0, 0), res)];
  let vD = velIn[voxelIndex(v + vec3<i32>( 0,-1, 0), res)];
  let vU = velIn[voxelIndex(v + vec3<i32>( 0, 1, 0), res)];
  let vB = velIn[voxelIndex(v + vec3<i32>( 0, 0,-1), res)];
  let vF = velIn[voxelIndex(v + vec3<i32>( 0, 0, 1), res)];

  let uL = select(vL.xyz, vec3<f32>(0.0), vL.a > 0.5);
  let uR = select(vR.xyz, vec3<f32>(0.0), vR.a > 0.5);
  let uD = select(vD.xyz, vec3<f32>(0.0), vD.a > 0.5 || v.y == 0);
  let uU = select(vU.xyz, vec3<f32>(0.0), vU.a > 0.5);
  let uB = select(vB.xyz, vec3<f32>(0.0), vB.a > 0.5);
  let uF = select(vF.xyz, vec3<f32>(0.0), vF.a > 0.5);

  let div = 0.5 * ((uR.x - uL.x) + (uU.y - uD.y) + (uF.z - uB.z));
  let thermo = thermoIn[idx];
  let expansion = thermo.a * u.combustionExpansion * 0.08;
  let fireSource = clamp(thermo.g * 0.45 + thermo.b * 0.35, 0.0, 6.0);

  outBuf0[idx] = vec4<f32>(div - expansion, fireSource, 0.0, 0.0);
}

@compute @workgroup_size(4, 4, 4)
fn csPressure(@builtin(global_invocation_id) gid: vec3<u32>) {
  let res = i32(u.gridRes);
  let v = vec3<i32>(gid);
  if (v.x >= res || v.y >= res || v.z >= res) { return; }

  let idx = voxelIndex(v, res);
  let centerP = thermoIn[idx]; // thermoIn bound to pressureIn here
  let divSample = auxIn[idx];  // auxIn bound to divergenceBuf here

  let iL = voxelIndex(v + vec3<i32>(-1, 0, 0), res);
  let iR = voxelIndex(v + vec3<i32>( 1, 0, 0), res);
  let iD = voxelIndex(v + vec3<i32>( 0,-1, 0), res);
  let iU = voxelIndex(v + vec3<i32>( 0, 1, 0), res);
  let iB = voxelIndex(v + vec3<i32>( 0, 0,-1), res);
  let iF = voxelIndex(v + vec3<i32>( 0, 0, 1), res);

  let pL = select(thermoIn[iL].rg, centerP.rg, velIn[iL].a > 0.5);
  let pR = select(thermoIn[iR].rg, centerP.rg, velIn[iR].a > 0.5);
  let pD = select(thermoIn[iD].rg, centerP.rg, velIn[iD].a > 0.5 || v.y == 0);
  let pU = select(thermoIn[iU].rg, centerP.rg, velIn[iU].a > 0.5);
  let pB = select(thermoIn[iB].rg, centerP.rg, velIn[iB].a > 0.5);
  let pF = select(thermoIn[iF].rg, centerP.rg, velIn[iF].a > 0.5);

  let pNew = (pL.x + pR.x + pD.x + pU.x + pB.x + pF.x - divSample.r) / 6.0;
  let irrNew = (pL.y + pR.y + pD.y + pU.y + pB.y + pF.y + divSample.g * 0.55) / 6.28;

  outBuf0[idx] = vec4<f32>(pNew, irrNew, 0.0, 1.0);
}

@compute @workgroup_size(4, 4, 4)
fn csGradient(@builtin(global_invocation_id) gid: vec3<u32>) {
  let res = i32(u.gridRes);
  let v = vec3<i32>(gid);
  if (v.x >= res || v.y >= res || v.z >= res) { return; }

  let idx = voxelIndex(v, res);
  var vel = velIn[idx];
  if (vel.a > 0.5) {
    outBuf0[idx] = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    return;
  }

  let pC = thermoIn[idx].r; // thermoIn bound to pressureBuf here
  let iL = voxelIndex(v + vec3<i32>(-1, 0, 0), res);
  let iR = voxelIndex(v + vec3<i32>( 1, 0, 0), res);
  let iD = voxelIndex(v + vec3<i32>( 0,-1, 0), res);
  let iU = voxelIndex(v + vec3<i32>( 0, 1, 0), res);
  let iB = voxelIndex(v + vec3<i32>( 0, 0,-1), res);
  let iF = voxelIndex(v + vec3<i32>( 0, 0, 1), res);

  let sL = velIn[iL].a;
  let sR = velIn[iR].a;
  let sD = velIn[iD].a;
  let sU = velIn[iU].a;
  let sB = velIn[iB].a;
  let sF = velIn[iF].a;

  let pL = select(thermoIn[iL].r, pC, sL > 0.5);
  let pR = select(thermoIn[iR].r, pC, sR > 0.5);
  let pD = select(thermoIn[iD].r, pC, sD > 0.5 || v.y == 0);
  let pU = select(thermoIn[iU].r, pC, sU > 0.5);
  let pB = select(thermoIn[iB].r, pC, sB > 0.5);
  let pF = select(thermoIn[iF].r, pC, sF > 0.5);

  var vNew = vel.xyz - 0.5 * vec3<f32>(pR - pL, pU - pD, pF - pB);
  if ((sL > 0.5 && vNew.x < 0.0) || (sR > 0.5 && vNew.x > 0.0)) { vNew.x = 0.0; }
  if ((sD > 0.5 && vNew.y < 0.0) || (sU > 0.5 && vNew.y > 0.0)) { vNew.y = 0.0; }
  if ((sB > 0.5 && vNew.z < 0.0) || (sF > 0.5 && vNew.z > 0.0)) { vNew.z = 0.0; }

  outBuf0[idx] = vec4<f32>(vNew, 0.0);
}

`;

export const WGSL_RAYMARCH_SHADER = /* wgsl */ `
struct RenderUniforms {
  camPos: vec3<f32>,
  tanHalfFov: f32,
  camForward: vec3<f32>,
  aspect: f32,
  camRight: vec3<f32>,
  time: f32,
  camUp: vec3<f32>,
  gridRes: u32,

  sunDir: vec3<f32>,
  voxelQuantization: f32,

  renderChannel: u32,
  colorPalette: u32,
  raymarchSteps: u32,
  shadowSteps: u32,

  densityExtinction: f32,
  smokeAlbedo: f32,
  shadowDensity: f32,
  fireIntensity: f32,

  temperatureScale: f32,
  internalScattering: f32,
  phaseAnisotropy: f32,
  ambientIntensity: f32,

  sunIntensity: f32,
  exposure: f32,
  sliceAxis: u32,
  slicePos: f32,

  obstacleType: u32,
  obstacleX: f32,
  obstacleY: f32,
  obstacleZ: f32,

  obstacleRadius: f32,
  showBoundingBox: u32,
  showVoxelGridLines: u32,
  showFloorGrid: u32,

  boxMin: vec3<f32>,
  pad0: f32,
  boxMax: vec3<f32>,
  pad1: f32,

};

@group(0) @binding(0) var<uniform> r: RenderUniforms;
@group(0) @binding(1) var<storage, read> thermoBuf: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> velBuf: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read> curlBuf: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> presBuf: array<vec4<f32>>;

struct VSOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex
fn vsMain(@builtin(vertex_index) vid: u32) -> VSOut {
  let x = f32((vid & 1u) << 2u) - 1.0;
  let y = f32((vid & 2u) << 1u) - 1.0;
  var out: VSOut;
  out.pos = vec4<f32>(x, y, 0.0, 1.0);
  out.uv = vec2<f32>(x * 0.5 + 0.5, y * 0.5 + 0.5);
  return out;
}

fn voxelIndex(v: vec3<i32>, res: i32) -> u32 {
  let c = clamp(v, vec3<i32>(0), vec3<i32>(res - 1));
  return u32(c.x + c.y * res + c.z * res * res);
}

fn sampleThermo(uvw: vec3<f32>, quantize: f32) -> vec4<f32> {
  let res = i32(r.gridRes);
  let fRes = f32(res);
  let c = clamp(uvw, vec3<f32>(0.0), vec3<f32>(0.9999));
  if (quantize >= 0.99) {
    return thermoBuf[voxelIndex(vec3<i32>(floor(c * fRes)), res)];
  }
  let pos = clamp(c * fRes - vec3<f32>(0.5), vec3<f32>(0.0), vec3<f32>(fRes - 1.0001));
  let i0 = vec3<i32>(floor(pos));
  let i1 = min(i0 + vec3<i32>(1), vec3<i32>(res - 1));
  let f = pos - vec3<f32>(i0);

  let c000 = thermoBuf[voxelIndex(vec3<i32>(i0.x, i0.y, i0.z), res)];
  let c100 = thermoBuf[voxelIndex(vec3<i32>(i1.x, i0.y, i0.z), res)];
  let c010 = thermoBuf[voxelIndex(vec3<i32>(i0.x, i1.y, i0.z), res)];
  let c110 = thermoBuf[voxelIndex(vec3<i32>(i1.x, i1.y, i0.z), res)];
  let c001 = thermoBuf[voxelIndex(vec3<i32>(i0.x, i0.y, i1.z), res)];
  let c101 = thermoBuf[voxelIndex(vec3<i32>(i1.x, i0.y, i1.z), res)];
  let c011 = thermoBuf[voxelIndex(vec3<i32>(i0.x, i1.y, i1.z), res)];
  let c111 = thermoBuf[voxelIndex(vec3<i32>(i1.x, i1.y, i1.z), res)];

  let tri = mix(
    mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
    mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y),
    f.z
  );
  if (quantize <= 0.01) { return tri; }
  let nearest = thermoBuf[voxelIndex(vec3<i32>(floor(c * fRes)), res)];
  return mix(tri, nearest, quantize);
}

fn samplePres(uvw: vec3<f32>) -> vec4<f32> {
  let res = i32(r.gridRes);
  let fRes = f32(res);
  let pos = clamp(uvw * fRes - vec3<f32>(0.5), vec3<f32>(0.0), vec3<f32>(fRes - 1.0001));
  let i0 = vec3<i32>(floor(pos));
  let i1 = min(i0 + vec3<i32>(1), vec3<i32>(res - 1));
  let f = pos - vec3<f32>(i0);
  let c000 = presBuf[voxelIndex(vec3<i32>(i0.x, i0.y, i0.z), res)];
  let c111 = presBuf[voxelIndex(vec3<i32>(i1.x, i1.y, i1.z), res)];
  return mix(c000, c111, (f.x + f.y + f.z) * 0.3333);
}

fn evaluateBlackbodyPalette(tempRaw: f32, palette: u32) -> vec3<f32> {
  let t = clamp(tempRaw * r.temperatureScale * 0.28, 0.0, 1.6);
  if (t <= 0.01) { return vec3<f32>(0.0); }

  if (palette == 0u || palette == 1u) {
    let c1 = vec3<f32>(0.55, 0.03, 0.005);
    let c2 = vec3<f32>(1.00, 0.24, 0.01);
    let c3 = vec3<f32>(1.00, 0.68, 0.10);
    let c4 = vec3<f32>(1.00, 0.96, 0.78);
    var col = mix(vec3<f32>(0.0), c1, smoothstep(0.0, 0.22, t));
    col = mix(col, c2, smoothstep(0.18, 0.52, t));
    col = mix(col, c3, smoothstep(0.48, 0.88, t));
    col = mix(col, c4, smoothstep(0.82, 1.35, t));
    return col * (t * t * 1.65);
  } else if (palette == 2u) {
    let col = vec3<f32>(
      smoothstep(0.01, 0.22, t),
      smoothstep(0.10, 0.55, t),
      smoothstep(0.28, 0.95, t) * 1.15
    );
    return col * (t * t * 2.1);
  } else if (palette == 3u) {
    let c1 = vec3<f32>(0.02, 0.38, 0.08);
    let c2 = vec3<f32>(0.14, 0.95, 0.28);
    let c3 = vec3<f32>(0.72, 1.00, 0.45);
    var col = mix(vec3<f32>(0.0), c1, smoothstep(0.0, 0.25, t));
    col = mix(col, c2, smoothstep(0.22, 0.58, t));
    col = mix(col, c3, smoothstep(0.55, 1.25, t));
    return col * (t * t * 1.7);
  } else if (palette == 4u) {
    let c1 = vec3<f32>(0.04, 0.12, 0.65);
    let c2 = vec3<f32>(0.08, 0.58, 1.00);
    let c3 = vec3<f32>(0.55, 0.95, 1.00);
    var col = mix(vec3<f32>(0.0), c1, smoothstep(0.0, 0.25, t));
    col = mix(col, c2, smoothstep(0.22, 0.58, t));
    col = mix(col, c3, smoothstep(0.55, 1.25, t));
    return col * (t * t * 1.85);
  } else {
    let c1 = vec3<f32>(0.32, 0.02, 0.48);
    let c2 = vec3<f32>(0.88, 0.08, 0.65);
    let c3 = vec3<f32>(1.00, 0.55, 0.92);
    var col = mix(vec3<f32>(0.0), c1, smoothstep(0.0, 0.25, t));
    col = mix(col, c2, smoothstep(0.22, 0.58, t));
    col = mix(col, c3, smoothstep(0.55, 1.25, t));
    return col * (t * t * 1.75);
  }
}

fn intersectBox(ro: vec3<f32>, rd: vec3<f32>, bMin: vec3<f32>, bMax: vec3<f32>) -> vec2<f32> {
  let invRd = 1.0 / rd;
  let t0 = (bMin - ro) * invRd;
  let t1 = (bMax - ro) * invRd;
  let tSmaller = min(t0, t1);
  let tBigger  = max(t0, t1);
  let tNear = max(max(tSmaller.x, tSmaller.y), tSmaller.z);
  let tFar  = min(min(tBigger.x, tBigger.y), tBigger.z);
  return vec2<f32>(tNear, tFar);
}

fn marchSunTransmittance(startUVW: vec3<f32>, quantize: f32) -> f32 {
  var stepLen = 0.065;
  var opticalDepth = 0.0;
  var pos = startUVW;
  for (var i = 0u; i < 12u; i = i + 1u) {
    if (i >= r.shadowSteps) { break; }
    pos += r.sunDir * stepLen;
    if (any(pos < vec3<f32>(0.0)) || any(pos > vec3<f32>(1.0))) { break; }
    let d = sampleThermo(pos, quantize).r;
    opticalDepth += max(0.0, d) * stepLen * r.shadowDensity;
    stepLen *= 1.25;
  }
  let primary = exp(-opticalDepth);
  let secondary = exp(-opticalDepth * 0.28) * 0.65;
  return mix(primary, max(primary, secondary), 0.48);
}

fn acesToneMap(x: vec3<f32>) -> vec3<f32> {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
}

@fragment
fn fsMain(in: VSOut) -> @location(0) vec4<f32> {
  let boxMin = r.boxMin;
  let boxMax = r.boxMax;

  let ndc = in.uv * 2.0 - vec2<f32>(1.0);
  let rayOrigin = r.camPos;
  let rayDir = normalize(
    r.camForward +
    ndc.x * r.aspect * r.tanHalfFov * r.camRight +
    ndc.y * r.tanHalfFov * r.camUp
  );

  let skyGrad = clamp(rayDir.y * 0.5 + 0.5, 0.0, 1.0);
  var bgSky = mix(vec3<f32>(0.035, 0.04, 0.052), vec3<f32>(0.012, 0.015, 0.022), pow(skyGrad, 0.7));

  var tGround = 1e9;
  if (r.showFloorGrid == 1u && rayDir.y < -1e-4) {
    let tg = (boxMin.y - rayOrigin.y) / rayDir.y;
    if (tg > 0.0 && tg < 18.0) {
      let gp = rayOrigin + rayDir * tg;
      if (max(abs(gp.x), abs(gp.z)) < 4.5) {
        tGround = tg;
        let radialFade = 1.0 - smoothstep(1.2, 4.2, length(gp.xz));
        let inFootprint = step(abs(gp.x), 0.5) * step(abs(gp.z), 0.5);
        let floorBase = mix(vec3<f32>(0.028, 0.032, 0.040), vec3<f32>(0.045, 0.052, 0.065), inFootprint);
        let sampleAbove = clamp((vec3<f32>(gp.x, boxMin.y + 0.16, gp.z) - boxMin) / (boxMax - boxMin), vec3<f32>(0.0), vec3<f32>(1.0));
        let bounceIrr = samplePres(sampleAbove).g;
        let pyroBounce = evaluateBlackbodyPalette(max(0.85, bounceIrr * 1.4), r.colorPalette) * bounceIrr * 0.45;
        let bounceCol = pyroBounce;
        bgSky = mix(bgSky, floorBase + bounceCol, radialFade);
      }
    }
  }

  let boxHit = intersectBox(rayOrigin, rayDir, boxMin, boxMax);
  let tEnter = max(boxHit.x, 0.0);
  let tExit  = min(boxHit.y, tGround);

  if (tExit <= tEnter) {
    let mapped = acesToneMap(bgSky * r.exposure);
    return vec4<f32>(pow(mapped, vec3<f32>(1.0 / 2.2)), 1.0);
  }

  let numSteps = r.raymarchSteps;
  let baseStepSize = 1.65 / f32(numSteps);
  let jitter = fract(52.9829189 * fract(dot(in.pos.xy, vec2<f32>(0.06711056, 0.00583715))));
  var t = tEnter + jitter * baseStepSize;

  var accumLight = vec3<f32>(0.0);
  var transmittance = 1.0;

  let sunLightColor = vec3<f32>(1.0, 0.93, 0.82) * r.sunIntensity;
  let skyAmbientColor = vec3<f32>(0.30, 0.38, 0.52) * r.ambientIntensity;
  let quantize = select(r.voxelQuantization, 1.0, r.renderChannel == 1u);


  for (var i = 0u; i < 160u; i = i + 1u) {
    if (i >= numSteps || t >= tExit) { break; }
    let pWorld = rayOrigin + rayDir * t;
    let uvw = (pWorld - boxMin) / (boxMax - boxMin);

    let thermo = sampleThermo(uvw, quantize);
    let smoke = thermo.r;
    let temp  = thermo.g;
    let react = thermo.a;

    if (smoke > 0.004 || temp > 0.015) {
      let extinction = max(0.001, smoke * r.densityExtinction + temp * 0.65);
      let stepTrans = exp(-extinction * baseStepSize);

      let sunTrans = marchSunTransmittance(uvw, quantize);
      let powder = 1.0 - 0.45 * exp(-smoke * r.densityExtinction * 2.2);
      let directSunScatter = sunLightColor * sunTrans * powder * 0.65;
      let ambientScatter = skyAmbientColor * mix(0.45, 1.15, uvw.y);

      let fireIrr = samplePres(uvw).g;
      let internalLight = evaluateBlackbodyPalette(max(0.75, fireIrr * 1.25), r.colorPalette)
                        * fireIrr * r.internalScattering * (0.45 + 0.55 * exp(-smoke * 0.9));

      let smokeScattering = r.smokeAlbedo * (directSunScatter + ambientScatter) + internalLight;
      let fireEmission = evaluateBlackbodyPalette(temp, r.colorPalette) * r.fireIntensity;

      let totalSource = smokeScattering * (smoke * r.densityExtinction) + fireEmission * (1.0 + react * 0.25);
      let stepIntegral = totalSource * ((1.0 - stepTrans) / extinction);

      accumLight += transmittance * stepIntegral;
      transmittance *= stepTrans;

      if (transmittance < 0.008) {
        transmittance = 0.0;
        break;
      }
    }
    t += baseStepSize;
  }

  var finalCol = accumLight + bgSky * transmittance;
  finalCol = acesToneMap(finalCol * r.exposure);
  return vec4<f32>(pow(finalCol, vec3<f32>(1.0 / 2.2)), 1.0);
}
`;
