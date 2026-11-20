# 🧩 World Rendering Ground-Up Redo Plan

> **TL;DR**: First, purge every remaining SDF and 3D Volume SDF-baking unit from `Engine/` (archiving `GpuSdfBaker` and volume SDF `.slang` units into `Experimental/RemovedSdfGi/Source/BakingSdf/`) so `Engine/` starts from a 100% SDF-free baseline. Next, execute the world renderer in three strict phases: **(1st) Standalone Direct PBR (`MatCap` + `OpenPbrDirect`, zero GI, zero SDF)**, **(2nd) GTX SDF Shadows + GI (`GtxLowEnd` & `GtxHighEnd` presets)** built exclusively from **Shadow Candidate 1 (`HybridConeShadowSequence`)** and **GI Candidates 1 + 2 (`StokesContourIrradianceSequence` + `SurfelRadiosityLatticeSequence`)**, and **(3rd) Unified Deterministic Path Tracer (`DeterministicPathTraceSequence`)** serving both **Games** and **Actual Scene Rendering** through settings alone (`1:1` with `Experimental/DeterministicPathTracer/`).

---

## 1. Locked Architecture & Candidate Decisions (`Experimental/RemovedSdfGi/`)

> **Decision Legend** (`SKILL-Emoji.md` §5): **Accuracy / Visual Fidelity**: `⭐⭐⭐` = best (`RMSE` vs. `128 spp` BVH+NEE reference, denoiser `OFF`); **Runtime Cost**: `✔️` = lowest/fastest, `🚩` = moderate, `🔴` = heaviest; **Status**: `🥇` = selected for `Engine/`, `🔴` = rejected.

### 1.1 Solar & Dynamic Rig Shadow Candidate Decision Table

Empirical metrics from [`Experimental/RemovedSdfGi/Evidence/CleanEmissiveShadows/shadow_validation_summary.json`](../../Experimental/RemovedSdfGi/Evidence/CleanEmissiveShadows/shadow_validation_summary.json) (`560×315`, `25,068`-tri `ControlVehicle` + live deforming `10`-limb rig, zero denoiser):

| Rank | Shadow Candidate | Mechanism | Shadow RMSE | Full-Lit HDR RMSE | Cost (`560×315`) | Visual Verdict & Selection |
| :---: | :--- | :--- | :---: | :---: | :---: | :--- |
| 🥇 | **Candidate 1: `HybridConeShadowSequence`** | `0.28 m` near-contact `BVH4` ray + `SmoothMin` (`K = 0.12`) directional cone over C0-continuous local dynamic SDF (`DynMid = max(0.056, 0.65·ConeR)`) + interior analytic boxes + live deforming bone capsules | `0.2983` | **`0.1340`** (`⭐⭐⭐`) | `🚩` (`616.7 ms`) | 🟢 **SELECTED (Sole Shadow Architecture)** — Razor-sharp tyre/foot contact, smooth distance-hardening penumbra, live sprint bone-capsule deformation (`0.08 ms` update), zero grille pinholes, zero AABB seams. |
| 🔴 | **Candidate 2: `SurfelOccluderField`** | Area-stratified blocker surfel disks (`0.16 m` grid) + quartic multiplicative transmittance + bone capsules | `0.2973` | `0.1814` (`⭐`) | `🔴` (`806.4 ms`) | 🔴 **REJECTED** — Produces visible scalloped disk blotches under the vehicle chassis (`01_FullLit_ShadowAndGi_Comparison.png`). |
| 🔴 | **Candidate 3: `ClipmapConeShadow`** | World-space 3D voxel clipmap Hermite distance cone march + bone capsules | `0.3068` | `0.2464` | `✔️` (`318.8 ms`) | 🔴 **REJECTED** — Voxel grid resolution loss crushes interior room lighting and introduces clipmap boundary artifacts. |
| 🔴 | **Legacy `Before` (`VoxelSdfClipmap`)** | `TraceClipmapSdfPenumbra` over 3-cascade toroidal clipmap | `0.4170` | `0.1968` | `✔️` (`70.6 ms`) | 🔴 **REJECTED** — Severe staircase AABB phantom rings beneath vehicle and missing deforming character shadow. |

**Recommendation Locked**: Use **Candidate 1 (`HybridConeShadowSequence`) exclusively** for SDF shadows in Order 2. Reject Candidates 2 and 3.

