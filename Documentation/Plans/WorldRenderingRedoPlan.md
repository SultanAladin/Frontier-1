# 🧩 World Rendering Ground-Up Redo Plan

> **TL;DR**: First, purge every remaining SDF and 3D Volume SDF-baking unit from `Engine/` (archiving `GpuSdfBaker` and volume SDF `.slang` units into `Experimental/RemovedSdfGi/Source/BakingSdf/`) so `Engine/` starts from a 100% SDF-free baseline. Next, build in three strict orders: **(1st) Standalone Direct PBR (`MatCap` + `OpenPbrDirect`, zero GI, zero SDF)**, **(2nd) Real-Time GI (`GtxLowEnd` & `HighEnd` presets)** pairing **GI Candidates 1 + 2 (`StokesContourIrradianceSequence` + `SurfelRadiosityLatticeSequence`)** with **Shadow Candidate 1 (`HybridConeShadowSequence`) on `GtxLowEnd`** and **exact `BVH4` / `RayQuery` Shadows (`SolarFrustumShadow` + `PlaneAwareShadowFilter` + GPU Skinned `BVH4` Refit, zero SDF!) on `HighEnd`**, and **(3rd) Unified Deterministic Path Tracer (`DeterministicPathTraceSequence`)** serving both **Games** and **Actual Scene Rendering** through settings alone (`1:1` with `Experimental/DeterministicPathTracer/`).

---

## 1. Locked Architecture & Candidate Decisions (`Experimental/RemovedSdfGi/`)

> **Decision Legend** (`SKILL-Emoji.md` §5): **Accuracy / Thin-Feature Fidelity**: `⭐⭐⭐` = best; **Real-Time Cost / VRAM**: `✔️` = lowest/fastest, `🚩` = moderate, `🔴` = heaviest; **Status**: `🥇` = primary selection, `🥈` = tier-specific selection, `🔴` = rejected.

### 1.1 SDF Shadow Candidate Benchmark (`Experimental/RemovedSdfGi/` — `GtxLowEnd` Tier)

Empirical metrics from [`Experimental/RemovedSdfGi/Evidence/CleanEmissiveShadows/shadow_validation_summary.json`](../../Experimental/RemovedSdfGi/Evidence/CleanEmissiveShadows/shadow_validation_summary.json) (`560×315`, `25,068`-tri `ControlVehicle` + live deforming `10`-limb rig, denoiser `OFF`):

| Rank | SDF Shadow Candidate | Mechanism | Shadow RMSE | Full-Lit HDR RMSE | Cost (`560×315` CPU) | Visual Verdict & Selection |
| :---: | :--- | :--- | :---: | :---: | :---: | :--- |
| 🥇 | **Candidate 1: `HybridConeShadowSequence`** | `0.28 m` near-contact `BVH4` ray + `SmoothMin` (`K = 0.12`) directional cone over C0-continuous local dynamic SDF (`DynMid = max(0.056, 0.65·ConeR)`) + interior analytic boxes + live deforming bone capsules | `0.2983` | **`0.1340`** (`⭐⭐⭐`) | `🚩` (`616.7 ms`) | 🟢 **SELECTED for `GtxLowEnd`** — Razor-sharp tyre/foot contact, smooth distance-hardening penumbra, live bone-capsule deformation (`0.08 ms`), zero AABB seams. |
| 🔴 | **Candidate 2: `SurfelOccluderField`** | Area-stratified blocker surfel disks (`0.16 m` grid) + quartic multiplicative transmittance + bone capsules | `0.2973` | `0.1814` (`⭐`) | `🔴` (`806.4 ms`) | 🔴 **REJECTED** — Visible scalloped disk blotches beneath the vehicle chassis (`01_FullLit_ShadowAndGi_Comparison.png`). |
| 🔴 | **Candidate 3: `ClipmapConeShadow`** | World-space 3D voxel clipmap Hermite distance cone march + bone capsules | `0.3068` | `0.2464` | `✔️` (`318.8 ms`) | 🔴 **REJECTED** — Voxel resolution loss crushes interior room lighting and introduces clipmap boundary seams. |
| 🔴 | **Legacy `Before` (`VoxelSdfClipmap`)** | `TraceClipmapSdfPenumbra` over 3-cascade toroidal clipmap | `0.4170` | `0.1968` | `✔️` (`70.6 ms`) | 🔴 **REJECTED** — Staircase AABB phantom rings beneath vehicle and missing deforming character shadow. |

---

### 1.2 Can High-End Real-Time GI Use `BVH4` Shadows Instead of `HybridConeShadow`? — **YES (`🥇` Recommended)**

