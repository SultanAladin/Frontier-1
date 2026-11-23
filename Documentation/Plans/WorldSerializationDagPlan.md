# Frontier `Engine/Serialization/` — Suffix Registry, Hybrid TTF-Style Container & Persistent Dependency DAG Plan

> **TL;DR**: Unifies all Frontier persistence under **`Engine/Serialization/`** using the Two-Tier Hybrid container (human-readable UTF-8 PascalCase TOML header + SFNT/TTF-style `64 B`-aligned binary section directory), replaces hardcoded `EditorLifecycle` / `ShowcaseGalleryScene` fallbacks with TOML-driven `.EngineConfiguration` → `.WorldSpace` startup, and introduces a persistent **Dependency DAG** (`xxHash64` + `SHA-256` + `FNV-1a 64`) that loads pre-compiled binaries from `Build/CompiledRepository/` instead of recomputing on boot. All rules from `DocumentFormatsInstancePlan.md`, `DocumentSpacesRevisionPlan.md`, and `DataDrivenSpaceContentFormattingPlan.md` remain active, and **zero banned vocabulary** appears anywhere in C++ units, folders, or specifications.

---

## 1. Mandatory Decisions — Suffix Policy & Canonical Casing

> **Legend** — Blacklist Compliance / Readability / Longevity: `🔴` = Highest (best) · `🚩` = Moderate · `✔️` = Lowest. Migration Cost: `✔️` = Lowest (best). Quality: plain `⭐` (`1–5`).

### 1.1 Handling the 10 Draft Suffixes Colliding with `BannedWordsVocabulary.md`

Ten entries from the initial 41-suffix list contain substrings that collide with `BannedWordsVocabulary.md`. All other **31 approved suffixes** are already 100% clean and adopted verbatim.

| Rank | Policy | Canonical Suffixes on Disk (Write) | Compatibility on Read | Compliance | Migration Cost | Rating |
| :---: | :--- | :--- | :--- | :---: | :---: | :---: |
| 🏆 🥇 | **Option A — Clean Canonical Suffixes + Case-Insensitive Alias Decode** | Upgrades colliding suffixes to 100% clean PascalCase (e.g. `.EngineConfiguration`, `.ParticleSequence`, `.SpawnSource`, `.ShaderNetwork`, `.ComputeProgram`, `.RenderSequence`, `.NoiseNetwork`, `.RadianceClipmap`, `.VariableRegister`, `.PixelSheet`). | Decodes both clean canonical suffixes and legacy/draft aliases case-insensitively. | 🔴 | ✔️ | ⭐⭐⭐⭐⭐ |
| 🥈 | **Option B — Clean `.EngineConfiguration` + Disk-Only Exemptions for Remaining 9** | Upgrades `.EngineConfiguration` immediately; keeps the remaining 9 literal disk suffixes as narrow on-disk exemptions while keeping 100% of C++ under `Engine/Serialization/` clean. | Exact match on disk; requires permanent exemption entries in `BannedWordsVocabulary.md`. | 🚩 | ✔️ | ⭐⭐⭐ |

### 1.2 Canonical Output Casing (Old-Plan Option 2A Preserved)

| Rank | Casing Policy | Write Behavior | Read Behavior (Windows · Linux · Android) | Portability | Consistency with Existing `Content/` | Rating |
| :---: | :--- | :--- | :--- | :---: | :---: | :---: |
| 🏆 🥇 | **Option 2A — Canonical PascalCase Write, Case-Insensitive Read** *(Old-Plan Rule)* | Encoders write canonical PascalCase (`.WorldSpace`, `.FontSpace`, `.SkyAtmosphere`, `.EngineConfiguration`). | Decoders match any casing (`.worldspace`, `.WorldSpace`, `.Fontspace`) case-insensitively and detect collisions on case-sensitive filesystems. | 🔴 | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥈 | **Option 2B — Lowercase Write, Case-Insensitive Read** | Encoders write all-lowercase (`.worldspace`, `.fontspace`). | Case-insensitive decode; diverges from existing `Content/*.WorldSpace` and `EngineContent/*.FontSpace`. | 🚩 | 🚩 | ⭐⭐⭐ |

💡 **Recommendation**: Adopt **Option A + Option 2A**. Every C++ identifier, folder, TOML header, and on-disk suffix stays 100% compliant with `BannedWordsVocabulary.md` while decoding any legacy or draft casing seamlessly.

---

## 2. Authoritative `Engine/Serialization/` Directory Layout

All native persistence migrates cleanly into **`Engine/Serialization/`** (consolidating `Engine/Codecs/` via phased non-breaking move). Strict architectural boundaries remain enforced:

- **`Engine/Serialization/`**: Native Frontier Two-Tier Hybrid TOML + `64 B` binary codecs, persistent Dependency DAG, compiled disk repository, and schema migration.
- **`Engine/Interchange/`**: Strictly external third-party formats (`.ttf`/`.otf`, `.obj`, `.gltf`, `.step`, `.ies`, `.exr`, `.png`, `.wav`, `.svg`, MaterialX) — never mixed into `Engine/Serialization/`.
- **`*Exchange`** (`InputExchange`, `WorkspaceExchange`, `ClipboardExchange`): Strictly live in-memory bi-directional runtime transfer — never disk serialization.

