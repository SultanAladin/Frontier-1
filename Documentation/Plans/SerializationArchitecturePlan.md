# Frontier `Engine/Serialization/` — General-Purpose Format Architecture, Embedded Multi-Pillar Containers & Persistent `DependencyOrder` Plan

> **TL;DR**: Comprehensive general-purpose serialization blueprint for **Frontier** covering every authoring and runtime pillar — from textures, pixels, SVG vector graphics, images, and fonts to 3D geometry, CAD sketches, materials, full camera optics, atmosphere, acoustics, physics, fracture networks, particles, fluids, force fields, behavior logic, and runtime interfaces. Enforces the **`*Space` Stratification Rule** (`*Space` strictly for top-level pillars with nothing higher above them; sub-formats like `.CameraOptics`, `.Atmosphere`, `.FractureNetwork`, `.WorkspaceLayout`, `.EngineSettings`, `.RuntimeConfiguration`, `.PlatformSettings`, and `.ProjectIndex` never end in `Space`), supports **single-file multi-pillar embedding** (e.g., `VehicleDefault.Instance` or `ExhaustPlume.ParticleSpace`), replaces hardcoded showcase scenes with TOML-driven startup, and provides **5–6 banned-word-free alternatives for every category** alongside a redesigned `Engine/Serialization/` layout and persistent zero-recompute `DependencyOrder`.

---

## 1. Foundational Rules & Continuity Guarantees

| Rule | Enforcement Policy |
| :--- | :--- |
| **① `*Space` Stratification Rule** | Suffix **`*Space`** is reserved **exclusively** for foundational top-level pillars that have nothing higher above them (`WorldSpace`, `GeometrySpace`, `MaterialSpace`, `TextureSpace`, `ImageSpace`, `PixelSpace`, `VectorSpace`, `ColourSpace`, `FontSpace`, `ParticleSpace`, `PhysicsSpace`, `VolumetricSpace`, `AudioSpace`, `MotionSpace`, `BehaviorSpace`, `InterfaceSpace`, `NetworkSpace`, `InputSpace`). Specialized sub-formats, optics, settings, profiles, and networks **never** end in `Space` (`.CameraOptics`, `.Atmosphere`, `.FractureNetwork`, `.WorkspaceLayout`, `.EngineSettings`, `.RuntimeConfiguration`, `.PlatformSettings`, `.ProjectIndex`). |
| **② Embed-or-Link Containers** | Any container (`.Instance`, `.WorldSpace`, `.ParticleSpace`, `.InterfaceSpace`) can **embed** nested pillars directly inside its own Tier 1 TOML header + Tier 2 `64 B`-aligned SFNT/TTF-style binary section directory **and/or** **link** external shared files by `StableIdentifier` + relative path + `SHA-256`. |
| **③ Option 2A Casing & Zero Abbreviations** | Encoders write canonical **PascalCase** suffixes; decoders match case-insensitively across Windows, Linux, and Android. Shortened suffixes (`.InSpace` or truncated `Configuration`) are permanently barred. |
| **④ Old-Plan Continuity** | Preserves `215 MiB` per-document and `750 MiB` global `.Revision` parametric undo budgets (strictly separate from compiled disk binaries), `.UVTile` (never `.UVChart`), `Serialization/` vs. `Interchange/` vs. `*Exchange` boundaries, `.slang`-only shaders, and `Editor` / `Development` / `Shipping` isolation. |
| **⑤ Zero Banned Vocabulary** | Every folder, file, C++ identifier, TOML header key, and specification complies strictly with `Documentation/BannedWordsVocabulary.md` including all `2026-11-23` user rulings. |

```text
                       TIER A — TOP-LEVEL FOUNDATIONAL PILLARS (*Space ONLY)
   Nothing higher exists above these foundational pillars; each owns a primary coordinate/physical medium.
 ┌──────────────┬───────────────┬──────────────┬───────────────┬───────────────┬────────────────┐
 │ .WorldSpace  │.GeometrySpace │.MaterialSpace│ .TextureSpace │  .ImageSpace  │  .PixelSpace   │
 │ .VectorSpace │ .ColourSpace  │  .FontSpace  │.ParticleSpace │ .PhysicsSpace │.VolumetricSpace│
 │ .AudioSpace  │ .MotionSpace  │.BehaviorSpace│.InterfaceSpace│ .NetworkSpace │  .InputSpace   │
 └──────────────┴───────────────┴──────────────┴───────────────┴───────────────┴────────────────┘
                                               │
                         Embedded inside OR linked by top-level pillars & .Instance
                                               ▼
                       TIER B — SPECIALIZED SUB-FORMATS (NEVER END IN *Space)
 ┌─────────────────────┬───────────────────────┬───────────────────────┬────────────────────────┐
 │ Optics & Render     │ Physics & Simulation  │ Logic & Sequencing    │ Configuration & Layout │
 │ .CameraOptics       │ .FractureNetwork      │ .TriggerZone          │ .EngineSettings        │
 │ .Atmosphere         │ .ShardCluster         │ .EventBus             │ .RuntimeConfiguration  │
 │ .PostStack          │ .ConstraintSet        │ .VariableRegister     │ .PlatformSettings      │
 │ .RenderSequence     │ .ConvexHull           │ .TimerProfile         │ .ProjectIndex          │
 │ .ShaderNetwork      │ .ImpactProfile        │ .Curve                │ .GraphicsSettings      │
 │ .ComputeProgram     │ .ForceField           │ .Timeline             │ .UserProfile           │
 │ .IesProfile         │ .FluidGrid            │ .Component            │ .WorkspaceLayout       │
 │ .SpectralCurve      │ .SpawnSource          │ .Instance             │ .Tooltip               │
 │ .Irradiance         │ .RibbonTrail          │ .Sketch               │                        │
 │ .RadianceClipmap    │ .SwarmBehavior        │ .Pigment              │                        │
 │ .RayStructure       │ .NoiseNetwork         │ .Terrain              │                        │
 │ .DistanceField      │ .Soundscape           │                       │                        │
 └─────────────────────┴───────────────────────┴───────────────────────┴────────────────────────┘
```

---

## 2. Foundational Serialization Mechanism Naming — 6 Alternatives Each

> **Legend** — Mechanical Precision / Zero-Trope Clarity: `🔴` = Highest (best) · `🚩` = Moderate · `✔️` = Lowest. Quality: plain `⭐` (`1–5`).

### 2.1 Zero-Recompute Incremental Build Mechanism

| Rank | Candidate Name | Physical / Algorithmic Mechanism | Precision | Rating |
| :---: | :--- | :--- | :---: | :---: |
| 🏆 🥇 | **`DependencyOrder`** | Acyclic topologically sorted build sequence tracking upstream hashes and scheduling dirty-only bakes | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥈 | **`IncrementalBuildOrder`** | Explicitly emphasizes partial dirty-only rebuilds across sessions | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥉 | **`BuildDependencyChain`** | Ordered directed chain of source-to-compiled dependencies | 🔴 | ⭐⭐⭐⭐ |
| 🏅 | **`AcyclicBuildLedger`** | Verified cycle-free ledger associating source keys with compiled disk binaries | 🚩 | ⭐⭐⭐⭐ |
| 🎖️ | **`TopologicalBuildSchedule`** | Emphasizes compile-time topological sorting and parallel dispatch | 🚩 | ⭐⭐⭐ |
| 6 | **`DirtyPropagationSequence`** | Highlights hash-driven invalidation propagation from modified sources | 🚩 | ⭐⭐⭐ |

### 2.2 Input Trace & Build Origin Stamp

| Rank | Candidate Name | Physical / Algorithmic Mechanism | Precision | Rating |
| :---: | :--- | :--- | :---: | :---: |
| 🏆 🥇 | **`InputSignature`** | Exact deterministic bundle of source hashes, schema version, tool version, units & ABI | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥈 | **`OriginStamp`** | Compact header stamp identifying the exact source files and compiler that built a binary | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥉 | **`SourceTrace`** | Auditable diagnostic chain showing which upstream edit invalidated a compiled binary | 🔴 | ⭐⭐⭐⭐ |
| 🏅 | **`BuildOrigin`** | Direct structural name recording source inputs and compiler metadata | 🚩 | ⭐⭐⭐⭐ |
| 🎖️ | **`GenerationStamp`** | Monotonic generation + `SHA-256` seal protecting against stale async publication | 🚩 | ⭐⭐⭐ |
| 6 | **`DerivationSeal`** | Cryptographic seal verifying compiled output matches its originating sources | 🚩 | ⭐⭐⭐ |

### 2.3 Compilation / Bake Rule Description

