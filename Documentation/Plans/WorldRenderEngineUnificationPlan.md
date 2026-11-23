# World Render Engine Unification Plan (5 Engines + Shadow Lab + BVH Reflections)

> 🔴 **SUPERSEDED — GI / SDF REMOVED (`63db120`) & PRESERVED IN [`Experimental/RemovedSdfGi/`](../../Experimental/RemovedSdfGi/README.md) FOR CLEAN REDO:** Active `WorldRenderEngine` modes in `Engine/GeometricRaster/` are currently `MatCap` (`0u`) and `OpenPbrDirect` (`1u`). SDF shadows, SDF GI, and emissive GI are queued for ground-up re-architecture (`BKL-012`) from `Experimental/RemovedSdfGi/`.

**Status:** Proposed Architecture Plan (Pre-Implementation Review)  
**Target Modules:** `Engine/GeometricRaster/`, `Engine/Shaders/`, `Engine/Components/Panes/`, `Engine/Lifecycle/`, `Engine/Workspaces/Material/`, `Exhibits/`  
**Reference Proof:** `Experimental/SdfLightingDemoCpp/` (5-engine verification committed at `a6f5eca`) + `Experimental/ShadowAlgorithmLab/` + `Experimental/DeterministicPathTracer/`  
**Hard Invariants:**
- **Zero SDF Shadows & Zero SDF Reflections** — remove `TraceSdfConeVisibility` from solar shadow penumbras and reflections; use Shadow Algorithm Lab (`ContactPcss`, `Pcss`, `Smrt`, `Pcf`, `WideTreeRayQuery`) + `WideBoundingTree` (BVH4) / Sky Reflections everywhere.
- **SDF Strictly for GI Only** — `SignedDistanceVolume` + 3D Radiosity Mip-Volume is used exclusively by the `SdfGlobalIllumination` engine.
- **Strict Slang (`.slang`) Shaders + Zero-`And` + Zero Banned Vocabulary** on all newly introduced symbols, shaders, and files.
- **Non-Degrading Quality Gate:** Zero checkerboarding or half-pixel skipping (`<= 5%` pixel delta against reference, verified via `Exhibits/Images/WorldRenderEngines/*.png`).

---

## 1. Current Engine Inventory & Architectural Gaps

| Render Engine | Existing Code in `Engine/` | Missing Pieces / Architectural Mismatches |
| :--- | :--- | :--- |
| **1. `MatCap`** | `Engine/Shaders/GeometricRaster/MatCapEvaluation.slang` (`SampleBakedMatCap`, `SampleStudioMatCapMaterial`, `EvaluateStudioSurfaceMaterial`), imported by `SurfaceShading.slang` (UV/Fracture viewports). | Not wired into `TieredWorldRenderSequence`, `VulkanWorldRenderSequence`, `WorldViewportRadiometricCompute.slang`, or `ViewportPanel` engine selection. |
| **2. `OpenPbrDirect` (PBR without GI)** | `MaterialPreviewRenderer` (`PreviewRenderMode::DirectStudioOpenPbr`), `LinearCosineLightIntegration.{h,cpp}`, `EvaluateSurfaceBrdf` + `EvaluateAreaQuadLight` (LTC) in `WorldViewportRadiometricCompute.slang`. | No dedicated GI-off engine mode in `TieredWorldRenderSequence`; `SolarShadowSequence` (`Tier1GtxCalibrated`) still uses SDF cone shadows (`TraceSdfConeVisibility`) instead of Shadow Lab algorithms. |
| **3. `DdgiRaytracing`** | `IrradianceLatticeSequence.{h,cpp}` (`OctahedralIrradianceCell` 8×8 irradiance + 16×16 Chebyshev depth, `SampleChebyshevIrradianceOverPi`), `OctahedralIrradianceLattice.slang`, `SampleRtxIrradianceLattice`. | Currently `SampleRtxIrradianceLattice` in `WorldViewportRadiometricCompute.slang` omits the Chebyshev depth-moment anti-leak weighting that CPU `SampleChebyshevIrradianceOverPi` already implements; solar shadows lack Shadow Lab parity modes. |
| **4. `SdfGlobalIllumination`** | `IrradianceLatticeSequence::EvaluateGtxSignedDistanceConeGi`, `SignedDistanceConeIllumination.slang`, `EvaluateGtxConeBounceGi` in `WorldViewportRadiometricCompute.slang`. | Currently hardcodes Cornell wall heuristics (`LeftProx`/`RightProx`) instead of a general 3D Radiosity SDF Mip-Volume + IGN cone march + joint normal/depth bilateral filter; `SignedDistanceVolume` is also leaking into shadows (`TraceSdfConeVisibility`). |
| **5. `DeterministicPathTracer`** | Enum placeholder `PreviewRenderMode::DeterministicPathTracer` in `MaterialPreviewRenderer.h` & `MaterialViewportHost.cpp` (`"Path Tracer"`); validated design in `Exhibits/DeterministicPathTracer/`. | Not implemented in `Engine/GeometricRaster/` or `Engine/Shaders/` yet (missing MIS/NEE integrator, Heitz 2018 GGX VNDF sampling, progressive accumulation buffer, and A-Trous edge-stopping denoiser). |

