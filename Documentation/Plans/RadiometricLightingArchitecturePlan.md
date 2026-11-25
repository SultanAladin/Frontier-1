# 🧩 `RadiometricLightingArchitecturePlan` — Unified Many-Light Photometric Architecture

> **TL;DR**: Unify all 7 luminaire classes (**LM-63 IES**, **Automotive ECE/SAE/DRL**, **Polygonal Area LTC**, **Linear Tube/Ribbon**, **Point/Spot**, **Emissive Surface/LED Volume**, and **Celestial/Portal**) into one GPU **`RadiometricLuminaireRegister`** indexed by a **Spatial-Orientation Cone Partition (`LightPartitionEntry32`)**. Every render mode (`0=MatCap` through `5=PathTracerSceneFinalRender`) evaluates **100% analytical BRDF integrands** (`Dual-Lobe Clipped LTC` + `128x32 IES` + `Picott/Karis Tube` + `Stokes Spherical-Polygon Contour`), using **Deterministic Adaptive Split-Cut (`K = 8..12`)** in real time (`Engines 1..3`), **local `C0` SDF + `ArticulatedCapsuleRig` strictly for soft scalar visibility** on `GtxLowEnd` (`Engine 2`), and **Owen-scrambled Sobol `(0,2)`-net MIS** in `PathTracer` (`Engines 4..5`).

---

## 🔍 1. Audited Baseline Gaps in `Engine/` (`2026-11-25`)

| Existing Unit | Verified Capability | Blocking Gap Before `AutoSpace` 3-Lobby Build |
| :--- | :--- | :--- |
| `Engine/Codecs/PhotometricProfile.{h,cpp}` · `PhotometrySpaceCodec.{h,cpp}` | Parses ANSI/IESNA LM-63-1995/2002 Type-C `.ies`, folds C-plane symmetry, resamples `128x32` (`4,096` float) candela grid + `CutoffHalfAngleRad`. | ✔️ Complete; add analytic 4-leaf barn-door frustum & ECE `-1.0°` kink mask parameters to `GpuRadiometricLuminaire64`. |
| `Engine/Editor/Inspectors/*Light*Inspector.{h,cpp}` (`8` files) | All 7 Project-Zero physical luminaire UIs (`Point`, `Spot`, `IES`, `Area`, `Tube`, `LED`, `Environment`) already built in C++20. | 🚩 `TubeLightInspector` & `LedLightInspector` lack a GPU analytic line-segment / capsule integrator in `WorldViewportRadiometricCompute.slang`. |
| `Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.{h,cpp}` | Exact Heitz (2016) upper-hemisphere clipped-polygon LTC diffuse + GGX quad integrator. | 🔴 `WorldViewportUniformBlock` hardcodes `GpuAreaQuadLight AreaLights[4]` (`WorldViewportRadiometricCompute.slang:48`), capping the entire world to **4 area lights** (insufficient for softboxes + umbrellas + rim panels + LED walls + car taillamp bars). |
| `Engine/GeometricRaster/DirectLighting/PhotometricLightTree.{h,cpp}` | Builds breadth-first Conty Estevez & Kulla (2018) binary spatial-orientation cone hierarchy (`PackUnifiedLightRegister`). | 🔴 **Not traversed in `WorldViewportRadiometricCompute.slang`**: all 5 shader stages (`lines 2512, 2674, 3040, 3798, 5070`) execute an `O(N)` brute-force `for (PL = 0; PL < PhotoLampCount; ++PL)` loop and exclude area, tube, and emissive lights from the hierarchy. |
| `WorldViewportRadiometricCompute.slang` (`EmissiveRadiancePad`) | Emissive triangles (`Lobby 2` Forest LED Volume, car DRLs, pit screens) contribute only when stochastically hit by GI/PT rays. | 🔴 Zero direct illumination in `Engine 1 (OpenPbrDirect)` and high 1-spp firefly variance on glossy automotive clearcoat (`α_coat ≈ 0.02`) in `Engine 4`. |

---

## 🏆 2. Mandatory Decision Tables

### 2.1 Many-Light Direct Solver Selection (`50–512+` Active Luminaires)