```text
Engine/
└── Serialization/                                 // Sole owner of native TOML + 64B binary persistence & Dependency DAG
    ├── README.md                                  // Suffix registry ledger, container wire layout & naming audit
    ├── Container/                                 // Tier 1 TOML Header + Tier 2 SFNT/TTF-style 64B binary framing
    │   ├── SpaceContainerCodec.{h,cpp}            // Migrated from Engine/Codecs/; PascalCase TOML + 64B sections + FNV-1a 64
    │   ├── SuffixRegistry.{h,cpp}                 // Authoritative 56-suffix lookup (41 priority + 15 retained) + alias resolution
    │   ├── RevisionSectionCodec.{h,cpp}           // Embedded .Revision parametric stream (215 MiB doc / 750 MiB global cap)
    │   └── AtomicFileWriter.{h,cpp}               // Same-volume temp write, fsync flush & atomic rename (Win32/POSIX/Android)
    │
    ├── DependencyDAG/                             // Zero-recompute persistent build DAG & disk repository
    │   ├── DependencyDagLedger.{h,cpp}            // Acyclic edge register, topological sort & cycle-path reporter
    │   ├── ContentHashCalculator.{h,cpp}          // Fast xxHash64 dirty scan + SHA-256 canonical build key + FNV-1a 64 check
    │   ├── CompiledDiskRepository.{h,cpp}         // Bounded Build/CompiledRepository/<Category>/<Hash>.fbin mmap loader & GC
    │   ├── BakeRecipeScheduler.{h,cpp}            // Dirty-only parallel ExecutionChannels dispatch, dedup & cancellation
    │   └── ProvenanceInspector.{h,cpp}            // Diagnostic trace explaining exact hit/miss & upstream invalidation cause
    │
    ├── Codecs/                                    // Per-category native Encoders & Decoders (C++20 concept-constrained)
    │   ├── Configuration/
    │   │   ├── EngineConfigurationCodec.{h,cpp}   // .EngineConfiguration (boot tick rate, memory limits, Vulkan, startup world)
    │   │   ├── GraphicsSettingsCodec.{h,cpp}      // .GraphicsSettings (resolution, V-Sync, FPS cap, shadow/texture presets)
    │   │   ├── UserProfileCodec.{h,cpp}           // .UserProfile (local player preferences, achievements, accessibility)
    │   │   ├── ProjectCodec.{h,cpp}               // .ProjectSpace (project root index, startup world link, dependency locks)
    │   │   └── InputCodec.{h,cpp}                 // .InputSpace (chord registers, navigation gestures, axis calibration)
    │   │
    │   ├── World/
    │   │   ├── WorldSpaceCodec.{h,cpp}            // .WorldSpace (non-kinship containment tree, placements, environment links)
    │   │   ├── ComponentCodec.{h,cpp}             // .Component (reusable typed instance attachments; migrates .ComponentSpace)
    │   │   ├── TriggerZoneCodec.{h,cpp}           // .TriggerZone (sensor bounds & entry/stay/exit signal routing)
    │   │   ├── EventBusCodec.{h,cpp}              // .EventBus (typed signal schemas & decoupled participant routing)
    │   │   ├── VariableRegisterCodec.{h,cpp}      // .VariableRegister (global switches, counters, multipliers, diagnostics)
    │   │   ├── TimerProfileCodec.{h,cpp}          // .TimerProfile (countdowns, cooldowns, time-dilation presets)
    │   │   ├── CurveCodec.{h,cpp}                 // .Curve (1D/2D easing & response curves shared across UI/motion/audio)
    │   │   ├── TimelineCodec.{h,cpp}              // .Timeline (multi-track keyframe sequencer: motion, cameras, audio, captions)
    │   │   ├── SoundscapeCodec.{h,cpp}            // .Soundscape (ambient beds, dynamic stems, spatial snapshots; + .Acoustic)
    │   │   └── NetworkCodec.{h,cpp}               // .NetworkSpace (session routing & replication policy)
    │   │
    │   ├── Interface/
    │   │   ├── InterfaceCodec.{h,cpp}             // .Interface (runtime screens, HUD anchors, containers, z-order)
    │   │   ├── FontCodec.{h,cpp}                  // .FontSpace (baked .ttf/.otf metrics, kerning, MSDF/SDF + contour pages)
    │   │   ├── PixelSheetCodec.{h,cpp}            // .PixelSheet (packed sprite sheets, UI icon pages, virtual texture pages)
    │   │   ├── TooltipCodec.{h,cpp}               // .Tooltip (hover description layouts, stat comparisons, status indicators)
    │   │   └── EditorLayoutCodec.{h,cpp}          // .EditorLayout (FRONTIER_PROFILE_EDITOR=1 ONLY — excluded from Development/Shipping)
    │   │
    │   ├── Graphics/
    │   │   ├── RenderSequenceCodec.{h,cpp}        // .RenderSequence (DAG steps, transient VRAM aliasing, async compute sync)
    │   │   ├── ShaderNetworkCodec.{h,cpp}         // .ShaderNetwork (authoring network → slangc .slang → SPIR-V .spv)
    │   │   ├── ComputeProgramCodec.{h,cpp}        // .ComputeProgram (standalone .slang compute → SPIR-V .spv)
    │   │   ├── PostStackCodec.{h,cpp}             // .PostStack (ACES tone curves, 3D grading LUTs, veiling glare, lens distortion)
    │   │   ├── IesProfileCodec.{h,cpp}            // .IesProfile (measured LM-63 Type C candela curves + 128x32 LUT)
    │   │   ├── SkyAtmosphereCodec.{h,cpp}         // .SkyAtmosphere (Rayleigh/Mie/Ozone curves + 3-LUT sky & aerial fog)
    │   │   ├── RadianceClipmapCodec.{h,cpp}       // .RadianceClipmap (toroidal clipmap / surfel GI checkpoint + provenance)
    │   │   ├── IrradianceCodec.{h,cpp}            // .Irradiance (precomputed diffuse bounce textures & directional SH lobes)
    │   │   ├── SpectralCurveCodec.{h,cpp}         // .SpectralCurve (wavelength dispersion for thin-film iridescence & prisms)
    │   │   ├── LensApertureCodec.{h,cpp}          // .LensAperture (bokeh aperture blades, chromatic fringe, vignette, exposure)
    │   │   ├── RayStructureCodec.{h,cpp}          // .RayStructure (portable geometry recipe + device-keyed BVH4/BLAS/TLAS)
    │   │   ├── DistanceFieldCodec.{h,cpp}         // .DistanceField (3D scalar SDF/UDF grids with explicit sign, units & bounds)
    │   │   ├── GeometryCodec.{h,cpp}              // .GeometrySpace (PolygonNetwork, creased normals, ClusterPyramid LODs)
    │   │   ├── SurfaceParameterCodec.{h,cpp}      // .SurfaceParameterSpace / .UVSpace (UDIM 1001+ & .UVTile seams/pins)
    │   │   ├── MaterialCodec.{h,cpp}              // .MaterialSpace (10-lobe OpenPBR 1.1 + 128B register + 32B bounce summary)
    │   │   ├── TextureCodec.{h,cpp}               // .TextureSpace (multi-UDIM channel-packed BCn/ASTC mipchains)
    │   │   ├── ImageCodec.{h,cpp}                 // .ImageSpace (linear-light HDR/SDR layered canvas & optical sheets)
    │   │   ├── ColourCodec.{h,cpp}                // .ColourSpace (CIE XYZ, ACEScg, Rec.2020, Oklab primaries & ramps)
    │   │   ├── VectorCodec.{h,cpp}                // .VectorSpace (exact Bezier contours, stencils & vector layers)
    │   │   ├── VolumetricCodec.{h,cpp}            // .VolumetricSpace (PlanetaryClouds, CondensateNoiseVolume & height fog)
    │   │   └── PhotometryCodec.{h,cpp}            // .PhotometrySpace (solar ephemeris & clipped-horizon area luminaires)
    │   │
    │   └── Simulation/
    │       ├── FractureTreeCodec.{h,cpp}          // .FractureTree (Voronoi shatter containment, connectivity, stress thresholds)
    │       ├── ShardClusterCodec.{h,cpp}          // .ShardCluster (pre-fractured brittle shards, mass, inertia, sleep thresholds)
    │       ├── ConstraintSetCodec.{h,cpp}         // .ConstraintSet (Jolt joints, ragdoll hinges, pistons, breakable welds)
    │       ├── ConvexHullCodec.{h,cpp}            // .ConvexHull (simplified outer polyhedra & generation provenance)
    │       ├── ImpactProfileCodec.{h,cpp}         // .ImpactProfile (material-pair collision lookup: sparks, decals, fracture)
    │       ├── NoiseNetworkCodec.{h,cpp}          // .NoiseNetwork (deterministic Simplex/Worley/Perlin/Ridged procedural seeds)
    │       ├── ParticleSequenceCodec.{h,cpp}      // .ParticleSequence (schema reserved — runtime consumer queued in BKL-013)
    │       ├── SpawnSourceCodec.{h,cpp}           // .SpawnSource (schema reserved — runtime consumer queued in BKL-013)
    │       ├── VectorFieldCodec.{h,cpp}           // .VectorField (3D velocity grid schema — runtime solver queued in BKL-013)
    │       ├── RibbonTrailCodec.{h,cpp}           // .RibbonTrail (strip-geometry recipe — runtime consumer queued in BKL-013)
    │       ├── SwarmBehaviorCodec.{h,cpp}         // .SwarmBehavior (flocking & boids recipe — runtime solver queued in BKL-013)
    │       └── FluidGridCodec.{h,cpp}             // .FluidGrid (Navier-Stokes grid schema — runtime solver queued in BKL-013)
    │
    └── Migration/                                 // Forward-compatible legacy readers & explicit schema upgraders
        ├── LegacySuffixMigrator.{h,cpp}           // Non-destructive .ComponentSpace/.RenderSpace/.Acoustic/.UVSpace upgrades
        └── ContainerIntegrityValidator.{h,cpp}    // Bounds, offset-overflow, section-overlap & truncation security checks
```

