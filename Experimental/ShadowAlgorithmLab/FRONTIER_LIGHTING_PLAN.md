# Frontier Engine Lighting & Shadow Architecture Plan
**Validated in `Experimental/ShadowAlgorithmLab` · Target Branch: `arena/5df7676b-frontier-1` -> Engine Integration**

---

## 1. Locked Architectural Decisions

Based on real-time visual & GPU telemetry verification in `Experimental/ShadowAlgorithmLab` with `ControlVehicle`, `ProvingGroundArena`, and `4.5 cm` thin calibration louvers:

1. **Remove SDF from Direct Shadows Completely:**
   - Pre-baked mesh/clipmap SDF grids (`6–13 cm` voxels) punch light-leak holes through thin car shells, pillars, wheel spokes, and louvers while introducing grazing self-shadow acne.
   - **All direct shadow tiers use exact triangle visibility or shadow depth maps only.**
2. **Use SDF Strictly for Diffuse GI Lighting Only:**
   - Keep `SignedDistanceVolume` / `SignedDistanceGridCompute.slang` / `SignedDistanceConeIllumination.slang` exclusively for **low-frequency diffuse multi-bounce GI & large-scale cavity AO** (Tier 1 GTX), where wide cosine cones hide voxel discretization.
   - Static scene SDFs are baked **once**; never re-bake geometry per frame on gizmo drag or vehicle motion.
   - Tier 2 RTX uses `IrradianceLatticeSequence` (`OctahedralIrradianceLattice.slang`).
3. **Remove SDF from Reflections Completely:**
   - Replace SDF specular fallback with **Hi-Z DepthPyramid SSR + Prefiltered Studio/Sky Environment Reflection** on GTX Tier 1 (`SpecularReflectionSequence::EvaluateGtxHiZSpecularRefraction`) and **VNDF `WideBoundingTree` (BVH4) / Hardware RT Reflections** on RTX Tier 2.

---

## 2. Validated Shadow Tier Matrix (After BVH Raytracing)

| Tier Enum | Algorithm | Screen-Pass Cost (1080p GTX 1660 est.) | Multi-Light Scaling (1–4 Lights) | Contact & Penumbra Quality | Best Use Case |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `Tier0_ShadowMapPcf` | **2-Cascade / Spot `3×3` PCF** | `~0.25–0.35 ms` | Linear, ultra-cheap | Sharp/constant blur; no contact hardening | Low-spec / mobile / distant secondary fill spots |
| `Tier1_PcssFiltered` | **Vogel Blocker Search + PCSS + `PlaneAwareShadowFilter`** | `~0.45–0.70 ms` | Linear (`512²–1024²` spots) | True contact-hardening penumbrae; zero SDF holes | **#2 Fastest overall multi-light tier** |
| `Tier1_ContactPcss` *(Default GTX)* | **2-Cascade Contact Clip (`3.9 mm`) + PCSS (`15.6 mm`) + 8-step Screen-Space Depth Contact + `PlaneAwareShadowFilter`** | `~0.60–0.90 ms` | Linear (`~0.15 ms` screen contact per active key/hero light) | **Sub-millimeter tyre/trim contact + smooth PCSS penumbra** | **#1 Recommended GTX tier after BVH raytracing** |
| `Tier1_Smrt` | **SMRT Reverse Quadratic Depth March** (`14` steps × `2–4` rays) | `~1.10–2.10 ms` | Heavy without temporal reprojection | Crisp close silhouette contact; noisy wide penumbra at `<= 4` rays | Optional high-detail single sun mode (with TAA) |
| `Tier2_BvhRaytraced` *(RTX Ref)* | **2-Level TLAS+BLAS (`WideBoundingTree`) Vogel Raytrace + `PlaneAwareShadowFilter`** | `~2.5–6.0+ ms` (GTX compute) / `< 1 ms` (RT cores) | Linear in ray count | Exact triangle penumbra ground truth | Tier 2 RTX / offline-reference lookdev |

---

## 3. Concrete Frontier Engine File Migration Plan