| Rank | Candidate Name | Physical / Algorithmic Mechanism | Precision | Rating |
| :---: | :--- | :--- | :---: | :---: |
| 🏆 🥇 | **`BuildSpecification`** | Typed C++20 / TOML specification declaring inputs, parameters, target ABI & output layout | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥈 | **`CompileDescriptor`** | Parameter struct passed into a GPU/CPU compiler or baker unit | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥉 | **`BakeProcedure`** | Deterministic executable transformation turning authoring TOML/interchange into `64 B` binaries | 🔴 | ⭐⭐⭐⭐ |
| 🏅 | **`BuildBlueprint`** | Portable device-independent build description (e.g. for `.RayStructure` BVH4/BLAS) | 🚩 | ⭐⭐⭐⭐ |
| 🎖️ | **`TransformRuleSet`** | Declarative conversion rules governing source-to-binary compilation | 🚩 | ⭐⭐⭐ |
| 6 | **`GenerationDefinition`** | Parametric ruleset for procedural generators (`.NoiseNetwork`, `.FractureNetwork`, `.ConvexHull`) | 🚩 | ⭐⭐⭐ |

### 2.4 Non-Kinship Containment & Lateral Links

| Rank | Containment Structure | Lateral Link Field | Mechanical Meaning | Precision | Rating |
| :---: | :--- | :--- | :--- | :---: | :---: |
| 🏆 🥇 | **`ContainmentIndex`** | **`NextLateralIndex`** | Flat contiguous array linking `EnclosingIndex`, `FirstNestedIndex`, `NextLateralIndex`, `DepthLevel` | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥈 | **`NestedNetwork`** | **`NextEntryIndex`** | Directed containment network of placed instances & folders | 🔴 | ⭐⭐⭐⭐ |
| 🥉 | **`PartitionNetwork`** | **`AdjacentIndex`** | Ideal for spatial/fracture partitions (`.FractureNetwork`) with boundary connectivity | 🔴 | ⭐⭐⭐⭐ |
| 🏅 | **`ContainmentRegister`** | **`NextRowIndex`** | Emphasizes contiguous outliner/world row alignment | 🚩 | ⭐⭐⭐⭐ |
| 🎖️ | **`BranchingIndex`** | **`ForwardLinkIndex`** | Zero-kinship multi-level index with explicit forward linked-index slots | 🚩 | ⭐⭐⭐ |
| 6 | **`LayeredContainment`** | **`NextLevelEntryIndex`** | Depth-indexed flat array ordered by `DepthLevel` for zero-recursion linear scan | 🚩 | ⭐⭐⭐ |

---

## 3. Redesigned `Engine/Serialization/` Folder Architecture — 5 Alternatives

### 3.1 Folder Organization Comparison

| Rank | Architecture Option | Top-Level Subfolders under `Engine/Serialization/` | Modularity | Navigation Clarity | Rating |
| :---: | :--- | :--- | :---: | :---: | :---: |
| 🏆 🥇 | **Option 1 — Pillar-Aligned Functional Layout** | `Container/` · `DependencyOrder/` · `Embedding/` · `Codecs/{Configuration,World,Visual,Optics,Simulation,Logic,Interface}/` · `Migration/` | 🔴 | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥈 | **Option 2 — Flat Codec Directory + Shared Foundation** | `Container/` · `DependencyOrder/` · `Embedding/` · `Codecs/` *(flat)* · `Migration/` | 🚩 | 🚩 | ⭐⭐⭐⭐ |
| 🥉 | **Option 3 — Framing / Compilation / Formats Split** | `Framing/` · `IncrementalBuild/` · `Embedded/` · `Formats/` · `Compatibility/` | 🔴 | 🚩 | ⭐⭐⭐⭐ |
| 🏅 | **Option 4 — Pillar-Colocated Codecs** | Shared `Engine/Serialization/{Container,DependencyOrder,Embedding,Migration}/`; codecs live inside each `Engine/<Pillar>/Serialization/` | 🔴 | 🚩 | ⭐⭐⭐ |
| 🎖️ | **Option 5 — Encoder / Decoder Directory Split** | `Container/` · `DependencyOrder/` · `Encoders/` · `Decoders/` · `Migration/` | 🚩 | ✔️ | ⭐⭐ |

### 3.2 Recommended `Engine/Serialization/` Directory Layout (`Option 1`)

Every single filename, directory, and codec below honors the user's exact fixes (`PixelSpaceCodec`, `AtmosphereCodec`, `CameraOpticsCodec`, `FractureNetworkCodec`, `WorkspaceLayoutCodec`, `EngineSettingsCodec`, `RuntimeConfigurationCodec`, `PlatformSettingsCodec`, `ProjectIndexCodec`) and contains **zero banned words**:

```text
Engine/
└── Serialization/                                     // Sole owner of native Frontier persistence & zero-recompute builds
    ├── README.md                                      // Suffix registry, embedding rules, wire layout & naming audit
    │
    ├── Container/                                     // Sectioned Document Container (TOML Header + SFNT/TTF 64B binary + mmap)
    │   ├── DocumentContainerCodec.{h,cpp}             // UTF-8 PascalCase TOML + 64B sections + FNV-1a 64 + ExecutionChannels + AssetStreamThread
    │   ├── MemoryMappedFileView.{h,cpp}               // Cross-platform read-only zero-copy OS mmap / Win32 MapViewOfFile
    │   ├── SuffixRegistry.{h,cpp}                     // Canonical PascalCase registry + case-insensitive alias decoder
    │   ├── RevisionSectionCodec.{h,cpp}               // Embedded .Revision stream (215 MiB per-doc / 750 MiB global cap)
    │   └── AtomicFileWriter.{h,cpp}                   // Same-volume temp write, fsync flush & atomic rename (Win/Linux/Android)
    │
    ├── Embedding/                                     // Multi-pillar single-file embedding & external link resolution
    │   ├── EmbeddedSectionRouter.{h,cpp}              // Dispatches embedded .GeometrySpace/.MaterialSpace/.BehaviorSpace blocks
    │   ├── ExternalLinkResolver.{h,cpp}               // Resolves StableIdentifier + relative path + SHA-256 shared file links
    │   └── InstanceContainerCodec.{h,cpp}             // .Instance multi-pillar package (e.g. VehicleDefault.Instance)
    │
    ├── DependencyOrder/                               // Persistent zero-recompute build order & compiled disk repository
    │   ├── DependencyOrderLedger.{h,cpp}              // Acyclic edge register, topological sort & cycle-path reporter
    │   ├── ContentHashCalculator.{h,cpp}              // Fast xxHash64 dirty scan + SHA-256 build key + FNV-1a 64 integrity
    │   ├── CompiledDiskRepository.{h,cpp}             // Bounded Build/CompiledRepository/<Category>/<Key>.fbin mmap loader & GC
    │   ├── BakeBuildScheduler.{h,cpp}                 // Dirty-only parallel ExecutionChannels dispatch, dedup & cancellation
    │   └── SourceTraceInspector.{h,cpp}               // Diagnostic trace showing exact hit/miss & upstream invalidation cause
    │
    ├── Codecs/                                        // Per-pillar native Encoders & Decoders (C++20 concept-constrained)
    │   ├── Configuration/                             // Boot, runtime, platform, project & user settings (non-*Space)
    │   │   ├── EngineSettingsCodec.{h,cpp}            // .EngineSettings (tick rate, memory ceilings, Vulkan 1.3, startup world)
    │   │   ├── RuntimeConfigurationCodec.{h,cpp}      // .RuntimeConfiguration (active profile toggles, thread lanes, diagnostic switches)
    │   │   ├── PlatformSettingsCodec.{h,cpp}          // .PlatformSettings (Windows/Linux/Android/Headless hardware overrides)
    │   │   ├── ProjectIndexCodec.{h,cpp}              // .ProjectIndex (project root directory, startup world link, dependency locks)
    │   │   ├── GraphicsSettingsCodec.{h,cpp}          // .GraphicsSettings (resolution, V-Sync, FPS cap, shadow/texture quality)
    │   │   └── UserProfileCodec.{h,cpp}               // .UserProfile (local player preferences, accessibility, playtime — outside Git)
    │   │
    │   ├── World/                                     // World spaces, placements, terrain & attachments
    │   │   ├── WorldSpaceCodec.{h,cpp}                // .WorldSpace (ContainmentIndex, .Instance placements, environment links)
    │   │   ├── ComponentCodec.{h,cpp}                 // .Component (reusable typed attachments; migrates legacy .ComponentSpace)
    │   │   └── TerrainCodec.{h,cpp}                   // .Terrain (heightfield tiles, biome masks, splat weights)
    │   │
    │   ├── Visual/                                    // 3D Geometry, CAD, Pixels, Images, Textures, Vectors, Colour & Fonts
    │   │   ├── GeometrySpaceCodec.{h,cpp}             // .GeometrySpace (PolygonNetwork, creased normals, ClusterPyramid LODs)
    │   │   ├── SurfaceParameterCodec.{h,cpp}          // .SurfaceParameterSpace / .UVSpace (UDIM 1001+ & .UVTile seams/pins)
    │   │   ├── SketchCodec.{h,cpp}                    // .Sketch (parametric B-Rep / NURBS 2D & 3D CAD drafting for FrontierSketcher)
    │   │   ├── PixelSpaceCodec.{h,cpp}                // .PixelSpace (packed sprite/icon sheets, virtual page directories, pixel layouts)
    │   │   ├── ImageSpaceCodec.{h,cpp}                // .ImageSpace (linear-light HDR/SDR layered canvas & optical sheets)
    │   │   ├── TextureSpaceCodec.{h,cpp}              // .TextureSpace (multi-UDIM channel-packed BC7/ASTC GPU mipchains)
    │   │   ├── VectorSpaceCodec.{h,cpp}               // .VectorSpace (SVG/Bezier contours, stencils, variable strokes & vector layers)
    │   │   ├── ColourSpaceCodec.{h,cpp}               // .ColourSpace (CIE XYZ, ACEScg, Rec.2020, Oklab primaries, swatches & ramps)
    │   │   ├── PigmentCodec.{h,cpp}                   // .Pigment (natural media brush, bristle, dab & stylus stroke presets)
    │   │   └── FontSpaceCodec.{h,cpp}                 // .FontSpace (baked .ttf/.otf character metrics, kerning, MSDF/SDF + contours)
    │   │
    │   ├── Optics/                                    // Materials, Atmosphere, Full Camera Optics, Lighting & Render Sequences
    │   │   ├── MaterialSpaceCodec.{h,cpp}             // .MaterialSpace (10-lobe OpenPBR 1.1 + Glinty, 128B register, 32B bounce)
    │   │   ├── ShaderNetworkCodec.{h,cpp}             // .ShaderNetwork (authoring network → slangc .slang → SPIR-V .spv)
    │   │   ├── ComputeProgramCodec.{h,cpp}            // .ComputeProgram (standalone .slang compute → SPIR-V .spv)
    │   │   ├── SpectralCurveCodec.{h,cpp}             // .SpectralCurve (wavelength dispersion for thin-film iridescence & prisms)
    │   │   ├── NoiseNetworkCodec.{h,cpp}              // .NoiseNetwork (deterministic Simplex/Worley/Perlin/Ridged seeds & LUTs)
    │   │   ├── AtmosphereCodec.{h,cpp}                // .Atmosphere (sky Rayleigh/Mie/Ozone 3-LUT, solar/lunar/star & aerial fog)
    │   │   ├── CameraOpticsCodec.{h,cpp}              // .CameraOptics (ENTIRE lens: focal length, glass groups, aperture/bokeh, CA, vignette, sensor)
    │   │   ├── PostStackCodec.{h,cpp}                 // .PostStack (ACES tone curves, 3D grading LUTs, veiling glare & film response)
    │   │   ├── RenderSequenceCodec.{h,cpp}            // .RenderSequence (acyclic render steps, VRAM aliasing & async compute sync)
    │   │   ├── IesProfileCodec.{h,cpp}                // .IesProfile (measured LM-63 Type C candela distributions + 128x32 LUT)
    │   │   ├── PhotometryCodec.{h,cpp}                // .PhotometrySpace (solar ephemeris & clipped-horizon rectangular area lights)
    │   │   ├── IrradianceCodec.{h,cpp}                // .Irradiance (precomputed diffuse bounce textures & directional SH lobes)
    │   │   ├── RadianceClipmapCodec.{h,cpp}           // .RadianceClipmap (world-space surfel/clipmap GI configuration & checkpoint)
    │   │   ├── RayStructureCodec.{h,cpp}              // .RayStructure (portable geometry build spec + device-keyed BVH4/BLAS/TLAS)
    │   │   └── DistanceFieldCodec.{h,cpp}             // .DistanceField (bounded 3D scalar SDF/UDF grids, sign convention & units)
    │   │
    │   ├── Simulation/                                // Physics, Destruction, Particles, Volumetrics, Fluids, ForceFields & Audio
    │   │   ├── PhysicsSpaceCodec.{h,cpp}              // .PhysicsSpace (rigid dynamics bodies, inertia, vehicle powertrain & worlds)
    │   │   ├── FractureNetworkCodec.{h,cpp}           // .FractureNetwork (Voronoi shatter connectivity network & Gc stress thresholds)
    │   │   ├── ShardClusterCodec.{h,cpp}              // .ShardCluster (pre-fractured brittle shards, mass, inertia & sleep velocity)
    │   │   ├── ConstraintSetCodec.{h,cpp}             // .ConstraintSet (Jolt v5.6.0 joints, ragdoll hinges, pistons & BreakableBonds)
    │   │   ├── ConvexHullCodec.{h,cpp}                // .ConvexHull (simplified outer polyhedra & build origin stamp)
    │   │   ├── ImpactProfileCodec.{h,cpp}             // .ImpactProfile (material-pair collision response: sparks, decals, fracture)
    │   │   ├── VolumetricSpaceCodec.{h,cpp}           // .VolumetricSpace (PlanetaryClouds, CondensateNoiseVolume, height/local fog)
    │   │   ├── AudioSpaceCodec.{h,cpp}                // .AudioSpace (spatial acoustics, reverb zones, occlusion & PCM/Opus routing)
    │   │   ├── SoundscapeCodec.{h,cpp}                // .Soundscape (ambient beds, dynamic music stems & spatial bus snapshots)
    │   │   ├── ParticleSpaceCodec.{h,cpp}             // .ParticleSpace (multi-source particle timeline + embedded fields/colours — BKL-013)
    │   │   ├── SpawnSourceCodec.{h,cpp}               // .SpawnSource (spawn rates, burst curves, lifetime gradients — BKL-013)
    │   │   ├── ForceFieldCodec.{h,cpp}                // .ForceField (3D velocity/curl/vortex/turbulence fields — BKL-013)
    │   │   ├── FluidGridCodec.{h,cpp}                 // .FluidGrid (Eulerian 2D/3D Navier-Stokes velocity/pressure/vorticity — BKL-013)
    │   │   ├── RibbonTrailCodec.{h,cpp}               // .RibbonTrail (strip-geometry arcs, contrails, tracers, lightning — BKL-013)
    │   │   └── SwarmBehaviorCodec.{h,cpp}             // .SwarmBehavior (boids, flocking & GPU-instanced crowd/spark rules — BKL-013)
    │   │
    │   ├── Logic/                                     // Behavior Logic, Signals, Motion, Curves & Sequencing
    │   │   ├── BehaviorSpaceCodec.{h,cpp}             // .BehaviorSpace (compiled/declarative behavior logic controlling instances)
    │   │   ├── MotionSpaceCodec.{h,cpp}               // .MotionSpace (kinematic rigs, procedural dynamics & motion blending)
    │   │   ├── TriggerZoneCodec.{h,cpp}               // .TriggerZone (non-blocking sensor bounds firing entry/stay/exit signals)
    │   │   ├── EventBusCodec.{h,cpp}                  // .EventBus (named signal schemas & decoupled participant routing)
    │   │   ├── VariableRegisterCodec.{h,cpp}          // .VariableRegister (global switches, counters, multipliers & diagnostic overrides)
    │   │   ├── TimerProfileCodec.{h,cpp}              // .TimerProfile (reusable countdowns, cooldowns & time-dilation presets)
    │   │   ├── CurveCodec.{h,cpp}                     // .Curve (1D/2D easing & response curves shared across UI/motion/audio)
    │   │   └── TimelineCodec.{h,cpp}                  // .Timeline (multi-track keyframe sequencer: motion, cameras, audio, captions)
    │   │
    │   └── Interface/                                 // Runtime UI, Workspace Layout, Tooltips, Input & Networking
    │       ├── InterfaceSpaceCodec.{h,cpp}            // .InterfaceSpace (runtime HUD screens, anchors, non-kinship nesting & z-order)
    │       ├── TooltipCodec.{h,cpp}                   // .Tooltip (hover description layouts, stat comparisons & status indicators)
    │       ├── WorkspaceLayoutCodec.{h,cpp}           // .WorkspaceLayout (FRONTIER_PROFILE_EDITOR=1 ONLY — dock splits, snap & gizmos)
    │       ├── InputSpaceCodec.{h,cpp}                // .InputSpace (hardware chord registers, navigation gestures & axis calibration)
    │       └── NetworkSpaceCodec.{h,cpp}              // .NetworkSpace (session routing, transport failover & differential replication)
    │
    └── Migration/                                     // Forward-compatible legacy readers & strict integrity guards
        ├── LegacySuffixMigrator.{h,cpp}               // Non-destructive upgrades (.ProjectSpace/.ComponentSpace/.RenderSpace/.Acoustic)
        └── ContainerIntegrityValidator.{h,cpp}        // Bounds, offset-overflow, section-overlap, path-escape & truncation guards
```

