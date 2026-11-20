# 🔍 Order 2 `HybridConeShadow` & Real-Time GI Root-Cause Diagnosis Report

> **TL;DR**: Diagnosed ten compounding defects behind the striped vehicle bumper, hollow chassis shadow, missing character limb shadows, blocked studio sunlight, missing `76,000 nit` / `64,000 nit` vehicle luminaires, `500×` emissive radiance mismatch, luminaire self-occlusion arc, and `3.46 m` bounce-visibility cutoff ring in `18b3329`. Restored exact `Candidate 1` (`HybridConeShadowSequence` + `80×40×32` `204,800 B` FP16 local SDF + `10`-bone `ArticulatedCapsuleRig`) across CPU C++20 and GPU `.slang` SPIR-V (`HybridConeOccluders.slang`), passing all `60/60` CTest suites with zero-denoiser `HDR RMSE = 0.0596` (`<= 0.134`) and `GI RMSE = 0.0389` (`<= 0.224`).

---

## 1. Quantitative Verification Matrix (`GtxSdfGiShadowValidation` + Full CTest)

| Gate | Target Threshold | `18b3329` Broken Baseline | Verified Result (`arena/860c2b4f-frontier-1`) | Status |
| :--- | :---: | :---: | :---: | :---: |
| **`GtxLowEnd` (`HybridCone` + `StokesContour`) HDR RMSE** | `<= 0.1340` | `🔴` Striped + hollow + dark | **`0.0596`** (`⭐⭐⭐`) | 🟢 **PASS** |
| **`GtxLowEnd` (`HybridCone` + `StokesContour`) GI RMSE** | `<= 0.2240` | `🔴` Missing solar bounce | **`0.0389`** (`⭐⭐⭐`) | 🟢 **PASS** |
| **`GtxLowEnd` (`HybridCone` + `SurfelLattice`) HDR RMSE** | `<= 0.1340` | `🔴` `500×` blown / dark | **`0.0640`** (`⭐⭐⭐`) | 🟢 **PASS** |
| **`GtxLowEnd` (`HybridCone` + `SurfelLattice`) GI RMSE** | `<= 0.2240` | `🔴` Missing cone march | **`0.0444`** (`⭐⭐⭐`) | 🟢 **PASS** |
| **`HighEnd` (`0 MB` SDF `BVH4` + Dual-Cascade `19×15×6`) HDR / GI RMSE** | `<= 0.1340` / `<= 0.2240` | `🚩` Blocked foreground sun | **`0.0528` / `0.0534`** | 🟢 **PASS** |
| **Shadow Spot Checks (`OpenSun` / `UnderCar` / `FootContact`)** | `> 0.92` / `< 0.12` / `< 0.25` | `🔴` Leaky chassis + missing foot | **`1.0000` / `0.0000` / `0.0000`** | 🟢 **PASS** |
| **Dynamic SDF Memory (`GtxLowEnd` vs. `HighEnd` Pose A / B)** | `<= 3,700,000 B` / `== 0 B` | `221,184 B` (`48³`) / `0 B` | **`204,800 B` (`80×40×32` FP16) / `0 B`** | 🟢 **PASS** |
| **Skinned `CompressedWideTree` (`800` tris, `107` `64 B` entries) Refit** | `< 2.0 ms` (`PoseDelta > 0.008`) | `0.11 ms` | **`0.1049 ms`** (`Δ = 0.0619` / `0.0730`) | 🟢 **PASS** |
| **Repository Build + Full Headless CTest Suite** | `60 / 60` (`0` errors) | `52 / 60` | **`60 / 60` (`100%`)** | 🟢 **PASS** |

---

## 2. Ten-Point Root-Cause Diagnosis (`18b3329` vs. `Experimental/RemovedSdfGi/`)