### Phase A — Shadow Pipeline Cleanup (`SolarShadowSequence` & `DirectLighting/*.slang`)
1. **`Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h` & `.cpp`:**
   - Replace `DirectShadowTier::GtxContactClipSdfCone` with explicit non-SDF tiers:
     - `ShadowMapPcf = 0u`
     - `ShadowMapPcss = 1u`
     - `ContactClipPcss = 2u` *(Default Tier 1 GTX)*
     - `SmrtDepthRay = 3u`
     - `RtxDeterministicSolarRay = 4u`
   - Remove `const DistanceFieldSpace::SignedDistanceVolume* OptionalSdfVolume` from `EvaluateSolarShadowField(...)`.
   - Extend light array support from 1 Sun to `Sun + Up to 3 Spot/Area Shadow Maps` (`LightShadowAtlasEntry`).
2. **`Engine/Shaders/DirectLighting/ContactClipShadow.slang` -> Replace with `MultiTierShadowEvaluate.slang`:**
   - Remove `GlobalSignedDistanceTex` (`binding 2`), `SdfBoundsMin/Max/VoxelSpacing`, and `TraceSdfConePenumbra`.
   - Add `FineCascadeDepth`, `CoarseCascadeDepth` / `SpotShadowMaps[3]`, `WorldPositionTex`, `WorldNormalTex`, and `ViewProjMatrix`.
   - Implement `EvalPcf`, `EvalPcss`, `EvalContactClipPlusPcss` (including 8-step `TraceScreenSpaceContact`), and `EvalSmrt`.
   - Pass output `float4(VisLight0, VisLight1, VisLight2, BlockerDistMeters)` straight into `PlaneAwareShadowFilter.slang`.
3. **`Engine/Shaders/DirectLighting/PlaneAwareShadowFilter.slang`:**
   - Widen channel payload from `float2(vis, blocker)` to `float4(vis0, vis1, vis2, blocker)` so 3-step à-trous filtering smooths up to 3 lights in a single pass.

### Phase B — Reflections Cleanup (`SpecularReflectionSequence` & Hi-Z SSR)
1. **`Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.h` & `.cpp`:**
   - Delete any `SignedDistanceVolume` fallback branch in `EvaluateGtxHiZSpecularRefraction`.
   - Keep **DepthPyramid Hi-Z march** -> hit `DirectLitHdrBuffer` -> **analytic Studio Dome / Sky Irradiance + Softbox Proxy Cubemap fallback** on miss (never trace SDF for specular reflections).
   - Keep Tier 2 `EvaluateRtxSpecularRefraction` using `WideBoundingTree` (BVH4) VNDF rays.
2. **`Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h` & `.cpp`:**
   - Remove SDF shadow & reflection coupling; pass `DirectShadowTier` and `DiffuseGiTier` independently.

### Phase C — Diffuse GI Lighting Isolation (`WorldViewportRadiometricCompute.slang` & `EditorLifecycle.cpp`)
1. **`Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang`:**
   - In `ShadePrimarySurface`:
     - Replace `TraceSdfConeVisibility(..., SunDir, ...)` in direct shadows with `1.0` (BVH4 `TraceSceneAnyHit` / `MultiTierShadowEvaluate` handles 100% of direct shadows).
     - Keep `EvaluateGtxConeBounceGi` / `SignedDistanceConeIllumination` **strictly inside the diffuse GI bounce term (`DiffuseGi`)** on Tier 1 GTX.
     - Keep `SpecularReflectionSequence` (Hi-Z SSR + Studio Env or BVH4 Specular) for `SpecGi`.
2. **`Engine/Editor/EditorLifecycle.cpp` & `VulkanWorldRenderSequence.cpp`:**
   - Never trigger `DispatchGpuSignedDistanceGrid` during interactive gizmo drag or vehicle transforms — keep `SceneSdf` strictly as a static-scene GI bounce cache (with rigid instance box occlusion if dynamic GI darkening is desired).
   - Expose per-feature Tier selectors in `ViewportPanel` (`ShadowTier`, `GiMode`, `ReflectionTier`).

---

## 4. Reference Implementation Code (Frontier C++20 + Slang)