---

## 4. Comprehensive General-Purpose Format Directory — 5+ Alternatives Per Category

Every category below covers a pillar of Frontier's general-purpose architecture, obeys the **`*Space` Stratification Rule**, avoids all banned words, and presents **5–6 ranked naming alternatives** so you can lock your exact preference for every slot.

### Category 1 — Boot, Runtime, Platform & Project Configuration *(Non-`*Space`)*

Your requested baseline splits configuration cleanly into four focused files: **`.EngineSettings`** + **`.RuntimeConfiguration`** + **`.PlatformSettings`** + **`.ProjectIndex`**, paired with **`.GraphicsSettings`**, **`.UserProfile`**, and Editor-only **`.WorkspaceLayout`**.

| Slot | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Boot Engine Settings** | **`.EngineSettings`** | `.BootSettings` | `.EngineConfiguration` | `.LaunchSettings` | `.HostSettings` | `.StartupSettings` | `Engine/Configuration/EngineConfiguration` |
| **Runtime Behaviour** | **`.RuntimeConfiguration`** | `.ExecutionConfiguration` | `.SessionConfiguration` | `.RuntimeSettings` | `.LiveConfiguration` | `.EngineProfile` | `Engine/Configuration/BuildConfiguration` |
| **OS / Hardware Overrides** | **`.PlatformSettings`** | `.DeviceSettings` | `.TargetSettings` | `.HardwareSettings` | `.PlatformConfiguration` | `.DeviceProfile` | `Engine/Windowing/` + `Configuration/` |
| **Project Root Index** | **`.ProjectIndex`** | `.ProjectConfiguration` | `.ProjectDirectory` | `.ProjectRegistry` | `.ProjectDescriptor` | `.ProjectSpace` *(legacy)* | `Engine/Configuration/ProjectSpaceRepository` |
| **Display & Quality** | **`.GraphicsSettings`** | `.DisplaySettings` | `.QualityPreset` | `.RenderSettings` | `.VisualSettings` | `.ViewportQuality` | `Engine/Configuration/` + `GeometricRaster/` |
| **Local Player Prefs** | **`.UserProfile`** | `.PlayerProfile` | `.AccountPreferences` | `.LocalPreferences` | `.AccessibilityProfile` | `.UserSettings` | `Engine/Lifecycle/` *(outside Git)* |
| **Editor Dock Layout** | **`.WorkspaceLayout`** | `.EditorLayout` | `.DockLayout` | `.PanelArrangement` | `.AuthoringLayout` | `.PaneLayout` | `Engine/Editor/` + `Components/Docking/` *(Editor-Only)* |

---

### Category 2 — World Pillar & Multi-Pillar Embedded Containers

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Top-Level World** | 🟢 Yes | **`.WorldSpace`** | `.RealmSpace` | `.RegionSpace` | `.SectorSpace` | `.TerrainSpace` | `.LevelSpace` | `Engine/Lifecycle/` + `GeometricRaster/` |
| **Multi-Pillar Package** | 🔴 No | **`.Instance`** | `.WorldInstance` | `.PlacementBundle` | `.CompoundInstance` | `.PackageInstance` | `.PrefabInstance` | `Engine/Serialization/Embedding/` + `ComponentClass/` |
| **Modular Attachment** | 🔴 No | **`.Component`** | `.Attachment` | `.TraitBlock` | `.CapabilityBlock` | `.PartDefinition` | `.ComponentPart` | `Engine/ComponentClass/` |
| **Terrain Tiles** | 🔴 No | **`.Terrain`** | `.HeightField` | `.LandscapeTile` | `.ElevationGrid` | `.GroundTile` | `.BiomeTile` | `Engine/GeometrySpace/` + `World/` |

---

### Category 3 — 3D Geometry, Surface Parameterization, CAD & Convex Bounds

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **3D Polygon & LODs** | 🟢 Yes | **`.GeometrySpace`** | `.SurfaceSpace` | `.FormSpace` | `.PolyhedronSpace` | `.SolidSpace` | `.ShapeSpace` | `Engine/GeometrySpace/Network/` + `SpatialAcceleration/ClusterPyramid` |
| **UV / UDIM Tiles** | 🔴 No | **`.SurfaceParameter`** | `.UVLayout` | `.ParameterTile` | `.SeamLayout` | `.UnwrapLayout` | `.SurfaceParameterSpace` *(legacy)* | `Engine/Workspaces/UV/` *(embeds `.UVTile`)* |
| **Parametric CAD / B-Rep** | 🔴 No | **`.Sketch`** | `.ParametricSolid` | `.DraftingSheet` | `.BoundarySolid` | `.CurveProfile` | `.SolidSketch` | `Engine/Workspaces/ParametricSketcher/` |
| **Collision Convex Hull** | 🔴 No | **`.ConvexHull`** | `.OuterPolyhedron` | `.CollisionHull` | `.BoundPolyhedron` | `.ConvexVolume` | `.BroadphaseHull` | `Engine/GeometrySpace/Fracture/ConvexPartitioning` |

---

### Category 4 — Pixels, Images, GPU Textures, Colour Science & Natural Media

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Packed Pixels / Sprites / Pages** | 🟢 Yes | **`.PixelSpace`** | `.PixelSheet` | `.SpriteSheet` | `.PageSheet` | `.RasterSheet` | `.IconSheet` | `Engine/Components/PixelSpace` + `ThumbnailSheetRepository` |
| **Layered Linear Images / HDRI** | 🟢 Yes | **`.ImageSpace`** | `.CanvasSpace` | `.PhotoSpace` | `.PictureSpace` | `.RasterSpace` | `.BitmapSpace` | `Engine/ColourSpace/` + `Content/Optics/` |
| **GPU Mipchains / UDIM Channels** | 🟢 Yes | **`.TextureSpace`** | `.ChannelSpace` | `.TexelSpace` | `.SurfaceTexels` | `.MipSpace` | `.MaterialTexels` | `Engine/Workspaces/Texturing/` |
| **Tristimulus Colour & Ramps** | 🟢 Yes | **`.ColourSpace`** | `.ChromaSpace` | `.SpectrumSpace` | `.RadiometrySpace` | `.GamutSpace` | `.ToneSpace` | `Engine/ColourSpace/` |
| **Natural Media Brushes** | 🔴 No | **`.Pigment`** | `.BrushMedium` | `.StrokeMedium` | `.PaintMedium` | `.BristleProfile` | `.InkMedium` | `Engine/Workspaces/Texturing/` |

---

### Category 5 — 2D/3D Vector Graphics (SVG/Bézier) & Typography

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **SVG / Bézier / Stencils** | 🟢 Yes | **`.VectorSpace`** | `.ContourSpace` | `.PathSpace` | `.SplineSpace` | `.CurveGraphic` | `.OutlineSpace` | `Engine/Workspaces/Texturing/` (`Pattern`/`Stamp`) + `VectorIcons/` |
| **Baked `.ttf`/`.otf` Fonts** | 🟢 Yes | **`.FontSpace`** | `.TypefaceSpace` | `.LetterSpace` | `.TypographySpace` | `.TypeSpace` | `.CharacterSpace` | `Engine/Components/Appearance/TypographyRepository` + `Interchange/FontInterchange` |

---

### Category 6 — Materials, Shaders, Dispersion & Procedural Noise

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **OpenPBR 1.1 + Glinty Materials** | 🟢 Yes | **`.MaterialSpace`** | `.ShadingSpace` | `.SurfaceOpticsSpace` | `.FinishSpace` | `.CoatingSpace` | `.AppearanceSpace` | `Engine/MaterialSpace/` |
| **Visual Shader Network** | 🔴 No | **`.ShaderNetwork`** | `.ShadingNetwork` | `.ClosureNetwork` | `.SlangNetwork` | `.SurfaceProgram` | `.ShaderBlueprint` | `Engine/Shaders/` *(compiles via `slangc` → `.spv`)* |
| **Standalone GPU Compute** | 🔴 No | **`.ComputeProgram`** | `.SlangCompute` | `.DispatchProgram` | `.GpuProcedure` | `.ComputeDispatch` | `.ComputeUnit` | `Engine/Shaders/Compute/` + `GeometricRaster/Compute/` |
| **Wavelength Dispersion** | 🔴 No | **`.SpectralCurve`** | `.DispersionProfile` | `.WavelengthProfile` | `.RefractionSpectrum` | `.IridescenceCurve` | `.OpticalSpectrum` | `Engine/ColourSpace/SpectralDistribution` + `ThinFilmInterference` |
| **Deterministic Procedural Noise** | 🔴 No | **`.NoiseNetwork`** | `.NoiseField` | `.FractalNoise` | `.ProceduralNoise` | `.NoiseGenerator` | `.LatticeNoise` | `Engine/GeometricRaster/Atmosphere/CloudNoiseVolume` |