### Build-Profile Visibility (`Editor` · `Development` · `Shipping`)

| Scope | `Editor` (`FRONTIER_PROFILE_EDITOR=1`) | `Development` | `Shipping` |
| :--- | :---: | :---: | :---: |
| `Engine/Serialization/Container/` + Runtime `Codecs/` | 🟢 Compiled | 🟢 Compiled | 🟢 Compiled |
| `Engine/Serialization/DependencyDAG/` (Read Baked Repository) | 🟢 Read + Incremental Bake | 🟢 Read + Incremental Bake | 🟢 Read-Only Pre-Cooked |
| `EditorLayoutCodec` (`.EditorLayout`) & Editor Tooltips | 🟢 Compiled | 🔴 Excluded at CMake | 🔴 Excluded at CMake |
| `Engine/Editor/` & `Experimental/` HTML References | 🟢 Editor-Only | 🔴 Excluded at CMake | 🔴 Excluded at CMake |

---

## 3. Complete Frontier Engine-to-Suffix Correspondence

Priority follows the user's explicit rule: **all 41 newly approved suffixes first** (with Option A clean canonical PascalCase spellings), followed by **15 retained old-plan suffixes** for Frontier categories not superseded by the 41. Every entry links to real `Engine/` directories.

### 3A. Configuration, World & Gameplay (`Engine/Configuration/`, `Engine/Lifecycle/`, `Engine/ComponentClass/`, `Engine/AudioSpace/`)

| Frontier Engine Unit | Canonical Suffix (Option A) | Draft Alias | Tier 1 / Tier 2 Format | Persisted / Pre-Compiled Binary Payload | Status |
| :--- | :--- | :--- | :--- | :--- | :---: |
| `Engine/Configuration/EngineConfiguration` | **`.EngineConfiguration`** | `.engineconfig` | TOML | Boot tick rate, memory ceilings, Vulkan 1.3 API, startup `.WorldSpace` path | 🟢 Active |
| `Engine/Configuration/` + `GeometricRaster/` | **`.GraphicsSettings`** | `.graphicssettings` | TOML | Display resolution, V-Sync, FPS cap, shadow cascade & texture quality presets | 🟢 Active |
| `Engine/Configuration/` + `Lifecycle/` | **`.UserProfile`** | `.userprofile` | TOML *(user dir)* | Local player preferences, accessibility switches, playtime, achievements *(isolated from Git & shared repo)* | 🟢 Active |
| `Engine/Lifecycle/` + `GeometricRaster/` | **`.WorldSpace`** | `.worldspace` | TOML + `64 B` Bin | Top-level world index, non-kinship `RecordEntry` containment tree, transforms, bounds, environment links | 🟢 Active |
| `Engine/ComponentClass/` | **`.Component`** | `.component` | TOML + `64 B` Bin | Reusable typed attachments (RigidBody, PolygonNetwork link, AudioSource); explicit migration from `.ComponentSpace` | 🟢 Active |
| `Engine/SimulationSpace/PhysicsSpace/` | **`.TriggerZone`** | `.triggerzone` | TOML | Non-blocking sensor volumes firing entry, stay, and exit signals | 🚧 Planned |
| `Engine/Input/` + `Lifecycle/` | **`.EventBus`** | `.eventbus` | TOML | Named signal schemas & decoupled participant routing *(non-DAG runtime traffic)* | 🚧 Planned |
| `Engine/Configuration/` + `Lifecycle/` | **`.VariableRegister`** | `.variabletable` | TOML | Global game switches, score counters, difficulty multipliers, diagnostic overrides | 🚧 Planned |
| `Engine/Constants/TimeConstants.h` + `Lifecycle/` | **`.TimerProfile`** | `.timerprofile` | TOML | Reusable countdowns, ability cooldowns, time-dilation presets | 🚧 Planned |
| `Engine/Components/Controls/ScalarCurveEditor` + `Mathematics/PolynomialCurves` | **`.Curve`** | `.curve` | TOML + `64 B` Bin | 1D/2D easing & response keys, tangents, extrapolation, pre-sampled LUT | 🟢 Active |
| `Engine/Components/Controls/SimulationTimelineFooter` | **`.Timeline`** | `.timeline` | TOML + `64 B` Bin | Multi-track keyframe sequencer for motion, camera cuts, audio cues & captions | 🚧 Planned |
| `Engine/AudioSpace/` (`AudioExtension`, `AudioBusAccumulator`, `SpatialAcoustics`) | **`.Soundscape`** | `.soundscape` | TOML + `64 B` PCM/Opus | Ambient beds, dynamic music stems, spatial routing snapshots *(interchange WAV/Opus via `Interchange/`)* | 🟢 Active |

