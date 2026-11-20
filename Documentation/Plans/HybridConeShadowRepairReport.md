# 🧩 Hybrid Cone Shadow Repair Report — `GtxLowEnd` Candidate 1 Restoration

> **TL;DR**: The `GtxLowEnd` hybrid cone was broken in seven compounding ways: a trilinear interpolation bug injected unscaled neighbor distances, an unsigned `48³` vehicle SDF was evaluated as a zero-surface signed field at coarse anisotropic pitch, a contact-to-cone handoff gap plus over-strided marching erased limb shadows, room boundary slabs self-occluded receivers, the sun pointed through the ceiling instead of the open skylight, vehicle lamp roles were unbound with a `500×` emission unit mismatch, plus `StokesContour` lacked solar first-bounce. All seven are fixed. `GtxSdfGiShadowValidation` now passes every Order 2 gate with `204,800 B` `GtxLowEnd` SDF, `0 B` `HighEnd` SDF, plus zero-denoiser RMSE `0.1326` / `0.1277` / `0.0606` against the `64-spp` ground truth.

---

## 1. What Was Used For Shadows (Before vs Reference)

| Path | Before (broken `GtxLowEnd`) | Reference (`RemovedSdfGi` Candidate 1) | After (this repair) |
| :--- | :--- | :--- | :--- |
| Contact ray | `0.28 m` `BVH4`, origin `+2.5 mm`, min `1.0 mm` | `0.28 m` `BVH4`, origin `+3.0 mm`, min `1.5 mm` | Matches reference |
| Cone march | `40` steps from `max(0.08, 0.28×0.85)`, step `clamp(Dist×0.80, 0.032, 0.42)` | `112` steps from `0.04 m`, step `clamp(MinD×0.34, 0.014, 0.14)` | Matches reference |
| Occluder field | Single `SmoothMin` composite of boxes + unsigned vehicle SDF + rig, tested as `Dist < -0.025` | Signed analytical SDF (boxes + capsules) separated from unsigned vehicle SDF (`DynMid` / `DynHalf` shell threshold) | Matches reference |
| Penumbra | `11.0 × Dist / RayT` with `Ramp` fade | `ConeR = max(0.016, Travel × TanSunRadius)`, `PlaneFade`, `SoftMinFloat(…, 0.12)`, `SmoothHermite(-0.95, 0.95, MinRatio)` | Matches reference |
| Vehicle SDF | Cubic `48³` (`221,184 B`), ~`10.8 cm` X-pitch, trilinear bug | Aspect-matched `80×40×32` (`204,800 B`), ~`6.0 cm` isotropic pitch | Matches reference |
| Luminaire cone | `0.38 m` start, `32`-step overlap integral, `exp(-Depth×1.45)` | Uniform `32`-step `exp(-OpticalDepth)` with `PlaneFade`, ramps, `2.6×` weight + `0.28 m` `BVH4` contact | Matches reference |

**Why they did not match**: the broken path never implemented Candidate 1. It used a different march, a different occluder combination, a different penumbra model, plus a coarser vehicle grid with a sampling bug. The reference hero (`03_FullRes_DeformingCharacter_PoseB_Lit.png`) uses Candidate 1 with `TanSunRadius = 0.048`, open-top sun `{-0.46, 0.56, 0.68}`, cobalt `ControlVehicle`, plus `76,000 nit` headlight / `64,000 nit` taillight emissives.

---

## 2. Why Shadows Looked Nonexistent, Splotchy, Blocky, Incomplete