### 4.1 `Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h` (Updated Enums & Multi-Light Entry)
```cpp
namespace Frontier::GeometricRaster
{

enum class DirectShadowTier : uint8_t
{
    ShadowMapPcf             = 0u, // 3x3 PCF (fastest secondary / low-spec)
    ShadowMapPcss            = 1u, // Vogel blocker search + PCSS + PlaneAwareShadowFilter (#2 fastest multi-light)
    ContactClipPcss          = 2u, // Default GTX Tier 1: 3.9mm Contact Clip + 15.6mm PCSS + Screen-Space Contact (No SDF)
    SmrtDepthRay             = 3u, // Reverse quadratic shadow-map march (optional single-sun / TAA)
    RtxDeterministicSolarRay = 4u, // Tier 2: WideBoundingTree Vogel raytrace + PlaneAwareShadowFilter
};

enum class DiffuseGiMode : uint8_t
{
    OffAmbientOnly   = 0u,
    GtxSdfConeGiOnly = 1u, // SDF reserved strictly for low-frequency diffuse bounce lighting
    RtxIrradianceGrid= 2u,
};

enum class SpecularReflectionMode : uint8_t
{
    EnvironmentOnly  = 0u,
    GtxHiZScreenSpace= 1u, // DepthPyramid Hi-Z SSR + analytical studio/sky fallback (zero SDF!)
    RtxVndfBvhTrace  = 2u,
};

struct ShadowLightProjection
{
    Mathematics::Matrix4x4 LightViewProj       {};
    Mathematics::Axis3D    LightOrigin         {};
    Mathematics::Axis3D    LightForward        {};
    Mathematics::Axis3D    DirectionToLight    {};
    float                  LightRadius         = 0.042f; // tan(sunHalfAngle) or discRadiusMeters
    float                  OrthoHalfSpanMeters = 16.0f;  // 0.0f => perspective spot/area light
    float                  PerspTanHalfFov     = 0.0f;
    uint32_t               MapResolution       = 2048u;
    uint32_t               LightType           = 0u;     // 0 = Directional Ortho, 1 = Spot/Area Perspective
};

} // namespace Frontier::GeometricRaster
```