---

### Category 7 — Atmosphere, Lighting & Photometry

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Sky, Scattering, Fog & Celestials** | 🔴 No | **`.Atmosphere`** | `.SkyMedium` | `.AtmosphericProfile` | `.CelestialAtmosphere` | `.AerialMedium` | `.SkyOptics` | `Engine/GeometricRaster/Atmosphere/` (`AtmosphereCodec`) |
| **Measured LM-63 Luminaires** | 🔴 No | **`.IesProfile`** | `.CandelaProfile` | `.LuminaireDistribution` | `.Goniophotometry` | `.LampDistribution` | `.PhotometricBeam` | `Engine/Codecs/PhotometricProfile` + `DirectLighting/` |
| **Solar & Area Luminaires** | 🟢/🔴 | **`.Photometry`** | `.LightProfile` | `.SolarEphemeris` | `.AreaLuminaire` | `.RadiantSource` | `.PhotometrySpace` *(legacy)* | `Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver` + `DirectLighting/` |
| **Diffuse Bounce Irradiance** | 🔴 No | **`.Irradiance`** | `.DiffuseBounce` | `.HarmonicIrradiance` | `.HemisphereBounce` | `.BounceRadiance` | `.AmbientLobes` | `Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence` |
| **Real-Time World GI Checkpoints** | 🔴 No | **`.RadianceClipmap`** | `.SurfelLattice` | `.RadianceVolume` | `.GlobalRadiance` | `.ClipmapRadiance` | `.RadianceField` | `Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence` |

---

### Category 8 — Full Camera Optics, Post-Processing, Render Order & Ray Structures

Notice: **`.CameraOptics`** replaces `.LensAperture` so it holds the **entire physical camera & lens stack** (focal length, multi-group optical distortion, aperture blade shape & bokeh, chromatic aberration, breathing, vignette, shutter, ISO, sensor spectral response, and exposure curves).

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Complete Physical Lens & Sensor** | 🔴 No | **`.CameraOptics`** | `.OpticalLens` | `.LensProfile` | `.CameraLensOptics` | `.ImagingOptics` | `.CineOptics` | `Engine/Components/Camera/` + `Atmosphere/LensFlareSequence` (`CameraOpticsCodec`) |
| **Tone, Grading & Glare Stack** | 🔴 No | **`.PostStack`** | `.ToneStack` | `.GradingStack` | `.DisplayTransform` | `.OpticalPostStack` | `.FilmResponseStack` | `Engine/GeometricRaster/PostProcess/` |
| **Acyclic Render Execution Plan** | 🔴 No | **`.RenderSequence`** | `.RenderSchedule` | `.ExecutionSequence` | `.RasterSchedule` | `.CommandSequence` | `.GpuWorkSchedule` | `Engine/GeometricRaster/` (`TieredWorldRenderSequence`) |
| **Hardware / BVH4 Ray Structure** | 🔴 No | **`.RayStructure`** | `.RayAcceleration` | `.WideBoundingIndex` | `.TraceStructure` | `.RayBoundsIndex` | `.IntersectionIndex` | `Engine/SpatialAcceleration/` (BVH4 & `VK_KHR_acceleration_structure`) |
| **3D Scalar SDF / UDF Volumes** | 🔴 No | **`.DistanceField`** | `.SignedDistanceGrid` | `.UnsignedDistanceGrid` | `.ScalarDistanceVolume` | `.VoxelDistanceGrid` | `.ProximityVolume` | `Engine/DistanceFieldSpace/LocalSignedDistanceVolume` |

---

### Category 9 — Physics, Joints, Fracture Networks & Collision Response

Notice: **`.FractureNetwork`** uses `Network` for chunk connectivity and rupture propagation.

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Rigid & Vehicle Physics** | 🟢 Yes | **`.PhysicsSpace`** | `.MechanicsSpace` | `.DynamicsSpace` | `.KineticsSpace` | `.RigidSpace` | `.SimulationSpace` *(legacy)* | `Engine/SimulationSpace/PhysicsSpace/` |
| **Voronoi Shatter Connectivity** | 🔴 No | **`.FractureNetwork`** | `.ShatterNetwork` | `.RuptureNetwork` | `.CleavageNetwork` | `.CellFracture` | `.VoronoiNetwork` | `Engine/GeometrySpace/Fracture/` (`FractureNetworkCodec`) |
| **Pre-Fractured Debris Pieces** | 🔴 No | **`.ShardCluster`** | `.FragmentCluster` | `.DebrisCluster` | `.ShatterPieces` | `.BreakageCluster` | `.ChunkCluster` | `Engine/GeometrySpace/Fracture/` + `FractureSimulationRunner` |
| **Mechanical Joints & Welds** | 🔴 No | **`.ConstraintSet`** | `.JointSet` | `.LinkageSet` | `.RigConstraints` | `.BondSet` | `.ArticulationSet` | `Engine/SimulationSpace/PhysicsSpace/ConstraintSolver` |
| **Material-Pair Collision Rules** | 🔴 No | **`.ImpactProfile`** | `.ContactProfile` | `.CollisionResponse` | `.SurfaceImpact` | `.StrikeProfile` | `.ImpulseResponse` | `Engine/SimulationSpace/PhysicsSpace/CollisionDetection` |

---

### Category 10 — `ForceField` vs. `VectorField` Comparison *(6 Alternatives)*

You explicitly asked **`ForceField or VectorField ?`**. Here is the exact mechanical distinction and 6 ranked options:

| Rank | Candidate Suffix | Physical Meaning | When It Wins | Precision | Rating |
| :---: | :--- | :--- | :--- | :---: | :---: |
| 🏆 🥇 | **`.ForceField`** | Physical force/acceleration field ($\mathbf{F}(\mathbf{x}, t)$ in $\text{N}$ or $\text{m/s}^2$): wind, gravity wells, vortex drag, magnetic push, wake turbulence — drives particles, cloth, fluids & rigid bodies | Best single unified physical name; aligns with `ForceModel` exemption in `BannedWordsVocabulary.md` | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥈 | **`.VectorField`** | Pure mathematical 3D directional vector grid ($\mathbf{v}(\mathbf{x}) \in \mathbb{R}^3$) — can represent velocity, curl noise, gradients, or normals regardless of physical units | Best if you want pure unit-agnostic 3D vector grids separate from force equations | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥉 | **`.VelocityField`** | Specific Eulerian velocity grid ($\text{m/s}$) advecting particles & smoke | Narrower than forces (excludes pressure/gravity acceleration) | 🔴 | ⭐⭐⭐⭐ |
| 🏅 | **`.ImpulseField`** | Transient directional impulse volume (explosions, shockwaves, sonic blasts) | Great for blasts, too narrow for steady wind/drag | 🚩 | ⭐⭐⭐ |
| 🎖️ | **`.SpatialField`** | Generic 3D field container holding scalar, vector, or tensor channels | Slightly broad; overlaps `.VolumetricSpace` | 🚩 | ⭐⭐⭐ |
| 6 | **`.DynamicField`** | Time-varying procedural + grid field solver specification | Less concrete than `.ForceField` or `.VectorField` | 🚩 | ⭐⭐⭐ |

💡 **Recommendation**: Use **`.ForceField`** for physical forces driving simulation (wind, vortices, drag, wakes, attractors) and keep **`.VectorField`** strictly as an embedded sub-section tag inside `.ForceField` / `.VolumetricSpace` when storing raw `64 B`-aligned 3D float3 vector grids!

---