| Rank | High-End Real-Time Shadow + GI Visibility Architecture | `1080p` GPU Cost (`RTX` / `High-End GTX`) | SDF VRAM | Thin Geometry (`< 5 cm` Louvers / Grilles / Spokes) | Skinned Deforming Characters | Verdict |
| :---: | :--- | :--- | :---: | :--- | :--- | :--- |
| 🥇 | **`BVH4` / `RayQuery` Vogel Shadows + `PlaneAwareShadowFilter` + Skinned `BVH4` Refit** (`SolarFrustumShadow.slang` + `PlaneAwareShadowFilter.slang` + `RefitWideBoundingTreeCompute.slang`) | **`✔️` `< 0.9 ms` (`VK_KHR_ray_query`)**<br>**`🚩` `~1.5–2.6 ms` (`64 B` `BVH4` GTX `1–2` rays/px)** | **`✔️` `0 MB` (Zero SDF)** | `⭐⭐⭐` **`0.0 mm` pinhole error** — exact triangle silhouettes on wipers, grilles, wheel spokes, and `4.5 cm` louvers | `⭐⭐⭐` **Exact triangle skinning** via bottom-up GPU `BVH4` AABB refit (`< 0.08 ms` for `20k` tris; zero capsule approximation) | 🟢 **RECOMMENDED for `HighEnd` Real-Time GI** (both `RTX`/`RDNA2+`/`Arc` and upper-tier `GTX`) |
| 🥈 | **Candidate 1: `HybridConeShadowSequence` (SDF Cone + `0.28 m` `BVH4` Contact + Bone Capsules)** | `✔️` `~0.9–1.6 ms` (`32–48` regular 3D `fp16` steps on entry GTX) | `🚩` `~1–4 MB` per dynamic asset (`48³–64³`) | `⭐⭐` `DynMid >= 5.6 cm` prevents grille holes, but softens sub-`5 cm` slits beyond `0.28 m` | `⭐⭐` Fast (`< 0.08 ms`) tapered `10+`-capsule proxy; approximates fingers/cloth folds | 🟢 **KEEP for `GtxLowEnd`** (entry GTX `1050 Ti` / `1060` / iGPU where regular 3D texture fetches beat divergent `BVH4` stacks) |
| 🔴 | **Unfiltered Brute-Force `8–16` `BVH4` Rays/Pixel Every Frame** | `🔴` `4.5–9.0+ ms` on GTX compute | `✔️` `0 MB` | `⭐⭐⭐` Exact | `⭐⭐⭐` Exact | 🔴 **REJECTED for `60 FPS` GTX** — Use `1–2` Vogel/blue-noise rays + `PlaneAwareShadowFilter` instead (reserve unfiltered high-`spp` for Order 3 `SceneFinalRender`). |

**Key Architectural Consequence for High-End GI**:
- **Yes — High-End Real-Time GI (`StokesContour` + `SurfelRadiosity`) can run with `100% BVH4` shadows and `0 MB` of SDF in VRAM**:
  1. **Direct Sun / Spot / Point Shadows**: `SolarFrustumShadow.slang` (`1–2` rays/px on High-End GTX `64 B` `TLAS4+BLAS4`, `2–4` rays/px on `VK_KHR_ray_query`) + coplanar blocker dilation + 3-step continuous-sigma `PlaneAwareShadowFilter.slang` (`σ_px ∝ BlockerDist · tan(SunRadius)`) preserves razor-sharp `0 px` tyre/foot contact and smooth aerial penumbra.
  2. **Emissive Cluster Occlusion Inside `StokesContour` & `SurfelRadiosity`**: Both GI candidates test occlusion **once per luminaire cluster** (`6` clusters on `ControlVehicle`, not per triangle), and `SurfelRadiosityLatticeSequence` **already** traces `96` Fibonacci `BVH4` rays per lattice cell for Chebyshev depth moments + back-face relocation. Replacing `EvaluateHybridConeVisibility` with `1–2` `BVH4` cluster rays makes **`HighEnd` GI completely SDF-free**!
  3. **Where SDF Remains (`GtxLowEnd` Only)**: `HybridConeShadowSequence` (Candidate 1) + `LocalSignedDistanceVolume` (`48³`) stays exclusively as the predictable-bandwidth `GtxLowEnd` shadow & luminaire-cone path for older non-RT GPUs.

---

### 1.3 Emissive & Multi-Bounce Global Illumination Candidate Decision Table

Empirical metrics from [`Experimental/RemovedSdfGi/Evidence/CleanEmissiveGi/validation_summary.json`](../../Experimental/RemovedSdfGi/Evidence/CleanEmissiveGi/validation_summary.json) (`560×315`, `25,068`-tri `ControlVehicle` + `42` emissive triangles across `6` clusters, denoiser `OFF`):

