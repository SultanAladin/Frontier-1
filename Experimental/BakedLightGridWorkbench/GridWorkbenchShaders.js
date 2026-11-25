/*============================================================================================================================================
                                                      GRIDWORKBENCHSHADERS.JS
============================================================================================================================================*/
// 🧩 WebGL2 GLSL ES 3.00 shaders for Ground-Truth Unmerged Lights, Multi-Resolution 8x4x4 Spatial-Directional Baked Light Grids,
//    Live |GroundTruth - BakedGrid| Radiometric Error Heatmaps, Nanite LOD Tier Diagnostics, and 3D Wireframe/Solid Debug Overlays.
//    Uses exact integer texelFetch bilinear interpolation on RGBA32F textures (zero OES_texture_float_linear dependency, zero tile bleed).

export const SurfaceVertexShaderSource = `#version 300 es
precision highp float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aAlbedo;
layout(location = 3) in float aRoughness;

uniform mat4 uViewProjXf;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec3 vAlbedo;
out float vRoughness;

void main()
{
    vWorldPos = aPosition;
    vWorldNormal = normalize(aNormal);
    vAlbedo = aAlbedo;
    vRoughness = aRoughness;
    gl_Position = uViewProjXf * vec4(aPosition, 1.0);
}
`;

export const SurfaceFragmentShaderSource = `#version 300 es
precision highp float;
precision highp sampler2D;

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec3 vAlbedo;
in float vRoughness;

uniform vec3 uCameraPos;
uniform int uRenderViewMode;   // 0 = Ground Truth, 1 = Baked Grid, 2 = |GT - Grid| Error Ramp, 3 = Nanite LOD Diagnostic
uniform int uBakingMode;       // 0 = Naive Center-Averaged, 1 = Spatial Unoptimized, 2 = Residual-Error Optimized
uniform int uEnableDistLod;    // 0 = Force LOD 0 everywhere, 1 = Continuous Nanite Distance-LOD + Culling
uniform float uLodBias;
uniform float uExposure;
uniform float uErrorGain;

uniform int uRawSourceCount;
uniform sampler2D uRawSourcesTex;   // 4 x RawSourceCount RGBA32F
uniform int uClusterCount;
uniform sampler2D uClusterMetaTex;  // 4 x ClusterCount RGBA32F
uniform sampler2D uCellAnchorTex;   // 2 x AnchorCount RGBA32F
uniform sampler2D uOctSheetTex;     // 256 x 256 RGBA32F

out vec4 oFragColor;

const float PI = 3.141592653589793;

// -Y pole octahedral projection matching LightGridBaker.js bit-for-bit
vec2 OctEncodeMinusY(vec3 dir)
{
    float qx = dir.x;
    float qy = -dir.y;
    float qz = dir.z;
    float l1 = max(abs(qx) + abs(qy) + abs(qz), 1e-8);
    vec2 oct = vec2(qx, qz) / l1;
    if (qy < 0.0)
    {
        vec2 signs = vec2(oct.x >= 0.0 ? 1.0 : -1.0, oct.y >= 0.0 ? 1.0 : -1.0);
        oct = (1.0 - abs(oct.yx)) * signs;
    }
    return 0.5 * oct + 0.5;
}

// Exact bleed-free bilinear lookup inside an [originPx .. originPx + res - 1] sub-tile via RGBA32F texelFetch
vec3 SampleOctahedralTile(vec2 originPx, float res, vec3 rayDir)
{
    vec2 uv = OctEncodeMinusY(rayDir);
    vec2 f = clamp(uv * res - 0.5, vec2(0.0), vec2(res - 1.0001));
    ivec2 p0 = ivec2(floor(f));
    ivec2 p1 = min(p0 + ivec2(1, 1), ivec2(int(res) - 1));
    vec2 t = f - vec2(p0);
    ivec2 org = ivec2(int(originPx.x + 0.5), int(originPx.y + 0.5));

    vec3 c00 = texelFetch(uOctSheetTex, org + ivec2(p0.x, p0.y), 0).rgb;
    vec3 c10 = texelFetch(uOctSheetTex, org + ivec2(p1.x, p0.y), 0).rgb;
    vec3 c01 = texelFetch(uOctSheetTex, org + ivec2(p0.x, p1.y), 0).rgb;
    vec3 c11 = texelFetch(uOctSheetTex, org + ivec2(p1.x, p1.y), 0).rgb;

    vec3 cx0 = mix(c00, c10, t.x);
    vec3 cx1 = mix(c01, c11, t.x);
    return mix(cx0, cx1, t.y);
}

// Physical Lambert + Karis Finite-Aperture GGX BRDF matching LightGridBaker.js bit-for-bit
vec3 ShadeFromRadiator(
    vec3 P, vec3 N, vec3 V, float roughness, vec3 albedo,
    vec3 anchorPos, float apertureRadius, vec3 candelaRgb
)
{
    vec3 toLight = anchorPos - P;
    float distSq = dot(toLight, toLight);
    float dist = sqrt(max(distSq, 1e-8));
    vec3 L = toLight / dist;
    float nDotL = dot(N, L);
    if (nDotL <= 0.0) return vec3(0.0);

    float falloff = 1.0 / (distSq + apertureRadius * apertureRadius);
    vec3 E = candelaRgb * (nDotL * falloff);

    float nDotV = max(dot(N, V), 1e-4);
    vec3 H = normalize(L + V);
    float nDotH = max(dot(N, H), 0.0);
    float vDotH = max(dot(V, H), 0.0);

    float alpha = max(roughness * roughness, 0.02);
    float alphaPrime = clamp(alpha + apertureRadius / (2.0 * dist + 1e-4), 0.02, 1.0);
    float a2 = alpha * alpha;
    float ap2 = alphaPrime * alphaPrime;

    float dDenom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    float sphereNorm = (alpha * alpha) / (ap2 + 1e-6);
    float D = (a2 / max(PI * dDenom * dDenom, 1e-6)) * sphereNorm;

    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float gV = nDotV / (nDotV * (1.0 - k) + k);
    float gL = nDotL / (nDotL * (1.0 - k) + k);
    float G = gV * gL;

    float f0 = 0.04;
    float fresnel = f0 + (1.0 - f0) * pow(max(1.0 - vDotH, 0.0), 5.0);
    float spec = (D * G * fresnel) / max(4.0 * nDotV * nDotL, 1e-4);
    float diffScale = (1.0 - fresnel) / PI;

    return E * (albedo * diffScale + vec3(spec));
}

// Evaluates exact Ground-Truth unmerged light source idx
vec3 EvaluateOneRawSource(int idx, vec3 P, vec3 N, vec3 V, float roughness, vec3 albedo)
{
    vec4 t0 = texelFetch(uRawSourcesTex, ivec2(0, idx), 0); // pos.xyz, radius
    vec4 t1 = texelFetch(uRawSourcesTex, ivec2(1, idx), 0); // axis.xyz, cutoffCos
    vec4 t2 = texelFetch(uRawSourcesTex, ivec2(2, idx), 0); // peakCandela.rgb, focusExp
    vec4 t3 = texelFetch(uRawSourcesTex, ivec2(3, idx), 0); // enableEceCutoff

    vec3 srcPos = t0.xyz;
    float radius = t0.w;
    vec3 axis = t1.xyz;
    float cutoffCos = t1.w;
    vec3 peakCd = t2.rgb;
    float focusExp = t2.w;

    vec3 fromSrc = P - srcPos;
    float dist = max(length(fromSrc), 1e-5);
    vec3 rayDir = fromSrc / dist;

    float cosTheta = dot(rayDir, axis);
    if (cosTheta <= cutoffCos) return vec3(0.0);

    float span = max(1.0 - cutoffCos, 1e-4);
    float win = clamp((cosTheta - cutoffCos) / span, 0.0, 1.0);
    float smoothEdge = win * win * (3.0 - 2.0 * win);
    float gain = pow(max(cosTheta, 0.0), focusExp) * smoothEdge;

    if (t3.x > 0.5)
    {
        vec3 upRef = vec3(0.0, 1.0, 0.0);
        vec3 rVec = normalize(cross(upRef, axis));
        vec3 uVec = cross(axis, rVec);
        float latAngle = dot(rayDir, rVec);
        float vertAngle = dot(rayDir, uVec);
        float kinkSlope = latAngle > 0.0 ? min(0.085, latAngle * 0.27) : 0.0;
        float cutoffLimit = -0.016 + kinkSlope;
        float eceMask = 1.0 - clamp((vertAngle - cutoffLimit) / 0.032, 0.0, 1.0);
        gain *= (0.04 + 0.96 * eceMask * eceMask);
    }

    return ShadeFromRadiator(P, N, V, roughness, albedo, srcPos, radius, peakCd * gain);
}

vec3 EvaluateAllGroundTruth(vec3 P, vec3 N, vec3 V, float roughness, vec3 albedo)
{
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 96; ++i)
    {
        if (i >= uRawSourceCount) break;
        sum += EvaluateOneRawSource(i, P, N, V, roughness, albedo);
    }
    return sum;
}

vec3 EvaluateAnchorRange(int startAnchor, int count, vec3 P, vec3 N, vec3 V, float roughness, vec3 albedo)
{
    vec3 acc = vec3(0.0);
    for (int k = 0; k < 24; ++k)
    {
        if (k >= count) break;
        int aIdx = startAnchor + k;
        vec4 a0 = texelFetch(uCellAnchorTex, ivec2(0, aIdx), 0); // anchorPos.xyz, apertureRadius
        vec4 a1 = texelFetch(uCellAnchorTex, ivec2(1, aIdx), 0); // originX, originY, tileRes, 0

        vec3 anchorPos = a0.xyz;
        float radius = a0.w;
        vec3 fromAnchor = P - anchorPos;
        float dist = max(length(fromAnchor), 1e-5);
        vec3 rayDir = fromAnchor / dist;

        vec3 cd = SampleOctahedralTile(a1.xy, a1.z, rayDir);
        if (cd.r + cd.g + cd.b <= 1e-7) continue;
        acc += ShadeFromRadiator(P, N, V, roughness, albedo, anchorPos, radius, cd);
    }
    return acc;
}

vec3 EvaluateDiscreteClusterTier(
    int tier, int lod0Start, int lod0Count, int lod1Start, int lod1Count, int lod2Start, int lod3Start,
    vec3 P, vec3 N, vec3 V, float roughness, vec3 albedo
)
{
    if (tier == 0) return EvaluateAnchorRange(lod0Start, lod0Count, P, N, V, roughness, albedo);
    if (tier == 1) return EvaluateAnchorRange(lod1Start, lod1Count, P, N, V, roughness, albedo);
    if (tier == 2) return EvaluateAnchorRange(lod2Start, 1, P, N, V, roughness, albedo);
    return EvaluateAnchorRange(lod3Start, 1, P, N, V, roughness, albedo);
}

// Evaluates all Baked Light Grid clusters and also outputs minContinuousLod & totalLookups for diagnostic mode
vec3 EvaluateAllBakedGrids(vec3 P, vec3 N, vec3 V, float roughness, vec3 albedo, out float outMinLod, out float outLookups)
{
    vec3 totalRgb = vec3(0.0);
    outMinLod = 4.0;
    outLookups = 0.0;

    for (int c = 0; c < 16; ++c)
    {
        if (c >= uClusterCount) break;
        vec4 m0 = texelFetch(uClusterMetaTex, ivec2(0, c), 0); // boxCenter.xyz, diameter
        vec4 m1 = texelFetch(uClusterMetaTex, ivec2(1, c), 0); // infRadius, naiveStart, lod0Start, lod0Count
        vec4 m2 = texelFetch(uClusterMetaTex, ivec2(2, c), 0); // lod1Start, lod1Count, lod2Start, lod3Start

        vec3 boxCenter = m0.xyz;
        float diameter = m0.w;
        float infRadius = m1.x;
        int naiveStart = int(m1.y + 0.5);
        int lod0Start  = int(m1.z + 0.5);
        int lod0Count  = int(m1.w + 0.5);
        int lod1Start  = int(m2.x + 0.5);
        int lod1Count  = int(m2.y + 0.5);
        int lod2Start  = int(m2.z + 0.5);
        int lod3Start  = int(m2.w + 0.5);

        float dist = length(P - boxCenter);
        float attenWindow = 1.0;
        float lodCont = 0.0;

        if (uEnableDistLod != 0)
        {
            if (dist >= infRadius) continue;
            float ratio = dist / infRadius;
            float r2 = ratio * ratio;
            float win = max(0.0, 1.0 - r2 * r2);
            attenWindow = win * win;
            if (attenWindow <= 1e-5) continue;

            float nearRef = max(diameter * 2.2, 1.8);
            lodCont = clamp(log2(max(dist / nearRef, 1.0)) * 1.15 + uLodBias, 0.0, 3.0);
        }

        outMinLod = min(outMinLod, lodCont);

        if (uBakingMode == 0)
        {
            totalRgb += EvaluateAnchorRange(naiveStart, 1, P, N, V, roughness, albedo) * attenWindow;
            outLookups += 1.0;
            continue;
        }

        if (uEnableDistLod == 0)
        {
            totalRgb += EvaluateDiscreteClusterTier(0, lod0Start, lod0Count, lod1Start, lod1Count, lod2Start, lod3Start, P, N, V, roughness, albedo);
            outLookups += float(lod0Count);
            continue;
        }

        int lowTier = min(3, int(floor(lodCont)));
        int highTier = min(3, lowTier + 1);
        float frac = lodCont - float(lowTier);

        float countLow  = lowTier == 0 ? float(lod0Count) : (lowTier == 1 ? float(lod1Count) : 1.0);
        float countHigh = highTier == 0 ? float(lod0Count) : (highTier == 1 ? float(lod1Count) : 1.0);

        if (lowTier == highTier || frac < 0.02)
        {
            vec3 cLow = EvaluateDiscreteClusterTier(lowTier, lod0Start, lod0Count, lod1Start, lod1Count, lod2Start, lod3Start, P, N, V, roughness, albedo);
            totalRgb += cLow * attenWindow;
            outLookups += countLow;
        }
        else
        {
            vec3 cA = EvaluateDiscreteClusterTier(lowTier,  lod0Start, lod0Count, lod1Start, lod1Count, lod2Start, lod3Start, P, N, V, roughness, albedo);
            vec3 cB = EvaluateDiscreteClusterTier(highTier, lod0Start, lod0Count, lod1Start, lod1Count, lod2Start, lod3Start, P, N, V, roughness, albedo);
            float smoothF = frac * frac * (3.0 - 2.0 * frac);
            totalRgb += mix(cA, cB, smoothF) * attenWindow;
            outLookups += mix(countLow, countHigh, smoothF);
        }
    }

    return totalRgb;
}

vec3 AcesTonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Perceptual Inferno-style thermal ramp: 0 = deep midnight indigo -> violet -> crimson -> amber -> white-hot
vec3 ThermalErrorRamp(float t)
{
    float x = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.03, 0.04, 0.12);
    vec3 c1 = vec3(0.32, 0.07, 0.48);
    vec3 c2 = vec3(0.82, 0.16, 0.24);
    vec3 c3 = vec3(0.99, 0.68, 0.12);
    vec3 c4 = vec3(1.00, 0.98, 0.88);
    if (x < 0.25) return mix(c0, c1, x * 4.0);
    if (x < 0.50) return mix(c1, c2, (x - 0.25) * 4.0);
    if (x < 0.78) return mix(c2, c3, (x - 0.50) / 0.28);
    return mix(c3, c4, (x - 0.78) / 0.22);
}

vec3 LodTierColor(float lod)
{
    if (lod >= 3.8) return vec3(0.14, 0.16, 0.22); // Culled outside influence radius
    vec3 c0 = vec3(0.14, 0.94, 0.56); // LOD 0: 8x4x4 Full Spatial Grid (Emerald)
    vec3 c1 = vec3(0.18, 0.76, 1.00); // LOD 1: Bimodal 2-Pole Split (Cyan)
    vec3 c2 = vec3(0.98, 0.74, 0.18); // LOD 2: 1-Sample Far Grid (Amber)
    vec3 c3 = vec3(0.84, 0.34, 0.96); // LOD 3: Ultra-Far 4x4 Coarse (Violet)
    if (lod < 1.0) return mix(c0, c1, lod);
    if (lod < 2.0) return mix(c1, c2, lod - 1.0);
    return mix(c2, c3, clamp(lod - 2.0, 0.0, 1.0));
}

void main()
{
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    // Subtle dark showroom ambient fill so unlit silhouettes stay legible without polluting error math
    float skyUp = 0.5 * N.y + 0.5;
    vec3 ambientFill = vAlbedo * mix(vec3(0.012, 0.014, 0.020), vec3(0.032, 0.038, 0.052), skyUp);

    // Subtle 1m calibration grid lines on floor (y == 0)
    float floorGrid = 0.0;
    if (abs(vWorldPos.y) < 0.005)
    {
        vec2 g = abs(fract(vWorldPos.xz) - 0.5);
        float line = 1.0 - smoothstep(0.485, 0.498, max(g.x, g.y));
        floorGrid = line * 0.018;
    }

    if (uRenderViewMode == 0)
    {
        vec3 exactDirect = EvaluateAllGroundTruth(vWorldPos, N, V, vRoughness, vAlbedo);
        vec3 hdr = (exactDirect + ambientFill + floorGrid) * uExposure;
        vec3 ldr = pow(AcesTonemap(hdr), vec3(1.0 / 2.2));
        oFragColor = vec4(ldr, 1.0);
        return;
    }

    float minLod = 4.0;
    float lookups = 0.0;
    vec3 gridDirect = EvaluateAllBakedGrids(vWorldPos, N, V, vRoughness, vAlbedo, minLod, lookups);

    if (uRenderViewMode == 1)
    {
        vec3 hdr = (gridDirect + ambientFill + floorGrid) * uExposure;
        vec3 ldr = pow(AcesTonemap(hdr), vec3(1.0 / 2.2));
        oFragColor = vec4(ldr, 1.0);
        return;
    }

    if (uRenderViewMode == 2)
    {
        // Live Pixel-by-Pixel Radiometric Error |L_GroundTruth - L_BakedGrid|
        vec3 exactDirect = EvaluateAllGroundTruth(vWorldPos, N, V, vRoughness, vAlbedo);
        vec3 diff = abs(exactDirect - gridDirect);
        float errMag = length(diff) * 0.57735027 * uErrorGain; // RMS across R,G,B * Gain
        vec3 heat = ThermalErrorRamp(sqrt(clamp(errMag, 0.0, 1.0)));
        // Faint geometric silhouette cue so user sees car/spheres inside zero-error zones
        float sil = pow(1.0 - max(dot(N, V), 0.0), 2.5) * 0.07 + floorGrid * 1.4;
        oFragColor = vec4(clamp(heat + vec3(sil * 0.5, sil * 0.6, sil), 0.0, 1.0), 1.0);
        return;
    }

    // Mode 3: Nanite Distance-LOD Tier & Active Sample Count Diagnostic
    vec3 tierTint = LodTierColor(minLod);
    float litLum = dot(gridDirect, vec3(0.2126, 0.7152, 0.0722)) * uExposure;
    float shade = 0.26 + 0.74 * clamp(litLum / (1.0 + litLum), 0.0, 1.0);
    vec3 diagRgb = tierTint * shade + vec3(floorGrid * 2.0);
    oFragColor = vec4(pow(clamp(diagRgb, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
`;

export const DebugOverlayVertexShaderSource = `#version 300 es
precision highp float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

uniform mat4 uViewProjXf;

out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uViewProjXf * vec4(aPosition, 1.0);
}
`;

export const DebugOverlayFragmentShaderSource = `#version 300 es
precision highp float;

in vec4 vColor;
uniform float uAlphaScale;
out vec4 oFragColor;

void main()
{
    oFragColor = vec4(vColor.rgb, vColor.a * uAlphaScale);
}
`;
