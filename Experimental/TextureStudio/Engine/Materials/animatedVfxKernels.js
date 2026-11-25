/*============================================================================================================================================
                                                        ANIMATEDVFXKERNELS.JS
============================================================================================================================================*/
// 🧪 Photorealistic GLSL ES 3.0 kernels for the 13 proposed Procedural & Animated OpenPBR materials (uType 47..59).
//    Calibrated to Texture Studio's R=1.43 shaderball unit space (pN = pp / 1.43 in [-1..+1]^3) with real view-angle Fresnel.

export function animatedVfxGLSL() {
  return `
  uniform float uTime;
  uniform float uAnimSpeed;

  float vfxHash21(vec2 p){
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
  }
  vec2 vfxHash22(vec2 p){
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
  }
  float vfxHash31(vec3 p){
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
  }
  float vfxNoise2(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
      mix(vfxHash21(i), vfxHash21(i + vec2(1.0, 0.0)), u.x),
      mix(vfxHash21(i + vec2(0.0, 1.0)), vfxHash21(i + vec2(1.0, 1.0)), u.x),
      u.y
    );
  }
  float vfxFbm2(vec2 p){
    float v = 0.0;
    float a = 0.5;
    mat2 rot = mat2(0.80, 0.60, -0.60, 0.80);
    for(int i = 0; i < 4; i++){
      v += a * vfxNoise2(p);
      p = rot * p * 2.04 + vec2(17.1, 31.7);
      a *= 0.5;
    }
    return v;
  }
  vec3 vfxVoronoi2D(vec2 x){
    vec2 n = floor(x);
    vec2 f = fract(x);
    float f1 = 8.0, f2 = 8.0, cid = 0.0;
    for(int j = -1; j <= 1; j++){
      for(int i = -1; i <= 1; i++){
        vec2 g = vec2(float(i), float(j));
        vec2 o = 0.14 + 0.72 * vfxHash22(n + g);
        vec2 r = g + o - f;
        float d = length(r);
        if(d < f1){
          f2 = f1;
          f1 = d;
          cid = vfxHash21(n + g);
        } else if(d < f2){
          f2 = d;
        }
      }
    }
    return vec3(f1, f2 - f1, cid);
  }
  // Exact 1:1 port of ParticleEditor Final hexGrid(pIn) -> vec2(distFromEdge, cellSeed)
  vec2 vfxShieldHexGrid(vec2 pIn){
    vec2 r = vec2(1.0, 1.7320508);
    vec2 h = r * 0.5;
    vec2 a = (pIn - r * floor(pIn / r)) - h;
    vec2 b = ((pIn - h) - r * floor((pIn - h) / r)) - h;
    vec2 gv = (dot(a, a) < dot(b, b)) ? a : b;
    float ed = max(dot(abs(gv), normalize(r)), abs(gv.x));
    vec2 id = pIn - gv;
    return vec2(0.5 - ed, vfxHash21(floor(id * 2.0 + 0.5)));
  }
  vec3 vfxThinFilmRgb(float opd){
    vec3 phase = 6.2831853 * opd / vec3(0.64, 0.54, 0.45);
    return 0.5 + 0.5 * cos(phase);
  }
  vec3 vfxBlackbody(float t){
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.14, 0.005, 0.001);
    vec3 c1 = vec3(0.88, 0.11, 0.006);
    vec3 c2 = vec3(1.85, 0.58, 0.03);
    vec3 c3 = vec3(3.10, 2.25, 1.05);
    if(t < 0.34) return mix(c0, c1, t / 0.34);
    if(t < 0.70) return mix(c1, c2, (t - 0.34) / 0.36);
    return mix(c2, c3, (t - 0.70) / 0.30);
  }
  vec2 vfxRainDrop(vec2 uv, float t){
    vec2 aspect = vec2(1.0, 2.2);
    vec2 st = uv * aspect;
    st.y += t * 0.42;
    vec2 id = floor(st);
    float n = vfxHash21(id);
    st.y += n * 6.2831;
    vec2 gv = fract(st) - 0.5;
    float xOff = (n - 0.5) * 0.52;
    float dropT = fract(t * (0.28 + 0.25 * n) + n);
    float yOff = 0.42 - dropT * 0.84;
    vec2 d = (gv - vec2(xOff, yOff)) / vec2(0.15, 0.20);
    float bead = smoothstep(1.0, 0.12, length(d));
    float trailX = smoothstep(0.13, 0.02, abs(gv.x - xOff));
    float trailY = smoothstep(yOff, yOff + 0.58, gv.y) * smoothstep(0.5, yOff, gv.y);
    float trail = trailX * trailY * 0.58;
    return vec2(bead, clamp(bead + trail, 0.0, 1.0));
  }
`;
}