### Category 11 — Particles, Volumetrics, Fluids, Trails & Swarms

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Particulate Simulation** | 🟢 Yes | **`.ParticleSpace`** | `.ParticulateSpace` | `.GranularSpace` | `.PlumeSpace` | `.SpraySpace` | `.ClusterSpace` | `🚧 BKL-013` *(embeds `.ForceField`, `.ColourSpace`, `.Curve`, `.SpawnSource`)* |
| **Clouds, Fog & Participating Media** | 🟢 Yes | **`.VolumetricSpace`** | `.MediumSpace` | `.DensitySpace` | `.CloudSpace` | `.PlumeVolumeSpace` | `.AerosolSpace` | `Engine/GeometricRaster/Atmosphere/` (`VolumetricCloudSequence`, `VolumetricFogSequence`) |
| **Eulerian Navier-Stokes Fluids** | 🔴 No | **`.FluidGrid`** | `.NavierStokesGrid` | `.FluidVolumeGrid` | `.LiquidVolume` | `.AdvectionGrid` | `.EulerianFluid` | `🚧 BKL-013` *(Velocity, Pressure, Vorticity, Divergence 3D grids)* |
| **Particle Spawn Rules** | 🔴 No | **`.SpawnSource`** | `.ParticleSource` | `.BurstSource` | `.BirthProfile` | `.DischargeSource` | `.SeedVolume` | `🚧 BKL-013` *(Spawn rates, burst curves & lifetime gradients)* |
| **Continuous Strip Trails** | 🔴 No | **`.RibbonTrail`** | `.StripTrail` | `.WakeRibbon` | `.ContrailStrip` | `.ArcRibbon` | `.StreakStrip` | `🚧 BKL-013` |
| **Flocking & Boids Crowd Rules** | 🔴 No | **`.SwarmBehavior`** | `.FlockBoids` | `.CrowdSteering` | `.SwarmDynamics` | `.AgentFlocking` | `.CollectiveMotion` | `🚧 BKL-013` |

---

### Category 12 — Spatial Acoustics & Audio

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Top-Level Spatial Acoustics** | 🟢 Yes | **`.AudioSpace`** | `.AcousticSpace` | `.SoundSpace` | `.SonicSpace` | `.WaveSpace` | `.AuditorySpace` | `Engine/AudioSpace/` (`AudioExtension`, `SpatialAcoustics`, `AudioBusAccumulator`) |
| **Ambient Beds, Stems & Snapshots** | 🔴 No | **`.Soundscape`** | `.AcousticScene` | `.BusSnapshot` | `.StemArrangement` | `.AudioBed` | `.AmbienceProfile` | `Engine/AudioSpace/` *(raw WAV/Opus enters via `Interchange/`)* |

---

### Category 13 — Behavior & Control Logic

Because declarative/compiled behavior logic controlling instances, vehicles, and world responses is a **top-level foundational pillar**, it earns **`*Space`**:

| Rank | Candidate Pillar | Mechanical Meaning | Clarity | Rating |
| :---: | :--- | :--- | :---: | :---: |
| 🏆 🥇 | **`.BehaviorSpace`** | Declarative & compiled behavior rules, transitions, and physical responses attached to `.Instance` or `.WorldSpace` | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥈 | **`.LogicSpace`** | Pure deterministic logic rules, conditions, and signal reactions with zero legacy language jargon | 🔴 | ⭐⭐⭐⭐⭐ |
| 🥉 | **`.DirectiveSpace`** | Command-driven operational directives aligned with Frontier's existing `Directive` vocabulary | 🔴 | ⭐⭐⭐⭐ |
| 🏅 | **`.RuleSpace`** | Condition-to-response rule networks governing gameplay, physics reactions, and UI logic | 🚩 | ⭐⭐⭐⭐ |
| 🎖️ | **`.AutomationSpace`** | Triggered mechanical/environmental automation (doors, vehicle units, waves, timers) | 🚩 | ⭐⭐⭐ |
| 6 | **`.ControlSpace`** | Feedback control loops, vehicle drivetrain governors, and interactive control response | 🚩 | ⭐⭐⭐ |

---

### Category 14 — Triggers, Signals, Variables, Timers, Curves & Timelines

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Kinematics & Motion Blending** | 🟢 Yes | **`.MotionSpace`** | `.KineticSpace` | `.AnimationSpace` | `.PoseSpace` | `.TrajectorySpace` | `.ArticulationSpace` | `Engine/SimulationSpace/` + `Components/Motion/` |
| **Non-Blocking Sensor Volumes** | 🔴 No | **`.TriggerZone`** | `.SensorVolume` | `.ProximityZone` | `.DetectionVolume` | `.BoundarySensor` | `.ActivationZone` | `Engine/SimulationSpace/PhysicsSpace/` |
| **Decoupled Signal Routing** | 🔴 No | **`.EventBus`** | `.SignalBus` | `.MessageBus` | `.SignalChannel` | `.BroadcastBus` | `.SignalRouting` | `Engine/Input/` + `Lifecycle/` |
| **Global Switches & Counters** | 🔴 No | **`.VariableRegister`** | `.GameCounters` | `.GlobalRegisters` | `.ParameterLedger` | `.SessionVariables` | `.RuleRegisters` | `Engine/Configuration/` + `Lifecycle/` |
| **Cooldowns & Time Dilation** | 🔴 No | **`.TimerProfile`** | `.ClockProfile` | `.CooldownPreset` | `.TimeScalePreset` | `.IntervalProfile` | `.ChronoPreset` | `Engine/Constants/TimeConstants.h` |
| **1D/2D Easing & Falloff Curves** | 🔴 No | **`.Curve`** | `.ResponseCurve` | `.EasingProfile` | `.SplineCurve` | `.ScalarCurve` | `.FalloffCurve` | `Engine/Components/Controls/ScalarCurveEditor` + `PolynomialCurves` |
| **Multi-Track Keyframe Sequencer** | 🔴 No | **`.Timeline`** | `.TrackSequence` | `.KeyframeSequence` | `.CutSequence` | `.CueTimeline` | `.TrackSchedule` | `Engine/Components/Controls/SimulationTimelineFooter` |

---

### Category 15 — Runtime Interface, Tooltips, Input & Multiplayer Networking

| Slot | `*Space`? | 🏆 🥇 Primary | 🥈 Alt 2 | 🥉 Alt 3 | 🏅 Alt 4 | 🎖️ Alt 5 | Alt 6 | Frontier Owner |
| :--- | :---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Runtime Screens & HUDs** | 🟢 Yes | **`.InterfaceSpace`** | `.Interface` *(non-Space)* | `.HudLayout` | `.ScreenSpace` | `.ViewportInterface` | `.OverlaySpace` | `Engine/Components/` (`PixelSpace`, `Panes/`, `Controls/`) |
| **Hover Descriptions** | 🔴 No | **`.Tooltip`** | `.HoverOverlay` | `.CalloutTooltip` | `.InspectionHint` | `.HoverReadout` | `.DetailCallout` | `Engine/Components/Controls/ControlTooltip` |
| **Hardware Input & Chords** | 🟢 Yes | **`.InputSpace`** | `.PeripheralSpace` | `.ChordLayout` | `.ControlBindings` | `.IntakeSpace` | `.GestureSpace` | `Engine/Input/` + `Configuration/InputSettings` |
| **Multiplayer Replication** | 🟢 Yes | **`.NetworkSpace`** | `.ReplicationSpace` | `.TransportSpace` | `.EndpointSpace` | `.SessionSpace` | `.SyncSpace` | `Engine/NetworkingSpace/` |

---

## 5. Multi-Pillar Embedded Container Architecture (Concrete Blueprints)

Frontier's **Sectioned Document Container (`DocumentContainerCodec`)** natively supports **both standalone files and single-file multi-pillar embedding** with zero format fragmentation. Every container begins with a human-readable UTF-8 PascalCase TOML header (fast header-only open without reading Tier 2 payloads), followed by an SFNT/TTF-inspired `64 B`-aligned binary section directory (`[['.SectionDirectory']]`) verified in parallel across `ExecutionChannels`, zero-copy OS memory mapping (`MemoryMappedFileView`), and storage-aware slice streaming via `AssetStreamThread`.

### 5.1 Embedded Multi-Pillar Blueprints