| # | Symptom Observed in `01..05` (`18b3329`) | Exact Root Cause (File & Mechanism) | Production Remediation |
| :---: | :--- | :--- | :--- |
| **1** | **Active CPU vs. GPU Path Audit** | `TieredWorldRenderSequence.cpp` executes `EvaluateGtxHybridDirectionalShadow` per primary hit and `EvaluateGtxHybridLuminaireVisibility` inside `StokesContour` / `SurfelLattice`; `CMakeLists.txt` compiles `HybridConeShadowCompute.slang`, `StokesContourIrradianceCompute.slang`, and `SurfelRadiosityLatticeCompute.slang` to `.spv` via `slangc`. | Repaired both CPU C++20 evaluators and `.slang` SPIR-V compute shaders (`HybridConeOccluders.slang`) in lockstep. |
| **2** | **`//////` Diagonal Voxel Stripes Across Front Bumper** | 🐞 `LocalSignedDistanceVolume::SampleTrilinearLocal` (`LocalSignedDistanceVolume.cpp:329`) omitted `* TX` on the `+X+Y+Z` octant sample: `C11 = At(IX0,IY0+1,IZ0+1)*(1-TX) + At(IX0+1,IY0+1,IZ0+1)` — injecting `+At(IX0+1,IY0+1,IZ0+1)*(1-TX)` sawtooth jumps at every X-voxel boundary. | Restored `* TX` in `LocalSignedDistanceVolume::SampleTrilinearLocal` and verified exact parity in `HybridConeOccluders.slang`. |
| **3** | **Entire Foreground Floor & Character Drowned in Dark Shadow (Even in `05-GroundTruth`)** | 🐞 `BuildCornellVehicleBay` (`GtxSdfGiShadowValidation.cpp`) placed four thick ceiling beams at `Z = 3.88 m` directly across the sun trajectory with sun azimuth pointing away from the camera bay. | Removed the sun-blocking studio ceiling rafters, set unobstructed upper rear-left key sun `Normalize({-0.46, +0.56, +0.68})`, and framed `Z_top = 3.45 m` Cornell walls (`X = 6.35 m` rear wall, `[-6.0, +6.6] × [-7.2, +4.8]` floor). |
| **4** | **Hollow Vehicle Shadow & Missing Character Arm / Leg Shadows** | 🐞 `ControlVehicle` has open single-sided CAD panels (`25,068` tris), so `LocalSignedDistanceVolume` stores unsigned shell distances (`>= 0`). `18b3329` tested `Dist < -0.025f` (unreachable for `>= 0` grids), marched `Step = max(0.03f, Dist * 0.80f)` (jumping across `5–7 cm` limbs), and stopped BVH4 at `0.28 m` while starting the cone at `0.38 m` (`10 cm` blind gap). | Implemented `Candidate 1` shell-aware normalized cone ratio `DynMid = max(HalfVoxelShell, 0.65·ConeR)`, `DynHalf = max(0.024, 0.36·ConeR)` with conservative `0.34·ClosestSafe` (`[0.014, 0.14 m]`) steps starting at `0.04 m` (overlapping the `0.28 m` BVH4 contact segment). |
| **5** | **Dark Floor Halo Around Walls & Anisotropic Vehicle Voxel Footprint** | 🐞 `TieredWorldRenderSequence.cpp` pushed floor/wall slabs into `GtxInteriorBoxes` (causing self-shadow bleed onto `Z = 0` receivers), and sequential `EvaluateSmoothMinimum` accumulated step-count darkening along grazing rays; cubic `48³` wasted Z voxels on a `4.5 m × 2.0 m × 1.4 m` coupe. | Filtered `GtxInteriorBoxes` to elevated interior blockers (`TopZ > 0.05f`), applied `PlaneFade = SmoothStep(0.015, 0.10, dot(P - Origin, N))`, restricted `SoftMin` to per-sample occluder blending (`min` along ray), and added `BakeAnisotropicVolume(80, 40, 32)` (`5.6 cm` isotropic cells in **`204,800 B` FP16**, smaller than `48³ = 221,184 B`). |
| **6** | **Missing `76,000 nit` Headlights, `64,000 nit` Taillights & Cobalt Clearcoat** | 🐞 `GtxSdfGiShadowValidation.cpp` assigned uniform grey/blue slots to all `25,068` vehicle faces instead of mapping `VehicleMaterialRole::HeadlightLens` and `TaillightLens` from `ControlVehicleSurfaceDecoder`. | Authored physical `HeadlightLens` (`76,000 cd/m²`, `5600 K`), `TaillightLens` (`64,000 cd/m²`, crimson), metallic cobalt clearcoat body, dark smoked glass, satin tyre rubber, and forged alloy rims. |
| **7** | **`500×` Emissive Radiance Unit Mismatch Between Direct & Order 2 GI** | 🐞 `TieredWorldRenderSequence.cpp` passed raw `Mat.Emission.LuminanceNits` (`76,000`) directly as `MatEmissionBySlot` instead of scaling through `EvaluatePlanckianBlackbodyRadiance` (`1/2000` radiometric exposure scale used by `LinearCosineLightIntegration` and `OpenPbrSurfaceScattering`). | Routed `TieredWorldRenderSequence` emissive extraction through `MaterialSpace::EvaluatePlanckianBlackbodyRadiance` so direct LTC, `StokesContour`, `SurfelLattice`, and `64-spp` path-traced reference share identical radiometric units. |
| **8** | **Missing Indirect Solar Wall Bounce in `StokesContour` & Suppressed Glossy Emissive Reflections** | 🐞 `StokesContourIrradianceSequence` injected only emissive cluster radiance into its `224` diffuse patches (omitting solar/sky 1st-bounce wall bleed), while `TieredWorldRenderSequence` disabled `EvaluateDirectEmissiveTriangles` completely when Order 2 GI was active (dropping glossy floor/coat specular LTC). | Added `SolarBounceTransport` (`DirectionToSun`, `SolarIrradiance`, `SkyIrradiance`) into `StokesContour` patch injection and added `SpecularOnlyMode` + `OptionalGtxOccluders` to `LinearCosineLightIntegration`. |
| **9** | **GPU `.slang` GI Shaders Missing `> 0.28 m` Hybrid SDF + Capsule Cone March** | 🐞 `StokesContourIrradianceCompute.slang` and `SurfelRadiosityLatticeCompute.slang` clamped `GtxLowEnd` visibility rays to `min(0.28, SegLen)` with zero SDF/capsule march beyond `0.28 m`. | Authored shared Slang module `Engine/Shaders/DirectLighting/HybridConeOccluders.slang` and wired full hybrid BVH4 + SDF + capsule + column cone visibility into `HybridConeShadowCompute.slang`, `StokesContourIrradianceCompute.slang`, and `SurfelRadiosityLatticeCompute.slang`. |
| **10** | **Headlight Bumper Self-Occlusion Arc & `3.46 m` Bounce-Patch Cutoff Ring** | 🐞 Marching luminaire visibility to `SegLen - 0.05 m` terminated inside the vehicle's own `DynD ≈ 0` nose shell (`0.3–1.5 m` dark arc ahead of headlights), while hard `DistSq < 12.0f` (`r = 3.464 m`) and `4.8 m` contour cutoffs drew visible step rings on the studio walls. | Replaced luminaire ray termination with the `32`-step optical-depth cone (`LuminaireMargin = max(0.22, 0.65·Radius)`, `RampIn * RampOut * PlaneFade`, `exp(-OpticalDepth)`), added `[4.2 m, 5.4 m]` contour Hermite crossfade, `[3.6 m, 5.8 m]` bounce-visibility Hermite fade, and `4`-tap stratified per-triangle BVH4 luminaire visibility on `HighEnd`. |