export function animatedVfxColor() {
  return `
    vec3 vfxEmitColor = vec3(0.0);
    float vfxEmitMask = 0.0;
    float vfxRoughOverride = -1.0;
    float vfxMetalOverride = -1.0;
    float vfxDiscardMask = 0.0;
    float vfxAlphaOverride = -1.0;
    #if uType >= 47 && uType <= 59
    {
      float vT = uTime * max(0.05, uAnimSpeed);
      vec3 pN = pp / 1.43; // Unit-sphere coordinates in [-1..+1]^3 centered at origin
      vec3 vNormObj = normalize(vProcNormal + vec3(1e-6));
      vec3 vWorldN = normalize(vProcWorldNormal + vec3(1e-6));
      vec3 vViewW = normalize(cameraPosition - vProcWorldPos);
      float vNdotV = clamp(abs(dot(vWorldN, vViewW)), 0.0, 1.0);
      float vFresnel = pow(1.0 - vNdotV, 2.6);
      float vSchlick = 0.02 + 0.98 * pow(1.0 - vNdotV, 5.0);
      // Seamless blended triplanar / spherical coordinates on unit sphere
      vec2 triUv = (abs(vNormObj.y) > 0.68) ? pN.xz : ((abs(vNormObj.x) > 0.68) ? pN.zy : pN.xy);
      vec2 sphUv = vec2(atan(vNormObj.x, vNormObj.z) * 0.63661977, asin(clamp(vNormObj.y, -0.995, 0.995)) * 0.63661977);

      if (uType == 47) {
        // 47: Derez Lattice Dissolve (ControlVehicle 16^3 cubic voxel dissolve + incandescent white-hot->cyan plasma rim)
        float cellGrid = max(5.0, uScale);
        vec3 cellId = floor((pN + 1.0) * cellGrid);
        vec3 cellLocal = fract((pN + 1.0) * cellGrid);
        float jitter = vfxHash31(cellId) * 0.20;
        float sweepClock = 0.30 + 0.44 * (0.5 + 0.5 * sin(vT * 1.15));
        float waveCoord = clamp((pN.x * 0.72 + pN.y * 0.36 + 1.05) * 0.50, 0.0, 1.0);
        float tRelease = waveCoord * 0.80 + jitter;
        float delta = tRelease - sweepClock;
        if (delta < 0.0) {
          vfxDiscardMask = 1.0;
        }
        float rimBand = 1.0 - smoothstep(0.0, 0.16, delta);
        float hotCore = pow(rimBand, 2.4);
        vec3 dEdge = min(cellLocal, 1.0 - cellLocal);
        float latticeSeam = 1.0 - smoothstep(0.02, 0.09, min(dEdge.x, min(dEdge.y, dEdge.z)));
        float flake = vfxHash31(floor(pN * 95.0));
        diffuseColor.rgb = vec3(0.012, 0.015, 0.022) + vec3(0.012, 0.024, 0.036) * smoothstep(0.75, 0.99, flake);
        diffuseColor.rgb = mix(diffuseColor.rgb, uSecondary * 0.32, rimBand * 0.55);
        vfxEmitColor = mix(uSecondary, uTertiary, hotCore);
        vfxEmitMask = clamp(rimBand * (0.90 + 0.85 * latticeSeam) + hotCore * 1.75 + latticeSeam * rimBand * 0.65, 0.0, 3.2);
        surfaceHeight = (latticeSeam * 0.0022 - rimBand * 0.0016) * uGrain;
        vfxRoughOverride = mix(0.06, 0.22, rimBand);
        vfxMetalOverride = mix(0.32, 0.06, rimBand);
      }
      else if (uType == 48) {
        // 48: Photorealistic Cooled Pahoehoe Volcanic Basalt & Recessed Incandescent Magma Fissures
        vec2 uv0 = sphUv * uScale * 1.45;
        vec2 warp = vec2(
          vfxFbm2(uv0 * 1.35 + vec2(3.1, 7.4)),
          vfxFbm2(uv0 * 1.35 + vec2(11.7, 2.3))
        ) - 0.5;
        vec2 uvW = uv0 + warp * 0.68;

        // Broad irregular volcanic basalt crust slabs + secondary tectonic cooling fractures
        vec3 vorMacro = vfxVoronoi2D(uvW * 1.05);
        vec3 vorMicro = vfxVoronoi2D(uvW * 2.40 + warp * 0.75);
        float crustNoise = vfxFbm2(uvW * 3.2);
        float ropyFold = vfxFbm2(vec2(uvW.x * 2.2 + crustNoise * 2.0, uvW.y * 4.8 - crustNoise * 1.5));
        float ashMicro = vfxNoise2(uv0 * 14.0);

        // Irregular tectonic opening mask: broad solid dark basalt slabs separated by dramatic jagged magma channels
        float faultMask = smoothstep(0.28, 0.76, vfxFbm2(uv0 * 0.80 + vec2(vT * 0.04, -vT * 0.03)));
        float primaryCrack = 1.0 - smoothstep(0.012, 0.095 + 0.075 * faultMask, vorMacro.y + (crustNoise - 0.5) * 0.045);
        float secondaryCrack = (1.0 - smoothstep(0.006, 0.046, vorMicro.y)) * faultMask * 0.65;
        float fissure = clamp(max(primaryCrack, secondaryCrack), 0.0, 1.0);

        float pulse = 0.80 + 0.20 * sin(vT * 1.4 + vorMacro.z * 6.28 + crustNoise * 3.2);
        float lavaTemp = clamp(pow(fissure, 1.38) * (0.65 + 0.45 * faultMask) * pulse, 0.0, 1.0);
        float hotVein = smoothstep(0.45, 0.94, lavaTemp);

        // Charcoal & weathered volcanic basalt crust with dark ash pockets and scorched iron-oxide crack rims
        vec3 basaltDark = vec3(0.042, 0.040, 0.039);
        vec3 basaltAsh  = vec3(0.118, 0.110, 0.104);
        vec3 scorchRim  = vec3(0.17, 0.052, 0.022);
        vec3 crustCol = mix(basaltDark, basaltAsh, clamp(crustNoise * 0.62 + ropyFold * 0.38, 0.0, 1.0));
        crustCol *= (0.84 + 0.30 * ashMicro);
        float rimHeatZone = smoothstep(0.05, 0.38, fissure) * (1.0 - hotVein);
        crustCol = mix(crustCol, scorchRim, rimHeatZone * 0.68);

        diffuseColor.rgb = mix(crustCol, vec3(0.022, 0.005, 0.002), smoothstep(0.25, 0.70, fissure));
        vfxEmitColor = vfxBlackbody(lavaTemp);
        vfxEmitMask = smoothstep(0.12, 0.90, lavaTemp) * 2.10;

        float rockRelief = (ropyFold * 0.55 + crustNoise * 0.35 + ashMicro * 0.10) * (1.0 - fissure * 0.88);
        surfaceHeight = (rockRelief * 0.014 - fissure * 0.018) * uGrain;
        float obsidianRidge = smoothstep(0.72, 0.92, ropyFold) * (1.0 - fissure);
        vfxRoughOverride = mix(mix(0.88, 0.34, obsidianRidge), 0.18, smoothstep(0.35, 0.80, fissure));
        vfxMetalOverride = 0.03;
      }
      else if (uType == 49) {
        // 49: Energy Shield Hex Barrier — EXACT 1:1 PORT of ParticleEditor Final/Engine/shaders.js (shape == 16u, energy-shield-hex)
        vec3 n = normalize(vProcNormal + vec3(1e-6));
        float fres = pow(1.0 - vNdotV, 2.55);
        float density = clamp(uScale * 0.95, 0.8, 5.5);
        float rippleRate = 0.55;
        vec2 spBase = vec2(atan(n.x, n.z) * 3.2, asin(clamp(n.y, -0.98, 0.98)) * 3.6);
        vec2 spScaled = spBase * density + vec2(vT * 0.18, 0.0);
        vec2 hg = vfxShieldHexGrid(spScaled);
        float patWire = 1.0 - smoothstep(0.02, 0.085, hg.x);
        float cellShimmer = 0.72 + 0.28 * sin(vT * 2.4 + hg.y * 6.2831853);

        float phase0 = vT * rippleRate;
        float cyc0 = fract(phase0);
        vec3 imp0 = normalize(vec3(0.55 * cos(floor(phase0) * 2.3 + 0.6), 0.32 * sin(floor(phase0) * 1.7 + 0.4), 0.78));
        float gd0 = acos(clamp(dot(n, imp0), -1.0, 1.0));
        float rip0 = exp(-pow((gd0 - cyc0 * 2.1) * 9.5, 2.0)) * (1.0 - smoothstep(0.55, 1.0, cyc0));
        float flash0 = exp(-gd0 * gd0 * 22.0) * exp(-cyc0 * 5.2);

        float phase1 = vT * rippleRate + 0.5;
        float cyc1 = fract(phase1);
        vec3 imp1 = normalize(vec3(-0.55 * sin(floor(phase1) * 2.9 + 0.8), 0.36, 0.75));
        float gd1 = acos(clamp(dot(n, imp1), -1.0, 1.0));
        float rip1 = exp(-pow((gd1 - cyc1 * 2.1) * 9.5, 2.0)) * (1.0 - smoothstep(0.55, 1.0, cyc1));
        float flash1 = exp(-gd1 * gd1 * 22.0) * exp(-cyc1 * 5.2);

        float impact = rip0 + rip1;
        float cellBoost = patWire * (0.28 + 0.62 * fres + 1.35 * impact) * cellShimmer;
        float shieldEnergy = clamp(fres * 0.58 + cellBoost + impact * 0.65 + (flash0 + flash1) * 1.15, 0.0, 1.6);

        vec3 colA = vec3(0.133, 0.722, 1.000);
        vec3 colB = vec3(0.239, 0.420, 1.000);
        vec3 colC = vec3(0.549, 0.961, 1.000);
        vec3 shCol = mix(colA, colC, clamp(impact * 0.75, 0.0, 0.88));
        shCol = mix(shCol, colB, clamp(patWire * 0.35 + fres * 0.25, 0.0, 0.72));
        shCol = mix(shCol, vec3(0.95, 1.0, 1.0), clamp((flash0 + flash1) * 0.92 + impact * patWire * 0.88 + pow(fres, 2.2) * 0.45, 0.0, 0.95));

        diffuseColor.rgb = vec3(0.012, 0.032, 0.072);
        vfxEmitColor = shCol;
        vfxEmitMask = shieldEnergy * 1.45;
        vfxAlphaOverride = clamp(0.16 + fres * 0.56 + cellBoost * 0.85 + impact * 0.62 + (flash0 + flash1) * 0.95, 0.14, 0.98);
        surfaceHeight = (patWire * 0.0020 + impact * 0.0025) * uGrain;
        vfxRoughOverride = 0.08;
        vfxMetalOverride = 0.04;
      }
      else if (uType == 57) {
        // 57: Material-to-Material Transition (Vehicle HMI) — Brushed Raw Aerospace Titanium <-> Liquid Obsidian Clearcoat
        float sweepPos = 0.72 * sin(vT * 0.95);
        float organic = (vfxFbm2(sphUv * uScale * 1.2) - 0.5) * 0.18;
        vec2 hexCell = vfxShieldHexGrid(sphUv * uScale * 2.6);
        float hexJitter = (hexCell.y - 0.5) * 0.12;
        float frontDist = (pN.x * 0.92 + pN.y * 0.34 + organic + hexJitter) - sweepPos;
        float blendM = smoothstep(-0.045, 0.045, frontDist);

        float seamLine = exp(-pow(frontDist * 18.0, 2.0));
        float hexEdge = (1.0 - smoothstep(0.02, 0.12, hexCell.x)) * exp(-pow(frontDist * 7.0, 2.0));

        // Material A (left of wave): Anisotropic Brushed Aerospace Titanium (bright machined silver-champagne)
        float brushLines = vfxNoise2(vec2(sphUv.x * 110.0, sphUv.y * 16.0));
        float fineMicro  = vfxNoise2(sphUv * 48.0);
        vec3 matACol = mix(vec3(0.62, 0.66, 0.72), vec3(0.88, 0.91, 0.95), brushLines * 0.70 + fineMicro * 0.30);
        float matARough = mix(0.20, 0.32, brushLines);
        float matAMetal = 0.90;
        float matAHeight = (brushLines - 0.5) * 0.0024;

        // Material B (right of wave): Deep Liquid Obsidian Automotive Clearcoat
        float pearlFlake = vfxHash31(floor(pN * 95.0));
        vec3 matBCol = vec3(0.012, 0.015, 0.022) + vec3(0.014, 0.028, 0.048) * smoothstep(0.74, 0.99, pearlFlake) + vec3(0.016, 0.026, 0.044) * vFresnel;
        float matBRough = 0.05;
        float matBMetal = 0.24;
        float matBHeight = 0.0;

        diffuseColor.rgb = mix(matACol, matBCol, blendM);
        vfxRoughOverride = mix(matARough, matBRough, blendM);
        vfxMetalOverride = mix(matAMetal, matBMetal, blendM);
        surfaceHeight = (mix(matAHeight, matBHeight, blendM) + hexEdge * 0.0028) * uGrain;
        vfxEmitColor = mix(uTertiary, vec3(1.0, 1.0, 1.0), seamLine * 0.78);
        vfxEmitMask = clamp(seamLine * 2.45 + hexEdge * 1.75 + (1.0 - blendM) * 0.14 * vFresnel, 0.0, 3.4);
      }
      else if (uType == 58) {
        // 58: Holographic Scan & Telemetry (Vehicle HMI) — Sweeping Cyan Lidar Plane + Trailing Contour & Wire Grid
        float scanY = 0.74 * sin(vT * 1.05);
        float distToBeam = pN.y - scanY;

        float laserCore = exp(-pow(distToBeam * 32.0, 2.0));
        float wakeMask = smoothstep(-0.88, 0.0, distToBeam) * smoothstep(0.06, -0.01, distToBeam);

        // Precision metric orthographic grid + elevation contours revealed cleanly inside scan wake
        vec3 gCoord = abs(fract(pN * uScale - 0.5) - 0.5);
        float gridXZ = 1.0 - smoothstep(0.02, 0.075, min(gCoord.x, gCoord.z));
        float contourY = 1.0 - smoothstep(0.02, 0.09, abs(fract(pN.y * uScale * 1.25) - 0.5));
        float scanLines = 0.5 + 0.5 * sin(pN.y * 120.0 - vT * 12.0);

        float wakeTelemetry = wakeMask * (0.18 + 0.72 * gridXZ + 0.62 * contourY) * (0.74 + 0.26 * scanLines);
        float fresHolo = pow(1.0 - vNdotV, 2.9) * (0.35 + 0.75 * wakeMask);

        // Fine carbon-obsidian weave substrate
        float carbon = sin(sphUv.x * 72.0) * sin(sphUv.y * 72.0) * 0.5 + 0.5;
        diffuseColor.rgb = mix(vec3(0.014, 0.018, 0.025), vec3(0.030, 0.042, 0.056), carbon * 0.5);
        vfxEmitColor = mix(uSecondary, uTertiary, clamp(laserCore * 0.85 + contourY * 0.35, 0.0, 1.0));
        vfxEmitMask = clamp(laserCore * 2.85 + wakeTelemetry * 1.25 + fresHolo * 1.05, 0.0, 3.4);
        surfaceHeight = (carbon * 0.0008 + contourY * wakeMask * 0.0018) * uGrain;
        vfxRoughOverride = mix(0.14, 0.06, wakeMask);
        vfxMetalOverride = 0.38;
      }
      else if (uType == 59) {
        // 59: Realistic Physical Ocean Water Shader (multi-directional Gerstner swells + Schlick Fresnel + crest SSS + micro-foam)
        vec2 wp = sphUv * uScale * 2.2;
        float h = 0.0;
        h += sin(dot(wp, vec2( 1.00,  0.42)) * 1.45 + vT * 1.85) * 0.36;
        h += sin(dot(wp, vec2(-0.74,  0.88)) * 2.25 + vT * 2.35) * 0.26;
        h += sin(dot(wp, vec2( 0.48, -1.12)) * 3.60 - vT * 2.95) * 0.14;
        h += sin(dot(wp, vec2(-1.25, -0.62)) * 5.10 + vT * 3.60) * 0.08;
        float capA = vfxFbm2(wp * 2.4 + vec2(vT * 0.55, -vT * 0.38));
        float capB = vfxFbm2(wp * 4.8 + vec2(-vT * 0.65, vT * 0.48));
        float waveH = h + (capA - 0.5) * 0.32 + (capB - 0.5) * 0.14;

        float crest = smoothstep(0.24, 0.84, waveH);
        float foamNoise = vfxFbm2(wp * 4.2 + vec2(vT * 0.25, vT * 0.18));
        float microFoam = smoothstep(0.68, 0.94, crest * 0.85 + foamNoise * 0.45) * smoothstep(0.50, 0.85, foamNoise);

        vec3 deepWater = vec3(0.006, 0.038, 0.064);
        vec3 shallowTurq = vec3(0.022, 0.21, 0.255);
        vec3 skyRefl = vec3(0.36, 0.56, 0.72);
        vec3 waterCol = mix(deepWater, shallowTurq, crest * (0.38 + 0.55 * (1.0 - vNdotV)));
        waterCol = mix(waterCol, skyRefl, vSchlick * 0.88);
        waterCol = mix(waterCol, vec3(0.88, 0.94, 0.98), microFoam * 0.75);

        diffuseColor.rgb = waterCol;
        vfxEmitColor = vec3(0.03, 0.32, 0.38);
        vfxEmitMask = crest * (0.28 + 0.42 * vFresnel) * (1.0 - microFoam);
        surfaceHeight = waveH * 0.0135 * uGrain;
        vfxRoughOverride = mix(0.032, 0.48, microFoam);
        vfxMetalOverride = 0.02;
      }
      else if (uType == 50) {
        // 50: Liquid Obsidian Clearcoat (ControlVehicle deep piano-black paint with micro-pearl depth)
        float microFlake = vfxHash31(floor(pN * 110.0));
        float pearlSpark = smoothstep(0.76, 0.995, microFlake) * (0.28 + 0.72 * vFresnel);
        float orangePeel = vfxFbm2(sphUv * 24.0);
        diffuseColor.rgb = vec3(0.010, 0.013, 0.019) + vec3(0.015, 0.030, 0.052) * vFresnel + vec3(0.07, 0.11, 0.16) * pearlSpark;
        surfaceHeight = (orangePeel - 0.5) * 0.00032 * uGrain;
        vfxRoughOverride = 0.042;
        vfxMetalOverride = 0.26;
      }
      else if (uType == 51) {
        // 51: Rain-Streaked Smoked Glass (dark automotive glass + lens-shaped beads + wet trails + fine mist)
        vec2 rainUv = vec2(atan(vNormObj.x, vNormObj.z) * 1.4, vNormObj.y * 2.2) * (uScale * 0.48);
        vec2 r1 = vfxRainDrop(rainUv, vT);
        vec2 r2 = vfxRainDrop(rainUv * 1.65 + vec2(3.7, 1.4), vT * 0.72);
        float bead = max(r1.x, r2.x * 0.85);
        float wet = clamp(max(r1.y, r2.y * 0.85), 0.0, 1.0);
        float mist = vfxFbm2(sphUv * 18.0) * (1.0 - wet);
        diffuseColor.rgb = mix(vec3(0.018, 0.026, 0.036), vec3(0.055, 0.078, 0.105), mist * 0.42 + vFresnel * 0.35);
        diffuseColor.rgb += vec3(0.22, 0.30, 0.38) * bead * 0.45;
        surfaceHeight = (bead * 0.0105 + wet * 0.0020 + mist * 0.0005) * uGrain;
        vfxRoughOverride = mix(0.28, 0.022, clamp(wet * 1.15, 0.0, 1.0));
        vfxMetalOverride = 0.06;
      }
      else if (uType == 52) {
        // 52: Photorealistic Wet Asphalt & Rain Puddles (mineral stone aggregate + damp pores + mirror puddles w/ ripples)
        vec2 uvA = sphUv * uScale * 1.8;
        float puddleField = vfxFbm2(uvA * 0.48 + vec2(2.4, 5.1));
        float dampZone = smoothstep(0.42, 0.54, puddleField);
        float puddle = smoothstep(0.51, 0.60, puddleField);

        vec3 agg = vfxVoronoi2D(uvA * 5.8);
        float fineGrit = vfxNoise2(uvA * 28.0);
        float mineralFlecks = smoothstep(0.76, 0.96, agg.z);
        vec3 dryAsphalt = mix(vec3(0.088, 0.092, 0.100), vec3(0.175, 0.182, 0.194), fineGrit * 0.65 + mineralFlecks * 0.35);
        vec3 dampAsphalt = dryAsphalt * 0.40;
        vec3 puddleWater = vec3(0.016, 0.022, 0.032) + vec3(0.14, 0.18, 0.24) * vSchlick;

        vec2 ripCell = floor(uvA * 1.35);
        vec2 ripFrac = fract(uvA * 1.35) - 0.5;
        float rSeed = vfxHash21(ripCell);
        float rAge = fract(vT * (0.55 + 0.35 * rSeed) + rSeed);
        float rDist = length(ripFrac - (vfxHash22(ripCell) - 0.5) * 0.42);
        float ripple = sin((rDist - rAge * 0.46) * 44.0) * exp(-abs(rDist - rAge * 0.46) * 18.0) * (1.0 - rAge) * puddle;

        diffuseColor.rgb = mix(mix(dryAsphalt, dampAsphalt, dampZone), puddleWater, puddle);
        float stoneRelief = ((1.0 - agg.x) * 0.55 + fineGrit * 0.45) * (1.0 - puddle);
        surfaceHeight = (stoneRelief * 0.0068 + ripple * 0.0055) * uGrain;
        vfxRoughOverride = mix(mix(0.88, 0.36, dampZone), 0.022, puddle);
        vfxMetalOverride = 0.02;
      }
      else if (uType == 53) {
        // 53: Photorealistic Hoarfrost & Sparkle Snow (wind-sculpted sastrugi drifts + cool SSS shadows + view-dependent glints)
        vec2 uvS = sphUv * uScale * 1.25;
        float drift = vfxFbm2(vec2(uvS.x * 0.72, uvS.y * 1.85));
        float powder = vfxFbm2(uvS * 4.2);
        float fineCryst = vfxNoise2(uvS * 18.0);

        vec3 crystCell = floor(pN * 92.0);
        vec3 crystNorm = normalize(vec3(
          vfxHash31(crystCell) - 0.5,
          vfxHash31(crystCell + 19.3) - 0.5,
          vfxHash31(crystCell + 47.7) - 0.5
        ));
        vec3 halfDir = normalize(vViewW + normalize(vec3(0.45, 0.85, 0.35)));
        float aligned = pow(max(0.0, dot(crystNorm, halfDir)), 28.0);
        float twinkle = 0.60 + 0.40 * sin(vT * 3.8 + vfxHash31(crystCell) * 6.283);
        float glint = smoothstep(0.45, 0.92, aligned * twinkle);

        vec3 troughIce = vec3(0.56, 0.68, 0.82);
        vec3 pureSnow  = vec3(0.92, 0.95, 0.99);
        diffuseColor.rgb = mix(troughIce, pureSnow, clamp(drift * 0.65 + powder * 0.35, 0.0, 1.0));
        diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.98, 0.99, 1.0), fineCryst * 0.14);
        vfxEmitColor = vec3(0.90, 0.97, 1.0);
        vfxEmitMask = glint * 1.65;
        surfaceHeight = (drift * 0.0082 + powder * 0.0028 + fineCryst * 0.0010) * uGrain;
        vfxRoughOverride = mix(0.66, 0.08, glint);
        vfxMetalOverride = 0.01;
      }
      else if (uType == 54) {
        // 54: Photorealistic Submerged Limestone & Chromatic Solar Caustics
        vec2 uvC = sphUv * uScale * 1.75;
        float stoneFbm = vfxFbm2(uvC * 1.85);
        vec3 pebble = vfxVoronoi2D(uvC * 2.2);
        float stoneGrit = vfxNoise2(uvC * 16.0);
        vec3 wetLimestone = mix(vec3(0.06, 0.14, 0.18), vec3(0.18, 0.29, 0.32), stoneFbm * 0.6 + (1.0 - pebble.x) * 0.4);
        wetLimestone *= (0.82 + 0.32 * stoneGrit);

        vec2 flowA = vec2(vT * 0.24, -vT * 0.18);
        vec2 flowB = vec2(-vT * 0.19, vT * 0.26);
        float cR = pow(1.0 - smoothstep(0.0, 0.16, min(vfxVoronoi2D(uvC * 1.15 + flowA + vec2(0.022, 0.0)).y, vfxVoronoi2D(uvC * 1.50 + flowB).y)), 2.2);
        float cG = pow(1.0 - smoothstep(0.0, 0.16, min(vfxVoronoi2D(uvC * 1.15 + flowA).y, vfxVoronoi2D(uvC * 1.50 + flowB).y)), 2.2);
        float cB = pow(1.0 - smoothstep(0.0, 0.16, min(vfxVoronoi2D(uvC * 1.15 + flowA - vec2(0.022, 0.0)).y, vfxVoronoi2D(uvC * 1.50 + flowB).y)), 2.2);
        vec3 causticRgb = vec3(cR, cG, cB) * vec3(0.74, 0.97, 1.00);
        float topAtten = smoothstep(-0.35, 0.55, vNormObj.y);

        diffuseColor.rgb = wetLimestone + causticRgb * (0.30 + 0.60 * topAtten);
        vfxEmitColor = causticRgb;
        vfxEmitMask = (cR + cG + cB) * 0.30 * (0.38 + 0.62 * topAtten);
        surfaceHeight = (stoneFbm * 0.0068 + (1.0 - pebble.x) * 0.0042 + stoneGrit * 0.0010) * uGrain;
        vfxRoughOverride = mix(0.40, 0.16, stoneFbm);
        vfxMetalOverride = 0.02;
      }
      else if (uType == 55) {
        // 55: Liquid Chrome Mercury (smooth mirror silver ripples)
        float s = uScale * 1.65;
        float w1 = sin(pN.x * s * 1.45 + pN.z * s * 0.95 - vT * 2.4);
        float w2 = cos(pN.y * s * 1.65 - pN.x * s * 1.10 + vT * 1.95);
        float w3 = sin(length(pN.xz) * s * 1.90 - vT * 2.85);
        float waveH = (w1 * 0.40 + w2 * 0.35 + w3 * 0.25);
        float crest = smoothstep(-0.35, 0.85, waveH);
        diffuseColor.rgb = mix(vec3(0.76, 0.81, 0.88), vec3(0.96, 0.98, 1.00), crest);
        vfxEmitColor = mix(vec3(0.16, 0.20, 0.26), vec3(0.46, 0.52, 0.60), pow(1.0 - vNdotV, 1.5) * (0.45 + 0.55 * crest));
        vfxEmitMask = 0.30 + 0.38 * vFresnel;
        surfaceHeight = waveH * 0.0155 * uGrain;
        vfxRoughOverride = mix(0.12, 0.045, crest);
        vfxMetalOverride = 0.98;
      }
      else if (uType == 56) {
        // 56: Photorealistic Anodized Thin-Film Iridescence
        float brush = vfxNoise2(vec2(sphUv.x * 88.0, sphUv.y * 12.0));
        float filmWarp = vfxFbm2(sphUv * uScale * 0.95 + vec2(vT * 0.12, -vT * 0.08));
        float opd = 0.72 + 1.35 * vNdotV + 0.42 * filmWarp + 0.12 * sin(vT * 0.65);
        vec3 thinFilm = vfxThinFilmRgb(opd);
        vec3 darkTitanium = vec3(0.055, 0.062, 0.078) * (0.82 + 0.34 * brush);
        float iridWeight = clamp(0.28 + 0.72 * pow(1.0 - vNdotV, 1.35), 0.0, 1.0);
        diffuseColor.rgb = mix(darkTitanium, thinFilm * vec3(0.72, 0.76, 0.84), iridWeight * 0.82);
        vfxEmitColor = thinFilm * vec3(0.45, 0.55, 0.70);
        vfxEmitMask = pow(1.0 - vNdotV, 2.4) * 0.48;
        surfaceHeight = ((brush - 0.5) * 0.0014 + filmWarp * 0.0012) * uGrain;
        vfxRoughOverride = mix(0.11, 0.22, brush);
        vfxMetalOverride = 0.88;
      }
    }
    #endif
`;
}