| Rank | Candidate Architecture | Primary Raster (`Engines 1..3`) | Off-Screen Clearcoat Reflections & Surfel GI | `PathTracer` (`Engines 4..5`) | Hot-Overlap (`60–228` Lamps on Hero Car) | Dynamic Zero-Ghosting (`CLAUDE.md` / `WorldRenderingRedoPlan`) | Indoor / Outdoor `100:1` HDR (`Lobby 3` Pit Garage) | Verdict |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| 🥇 | **⭐ Unified `RadiometricLuminaireRegister` + Spatial-Orientation Cone Partition (`Deterministic Split-Cut K=8..12` + Sobol MIS)** | **🟢 `O(log N + K)`** zero-noise exact LTC/IES/Tube/Stokes | **🟢 Identical world-space query** at any 3D reflection or surfel point | **🟢 Same hierarchy** provides exact PMF for Sobol NEE + MIS | **🟢 Prunes dead cones**, evaluates top-`K` exact + energy-preserving cluster representatives (`10x` speedup verified in `ManyLights.json`) | **🟢 `0` temporal history** — zero wheel/lift/turn-signal ghosting | **🟢 Orientation + portal cone bounds** isolate indoor pit bay from occluded stadium floodlights | **🟢 Selected across all 6 engines** |
| 🥈 | **Optional `16x16` Screen-Tile Leaf Bitmask Acceleration** *(Strictly as primary-ray pre-filter in front of `LightPartition`)* | **🟢 Fast `uint64` wave mask** for primary camera rays | 🔴 Unusable off-frustum (still requires `LightPartition`) | 🔴 Unusable for multi-bounce path tracing | 🚩 Bitmask still leaves `40+` overlapping showroom lights active per tile | **🟢 Stateless per tick** | ✔️ Neutral | **✔️ Optional Phase-2 micro-optimization only** |
| 🥉 | **Clustered Forward / Deferred (`3D Camera Froxels`)** | 🚩 Conservative range spheres over-assign narrow `35 m` ECE headlamps & `7.2 m` softboxes to thousands of froxels | **🔴 Fails completely** on off-screen `BVH4` clearcoat reflections & world-space GI surfels | **🔴 Fails** (cannot compute importance PDFs at bounce vertices) | **🔴 Still `O(K)` shades + `O(K)` shadow queries** when `60` showroom/car lights physically overlap the chassis | **🟢 Stateless** | 🚩 Artificial tight range clamps required to avoid froxel overflow clip floor reflections | **🔴 Rejected as primary architecture** |
| 4 | **Spatiotemporal ReSTIR DI** *(Temporal + Screen-Neighbor RIS)* | 🔴 Replaces silky analytic LTC softboxes & sharp ECE `-1.0°` cutoffs with 1-spp stippled point noise requiring heavy temporal blur | 🔴 Screen-space reservoirs invalid for secondary reflection hits & world-space surfels | 🚩 Biased unless expensive visibility reuse / shift Jacobians run per bounce | 🚩 `α_coat = 0.02` clearcoat normal curvature breaks neighbor Jacobians → boiling specular fireflies | **🔴 Violates zero-temporal-lag rule**: smears highlights across spinning spokes, hydraulic lifts & blinking hazards | **🔴 Unoccluded target-PDF starvation**: `90 klx` outdoor sun steals 95% of candidate draws inside concrete pit bay | **🔴 Rejected for real-time (`Engines 0..3`); deterministic zero-temporal Sobol RIS restricted to `Engines 4..5` residual tail** |
| 5 | **Pure 3D Voxel / SDF Light Injection** | 🔴 `8–25 cm` voxels obliterate ECE cutoffs, barn doors, DRLs & rectangular clearcoat highlights | 🚩 Coarse diffuse bounce only; zero sharp specular reflections | 🔴 Incompatible | ✔️ `O(1)` lookup | 🚩 Clipmap cascade scroll shimmer | 🔴 Bleeds exterior sunlight through thin garage walls & car fenders | **🔴 Rejected for direct lighting (used strictly as `SurfelRadiosityLattice` far-field indirect bounce)** |