| Rank | GI Candidate | Mechanism | Normal-GI RMSE | Lattice (DDGI) RMSE | Shade-Time Lookup | Visual Verdict & Selection |
| :---: | :--- | :--- | :---: | :---: | :--- | :--- |
| 🥇 | **Candidate 1: `StokesContourIrradianceSequence`** | Exact Sutherland-Hodgman clipped spherical-polygon Stokes contour integral (`F = \|∮ θ (e×n)/\|e×n\|\| / 2π`) per emissive triangle + cluster visibility + diffuse surface patch multi-bounce | **`0.2238`** (`⭐⭐⭐`) | **`0.2238`** (`⭐⭐⭐`) | `O(N_tri)` exact contour + `O(1)` 2nd-bounce lattice | 🟢 **SELECTED (Direct Emissive Contour + High-End GI)** — Exact zero-noise luminaire horizon boundaries, zero ray-miss blotches, smooth red/emerald wall bleed. |
| 🥇 | **Candidate 2: `SurfelRadiosityLatticeSequence`** | Sobol' `(0,2)` area-stratified emissive micro-disks + Bunnell disk-to-disk form factors + solid-angle cluster cutoff (`> 4.8 m`) + 2-bounce surfel radiosity + analytically baked Dual-Cascade C1-Hermite Octahedral Lattice (`19×15×6` + `9×7×4`) | **`0.2239`** (`⭐⭐⭐`) | **`0.2269`** (`⭐⭐⭐`) | `O(1)` C1-Hermite trilinear octahedral lookup | 🟢 **SELECTED (O(1) Multi-Bounce Lattice & Low-End/High-End GI)** — Blotch-free `O(1)` shade-time cost, zero cascade seam, zero stochastic ray variance. |
| 🔴 | **Candidate 3: `ContinuousFieldL1`** | 3D prefiltered directional `L1` SH clipmap (`64` cones) + `192`-ray Veach-MIS Hermite lattice | `0.2259` | `0.2262` | `O(64)` cone march / `O(1)` lattice | 🔴 **REJECTED** — Crushes indirect wall brightness and introduces dark corner falloff (`08_Comparison_ThreeCandidates_SameVehicle.png`). |
| 🔴 | **Legacy `Before` (`Engine DDGI` / `Clipmap`)** | Stochastic probe rays / raw 3-cascade SDF voxel cone march | `0.2402` | `0.5361` | `O(1)` / `O(cones)` | 🔴 **REJECTED** — Severe probe light leaks, cutoff rings, and blocky floor voxel streaks. |

---

### 1.4 Master Execution Roadmap (`Phase 0 → Order 1 → Order 2 → Order 3`)

```text
[Phase 0: Purge All SDF & SDF-Baking from Engine/]
   └── Archive GpuSdfBaker + VolumeSdf shaders -> Experimental/RemovedSdfGi/Source/BakingSdf/
   └── Strip VolumeSdf/.fvol from Baking, Fracture, ComponentEditor, AudioSpace, Material, FontCodec
   └── Gate: 0 non-BSDF "sdf / fvol / distancefield" hits in Engine/ + 58/58 CTests green
          │
          ▼
[Order 1 (1st): Standalone Direct PBR — Zero GI, Zero SDF]
   └── MatCap + OpenPbrDirect (10-lobe OpenPBR, Sun + 3-LUT Sky/Clouds/Fog/Moons/Stars, LTC quads, IES/Point)
   └── Direct non-SDF SolarShadowSequence (BVH4 RayQuery + 2-Cascade Contact PCSS) + BVH4 Specular/Glass
   └── Gate: Deterministic CPU + Vulkan .slang parity proofs in Exhibits/Images/DirectPbr/
          │
          ▼
[Order 2 (2nd): Real-Time GI — GtxLowEnd (SDF Shadow Cand 1) & HighEnd (BVH4 Shadows, Zero SDF)]
   ├── GtxLowEnd : HybridConeShadowSequence (Cand 1) + LocalSignedDistanceVolume (48^3) + Stokes/Surfel GI
   ├── HighEnd   : BVH4/RayQuery Vogel Shadows + PlaneAwareFilter + Skinned BVH4 Refit (0 MB SDF!) + Full Stokes + Surfel GI
   └── Gate: Zero-denoiser ControlVehicle + deforming character proof (HDR RMSE <= 0.134, GI RMSE <= 0.224)
          │
          ▼
[Order 3 (3rd): Unified Deterministic Path Tracer — Games + Actual Scene Rendering]
   └── Step 0: Auto-capture live HTML screenshots of Experimental/DeterministicPathTracer/ first
   └── Single .slang compute integrator (Sobol (0,2), lobe + Fresnel split, PhotometricLightTree, 64 B 4-wide BVH4)
   └── GameRealtime vs SceneFinalRender differ ONLY in settings (trace 1/2..1/4, spp, denoiser, reprojection)
   └── Gate: Bit-exact FNV-1a digest repeat + 1:1 HTML UI & pixel parity proofs
```

