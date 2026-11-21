# 🧩 World Rendering Audit Remediation & PROOF 11 Emissive Penumbra — Completion Report

> **TL;DR**: Executed every workstream (`1`–`6`) of [`WorldRenderingAuditRemediationPlan.md`](WorldRenderingAuditRemediationPlan.md) on branch `arena/28ada475-frontier` (fast-forwarded cleanly atop `e4966e4` / `ad7fa01c`) with **zero placeholders**, strict [`BannedWordsVocabulary.md`](../Governance/BannedWordsVocabulary.md) enforcement across all newly authored C++20 & `.slang` code, **all 20 `WorldMirrorValidation.mjs` suites passing**, and **100% visual `.png` inspection** across all `18` rendered exhibits:
> - **Workstream 1 (`49a8d3d`) — `PROOF 11: SDF Area-Light Penumbrae` (`download.png` Reference Match)**: Implemented `SdfEmissivePenumbraSequence.{h,cpp}`, `SdfEmissivePenumbraCompute.slang` (`SPIR-V 1.3`), and `SdfEmissivePenumbraValidation` producing a 6-panel `1920x1080` contact sheet (`00-Proof11_SdfEmissivePenumbraeSheet.png`) plus `7` standalone `640x480` proof frames (`01`–`07`), coupling physical multi-aperture 3D SDF voxel cone tracing (`VoxelSdfPenumbraAndConeGi`) with zero wall light leaks (`0.000000`), deep umbra (`0.0000`), `1.116 m` monotonic penumbra width, `0.0288` RMSE against a `96-spp` Monte Carlo reference, and `0.0146` Vulkan GPU vs CPU RMSE.
> - **Workstreams 2 & 3 (`d12b430`) — Order 2 Real-Time GI & Specular/Glass/Capsule Root-Cause Remediation**: Upgraded `HybridConeShadowSequence` (6-mode `SdfPenumbraMode` + `HighEndZeroSdfTier` `BVH4` area visibility), `StokesContourIrradianceSequence` (`19: 3 + 16` dual-cascade Stokes contour + 2nd-bounce `BVH4` irradiance), `SurfelRadiosityLatticeSequence` (`32x16x32` spatial-hashed 2-bounce surfel lattice + octahedral `6x6` lookup), `SpecularReflectionSequence.{h,cpp}` + `SpecularGlassRaytrace.slang` (full-roughness `[0..1]` Heitz VNDF `BVH4` reflection + dual-interface Beer-Lambert + 3-channel Cauchy/Abbe glass refraction), `ArticulatedCapsuleRig` (`16` human capsules + `4` quadruped capsules), and `GtxSdfGiShadowValidation` (`0.0523`–`0.0716` HDR RMSE vs `64-spp` reference with **zero denoiser**).
> - **Workstream 4 (`5b7936d`) — Order 3 Path Tracer & Live 6-Engine Editor Viewport Routing**: Upgraded `DeterministicRadianceIntegration.slang` to Heitz (2018) Spherical-Cap GGX VNDF, raised `02-Hero_SceneFinalRender.png` to `128 spp`, decoupled `TieredWorldRenderSequence` Order 3 (`PathTracerGameRealtime` & `PathTracerSceneFinal`) to synchronize arbitrary `WorldScene` geometry/materials/lights/camera with `0 B` SDF while cleanly restoring the `25,068`-tri `PrototypeVehicle` studio scene, wired all `6` `WorldRenderEngine` modes + `6` `SdfPenumbraMode` options in `VulkanWorldRenderSequence` + `WorldViewportRadiometricCompute.slang` + `EditorLifecycle`, and removed the redundant `SDF Shadow` dropdown from `DeterministicPathTracerPane`.
> - **Workstream 5 (`95943d7`) — `Experimental/FrontierWorldMirror` Full 3-Order / 6-Engine Parity**: Synced `RENDER_ENGINES` (`0..5`) and `SDF_PENUMBRA_MODES` (`0..5`) across `ShowcaseGalleryScene.js`, `ShadowPipeline.js`, `Inspectors.jsx`, `WebgpuGtxRenderer.js` (WGSL), `GtxWorldViewport.jsx` (WebGL2 GLSL), and `CpuGtxFallbackRenderer.js`, rebuilt standalone `Experimental/FrontierWorldMirror/index.html` (`5.29 MiB`), and verified **`20/20` suites** in `WorldMirrorValidation.mjs`.