> 💡 **Recommendation**: Adopt **Rank 🥇 (`RadiometricLuminaireRegister` + Spatial-Orientation Cone Partition)** as the single world-space light structure shared across primary shading, `BVH4` dual-layer clearcoat reflections, `SurfelRadiosityLattice` GI injection, and `DeterministicPathTracer` NEE/MIS.

---

### 2.2 SDF Lighting vs. Analytical Lighting Across Render Tiers

| Rank | Evaluation Strategy | Sharp Photometry (ECE `-1.0°` Kink, Barn Doors) | Glossy Clearcoat Softbox / Tube Reflections (`α = 0.02`) | Thin Sheet-Metal & Tire Contact (`2–8 mm` Fenders) | `GtxLowEnd` (`GTX 1650 SUPER` 60 FPS Budget) | Cross-Engine Radiometric Parity (`Engine 1 ↔ 2 ↔ 3 ↔ 5`) | Verdict |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| 🥇 | **⭐ 100% Analytical Radiometry (`LTC` + `IES` + `Tube` + `Stokes`) × Engine-Tiered Visibility (`HybridConeShadow` on `Engine 2`, `BVH4` on `Engine 3..5`)** | **🟢 Exact angular resolution** (`128x32` LM-63 + analytic frustum clip) | **🟢 Exact view-dependent clipped polygon / segment specular** | **🟢 Zero fender light-bleed**; `0–0.45 m` depth + `ArticulatedCapsuleRig` locks crisp `0 px` tire contact | **🟢 Fits `≤ 2.4 ms`**: analytical math is ALU-bound; local `48³` `C0` SDF traces only top-`4` soft penumbra cones (`> 0.35 m`) | **🟢 Identical unoccluded radiance** across every engine; only visibility precision scales with tier | **🟢 Selected Mandate** |
| 🥈 | **100% Analytical Radiometry × Pure `BVH4` Ray-Traced Shadows on All Tiers** | **🟢 Exact** | **🟢 Exact** | **🟢 Exact triangle contact** | 🔴 Software `BVH4` multi-tap area/IES penumbra rays exceed GTX 1650 frame budget at 1080p when many lights overlap | **🟢 Exact** | **🟢 Used on `Engine 3 (HighEnd)` & `Engines 4..5`; too heavy for `Engine 2 (GtxLowEnd)`** |
| 🥉 | **Pure SDF Lighting** *(SDF gradient normals / voxelized light radiance / SDF-only contact shadows)* | **🔴 Destroyed** by voxel Nyquist limit | **🔴 Impossible** (scalar field lacks narrow GGX view lobes) | **🔴 Severe light leaking** through hoods/doors + rounded floating tire blobs | ✔️ Cheap ALU | **🔴 Diverges visually** from `OpenPbrDirect` & `PathTracer` | **🔴 Strictly Forbidden (`CLAUDE.md` §1.2 & §2.1)** |

> 💡 **Direct Answer — Should SDF Tiers Use SDF Lighting or Analytical Lighting?**
> **Always use 100% Analytical Lighting for radiance, photometry, and BRDF integration (`Heitz LTC`, `LM-63 128x32 IES`, `Picott/Karis Tube`, `Stokes Contour`) across every mode — including `Engine 2 (RealTimeGiGtxLowEnd)`.** Never evaluate light falloff, color, beam shape, or specular reflections through an SDF/voxel grid. On `Engine 2`, SDF (`local 48³ C0` grid paired with `0–0.45 m` screen-space depth raymarch and `ArticulatedCapsuleRig` chassis/wheel cones) computes **only the scalar mid/far-field soft shadow factor `Vis ∈ [0, 1]`** that multiplies the exact analytical BRDF result.

---

## 🧩 3. Unified 7-Class Luminaire Taxonomy & `64-Byte` GPU Layout

All active world luminaires pack contiguously into `StructuredBuffer<GpuRadiometricLuminaire64>` (`64 B` cache-line aligned, replacing both `WorldViewportUniformBlock.AreaLights[4]` and standalone `GpuPhotometricLamp64`):