---

## 2. Phase 0 — Complete Purge of Residual SDF & SDF-Baking from `Engine/`

| # | Domain & Files | Current Residual SDF Content | Phase 0 Action |
| :---: | :--- | :--- | :--- |
| **0.1** | `Engine/GeometricRaster/Baking/GpuSdfBaker.{h,cpp}`<br>`Engine/Shaders/Baking/{BakeVolumeSdfCompute,ExtractSdfSectionCompute}.slang`<br>`Exhibits/Images/Texturing/10-BakingWorkspaceSplitSdf.png` | Pure-GPU Vulkan 3D Volume SDF baker (`GTX BVH4` + `RTX RayQuery`), 2D SDF cross-section extractor, and `.fvol` exporter | **Archive byte-exact** into `Experimental/RemovedSdfGi/Source/BakingSdf/`, then **delete** from `Engine/` and `Exhibits/Images/Texturing/`, and remove shader/source entries from `CMakeLists.txt` (`L555–557`, `L784`). |
| **0.2** | `Engine/Workspaces/Texturing/Baking{WorkspaceSession,InspectorPane,TargetsPane,ViewportPane}.{h,cpp}`<br>`Engine/GeometricRaster/Baking/{GpuTextureBaker.cpp,TextureBakerRasterizer.h}`<br>`Engine/Shaders/Baking/BakeShaderLayouts.slang`<br>`Exhibits/{BakingGPUValidation,BakingWorkspaceUIValidation}.cpp` | `BakerTargetId::VolumeSdf`, `"3D Volume SDF & Slice Inspector"`, `RefreshSdfSectionOnGpu()`, `ExportVolumeSdfFvol()`, `ColorizeSdfDistance()` | Strip all `VolumeSdf` / `.fvol` targets, UI controls, shaders, and validation checks. Narrow `FrontierBaking` cleanly to a **17-Target High-to-Low PBR Surface Texture Baker**; update `EngineConfiguration.h`, `FrontierBakingLaunch.cpp`, `WorkspaceCapabilityRegister.cpp`, and `FolderStructure.md` / READMEs. |
| **0.3** | `Engine/GeometrySpace/Fracture/ConvexPartitioning.{h,cpp}`<br>`Engine/Workspaces/Fracture/{FractureWorkspaceSession.{h,cpp},FractureInspectorPanel.cpp}`<br>`Exhibits/FractureGeometryValidation.cpp` | `PieceSdfSectionOutput`, `SdfVolumeFootprint`, `ComputePieceSdfSection()`, `ComputeSdfVolumeFootprint()`, `QuerySdfFootprint()`, `SdfEnabled`, `SdfResolution`, `SignedDistanceLocal()` | Remove `PieceSdfSectionOutput`, `SdfVolumeFootprint`, and SDF baking/footprint queries from `Fracture`; rename analytic Voronoi seed-containment helper `SignedDistanceLocal` → `EvaluatePrimitiveBoundaryClearance`. |
| **0.4** | `Engine/Workspaces/ComponentEditor/{ComponentContentTray,ComponentDrawerPanel}.cpp`<br>`Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.cpp`<br>`Engine/Shaders/ComponentEditor/ComponentFieldHull.slang`<br>`Engine/Components/ContentBrowser/ThumbnailSheetRepository.cpp` | `"SDF"` compile badge/row (`"64^3"`, `"Signed distance shell"`), field-hull SDF comment, and `"Cumulus Density SDF"` mock asset entry | Remove `"SDF"` compile chips/rows from `ComponentEditor` (`UV`, `LOD`, `Collision`), update `ComponentFieldHull.slang` header/comments to collision hull bounds, and rename `"Cumulus Density SDF"` → `"Cumulus Density Volume"`. |
| **0.5** | `Engine/AudioSpace/{SpatialAcoustics,AudioExtension}.{h,cpp}`<br>`Exhibits/AudioSpaceValidation.cpp` | `SdfAcousticFieldReading` (implementation already traces `WideBoundingTree` `BVH4` rays, zero SDF) | Rename `SdfAcousticFieldReading` → **`RayAcousticFieldReading`** and update comments/READMEs to reflect `BVH4` ray occlusion + 6-axis corridor clearance. |
| **0.6** | `Engine/Workspaces/Material/MaterialPreviewRenderer.cpp`<br>`Engine/Codecs/FontCodec.{h,cpp}`<br>`Exhibits/{GeometryCompletionValidation,DrawerGestureWorldValidation,SolarShadowValidation,WorkspaceRevisionValidation}.cpp` | Local analytic specimen lambdas `BoxSdf`/`ClothSdf`, `.FontSpace` `SignedDistanceField` vector, and stale SDF titles/comments in exhibits | Rename preview lambdas to `RoundedBoxImplicit` / `DrapedSheetImplicit`, rename `FontSpaceDocumentModel::SignedDistanceField` → `ContourClearanceSamples`, and clean stale SDF strings in `GeometryCompletionValidation.cpp` (`BVH4` slice readout). |