### 3B. Interface, Typography & Editor Layout (`Engine/Components/`, `Engine/Editor/`, `Engine/Workspaces/`)

| Frontier Engine Unit | Canonical Suffix (Option A) | Draft Alias | Tier 1 / Tier 2 Format | Persisted / Pre-Compiled Binary Payload | Status |
| :--- | :--- | :--- | :--- | :--- | :---: |
| `Engine/Components/` (`PixelSpace`, `Panes/`, `Controls/`) | **`.Interface`** | `.interface` | TOML | Runtime UI screens, HUD anchor points, containers, non-kinship nesting & z-order | 🟢 Active |
| `Engine/Components/Appearance/TypographyRepository` + `Interchange/FontInterchange` | **`.FontSpace`** | `.Fontspace` | TOML + `64 B` Bin | Baked `.ttf`/`.otf` character metrics, kerning pairs, MSDF/SDF + contour pages *(zero boot-time `.ttf` parsing)* | 🟢 Active |
| `Engine/Components/ContentBrowser/ThumbnailSheetRepository` + `VectorIcons/` | **`.PixelSheet`** | `.pixelatlas` | TOML + `64 B` BC7/RGBA | Typed sprite sheets, UI icon pages & virtual texture page directories *(editor icon pages separated from Shipping)* | 🟢 Active |
| `Engine/Components/Controls/ControlTooltip` | **`.Tooltip`** | `.tooltip` | TOML | Dynamic hover descriptions, stat comparisons & status indicators *(editor tooltips excluded from Shipping)* | 🟢 Active |
| `Engine/Editor/` + `Components/Docking/` | **`.EditorLayout`** | `.editorlayout` | TOML *(Editor-Only)* | Dock panel arrangements, grid snap sizes, gizmo scales & viewport debug toggles *(never overwrites `.WorldSpace`)* | 🟢 Editor-Only |

### 3C. Rendering, Lighting, Shaders & Spatial Acceleration (`Engine/GeometricRaster/`, `Engine/Shaders/`, `Engine/SpatialAcceleration/`, `Engine/DistanceFieldSpace/`)

| Frontier Engine Unit | Canonical Suffix (Option A) | Draft Alias | Tier 1 / Tier 2 Format | Persisted / Pre-Compiled Binary Payload | Status |
| :--- | :--- | :--- | :--- | :--- | :---: |
| `Engine/GeometricRaster/` (`TieredWorldRenderSequence`, `VulkanWorldRenderSequence`) | **`.RenderSequence`** | `.rendergraph` | TOML → Compiled DAG | Acyclic render step dependencies, transient VRAM aliasing, async compute sync *(explicit temporal resources for history)* | 🟢 Active |
| `Engine/Shaders/` + `MaterialSpace/` | **`.ShaderNetwork`** | `.shadergraph` | TOML → `.slang` → `.spv` | Authoring network compiled via `slangc` into SPIR-V `.spv` + reflection *(strictly `.slang`, never raw `.glsl`/`.hlsl`)* | 🟢 Active |
| `Engine/Shaders/Compute/` + `GeometricRaster/Compute/` | **`.ComputeProgram`** | `.computekernel` | TOML → `.slang` → `.spv` | Standalone `.slang` compute program & compiled SPIR-V variants (frustum culling, terrain, filters) | 🟢 Active |
| `Engine/GeometricRaster/` (Tone, Glare, Grading) | **`.PostStack`** | `.poststack` | TOML + `64 B` 3D LUT | Exposure, ACES tone curves, 3D colour-grading LUTs, veiling glare thresholds & lens distortion | 🟢 Active |
| `Engine/Codecs/PhotometricProfile` + `GeometricRaster/DirectLighting/PhotometricLightTree` | **`.IesProfile`** | `.iesprofile` | TOML + `64 B` `128×32` LUT | Measured LM-63 Type C luminaire distributions, units, orientation & resampled candela grid | 🟢 Active |
| `Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence` | **`.SkyAtmosphere`** | `.skyatmosphere` | TOML + `64 B` 3-LUT | Rayleigh/Mie/Ozone source curves + baked `256×64` Transmittance, `32×32` Multi-Scatter & aerial fog LUTs | 🟢 Active |
| `Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence` | **`.RadianceClipmap`** | `.radiancecache` | TOML + `64 B` Bin | World-space surfel/clipmap GI configuration & hardware/scene-keyed checkpoint *(invalidated on lighting/geometry change)* | 🟢 Active |
| `Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence` | **`.Irradiance`** | `.irradiance` | TOML + `64 B` Bin | Precomputed diffuse bounce textures & directional SH1/SH2 lobe coefficients | 🟢 Active |
| `Engine/ColourSpace/SpectralDistribution` + `MaterialSpace/Scattering/ThinFilmInterference` | **`.SpectralCurve`** | `.spectralcurve` | TOML + `64 B` Bin | Wavelength-sampled dispersion & interference profiles (thin-film iridescence, crystal prisms) | 🟢 Active |
| `Engine/Components/Camera/` + `GeometricRaster/Atmosphere/LensFlareSequence` | **`.LensAperture`** | `.lensaperture` | TOML | Physically modeled aperture bokeh blades, chromatic fringes, vignette & sensor exposure curves | 🟢 Active |
| `Engine/SpatialAcceleration/` (`WideBoundingTree`, `CompressedWideTree`) | **`.RayStructure`** | `.raystructure` | TOML Recipe + Device Bin | Portable geometry build recipe + device/driver-keyed `64 B`/`128 B` 4-Wide BVH4 or `VK_KHR_acceleration_structure` BLAS/TLAS | 🟢 Active |
| `Engine/DistanceFieldSpace/LocalSignedDistanceVolume` | **`.DistanceField`** | `.distancefield` | TOML + `64 B` 3D Grid | Bounded `48³..64³` fp16 scalar SDF/UDF grids with explicit sign convention, physical units, voxel transform & truncation | 🟢 Active |