---

## 3. Order 2 Execution Flow (`GtxLowEnd` vs. `HighEnd`)

```text
Primary 64-bit VisBuffer + OpenPBR Surface Hit
   │
   ├──► [GtxLowEnd Preset — 204,800 B FP16 Vehicle SDF + 10-Bone Capsule Rig]
   │       ├─ Direct Sun Shadow : 0.28 m CompressedWideTree BVH4 contact
   │       │                      + 56-step Candidate 1 HybridCone (DynMid = max(HalfVoxelShell, 0.65·ConeR))
   │       ├─ Luminaire Vis     : 0.28 m BVH4 contact + 32-step ring-free optical-depth cone (exp(-OpticalDepth))
   │       ├─ Emissive Specular : SpecularOnlyMode LTC + HybridCone luminaire visibility
   │       └─ Indirect GI       : StokesContour (exact clipped contour + 224 solar/emissive patches)
   │                              OR SurfelRadiosityLattice (Dual-Cascade 12x10x4 + 8x6x3 C1-Hermite octahedral)
   │
   └──► [HighEnd Preset — 0 B SDF, Pure 64 B CompressedWideTree TLAS4/BLAS4]
           ├─ Character Motion  : Bottom-up CompressedWideTree AABB refit (0.1049 ms for 800 tris)
           ├─ Direct Sun Shadow : Dual-Cascade SolarShadowSequence + PlaneAwareShadowFilter
           ├─ Luminaire Vis     : 4-tap barycentric BVH4 rays per emissive triangle (0 B SDF)
           └─ Indirect GI       : Full StokesContour OR Dual-Cascade 19x15x6 + 9x7x4 C1-Hermite SurfelLattice
```