---

## 2. Unified Engine & Shadow Architecture

### 2.1 Core Enumerations (`TieredWorldRenderSequence.h` & `SolarShadowSequence.h`)

Preserve `WorldRenderTier` (`Tier1GtxCalibrated`, `Tier2RtxDeterministic`) as compatibility aliases/mappings while introducing the explicit 5-engine selector and shadow/reflection selectors:

```cpp
enum class WorldRenderEngine : uint8_t
{
    MatCap                 = 0u, // View-space studio MatCap shading on primary BVH4/Visibility hits
    OpenPbrDirect          = 1u, // Direct OpenPBR + LTC Area Lights + Shadow Lab + BVH4/Sky Reflections (Zero GI)
    DdgiRaytracing         = 2u, // Raytraced 3D Octahedral Irradiance Lattice + Chebyshev Depth + Shadow Lab + BVH4 Reflections
    SdfGlobalIllumination  = 3u, // 3D Radiosity SDF Mip-Volume Cone GI + Bilateral Filter (SDF strictly GI-only) + Shadow Lab + BVH4 Reflections
    DeterministicPathTrace = 4u, // Progressive R2/PCG MIS + NEE + Heitz GGX VNDF + A-Trous Wavelet Denoiser
};

enum class SolarShadowAlgorithm : uint8_t
{
    ContactPcss      = 0u, // Orthographic depth32f PCSS + PlaneAwareFilter + short BVH4 contact ray
    PcssPlaneAware   = 1u, // Orthographic depth32f Blocker Search + Vogel PCSS + PlaneAwareFilter
    ShadowRayMarch   = 2u, // EEVEE-Next quadratic depth-buffer raymarch (SMRT)
    PcfBilinear      = 3u, // 3x3 tent-weighted PCF over depth32f
    WideTreeRayQuery = 4u, // Multi-ray Vogel disc BVH4 / VK_KHR_ray_query traversal
};

enum class SpecularReflectionSource : uint8_t
{
    WideTreeRaytrace = 0u, // Multi-bounce BVH4 / VK_KHR_ray_query + Sky fallback (Zero SDF!)
    SkyEnvironment   = 1u, // Analytic sky/studio environment lookup only
};
```

### 2.2 Mapping Compatibility (`WorldRenderTier` & `PreviewRenderMode`)
- `WorldRenderTier::Tier1GtxCalibrated` maps to `WorldRenderEngine::SdfGlobalIllumination` + `SolarShadowAlgorithm::ContactPcss` + `SpecularReflectionSource::WideTreeRaytrace`.
- `WorldRenderTier::Tier2RtxDeterministic` maps to `WorldRenderEngine::DdgiRaytracing` + `SolarShadowAlgorithm::WideTreeRayQuery` + `SpecularReflectionSource::WideTreeRaytrace`.
- `MaterialPreviewRenderer::PreviewRenderMode` (`DirectStudioOpenPbr`, `RealTimeGiSequence`, `DeterministicPathTracer`) delegates directly to the shared `OpenPbrDirect`, `DdgiRaytracing`/`SdfGlobalIllumination`, and `DeterministicPathTrace` paths so Material Studio and World Viewport share one calibrated radiometric core.

---

## 3. Subsystem-by-Subsystem Implementation Design