### 3D. Simulation, Destruction & Queued Ground-Up Redo Solvers (`Engine/GeometrySpace/Fracture/`, `Engine/SimulationSpace/PhysicsSpace/`, `🚧 BKL-013`)

| Frontier Engine Unit | Canonical Suffix (Option A) | Draft Alias | Tier 1 / Tier 2 Format | Persisted / Pre-Compiled Binary Payload | Status |
| :--- | :--- | :--- | :--- | :--- | :---: |
| `Engine/GeometrySpace/Fracture/` (`EngineeringBakedFracture`, `FastRuntimeFracture`) | **`.FractureTree`** | `.fracturetree` | TOML + `64 B` Bin | Voronoi shatter containment tree, chunk connectivity, stress & `Gc` rupture thresholds | 🟢 Active |
| `Engine/GeometrySpace/Fracture/ConvexPartitioning` + `FractureSimulationRunner` | **`.ShardCluster`** | `.shardcluster` | TOML + `64 B` Bin | Pre-fractured brittle debris pieces storing mass, inertia tensors & sleep velocity thresholds | 🟢 Active |
| `Engine/SimulationSpace/PhysicsSpace/ConstraintSolver` | **`.ConstraintSet`** | `.constraintset` | TOML | Jolt v5.6.0 joints (Fixed, Hinge, Slider, Distance, Point, Cone), pistons & `BreakableBond` welds | 🟢 Active |
| `Engine/GeometrySpace/Fracture/ConvexPartitioning` + `BroadphaseSolver` | **`.ConvexHull`** | `.convexhull` | TOML + `64 B` Bin | Outer convex polyhedra (`FlatConvexCell`) + generation provenance for Jolt collision & broadphase | 🟢 Active |
| `Engine/SimulationSpace/PhysicsSpace/CollisionDetection` | **`.ImpactProfile`** | `.impactprofile` | TOML | Material-pair collision lookup (sparks, decals, debris triggers, acoustic impulse & fracture thresholds) | 🚧 Planned |
| `Engine/GeometricRaster/Atmosphere/CloudNoiseVolume` + `Mathematics/` | **`.NoiseNetwork`** | `.noisegraph` | TOML + `64 B` LUT | Deterministic fractal noise recipes (Simplex, Worley, Perlin, Ridged) + baked 2D/3D noise volumes | 🟢 Active |
| `🚧 BKL-013` *(Paused Ground-Up Redo)* | **`.ParticleSequence`** | `.particlesystem` | TOML *(Schema Reserved)* | Master timeline coordinating synchronized spawn sources, LOD fallbacks & spatial bounds *(no simplified stubs)* | 🚧 Redo `BKL-013` |
| `🚧 BKL-013` *(Paused Ground-Up Redo)* | **`.SpawnSource`** | `.emitternode` | TOML *(Schema Reserved)* | Spawn rates, burst curves, lifetime gradients & `.slang` compute update rules | 🚧 Redo `BKL-013` |
| `🚧 BKL-013` *(Paused Ground-Up Redo)* | **`.VectorField`** | `.vectorfield` | TOML + `64 B` 3D Grid | 3D directional velocity grids (curl noise, rotational vortices, turbulence) with physical units & wrap rules | 🚧 Redo `BKL-013` |
| `🚧 BKL-013` *(Paused Ground-Up Redo)* | **`.RibbonTrail`** | `.ribbontrail` | TOML *(Schema Reserved)* | Continuous strip-geometry generators & material slot links | 🚧 Redo `BKL-013` |
| `🚧 BKL-013` *(Paused Ground-Up Redo)* | **`.SwarmBehavior`** | `.swarmbehavior` | TOML *(Schema Reserved)* | Boids, flocking rules & GPU-instanced crowd/spark simulation definitions | 🚧 Redo `BKL-013` |
| `🚧 BKL-013` *(Paused Ground-Up Redo)* | **`.FluidGrid`** | `.fluidgrid` | TOML + `64 B` 3D Grid | Eulerian 2D/3D Navier-Stokes solver grids (velocity, pressure, vorticity, divergence) | 🚧 Redo `BKL-013` |

### 3E. Retained Old-Plan Suffixes (Frontier Categories Not Replaced by the 41 Above)

Every existing old-plan suffix without a direct 1:1 replacement stays active under `Engine/Serialization/Codecs/` so zero existing `Content/` or `EngineContent/` files break:

| Retained Canonical Suffix | Existing Owner | Exact Scope (No Ambiguous Overlap) |
| :--- | :--- | :--- |
| **`.ProjectSpace`** | `Configuration/ProjectSpaceRepository` | Root project index, startup `.WorldSpace` reference, theme/input defaults & dependency locks |
| **`.GeometrySpace`** | `GeometrySpace/` + `SpatialAcceleration/ClusterPyramid` | 3D `PolygonNetwork`, creased normals, 128-face `ClusterPyramid` LODs & surface slot links |
| **`.SurfaceParameterSpace`** *(alias `.UVSpace`)* | `Workspaces/UV/` | UDIM `1001+` & **`.UVTile`** parameterization, seam cuts & pin coordinates *(old rule: `.UVTile` replaces `.UVChart` everywhere)* |
| **`.MaterialSpace`** | `MaterialSpace/` | 10-lobe OpenPBR 1.1 + Glinty `SurfaceMaterial`, `128 B` `CompactMaterialRegister`, `32 B` `BounceLayerSummary` |
| **`.TextureSpace`** | `Workspaces/Texturing/` | Multi-UDIM channel-packed GPU mipchains (BCn/ASTC) — distinct from `.PixelSheet` sprites/icons |
| **`.ImageSpace`** | `ColourSpace/` + `Content/Optics/` | Linear-light HDR/SDR layered canvas & baked optical sheets |
| **`.ColourSpace`** | `ColourSpace/` | CIE XYZ, ACEScg, Rec.2020, Oklab primaries, white points, transfer curves, swatches & ramps |
| **`.VectorSpace`** | `Workspaces/Texturing/` (`Pattern`/`Stamp`) | Exact Bézier/polynomial contours, stencils & 2D vector layer stacks |
| **`.VolumetricSpace`** | `GeometricRaster/Atmosphere/` (`VolumetricCloudSequence`, `VolumetricFogSequence`) | `PlanetaryClouds`, `CondensateNoiseVolume` & height fog — distinct from `.SkyAtmosphere` spectral LUTs |
| **`.PhotometrySpace`** | `GeometricRaster/Atmosphere/CelestialEphemerisSolver` + `DirectLighting/` | Solar ephemeris & clipped-horizon rectangular area luminaires — while `.IesProfile` owns measured LM-63 fixtures |
| **`.NetworkSpace`** | `NetworkingSpace/` | Session routing, transport failover & differential replication policies |
| **`.InputSpace`** | `Input/` + `Configuration/InputSettings` | Hardware chord registers, viewport navigation gestures & axis calibration |
| **`.Sketch`** · **`.Pigment`** · **`.Terrain`** | `Workspaces/ParametricSketcher/` · Paint · World | Parametric CAD B-Rep/NURBS sketches, natural media brush presets & terrain heightfield tiles |

---

## 4. Two-Tier Hybrid TTF/SFNT-Style Container Specification (Old-Plan Rules Enforced)

Every native file in `Content/`, `EngineContent/`, and `Projects/` uses the **Two-Tier Hybrid Container** — a human-readable UTF-8 PascalCase TOML header paired with an **SFNT/TTF-inspired `64 B`-aligned binary section directory** for direct offset jumping and zero-copy memory mapping:

```text
+======================================================================================================+
| TIER 1 — UTF-8 PASCALCASE TOML HEADER (Inspectable in Plain Text & Git Diffs)                        |
|   ['.DocumentHeader']                                                                                |
|     SchemaVersion        = 1                                                                         |
|     ContainerSuffix      = '.WorldSpace'         # Canonical PascalCase (Option 2A)                  |
|     StableIdentifier     = 'uuid-v7-or-stable'   # Persistent cross-save identity                    |
|     DocumentTitle        = 'VehicleClearSkyDefault'                                                  |
|     AuthoringWorkspace   = 'Frontier.WorldWorkspace'                                                 |
|     BinaryAlignmentBytes = 64                    # Mandatory SIMD & GPU staging alignment            |
|                                                                                                      |
|   ['.WorldContainment'] / [[ '.WorldPlacement' ]] / Category TOML Sections                           |
|                                                                                                      |
|   [['.SectionDirectory']]                        # SFNT/TTF-inspired random-access offset directory  |
|     SectionIdentifier     = 'WorldPlacementTransforms'                                               |
|     SubFormatTag          = '.WorldPlacement'                                                        |
|     RelativeAlignedOffset = 0                    # [B] - Verified multiple of 64, overflow-checked   |
|     ByteLength            = 64                   # [B] - Unpadded payload byte length                |
|     ChecksumFnv1a64       = '0xE35F950DC45D054F' # [hash] - Accidental-corruption verification       |
+======================================================================================================+
| EXPLICIT BOUNDARY:  # === FRONTIER_BINARY_BOUNDARY ===  +  [0..63] zero-pad bytes to 64 B boundary   |
+======================================================================================================+
| TIER 2 — PACKED 64-BYTE-ALIGNED BINARY SECTIONS (Direct Offset Jump / Memory-Mappable)               |
|   [Section 0 @ Tier2Start + 0]    → Vertices / SmoothNormals / LUT texels / Baked .FontSpace pages   |
|   [Section 1 @ Tier2Start + 64*k] → ClusterPyramid / BVH4 / Candela 128x32 / SDF 3D scalar grid      |
|   [Section N @ Optional]          → Embedded '.Revision' parametric operation stream                 |
+======================================================================================================+
```

### Enforced Old-Plan Container & Revision Invariants

1. **TTF/OTF Font Baking vs. Container Framing**: The `[['.SectionDirectory']]` offset layout is *inspired by* TrueType/SFNT section directories for O(1) random-access section lookup — it does not pretend a `.WorldSpace` is a font file. Genuine `.ttf` / `.otf` files enter through `Engine/Interchange/FontInterchange` and bake once into `.FontSpace` (metrics, kerning, MSDF/SDF + contour pages) so `.ttf` outlines are never reparsed at runtime.
2. **Hardened Wire Safety (`ContainerIntegrityValidator`)**:
   - Reject negative/overflowing offsets, overlapping section spans, unaligned offsets (`Offset % 64 != 0`), truncated files, or payloads exceeding declared bounds.
   - Verify `FNV-1a 64` checksum on every read section before dereferencing.
   - Preserve unknown forward-compatible TOML keys and binary sections on save, or refuse destructive overwrite with an explicit diagnostic.
   - Enforce path containment (no `..` escape outside project/repository root).
3. **Resolution-Independent `.Revision` Caps (`215 MiB` / `750 MiB`)**:
   - Retained verbatim in `Engine/Workspaces/Revision/`: parametric operations only, **`215 MiB` per open document**, **`750 MiB` global session ceiling**, deterministic cross-document monotonic sequence, and **Redo Purge → Prefix Fold** compaction.
   - **Strict Separation**: Revision caps govern parametric undo history only — never the compiled disk repository of the Build Dependency DAG.
4. **Zero-Abbreviation Discipline**: `.InSpace` remains permanently purged; shortened abbreviations (never abbreviate `Configuration`, `Development`, `Index`, or `Temporary`) are forbidden in headers, C++ types, and suffixes.

---

## 5. Persistent Dependency DAG & Zero-Recompute Disk Repository

To guarantee **zero redundant recomputation across boots and edits**, `Engine/Serialization/DependencyDAG/` persists every compiled/baked output to disk keyed by deterministic transitive content hashes.