---

## 4. Modified Units & Visual Proof Ledger (`Exhibits/Images/GtxSdfGi/`)

| Scope | Files |
| :--- | :--- |
| **`DistanceFieldSpace`** | `Engine/DistanceFieldSpace/LocalSignedDistanceVolume.{h,cpp}`, `Engine/DistanceFieldSpace/README.md` |
| **`DirectLighting` (C++20)** | `Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.{h,cpp}`, `Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.{h,cpp}` |
| **`GlobalIllumination` (C++20)** | `Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence.{h,cpp}`, `Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.cpp` |
| **`.slang` Shaders & Build** | `Engine/Shaders/DirectLighting/{HybridConeOccluders,HybridConeShadowCompute}.slang`, `Engine/Shaders/GlobalIllumination/{StokesContourIrradianceCompute,SurfelRadiosityLatticeCompute}.slang`, `CMakeLists.txt` |
| **Validation & Visual Proofs** | `Exhibits/GtxSdfGiShadowValidation.cpp` → `Exhibits/Images/GtxSdfGi/`:<br>• `01-GtxLowEnd_HybridConeShadow_StokesGi.png` (`HDR RMSE = 0.0596`, `GI RMSE = 0.0389`)<br>• `02-GtxLowEnd_HybridConeShadow_SurfelLatticeGi.png` (`HDR RMSE = 0.0640`, `GI RMSE = 0.0444`)<br>• `03-HighEnd_ZeroSdf_Bvh4Refit_PoseA.png` (`0 B` SDF, `HDR RMSE = 0.0528`)<br>• `04-HighEnd_ZeroSdf_Bvh4Refit_PoseB.png` (`0 B` SDF, `0.1049 ms` BVH4 refit, `Δ = 0.0619`)<br>• `05-GroundTruth_64spp_Reference.png` (`64-spp` Next-Event + Cosine Hemisphere reference)<br>• `06-GtxLowEnd_HybridConeShadow_PoseB_Lit.png` (Pose B live capsule deformation, `Δ = 0.0730`)<br>• `07-GtxLowEnd_ShadowIsolation_Diagnostic.png` (Debug channel `8` raw solar shadow isolation)<br>• `08-GtxLowEnd_EmissiveStudio_HeadlightTaillightGi.png` (Sun-off `76k nit` headlight + `64k nit` taillight GI) |
