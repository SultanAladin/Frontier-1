# Frontier Particle Rendering, Vehicle Derez/MoGraph, Procedural Snow/Sprite Texturing, Animated Materials & Content Browser Plan

> **Document Type**: Architecture & Implementation Plan (No Code Changes in This Revision)
> **Branch**: `arena/6eeea758-frontier` · **Date**: 2026-11-15
> **Live Reference Preview**: `Experimental/ParticleEditor Final/` running on `0.0.0.0:8096`
> **Naming Compliance**: All proposed C++ and `.slang` identifiers comply with `Documentation/BannedWordsVocabulary.md` (zero banned nouns/conjunctions; concrete hardware/domain suffixes `…Sequence`, `…Rasterization`, `…Register`, `…Repository`, `…RecordEntry`).

---

## 1. Executive Summary & Architectural Vision

With the **Cloth & Rigging** (`001–005`) and **Silk & Curl** (`024–033`) batches achieving validated 1:1 visual parity against `Experimental/ParticleEditor Final/`, the next leap moves Frontier from isolated preview rasterization into a **first-class, production-grade GPU Particle & VFX Architecture** seamlessly woven across five core Frontier domains:

1. **Unified, Depth-Occluded, GI-Coupled Particle Renderer (`Engine/ParticleSpace/` + `Engine/GeometricRaster/`)** — True 3D instanced particle geometry, strict scene depth occlusion (fixing the historical always-in-front bug), two-way Global Illumination participation (both receiving and emitting GI), and high-throughput GPU sprite/billboard rendering for weather & atmospheric presets.
2. **Content Browser Real Rendered Particle Thumbnails (`Engine/Components/ContentBrowser/`)** — Replace the placeholder 12-spoke starburst in `ThumbnailSheetRepository` with real pre-baked, ACES-tonemapped `128×128` RGBA32 screenshots (plus `256×256` hover previews) for every ported particle preset and user-saved `.ParticleSpace` document.
3. **Vehicle-Driven Derez, Reverse-Rez, Dissolve & MoGraph/Effectors (`ControlVehicle`)** — Port and evolve the exact **Vehicle HMI Derez** effect from `Frontier-OLD@arena/255c3b21-frontier` (`EffectRecords.wgsl` + `EffectRaster.wgsl` + `SurfaceRaster.wgsl`) onto Frontier's `ControlVehicle` asset alongside **Dissolve-to-Nothing**, **Dissolve-to-Butterfly**, and **MoGraph / Effectors**.
4. **3-Stage Procedural Snowflake & Particle Sprite Generator (`Experimental/` → `Engine/Workspaces/Texturing/` → `Engine/Workspaces/Particle/`)** — Build a standalone HTML hexagonal snowflake + general sprite generator first, integrate it as a modular generator plugin inside Frontier's **Pattern Studio (`PatternStamp`)**, and bind its multi-channel procedural sprite atlases & dissolve masks directly into **Particle Workspace**.
5. **Procedural & Animated Material Extensions (`Engine/MaterialSpace/`)** — Extend Frontier's 10-lobe OpenPBR `SurfaceMaterial` & `CompactMaterialRegister` with a compact GPU procedural modulator stack, clock/age/temperature/release-wave animation inputs, and optical dissolve clipping so 3D particles, billboards, strands, and dissolving vehicles share one unified physical shading model.

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────┐
│                             FRONTIER UNIFIED PARTICLE ARCHITECTURE                              │
├──────────────────────────────┬──────────────────────────────────┬───────────────────────────────┤
│ 1. Pattern Studio (Texturing)│ 2. MaterialSpace (OpenPBR 10-Lobe)│ 3. ParticleSpace (Sim + Origin)│
│ • Generator Plugin Registry  │ • Static 10-Lobe OpenPBR Base    │ • ControlVehicle Surface Anchor│
│ • Hexagonal Snow Crystal Gen │ • ProceduralModulatorStack (GPU) │ • 60Hz VehiclePoseHistory     │
│ • General Sprite & Dissolve  │ • AnimatedClock & Particle Inputs│ • Derez 16³ Release Wave Clock│
│ • Multi-Channel Atlas Baker  │ • SurfaceDissolveClipSpec        │ • MoGraph Cloner + Effectors  │
│   (Alpha/Height/Normal/SDF)  │ • Planckian Blackbody + ThinFilm │ • Vulkan Compute Simulation   │
└──────────────┬───────────────┴────────────────┬─────────────────┴──────────────┬────────────────┘
               │ .TextureSpace Sprite Atlas     │ DynamicMaterialExtensionSlot   │ GPU Instance SSBOs
               ▼                                ▼                                ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────────┐
│                  4. TIERED WORLD RENDER SEQUENCE (GTX Tier 1 & RTX Tier 2)                      │
│  [A] Opaque 3D Particles + Dissolving ControlVehicle ──► Shared 64-bit Visibility & Depth Buffer│
│      (Exact Depth Occlusion + Shadow Casters + Reflections + Full 10-Lobe OpenPBR & GI!)        │
│  [B] ParticleRadianceInjectionSequence ──► VEPL Emissive Clusters + OctahedralIrradianceLattice │
│      (Derez Glow, Fire, Sparks & Lightning physically illuminate floors, walls & car paint!)    │
│  [C] Transparent Sprite / Strand / Ribbon Rasterization ──► Depth-Tested (READ-ONLY) + SoftFade │
│      • Samples IrradianceLatticeSequence (Chebyshev 3D GI) + SolarShadowSequence                │
│      • Depth-sorted / WBOIT alpha sprites (Snow, Smoke, Rain) + Linear-HDR additive (Fire, Arcs)│
└──────────────────────────────────────────────┬──────────────────────────────────────────────────┘
                                               ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────────┐
│             5. Content Browser (ThumbnailSheetRepository + ParticleThumbnailRasterizer)         │
│  • Deterministic hero-timestamp renders -> 128×128 RGBA32 ThumbnailSheet.ImageSpace tiles       │
│  • Indexed via existing 64-byte ContentIndexEntry64 under ContentCategory::Particles (11)       │
└─────────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Pillar 1 — Unified, Hardware-Accelerated Particle Renderer

### 2.1 Diagnosing & Fixing the Historical Limitations

In previous iterations, particles were drawn in an isolated post-overlay pass that bypassed scene depth and lighting, causing three structural defects:
- Particles rendered **on top of occluding geometry** (never disappearing behind walls, pillars, or vehicle bodywork).
- Particles were shaded with flat unlit colours and **neither received nor emitted Global Illumination**.
- 3D meshes and high-count weather billboards lacked a shared, low-overhead GPU-driven instancing architecture.