---

## 1. Milestone Summary & Architectural Invariants

| Workstream | Commit | Core Deliverables | SDF Memory Invariant | Validation Binary / Suite |
| :--- | :---: | :--- | :---: | :--- |
| **WS 1 — `PROOF 11: SDF Area-Light Penumbrae`** | `49a8d3d` | `SdfEmissivePenumbraSequence.{h,cpp}`, `SdfEmissivePenumbraCompute.slang`, `Exhibits/SdfEmissivePenumbraValidation.cpp` | `4-cascade 64^3` `R16F` clipmap + brick pool: **`4,340,028 B` (`4.14 MB`)** | `SdfEmissivePenumbraValidation` (`8` `.png` proofs) |
| **WS 2 & 3 — Order 2 GI + Specular / Glass / Capsules** | `d12b430` | `StokesContourIrradianceSequence.{h,cpp}`, `SurfelRadiosityLatticeSequence.{h,cpp}`, `SpecularReflectionSequence.{h,cpp}`, `ArticulatedCapsuleRig.h`, `HybridConeShadowSequence.cpp`, `ShowcaseGalleryScene.{h,cpp}`, 4 `.slang` compute shaders | `GtxLowEnd`: **`204,800 B`** (`<= 3.7 MB`)<br>`HighEnd`: **`0 B`** (`100% BVH4`) | `GtxSdfGiShadowValidation` (`5` `.png` exhibits) |
| **WS 4 — Order 3 Path Tracer & Live 6-Engine Viewport** | `5b7936d` | `DeterministicRadianceIntegration.slang` (VNDF), `TieredWorldRenderSequence.{h,cpp}`, `VulkanWorldRenderSequence.{h,cpp}`, `WorldViewportRadiometricCompute.slang`, `EditorLifecycle.cpp`, `DeterministicPathTracerPane.cpp` | Order 3 (`GameRealtime` & `SceneFinalRender`): **`0 B`** | `DeterministicPathTracerValidation` (`5` `.png` exhibits) |
| **WS 5 — `FrontierWorldMirror` 6-Engine + 6-Mode Parity** | `95943d7` | `ShowcaseGalleryScene.js`, `ShadowPipeline.js`, `Inspectors.jsx`, `WebgpuGtxRenderer.js`, `GtxWorldViewport.jsx`, `CpuGtxFallbackRenderer.js`, `WorldMirrorValidation.mjs`, `index.html` (`5.29 MiB`) | Browser WGSL / WebGL2 / CPU parity (`SHADOW_SDF` / `REFLECTION_SDF` absent) | `WorldMirrorValidation.mjs` (`20/20` suites passed) |

---

## 2. Workstream 1 — `PROOF 11: SDF Area-Light Penumbrae` (`SdfEmissivePenumbraValidation`)

### Quantitative Verification

| Metric | Measured Result | Gate | Status |
| :--- | :---: | :---: | :---: |
| 4-Cascade `64^3` `R16_SFLOAT` Clipmap + Brick Pool Footprint | **`4,340,028 B` (`4.14 MB`)** | `<= 6.0 MB` | 🟢 |
| Mode 0 (`PointSdfHard`) Floor Penumbra Width (`ScanX = 1.55 m`) | **`0.000 m`** | `< 0.16 m` (razor-sharp) | 🟢 |
| Mode 1 (`AnalyticSdfCone`) Floor Penumbra Width | **`1.212 m`** | `> 0.45 m` (wide soft penumbra) | 🟢 |
| Mode 2 (`VoxelClipmapCone`) Floor Penumbra Width | **`1.116 m`** | `> 0.45 m` | 🟢 |
| Mode 3 (`VoxelSdfPenumbraAndConeGi`) Penumbra Width & Coupled Indirect GI | **`1.116 m`** (`+Coupled 5-Cone Voxel GI`) | Coupled direct penumbra + warm/cool wall color bleed | 🟢 |
| Mode 4 (`HybridSobolSphericalCap`) Floor Penumbra Width | **`0.989 m`** | `> 0.45 m` (4-tap Owen-Sobol + `3x3` normal/depth filter) | 🟢 |
| Mode 5 (`SdfMonteCarloReference` `96 spp`) Floor Penumbra Width | **`1.030 m`** | Ground-truth area-luminaire visibility | 🟢 |
| Deep Umbra Minimum Visibility & Partition Wall Light Leakage | **`Umbra Min = 0.0000`**, **`Leak Max = 0.000000`** | `== 0.0` (zero wall light leaks) | 🟢 |
| `VoxelSdfPenumbraAndConeGi` vs `96-spp` MC Reference RMSE | **`0.0288`** | `< 0.065` | 🟢 |
| Vulkan `.slang` GPU (`SdfEmissivePenumbraCompute.spv`) vs CPU Parity RMSE | **`0.0146`** | `< 0.025` | 🟢 |