### Phase 0 Exit Gate
1. `grep -rn -i "sdf\|distancefield\|signeddistance\|fvol" Engine/` (excluding `BSDF`/`BRDF`/`BTDF`/`BCSDF` scattering identifiers) returns **`0` matches**.
2. Full CMake build (`Editor`, `Development`, `Shipping`) + all `58` CTests compile and pass cleanly.
3. Commit and push immediately (`CLAUDE.md` §2.0).

---

## 3. Order 1 (1st) — Standalone Direct PBR (`MatCap` + `OpenPbrDirect`, Zero GI, Zero SDF)

| Component | C++20 / `.slang` Units | Exact Scope & Invariants |
| :--- | :--- | :--- |
| **Shading & Surface Transport** | `TieredWorldRenderSequence.{h,cpp}`<br>`VulkanWorldRenderSequence.{h,cpp}`<br>`WorldViewportRadiometricCompute.slang`<br>`OpenPbrSurfaceScattering.{h,slang}` | `WorldRenderEngine::MatCap` (`0u`) and `WorldRenderEngine::OpenPbrDirect` (`1u`) across all 10 OpenPBR lobes + Debug channels `0..11`. |
| **Direct Light Sources** | `LinearCosineLightIntegration.{h,cpp}`<br>`BlackbodyEmission.h`<br>`PhotometricProfile.{h,cpp}` | Direct solar irradiance (`Neckel & Labs` limb darkening), analytic `LTC` polygonal quad/ribbon lights, punctured point/spot lights with `IES` candela tables, and direct `0`-bounce sky/studio hemisphere fallback (**zero GI**). |
| **Direct Shadows & Reflections** | `SolarShadowSequence.{h,cpp}`<br>`SolarFrustumShadow.slang`<br>`PlaneAwareShadowFilter.slang`<br>`SpecularReflectionSequence.{h,cpp}`<br>`SpecularGlassRaytrace.slang` | Direct non-SDF solar shadows (`WideTreeRayQuery` deterministic Vogel solar frustum + `ContactPcss` `3.9 mm` 2-cascade orthographic depth clip + plane-aware filter), `4`-ray short-range `BVH4` contact AO (`0–0.55 m`), and `BVH4` / sky specular & Abbe dispersion glass refraction. |
| **Unified Atmosphere** | `AtmosphericSkySequence`, `VolumetricCloudSequence`, `VolumetricFogSequence`, `LunarStarfieldSequence`, `LensFlareSequence` | Coupled 3-LUT Bruneton/Hillaire sky, single-source `PlanetaryClouds`, wind-advected height/local fog, `6` regolith moons, `1,640` Yale BSC5 stars, and zodiacal light. |

---

## 4. Order 2 (2nd) — Real-Time GI (`StokesContour` + `SurfelRadiosity`) Across `GtxLowEnd` & `HighEnd`

### 4.1 `GtxLowEnd` vs. `HighEnd` Real-Time Preset Matrix