```
GpuRadiometricLuminaire64 (64 Bytes = 4 x float4, 16-byte aligned)
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ Word 0 (float4 PositionRangeKind) : xyz = WorldPosition [m], w = PackKindFlagsRange()    │
│ Word 1 (float4 AxisRightWidth)    : xyz = TangentRight / SegDir, w = HalfWidth / HalfLen │
│ Word 2 (float4 AxisUpHeight)      : xyz = BitangentUp / ZeroAzimuth, w = HalfHeight / Rad│
│ Word 3 (float4 RadianceProfile)   : xyz = LinearRgbRadianceOrPeakCd, w = AuxParamsPacked │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

| Class ID | Luminaire Category | `AutoSpace` Examples (`Lobbies 1–3` + Vehicles) | Analytical Radiometric Integrand (`.slang`) | Visibility / Shadow Strategy |
| :---: | :--- | :--- | :--- | :--- |
| `0` | **`PointSphere`** | Under-chassis accent bulbs, gauge cluster spill, courtyard globes | Non-singular sphere exitance `(1 - (d/R)^4)^2 / (d^2 + r^2)` + **Karis representative sphere point** on dual-lobe GGX | Top-`S` shadow cut (`HybridConeShadow` cone angle `r/d` on `E2`; `BVH4` sphere disk tap on `E3..5`) |
| `1` | **`AnalyticSpot`** | Track spots, garage task lamps, theatrical pin-spots | Smooth dual-cone `smoothstep(cosOuter, cosInner, dot(-L, Axis))` × non-singular disk Karis GGX | Exact frustum/cone early-out + top-`S` aperture shadow |
| `2` | **`PhotometricIes`** | Studio **Barn-Door Fresnel**, Octa Strobe, Car-Park Downlight, Stadium Floodlight | Bilinear **`128x32` LM-63 Type-C candela lookup** × **Analytic 4-Leaf Barn-Door Frustum Mask** (`BarnFlareU`, `BarnFlareV`, `EdgeSoftness`) | Cone + barn-door frustum reject before any shadow ray; soft source radius `r` penumbra |
| `3` | **`AutomotiveLamp`** | **ECE/SAE Low-Beam** (`-1.0°` asymmetric step kink), **High-Beam**, **Fog Bar**, **Tail/Brake**, **Sequential Amber Turn** | `128x32` measured `.ies` (`LowBeamHeadlamp`, `TailLampRed`) + **exact analytic ECE horizontal/kinked cutoff plane** (`PitchCutoffRad`, `AsymKink15Deg`) so the horizon line stays razor-sharp at every distance | High-priority hero vehicle flag guarantees exact shadow slot for player/hero headlamps |
| `4` | **`PolygonalAreaLtc`** | `7.2×4.0 m` **Overhead Silk Softbox**, `1.55 m` **Parabolic Umbrella** (Disk/Octa), `1.15×0.68 m` **Rim LED Panel** | **Dual-Lobe Clipped Heitz LTC**: (1) Clearcoat GGX (`α_coat`, `F_coat`), (2) Basecoat Metallic/Dielectric GGX (`α_base`), (3) Exact clipped spherical-polygon diffuse form factor | `E1`: unoccluded / contact AO; `E2`: `HybridConeShadow` (`r_eff / d`); `E3`: 4-tap rotated Vogel aperture `BVH4` + `PlaneAwareShadowFilter`; `E4..5`: **Ureña (2018) solid-angle quad sampling** |
| `5` | **`LinearTubeSegment`** | **Lobby 3 Garage Linear Ceiling LEDs**, **Automotive Shaped DRLs**, **Full-Width Rear Lightbar**, LED Ribbons | **Picott Closed-Form Line-Segment Diffuse** integral + **Karis Closest-Point-on-Segment** energy-normalized Dual-Lobe GGX (`RadiusMeters`, `HalfLengthMeters`) | Segment closest-occlusion-point cone (`E2`) or stratified 2-tap segment `BVH4` query (`E3..5`) |
| `6` | **`EmissiveSurface`** | **Lobby 2 Curved Forest LED Volume Wall**, pit telemetry monitors, illuminated badges, neon strips | **Two-Tier Exact Extraction**:<br>• **Near (`d ≤ 4.8 m`)**: Zero-noise **Stokes Spherical-Polygon Contour** (`F = \|∮ θ (e×n)/\|e×n\|\| / 2π`) + Representative Specular Point<br>• **Far (`d > 4.8 m`)**: `LightPartition` cluster leaf + **`SurfelRadiosityLattice`** `O(1)` C1-Hermite indirect lookup | Near-field analytic horizon clip + `HybridConeShadow` / `BVH4` blocker check; zero 1-spp emissive fireflies |
| `7` | **`EnvironmentPortal`** | **3-LUT Bruneton Sky + Neckel-Labs Sun** (`SunDirConeTan`) + **Lobby 3 Open Pit-Garage Archway Portal** | Solar `erfc` limb-darkened disc + SkyView LUT + **Portal Rect LTC Window**: guides exterior sky luminance through architectural openings into dark interiors | Sun: Cascaded Frustum (`E1..2`) / Vogel Solar `BVH4` (`E3..5`); Portal: LTC clipped archway polygon × interior blocker cone |

---

## 🏷️ 4. Spatial-Orientation Cone Partition & Cross-Engine Execution

### 4.1 Single Buffer Layout (`RadiometricLuminaireRegister`)

```
StructuredBuffer Packing (Single Descriptor Binding, Zero UBO Size Pressure)
┌───────────────────────────┬────────────────────────────┬──────────────────────────────────┐
│ Header (16 B)             │ Luminaires [0 .. L-1]      │ PartitionEntries [0 .. 2L-2]     │
│ {L, NodeCnt, ProfCnt, Cut}│ 64 B × L (All 7 Classes)   │ 32 B × (2L-1) Bounding Cone BVH  │
└───────────────────────────┴────────────────────────────┴──────────────────────────────────┘
  + Binding 22: PhotometricProfileTables [128 × 32 floats × P] (Existing LM-63 resampled atlas)