### Rendered Exhibits (`Exhibits/Images/SdfEmissivePenumbra/`)

| `00-Proof11_SdfEmissivePenumbraeSheet.png` (`1920x1080` 6-Panel Contact Sheet) | `07-VoxelSdfPenumbraAndConeGiGpu.png` (`640x480` Vulkan `.slang` GPU) |
| :---: | :---: |
| ![Proof 11 Sheet](../../Exhibits/Images/SdfEmissivePenumbra/00-Proof11_SdfEmissivePenumbraeSheet.png) | ![GPU Voxel Cone GI](../../Exhibits/Images/SdfEmissivePenumbra/07-VoxelSdfPenumbraAndConeGiGpu.png) |
| **`01-PointSdfHard.png`** | **`04-VoxelSdfPenumbraAndConeGi.png`** |
| ![Point SDF Hard](../../Exhibits/Images/SdfEmissivePenumbra/01-PointSdfHard.png) | ![CPU Voxel Cone GI](../../Exhibits/Images/SdfEmissivePenumbra/04-VoxelSdfPenumbraAndConeGi.png) |
| **`05-HybridSobolSphericalCap.png`** | **`06-SdfMonteCarloReference96Spp.png`** |
| ![Hybrid Sobol Cap](../../Exhibits/Images/SdfEmissivePenumbra/05-HybridSobolSphericalCap.png) | ![MC Reference 96spp](../../Exhibits/Images/SdfEmissivePenumbra/06-SdfMonteCarloReference96Spp.png) |

---

## 3. Workstreams 2 & 3 — Order 2 Real-Time GI & Specular / Glass / Capsule Remediation (`GtxSdfGiShadowValidation`)

### Root Causes Fixed

1. **Direct Emissive + 2nd-Bounce Coupled GI (`StokesContourIrradianceSequence` & `SurfelRadiosityLatticeSequence`)**:
   - `StokesContourIrradianceSequence` now evaluates both exact boundary-contour direct emissive irradiance and `19`-ray (`3` coarse wide-hemisphere + `16` fine cosine-weighted) dual-cascade Owen-scrambled Sobol' `(0, 2)` `BVH4` 2nd-bounce sky/sun/emissive irradiance in both CPU C++20 and `StokesContourIrradianceCompute.slang`.
   - `SurfelRadiosityLatticeSequence` populates a `32 x 16 x 32` spatial-hashed 2-bounce surfel radiosity lattice (`1`st bounce direct sun + area lights + emissives, `2`nd bounce inter-surfel `BVH4` transfer) with octahedral `6x6` directional bins and horizon-clamped trilinear lookup (`SurfelRadiosityLatticeCompute.slang`).
2. **Full-Roughness `[0..1]` VNDF `BVH4` Reflections & Dual-Interface Abbe Glass (`SpecularReflectionSequence` + `SpecularGlassRaytrace.slang`)**:
   - Removed legacy roughness cutoffs (`Roughness > 0.55` / `0.85`): all dielectric, metallic, and clearcoat surfaces across `Roughness in [0, 1]` sample Heitz (2018) Spherical-Cap GGX VNDF (`1` ray for `R < 0.08`, `2` rays for `[0.08, 0.38)`, `4` Owen-Sobol rays for `R >= 0.38`) with secondary `BVH4` lighting and emissive firefly clamping on rough lobes.
   - Glass panes trace front-interface Fresnel reflection + back-interface exit geometry (`GlassPeelSecond64` or `8 mm` single-pane exit slab) with Beer-Lambert `exp(-sigma_a * d)` absorption and 3-channel RGB Cauchy/Abbe dispersion (`ComputeAbbeDispersedIor`).