| Parameter | `GtxLowEnd` Preset (Entry GTX / Integrated) | `HighEnd` Real-Time Preset (Upper GTX + RTX / RDNA2+ / Arc) |
| :--- | :--- | :--- |
| **Direct Solar / Spot Shadows** | **Candidate 1: `HybridConeShadowSequence`** (`0.28 m` `BVH4` contact ray + `32–48`-step `SmoothMin` SDF cone + `ArticulatedCapsuleRig`, `LocalSignedDistanceVolume` `48³`) | **`BVH4` / `RayQuery` Vogel Shadows + `PlaneAwareShadowFilter`** (`1–2` rays/px on `64 B` `TLAS4+BLAS4` GTX compute; `2–4` rays/px on `VK_KHR_ray_query`) + **GPU Skinned `BVH4` AABB Refit (`< 0.08 ms`)** — **`0 MB` SDF!** |
| **Luminaire Cluster Occlusion** | Hybrid SDF cone (`EvaluateHybridConeVisibility` over `6` luminaire clusters) | **`1–2` `BVH4` / `RayQuery` rays per luminaire cluster** (`0` SDF dependency) |
| **1st-Bounce Emissive GI** | **Stokes Contour** on hero/near luminaire clusters (`<= 4.8 m`) + **Surfel Cluster Aggregate Disk** beyond `4.8 m` (`SolidAngle < 0.025 sr`) | **Full Per-Triangle Stokes Spherical-Polygon Contour** (`StokesContourIrradianceSequence`) across all visible emissive triangles |
| **Multi-Bounce Indirect Diffuse GI** | **Surfel Radiosity Octahedral Lattice** (`12×10×4` inner + `8×6×3` outer, `O(1)` C1-Hermite trilinear lookup, amortized slice refresh) | **2-Bounce Bunnell Surfel Radiosity + Full Dual-Cascade C1-Hermite Octahedral Lattice** (`19×15×6` inner + `9×7×4` outer, `96` Fibonacci `BVH4` Chebyshev depth rays, `16×16` octahedral sheets) |
| **Denoiser Requirement** | **None (`OFF`)** — analytic contour + C1-Hermite surfel lattice is inherently noise-free | **None (`OFF` on GI)**; `PlaneAwareShadowFilter` (`σ_px ∝ BlockerDist · tan(SunRadius)`) on `1–4`-ray direct `BVH4` solar shadows |

---

## 5. Order 3 (3rd) — Unified Deterministic Path Tracer (`Games` + `Actual Scene Rendering`)

**Games** and **Actual Scene Rendering** execute the **exact same deterministic path-tracing integrator** (`1:1` with [`Experimental/DeterministicPathTracer/`](../../Experimental/DeterministicPathTracer/README.md)) — differing **solely in runtime configuration settings**:

| Configuration Knob | `GameRealtime` Profile (In-Game Live Play) | `SceneFinalRender` Profile (Look-Dev & Offline Scene Render) |
| :--- | :--- | :--- |
| **Integrator Shader** | `DeterministicRadianceIntegration.slang` (**identical**) | `DeterministicRadianceIntegration.slang` (**identical**) |
| **Trace Resolution** | `½` (`2×2` tile = `0.25 spp/dispatch`) or `¼` (`4×4` tile = `0.0625 spp/dispatch`) + tent/guide fill | `1×` (`1:1` full pixel trace) |
| **Samples per Dispatch** | `1` (`1–2` frames in flight, non-blocking pacing) | `1–4` (`target = 128..4096+ spp` until converged) |
| **Sample Pattern** | `BlueNoise` (`64²` void-and-cluster `(0,m,2)` XOR shift) | `Shared` (zero grain, pure `(0,m,2)` stratification) or `BlueNoise` |
| **Primary Visibility** | `Rasterized` (`r32uint` primary ID) at `1×` or `Traced` (`BVH4`) at `½` / `¼` | `Traced` (`BVH4` exact primary ray) |
| **Camera Motion & History** | `Reproject` (`span = 8`, virtual-image mirror motion, `0` glossy lag) | `Restart` (`0` temporal history carry-over) |
| **Denoiser (`DisplayReconstructionFilter`)** | **Enabled** (`σ = 4/n`, strength `1 → 0` over `4..64 spp`; **display-only**, raw float sums untouched) | **Disabled** (or auto-zeroed at `>= 64 spp`; raw float accumulation exact) |
| **IES Lamp Sampling** | `split4` or `split8` adaptive `PhotometricLightTree` (`228+` `.ies` lamps) | `split8` or `all` reference |
| **Indirect Bounces / Specular Depth** | `1–2` indirect bounces · `4–6` specular/glass chain | `2–4` indirect bounces · `10` specular/glass chain (`cutoff = 0.002`) |

---

## 6. Canonical Folder Structure (Across Phase 0 & Orders 1–3)

### 6.1 Top-Level Repository & `Engine/` Domain Layout