```text
  [Authored Source Files: .WorldSpace / .Component / .SkyAtmosphere / .ShaderNetwork / .ttf / .ies]
                                                 │
                                                 ▼  ① Fast Scan: xxHash64(source bytes + upstream hashes)
                                      [DAG Dirty Check Ledger]
                                       │                    │
                     (xxHash64 + SHA-256                    │ (hash mismatch, missing binary,
                      + Schema + ABI match)                 │  or corrupted FNV-1a section)
                                       │                    │
                                       ▼                    ▼
                  [Zero-Copy Load / mmap from          ② Topological Bake via ExecutionChannels
                   Build/CompiledRepository/             (.ttf → .FontSpace pages
                   <Category>/<Sha256Prefix>.fbin]        .slang → SPIR-V .spv
                                                          .SkyAtmosphere → 3-LUT binary
                                                          .IesProfile → 128×32 candela grid
                                                          .FractureTree → Voronoi binary
                                                          .RayStructure → BVH4 / BLAS-TLAS)
                                                            │
                                                            ▼  ③ Atomic Temp-Write + fsync + Rename
                                                 [Write Build/CompiledRepository/<Category>/<Key>.fbin
                                                  + Atomically Update DAG Index Ledger]
```

### Three-Tier Hashing & Key Policy (`ContentHashCalculator`)

| Hash Function | Exact Mechanical Purpose | Scope |
| :--- | :--- | :--- |
| **`xxHash64`** | Ultra-fast change detection during startup scan & live editor edits | Source file bytes + sorted upstream `xxHash64` inputs |
| **`SHA-256`** | Collision-resistant canonical repository key (`<Category>/<Sha256>.fbin`) | Canonicalized TOML + upstream `SHA-256` keys + `SchemaVersion` + compiler/solver version + physical units + colour primaries + target platform ABI *(+ Vulkan device/driver UUID for `.RayStructure` & `.spv`)* |
| **`FNV-1a 64`** | Per-section accidental-corruption check inside Tier 2 binary blocks | Every `64 B`-aligned binary section in `.Space` & `.fbin` containers |

### DAG Safety, Granularity & Lifecycle Rules

1. **Acyclic Build Verification**: `DependencyDagLedger` enforces strict acyclicity on build edges and reports the exact cycle chain on violation. World containment trees, runtime `.EventBus` traffic, and temporal render history are distinct structures — never forced into the build DAG.
2. **Fine-Grained Dirty Propagation**:
   - Editing solar intensity in `.WorldSpace` updates only the lighting uniform — **zero rebuild** of `.GeometrySpace`, `.FontSpace`, `.MaterialSpace`, or `.SkyAtmosphere` LUTs.
   - Editing Rayleigh/Mie scattering in `.SkyAtmosphere` invalidates and rebakes **only** the sky 3-LUT binary and dependent aerial-perspective lookup — never geometry or fonts.
   - Editing a `.Curve` invalidates **only** the consumers that reference its `StableIdentifier`.
3. **Atomic Parallel Execution & Crash Safety**:
   - Parallel bakes run on `ExecutionChannels` with deduplication and cancellation when a newer edit supersedes an in-flight generation.
   - Results publish via `AtomicFileWriter` (write adjacent `.tmp` → flush → atomic rename) **only** if the source generation still matches; failures stay visibly reported and never silently masquerade stale binaries as fresh.
4. **Bounded Disk Quota & Git Hygiene**:
   - `Build/CompiledRepository/` is git-ignored (never track gigabytes of derived binaries in Git).
   - Reachability-aware cleanup prunes unreferenced derived `.fbin` files above a configurable disk ceiling — **never** deletes authored `Content/` documents, `.Revision` history, or unsaved edits.
5. **Hardware & Temporal Guards**:
   - Live Vulkan handles (`VkBuffer`, `VkImage`, `VkAccelerationStructureKHR`) are never serialized; dynamic GPU resources recreate from loaded `.fbin` bytes.
   - Device-bound outputs (`.RayStructure` BLAS/TLAS, SPIR-V permutations) include Vulkan API + driver UUID in their `SHA-256` key and safely rebake on GPU/driver change.
   - `.RadianceClipmap` checkpoints bind to geometry, lighting, material, algorithm, and hardware keys — never load stale GI across mismatched worlds.

---

## 6. Eliminating Hardcoded Showcase Scenes (`EditorLifecycle` & `ShowcaseGalleryScene`)

### Verified Code Audit (Current Gaps)

| Current File & Lines | Observed Violation | Remediation Target |
| :--- | :--- | :--- |
| `Engine/Lifecycle/EditorLifecycle.cpp:333-366` | `PopulateWorldSceneSession()` branches directly to hardcoded C++ scene builders (`BuildApocalypticCityRacingScene`, `BuildBlankPlaneSkyScene`, `BuildVehicleClearSkyDefaultScene`). | Read startup world path from `.EngineConfiguration` → `.ProjectSpace`, decode via `WorldSpaceDecoder` + `DependencyDAG`. |
| `Engine/Lifecycle/EditorLifecycle.cpp:368-385` | Overwrites loaded `.WorldSpace` atmosphere (`AtmosphereEnabled`, `CloudsEnabled`, `LocalHours`, `Moons[0].Visible`) from `ViewportPanel` display defaults and wrongly binds `SolarEnabled = CelestialStarsEnabled`. | Move viewport debug toggles strictly into `.EditorLayout` (Editor-only); decouple `SolarEnabled` from `StarfieldEnabled`; never overwrite authored `.WorldSpace` parameters on load. |
| `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.cpp:987-1032` | `BuildVehicleClearSkyDefaultScene()` silently falls back to hardcoded C++ geometry/materials if `VehicleClearSkyDefault.WorldSpace` is missing or fails to decode. | Surface an actionable diagnostic error on missing/corrupt `.WorldSpace` — **zero silent hardcoded fallback** for user worlds. Keep `FrontierDefaultContentGenerator` strictly as an explicit CLI/offline authoring tool. |
| `Content/Worlds/VehicleClearSkyDefault.WorldSpace` | Header banner comment and containment section name still use legacy blacklisted labels and link legacy `.VolumetricSpace` / `.PhotometrySpace` / `.RenderSpace`. | Upgrade encoder header to `TOML Header` and `['.WorldContainment']` with non-kinship `EnclosingIndex` / `FirstNestedIndex` / `NextPeerIndex` / `DepthLevel` + approved suffix links, while retaining backward-compatible legacy decode during migration. |