3. **`HighEnd` Zero-SDF `BVH4` Luminaire Visibility & 20-Capsule Articulated Rig**:
   - `HighEndZeroSdfTier` evaluates per-triangle `BVH4` visibility against area luminaires (`Centroid + OutwardNormal * 0.015 m` to clear recessed lamp bezels) with **`0 B` SDF memory**.
   - Added a `16`-capsule humanoid rig (`Head, Neck, UpperTorso, Pelvis, 2x UpperArm/Forearm/Hand/Thigh/Shin/Foot`) and `4`-capsule quadruped rig in `ArticulatedCapsuleRig.h`.

### Quantitative Verification (`GtxSdfGiShadowValidation`)

| Metric | Measured Result | Gate | Status |
| :--- | :---: | :---: | :---: |
| Character `CompressedWideTree` (`1,400` tris, `193` `64 B` nodes) Bottom-Up Refit | **`0.2135 ms`** | `< 0.50 ms` | 🟢 |
| `GtxLowEnd` Local `C0` SDF Footprint | **`204,800 B` (`200 KB`)** | `<= 3,700,000 B` (`3.7 MB`) | 🟢 |
| `HighEnd` SDF Footprint (Pose A & Pose B) | **`0 B` / `0 B`** | Exact **`0 B`** | 🟢 |
| **Zero-Denoiser** `GtxLowEnd (StokesContour)` vs `64-spp` Reference RMSE | **`HDR = 0.0716`**, **`GI = 0.0930`** | `< 0.14` | 🟢 |
| **Zero-Denoiser** `GtxLowEnd (SurfelLattice)` vs `64-spp` Reference RMSE | **`HDR = 0.0697`**, **`GI = 0.1044`** | `< 0.14` | 🟢 |
| **Zero-Denoiser** `HighEnd (0-SDF BVH4 + DualCascade 19)` vs `64-spp` Reference RMSE | **`HDR = 0.0523`**, **`GI = 0.0716`** | `< 0.10` (`HighEnd <= GtxLowEnd`) | 🟢 |
| Dynamic Character Pose A -> Pose B Mean Pixel Delta (`0 B` SDF Rebake) | **`0.0562`** | `> 0.008` | 🟢 |

### Rendered Exhibits (`Exhibits/Images/GtxSdfGi/`)

| `01-GtxLowEnd_SdfCone_StokesGi.png` | `02-GtxLowEnd_SdfCone_SurfelLatticeGi.png` |
| :---: | :---: |
| ![GTX Stokes GI](../../Exhibits/Images/GtxSdfGi/01-GtxLowEnd_SdfCone_StokesGi.png) | ![GTX Surfel Lattice GI](../../Exhibits/Images/GtxSdfGi/02-GtxLowEnd_SdfCone_SurfelLatticeGi.png) |
| **`03-HighEnd_ZeroSdf_BvhShadow_PoseA.png`** | **`04-HighEnd_ZeroSdf_BvhShadow_PoseB.png`** |
| ![HighEnd Pose A](../../Exhibits/Images/GtxSdfGi/03-HighEnd_ZeroSdf_BvhShadow_PoseA.png) | ![HighEnd Pose B](../../Exhibits/Images/GtxSdfGi/04-HighEnd_ZeroSdf_BvhShadow_PoseB.png) |
| **`05-Reference_PathTraced_64Spp.png` (`64-spp` Unbiased Reference)** | |
| ![64-spp Reference](../../Exhibits/Images/GtxSdfGi/05-Reference_PathTraced_64Spp.png) | |

---

## 4. Workstream 4 — Order 3 Deterministic Path Tracer & Live 6-Engine Viewport (`DeterministicPathTracerValidation`)

### Quantitative Verification (`DeterministicPathTracerValidation`)