---

### 1.2 Emissive & Multi-Bounce Global Illumination Candidate Decision Table

Empirical metrics from [`Experimental/RemovedSdfGi/Evidence/CleanEmissiveGi/validation_summary.json`](../../Experimental/RemovedSdfGi/Evidence/CleanEmissiveGi/validation_summary.json) (`560×315`, `25,068`-tri `ControlVehicle` + `42` emissive triangles across `6` clusters, zero denoiser):

| Rank | GI Candidate | Mechanism | Normal-GI RMSE | Lattice (DDGI) RMSE | Shade-Time Lookup | Visual Verdict & Selection |
| :---: | :--- | :--- | :---: | :---: | :--- | :--- |
| 🥇 | **Candidate 1: `StokesContourIrradianceSequence`** | Exact Sutherland-Hodgman clipped spherical-polygon Stokes contour integral (`F = \|∮ θ (e×n)/\|e×n\|\| / 2π`) per emissive triangle + cluster hybrid cone visibility + diffuse surface patch multi-bounce | **`0.2238`** (`⭐⭐⭐`) | **`0.2238`** (`⭐⭐⭐`) | `O(N_tri)` exact contour + `O(1)` 2nd-bounce lattice | 🟢 **SELECTED (Direct Emissive Contour + High-End GI)** — Exact zero-noise luminaire horizon boundaries, zero ray-miss blotches, smooth red/emerald wall bleed. |
| 🥇 | **Candidate 2: `SurfelRadiosityLatticeSequence`** | Sobol' `(0,2)` area-stratified emissive micro-disks + Bunnell disk-to-disk form factors + solid-angle cluster cutoff (`> 4.8 m`) + 2-bounce surfel radiosity + analytically baked Dual-Cascade C1-Hermite Octahedral Lattice (`19×15×6` + `9×7×4`) | **`0.2239`** (`⭐⭐⭐`) | **`0.2269`** (`⭐⭐⭐`) | `O(1)` C1-Hermite trilinear octahedral lookup | 🟢 **SELECTED (O(1) Multi-Bounce Lattice & Low-End/High-End GI)** — Blotch-free `O(1)` shade-time cost, zero cascade seam, zero stochastic ray variance. |
| 🔴 | **Candidate 3: `ContinuousFieldL1`** | 3D prefiltered directional `L1` SH clipmap (`64` cones) + `192`-ray Veach-MIS Hermite lattice | `0.2259` | `0.2262` | `O(64)` cone march / `O(1)` lattice | 🔴 **REJECTED** — Crushes indirect wall brightness and introduces dark corner falloff (`08_Comparison_ThreeCandidates_SameVehicle.png`). |
| 🔴 | **Legacy `Before` (`Engine DDGI` / `Clipmap`)** | Stochastic probe rays / raw 3-cascade SDF voxel cone march | `0.2402` | `0.5361` | `O(1)` / `O(cones)` | 🔴 **REJECTED** — Severe probe light leaks, cutoff rings, and blocky floor voxel streaks. |

**Recommendation Locked**: Combine **Candidate 1 (`StokesContourIrradianceSequence`) + Candidate 2 (`SurfelRadiosityLatticeSequence`)** for Order 2 GTX GI — pairing Stokes exact spherical-polygon contour integration for near-field/hero emissive luminaires with Surfel Radiosity + Dual-Cascade C1-Hermite Octahedral Lattice for `O(1)` multi-bounce indirect diffuse across `GtxLowEnd` and `GtxHighEnd` presets.

---

### 1.3 Master Execution Roadmap (`Phase 0 → Order 1 → Order 2 → Order 3`)

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
[Order 2 (2nd): SDF Shadows + Stokes GI + Surfel GI for GTX (GtxLowEnd & GtxHighEnd)]
   └── Isolated DistanceFieldSpace/ (LocalSignedDistanceVolume + ArticulatedCapsuleRig)
   └── Shadow Candidate 1: HybridConeShadowSequence (0.28 m BVH4 contact + SmoothMin C0 SDF + bone capsules)
   └── GI Candidate 1 + 2: StokesContourIrradianceSequence + SurfelRadiosityLatticeSequence
   └── Gate: Zero-denoiser ControlVehicle + deforming rig proof matching Evidence/ (HDR RMSE <= 0.134)
          │
          ▼