Frontier already owns the exact building blocks needed to eliminate all three cleanly:
- **Primary Visibility & Hardware Depth**: `Engine/GeometricRaster/Visibility/VisibilityClusterRasterization.{h,cpp}` and `VisibilityMaterialResolve.{h,cpp}` (64-bit Visibility Buffer + 2-layer glass peel + analytic ray-differential UV Jacobians + motion vectors).
- **Tiered Global Illumination**: `Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.{h,cpp}`, `IrradianceLatticeSequence.{h,cpp}` (Tier 2 RTX `8×8` octahedral `E(n)/π` + `16×16` Chebyshev depth moments; Tier 1 GTX 6-cone SDF GI), `SpecularReflectionSequence.{h,cpp}`, and `VulkanWorldRenderSequence.{h,cpp}`.
- **Particle Vulkan Compute Simulation**: `Engine/ParticleSpace/Simulation/ParticleVulkanExecutor.{h,cpp}` + 21 compiled `.spv` shaders in `build/CompiledShaders/`.

### 2.2 The Four Required Rendering Lanes

#### Lane 1 — True 3D Geometry Instancing (`ParticleGeometryRasterization`)
- **Geometry Sources**: Reuses Frontier's `ClusterPyramid` / `.facet` triangle cluster buffers for arbitrary 3D particle geometry — chamfered derez cubes, 8-way shatter shards, articulated 2-wing hinged butterflies, coins, hexagonal prisms, jagged ice hailstones, crumpled leaves, and arbitrary user-selected `.GeometrySpace` solids (for `effector-object-placer`).
- **Compact GPU Instance Payload (`ParticleGeometryInstanceEntry`, 64 bytes — 1 cache line)**:
  - `WorldOriginMeters` (`vec3f`) + `UniformScaleMeters` (`float`)
  - `OrientationQuat` (`vec4f`, normalized `xyzw`)
  - `PreviousOriginMeters` (`vec3f`) + `ArticulationPhase` (`float` — wing flap angle, shatter progress, or squash factor)
  - `MaterialSlot` (`uint16_t`), `GeometryAssetOrdinal` (`uint16_t`), `EmissiveGlowScale` (`fp16`), `NormalizedAge` (`fp16`), `CustomSeedHash` (`uint32_t`), `EffectorTintRgba8` (`uint32_t`)
- **Indirect GPU Dispatch**: Compute culling (`ParticleFrustumCull.slang`) writes compact `VkDrawIndexedIndirectCommand` entries per unique geometry asset, so 50,000 derez cubes, butterflies, or MoGraph clones execute in **1 indirect draw per unique shape**.
- **Motion Vectors Included**: Vertex shader evaluates both current and previous transforms (`CurrentClip` vs. `PreviousClip`), feeding TAA and motion blur cleanly with zero ghosting.

#### Lane 2 — True Depth Occlusion (Strict Scene Depth Integration)
- **Opaque & Alpha-Cutout 3D Particle Geometry** (Derez cubes, shatter shards, MoGraph cubes/hexagons/dots, coins, hail, debris, butterflies):
  - Rendered **inside the primary Visibility & Depth sequence** (`DEPTH_TEST = LESS_EQUAL` / Reverse-Z `GREATER_EQUAL`, `DEPTH_WRITE = ON`), writing valid depth, surface normal, material slot, and motion vector into the exact same depth and visibility targets as `ControlVehicle` and the world environment.
  - **Consequence**: Particles are occluded pixel-for-pixel behind car fenders, pillars, glass windshields, and terrain; they also **occlude** background geometry, cast real shadow-map / BVH shadows, and appear cleanly inside SSR/RT glossy reflections.
- **Transparent, Soft & Additive Sprites / Strands / Ribbons** (Snow, Rain, Smoke, Fog, Dust, Fire, Sparks, Silk, Bokeh, Lightning Arcs):
  - Rendered in a dedicated **Depth-Aware Transparent Particle Sequence** (`ParticleTransparentRasterization`) immediately after opaque lighting and before ACES tonemapping.
  - Binds the resolved opaque scene **Hardware Depth Buffer** (and `NearestDepth` / Hi-Z pyramid) with **`DEPTH_TEST = ON`, `DEPTH_WRITE = OFF`** — guaranteeing transparent snow, smoke, and sparks **never draw in front of occluding scene walls or vehicle bodywork**.
  - **Soft Depth Intersection Fade**: Volumetric billboards (smoke, mist, steam, dust, fire) compute `SoftAlpha = saturate((SceneLinearDepth - ParticleLinearDepth) / SoftFadeThicknessMeters)` to eliminate harsh intersection seams where smoke or snow drifts touch floors and car hoods.
  - **Correct Sorting & Blending**:
    - *Alpha-blended sprites (Snow, Smoke, Petals, Leaves, Dust)*: GPU radix/bitonic back-to-front depth sort per emitter bucket (with Weighted Blended OIT toggle for ultra-dense multi-layered smoke/sandstorms where sorting pops would distract).
    - *Emissive additive particles (Fire, Sparks, Lightning, Bokeh, Plasma)*: Pure linear HDR additive accumulation directly into `CompositeLinearHdr`, calibrated with our validated linear-vs-sRGB parity curve so intense sparks naturally trigger physical glare / ACES highlight roll-off without clipping to flat white.

#### Lane 3 — Two-Way Global Illumination (GI) Participation
Particles both **receive** GI from the scene and **inject** radiance back into the scene's GI:

1. **Non-Emissive Particles Receiving GI**:
   - **Opaque 3D Particles** go through `VisibilityMaterialResolve` + `TieredWorldRenderSequence` identically to static world geometry — automatically receiving **Tier 1 GTX (6-Cone SDF GI + Hi-Z SSR)** and **Tier 2 RTX (`IrradianceLatticeSequence` Chebyshev `E(n)/π` + `SpecularReflectionSequence` RT reflections)**.
   - **Transparent Sprites & Snow/Smoke Billboards** execute a lightweight **Volumetric GI & Shadow Lookup** in `ParticleSpriteShading.slang`:
     - Samples `IrradianceLatticeBuffer` (already bound in `VulkanWorldRenderSequence`!) at the sprite's world position using **dual-hemisphere normal-weighted lookup** (front normal from the sprite's procedural normal map + back-scatter lobe modulated by Henyey-Greenstein phase anisotropy `g`) — giving snowflakes subtle blue sky top-light + warm ground bounce, and giving smoke/dust plumes rich volumetric multi-scatter shading!
     - Samples `SolarShadowSequence` / directional visibility so snowfall falling underneath an overhang or beside `ControlVehicle` accurately darkens inside shadow volumes and brightens when entering sunlight!