### Phase A — Shadow Algorithm Lab Port (`Engine/GeometricRaster/DirectLighting/` + `Engine/Shaders/DirectLighting/`)
1. **Remove SDF Shadows Everywhere**:
   - Retire `TraceSdfConeVisibility` solar shadow calls from `WorldViewportRadiometricCompute.slang` and `SolarShadowSequence.cpp`.
   - Keep `SignedDistanceVolume` completely isolated from `SolarShadowSequence`.
2. **Unified `depth32float` Orthographic & Perspective Shadow Atlas**:
   - Extend `OrthographicShadowCascade` in `SolarShadowSequence.{h,cpp}` + `SolarFrustumShadow.slang` + `PlaneAwareShadowFilter.slang` to support all 5 `SolarShadowAlgorithm` modes (`ContactPcss`, `PcssPlaneAware`, `ShadowRayMarch`, `PcfBilinear`, `WideTreeRayQuery`) for both Solar Directional light and Area Perspective light occlusion (`LinearCosineLightIntegration`).
   - Add a GPU shadow-depth rasterization/dispatch buffer (`ShadowDepthBuffer` in `VulkanWorldRenderSequence`) so `WorldViewportRadiometricCompute.slang` evaluates identical `depth32float` `PCF`, `PCSS`, `SMRT`, `ContactPcss`, and `BVH4` shadows on the GPU.

### Phase B — Specular & Glass Reflections (`Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.*` + `SpecularGlassRaytrace.slang`)
1. **Remove SDF / Hi-Z-SDF Fallbacks**:
   - Route both Tier 1 & Tier 2 specular reflections through `WideTreeRaytrace` (BVH4 multi-bounce `ShadeSecondaryBounce` on GPU / `SceneTree` on CPU) with `SkyEnvironment` fallback.
   - Preserve 2-layer Abbe-dispersed crown glass refraction (`TraceThroughGlassSphere`) across `OpenPbrDirect`, `DdgiRaytracing`, `SdfGlobalIllumination`, and `DeterministicPathTrace`.

### Phase C — Five Render Engines (CPU Reference + Vulkan `.slang` Compute)

| Engine | CPU Path (`TieredWorldRenderSequence.cpp`) | GPU `.slang` Path (`WorldViewportRadiometricCompute.slang` + Modular `.slang`) |
| :--- | :--- | :--- |
| **1. `MatCap`** | View-space normal evaluation matching `MatCapEvaluation.slang` (`EvaluateMatCapSurface` with studio key/fill/rim + material tint & roughness response). | Calls `MatCapEvaluation.slang` directly from `WorldViewportRadiometricCompute.slang` (with procedural fallback when `BakedMatCapAtlas` is unbound). |
| **2. `OpenPbrDirect`** | `VisibilityMaterialResolve` + `LinearCosineLightIntegration` (with selected `SolarShadowAlgorithm`) + `SpecularReflectionSequence` (`WideTreeRaytrace`) + flat sky ambient (GI skipped). | Primary BVH4 hit -> `EvaluateSurfaceBrdf` + `EvaluateAreaQuadLight` + selected `SolarShadowAlgorithm` + `ShadeSecondaryBounce` specular (GI skipped). |
| **3. `DdgiRaytracing`** | `IrradianceLatticeSequence` (12×9×7 / configurable cells, 128 Fibonacci rays, 2+ bounces) + `SampleChebyshevIrradianceOverPi` + Shadow Lab + BVH4 reflections. | Upgrade `SampleRtxIrradianceLattice` in `WorldViewportRadiometricCompute.slang` & `OctahedralIrradianceLattice.slang` to upload & sample the `16×16` `ChebyshevDepth` moments buffer alongside `8×8` irradiance texels to prevent light leaking through walls/pillars. |
| **4. `SdfGlobalIllumination`** | Build 3-mip `SignedDistanceRadiosityVolume` (voxel SDF + direct BVH-shadowed surface/emissive radiosity) -> 16-ray IGN cone trace -> `7×7` normal/depth joint bilateral filter (`ApplyGiBilateralFilter`). | Upgrade `SignedDistanceGridCompute.slang` + `SignedDistanceConeIllumination.slang` + `WorldViewportRadiometricCompute.slang` to store packed radiosity alongside SDF voxels, perform IGN-rotated cone tracing, and run a joint bilateral spatial filter pass (`WorldGiBilateralCompute.slang`). |
| **5. `DeterministicPathTrace`** | Progressive R2/PCG Monte Carlo Path Tracer with MIS + NEE (Sun, LTC Area Quads, Emissive Primitives), Heitz (2018) GGX VNDF (`SampleGgxVndf`), isolated firefly clamp, and 3-pass albedo/normal/depth A-Trous denoiser. | New dedicated compute module `Engine/Shaders/GlobalIllumination/WorldPathTraceCompute.slang` + `WorldATrousDenoiseCompute.slang` invoked by `VulkanWorldRenderSequence` with persistent HDR accumulation buffer (`AccumulationHdrBuffer`) resetting on camera/scene change. |