[Order 3 (3rd): Unified Deterministic Path Tracer — Games + Actual Scene Rendering]
   └── Step 0: Auto-capture live HTML screenshots of Experimental/DeterministicPathTracer/ first
   └── Single .slang compute integrator (Sobol (0,2), lobe + Fresnel split, PhotometricLightTree, 4-wide BVH4)
   └── GameRealtime vs SceneFinalRender differ ONLY in settings (trace 1/2..1/4, spp, denoiser, reprojection)
   └── Gate: Bit-exact FNV-1a digest repeat + 1:1 HTML UI & pixel parity proofs
```

---

## 2. Phase 0 — Complete Purge of Residual SDF & SDF-Baking from `Engine/`

Before touching any world-rendering redo, `Engine/` **MUST** contain **zero** SDF types, 3D volume SDF bakers, `.fvol` exporters, or SDF section queries.

| # | Domain & Files | Current Residual SDF Content | Phase 0 Action |
| :---: | :--- | :--- | :--- |
| **0.1** | `Engine/GeometricRaster/Baking/GpuSdfBaker.{h,cpp}`<br>`Engine/Shaders/Baking/{BakeVolumeSdfCompute,ExtractSdfSectionCompute}.slang`<br>`Exhibits/Images/Texturing/10-BakingWorkspaceSplitSdf.png` | Pure-GPU Vulkan 3D Volume SDF baker (`GTX BVH4` + `RTX RayQuery`), 2D SDF cross-section extractor, and `.fvol` exporter | **Archive byte-exact** into `Experimental/RemovedSdfGi/Source/BakingSdf/`, then **delete** from `Engine/` and `Exhibits/Images/Texturing/`, and remove shader/source entries from `CMakeLists.txt` (`L555–557`, `L784`). |
| **0.2** | `Engine/Workspaces/Texturing/Baking{WorkspaceSession,InspectorPane,TargetsPane,ViewportPane}.{h,cpp}`<br>`Engine/GeometricRaster/Baking/{GpuTextureBaker.cpp,TextureBakerRasterizer.h}`<br>`Engine/Shaders/Baking/BakeShaderLayouts.slang`<br>`Exhibits/{BakingGPUValidation,BakingWorkspaceUIValidation}.cpp` | `BakerTargetId::VolumeSdf`, `"3D Volume SDF & Slice Inspector"`, `RefreshSdfSectionOnGpu()`, `ExportVolumeSdfFvol()`, `ColorizeSdfDistance()` | Strip all `VolumeSdf` / `.fvol` targets, UI controls, shaders, and validation checks. Narrow `FrontierBaking` cleanly to a **17-Target High-to-Low PBR Surface Texture Baker**; update `EngineConfiguration.h`, `FrontierBakingLaunch.cpp`, `WorkspaceCapabilityRegister.cpp`, and READMEs. |
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

Solidify the pure direct-lighting world renderer so games, editors, and material look-dev have a fast, artifact-free, physically calibrated baseline with **zero GI** and **zero SDF** dependencies.

| Component | C++20 / `.slang` Units | Exact Scope & Invariants |
| :--- | :--- | :--- |
| **Shading & Surface Transport** | `TieredWorldRenderSequence.{h,cpp}`<br>`VulkanWorldRenderSequence.{h,cpp}`<br>`WorldViewportRadiometricCompute.slang`<br>`OpenPbrSurfaceScattering.{h,slang}` | `WorldRenderEngine::MatCap` (`0u`) and `WorldRenderEngine::OpenPbrDirect` (`1u`) across all 10 OpenPBR lobes (base diffuse/metal, specular GGX VNDF + dielectric/conductor Fresnel, coat, fuzz, thin-film iridescence, subsurface/translucency, anisotropy, fiber BCSDF, emission exitance) + Debug channels `0..11`. |
| **Direct Light Sources** | `LinearCosineLightIntegration.{h,cpp}`<br>`BlackbodyEmission.h`<br>`PhotometricProfile` (`.ies`) | Direct solar irradiance (`Neckel & Labs` limb darkening), analytic `LTC` polygonal quad/ribbon lights (`PolygonalAreaQuadLight`), punctured point/spot lights with `IES` candela tables, and studio/sky hemisphere fallback (strictly direct `0`-bounce sky/studio term, **no GI**). |
| **Direct Shadows & Reflections** | `SolarShadowSequence.{h,cpp}`<br>`SolarFrustumShadow.slang`<br>`PlaneAwareShadowFilter.slang`<br>`SpecularReflectionSequence.{h,cpp}`<br>`SpecularGlassRaytrace.slang` | Direct non-SDF solar shadows (`WideTreeRayQuery` deterministic Vogel solar frustum + `ContactPcss` `3.9 mm` 2-cascade orthographic depth clip + plane-aware filter), `4`-ray short-range `BVH4` contact AO (`0–0.55 m`), and `BVH4` / sky specular & Abbe dispersion glass refraction. |
| **Unified Atmosphere** | `AtmosphericSkySequence`, `VolumetricCloudSequence`, `VolumetricFogSequence`, `LunarStarfieldSequence`, `LensFlareSequence` | Coupled 3-LUT Bruneton/Hillaire sky, single-source `PlanetaryClouds`, wind-advected height/local fog, `6` regolith moons, `1,640` Yale BSC5 stars, and zodiacal light. |

### Order 1 Exit Gate
- Dedicated `Exhibits/DirectPbrWorldValidation` proof sheet (`Exhibits/Images/DirectPbr/`) verifying `MatCap`, `OpenPbrDirect` (`GTX` & `RTX` direct shadow modes), `10`-lobe sphere gallery, `ControlVehicle` direct studio/sun lighting, and CPU-vs-Vulkan `.slang` radiometric parity (`<= 1–5%` delta).

---

## 4. Order 2 (2nd) — SDF Shadows + Stokes GI + Surfel GI for GTX (`GtxLowEnd` & `GtxHighEnd`)

Re-introduce SDF lighting **strictly as an isolated opt-in GTX rendering tier** layered cleanly above Order 1 without mutating standalone `OpenPbrDirect` or re-introducing SDF baking into `FrontierBaking`.

### 4.1 New / Restored Naming-Compliant Modules (Zero Banned Vocabulary)

> All identifiers pass the 7-gate check in `SKILL-Naming.md` (`Cache`, `Probe`, `Mesh`, `Node`, `Stage`, `Pass`, `Pipeline`, `Pool`, and conjunction `And` are **zero-occurrence**).

| Module Path | Responsibility |
| :--- | :--- |
| `Engine/Codecs/ControlVehicleSurfaceDecoder.{h,cpp}` | Decodes `FCT1` `.facet` vehicle geometry archives (`25,068` triangles, creased normals, wheel transforms) without depending on removed `ParticleSpace/`. |
| `Engine/DistanceFieldSpace/LocalSignedDistanceVolume.{h,cpp}` | Per-asset local `Float32` / `Float16` signed/conservative distance grid with C0-continuous Euclidean outer-box clamp (`hypot(BoundaryD, BoxD)`) + analytic box/primitive evaluators. |
| `Engine/DistanceFieldSpace/ArticulatedCapsuleRig.h` | Live deforming tapered bone-capsule evaluator (`EvaluateMinimumSignedDistance`, `< 0.1 ms` per pose update for animated characters). |
| `Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.{h,cpp}` + `Engine/Shaders/DirectLighting/HybridConeShadowCompute.slang` | **Shadow Candidate 1**: `0.28 m` near-contact `BVH4` ray + `SmoothMin` (`K = 0.12`) directional cone march over interior analytic boxes, `ArticulatedCapsuleRig`, and C0-continuous `LocalSignedDistanceVolume` (`DynMid = max(0.056, 0.65·ConeR)` eliminating thin-feature pinholes). |
| `Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence.{h,cpp}` + `Engine/Shaders/GlobalIllumination/StokesContourIrradianceCompute.slang` | **GI Candidate 1**: Sutherland-Hodgman tangent-plane clip + exact closed-form spherical-polygon Stokes contour integral per emissive triangle/cluster + hybrid cone penumbra. |
| `Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence.{h,cpp}` + `Engine/Shaders/GlobalIllumination/SurfelRadiosityLatticeCompute.slang` | **GI Candidate 2**: Sobol' `(0,2)` area-stratified emissive & diffuse surface disks + Bunnell disk-to-disk 2-bounce radiosity + analytically baked Dual-Cascade C1-Hermite Octahedral Lattice (`IrradianceOverPi` `16×16` + Chebyshev `DepthMoments` `16×16`). |

### 4.2 `GtxLowEnd` vs. `GtxHighEnd` Hardware Preset Configuration

| Parameter | `GtxLowEnd` Preset (Entry GTX / Integrated) | `GtxHighEnd` Preset (Desktop GTX / Upper Mid-Range) |
| :--- | :--- | :--- |
| **Solar & Local Shadow (`HybridConeShadowSequence`)** | `0.28 m` `BVH4` contact ray + `48`-step adaptive `SmoothMin` SDF cone + live `ArticulatedCapsuleRig` (`LocalSignedDistanceVolume` `48³`) | `0.28 m` `BVH4` contact ray + `96–112`-step adaptive `SmoothMin` SDF cone + live `ArticulatedCapsuleRig` (`LocalSignedDistanceVolume` `64³–96³`) |
| **1st-Bounce Emissive GI** | **Stokes Contour** on hero/near luminaire clusters (`<= 4.8 m`) + **Surfel Cluster Aggregate Disk** beyond `4.8 m` (`SolidAngle < 0.025 sr`) | **Full Per-Triangle Stokes Spherical-Polygon Contour** (`StokesContourIrradianceSequence`) across all visible emissive triangles + per-cluster hybrid cone visibility |
| **Multi-Bounce Indirect Diffuse GI** | **Surfel Radiosity Single/Dual-Cascade Octahedral Lattice** (`12×10×4` inner + `8×6×3` outer, `O(1)` C1-Hermite trilinear lookup per pixel, amortized slice refresh) | **2-Bounce Bunnell Surfel Radiosity + Full Dual-Cascade C1-Hermite Octahedral Lattice** (`19×15×6` inner + `9×7×4` outer, `96` Fibonacci `BVH4` Chebyshev depth rays, `16×16` octahedral sheets) |
| **Denoiser Requirement** | **None (`OFF`)** — deterministic analytic contour + C1-Hermite surfel lattice is inherently noise-free | **None (`OFF`)** — zero spatial/temporal denoiser blur |

### Order 2 Exit Gate
- `Exhibits/GtxSdfGiShadowValidation` reproducing both `ControlVehicle` emissive studio (`GI RMSE <= 0.224`) and solar + deforming sprint rig (`Full-Lit HDR RMSE <= 0.134`) on CPU and Vulkan `.slang` compute, saved to `Exhibits/Images/GtxSdfGi/`.

---

## 5. Order 3 (3rd) — Unified Deterministic Path Tracer (`Games` + `Actual Scene Rendering`)

Port [`Experimental/DeterministicPathTracer/`](../../Experimental/DeterministicPathTracer/README.md) `1:1` into native Vulkan `.slang` compute + C++20 (`DeterministicPathTraceSequence`). **Games** and **Actual Scene Rendering** execute the **exact same deterministic path-tracing integrator** — differing **solely in runtime configuration settings**, never via a simplified substitute shader.

### 5.1 Pre-Code Gate (`CLAUDE.md` §2.1 Automatic Step 0)
1. Start `python3 Experimental/DeterministicPathTracer/Serve.py` (`:8087`), capture live headless Chromium `.png` screenshots across all inspector sections (`Render`, `Camera`, `Look`, `Transport`, `Real-time`, `Determinism`, `Method`) and camera/realtime modes, and inspect every `.png` via `read_file` before writing C++ or `.slang`.
2. Match the HTML inspector controls `1:1` using `Engine/Components/` (`InspectorPanel`, `CapsuleButton`, `DropdownSelector`, `ScalarSlider`, `ViewportPanel` status overlays).

### 5.2 Unified `.slang` Compute & Host Architecture (`0` RNG · `0` Coin-Flips · `0` Russian Roulette)

| `.slang` / C++20 Unit | Exact `Experimental/DeterministicPathTracer/` Parity |
| :--- | :--- |
| `QuadratureSequence.slang` + `BlueNoiseRanks.bin` (`16 KiB`) | Fixed Sobol' `(0,2)` point set (`66` pairs, every `2^m` prefix is a `(0,m,2)`-net) + Laine–Karras nested-uniform digit permutation; supports `Shared`, `PerPixel` hash, and `BlueNoise` (`64²` void-and-cluster XOR shift). |
| `DeterministicRadianceIntegration.slang` | Single-stack 2-level traversal over `64 B` 4-wide compressed `BVH4` (`TLAS` + `BLAS`, `1–50+` instanced vehicles with per-instance material overrides); **exact Fresnel reflect + refract split**; **every active OpenPBR lobe traced per sample**; MIS (`β = 2`) with analytic softbox quadrature + `PhotometricLightTree`. |
| `PhotometricLightTree.{h,cpp}` | LM-63 `.ies` parser (`128×32` resampled candela table) + Conty Estevez & Kulla (2018) binary importance light tree (`228+` lamps in `~2 ms`) with unbiased adaptive `split4` / `split8` subtree drawing (`EnclosingIndex` / `FirstNestedIndex` / `NextPeerIndex`). |
| `VisibilityTriangleRasterization.slang` | Optional subpixel-shifted `r32uint` (`instance << 20 \| triangle`) depth raster pass for pinhole primary hits, with exact ray-triangle re-intersection. |
| `DisplayReconstructionFilter.slang` | Coat-aware + virtual-image mirror motion temporal reprojection (`weight × (1 - n/span)²`, `span = 8`) + `5`-step edge-aware à-trous wavelet denoiser (`1..16 px`, normal/plane/material/variance-scaled luminance `σ = 4/n`, fades to `0` at `64 spp`). **Reads raw float sums and writes display buffer only — accumulation sums stay untouched and bit-exact.** |
| `DisplayToneTransform.slang` | Exposure (`EV`), ACES fitted curve, and `sRGB` output, plus non-blocking 3-slot GPU timestamp readback (`reproject · guides · trace · denoise · display`) and live FNV-1a bit-exact repeat verifier. |

### 5.3 Settings-Only Differentiation: `GameRealtime` vs. `SceneFinalRender`

| Configuration Knob | `GameRealtime` Profile (In-Game Live Play) | `SceneFinalRender` Profile (Look-Dev & Offline Scene Render) |
| :--- | :--- | :--- |
| **Integrator Shader** | `DeterministicRadianceIntegration.slang` (**identical**) | `DeterministicRadianceIntegration.slang` (**identical**) |
| **Trace Resolution** | `½` (`2×2` tile = `0.25 spp/dispatch`) or `¼` (`4×4` tile = `0.0625 spp/dispatch`) + tent/guide fill | `1×` (`1:1` full pixel trace) |
| **Samples per Dispatch** | `1` (`1–2` frames in flight, non-blocking pacing) | `1–4` (`target = 128..4096+ spp` until converged) |
| **Sample Pattern** | `BlueNoise` (`64²` void-and-cluster dither for spatial filter decorrelation) | `Shared` (zero grain, pure `(0,m,2)` stratification) or `BlueNoise` |
| **Primary Visibility** | `Rasterized` (`r32uint` primary ID) at `1×` or `Traced` (`BVH4`) at `½` / `¼` | `Traced` (`BVH4` exact primary ray) |
| **Camera Motion & History** | `Reproject` (`span = 8`, virtual-image mirror motion, `0` glossy lag) | `Restart` (`0` temporal history carry-over) |
| **Denoiser (`DisplayReconstructionFilter`)** | **Enabled** (`σ = 4/n`, strength `1 → 0` over `4..64 spp`; `4.06` 8-bit error @ `1 spp`) | **Disabled** (or auto-zeroed at `>= 64 spp`; raw float accumulation exact) |
| **IES Lamp Sampling** | `split4` or `split8` adaptive light tree | `split8` or `all` reference |
| **Indirect Bounces / Specular Depth** | `1–2` indirect bounces · `4–6` specular/glass chain | `2–4` indirect bounces · `10` specular/glass chain (`cutoff = 0.002`) |

### Order 3 Exit Gate
- Bit-exact FNV-1a accumulation digest across `batch = 1 / 4 / 32` dispatches (`0` mismatches), `quadrature` host-vs-GPU check (`0` mismatches across `147,456` coordinates), `analytic` softbox RMS `<= 0.035%` @ `1024 spp`, `50`-vehicle instanced fleet scaling, and side-by-side HTML vs. C++ `.png` proofs in `Exhibits/Images/DeterministicPathTracer/`.