2. **Emissive Particles Injecting Radiance into GI (`ParticleRadianceInjectionSequence`)**:
   - Generalizes `Frontier-OLD`'s proven `FireIllumination.wgsl` + `EffectRaster.wgsl` emission path into a unified GPU compute sequence (`ParticleRadianceCluster.slang`):
     - **Spatial Flux Binning**: Emissive particles (`EmissionLuminanceNits > 0` or `BlackbodyTemperatureK > 800` or `DerezGlow > 0`) atomically accumulate flux `(RadianceRGB * Area, WeightedWorldPos)` into a compact coarse 3D grid aligned with `IrradianceLatticeConfiguration`.
     - **Direct Screen VEPLs**: The top `K` (`16–32`) brightest emissive clusters are emitted as **Virtual Emissive Particle Lights (VEPLs)** — windowed inverse-square point/sphere lights evaluated in the direct lighting shader with SDF soft-shadow occlusion (so a blazing car derez edge, campfire, or lightning bolt illuminates the floor and vehicle body with shadowed contact light!).
     - **Indirect Lattice Injection**: Emissive grid cells inject directional flux into neighboring `OctahedralIrradianceCell` probes during `IrradianceLatticeSequence` propagation (Tier 2 RTX) and into SDF cone bounce summaries (Tier 1 GTX), causing emissive glow to bounce organically off nearby matte walls and appear in glossy reflections!

#### Lane 4 — Efficient GPU Billboard / Sprite & Ribbon Rendering (`ParticleSpriteRasterization`)
Designed directly for the upcoming **Storms & Weather / Wind-Blown / Pyro & Water** preset batches (`snow`, `hail`, `rain`, `smoke`, `steam`, `watermist`, `ash`, `sand`, `sandstorm`, `pollen`, `bubbles`, `foam`, `confetti`, `leaves`, `petals`, `feathers`, `seeds`, `fire`, `embers`, `sparks`, `flares`, `splash`, `fireworks`):
- **Zero-Vertex-Buffer Pull Expansion**: Compute culls alive sprites → writes compacted index list + `VkDrawIndirectCommand` → vertex shader expands tight **6-vertex or octagonal cutout quads** purely from `gl_VertexIndex` + SSBO (`ParticleSlot`), cutting empty-alpha overdraw on round/hexagonal snowflakes by ~35%.
- **Four Orientation Modes**:
  1. `ScreenAlignedBillboard` (snow, smoke, bokeh, dust, embers)
  2. `VelocityStretchedBillboard` (rain streaks, sparks, plasma tracers — elongating cleanly along screen-projected velocity with minimum-thickness clamping)
  3. `WorldTumbleQuad` (3D tumbling leaves, petals, confetti, paper, feathers, large hexagonal snowflakes catching directional specular glints!)
  4. `ConnectedRibbonStrand` (existing silk/curl/lightning/vapor trails upgraded to hardware depth-tested GPU strips)
- **Bindless Multi-Channel Sprite Bank (`ParticleSpriteBank`)**: `VkImageView` `Texture2DArray` where each sprite family packs:
  - **Channel Sheet A (`RGBA8` / `BC7`)**: `RGB` = Albedo/Tint modulation, `A` = Smooth Coverage / Alpha
  - **Channel Sheet B (`RGBA8`)**: `RG` = Octahedral Tangent Normal (for 3D sunlight & GI shading on snow facets & smoke billows), `B` = Height / Thickness (subsurface translucency & refraction), `A` = Micro-Facet Sparkle / Roughness or SDF Distance
  - **Per-Particle Variation**: `VariantIndex = CustomSeedHash % AtlasTileCount` enables 16–64 unique procedural snowflake crystals or leaf shapes in a **single instanced draw call** with zero CPU overhead.
- **Half-Resolution Bilateral Volumetric Lane (Optional Toggle)**: For high-overdraw atmospheric presets (`smoke`, `steam`, `watermist`, `sandstorm`), render large soft sprites (`ScreenRadiusPx > 48`) into a half-resolution linear-HDR buffer with min/max depth-aware bilateral upsampling to sustain 120+ FPS on GTX-class hardware.

---

## 3. Pillar 2 — Content Browser Rendered Particle Thumbnails

### 3.1 Current State vs. Target Architecture

Currently, `Engine/Components/ContentBrowser/ThumbnailSheetRepository.cpp` (`lines 297–306`) fills every `ContentCategory::Particles` (`11`) tile with a synthetic 12-spoke starburst icon tinted by a single colour. Meanwhile, `ContentIndexEntry64` (`ThumbnailSheetRepository.h`) is **already** an exact 64-byte cache-line record pointing directly into pre-baked `128×128` RGBA32 slices inside `ThumbnailSheet.ImageSpace`!

We keep `ContentIndexEntry64` **100% ABI-compatible** (zero struct size change) and replace the synthetic starburst with genuine rendered screenshots of every ported particle system.

### 3.2 Deterministic Thumbnail Bake Pipeline (`ParticleThumbnailRasterizer`)

| Component | Path | Responsibility |
| :--- | :--- | :--- |
| **`ThumbnailCaptureSpec`** | `Engine/ParticleSpace/Presets/ParticlePresetCatalogue.h` | Per-preset hero capture metadata: `HeroSimTimeSeconds` (e.g., `2.4 s` for snow, `1.65 s` mid-wave for Vehicle Derez, `2.0 s` for curl fur), `CameraYawDeg`, `CameraPitchDeg`, `CameraDistanceMeters`, `FocalLengthMm`, `BackdropPreset` (`StudioDarkGrid`, `VehicleStudioBay`, `SkyGradient`), and `OptionalHeroAsset` (`None`, `ControlVehicle`, `StudioSphere`). |
| **`ParticleThumbnailRasterizer`** | `Engine/ParticleSpace/Simulation/ParticleThumbnailRasterizer.{h,cpp}` | Instantiates preset deterministically, warms up with fixed `1/60 s` steps to `HeroSimTimeSeconds`, renders a `256×256` linear HDR frame (supersampled 2×) via the unified Particle Renderer, applies ACES Filmic tonemapping + subtle studio framing vignette, Lanczos-downsamples to `128×128` RGBA32 for `ThumbnailSheet.ImageSpace`, and optionally caches the `256×256` tile for Content Browser hover inspection. |
| **Stock Sheet Packing** | `EngineContent/ParticleArchives/ParticleThumbnailSheet.ImageSpace` + `ContentIndex.Space` | Pre-baked during build/validation for all ported stock presets; `ThumbnailSheetRepository` reads real RGBA32 tile slices directly for `ContentCategory::Particles` (falling back to the starburst only if a tile slice offset is `UINT64_MAX`). |
| **Live User Project Thumbnails** | `Projects/<Project>/Saved/Thumbnails/Particles/<Hash>.rgba` | Whenever a user saves or modifies a custom `.ParticleSpace` document in Particle Workspace, an async background bake (or viewport snapshot capture) updates that single tile in `ThumbnailSheetRepository` and uploads the dirty `128×128` sub-rect to ImGui's texture sheet without rebuilding the entire sheet. |
| **Category Filter & Badges** | `Engine/Components/ContentBrowser/ContentBrowserPanel.{h,cpp}` | Sub-category pill/filter bar inside `Particles` (`All`, `Cloth & Rigging`, `Silk & Curl`, `Transitions & Derez`, `Motion & Grids`, `Storms & Weather`, `Pyro & Water`, `Wind-Blown`, `Shields & Tech`, `Foliage & Flora`) + subtle corner indicator showing whether the preset requires a host 3D asset (e.g., `ControlVehicle`). |

---