```text
Frontier/
├── CLAUDE.md · AGENTS.md · CMakeLists.txt · README.md
├── Documentation/                                 // Governance, SKILL-*.md, FolderStructure.md, Plans/ & Markdown Studio
├── Automation/                                    // Dependencies.json, 8 ImGui patches, slangc/SwiftShader, Studio server
├── EngineContent/                                 // Stock .GeometrySpace, Icons, FontsArchives, Materials, MatCaps, Worlds
├── EngineDocs/                                    // Architectural & domain reference guides
├── Projects/SampleProject/                        // Concrete game project container
├── Experimental/                                  // HTML/JS/WebGPU reference studios + preserved redo records
│   ├── DeterministicPathTracer/                   // Order 3 HTML/WebGPU ground-truth studio (:8087)
│   ├── DeterministicRaytracingLab/                // WebGPU BVH4 + octahedral lattice reference (:8097)
│   ├── ShadowAlgorithmLab/                        // Solar shadow tier benchmark (:8098)
│   ├── SdfLightingDemoCpp/                        // Standalone C++20 5-engine SDF reference
│   └── RemovedSdfGi/                              // Byte-exact preserved SDF/GI/Shadow sources + proof PNGs
│       └── Source/BakingSdf/                      // [Phase 0] Archived GpuSdfBaker + VolumeSdf .slang units
├── Exhibits/                                      // Headless CTest validations & deterministic .png proofs
│   ├── Images/{DirectPbr,GtxSdfGi,DeterministicPathTracer,...}/
│   ├── DirectPbrWorldValidation.cpp               // [Order 1] Standalone MatCap + OpenPbrDirect parity proof
│   ├── GtxSdfGiShadowValidation.cpp               // [Order 2] GtxLowEnd (HybridCone) & HighEnd (BVH4 + Stokes/Surfel) proof
│   └── DeterministicPathTracerValidation.cpp      // [Order 3] Bit-exact digest, 50-car fleet & Game/Render parity proof
└── Engine/                                        // Portable ISO C++20 + .slang Engine Root (Windows · Linux · Android)
    ├── MicroUtils/          ├── Configuration/    ├── Constants/         ├── Time/
    ├── Logging/             ├── Threading/        ├── FileSystem/        ├── ColourSpace/
    ├── Mathematics/         ├── GeometrySpace/    ├── SpatialAcceleration/
    ├── MaterialSpace/       ├── Codecs/           ├── Interchange/       ├── AudioSpace/
    ├── NetworkingSpace/     ├── SimulationSpace/  ├── Input/             ├── Windowing/
    ├── Annotation/          ├── Components/       ├── Lifecycle/         ├── Workspaces/
    ├── Editor/              ├── GeometricRaster/  └── Shaders/
    └── DistanceFieldSpace/                        // [Absent in Phase 0 & Order 1; added in Order 2 ONLY for GtxLowEnd]
```

### 6.2 Detailed Rendering, Acceleration, Baking & `.slang` Shader Tree