### Target Authoritative World Boot & Environment Authoring Sequence

1. **TOML-Driven Startup**:
   `Boot → EngineConfigurationDecoder (.EngineConfiguration) → GraphicsSettingsDecoder (.GraphicsSettings) + UserProfileDecoder (.UserProfile) + [Editor only: EditorLayoutDecoder (.EditorLayout)] → WorldSpaceDecoder (StartupWorldPath .WorldSpace) → DependencyDAG resolve & mmap`.
2. **Non-Kinship World Containment (`~32 B` Lean Instances)**:
   Every world placement and folder row uses flat contiguous storage with `StableIdentifier`, `EnclosingIndex`, `FirstNestedIndex`, `NextPeerIndex`, `DepthLevel`, `InstanceCategory`, `VisibilityEnabled`, transform, and suffix-typed file links — zero kinship pointers, zero virtual bloat.
3. **First Vertical Deliverable — Sun, Clouds, Moon, Stars & Atmosphere**:
   - **HTML Reference Gate First (`CLAUDE.md` §2.1)**: Serve `Experimental/ProjectZeroEditor`, capture live browser `.png` screenshots of the Construct directive list and Outliner right-click sheet across normal, hover, disabled, and edge-clamped conditions before writing native UI code.
   - **Authoritative Transactions**: Constructing Sun, Clouds, Moon, Stars, or Atmosphere executes one typed undoable transaction creating/reusing a single `Environment` folder by `StableIdentifier`, registering the real `Engine/GeometricRaster/Atmosphere/` component, updating `OutlinerPanel` + `InspectorPanel`, and persisting atomically to `.WorldSpace` + `.SkyAtmosphere` / `.VolumetricSpace` / `.PhotometrySpace`.
   - **Safe Removal & GPU Lifetime Sync**: Removing any environment instance unregisters its renderer contribution, synchronizes pending Vulkan work, updates selection cleanly without disturbing peer environment instances, and leaves shared files untouched.

---

## 7. Phased Implementation Sequence & Verification Gates

| Phase | Concrete Deliverable | Mandatory Exit Gate (Zero "It Compiles" Excuses) |
| :---: | :--- | :--- |
| **Phase 0** | Lock Option A vs B & Option 2A decisions; capture `Experimental/ProjectZeroEditor` Construct & Outliner right-click `.png` references; audit every existing `Engine/Codecs/` caller & legacy `Content/` file. | Reference `.png` captures inspected side-by-side; complete dependency inventory verified; zero banned words in any new header/doc. |
| **Phase 1** | Stand up `Engine/Serialization/{Container,Migration}/`: migrate `SpaceContainerCodec`, add `SuffixRegistry` (all 56 suffixes + case-insensitive aliases), `AtomicFileWriter`, and `ContainerIntegrityValidator`. | Round-trip, case-insensitivity, bounds/overflow/overlap/truncation & `FNV-1a 64` corruption checks succeed in `SerializationContainerValidation`. |
| **Phase 2** | Implement `Engine/Serialization/DependencyDAG/` (`DependencyDagLedger`, `ContentHashCalculator`, `CompiledDiskRepository`, `BakeRecipeScheduler`, `ProvenanceInspector`) + first vertical bake (`.FontSpace` from `.ttf` + `.SkyAtmosphere` 3-LUT). | Verified disk hit (`0 ms` rebake) on cold restart, minimal transitive invalidation on single parameter edit, cycle detection, cancellation & crash-safe atomic write checks succeed. |
| **Phase 3** | Remove hardcoded showcase fallbacks in `EditorLifecycle.cpp` & `ShowcaseGalleryScene.cpp`; wire `.EngineConfiguration` → `.WorldSpace` + `.GraphicsSettings` + `.UserProfile` + `.EditorLayout`; fix `SolarEnabled` / `StarfieldEnabled` coupling; land non-kinship `Environment` construct/remove/undo/save. | Missing/corrupt `.WorldSpace` reports explicit error (no hardcoded showcase); save/reopen preserves IDs, containment & visibility; side-by-side `.png` captures verify Sun/Clouds/Moon/Stars/Atmosphere parity. |
| **Phase 4** | Implement remaining active codecs under `Engine/Serialization/Codecs/` (`.Interface`, `.PixelSheet`, `.Tooltip`, `.Soundscape`, `.IesProfile`, `.PostStack`, `.RayStructure`, `.DistanceField`, `.RadianceClipmap`, `.Irradiance`, `.SpectralCurve`, `.LensAperture`, `.FractureTree`, `.ShardCluster`, `.ConstraintSet`, `.ConvexHull`, `.ImpactProfile`, `.NoiseNetwork`, `.Curve`, `.Timeline`, `.TriggerZone`, `.EventBus`, `.VariableRegister`, `.TimerProfile`). | Every active codec succeeds on round-trip, DAG hit/miss, migration & real engine consumer checks; `BKL-013` redo schemas stay explicitly marked reserved with zero simplified stubs. |
| **Phase 5** | Enforce `Editor` / `Development` / `Shipping` isolation & cross-platform build checks (Windows · Linux · Android · Headless); build & run `Frontier`, `FrontierSketcher`, and serialization validations; update `Documentation/FolderStructure.md`. | `Shipping` links zero `EditorLayoutCodec` / `Engine/Editor/` symbols; `Frontier` + `FrontierSketcher` + CTest validations green; every milestone recorded & pushed to active branch. |

---

## 8. Open Decisions Awaiting Confirmation

1. **Colliding Suffix Policy (§1.1)**: Confirm **🏆 🥇 Option A** (upgrade all 10 colliding suffixes to clean canonical PascalCase with case-insensitive alias decode) vs **🥈 Option B** (upgrade `.EngineConfiguration` only and grant disk-only exemptions to the remaining 9).
2. **Canonical Write Casing (§1.2)**: Confirm **🏆 🥇 Option 2A** (canonical PascalCase on write, case-insensitive on read — matching existing `Content/` and old plans) vs **🥈 Option 2B** (lowercase write).
3. **Empty `Environment` Folder Persistence**: Proposed default — retain an empty `Environment` folder after removing its last nested instance until the user explicitly deletes the folder (with undoable confirmation).