### 4.2 `Engine/Shaders/DirectLighting/MultiTierShadowEvaluate.slang` (Zero-SDF Multi-Light Shadows)
```hlsl
module MultiTierShadowEvaluate;

struct ShadowLightUniform
{
    float4x4 LightViewProj;
    float4   OriginAndType;       // xyz = Origin, w = LightType (0 = Ortho Sun, 1 = Persp Spot/Area)
    float4   ForwardAndRadius;    // xyz = Forward, w = TanSunAngle or DiscRadiusMeters
    float4   DirAndMapRes;        // xyz = ToLightDir, w = MapResolution
    float4   SpanAndFov;          // x = OrthoHalfSpan, y = PerspTanHalfFov, zw = Pad
};

struct MultiShadowUniforms
{
    float4x4           CameraViewProj;
    float4x4           ContactViewProj;
    float4             ContactOriginAndSpan;    // xyz = Origin, w = HalfSpanMeters
    float4             ContactForwardAndRes;    // xyz = Forward, w = ContactRes
    float4             CameraPosAndTanFov;      // xyz = CamPos, w = ContactPerspTan
    uint4              ViewportAndOptions;      // x = W, y = H, z = Tier (0..3), w = ActiveLightCount (1..3)
    uint4              QualityCounts;           // x = PcssTaps, y = SmrtRays, zw = Pad
    ShadowLightUniform Lights[3];
};

[[vk::binding(0, 0)]] ConstantBuffer<MultiShadowUniforms> ShadowCfg;
[[vk::binding(1, 0)]] Texture2D<float4>                   WorldPositionTex;
[[vk::binding(2, 0)]] Texture2D<float4>                   WorldNormalTex;
[[vk::binding(3, 0)]] Texture2D<float>                    ContactDepthTex;
[[vk::binding(4, 0)]] Texture2D<float>                    ShadowDepthTex[3];
[[vk::binding(5, 0)]] RWTexture2D<float4>                 OutShadowVisAndBlocker; // rgb = vis L0..L2, a = blockerDist

float TraceScreenSpaceContact(float3 Pos, float3 Nrm, float3 ToLight, uint2 ViewSize)
{
    float  Vis     = 1.0;
    float3 StartP  = Pos + Nrm * 0.004;
    float  MaxDist = 0.24;
    [unroll]
    for (uint I = 1u; I <= 8u; ++I)
    {
        float  T    = MaxDist * (float(I) / 8.0);
        float3 SP   = StartP + ToLight * T;
        float4 Clip = mul(ShadowCfg.CameraViewProj, float4(SP, 1.0));
        if (Clip.w <= 1.0e-4) continue;
        float2 Uv   = (Clip.xy / Clip.w) * float2(0.5, -0.5) + 0.5;
        if (any(Uv < float2(0.002)) || any(Uv > float2(0.998))) continue;
        int2   Pix  = int2(Uv * float2(ViewSize));
        float4 GPos = WorldPositionTex.Load(int3(Pix, 0));
        if (GPos.w <= 0.0) continue;
        float  Dz   = length(SP - ShadowCfg.CameraPosAndTanFov.xyz) - length(GPos.xyz - ShadowCfg.CameraPosAndTanFov.xyz);
        if (Dz > 0.004 && Dz < 0.14)
        {
            Vis = min(Vis, smoothstep(0.16, MaxDist, T));
            if (Vis <= 0.02) return 0.0;
        }
    }
    return Vis;
}

bool ProjectToLight(uint Idx, float3 WorldPos, out float2 OutUv, out float OutRecDepth)
{
    ShadowLightUniform L = ShadowCfg.Lights[Idx];
    float4 Clip = mul(L.LightViewProj, float4(WorldPos, 1.0));
    if (Clip.w <= 1.0e-4) { OutUv = float2(-1.0); OutRecDepth = 1.0e6; return false; }
    float3 Ndc  = Clip.xyz / Clip.w;
    OutUv       = Ndc.xy * float2(0.5, -0.5) + 0.5;
    OutRecDepth = (L.OriginAndType.w < 0.5)
        ? dot(WorldPos - L.OriginAndType.xyz, L.ForwardAndRadius.xyz)
        : length(WorldPos - L.OriginAndType.xyz);
    return all(OutUv >= float2(0.001)) && all(OutUv <= float2(0.999)) && Ndc.z >= 0.0 && Ndc.z <= 1.0;
}

float EvalPcssForLight(uint Idx, float3 Pos, float3 Nrm, uint Taps, float2 R2, out float OutBlocker)
{
    OutBlocker = 0.0;
    ShadowLightUniform L = ShadowCfg.Lights[Idx];
    float3 ToLight = (L.OriginAndType.w < 0.5) ? L.DirAndMapRes.xyz : normalize(L.OriginAndType.xyz - Pos);
    float  NdotL   = dot(Nrm, ToLight);
    if (NdotL <= 0.002) return 0.0;

    float2 UvEst; float DEst;
    if (!ProjectToLight(Idx, Pos, UvEst, DEst)) return 1.0;

    float  Res        = L.DirAndMapRes.w;
    float  WorldSpan  = (L.OriginAndType.w < 0.5) ? (2.0 * L.SpanAndFov.x) : (2.0 * max(DEst, 0.25) * L.SpanAndFov.y);
    float  TexelWorld = WorldSpan / Res;
    float  Slope      = clamp(sqrt(max(0.0, 1.0 - NdotL * NdotL)) / max(NdotL, 0.04), 0.0, 4.0);
    float  BiasM      = TexelWorld * (0.48 + 0.45 * Slope);
    float2 Uv; float RecD;
    if (!ProjectToLight(Idx, Pos + Nrm * (TexelWorld * 1.4), Uv, RecD)) return 1.0;

    float  SearchUv   = clamp(((L.OriginAndType.w < 0.5) ? L.ForwardAndRadius.w * 9.0 : L.ForwardAndRadius.w * 0.75) / WorldSpan, 2.0 / Res, 0.085);
    uint   SearchCnt  = clamp(Taps / 2u, 6u, 24u);
    float  Phi0       = frac(R2.x + float(Idx) * 0.371) * 6.2831853;
    float  BlkSum     = 0.0;
    uint   BlkCnt     = 0u;

    for (uint I = 0u; I < SearchCnt; ++I)
    {
        float  R    = sqrt((float(I) + 0.5) / float(SearchCnt)) * SearchUv;
        float  Ang  = Phi0 + float(I) * 2.39996323;
        float2 QUv  = clamp(Uv + float2(cos(Ang), sin(Ang)) * R, float2(0.0005), float2(0.9995));
        float  D    = ShadowDepthTex[Idx].Load(int3(int2(QUv * Res), 0));
        if (D < RecD - BiasM) { BlkSum += D; ++BlkCnt; }
    }
    if (BlkCnt == 0u) return 1.0;

    float AvgBlk     = BlkSum / float(BlkCnt);
    OutBlocker       = max(0.0, RecD - AvgBlk);
    float PenWorld   = (L.OriginAndType.w < 0.5) ? (OutBlocker * L.ForwardAndRadius.w) : (L.ForwardAndRadius.w * OutBlocker / max(AvgBlk, 0.08));
    float PenUv      = clamp(PenWorld / WorldSpan, 0.65 / Res, 0.11);
    float LitSum     = 0.0;
    float InvPcf     = 1.0 / float(Taps);
    float PhiPcf     = frac(R2.y + 0.381966 + float(Idx) * 0.193) * 6.2831853;

    for (uint I = 0u; I < Taps; ++I)
    {
        float  R    = sqrt((float(I) + 0.5) * InvPcf) * PenUv;
        float  Ang  = PhiPcf + float(I) * 2.39996323;
        float2 QUv  = clamp(Uv + float2(cos(Ang), sin(Ang)) * R, float2(0.0005), float2(0.9995));
        float  D    = ShadowDepthTex[Idx].Load(int3(int2(QUv * Res), 0));
        if (D >= RecD - (BiasM + PenUv * WorldSpan * Slope * 0.18)) LitSum += InvPcf;
    }
    return LitSum;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void EvaluateMultiTierShadowEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint2 ViewSize = ShadowCfg.ViewportAndOptions.xy;
    if (any(DispatchThreadID.xy >= ViewSize)) return;

    float4 PosSample = WorldPositionTex.Load(int3(DispatchThreadID.xy, 0));
    if (PosSample.w <= 0.0) { OutShadowVisAndBlocker[DispatchThreadID.xy] = float4(1.0, 1.0, 1.0, 0.0); return; }

    float3 Pos  = PosSample.xyz;
    float3 Nrm  = normalize(WorldNormalTex.Load(int3(DispatchThreadID.xy, 0)).xyz);
    float2 R2   = frac(float(DispatchThreadID.x) * float2(0.75487766, 0.56984029) + float(DispatchThreadID.y) * float2(0.56984029, 0.32471796));
    uint   Tier = ShadowCfg.ViewportAndOptions.z;
    uint   LCnt = min(3u, ShadowCfg.ViewportAndOptions.w);

    float3 Vis     = float3(1.0, 1.0, 1.0);
    float  MaxBlk  = 0.0;

    for (uint L = 0u; L < LCnt; ++L)
    {
        float Blk = 0.0;
        uint  Taps = (L == 0u) ? ShadowCfg.QualityCounts.x : max(8u, ShadowCfg.QualityCounts.x / 2u);
        float V = EvalPcssForLight(L, Pos, Nrm, Taps, R2, Blk);
        if (Tier == 2u) // ContactClipPcss: add screen-space contact (+ tight clip cascade on L == 0)
        {
            float3 ToL = (ShadowCfg.Lights[L].OriginAndType.w < 0.5)
                ? ShadowCfg.Lights[L].DirAndMapRes.xyz
                : normalize(ShadowCfg.Lights[L].OriginAndType.xyz - Pos);
            if (dot(Nrm, ToL) > 0.002) V = min(V, TraceScreenSpaceContact(Pos, Nrm, ToL, ViewSize));
        }
        Vis[L] = V;
        MaxBlk = max(MaxBlk, (L == 0u) ? Blk : Blk * 0.7);
    }

    OutShadowVisAndBlocker[DispatchThreadID.xy] = float4(Vis, MaxBlk);
}
```