## 4. Pillar 3 — Vehicle-Driven Derez, Dissolve-to-Nothing, Dissolve-to-Butterfly & MoGraph Effectors

### 4.1 Canonical Target Asset: `ControlVehicle`

Frontier already possesses the exact **`ControlVehicle`** multi-part asset (currently stored under `Experimental/{DeterministicPathTracer,UVEditor,VehicleProvingGrounds}/` and mirrored with pre-built `.sdfspace` volumes in `Frontier-OLD@arena/255c3b21-frontier/EngineContent/EnvironmentArchives/ControlVehicle/`):
- `ControlVehicleBody.facet` (+ `ControlVehicleBody.sdfspace`) — sculpted chassis, lime/metallic clearcoat paint, tinted cabin glass, headlights/taillights, carbon trim.
- `ControlVehicleRim.facet` (+ `ControlVehicleRim.sdfspace`) — 4 instanced alloy wheels.
- `ControlVehicleTyre.facet` (+ `ControlVehicleTyre.sdfspace`) — 4 instanced vulcanized rubber tyres.

**First Step**: Promote `ControlVehicle` (`.facet` + `.sdfspace` + material slot assignments) into canonical `EngineContent/EnvironmentArchives/ControlVehicle/` and expose a unified **`HostGeometryOrigin`** in `Engine/ParticleSpace/Origins/` so **Derez**, **Reverse-Rez**, **Dissolve-to-Nothing**, **Dissolve-to-Butterfly**, and **MoGraph Effectors** all target `ControlVehicle` out of the box (and seamlessly swap to any user `.GeometrySpace` solid!).

### 4.2 Shared Surface Sampling & Synchronized Clock (`SurfaceAnchorSampler` + `DissolveWaveClock`)

Before running any vehicle VFX, `SurfaceAnchorSampler` computes a deterministic area-weighted blue-noise distribution of surface anchor points across `ControlVehicle`'s triangles (body + 4 wheels, configurable `4,096` to `65,536` anchors):

```cpp
struct alignas(16) SurfaceAnchorRecordEntry // 32 bytes per anchor
{
    Mathematics::Axis3D LocalPositionMeters {}; // [m]   - Anchor position in host-body local space
    uint32_t            MaterialSlotAndPart {}; // [u16] low: OpenPBR MaterialSlot | high: PartOrdinal (Body/FL/FR/RL/RR)
    Mathematics::Axis3D LocalSurfaceNormal  {}; // [-]   - Outward unit normal in host-body local space
    float               AnchorAreaMetersSq  {}; // [m^2] - Voronoi area weight for uniform scaling/density
};
```

### 4.3 Exact Vehicle HMI Derez & Reverse-Rez (Faithful Port of `Frontier-OLD` `EffectRecords.wgsl` + `EffectRaster.wgsl` + `SurfaceRaster.wgsl`)

We preserve every single visual & mathematical detail that made the `Frontier-OLD` Vehicle HMI Derez demo look extraordinary, upgraded to Frontier's 10-lobe OpenPBR + Tiered GI renderer:

1. **Single Authoritative `16³` Lattice Release Clock (`EvaluateDerezCellRelease`)**:
   - Both the **Vehicle Surface Shader** (`VisibilityClusterRasterization` / `OpenPbrSurfaceScattering.slang`) and the **Instanced Derez Cube Shader** (`VehicleDerezCubeRasterization.slang`) include the exact same `.slang` function (`DerezWaveClock.slang`).
   - Every local-space point `P` on `ControlVehicle` maps to cell `(cx, cy, cz) ∈ [0..15]³` over `[BodyMinimum, BodyMaximum]` (configurable resolution `8³..32³`).
   - Cell release timestamp `T_release(cell)` combines a directional sweep (front-to-back, bottom-to-top, radial impact, or **Pattern Studio dissolve mask**!) plus deterministic per-cell hash jitter (`WaveSpread` + `HoldBase`).
   - **Zero Drift Guarantee**: Because both shaders evaluate the exact same function, the exact millisecond a `16³` cell of `ControlVehicle`'s hood or tyre vanishes, its cubes launch from the exact same coordinates.
2. **Incandescent Leading-Edge Fracture Glow (Vehicle Surface Shader)**:
   - When `Clock ∈ [T_release - EdgeWindow, T_release]`: pixel stays visible and ramps emission sharply from **electric cyan** → **white-hot core** (or material-tinted glow when `InheritFaceMaterials = true`), injecting real HDR luminance into GI and specular reflections!
   - When `Clock >= T_release`: pixel is cleanly discarded (`discard`), revealing hollow backlit inner silhouettes and escaping cubes.
3. **True Material Inheritance & Momentum Inheritance (`VehiclePoseHistory`)**:
   - **Material Inheritance (`InheritFaceMaterials = true`)**: Each cube wears the exact `MaterialSlot` of its originating vehicle face — lime clearcoated paint cubes detach from the body panels, refractive glass cubes detach from the canopy, brushed alloy cubes from the rims, matte rubber cubes from the tyres — PLUS additive face-tinted glow (`Emission = BaseAlbedo * GlowIntensity`)! Toggleable to **Cyber/Neon Swatch Mode** (`Cyan Glow / White-Hot Core / Glitch Magenta`).
   - **60 Hz Vehicle Motion Ring Buffer (`VehiclePoseHistoryEntry`)**: Records `(Position, Quaternion, LinearVelocity, AngularVelocity, Clock)` at 60 Hz while the vehicle drives/drifts/rotates. Each cube samples the interpolated vehicle pose at its exact `T_release` moment, inheriting `V_car + ω_car × r_local` plus outward surface-normal burst and hashed scatter — so a moving `ControlVehicle` sheds trailing streams of cubes obeying true momentum!
4. **Closed-Form Ballistics + 8-Child Floor Shatter**:
   - Analytic linear-drag gravity trajectory (`v'(τ) = g - k·v`, exact closed-form `x(τ)` and `v(τ)` with zero simulation drift or readback!).
   - Fast 8-step bracket + 10-iteration bisection (`ResolveFloorImpact`) computes exact ground-contact time `T_impact`.
   - Upon hitting the floor (`z = FloorZ`), each primary cube shatters cleanly into **8 tumbling micro-shards** (`(1 primary + 8 shards) × 36 cube vertices` per GPU instance) that bounce outward, spin rapidly, glow along impact edges, cast shadows/GI, and shrink smoothly to zero over `ShardLifeSeconds` (`1.2 s`).
5. **Reverse-Rez (Materialization Assembly)**:
   - Identical shared clock running backward: cubes begin distributed on a `2.6 m` outer spherical shell around `ControlVehicle`, arc inward with cubic ease-out onto their exact `SurfaceAnchorRecordEntry` coordinates, lock in with a brief incandescent seam flash, and reveal each `16³` cell of the car as its cubes arrive!
6. **Glitch-Dissolve Mode (`Style = GlitchSlices`)**:
   - Uses horizontal row-coherent slice jitter + brief RGB chromatic edge displacement along releasing `ControlVehicle` bands before voxelizing into glitch-magenta/cyan micro-strips.