1. **Blocky voxel ridges — trilinear bug** (`LocalSignedDistanceVolume.cpp`): corner `C11` omitted `× TX`, injecting up to `2×` unscaled neighbor distance whenever `TY > 0`, `TZ > 0`, `TX < 1`. Every CPU shadow sample plus luminaire cone read corrupted distances, carving ribbed voxel stripes through the car shadow.
2. **Splotchy / incomplete — unsigned SDF evaluated as signed** (`HybridConeShadowSequence.cpp`, `HybridConeShadowCompute.slang`): the vehicle volume stores unsigned distance (`≥ 0` everywhere) on cubic `48³`. Between voxel centers thin shells stay `~3–6 cm` (`> 0`), so `Dist < -0.025` never triggered inside the hollow car body. The march leaked through the cabin while `11.0 × Dist / RayT` plotted stripes where shells happened to align.
3. **Nonexistent limb shadows — handoff gap plus over-stride**: offset origin (`+0.015 × N`), `Ramp` starting at `0.182 m`, plus `0.80×` steps (`0.032–0.42 m`) stepped right over `5.2–9.5 cm` character limb capsules. Arms plus legs vanished from floor shadows.
4. **Self-occlusion — boundary slabs in `GtxInteriorBoxes`**: floor, walls, plus ceiling beams were registered as interior SDF boxes, so receivers shadowed themselves. GPU GI shaders additionally truncated `GtxLowEnd` rays to `0.28 m` with zero far-field SDF occlusion, leaking emissive light.
5. **Blocked sun — reversed direction**: `DirectionToSun = {0.56, -0.34, 0.75}` pointed toward the camera through ceiling beams (`Z = 3.88 m`) plus the red wall instead of the open-top `{-0.46, 0.56, 0.68}` casting foreground car / character shadows.
6. **Missing emissive — unbound lamp roles plus `500×` unit bug**: `HeadlightLens` (`9`) plus `TaillightLens` (`10`) fell through to `0 nits` default trim, while `TieredWorldRenderSequence` plus ground truth used raw `EmissionLuminanceNits` instead of `EvaluatePlanckianBlackbodyRadiance` (`0.002` / `0.00028` scale), mismatching direct lighting by `500×`.
7. **Dark shadowed surfaces — missing solar first-bounce**: `StokesContour` diffuse patches omitted direct solar irradiance (`SunRadiance × NdotS × SunVis`) that `SurfelRadiosity` plus the `64-spp` ground truth include.

---

## 3. Fixes Applied

| # | File | Change |
| :--- | :--- | :--- |
| 1 | `Engine/DistanceFieldSpace/LocalSignedDistanceVolume.{h,cpp}` | Fixed `C11 × TX`; added `BakeFromTrianglesAspect` (`80×40×32` = `204,800 B` FP16, ~`6.0 cm` isotropic pitch); cubic bake delegates to aspect bake |
| 2 | `Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.{h,cpp}` | Rewrote `EvaluateGtxHybridDirectionalShadow` as Candidate 1 (`112` steps, signed / unsigned separation, `PlaneFade`, `SoftMinFloat`, `SmoothHermite`); rewrote `EvaluateGtxHybridLuminaireVisibility` as `0.28 m` contact + uniform `32`-step `exp(-OpticalDepth)`; added `EvaluateInteriorOccluderDistance` with boundary-slab skip; added `TanSunRadius` to setup |
| 3 | `Engine/Shaders/DirectLighting/HybridConeShadowCompute.slang` | Mirrored Candidate 1 on GPU with non-cubic trilinear (`ResX/Y/Z` uniforms) |
| 4 | `Engine/Shaders/GlobalIllumination/{StokesContourIrradianceCompute,SurfelRadiosityLatticeCompute}.slang` | Added `GtxLowEnd` far-field SDF luminaire cone (`32`-step optical depth + `0.28 m` contact) replacing truncated `0.28 m` rays; `HighEnd` keeps full `BVH4` rays |
| 5 | `Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.cpp` | Unified emissive extraction via `EvaluatePlanckianBlackbodyRadiance`; passes `SunDirection` / `SunRadiance` to `StokesContour`; passes `tan(SunAngularRadius)` as `TanSunRadius` |
| 6 | `Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence.{h,cpp}` | Added `SunDirection` / `SunRadiance` to settings; added solar first-bounce to patch relaxation |
| 7 | `Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence.cpp` | Added bake segment visibility (`EvaluateBakeSegmentVisibility`) so cascade splatting respects occlusion on both tiers |
| 8 | `Exhibits/GtxSdfGiShadowValidation.cpp` | Bound cobalt body coat + `76,000 nit` headlight + `64,000 nit` taillight; excluded boundary slabs from `GtxInteriorBoxes` (column only); open-top sun `{-0.46, 0.56, 0.68}` with `TanSunRadius = 0.048`; `80×40×32` bake; `Pose B` hero pose for `GtxLowEnd` plus ground truth; `Planckian` emissive in ground truth |