```text
Engine/
├── Codecs/
│   ├── FontCodec.{h,cpp}                          // [Phase 0] SignedDistanceField -> ContourClearanceSamples
│   ├── ControlVehicleSurfaceDecoder.{h,cpp}       // [Order 2] Restored FCT1 .facet 25,068-tri vehicle decoder
│   └── PhotometricProfile.{h,cpp}                 // [Order 1/3] LM-63-2002 (.ies) 128x32 candela table codec
│
├── SpatialAcceleration/
│   ├── ClusterPyramid.{h,cpp}                     // 128-triangle cluster hierarchy for two-phase visibility culling
│   ├── WideBoundingTree.{h,cpp}                   // 128 B 4-wide SIMD BVH4 (CPU + GPU direct ray queries)
│   └── CompressedWideTree.{h,cpp}                 // [Order 2/3] 64 B 8-bit quantized 4-wide TLAS4 + BLAS4 (50+ instances) + GPU AABB refit
│
├── DistanceFieldSpace/                            // [Order 2 — GtxLowEnd ONLY; HighEnd & Order 1 use 0 MB SDF]
│   ├── LocalSignedDistanceVolume.{h,cpp}          // Per-asset Float16/Float32 local grid + C0 Euclidean outer-box clamp
│   └── ArticulatedCapsuleRig.h                    // Live deforming tapered bone-capsule evaluator (< 0.08 ms)
│
├── GeometricRaster/
│   ├── Baking/                                    // [Phase 0] Pure 17-target PBR surface baker (GpuSdfBaker DELETED)
│   │   ├── StudyGeometryBuilder.{h,cpp}
│   │   ├── BakingAccelerationStructure.{h,cpp}
│   │   ├── GpuTextureBaker.{h,cpp}
│   │   └── TextureBakerRasterizer.{h,cpp}
│   │
│   ├── Visibility/                                // Shared 64-bit primary visibility front-end
│   │   ├── DepthPyramidSequence.{h,cpp}
│   │   ├── ClusterCullEncoder.{h,cpp}
│   │   ├── VisibilityClusterRasterization.{h,cpp}
│   │   └── VisibilityMaterialResolve.{h,cpp}
│   │
│   ├── DirectLighting/
│   │   ├── SolarShadowSequence.{h,cpp}            // [Order 1 & Order 2 HighEnd] BVH4/RayQuery Vogel solar + 2-cascade ContactPcss + PlaneAwareFilter
│   │   ├── LinearCosineLightIntegration.{h,cpp}   // [Order 1] Analytic LTC polygonal area quads + point/IES + 4-ray BVH4 contact AO
│   │   ├── HybridConeShadowSequence.{h,cpp}       // [Order 2 GtxLowEnd] Candidate 1: 0.28 m BVH4 contact + SmoothMin C0 SDF + bone capsules
│   │   └── PhotometricLightTree.{h,cpp}           // [Order 3] Conty Estevez & Kulla adaptive split4/split8 .ies importance light tree
│   │
│   ├── GlobalIllumination/
│   │   ├── TieredWorldRenderSequence.{h,cpp}      // [Order 1–3] Unified CPU reference & engine selector
│   │   ├── VulkanWorldRenderSequence.{h,cpp}      // [Order 1–3] Unified Vulkan 1.3 .slang world render sequence
│   │   ├── ShowcaseGalleryScene.{h,cpp}           // Calibrated 10-lobe sphere gallery + ControlVehicle studio scene
│   │   ├── SpecularReflectionSequence.{h,cpp}     // [Order 1–2] BVH4 VNDF / Hi-Z SSR + Abbe dispersion glass refraction
│   │   ├── StokesContourIrradianceSequence.{h,cpp}// [Order 2] GI Candidate 1: exact spherical-polygon Stokes contour integral
│   │   ├── SurfelRadiosityLatticeSequence.{h,cpp} // [Order 2] GI Candidate 2: Bunnell surfel radiosity + Dual-Cascade C1-Hermite Octahedral Lattice
│   │   └── DeterministicPathTraceSequence.{h,cpp} // [Order 3] Unified zero-RNG Sobol' (0,2) path tracer (GameRealtime & SceneFinalRender)
│   │
│   ├── Atmosphere/                                // [Active] 3-LUT Sky, Neckel & Labs Sun, PlanetaryClouds, Fog, 6 Moons, 1640 Stars
│   └── PostProcess/                               // [Active] RadiometricToneStack, VeilingGlareFilter, AmbientOcclusionFilter
│
└── Shaders/                                       // 100% Slang (.slang) compiled via slangc -> SPIR-V
    ├── Baking/                                    // [Phase 0] BakeUvGBuffer, BakeSurfaceTransfer, BakeChannelResolve, BakeDilation (VolumeSdf DELETED)
    ├── Visibility/                                // ClusterVisibilityCull, DepthPyramidDownsample, Hardware/MicroFace VisBuffer, AnalyticResolve
    ├── DirectLighting/
    │   ├── WideTreeRayQuery.slang                 // [Active + Order 2/3] 4-wide BVH4 + compressed 64 B TLAS4/BLAS4 + VK_KHR_ray_query
    │   ├── RefitWideBoundingTreeCompute.slang     // [Order 2/3] Bottom-up GPU AABB refit for skinned/deforming characters (< 0.08 ms)
    │   ├── SolarFrustumShadow.slang               // [Order 1 & Order 2 HighEnd] Vogel solar disc BVH4/RayQuery + 4-ray contact/chassis AO
    │   ├── PlaneAwareShadowFilter.slang           // [Order 1 & Order 2 HighEnd] Coplanar blocker dilation + 3-step continuous-sigma filter
    │   └── HybridConeShadowCompute.slang          // [Order 2 GtxLowEnd] Candidate 1 SmoothMin C0 SDF + capsule cone shadow
    ├── GlobalIllumination/
    │   ├── SpecularGlassRaytrace.slang            // [Order 1–2] Cone-footprint multi-bounce specular & Abbe glass
    │   ├── StokesContourIrradianceCompute.slang   // [Order 2] GI Candidate 1 exact Stokes spherical-polygon contour compute
    │   ├── SurfelRadiosityLatticeCompute.slang    // [Order 2] GI Candidate 2 Bunnell surfel + Dual-Cascade C1-Hermite Octahedral Lattice
    │   ├── QuadratureSequence.slang               // [Order 3] Sobol' (0,2) + Laine-Karras permutation + 64^2 BlueNoise XOR shift
    │   ├── DeterministicRadianceIntegration.slang // [Order 3] Zero-RNG Fresnel + full-lobe split + PhotometricLightTree + reproject guides
    │   ├── VisibilityTriangleRasterization.slang  // [Order 3] Optional subpixel r32uint primary triangle-ID raster
    │   ├── DisplayReconstructionFilter.slang      // [Order 3] Mirror-aware history reprojection + 5-step edge-aware a-trous (display-only)
    │   └── DisplayToneTransform.slang             // [Order 3] Exposure + ACES + sRGB + non-blocking timestamp ring
    ├── Atmosphere/ · MaterialSpace/ · GeometricRaster/ · Colour/ · ComponentEditor/ · Sketcher/ · Vector/ · Compute/
    └── Compiler/
```