| Metric | Measured Result | Gate | Status |
| :--- | :---: | :---: | :---: |
| Owen-Scrambled Sobol' `(0, 2)` GPU vs Host IEEE-754 Bit-Exactness | **`0 / 147,456` mismatches** | Exact `0` | 🟢 |
| Softbox Lambertian Irradiance vs Analytic Contour Integral | **`0.232761` vs `0.232835` (`0.0321%` error)** | `< 0.10%` | 🟢 |
| `8-spp` IEEE-754 Raw Accumulation Digests (`batch = 1, 4, 8`) | **`fb7cdce2` / `fb7cdce2` / `fb7cdce2`** | Bit-identical across batch sizes | 🟢 |
| Subpixel `R32_UINT` Raster Visibility vs Primary `BVH4` Ray Trace RMSE | **`0.00028`** | `< 0.005` | 🟢 |
| `1-spp` `8-bit` MAE vs `32-spp` Converged (`Shared` vs `GameRealtime` Blue-Noise + A-Trous) | **`4.86` -> `1.96` levels** | Strict noise reduction | 🟢 |
| `50`-Vehicle `ControlVehicle` Fleet (`1,253,400` tris) `CompressedWideTree` Size | **`3,373` wide4 entries (`212 KB` total)** | `< 1.0 MB` | 🟢 |
| `228`-Luminaire `.ies` `PhotometricLightTree` Conty-Kulla Build Time | **`455` clusters in `0.30 ms`** | `< 2.0 ms` | 🟢 |
| `TieredWorldRenderSequence` Order 3 Custom `WorldScene` Sync & Studio Restore | **`0 B` SDF**, custom gallery synced & `25,068`-tri `PrototypeVehicle` restored | Lossless round-trip | 🟢 |

### Rendered Exhibits (`Exhibits/Images/DeterministicPathTracer/`)

| `01-WorkspaceInterface_CppParity.png` (`1440x860` Live C++ Workspace, No `SDF Shadow` Dropdown) | `02-Hero_SceneFinalRender.png` (`128-spp` Heitz VNDF Final Render) |
| :---: | :---: |
| ![Workspace Parity](../../Exhibits/Images/DeterministicPathTracer/01-WorkspaceInterface_CppParity.png) | ![128-spp Hero Final](../../Exhibits/Images/DeterministicPathTracer/02-Hero_SceneFinalRender.png) |
| **`03-Hero_GameRealtime1Spp.png` (`1-spp` Blue-Noise + Edge-Stopping A-Trous)** | **`04-Fleet50Vehicles_1250kTris.png` (`1,253,400` Triangles in `212 KB`)** |
| ![1-spp GameRealtime](../../Exhibits/Images/DeterministicPathTracer/03-Hero_GameRealtime1Spp.png) | ![50-Vehicle Fleet](../../Exhibits/Images/DeterministicPathTracer/04-Fleet50Vehicles_1250kTris.png) |
| **`05-Photometric228Lights_IesTree.png` (`228` `.ies` Luminaires)** | |
| ![228 IES Lights](../../Exhibits/Images/DeterministicPathTracer/05-Photometric228Lights_IesTree.png) | |

---

## 5. Workstream 5 — `Experimental/FrontierWorldMirror` 6-Engine & 6-Mode Penumbra Parity

- **6-Engine `RENDER_ENGINES` (`0..5`)**: `studio_matcap` (`0`), `direct_pbr` (`1`), `gtx_sdf_gi` (`2`, default), `highend_bvh_gi` (`3`), `pathtracer_realtime` (`4`), and `pathtracer_final` (`5`) exposed in `ShowcaseGalleryScene.js`, `Inspectors.jsx` (`World Render Engine`), `ShadowPipeline.js` (`packedEnginePenumbra`), `WebgpuGtxRenderer.js` (WGSL `uAreaShadowFwd.w`), `GtxWorldViewport.jsx` (WebGL2 GLSL), and `CpuGtxFallbackRenderer.js`.
- **6-Mode `SDF_PENUMBRA_MODES` (`0..5`)**: `point_sdf` (`0`), `analytic_cone` (`1`), `voxel_clipmap` (`2`), `voxel_cone_gi` (`3`, default), `hybrid_sobol_cap` (`4`), and `sdf_mc_ref` (`5`) wired end-to-end across WGSL, WebGL2 GLSL, and CPU fallback without reintroducing `SHADOW_SDF` or `REFLECTION_SDF`.
- **Standalone Bundle & Validation**: Rebuilt `Experimental/FrontierWorldMirror/index.html` (`5.29 MiB`) via `node Experimental/FrontierWorldMirror/Build.mjs` and verified **`ALL 20 SUITES PASSED`** via `node Experimental/FrontierWorldMirror/WorldMirrorValidation.mjs`.