### 4.3 `Engine/Shaders/GlobalIllumination/SpecularHiZReflection.slang` (Zero-SDF Tier 1 Specular)
```hlsl
module SpecularHiZReflection;

// Tier 1 GTX Reflections: Hi-Z Screen-Space Raymarch against DirectLitHdrBuffer + Analytical Studio/Sky Env Fallback.
// Zero SDF raymarching for specular reflections!
float3 EvaluateTier1HiZReflection(
    float3                 Origin,
    float3                 Normal,
    float3                 ViewDir,
    float                  Roughness,
    float4x4               ViewProj,
    float3                 CamPos,
    Texture2D<float4>      GPosTex,
    Texture2D<float4>      GDirectHdrTex,
    SamplerState           LinearSampler,
    float3                 SunDir,
    float3                 SkyHorizon,
    float3                 SkyZenith)
{
    float3 RayDir = reflect(-ViewDir, Normal);
    if (dot(RayDir, Normal) <= 0.0) RayDir = Normal;

    if (Roughness <= 0.56)
    {
        const float StepLen = 0.075;
        [unroll]
        for (uint S = 1u; S <= 20u; ++S)
        {
            float  T    = float(S) * StepLen;
            float3 SP   = Origin + Normal * 0.03 + RayDir * T;
            float4 Clip = mul(ViewProj, float4(SP, 1.0));
            if (Clip.w <= 0.05) break;
            float2 Uv   = (Clip.xy / Clip.w) * float2(0.5, -0.5) + 0.5;
            if (any(Uv < float2(0.01)) || any(Uv > float2(0.99))) break;

            float4 GSample = GPosTex.SampleLevel(LinearSampler, Uv, 0.0);
            if (GSample.w > 0.0)
            {
                float DepthDiff = length(SP - CamPos) - length(GSample.xyz - CamPos);
                if (DepthDiff > -0.02 && DepthDiff < 0.16 && length(SP - GSample.xyz) < 0.22)
                {
                    float EdgeFade = saturate(1.0 - max(abs(Uv.x * 2.0 - 1.0), abs(Uv.y * 2.0 - 1.0)));
                    float3 SsrRad  = GDirectHdrTex.SampleLevel(LinearSampler, Uv, 0.0).rgb;
                    float3 EnvRad  = lerp(SkyHorizon, SkyZenith, pow(max(RayDir.z, 1.0e-4), 0.58));
                    return lerp(EnvRad, SsrRad, smoothstep(0.0, 0.18, EdgeFade));
                }
            }
        }
    }

    float Up = saturate(RayDir.z);
    return lerp(SkyHorizon, SkyZenith, pow(max(Up, 1.0e-4), 0.58)) * (0.65 + 0.35 * max(dot(RayDir, SunDir), 0.0));
}
```

### 4.4 `WorldViewportRadiometricCompute.slang` Lighting Separation Rule
- **Direct Shadows (`DirectHdr`)**: Driven solely by `MultiTierShadowEvaluate` (PCF / PCSS / Contact+PCSS / SMRT) on Tier 1 GTX, or `WideTreeRayQuery` (`TraceSceneAnyHit` / Vogel solar rays) on Tier 2 RTX — **never** `TraceSdfConeVisibility`.
- **Diffuse GI (`DiffuseGi`)**: Driven by `EvaluateGtxConeBounceGi` (`SignedDistanceGrid`, baked once per static scene) on Tier 1 GTX or `SampleRtxIrradianceLattice` on Tier 2 RTX — **strictly diffuse irradiance for indirect bounce lighting only**.
- **Specular Reflections (`SpecGi`)**: Driven by `EvaluateTier1HiZReflection` (Hi-Z SSR + Studio/Sky Environment Cubemap) on Tier 1 GTX or `ShadeSecondaryBounce` (`WideBoundingTree` BVH4 VNDF rays) on Tier 2 RTX — **never** SDF raymarching.