### Phase D — UI & Lifecycle Integration (`ViewportPanel.{h,cpp}`, `EditorLifecycle.{h,cpp}`, `MaterialPreviewRenderer.{h,cpp}`)
1. **Extend `ViewportDisplaySettings` (`ViewportPanel.h`)**:
   - Add `WorldRenderEngine ActiveEngine = WorldRenderEngine::SdfGlobalIllumination;`
   - Add `SolarShadowAlgorithm ShadowAlgorithm = SolarShadowAlgorithm::ContactPcss;`
   - Add `SpecularReflectionSource ReflectionSource = SpecularReflectionSource::WideTreeRaytrace;`
   - Add `uint32_t PathTraceTargetSamples = 32u;` and `bool PathTraceDenoiseEnabled = true;`
2. **Expose Controls in `ViewportPanel::RenderSettingsSheet` (`ViewportPanel.cpp`)**:
   - Replace/augment the settings sheet using existing `DropdownSelector` / `CapsuleButton` / `ExclusiveSelector` controls in `PixelSpace`:
     - **Render Engine selector**: `MatCap`, `OpenPBR (No GI)`, `DDGI Raytracing`, `SDF GI`, `Path Tracer`
     - **Shadow Algorithm selector**: `Contact PCSS`, `PCSS + PlaneFilter`, `SMRT`, `PCF`, `BVH4 Ray`
     - **Reflection Source toggle**: `BVH4 Raytrace` vs `Sky Environment`
     - Retain `QualityTier`, `RenderScalePercent`, `DebugChannelIndex`, and progressive sample status in `OutlinerFooterReadings`.
3. **Wire `EditorLifecycle` (`EditorLifecycle.cpp`)**:
   - Pass `DisplayCfg.ActiveEngine`, `DisplayCfg.ShadowAlgorithm`, and `DisplayCfg.ReflectionSource` to both `GpuWorldRenderer->RenderWorldViewportRgba8(...)` and CPU fallback `ShowcaseRenderer->RenderWorldFrame(...)`.
   - Maintain progressive accumulation frame index (`PathTraceSampleIndex`) inside `EditorLifecycle`, invalidating automatically when `CameraChanged` or `SceneMutated` or `DisplayChanged`.

---

## 4. Verification & Exhibits Gate

1. **Build & Compile Shaders**:
   - Compile all modified/new `.slang` modules via `python3 Automation/PrepareShaderToolchain.py` (`slangc` -> SPIR-V 1.6).
   - Build `Editor`, `Development`, and `Shipping` CMake targets (`Engine/Editor/` excluded in `Development`/`Shipping`).
2. **Automated Validation & Visual Proofs (`Exhibits/TieredWorldRenderValidation.cpp` + `Exhibits/ProjectBrowserValidation.cpp`)**:
   - Extend `Exhibits/TieredWorldRenderValidation.cpp` to render deterministic `.png` captures for all 5 engines (`MatCap`, `OpenPbrDirect`, `DdgiRaytracing`, `SdfGlobalIllumination`, `DeterministicPathTrace`) plus Shadow Lab comparison (`ContactPcss` vs `ShadowRayMarch` vs `PcfBilinear` vs `WideTreeRayQuery`) into `Exhibits/Images/WorldRenderEngines/`.
   - Visually inspect every generated `.png` and `Editor` viewport capture (`--frames=4 --capture-png=...`) before finalizing and committing.
3. **Follow-Up Phase (Deferred Until Engine Verification Passes)**:
   - Port physical atmosphere scattering, volumetric clouds, solar/lunar discs, and starfield (`Experimental/FrontierWorldMirror/AtmosphereSky.js` & `MoonAtlasLoader.js`) into `Engine/GeometricRaster/Atmosphere/` + `.slang`.