### 4.4 Dissolve-to-Nothing (`dissolve-nothing`) on `ControlVehicle`

- Shares the exact `DerezWaveClock.slang` surface-discard infrastructure on `ControlVehicle`, switching wave profile to organic **3D Curl/Simplex Melt** or a **Pattern Studio Dissolve Mask** (e.g., `diamond-dissolve`, Voronoi ice-fracture, brushed burn line).
- Along the receding glowing perimeter (`T_release - BurnBand .. T_release`):
  - The vehicle surface darkens into charred matte roughness → molten amber/white Planckian blackbody edge (`2400 K → 1100 K`) → clean discard (`after: "none"`).
  - Spawns fine **depth-tested, GI-lit ash flakes, upward curling ember sparks, and soft drifting smoke wisps** directly from anchors crossing the melt front, rising under thermal buoyancy + curl-noise wind and tapering gracefully to zero opacity — leaving nothing behind.

### 4.5 Dissolve-to-Butterfly (`dissolve-butterflies`) on `ControlVehicle`

- Uses the same synchronized melt/sweep wave across `ControlVehicle`, setting `ReleaseAction = SpawnArticulatedButterfly`:
  - At `T_release`, each cell spawns a **3D articulated instanced butterfly** (`ButterflyGeometryAsset`: thorax + left wing + right wing hinged in vertex shader by `sin(Clock * FlapFreq + Phase)`).
  - **Material Continuity**: Butterflies start with a brief glowing transition blend from the vehicle's actual facet colour (lime/glass/alloy) into iridescent **OpenPBR L4 Thin-Film Interference** wings (Morpho electric blue, Monarch amber, or pearlescent white) textured with crisp **Pattern Studio butterfly wing venation & alpha cutouts**!
  - **Swarm Takeover**: After a `0.35 s` normal-burst detachment, each butterfly transitions smoothly into `BoidsFlockSolver` (`Separation`, `Alignment`, `Cohesion`, gentle upward helical draft, and soft avoidance around the remaining undissolved `ControlVehicle` SDF volume!), fluttering away across the studio while casting subtle shadows and catching sunlit GI.

### 4.6 MoGraph & Effectors (`effector-*`) on `ControlVehicle`