| Container Example | Directly Embedded Sub-Sections (Zero Extra Seeks) | Optional External Shared Links (Zero Duplication) |
| :--- | :--- | :--- |
| **`VehicleDefault.Instance`** | `.GeometrySpace` *(Body + 4 Rims + 4 Tyres `PolygonNetwork`, creased normals, `ClusterPyramid`)* · `.SurfaceParameter` *(`.UVTile` UDIMs)* · `.PixelSpace` *(dashboard icon sheet & decals)* · `.ImageSpace` *(optical lens flare / light sheets)* · `.TextureSpace` *(packed BC7/ASTC UDIM mipchains)* · `.MaterialSpace` *(11 OpenPBR 1.1 coatings)* · `.PhysicsSpace` *(`.ConvexHull` + `.ConstraintSet` suspension + `.FractureNetwork` + `.ImpactProfile`)* · `.ForceField` *(aerodynamic wake & downforce)* · `.VolumetricSpace` *(exhaust density plume)* · `.AudioSpace` *(`.Soundscape` RPM stems)* · `.BehaviorSpace` *(drivetrain, headlamp/taillamp & damage logic)* | Shared `.MaterialSpace` library coatings, shared `.ImpactProfile` asphalt/rubber lookup, shared `.IesProfile` headlamp beams |
| **`ExhaustPlume.ParticleSpace`** | `.SpawnSource` *(rate, burst, lifetime)* · `.ForceField` *(curl/vortex velocity grid)* · `.VolumetricSpace` *(smoke optical density)* · `.ColourSpace` *(temperature blackbody ramp)* · `.Curve` *(size/opacity/drag over lifetime)* · `.RibbonTrail` *(optional streak)* · `.SwarmBehavior` *(optional turbulence flocking)* | Shared `.MaterialSpace` & `.TextureSpace` flipbook channels |
| **`VehicleClearSkyDefault.WorldSpace`** | `['.ContainmentIndex']` *(`EnclosingIndex`, `FirstNestedIndex`, `NextLateralIndex`, `DepthLevel`)* · `[['.WorldPlacement']]` transforms · `.Atmosphere` *(3-LUT sky, sun, clouds, fog, moons, stars)* · `.CameraOptics` *(full physical lens & sensor)* · `.PostStack` *(ACES tone stack & 3D LUT)* · `.TriggerZone` · `.EventBus` · `.Timeline` · `.BehaviorSpace` | External `.Instance` (`VehicleDefault.Instance`), `.GeometrySpace` (`GroundPlane.GeometrySpace`), `.IesProfile`, `.Soundscape` |
| **`HudOverlay.InterfaceSpace`** | `['.ContainmentIndex']` *(anchors, z-order, responsive bounds)* · `.PixelSpace` *(packed UI icon sheets)* · `.VectorSpace` *(SVG/Bézier icons & scalable shapes)* · `.ColourSpace` *(theme swatches & ramps)* · `.Curve` *(spring & transition curves)* · `.Tooltip` *(hover descriptions)* | Shared `.FontSpace` (`FontRepository.FontSpace` baked once from `.ttf`/`.otf`) |

### 5.2 Concrete Wire Layout Example (`VehicleDefault.Instance`)

```toml
# Frontier Sectioned Document Container (Canonical PascalCase TOML Header)
['.DocumentHeader']
SchemaVersion        = 1
ContainerSuffix      = '.Instance'
StableIdentifier     = '0193b5c0-7a1e-7000-8000-000000000001'
DocumentTitle        = 'VehicleDefault'
AuthoringWorkspace   = 'Frontier.ComponentWorkspace'
BinaryAlignmentBytes = 64

# Embedded Pillars Carried Directly Inside This File (Dispatched by EmbeddedSectionRouter)
[['.EmbeddedPillar']]
PillarSuffix      = '.GeometrySpace'
SectionIdentifier = 'ControlVehicleGeometry'
EmbeddedFormat    = 'Tier2Binary'

[['.EmbeddedPillar']]
PillarSuffix      = '.PixelSpace'
SectionIdentifier = 'DashboardIconSheet'
EmbeddedFormat    = 'Tier2Binary'

[['.EmbeddedPillar']]
PillarSuffix      = '.TextureSpace'
SectionIdentifier = 'VehicleUdimMipChain'
EmbeddedFormat    = 'Tier2Binary'

[['.EmbeddedPillar']]
PillarSuffix      = '.MaterialSpace'
SectionIdentifier = 'ElevenSlotOpenPbr'
EmbeddedFormat    = 'TomlWithBinarySection'

[['.EmbeddedPillar']]
PillarSuffix      = '.ForceField'
SectionIdentifier = 'AerodynamicWakeField'
EmbeddedFormat    = 'Tier2Binary'

[['.EmbeddedPillar']]
PillarSuffix      = '.VolumetricSpace'
SectionIdentifier = 'ExhaustPlumeVolume'
EmbeddedFormat    = 'TomlWithBinarySection'

[['.EmbeddedPillar']]
PillarSuffix      = '.BehaviorSpace'
SectionIdentifier = 'VehicleDrivetrainLogic'
EmbeddedFormat    = 'TomlWithBinarySection'

# Shared External Files Linked Without Duplication (Resolved by ExternalLinkResolver)
[['.ExternalLink']]
LinkSlot          = 'HeadlampPhotometry'
TargetSuffix      = '.IesProfile'
RelativePath      = 'Content/Photometry/MeasuredLuminaires/LowBeamHeadlamp.IesProfile'
StableIdentifier  = '0193b5c0-7a1e-7000-8000-000000000042'
ExpectedSha256    = 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'

# SFNT/TTF-Style 64B-Aligned Binary Section Directory (O(1) Random-Access Jumps)
[['.SectionDirectory']]
SectionIdentifier     = 'ControlVehicleGeometry'
SubFormatTag          = '.GeometrySpace'
RelativeAlignedOffset = 0
ByteLength            = 3662656
ChecksumFnv1a64       = '0x8F3A1904C271D05B'

[['.SectionDirectory']]
SectionIdentifier     = 'AerodynamicWakeField'
SubFormatTag          = '.ForceField'
RelativeAlignedOffset = 3662656
ByteLength            = 262144
ChecksumFnv1a64       = '0x51A0C8127E4903D1'

# === FRONTIER_BINARY_BOUNDARY ===
# <0..63 zero-padding bytes to exact 64-byte alignment, followed by packed binary sections + optional .Revision>
```

### 5.3 Hardened Wire & Revision Rules

1. **Genuine `.ttf`/`.otf` vs. Container Framing**: `[['.SectionDirectory']]` borrows TrueType/SFNT random-access offset directories for O(1) section seeks and `mmap` loading — it does not claim a `.WorldSpace` is a font file. True `.ttf`/`.otf` fonts enter via `Engine/Interchange/FontInterchange` and bake once into `.FontSpace` (`FontMetrics`, kerning, MSDF/SDF + contours) so boot never reparses fonts.
2. **Integrity & Security (`ContainerIntegrityValidator`)**: Reject overflowing offsets, unaligned offsets (`Offset % 64 != 0`), overlapping byte spans, truncated streams, `..` path escapes, and `FNV-1a 64` mismatches. Preserve unknown forward-compatible TOML keys and binary sections on rewrite or refuse destructive save cleanly.
3. **Parametric `.Revision` Budgets (`215 MiB` / `750 MiB`)**: Retained unchanged in `Engine/Workspaces/Revision/` — parametric operations only, `215 MiB` per document, `750 MiB` global session cap, `Redo Purge → Prefix Fold` compaction, strictly isolated from `Build/CompiledRepository/`.

---

## 6. Persistent Zero-Recompute `DependencyOrder` & `Build/CompiledRepository/`

```text
  [Authored Inputs: .WorldSpace / .Instance / .Atmosphere / .ShaderNetwork / .ttf / .ies / .svg]
                                               │
                                               ▼  ① Fast Scan: xxHash64(source bytes + upstream hashes)
                                  [DependencyOrderLedger Check]
                                   │                         │
                  (xxHash64 + SHA-256                        │ (hash mismatch, missing binary,
                   + InputSignature match)                   │  or failed FNV-1a 64 integrity)
                                   │                         │
                                   ▼                         ▼
            [Zero-Copy mmap Load from                 ② Dirty-Only Topological Bake on ExecutionChannels
             Build/CompiledRepository/                  (.ttf → .FontSpace MSDF/SDF + contour pages
             <Category>/<Sha256>.fbin]                   .svg → .VectorSpace flattened GPU contours
                                                         .slang → SPIR-V .spv via slangc
                                                         .Atmosphere → 256×64 / 32×32 / 192×108 3-LUT
                                                         .IesProfile → 128×32 candela LUT
                                                         .FractureNetwork → Voronoi binary cells
                                                         .RayStructure → device-keyed BVH4 / BLAS-TLAS)
                                                             │
                                                             ▼  ③ AtomicFileWriter (temp → fsync → rename)
                                              [Publish Build/CompiledRepository/<Category>/<Key>.fbin
                                               + Update DependencyOrderLedger + InputSignature Stamp]
```

| Hash / Stamp | Mechanical Purpose | Exact Inputs Included |
| :--- | :--- | :--- |
| **`xxHash64`** | Sub-millisecond startup & live-edit dirty scan | Raw file bytes + sorted upstream `xxHash64` stamps |
| **`SHA-256`** | Collision-resistant repository filename key | Canonical TOML + upstream `SHA-256` keys + `SchemaVersion` + compiler/solver version + physical units + colour primaries + target ABI *(+ Vulkan API & driver UUID for `.RayStructure` / `.spv`)* |
| **`FNV-1a 64`** | Accidental corruption detection on read | Every `64 B`-aligned Tier 2 binary section inside `.Space` / `.Instance` / `.fbin` |

### Fine-Grained Invalidation & Safety Guarantees