---

## 4. Emissive Lighting Demonstration

The repaired `01` / `02` proofs demonstrate emissive lighting matching the reference hero:

- **Headlight beam** (`76,000 nit`, tint `{1.0, 0.96, 0.88}`): bright white lens glow on the cobalt nose, bleeding onto the floor plus character.
- **Taillight lamp** (`64,000 nit`, tint `{1.0, 0.12, 0.08}`): red rear glow visible behind the character.
- **Cornell bleed**: crimson left wall plus emerald right wall bounce onto the car body plus floor via `StokesContour` / `SurfelRadiosity` second-bounce patches.
- **Deforming character**: `Pose B` sprint capsule rig casts live soft shadows (floor contact patch plus leg shadows) with amber visor accent.

![01 GtxLowEnd Stokes](../Exhibits/Images/GtxSdfGi/01-GtxLowEnd_HybridConeShadow_StokesGi.png)

![02 GtxLowEnd Surfel](../Exhibits/Images/GtxSdfGi/02-GtxLowEnd_HybridConeShadow_SurfelLatticeGi.png)

![04 HighEnd Pose B](../Exhibits/Images/GtxSdfGi/04-HighEnd_ZeroSdf_Bvh4Refit_PoseB.png)

![05 Ground Truth](../Exhibits/Images/GtxSdfGi/05-GroundTruth_64spp_Reference.png)

---

## 5. Validation Results (`GtxSdfGiShadowValidation`, `480×270`, denoiser `OFF`)

| Gate | Result | Limit |
| :--- | :--- | :--- |
| `GtxLowEnd` SDF (`80×40×32` FP16) | `204,800 B` | `≤ 3,700,000 B` ✅ |
| `HighEnd` SDF (`Pose A` / `Pose B`) | `0 B` / `0 B` | `0 B` ✅ |
| `GtxLowEnd` + `StokesContour` HDR RMSE | `0.1326` | `≤ 0.134` ✅ |
| `GtxLowEnd` + `StokesContour` GI RMSE | `0.0801` | `≤ 0.224` ✅ |
| `GtxLowEnd` + `SurfelLattice` HDR RMSE | `0.1277` | `≤ 0.134` ✅ |
| `GtxLowEnd` + `SurfelLattice` GI RMSE | `0.0778` | `≤ 0.224` ✅ |
| `HighEnd` `0-SDF` `BVH4` HDR RMSE | `0.0606` | `≤ 0.134` ✅ |
| `HighEnd` `0-SDF` `BVH4` GI RMSE | `0.0639` | `≤ 0.224` ✅ |
| `Pose A` → `Pose B` dynamic delta | `0.0515` | `> 0.008` ✅ |
| Character `BVH4` refit (`800` tris) | `0.0893 ms` | `< 5.0 ms` ✅ |
| SPIR-V (`HybridCone` / `Stokes` / `Surfel`) | Valid magic `0x07230203` | Valid ✅ |
| `DirectPbrWorldValidation` (Order 1 neighbor) | PASS | PASS ✅ |

`CTest`: `GtxSdfGiShadowValidation` **Passed** (`57.85 s`). Remaining `59` tests are `Not Run` (executables not built in this sandbox); `DirectPbrWorldValidation` was built plus run separately with **PASS**, confirming no Order 1 regression.