Covers all 5 MoGraph presets (`effector-grid-cubes`, `effector-sphere-dots`, `effector-hand-halftone`, `effector-torus-hex`, `effector-object-placer`) plus direct `ControlVehicle` driving:
- **Cloner Distribution Modes**:
  1. `UniformGrid3D` (`X × Y × Z` lattice)
  2. `RadialSphere` / `FibonacciShell`
  3. `TorusHexLattice`
  4. `HostSurfaceAnchors` (clones placed across `ControlVehicle`'s exact surface normals & material slots — e.g., halftone dots, hexagonal armor plates, or placed 3D props!)
  5. `HostSignedDistanceVoxel` (voxelizes `ControlVehicleBody.sdfspace` into crisp architectural cubes/hexagons representing the car silhouette!)
- **Composable GPU Effector Stack (`EffectorStackCompute.slang`)**:
  - Evaluates up to `8` stacked effectors in deterministic order per clone:
    - **`PlainEffector`**: Uniform translation, rotation, scale, and colour tint driven by falloff weight `W ∈ [0, 1]`.
    - **`RandomEffector`**: Deterministic hashed positional/rotational/scale jitter & colour variation.
    - **`StepEffector`**: Progressive index-ordered cascade along clones.
    - **`ShaderNoiseEffector`**: Animated 3D Simplex/Curl field displacement & ripple waves.
    - **`FormulaWaveEffector`**: Analytic radial/planar harmonic sine/damped wave ripples (`sin(k·d - ω·t)`).
    - **`DelaySpringEffector`**: Per-clone spring-damper state (`x'' = k(target - x) - c·x'`) producing juicy cinema-grade overshoot and settle!
    - **`PatternStudioFieldEffector`**: Samples a live Pattern Studio procedural texture (halftone dots, diamond dissolve, concentric rings, stripes) projected planar/triplanar/UV onto `ControlVehicle` to modulate clone scale, extrusion, and emission!
  - **Falloff Field Primitives**: `Sphere`, `Box`, `Cylinder`, `LinearSweepPlane`, `TorusRing`, and `HostSdfShell` — combinable with `Add / Multiply / Max / Subtract` blend ops and live gizmo manipulation in the viewport!

---

## 5. Pillar 4 — 3-Stage Procedural Snowflake & Particle Sprite Texture Generator

Following your exact requested order: **Stage 1: Standalone HTML Prototype → Stage 2: Pattern Studio Plugin → Stage 3: Particle Workspace Integration**.

### 5.1 Stage 1 — Standalone HTML Procedural Generator (`Experimental/ParticleTextureGenerator/index.html`)

A self-contained, zero-dependency HTML5 + WebGL2/Canvas2D interactive studio (runnable immediately on a live preview port just like `ParticleEditor Final` and `GeometryCompiler`) for visually crafting and validating procedural snowflake crystals and general particle sprites before writing a single line of C++:

#### A. Dedicated Hexagonal Snowflake Crystal Generator (Physically Motivated `D6` Morphology)
Inspired by the **Nakaya / Libbrecht snow crystal morphology diagram**, combining analytical crystal branch grammars with optional Reiter cellular diffusion softening:
- **8 Authentic Snow Morphologies**:
  1. `StellarDendrite` (classic 6-armed branching fernlike star)
  2. `FernlikeDendrite` (dense multi-order side-branching at exact `60°` crystallographic angles)
  3. `SectoredPlate` (broad hexagonal prism with sculpted interior ice ribs & star core)
  4. `SimpleHexPlate` (clean translucent faceted hexagonal crystal with subtle bevel rims)
  5. `StellarPlate` (broad diamond-tipped arms bridging plates and dendrites)
  6. `CappedColumn` / `BulletRosette` (elongated crystalline shards for crisp wintry glints)
  7. `RimedGraupel` (soft frost-encrusted snow clump / frozen aggregate for heavy blizzards & hail)
  8. `TwinnedTwelveArm` (rare 12-branched `D12` rotated double crystal)
- **Parametric Controls**:
  - `SymmetryOrder` (`6` canonical, `12` twinned, plus `NaturalAsymmetry` `[0..0.25]` for realistic imperfect wind-grown flakes)
  - `CorePlateRadius`, `PrimaryArmLength`, `PrimaryArmTaper`, `SecondaryBranchCount`, `SecondaryBranchAngle` (default `60°`), `TertiaryTwigDensity`, `TipSharpness`
  - `FacetRidgeDepth`, `InternalIceRibs`, `RimingFrostAmount` (micro-droplet edge frosting), `EdgeSoftnessPx`, `Seed`
- **Multi-Channel Real-Time Preview & Export** (switchable in the viewport + 4-up quad view):
  1. **Alpha / Silhouette Mask** (crisp translucent ice plate + solid crystal spine)
  2. **Height / Ice Thickness** (beveled crystal ridge elevation)
  3. **Tangent-Space Normal Map** (derived from beveled 3D crystal facets — enables glittering sunlit ice sparkles in Frontier's renderer!)
  4. **Micro-Facet Sparkle & SDF Distance** (infinite-zoom sharp vector-grade edge + specular glint mask)
  5. **Lit 3D Tumbling Preview** (mini WebGL turntable preview showing the generated snowflake tumbling under directional sunlight + studio GI so you verify glints immediately!)

#### B. General Procedural Particle Sprite Library (Same Unified Generator)
- **Atmospheric & Optical**: Soft Gaussian Glow, Optical Aperture Bokeh (5–9 polygon blades + edge ring + subtle longitudinal chromatic fringe), Volumetric Billow Smoke/Cloud Puff (multi-octave Worley + fBm with analytical 3D puff normal map!), Heat Shimmer Distortion Normal.
- **Precipitation & Water**: Tapered Rain Streak, Refraction Water Droplet, Organic Splash Blob, Iridescent Soap Bubble Ring, Foam Cluster.
- **Pyro & Tech**: Directional Spark Streak, Starburst Lens Flare, Electric Plasma Core, Hexagonal Energy Shield Cell, Glitch Pixel Strip.
- **Organic & Botanical**: Maple / Birch / Oak Leaf Silhouettes + Vein Normals, Cherry Blossom Petal, Dandelion Pappus Seed, Feather Barb, **Articulated Butterfly Wing Mask + Venation Pattern** (directly feeding `dissolve-butterflies`!).
- **Dissolve & MoGraph Patterns**: `DiamondDissolve`, Voronoi Fracture, Curl Melt Threshold, Radial Halftone Grid, Concentric Ripple (directly feeding `ControlVehicle` Derez/Dissolve and MoGraph `PatternStudioFieldEffector`!).

#### C. Multi-Variant Atlas Packing & Recipe Export
- Generates `1×1`, `2×2` (4 variants), `4×4` (16 unique crystals/puffs), or `8×8` (64 unique variants) packed sprite atlases using deterministic seed stepping (`BaseSeed + TileIdx * 9781`).
- Exports both packed multi-channel `.png` sheets AND a portable JSON/TOML **Generator Recipe** (`*.ParticleSpriteRecipe.json`) whose exact math ports 1:1 into C++ / `.slang` compute shaders!

### 5.2 Stage 2 — Pattern Studio Plugin Architecture (`Engine/Workspaces/Texturing/`)

Inspecting `Engine/Workspaces/Texturing/PatternStampStackSession.h` reveals Pattern Studio **already** supports:
- `PatternStampWorkspaceMode::Stamp` (transparent alpha cutout decal mode!)
- Multi-channel `PatternStampViewChannel` (`Albedo`, `Roughness`, `Metalness`, `Height`, `Normal`, `Emissive`, `StackedPbrTile`, `Seamless2x2`)
- Non-destructive vector/layer stacks + GPU baking!

We introduce a clean, extensible **Procedural Generator Plugin Architecture** inside Pattern Studio:
- **`PatternGeneratorPluginRegistry`** (`Engine/Workspaces/Texturing/Plugins/PatternGeneratorPluginRegistry.{h,cpp}`):
  - Defines a lightweight, data-driven `PatternGeneratorDescriptor` interface (`GeneratorId`, `DisplayTitle`, `Category`, `ParameterDefinitions[]`, `SynthesizeChannelsCpuOrGpu(...)`).
  - Registers first-class built-in plugins:
    1. **`SnowflakeCrystalGenerator`** (exact 1:1 C++ & `.slang` port of the Stage 1 HTML hexagonal crystal math)
    2. **`ParticleSpriteGenerator`** (bokeh, smoke billows, rain droplets, sparks, bubbles, botanical leaves, butterfly wings)
    3. **`DissolveMaskGenerator`** (diamond dissolve, Voronoi ice-crack, curl melt, halftone fields)
- **Layer Stack Integration**: Add `PatternStampLayerCategory::ProceduralGenerator` to `PatternStampStackSession` so a user can:
  - Drop a **Snowflake Crystal** layer onto a `Stamp` canvas, stack frost riming, radial gradients, vector engraving, or blend two morphologies non-destructively!
  - Toggle **`AtlasGridMode` (`1×1`, `2×2`, `4×4`, `8×8`)** in Pattern Studio to preview and bake an entire randomized sheet of 16 or 64 unique snowflakes at once!
  - Bake directly to Frontier's `.TextureSpace` / `.ParticleSpriteAtlas` format and publish straight into Content Browser (`ContentCategory::Textures`).

### 5.3 Stage 3 — Direct Pattern Studio ↔ Particle Workspace Integration

- **`ParticleSpriteSourceSpec`** on every `ParticleOrigin` / `ParticleDocument`:
  - `SourceMode::BuiltinProceduralRecipe` — evaluates the exact deterministic generator recipe on GPU on load (zero heavy binary textures checked into Git for stock presets like `snow`, `hail`, `smoke`, `leaves`, `dissolve-butterflies`!).
  - `SourceMode::PatternStudioDocument` — binds a live `.PatternStamp` or baked `.TextureSpace` asset from Content Browser (supports both **Sprite Atlases** and **Dissolve / Effector Masks**!).
- **One-Click Round-Trip UX in `ParticleWorkspace`**:
  - Inside `ParticleInspectorPane`, every sprite-based or dissolve-based emitter displays its live sprite atlas thumbnail + **"Edit in Pattern Studio"** button.
  - Clicking it switches focus to Pattern Studio with that generator/layer stack loaded; editing a snowflake's branch count or dissolve mask sharpness automatically re-bakes the GPU `ParticleSpriteBank` array slice and updates the live Particle viewport immediately!

---

## 6. Pillar 5 — Extended Material System (`Engine/MaterialSpace/`) for Procedural & Animated Materials

Frontier's `Engine/MaterialSpace/SurfaceMaterial.h` already delivers an extraordinary **10-lobe physical OpenPBR architecture** (`Diffuse`, `Specular`, `Clearcoat`, `ThinFilm` Airy interference, `GlintFlakes` 4D multiscale microflakes, `Transmission` + Abbe dispersion, `Subsurface`, `Sheen`, `Fiber`, `PorosityLight` + Planckian blackbody emission).

To power **all** particles, dissolving vehicles, and animated VFX without duplicating shader code, we extend `MaterialSpace` cleanly along three orthogonal axes while keeping static opaque materials on the exact same zero-overhead fast path:

### 6.1 Extended Data Model (Zero Regression for Static Materials)

```cpp
// Appended as an optional side-channel referenced by CompactMaterialEntry::DynamicExtensionSlot (0xFFFF = static!)
enum class ProceduralFieldKind : uint8_t
{
    SimplexNoise3D         = 0, // [-] - Smooth 3D gradient noise (smoke erosion, melt waves, plasma)
    CurlNoise3D            = 1, // [-] - Divergence-free swirling field distortion
    VoronoiCellular3D      = 2, // [-] - Cellular fracture, cracked ice, scales, diamond facets
    HexLattice2D           = 3, // [-] - Hexagonal energy shields & honeycomb cells
    GradientRamp1D         = 4, // [-] - Directional / radial / age / temperature ramp via ColourRampSampler
    FresnelViewAngle       = 5, // [-] - View-angle grazing boost (shields, frost rims, bubbles)
    PatternStudioMask2D    = 6, // [-] - Samples live bound Pattern Studio procedural texture
    DerezCellWaveClock     = 7, // [-] - Shared 16^3 lattice release clock for Vehicle HMI Derez
};

enum class MaterialDrivenParameter : uint8_t
{
    BaseColorRgb           = 0,
    RoughnessScalar        = 1,
    MetalnessScalar        = 2,
    OpacityAlpha           = 3,
    AlphaClipThreshold     = 4, // Dissolve & derez cutout!
    EmissionLuminanceNits  = 5,
    BlackbodyKelvin        = 6, // Physical thermal fire/sparks/molten derez edges!
    ThinFilmThicknessNm    = 7, // Animated shimmering soap bubbles, butterfly wings & iridescent shells!
    GlintFlakeCoverage     = 8, // Twinkling sunlit snow crystals!
    NormalPerturbation     = 9,
};
```

- **Fixed-Slot GPU `ProceduralModulatorStack`** (up to `8` compact `ProceduralModulatorOp` entries per dynamic material — zero pointer chasing, zero shader divergence, full SPIR-V / Slang compatibility!):
  - Each op selects an **Input Domain** (`WorldPosition`, `LocalPosition`, `SurfaceUV`, `ParticleNormalizedAge`, `ParticleSpeed`, `ParticleTemperatureK`, `ParticleSeedHash`, `GlobalClockSeconds`, `DerezReleaseDelta`, `EffectorWeight`), evaluates a `ProceduralFieldKind` + scale/scroll velocity/octaves/ramp, and blends (`Replace`, `Multiply`, `Add`, `Lerp`, `SmoothstepThreshold`) into a `MaterialDrivenParameter`.
- **`AnimatedMaterialClockInputs`** (pushed once per frame in uniforms + per-particle attributes in SSBO):
  - Global: `SimulationTimeSeconds`, `DeltaTimeSeconds`, `LoopPeriodSeconds`.
  - Per-Particle / Per-Instance: `NormalizedAge [0..1]`, `SpeedMetersPerSec`, `TemperatureKelvin`, `CustomSeedHash`, `DerezReleaseDeltaSeconds`, `EffectorWeight [0..1]`, `AtlasTileIndex`.
- **Direct OpenPBR Synergy (Examples Unlocked Immediately)**:
  - **Snow Crystals**: `L1 Diffuse` (high albedo ice) + `L5 GlintMicroflakes` (sparkling sunlight glints modulated by Snowflake Normal/Sparkle map!) + `L7 Subsurface` (cool blue translucent ice edge) + GI lattice sampling!
  - **Hail & Ice Shards**: `L6 DielectricTransmission` (`IOR = 1.31`, Abbe dispersion!) + subtle frost roughness modulated by 3D Voronoi procedural noise!
  - **Fire, Embers & Molten Derez Edges**: `L10 PorosityAndEmission::BlackbodyTemperatureKelvin` driven smoothly by `ParticleNormalizedAge` & `DerezReleaseDelta` (`3400 K` white-hot → `1800 K` orange → `850 K` ember → `0 K` cooling ash) — physically accurate colour temperatures with zero hand-faked RGB gradients!
  - **Soap Bubbles & Dissolve-to-Butterfly Wings**: `L4 ThinFilmWaveParameters::ThinFilmThicknessNanometers` animated over `TimeSeconds + LocalUV` via `SimplexNoise3D` for living, shifting soap-film and Morpho-butterfly iridescence!
  - **Energy Shields (`energy-shield-hex`, `energy-shield-prism`)**: `HexLattice2D` + `FresnelViewAngle` + animated impact ripple wave driving `OpacityAlpha` and `EmissionLuminanceNits`!
  - **Vehicle Derez Material**: Existing `ControlVehicle` OpenPBR paint/glass/rubber materials remain untouched — attaching a `SurfaceDissolveClipSpec` (`DerezCellWaveClock`) adds the exact white-hot/cyan leading-edge emission + synchronized discard to **any** existing OpenPBR material!

### 6.2 Codec & Material Inspector Persistence

- Extend `Engine/Codecs/MaterialCodec.{h,cpp}` with an optional backwards-compatible `[ProceduralModulators]` and `[AnimationInputs]` block (older static `.MaterialSpace` files deserialize identically with `DynamicExtensionSlot = 0xFFFF`).
- Expose live animated material preview (Play/Pause clock scrubber + Particle Age preview slider) inside `MaterialSpecimenRasterizer` and Frontier's Material & Particle Inspector panes.

---

## 7. Complete Phased Implementation & Validation Roadmap

Every phase is independently testable, builds with `-j 2` to stay comfortably within container RAM limits, enforces `Documentation/BannedWordsVocabulary.md`, and ships visual proof sheets in `Exhibits/Images/`.

| Phase | Scope & Deliverables | Dependencies | Validation Suite & Exit Criteria |
| :--- | :--- | :--- | :--- |
| **Phase 1**<br>*HTML Snow & Sprite Generator* | Build standalone `Experimental/ParticleTextureGenerator/index.html` (8 hexagonal snowflake morphologies, general particle sprites, dissolve masks, 4-channel Alpha/Height/Normal/Sparkle+SDF output, `1×1..8×8` atlas packer, 3D lit tumbling preview, JSON recipe export). Live-serve for user review & tuning. | None (can start immediately!) | Interactive browser preview verified via headless Chromium capture proof sheet (`Snow_Morphologies_Proof.png`, `Sprite_Library_Proof.png`). |
| **Phase 2**<br>*MaterialSpace Procedural & Animated Extensions* | Add `ProceduralModulatorStack`, `AnimatedMaterialClockInputs`, `SurfaceDissolveClipSpec`, and `DynamicExtensionSlot` to `Engine/MaterialSpace/` + `OpenPbrSurfaceScattering.slang` + `MaterialCodec`. Support animated Blackbody, Thin-Film, Noise/Voronoi erosion, and Derez edge emission. | None | New `MaterialProceduralAnimatedValidation` suite (CPU + Vulkan SwiftShader): verifies static material parity (0 regression), animated age/blackbody/thin-film curves, and dissolve clip edges. |
| **Phase 3**<br>*Unified Depth-Occluded & GI-Coupled Particle Renderer* | Implement `ParticleGeometryRasterization` (3D instancing into shared 64-bit Visibility & Depth buffer), `ParticleTransparentRasterization` (depth-tested read-only billboards/ribbons + soft depth fade + sorting), `ParticleSpriteBank` (`Texture2DArray`), and `ParticleRadianceInjectionSequence` (VEPL direct lights + `IrradianceLatticeSequence` bounce injection + sprite GI/shadow sampling). | Phase 2 | New `ParticleSceneIntegrationValidation` suite: proves **(1)** 3D & sprite particles hide cleanly behind occluding walls/chassis, **(2)** emissive particles cast real GI colour bleed onto matte floor/walls, **(3)** non-emissive snow/smoke receives directional shadow + GI bounce, **(4)** 0 Vulkan validation errors. |
| **Phase 4**<br>*Pattern Studio Plugin + Particle Workspace Bridge* | Port Phase 1 generators 1:1 into `Engine/Workspaces/Texturing/Plugins/` (`PatternGeneratorPluginRegistry`, `SnowflakeCrystalGenerator`, `ParticleSpriteGenerator`, `DissolveMaskGenerator`). Wire `PatternStampLayerCategory::ProceduralGenerator` + multi-tile atlas baking + live `ParticleSpriteSourceSpec` link into `Engine/Workspaces/Particle/`. | Phase 1, Phase 3 | Extend `PatternStampWorkspaceUIValidation` + new `ParticleTextureBridgeValidation`: generates 16-variant snowflake atlas & diamond-dissolve mask inside Pattern Studio and renders them live in Particle Workspace. |
| **Phase 5**<br>*ControlVehicle Derez, Reverse-Rez, Dissolve & MoGraph* | Promote `ControlVehicle` (`.facet` + `.sdfspace`) to `EngineContent/EnvironmentArchives/ControlVehicle/`. Implement `SurfaceAnchorSampler`, `VehiclePoseHistory`, `DerezWaveClock.slang`, `VehicleDerezCubeSequence` (exact `16³` cell sync, face-material inheritance, 60 Hz momentum, 8-shard floor shatter), `dissolve-nothing`, `dissolve-butterflies` (articulated hinged wings + Boids takeoff), `glitch-dissolve`, and all 5 `effector-*` MoGraph presets driving `ControlVehicle`. | Phase 2, Phase 3, Phase 4 | New `ParticleVehicleDerezAndMoGraphValidation` suite: multi-frame sequence captures of `ControlVehicle` performing **(a)** HMI Derez + floor shatter + GI glow, **(b)** Reverse-Rez assembly, **(c)** Dissolve-to-Nothing, **(d)** Dissolve-to-Butterflies, and **(e)** MoGraph Effector waves — all with strict depth occlusion & GI bounce proof renders! |
| **Phase 6**<br>*Weather / Billboard / Pyro / Organic Preset Batches* | Port remaining preset batches onto the unified GPU Sprite & 3D Geometry renderer using Pattern Studio procedural sprites & animated OpenPBR materials:<br>• **6A — Storms & Weather**: `snow` (16-variant procedural snowflakes with L5 glints & GI), `hail` (3D refractive L6 ice), `rain`, `sandstorm`, `tornado`.<br>• **6B — Pyro, Water & Atmospheric**: `fire` (L10 Blackbody + VEPL GI), `smoke`, `steam`, `watermist`, `ash`, `embers`, `sparks`, `flares`, `explosion`, `fireworks`, `fireworks-spectacle`, `splash`, `bubbles` (L4 animated Thin-Film), `foam`.<br>• **6C — Wind-Blown, Foliage & Remaining**: `leaves`, `petals`, `feathers`, `seeds`, `confetti`, `paper`, `debris`, `glass`, `lapilli`, `pollen`, `sand`, `coin-fountain`, `coin-stack`, `boids-*`, `ferrofluid-*`, `fracture-*`, `shield-*`, `aurora-borealis`. | Phase 3, Phase 4 | Per-batch HTML vs. C++ side-by-side visual parity suites (matching our `First5` and `Spline` methodology) + Vulkan SwiftShader GPU execution checks. |
| **Phase 7**<br>*Content Browser Real Particle Thumbnails* | Implement `ParticleThumbnailRasterizer`, define `ThumbnailCaptureSpec` across all ported presets, bake `128×128` RGBA32 tiles into `ThumbnailSheet.ImageSpace` + `ContentIndex.Space` under `ContentCategory::Particles`, wire live on-save project thumbnail updates + sub-category filter pills in `ContentBrowserPanel`. | Phase 3 (incremental per batch from Phase 5 onward!) | Extend `ContentBrowserValidation`: verifies every ported particle entry loads a non-placeholder rendered screenshot tile, respects workspace visibility masks, and updates cleanly on custom preset save. |

---

## 8. Key Risks, Guardrails & Open Decisions to Confirm

### Technical Guardrails
1. **Zero Regression on Existing Validated Presets**: `Cloth & Rigging` (`001–005`) and `Silk & Curl` (`024–033`) suites must remain 100% passing at every phase; `ParticleSoftwareExecutor` and `ParticleVulkanExecutor` share the same simulation math and camera basis.
2. **Memory & Build Hygiene**: All validation binaries built cleanly with `-j 2` (avoiding 4 GB container OOM during debug linking); generated large `.ImageSpace` sheets stay compact and deterministic.
3. **Shader Budget & GTX/RTX Parity**:
   - Tier 2 RTX evaluates full Chebyshev `IrradianceLatticeSequence` + RT reflections on opaque 3D particles and VEPL-injected lattice GI.
   - Tier 1 GTX evaluates 6-Cone SDF GI + Hi-Z SSR + VEPL direct lights so every effect (including `ControlVehicle` Derez glow and GI-lit snowfall) works deterministically on both hardware tiers AND headless Vulkan SwiftShader!
4. **Strict Naming Compliance**: Every new file, C++ class/struct/enum, and `.slang` shader runs through our automated `Documentation/BannedWordsVocabulary.md` audit before every commit.

### Quick Decisions You Can Review While Browsing Particle Studio (`:8096`)
1. **Preset Tuning / Culling**: While inspecting the live **Particle Studio** on port `8096`, note any presets you want redesigned, enhanced beyond the HTML prototype (e.g., richer 3D snowflakes, denser fire GI, custom `ControlVehicle` paint colour), or moved into Quarantine.
2. **Phase 1 Kickoff Preference**: We can begin immediately with **Phase 1 (Standalone HTML Hexagonal Snowflake & Particle Sprite Generator)** so you can interactively shape the exact snowflake crystals and sprite looks right in your browser, OR kick off **Phase 3 (Depth-Occluded + GI Particle Renderer)** / **Phase 5 (`ControlVehicle` Derez)** first if you prefer seeing the Vehicle Derez in 3D first.
3. **Content Browser Thumbnail Timing**: Recommended approach is **incremental baking** — wire `ParticleThumbnailRasterizer` right alongside Phase 3 so that every single already-ported preset (`001–005`, `024–033`) immediately gets real rendered thumbnails in Content Browser, and every new batch (Vehicle Derez, Snow/Weather, etc.) automatically populates its thumbnails the moment it ships!