```

Each `LightPartitionEntry32` (`32 B`) stores `{ AabbMin, TotalPower, AabbMax, ConeAxisOct + CosAxisHalfAngle + CosEmissionSpread, LeftOrLeafIndex, LuminaireCount }`:
1. **CPU Build / Refit Cost**: `< 0.06 ms` for `L = 256` luminaires (`< 0.14 ms` for `L = 1,024`); static architectural lights stay in a persistent upper subtree while dynamic vehicle lamps (`4–50` cars × `6` lamps) refit in `< 0.02 ms` per tick.
2. **Upper-Bound Importance Metric at Shading Point `(P, N, V)`**:
   $$I_{\max}(C, P, N, V) = \frac{\Phi_C}{\max(d_{\min}^2(P, \text{AABB}_C), r_0^2)} \cdot \max\cos(\theta_i - \theta_{\text{bound}}) \cdot \max\cos(\theta_e - \theta_{\text{axis}} - \theta_{\text{spread}}) \cdot G_{\text{coat}}(V, N, \text{AABB}_C)$$
   Including the **clearcoat reflection cone term $G_{\text{coat}}$** ensures a dim rim panel or LED tube that sits **directly on the car's specular reflection vector $R = \text{reflect}(-V, N)$** is never merged into a coarse diffuse cluster!

### 4.2 Exact Behavior Across All 6 `WorldRenderEngine` Modes

| Engine Ordinal | Mode Name | Direct Luminaire Evaluation | Shadow / Visibility Budget | Emissive & Environment GI |
| :---: | :--- | :--- | :--- | :--- |
| **`0u`** | **`MatCap`** | Optional **Key-Rig Orientation Lock** (rotates MatCap tangent basis toward dominant `LightPartition` luminaire when enabled; defaults to view-locked studio clay/zebra) + direct self-emissive surface display (`DRLs`, `LED Wall`) | `0` shadow queries · `0 MB` SDF | Pure sphere-mapped studio MatCap (`EngineContent/MatCaps/`) |
| **`1u`** | **`OpenPbrDirect`** | Deterministic `LightPartition` **Split-Cut (`K = 8`)**: exact Dual-Lobe LTC + `128x32` IES + Analytic Tube + near Stokes Emissives + cluster tail | **`0 MB` SDF** · Cascaded Sun Shadow + Top-`4` `BVH4` any-hit / screen-space contact shadows | 3-LUT Sky + split-sum environment specular (`0` multi-bounce GI) |
| **`2u`** | **`RealTimeGiGtxLowEnd`** *(SDF / Hybrid Tier)* | Identical analytical `Split-Cut (K = 8)` shading (`100%` analytical BRDF — **zero SDF shading**) | **`HybridConeShadowSequence`**: `0–0.45 m` depth + `ArticulatedCapsuleRig` analytic tire/chassis cones + `> 0.35 m` local `48³` `C0` SDF soft penumbra cones on Top-`4` cut lights | **`StokesContour`** (`≤ 4.8 m` exact zero-noise emissive polygons) + **`SurfelRadiosityLattice`** (2-bounce octahedral `O(1)` lookup) + `BVH4` glossy coat reflection |
| **`3u`** | **`RealTimeGiHighEnd`** | Deterministic `LightPartition` **Split-Cut (`K = 12`)** (`100%` analytical BRDF) | **`0 MB` SDF** · 4-Wide `BVH4` Vogel aperture soft shadow rays (`4` taps on Top-`6` lights) + **`PlaneAwareShadowFilter`** (`0 px` contact preservation) | Full-res **`StokesContour`** + dense **`SurfelRadiosityLattice`** + multi-ray `BVH4` clearcoat & Abbe glass reflections |
| **`4u`** | **`PathTracerGameRealtime`** | Hybrid **Top-`2` Deterministic Analytic / Ureña Solid-Angle** + **1-tap Owen-Scrambled Sobol `(0,2)`-net `LightPartition` NEE + BSDF MIS** (`1–2 spp`) | Exact `BVH4` `TraceSceneAnyHit` visibility (`0 MB` SDF) | Unbiased multi-bounce (`3` bounces) + spatial plane-aware SVGF-free deterministic filter |
| **`5u`** | **`PathTracerSceneFinalRender`** | Full **Owen-Scrambled Sobol `(0,2)`-net `LightPartition` NEE + Ureña Solid-Angle Quad/Tube + BSDF MIS** across all bounces | Exact `BVH4` visibility + spectral Abbe dispersion (`0 MB` SDF) | Converges bit-deterministically (`eb29805c`-class repeatable hash) to ground truth (`4–16` bounces) |

---

## 🏁 5. `AutoSpace` 3-Lobby Rig Specification (`arena/fa8c2344-frontier-1` Target)

| Target Level | Vehicles & Dynamic Automotive Lamps | Architectural / Studio Luminaires | Special Radiometric Requirement Solved by Architecture |
| :--- | :--- | :--- | :--- |
| **`Lobby 1 · Cyclorama`** | `3` vehicles (`Hero Coupe`, `Alpine GT`, `Liquid Tourer`) × `6` lamps (`2` ECE Low-Beam, `2` DRL Tubes, `2` Tail/Brake Bars) = **`18` car lights** | `1` Overhead Silk Softbox (`7.2×4.0 m` LTC), `1` Barn-Door Fresnel (`5400 K` IES + 4-leaf clip), `1` Parabolic Umbrella (`1.55 m` LTC), `1` Rim LED Panel (`6300 K` LTC), `1` Octa Strobe (`5600 K` IES) = **`5` studio rigs** | Removes `AreaLights[4]` cap; preserves silky continuous `7.2 m` softbox highlight across curved hood clearcoat + crisp Barn-Door Fresnel falloff. |
| **`Lobby 2 · Forest Stage`** | `4` vehicles (`Hero` angled on granite rock incline + `3` staging bays) × `6` lamps = **`24` car lights** | Same `5` studio rigs (`3450 K` golden woodland Fresnel key) + **Curved Sunlit Forest LED Volume Wall** (`EmissiveSurface` Class `6`) | Curved LED backdrop illuminates mossy rock & reflects in clearcoat via **Stokes Contour + LTC representative panels** with zero 1-spp firefly grain; volumetric fan dust motes sample `LightPartition` analytic cone inscattering. |
| **`Lobby 3 · Racing Garage`** | `4` vehicles (`Hero Signal-Orange GT`, `Track Coupe` on live **Hydraulic 4-Post Lift**, `2` paddock cars) × `6` lamps = **`24` car lights** | **Exterior Sun + 3D Sky Dome** (`90 klx`), **Open Pit Archway Sky Portal** (`Class 7`), **Garage Ceiling Linear LED Banks** (`Class 5` Tube `5800 K`), **Mechanic Bay Overhead IES Worklamp** (`3400 K`), **Telemetry Screen Emissives** (`Class 6`) | **Portal Rect Aperture + Cone Partition** prevents `90 klx` outdoor stadium sunlight from starving `3400 K` indoor pit-bay lamps; zero temporal ghosting as hydraulic lift raises `Track Coupe` live. |

---

## 📝 6. Reuse-First C++20 & `.slang` Implementation Order (`CLAUDE.md` §2.9)

1. **Extend `Engine/GeometricRaster/DirectLighting/PhotometricLightTree.{h,cpp}` in place** → generalize leaf ingestion from point-only `PhotometricPointRadiator` to all 7 classes (`GpuRadiometricLuminaire64`: `Point`, `Spot`, `PhotometricIes` + barn-door/ECE params, `PolygonalAreaLtc`, `LinearTubeSegment`, `EmissiveSurface`, `EnvironmentPortal`) and emit `LightPartitionEntry32` with specular-bounding cone metadata.
2. **Add `Engine/Shaders/DirectLighting/RadiometricLuminaireEvaluation.slang`** (included by `WorldViewportRadiometricCompute.slang` and `SpecularGlassRaytrace.slang`) → implements `EvaluateDualLobeClippedLtc`, `EvaluateLinearTubeSegment` (Picott + Karis), `EvaluateBarnDoorPhotometricGain` (`128x32` LM-63 + 4-leaf frustum + ECE `-1.0°` kink), `EvaluateAdaptivePartitionCut` (`K = 8..12` with $G_{\text{coat}}$ specular preservation), and `SamplePartitionLuminairesSobol` (for `Engines 4..5`).
3. **Wire `WorldViewportRadiometricCompute.slang` & `VulkanWorldRenderSequence`** → replace `AreaLights[4]` UBO slice and the 5 brute-force `for (PL < PhotoLampCount)` loops (`lines 2512, 2674, 3040, 3798, 5070`) with `EvaluateAdaptivePartitionCut` (`Engines 1..3`, secondary reflections, and surfel radiosity) and `SamplePartitionLuminairesSobol` (`Engines 4..5`).
4. **Connect Existing 8 C++20 Light Inspectors (`Engine/Editor/Inspectors/*Light*Inspector.cpp`)** → bind `TubeLightInspector`, `LedLightInspector`, `IesLightInspector` (barn-door / ECE cutoff), and `AreaLightInspector` directly into `RadiometricLuminaireRegister` with zero `vkQueueWaitIdle` stalls.
5. **Verify Before Constructing `AutoSpace` Lobbies 1–3** → run deterministic SwiftShader offscreen Exhibit validation (`RadiometricPartitionValidation`) confirming `< 1.0` 8-bit MAE between `Split12` and brute-force at `256` mixed luminaires across `Engines 1, 2, 3, 5`, then construct `AutoSpace` Lobbies 1–3 natively (excluding HTML UI per user instruction).

---

## 🔍 7. Interactive HTML Reference Lab

- **Path**: [`Experimental/RadiometricLightingLab/index.html`](../../Experimental/RadiometricLightingLab/index.html)
- **Live Capabilities**: Real-time WebGL2 3D automotive showroom demonstrating all **7 luminaire classes** (`Dual-Lobe Clipped LTC Softbox`, `128x32 LM-63 IES + 4-Leaf Barn-Door Fresnel`, `ECE -1.0° Asymmetric Headlamps + DRL Tubes + Taillamp Bar`, `Linear Ceiling LEDs`, `Stokes Contour Emissive LED Volume`, and `Pit-Archway Sky Portal`), **4 Render Modes** (`MatCap`, `OpenPBR Direct`, `Hybrid SDF-Vis + Analytic BRDF`, `Progressive Sobol Raytrace`), **4 Solver Comparisons** (`Unified Analytic + Partition Cut` vs. `Naive Clustered Froxels` vs. `1-spp ReSTIR DI` vs. `Pure SDF Voxel Lighting`), scaling up to **`96` simultaneous dynamic lights** with live C0°/C90° polar candela plot and split-screen wipe comparator.
