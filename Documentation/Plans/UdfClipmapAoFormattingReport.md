# 📝 UDF Clipmap AO Formatting, Magic-Number Remediation & Build Scope Report (`2026-11-22`)

> **TL;DR**: Replaced **64+ inline magic numbers** across `ClusterCullEncoder.h`, `ShowcaseGalleryScene.cpp`, `ApocalypticCityBuilder.cpp`, `VulkanWorldRenderSequence.{h,cpp}`, `WorldViewportRadiometricCompute.slang`, and `DynamicClipmapNaniteViewportValidation.cpp` with vertically aligned, `[unit]`-tagged `constexpr` / `static const` blocks; eliminated raw `new` via value-owned `std::deque<PreparedPyramidDistanceVolume>`; verified **`0` banned-word hits** against `Documentation/BannedWordsVocabulary.md`; and restricted standalone desktop builds + active CTest workspace checks to **only `Frontier` and `SolidArc` (`FrontierSketcher`)** (`8/8` targeted Vulkan render + smoke suites passing).

---

## 🔍 1. Why Magic Numbers Appeared During the Initial Port

During the direct 1:1 port from `Experimental/ShadowAlgorithmLab` (`ao-workbench.js` / `UdfClipmapPorting.md`) into `Frontier`'s C++ / `.slang` radiometric compute pipeline, numeric literals entered the codebase from **three distinct sources** without first being consolidated into Frontier `CONSTANTS` blocks:

| Root Cause | Examples in Initial Port | Why It Happened | Remediation Applied |
| :--- | :--- | :--- | :--- |
| **① Direct JS Workbench Shader Transliteration** | `12u` rays, `10u` march steps, `0.85` trace reach, `0.028` normal bias, `0.72` under-relaxation, `8.0` truncation cells, `0.02..0.12` cascade fade, `0.004` / `0.006` bilateral world variance | `ao-workbench.js` embeds WebGL GLSL tuning literals directly inside loop bodies and `smoothstep()` calls rather than declaring top-of-file shader constants. | Extracted `35` `static const` constants with `[m]`, `[m^2]`, `[rad]`, `[count]`, and `[-]` unit tags in `WorldViewportRadiometricCompute.slang`. |
| **② Unshared Bitfield, Shape-Tag & Datum Encoding** | `0u..6u` shape tags, `0xFFFFu` 16-bit slot/resolution masks, `1u << 16u` active bit, `1u << 17u` ground-slab bit, `-1.0e9f` / `-1.0e8f` ground datum bounds, `1u/2u/4u` denoise step bits, `9u/15u` debug view IDs | C++ host upload code (`VulkanWorldRenderSequence.cpp`) and GPU `.slang` shaders packed flags into existing `std140` `uint4` words (`ResolutionOffset.w`, `DenoiseStepParams.y`, `GlobalDistanceFineToroidalCell.w`) using raw bit shifts at each call site. | Centralized shared `DistanceShape*`, `Instance*BitShift`, and `AnalyticGround*` constants in `ClusterCullEncoder.h` and mirrored matching named masks in `.cpp` / `.slang`. |
| **③ Inline Mathematical & Voxel-Heuristic Constants** | `2.39996323` golden angle $\pi(3-\sqrt{5})$, `52.9829189` / `0.06711056` / `0.00583715` Jimenez IGN hash, `0.035f..0.05f` min voxel pitch, `12u..96u` axis clamps, `4u` brick quantum, `16.0f` / `1.30f` / `0.5f` ground-deck classifier | Per-asset UDF baking (`ShowcaseGalleryScene.cpp`, `ApocalypticCityBuilder.cpp`, `AcquirePyramidDistanceVolume`) wrote grid-sizing clamps and sampling math inline inside helper lambdas. | Extracted vertically aligned `constexpr` blocks with physical units (`[m]`, `[deg]`, `[count]`) at namespace scope in each C++ translation unit. |

---

## 🧩 2. Extracted Named Constants & Formatting Remediation

All modified headers, C++ sources, `.slang` shaders, and validation exhibits now conform to `SKILL-Formatting.md`, `SKILL-Naming.md`, `CodingStandards.md`, and `BannedWordsVocabulary.md`:

| File | Key Structural, Formatting & Constant Cleanups |
| :--- | :--- |
| [`Engine/GeometricRaster/Visibility/ClusterCullEncoder.h`](../../Engine/GeometricRaster/Visibility/ClusterCullEncoder.h) | Added aligned `CONSTANTS` section (`DistanceShapeBakedVolume = 0u` .. `DistanceShapeExcluded = 6u`, `InstanceMaterialSlotMask`, `InstanceActiveBitShift`, `InstanceGroundSlabBitShift`, `UnsetAnalyticGroundPlaneZ`, `ValidAnalyticGroundFloorZ`). |
| [`Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.cpp`](../../Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.cpp) | Replaced `11` inline UDF baking literals with unit-tagged `constexpr` constants (`MinimumBakeSpanMeters`, `MinimumVoxelPitchMeters`, `MinimumAxisResolution`, `MaximumAxisResolution`, `ResolutionAlignmentQuantum`, `MarginSpanFraction`, `MinimumMarginMeters`, `MaximumMarginMeters`); wired `EncodeAnalyticalShapeCode` to `DistanceShape*`. |
| [`Engine/GeometricRaster/GlobalIllumination/ApocalypticCityBuilder.cpp`](../../Engine/GeometricRaster/GlobalIllumination/ApocalypticCityBuilder.cpp) | Extracted `14` aligned `constexpr` UDF constants (`DeerUdfMaxResolution` .. `VehicleUdfMaxResolution`); marked both `OvergrownAsphaltGrassClusters` and `ClimbingTowerIvyCurtains` with `DistanceShapeExcluded` so thin vine/grass AABBs never inject phantom occlusion boxes. |
| [`Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h`](../../Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h) · [`.cpp`](../../Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.cpp) | Replaced `std::unique_ptr` + raw `new` (`CodingStandards.md` §5 violation) and `Cache`-named members (`SKILL-Naming.md` §1) with value-owned `std::deque<PreparedPyramidDistanceVolume> PreparedPyramidVolumes`; extracted `28` aligned `constexpr` UDF, ground-slab, debug-channel, and bilateral-denoise constants. |
| [`Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang`](../../Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang) | Added `35`-entry `UDF CLIPMAP & BILATERAL AO CONSTANTS` table (`UdfTruncationVoxelMultiple`, `GoldenAngleRadians`, `IgnOuterMultiplier`, `UdfAoRayCount`, `UdfAoMaxMarchSteps`, `UdfAoTraceRadiusMeters`, `DenoiseAoWorldFloorSq`, `InstanceGroundSlabBit`, `RadiometricSurfaceBandScale`); expanded shorthand shader locals into full PascalCase identifiers with Allman braces. |
| [`Exhibits/DynamicClipmapNaniteViewportValidation.cpp`](../../Exhibits/DynamicClipmapNaniteViewportValidation.cpp) | Extracted `21` aligned `constexpr` validation constants (`[id]`, `[count]`, `[0-1]`, `[0-255]`) for open-floor luma, tyre-contact drop, and 5-point discrete Laplacian smoothness gates. |

---

## 🛠️ 3. Build & Validation Scope Restricted to `Frontier` + `SolidArc`

Per standing build instructions, standalone workspace executables and CTest checks for unrelated authoring studios were removed from active build and test execution:

| Scope Area | Kept Active | Removed from Active Build / CTest |
| :--- | :--- | :--- |
| **Desktop Executables (`CMakeLists.txt`, CI)** | `Frontier` (`FrontierHostLaunch.cpp`) · `FrontierSketcher` (`SolidArc`, `FrontierSketcherLaunch.cpp`) · `FrontierDefaultContentGenerator` (`EXCLUDE_FROM_ALL`) | `FrontierEditor`, `FrontierUV`, `FrontierBaking`, `FrontierPattern`, `FrontierStamp`, `FrontierTexturePaint`, `FrontierMaterial`, `FrontierFracture` |
| **Validation Exhibits (`ctest`)** | `44` Core / World Render / Atmosphere / Nanite / GI / PathTracer + `Sketcher*Validation` + `FrontierExecutableSmoke` + `FrontierSketcherExecutableSmoke` | `26` unrelated workspace checks (`UV*`, `Fracture*`, `Baking*`, `PatternStamp*`, `VectorGraphicsGPUValidation`, `TexturePaint*`, `Material*`, `ComponentEditor*`, and removed `*ExecutableSmoke` targets) |

---

## ✅ 4. Quantitative Verification Summary

- **Static Governance Audit**: **`0` banned identifier hits** across all modified C++ / `.slang` code (`BannedWordsVocabulary.md` + `SKILL-Naming.md`), **`0` raw `new`/`delete`**, **`0` compiler warnings** in modified files across both `Editor` and `Shipping` profiles.
- **`DynamicClipmapNaniteViewportValidation` (`34/34` PASS)**:
  - `OpenFloor`: `Filt = 254.58 / 255`, `Raw = 254.59 / 255` (`> 246.0` gate — zero 2 m grey skirt).
  - `TyreContactMin`: `Filt = 119.00 / 255`, `Raw = 71.00 / 255`; `CreviceLap`: `Raw = 6.73 -> Filt = 3.79` (`< 6.0` gate).
  - `OpenFloor Clipmap Seam`: `GTX(MeanStep = 0.02, MaxStep = 0.44)`, `BVH4(MeanStep = 0.01, MaxStep = 0.56)`; `Toroidal Slab Scroll`: `16,384 / 262,144` voxels (`6.2%`).
- **Companion Render & Host Suites (`8/8` PASS)**: `DirectPbrWorldValidation`, `GtxGlobalDistanceFieldViewportValidation`, `GtxSdfGiShadowValidation`, `TieredWorldRenderValidation`, `ApocalypticCityValidation` (`3/3` shots, `Objects = 55`), `FrontierExecutableSmoke`, and `FrontierSketcherExecutableSmoke` (`SolidArc`).