- **Minimal Recomputation**: Adjusting solar intensity in `.WorldSpace` updates only the lighting uniform — zero rebuild of `.GeometrySpace`, `.FontSpace`, `.TextureSpace`, or `.Atmosphere` LUTs. Editing Rayleigh/Mie coefficients in `.Atmosphere` rebakes **only** the sky 3-LUT and dependent aerial-perspective lookup. Editing an embedded pillar inside `VehicleDefault.Instance` invalidates **only** that section's `InputSignature`, not unrelated embedded sections.
- **Cycle Rejection**: `DependencyOrderLedger` verifies strict acyclicity across build dependencies and reports the full cycle chain on error. Runtime `.EventBus` messaging, world `ContainmentIndex`, and temporal render feedback remain separate non-build structures.
- **Crash-Safe Parallel Publication**: `BakeBuildScheduler` cancels superseded in-flight bakes when a newer edit arrives and publishes via `AtomicFileWriter` only when the `GenerationStamp` still matches.
- **Disk Quota & Git Exclusion**: `Build/CompiledRepository/` is git-ignored and reachability-pruned above a configurable disk ceiling — never deletes authored `Content/` files or `.Revision` history.

---

## 7. Eliminating Hardcoded Showcase Scenes (`EditorLifecycle` & `ShowcaseGalleryScene`)

| Audited Source Location | Observed Violation | Target Replacement |
| :--- | :--- | :--- |
| `Engine/Lifecycle/EditorLifecycle.cpp:333-366` | `PopulateWorldSceneSession()` selects hardcoded C++ scene builders (`BuildApocalypticCityRacingScene`, `BuildBlankPlaneSkyScene`, `BuildVehicleClearSkyDefaultScene`). | Boot chain reads `.EngineSettings` + `.RuntimeConfiguration` + `.PlatformSettings` + `.ProjectIndex` → loads `StartupWorldPath` via `WorldSpaceCodec` + `DependencyOrder`. |
| `Engine/Lifecycle/EditorLifecycle.cpp:368-385` | Overwrites loaded `.WorldSpace` atmosphere fields from `ViewportPanel` display defaults and wrongly binds `SolarEnabled = CelestialStarsEnabled`. | Isolate viewport debug toggles inside Editor-only `.WorkspaceLayout`; decouple `SolarEnabled` from `StarfieldEnabled`; never overwrite authored `.WorldSpace` parameters on load. |
| `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.cpp:987-1032` | `BuildVehicleClearSkyDefaultScene()` silently manufactures hardcoded C++ geometry/materials if `.WorldSpace` is missing or fails decode. | Surface an actionable diagnostic error on missing/corrupt `.WorldSpace` (**zero silent hardcoded fallback**). Restrict `FrontierDefaultContentGenerator` strictly to explicit offline CLI invocation. |
| `Content/Worlds/VehicleClearSkyDefault.WorldSpace` | Header comments & legacy containment section name still use blacklisted labels and link legacy suffixes. | Upgrade encoder to `['.ContainmentIndex']` (`EnclosingIndex`, `FirstNestedIndex`, `NextLateralIndex`, `DepthLevel`) and approved suffixes while keeping backward-compatible decode during migration. |

### First Authoring Deliverable — Sun, Clouds, Moon, Stars & Atmosphere

1. **HTML Reference Gate (`CLAUDE.md` §2.1)**: Capture live browser `.png` screenshots of `Experimental/ProjectZeroEditor` Construct & Outliner right-click sheets (normal, hover, disabled, edge-clamped) before writing native UI code.
2. **Authoritative Transactions**: Constructing Sun, Clouds, Moon, Stars, or Atmosphere executes one undoable transaction creating/reusing a single `Environment` folder by `StableIdentifier`, wiring the real `Engine/GeometricRaster/Atmosphere/` runtime unit, updating `OutlinerPanel` + `InspectorPanel`, and writing atomically to `.WorldSpace` + `.Atmosphere` / `.VolumetricSpace`.
3. **Safe Removal & GPU Sync**: Removing any environment instance unregisters its renderer contribution, synchronizes pending Vulkan work, updates selection cleanly without disturbing lateral environment rows, and leaves shared files untouched.

---

## 8. Phased Implementation Sequence & Verification Gates

| Phase | Deliverable | Mandatory Exit Gate |
| :---: | :--- | :--- |
| **Phase 0** | Lock naming choices from §§2–4; capture `Experimental/ProjectZeroEditor` Construct & Outliner right-click `.png` references; audit all `Engine/Codecs/` callers & legacy `Content/` files. | Reference `.png` captures inspected; full caller inventory verified; zero banned words in all new docs/headers. |
| **Phase 1** | Stand up `Engine/Serialization/{Container,Embedding,Migration}/`: migrate `SpaceContainerCodec`, implement `SuffixRegistry`, `EmbeddedSectionRouter`, `ExternalLinkResolver`, `InstanceContainerCodec`, `AtomicFileWriter` & `ContainerIntegrityValidator`. | Round-trip, embedded multi-pillar `.Instance`, case-insensitivity, bounds/overflow/overlap/truncation & `FNV-1a 64` corruption checks succeed in `SerializationContainerValidation`. |
| **Phase 2** | Implement `Engine/Serialization/DependencyOrder/` (`DependencyOrderLedger`, `ContentHashCalculator`, `CompiledDiskRepository`, `BakeBuildScheduler`, `SourceTraceInspector`) + vertical bakes (`.ttf` → `.FontSpace`, `.Atmosphere` 3-LUT, `.IesProfile` `128×32`). | Cold-boot disk hit (`0 ms` rebake), minimal invalidation on single parameter edit, cycle detection, cancellation & crash-safe atomic rename checks succeed. |
| **Phase 3** | Remove hardcoded showcase fallbacks in `EditorLifecycle.cpp` & `ShowcaseGalleryScene.cpp`; wire `.EngineSettings` + `.RuntimeConfiguration` + `.PlatformSettings` + `.ProjectIndex` → `.WorldSpace` + `.GraphicsSettings` + `.UserProfile` + `.WorkspaceLayout`; land `Environment` construct/remove/undo/save. | Missing/corrupt `.WorldSpace` reports explicit error (no hardcoded fallback); save/reopen preserves IDs, `ContainmentIndex` & visibility; side-by-side `.png` captures verify Sun/Clouds/Moon/Stars/Atmosphere parity. |
| **Phase 4** | Migrate existing `Engine/Codecs/` into `Engine/Serialization/Codecs/` and implement remaining active codecs (`PixelSpaceCodec`, `CameraOpticsCodec`, `AtmosphereCodec`, `FractureNetworkCodec`, `BehaviorSpaceCodec`, `InterfaceSpaceCodec`, `SoundscapeCodec`, `PostStackCodec`, `RayStructureCodec`, `DistanceFieldCodec`, `RadianceClipmapCodec`, `IrradianceCodec`, `SpectralCurveCodec`, `NoiseNetworkCodec`, `CurveCodec`, `TimelineCodec`, `TriggerZoneCodec`, `EventBusCodec`, `VariableRegisterCodec`, `TimerProfileCodec`, `TooltipCodec`). | Every active codec succeeds on round-trip, embedded-vs-linked loading, `DependencyOrder` hit/miss & real engine consumer checks; `BKL-013` redo schemas (`ParticleSpace`, `ForceField`, `FluidGrid`, `SpawnSource`, `RibbonTrail`, `SwarmBehavior`) stay reserved with zero simplified stubs. |
| **Phase 5** | Enforce `Editor` / `Development` / `Shipping` isolation & Windows / Linux / Android / Headless compilation; build & run `Frontier`, `FrontierSketcher`, and serialization validations; update `Documentation/FolderStructure.md`. | `Shipping` links zero `WorkspaceLayoutCodec` / `Engine/Editor/` symbols; `Frontier` + `FrontierSketcher` + CTest validations green; every milestone pushed to active branch. |

---

## 9. Decisions to Lock Before Phase 1

1. **Foundational Mechanism Names (§2)**: Confirm **`DependencyOrder`** + **`InputSignature`** + **`BuildSpecification`** + **`ContainmentIndex`** / **`NextLateralIndex`** (or pick any ranked alternative from §§2.1–2.4).
2. **`Engine/Serialization/` Layout (§3)**: Confirm **Option 1 (Pillar-Aligned Functional Layout)** or select Options 2–5.
3. **Key Category Picks (§4)**: Confirm **`.ForceField`** (with embedded `.VectorField` grid sub-sections) vs. standalone **`.VectorField`** (§4 Cat. 10), **`.BehaviorSpace`** vs. **`.LogicSpace`** (§4 Cat. 13), and **`.InterfaceSpace`** vs. **`.Interface`** (§4 Cat. 15).
4. **Empty `Environment` Folder Retention**: Proposed default — keep an empty `Environment` folder until explicitly deleted via an undoable confirmation command.
