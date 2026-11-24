# 🌊 Unified ForceFields, Particle Physics, XPBD & Fluid 1:1 Native Port Plan

> **TL;DR**: Authoritative, zero-divergence 1:1 native C++20 + Vulkan 1.2 `.slang` porting blueprint converting the unified HTML/WebGL2/WebGPU studio (`SultanAladin/Frontier-1@arena/fa8c2344-frontier-1` → `Experimental/FluidEditor/` + `Experimental/ClothEditor/` XPBD kernel) into Frontier's native simulation engine. Built strictly in upstream dependency order: **Phase 0 (HTML PNG Capture Baseline)** → **Phase 1 (ForceFields Backbone)** → **Phase 2 (GPU Particle Simulation, GPU Collisions & GPU XPBD Constraints)** → **Phase 3 (3D Eulerian + Sparse Infinite Fluid Solver & Multiple-Scattering Volume Renderer)** → **Phase 4 (1:1 HTML-Parity Workspace UI via `Engine/Components/`)** → **Phase 5 (Two-Way JoltPhysics Coupling, Serialization & Visual Verification Exhibits)**.

**Date**: `2026-11-23` · **Status**: `🚩 Ready for Execution` · **Branch**: `arena/6e2e878f-frontier`  
**Authoritative References**:
- `SultanAladin/Frontier-1@arena/fa8c2344-frontier-1` (`ab94d2e9`) → `Experimental/FluidEditor/` (`46,086` LOC across `30` JS/HTML/MD files, `28` TOML layer specs, `20` analytic force models, `54` scene presets, `71` particle templates, `19` procedural 3D carrier solids, `18` analytical sprite cutouts)
- `SultanAladin/Frontier-1@arena/fa8c2344-frontier-1` (`ab94d2e9`) → `Experimental/ClothEditor/Engine/{ClothSolvers.js,WebGpuClothAccelerator.js}` (conflict-colored persistent-dual Orthotropic XPBD compute formulation)
- `CLAUDE.md` (§1.2, §2.0–§2.9), `Documentation/Backlog.md` (`BKL-013`), `Documentation/BannedWordsVocabulary.md`, `Documentation/FolderStructure.md`

---

## 🎯 1. Post-Mortem Guardrails (`daed305` / `BKL-013`)

Five prior C++ workspaces (`Fluid`, `ForceField`, `Particle`, and two coupled studios) were removed in commit `daed305` (`BKL-013`) because earlier implementations diverged from the HTML reference. Every phase of this plan is governed by hard non-negotiable guardrails that make those failure modes structurally impossible:

| Prior `daed305` Failure Mode | Structural Guardrail Enforced in This Plan |
| :--- | :--- |
| **Substituted simplified approximations** (e.g., flat billboard point sprites instead of full strand/ribbon/object/derez/volume solvers) | **100% algorithmic 1:1 port**: every one of the `20` force models, `28` TOML layer categories (`101–109`, `201–219`), `16` spawn shapes, `19` 3D carrier solids, `18` sprite cutouts, `11` strand modes, `10` firework patterns, `3` fluid grid topologies, and Wrenninge 3-octave volume raymarcher is ported verbatim with identical numeric codes and math. |
| **Reinvented non-matching UI** instead of mirroring HTML layout & controls | **Mandatory Step 0 PNG capture gate (`CLAUDE.md` §2.1)** before writing C++ UI; all panels built strictly from Frontier's existing `Engine/Components/{Panes,Controls,Diagnostics}/` mirroring `FluidEditor` `index.html`, `StudioPanels.js`, and `TimelinePanel.js` 1:1. |
| **Broken shared component couplings & CPU-drawn viewport fallbacks** | **100% GPU `.slang` viewport overlays (`CLAUDE.md` §2.2)** for all gizmos, velocity vectors, force hulls, slices, ribbons, and strands; read-only wait-free `PublishedReadingExchange` between simulation, render, and `PhysicsThread` — zero mutation of shared engine components. |
| **Additive features breaking preset fidelity** | **Strict separation between 1:1 parity and Frontier GPU extensions**: GPU XPBD constraints (`Code 220`) and GPU spatial-hash / `RigidColliderProxy` particle collisions (`Code 221`) are additive opt-in layers; all `54` HTML presets and `71` particle templates reproduce pixel-for-pixel when those layers are absent. |
| **Banned vocabulary & naming drift** | **Automated naming compliance table (§3)**: every HTML term containing a Frontier banned word (`Emitter`, `Flow`, `Vortex`, `Domain`, `Mesh`, `Palette`, `Pool`, `Catalog`, `Studio`, `Pass`, `Stage`, `Snapshot`, `And`) has an exact locked C++20 / `.slang` identifier mapping while keeping TOML serialization keys and UI display strings 100% identical to the HTML reference. |

---

## 🔒 1B. Mechanical Anti-Invention Protocol (6 Hard Locks)

Promises do not stop agent drift on a `52,155`-line multi-domain port; **mechanical gates do**. Every single turn of execution MUST obey all 6 hard locks:

| Lock | Rule | Enforcement Mechanism |
| :---: | :--- | :--- |
| **L1** | **Strict 1-Slice-Per-Turn Cap** | Implement **at most 1 numbered slice (`S01`–`S28`) per turn**. Read the exact reference source line range with `read_file` in the same turn immediately before writing a single line of C++ or `.slang`. Never port a whole subsystem from memory. |
| **L2** | **Reference-Generated Oracles (Zero Self-Written Math Tests)** | Every math, stack, keytrack, and shader slice is verified against **golden JSON/binary/PNG outputs dumped directly from the authoritative JS/HTML repo** (`Node.js` / headless Chromium in `S01`) — never against synthetic assertions written by the agent from its own C++ code. |
| **L3** | **Hard Wall: 100% 1:1 HTML Parity BEFORE Additive GPU Physics** | Slices `S01`–`S23` lock exact 1:1 HTML parity first (ForceFields → 1:1 Particles → 1:1 Fluid → 1:1 UI). Additive Frontier GPU Physics (`S24`–`S27`: Jolt `RigidColliderProxy` two-way collisions, GPU spatial-hash self-collision, conflict-colored GPU Orthotropic XPBD from `WebGpuClothAccelerator.js`) live strictly in additive layers (`220`/`221`) and **cannot begin until `S01`–`S23` are green**. |
| **L4** | **Exact Line-Range Traceability Header** | Every ported `.h`, `.cpp`, and `.slang` file begins with a comment citing its exact upstream file and line range (e.g., `// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L294-L493`) and mirrors every branch, clamp, and magic constant verbatim. |
| **L5** | **Stop-On-Gap Rule (Zero Silent Approximation)** | If any slice hits a WebGL2→Vulkan discrepancy, missing dependency, ambiguous branch, or turn-size limit, **STOP immediately** and report the exact file + line range to the user. Quietly swapping in a simpler substitute to "make it compile" is a hard violation. |
| **L6** | **4-Point Slice Exit Gate** | A slice cannot be marked complete until all four pass: **(1)** branch/constant parity audit against the exact reference lines, **(2)** reference oracle diff passes (`max abs err < 1e-4` float / exact int / PNG match), **(3)** `BannedWordsVocabulary.md` + zero-`And` grep returns `0` hits, **(4)** compiled, tested (`CTest`), committed & pushed. |

---

## 🏗️ 2. Dependency Order & System Architecture

```text
Phase 0: HTML Reference Visual Capture (Playwright / Headless Chromium PNGs + Metrics)
   │
   ▼
Phase 1: ForceFields Backbone  ◄── FIRST! Zero upstream simulation dependencies
   │  • 20 Analytic ForceModels (codes 0..19) + 7 Falloff Curves (codes 0..6)
   │  • Shared CPU SIMD + Vulkan .slang GPU ForceModelEvaluation (bit-identical)
   │  • KeyTracks (Fritsch-Carlson Hermite / Bezier / Cycle / PingPong)
   │  • GPU Force Vector / Gizmo Rasterization + JoltPhysics ForceField Coupling
   │
   ├──────────────────────────────────────────┐
   ▼                                          ▼
Phase 2: GPU Particle Simulation +          Phase 3: 3D Eulerian + Sparse Infinite
         GPU Collisions + GPU XPBD                   Fluid Solver & Volume Renderer
   │  • 19 Particle Layers (201..219)          │  • 9 Gas Layers (101..109)
   │  • 16 Shapes, 19 3D Solids, 18 Cutouts    │  • Fixed / 5³ WorldGrid / Sparse Infinite
   │  • 64-Sample Catmull-Rom Ribbons          │  • RK2 + Selle 2008 MacCormack Advection
   │  • Derez / Mocap / Effectors / Boids      │  • Warm-Started Jacobi + Fire-Light Diffusion
   │  • 11 Strands/Webs/Arcs + 10 Fireworks    │  • Wrenninge 3-Octave Multiple-Scattering Raymarch
   │  • + GPU RigidColliderProxy Collisions    │  • 8,192 Stream Tracers + Debris + Flipbook Baker
   │  • + GPU Spatial-Hash Self-Collisions     │  • 192 MiB GPU Checkpoint Ring
   │  • + GPU Orthotropic XPBD Constraints     │  • Heat Shimmer / Glitch / Distortion Overlay
   │                                           │
   └─────────────────────┬─────────────────────┘
                         ▼
Phase 4: Unified Workspace UI (1:1 HTML Parity via Engine/Components/)
   • OutlinerPanel + ViewportPanel + InspectorPanel + SimulationTimelineFooter
   • Paged Lettered Tabs, Layer Stack Editor, Asset/Preset/Template Drawer, F3 Telemetry
                         │
                         ▼
Phase 5: Serialization, Cross-Thread Coupling & Headless Visual Verification Exhibits
   • .ForceField / .FluidGrid / .ParticleSpace / .VolumetricSpace TOML + Binary Codecs
   • 4 Headless Validation Executables + Side-by-Side HTML vs. Native PNG Audits
```

---

## 📛 3. Mandatory Naming Compliance Translation (`BannedWordsVocabulary.md` & `CLAUDE.md` §2.8)

Frontier enforces strict naming rules across all C++20 identifiers, `.slang` shader identifiers, and filenames: zero `And` conjunctions, `Model` permitted **exclusively** for ForceFields (`ForceModel`, `TurbulenceModel`, `StreamTransportModel`, `VectorFieldModel`), and banned terms (`Law`, `Emitter`/`Emit`, `Flow`, `Vortex`, `Domain`, `Mesh`, `Palette`, `Pool`, `Catalog`, `Studio`, `Pass`, `Stage`, `Pipeline`, `Cache`, `Store`, `Snapshot`, `Item`, `Card`, `Element`, `Entity`, `Asset`, `System`) strictly barred from code identifiers.

> **Important**: Document keys inside Frontier's Space Content files (`EngineContent/Simulation/LayerSpecifications.VolumetricSpace`, `Presets/*.FluidGrid`, `ParticleTemplates/*.ParticleSpace` — using Frontier's registered Data-Driven Space Content suffixes in `Engine/Serialization/Container/SuffixRegistry.{h,cpp}`, never `.toml`) and user-facing UI labels (e.g., `"Vortex Column"`, `"Gas Domain"`) remain **100% verbatim** to maintain wire compatibility with all `54` presets and `71` templates. Only C++20 / `.slang` code identifiers and filenames use the compliant Frontier names below:

| HTML Reference Term | C++20 / `.slang` Compliant Identifier | UI / TOML Parity Note |
| :--- | :--- | :--- |
| `ForceGeneratorCatalog` / `Force` | `ForceModelRegistry` / `ForceModelNode` / `ForceModelOrdinal` | Sole approved `Model` exemption (`BannedWordsVocabulary.md` §3.1) |
| `VortexColumn` / `VortexRing` | `ForceModelOrdinal::RotationalColumn` (`3`), `RotationalRing` (`9`) | UI displays `"Vortex Column"`, `"Vortex Ring"` |
| `FlowStream` / `FlowTracers` | `ForceModelOrdinal::DirectedStream` (`16`), `StreamTracerSimulation` | UI displays `"Flow Stream"`, `"Flow Tracers"` |
| `Domain` (`GasVolume` domain) | `FluidRegion` / `FluidRegionLayer` (`Code 101`) | UI displays `"Gas Domain"`; TOML token `"domain"` |
| `Emitter` / `Emit` | `ReleaseSource` / `SpawnSource` / `ParticleSpawnMode::ContinuousRelease` | UI displays `"Emitter"` / `"Emit"`; TOML token `"source"` |
| `ParticleSystem` / `ParticleCatalog` | `ParticleCloudNode` / `ParticleTemplateRegistry` | Never use `System` or `Catalog` |
| `ObjectMeshCatalog` / `Mesh` | `CarrierSolidRegistry` / `CarrierPolygonNetwork` | Uses Frontier `PolygonNetwork` convention |
| `Palette` / `PalettePresets` | `ColourSequence` / `ColourSequencePreset` | Maps directly to `Engine/Components/Controls/ColourRampEditor` |
| `TilePool` (Sparse Infinite Grid) | `SparseTileBank` / `SparseTileAllocator` | Never use `Pool` |
| `LayerStack` / `LayerSpecifications` | `EffectLayerStack` / `EffectLayerSpecification` | Generated directly from `LayerSpecifications.toml` |
| `CheckpointRing` / `Snapshot` | `SimulationCheckpointRing` / `PublishedFluidReading` | Never use `Snapshot` |
| `RenderPass` / `ComputePass` | `*Compute.slang` / `*Rasterization.slang` / `*Step` | Follows `Engine/Shaders/` naming convention |
| `FluidStudio` / `StudioPanels` | `FluidWorkspace` / `FluidWorkspaceLayout` | Never use `Studio` in C++ identifiers |

---

## 📂 4. Canonical Frontier File Layout (`FolderStructure.md` Compliant)

Every new C++ module, shader, codec, content template, and validation exhibit fits cleanly into Frontier's existing directory structure while reusing existing engine modules (`CLAUDE.md` §2.9):

```text
Engine/
├── SimulationSpace/
│   ├── ForceFields/                                 # PHASE 1 (Shared Upstream Backbone)
│   │   ├── ForceModelTypes.h                        # ForceModelOrdinal (0..19), ForceFalloffOrdinal (0..6), 64B ForceModelGpuRecord
│   │   ├── ForceModelRegistry.{h,cpp}               # 20 descriptors, defaults, ranges, TOML keys (1:1 ForceGeneratorCatalog.js)
│   │   ├── ForceFieldSampler.{h,cpp}                # Bit-identical CPU SIMD evaluation matching ForceModelEvaluation.slang
│   │   ├── ForceFieldRegister.{h,cpp}               # Multi-generator scene register + PublishedForceFieldReading exchange
│   │   ├── ForceFieldGpuUploader.{h,cpp}            # Vulkan SSBO packing (up to 256 active generators) + timing weights
│   │   └── KeyTrackSampler.{h,cpp}                  # 1:1 KeyTracks.js (Fritsch-Carlson Hermite, Bezier, Hold/Cycle/PingPong)
│   │
│   ├── EffectLayers/                                # SHARED TOML LAYER ENGINE (Phase 1 -> 2 & 3)
│   │   ├── EffectLayerSpecification.{h,cpp}         # Auto-parsed / generated from LayerSpecifications.toml (Codes 101..109, 201..221)
│   │   ├── EffectLayerStack.{h,cpp}                 # 1:1 LayerStack.js phase evaluation, blend weights, halt diagnostics
│   │   └── SimulationCheckpointRing.{h,cpp}         # 1:1 SimulationCheckpoints.js (192 MiB GPU ring, 64 checkpoints)
│   │
│   ├── Fluid/                                       # PHASE 3 (3D Eulerian + Sparse Infinite Gas/Pyro)
│   │   ├── FluidRegionTypes.h                       # Fixed / WorldGrid (up to 5³) / Sparse Infinite grid descriptors
│   │   ├── SparseTileBank.{h,cpp}                   # 64×16×64 R16UI tile lookup table + GPU occupancy allocation/eviction
│   │   ├── FluidGpuSolver.{h,cpp}                   # Vulkan compute encoder: Advect/MacCormack/Inject/Forces/Curl/Vorticity/Thermo/Jacobi/Gradient
│   │   ├── FluidVolumeRenderer.{h,cpp}              # Wrenninge 3-octave raymarcher, slice, velocity/force vectors, tracers, debris, flipbook
│   │   ├── FluidPresetRegistry.{h,cpp}              # Loader for all 54 Presets/*.toml scenes
│   │   └── FluidSimulationPostOverlay.{h,cpp}       # Heat shimmer, glitch & lensing post-composite
│   │
│   └── PhysicsSpace/                                # EXISTING JoltPhysics v5.6.0 (Zero breakage!)
│       └── ForceFieldPhysicsCoupling.{h,cpp}        # Phase 1 & 5: applies PublishedForceFieldReading + particle impulse readback to Jolt bodies
│
├── ParticleSpace/                                   # PHASE 2 (Top-Level Pillar — TierA_SpacePillars)
│   ├── ParticleCloudTypes.h                         # All 16 shapes, 18 cutouts, 11 strands, 10 fireworks, Derez/Mocap/Effector/Swarm enums
│   ├── CarrierSolidRegistry.{h,cpp}                 # 19 built-in procedural 3D solids + .GeometrySpace/.obj/.stl import + surface anchor sampler
│   ├── FireworkPatternSampler.{h,cpp}               # 1:1 FireworkPatterns.js (ClassicBouquet..GrandFinale, 3D SkyText glyphs)
│   ├── ParticleTemplateRegistry.{h,cpp}             # Loader for all 71 ParticleTemplates/*.toml
│   ├── ParticleGpuSimulation.{h,cpp}                # Vulkan compute: Spawn/Update, Ribbon Trails, Spatial-Hash Collisions, XPBD Constraints
│   ├── ParticleGpuRenderer.{h,cpp}                  # Billboard cutouts, 3D instanced carriers, backing surfaces, Catmull-Rom ribbons, strands
│   └── ParticleXpbdNetworkBuilder.{h,cpp}           # Conflict-colored constraint batcher (Distance/Warp/Weft/Shear/Bend/Tether) -> GPU SSBO
│
├── Shaders/                                         # CLAUDE.md §2.3: 100% .slang ONLY
│   ├── ForceFields/
│   │   ├── ForceFieldNoise.slang                    # 1:1 NoiseChunk (Hash31, ValueNoise, Fbm, CurlNoise)
│   │   ├── ForceModelEvaluation.slang               # 1:1 ForceFieldChunk (all 20 force models + 7 falloffs + saturation helper)
│   │   └── ForceFieldOverlayRasterization.slang     # 100% GPU gizmos, field arrows, helices, containment cages, dipole loops
│   │
│   ├── ParticleSpace/
│   │   ├── ParticleShapeAnchor.slang                # 1:1 ParticleShapeChunk (16 spawn shapes + 4 Mocap clips + ObjectSurface)
│   │   ├── ParticleUpdateCompute.slang              # 1:1 ParticleUpdateFragment (Emit/Cloner, Derez, Effectors, Swarm, Forces, Gas)
│   │   ├── ParticleCollisionCompute.slang           # Analytic spheres + Jolt RigidColliderProxy SSBO + atomic rigid impulse feedback
│   │   ├── ParticleSpatialHashCompute.slang         # Uniform 3D cell sort + 27-neighbor elastic/friction particle-particle collision
│   │   ├── ParticleXpbdCompute.slang                # Conflict-colored persistent-dual Orthotropic XPBD (Distance/Strain/Bend/Contact)
│   │   ├── ParticleTrailUpdateCompute.slang         # 1:1 TrailUpdateFragment (ring buffer up to 96 history samples)
│   │   ├── ParticleSpriteRasterization.slang        # 1:1 ParticleRenderVertex + ParticleFragment (18 cutouts, DOF, glow, streaks)
│   │   ├── ParticleRibbonRasterization.slang        # 1:1 TrailVertex (Catmull-Rom subdivided ribbons)
│   │   ├── ParticleCarrierSolidRasterization.slang  # 1:1 ObjectRenderVertex + ObjectFragment + 3 backing surfaces
│   │   └── ParticleStrandRasterization.slang        # 1:1 StrandVertex + StrandFragment (11 strand/web/arc modes)
│   │
│   └── Fluid/
│       ├── FluidVolumeAccess.slang                  # 1:1 VolumeChunk + BoundaryChunk + ColliderChunk (Fixed, WorldGrid, Sparse Infinite)
│       ├── FluidAdvectCompute.slang                 # 1:1 AdvectFragment (RK2 forward/reverse)
│       ├── FluidMacCormackCompute.slang             # 1:1 MacCormackFragment (Selle 2008 overshoot fallback + Courant blend)
│       ├── FluidInjectCompute.slang                 # 1:1 InjectFragment (Sphere/Disc/Box sources + jitter + pulse)
│       ├── FluidForcesCompute.slang                 # 1:1 ForcesFragment (buoyancy + gravity + velocity-saturated ForceModelEvaluation)
│       ├── FluidCurlVorticityCompute.slang          # 1:1 CurlFragment + VorticityFragment
│       ├── FluidThermodynamicsCompute.slang         # 1:1 ThermodynamicsFragment (ignition 0.9, fuel burn, cooling, outflow sponge)
│       ├── FluidDivergencePressureCompute.slang     # 1:1 DivergenceFragment + PressureFragment (0.96 warm-start + 6.28 fire-light diffusion)
│       ├── FluidGradientShiftCompute.slang          # 1:1 GradientFragment + ShiftFragment + OccupancyFragment
│       ├── FluidTracerDebrisCompute.slang           # 1:1 TracerUpdateFragment + DebrisUpdateFragment
│       ├── FluidVolumeRaymarch.slang                # 1:1 SceneFragment (Wrenninge 3-octave, 2-lobe HG, geometric shadows, powder, ACES)
│       ├── FluidOverlayRasterization.slang          # Velocity vectors, slice plane, tracers, debris, region bounds gizmos
│       └── SimulationPostOverlay.slang              # Heat shimmer (109), Glitch (217), Gravitational/Shockwave/Heat Distortion (218)
│
├── Workspaces/
│   └── Fluid/                                       # PHASE 4 (Unified ForceField + Particle + Fluid Workspace)
│       ├── FluidWorkspaceSession.{h,cpp}            # Scene state, playback, checkpoint scrubbing, preset/template loading
│       ├── FluidOutlinerAdapter.{h,cpp}             # Drives Engine/Components/Panes/OutlinerPanel (6 collections + inline visibility/delete)
│       ├── FluidInspectorRouter.{h,cpp}             # Drives InspectorPanel (paged lettered tabs + 28-layer stack editor + inline key buttons)
│       ├── FluidTimelineAdapter.{h,cpp}             # Drives SimulationTimelineFooter + SimulationTimeRuler + KeyTrack curve/diamond editor
│       ├── FluidDrawerPanel.{h,cpp}                 # Bottom slide drawer: 54 Scenes, 71 Particle Templates, 20 Force Models, Flipbook Baker
│       └── FluidWorkspaceExtension.{h,cpp}          # Lifecycle integration + F3 Diagnostics tab registration
│
└── Codecs/
    └── Simulation/                                  # PHASE 5 (TOML + Binary DocumentContainerCodec)
        ├── ForceFieldCodec.{h,cpp}                  # .ForceField serialization
        ├── FluidGridCodec.{h,cpp}                   # .FluidGrid / .VolumetricSpace serialization
        └── ParticleSpaceCodec.{h,cpp}               # .ParticleSpace serialization (stacks + tracks + custom solids)

EngineContent/Simulation/                            # Frontier Data-Driven Space Content (SuffixRegistry.h — zero .toml files)
├── LayerSpecifications.VolumetricSpace              # Canonical 28 HTML layers + Frontier additive codes 220 (XPBD) & 221 (Collisions)
├── Presets/*.FluidGrid                              # All 54 reference Fluid/Force/Particle scene presets
└── ParticleTemplates/*.ParticleSpace                # All 71 reference Particle templates

Exhibits/                                            # PHASE 5 (Automated Numeric + Visual Verification)
├── ForceFieldValidation.cpp                         # CPU vs. GPU .slang bit-parity across all 20 models × 7 falloffs + Jolt coupling
├── ParticleGpuPhysicsValidation.cpp                 # 16 shapes, Derez, Ribbons, Strands, Jolt RigidColliderProxy, Self-Collision, XPBD Cloth/Web
├── FluidSolverValidation.cpp                        # Fixed/WorldGrid/Infinite conservation, combustion, fire-light, Wrenninge volume PNGs
└── FluidWorkspaceUiValidation.cpp                   # Headless UI render vs. Step 0 HTML reference PNGs
```

### Existing Engine Modules Reused Directly (`CLAUDE.md` §2.9 — Zero Duplication)

| Frontier Existing Module | Exact Role in This Architecture |
| :--- | :--- |
| `Engine/GeometricRaster/Compute/ComputeDispatchEncoder.{h,cpp}` | Command recording, descriptor binding, barrier synchronization for all ForceField, Particle, XPBD, and Fluid compute dispatches |
| `Engine/GeometricRaster/Compute/TimelineTransferRegister.{h,cpp}` | Async GPU↔CPU staging for sparse-tile occupancy readback, particle rigid-impulse feedback, checkpoint ring, and flipbook PNG bake |
| `Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.{h,cpp}` | Supplies live 80-byte GPU-aligned `RigidColliderProxy` SSBO directly from JoltPhysics v5.6.0 into Fluid solid cells & GPU Particle collisions |
| `Engine/SimulationSpace/PhysicsSpace/PublishedReadingExchange.h` | Lock-free multi-reader double/triple-buffered exchange between `PhysicsThread`, `FluidWorkspaceSession`, and RenderThread |
| `Engine/Shaders/Colour/ColourRampSampling.slang` | Shared analytical colour ramps (`Turbo`, `Inferno`, `Viridis`, `Magma`, `Plasma`, etc.) used by particles, slices, and vector overlays |
| `Engine/Components/Panes/{OutlinerPanel,ViewportPanel,InspectorPanel}` | Exact 3-dock Project-Zero workspace host without writing a single custom panel shell |
| `Engine/Components/Controls/{ColourRampEditor,ColourSwatchPicker,SimulationPlaybackControls,SimulationTimeRuler,SimulationTimelineFooter,PolarDirectionDial,ScalarCurveEditor,Vector3CapsuleInput,InspectorSectionPrimitives,ExclusiveSelector,DropdownSelector,ScalarSlider}` | Exact interactive controls matching `StudioPanels.js` and `TimelinePanel.js` |
| `Engine/Components/Diagnostics/DiagnosticsOverlay.{h,cpp}` | Adds `Simulation` telemetry sub-panel inside `F3` overlay (Force count, Particle/XPBD/Collision/Fluid per-step GPU timestamps, Active Sparse Tiles, Checkpoint MiB) |

---

## 📸 Phase 0 — Mandatory HTML Visual Reference Capture (`CLAUDE.md` §2.1)

**Gate**: ZERO C++ UI or shader rendering code is merged until baseline `.png` renders and numeric parity dumps are captured from `/tmp/frontier1-ref/Experimental/FluidEditor/index.html` and stored under `Exhibits/Images/FluidHtmlReference/`:

| Capture ID | Reference View / Preset | Verified Attributes |
| :--- | :--- | :--- |
| `01_Startup_DefaultCampfire.png` | Default launch state (Outliner + Viewport + Inspector + Toolbar + Timeline) | Exact 3-dock proportions, dark obsidian tokens, bottom timeline strip, Wrenninge flame+smoke shading, floor shadow & fire-light spill |
| `02_Inspector_GasDomainStack.png` | Gas Domain selected → Layer Stack tab (`101–109`) | Phase grouping (`Inject/Advance/Shade/Overlay`), layer enable/weight controls, selected layer inspector card |
| `03_Inspector_ForceModels.png` | Force Generator selected + Force Vectors & Gizmos enabled | All 20 force model gizmos (wind ribbons, vortex helices, dipole loops, containment cages) + Inspector tabs |
| `04_Particles_ObjectDerez.png` | `ObjectSurface` (`UtahTeapot`) + `Derez` + `ObsidianGlossy` backing | Exact glowing derez seam, shatter drift, backing surface reflections |
| `05_Particles_RibbonsStrands.png` | Catmull-Rom `Ribbon` trails + `OrbWeaveWeb` / `TeslaArcs` strands | Taper/fade alpha curve, catenary sag, electric jagged arc glow |
| `06_Fluid_InfiniteSparseGrid.png` | `Infinite` sparse grid mode + active tile wireframe gizmos | Dynamic tile allocation around moving plume, zero boundary clipping |
| `07_Drawer_PresetsTemplates.png` | Bottom Drawer open on `Scenes (54)`, `Particles (71)`, `Forces (20)`, `Flipbook` | Exact category filter tabs, search input, card grid proportions |
| `08_Timeline_KeyTracks.png` | Expanded timeline with keyed tracks + checkpoint markers | Diamond key indicators, Fritsch-Carlson easing selector, playback bar |
| `09_NumericGoldenDump.json` | Headless JS evaluation of all `20` ForceModels × `7` Falloffs at `16` test points + `KeyTracks` curve samples | Used as exact `1e-5` tolerance golden dataset by `ForceFieldValidation` |

---

## ⚡ Phase 1 — ForceFields Backbone (First! Shared Upstream Dependency)

> **Why ForceFields MUST come first**: Fluid gas velocity (`FluidForcesCompute.slang`), Particle integration (`ParticleUpdateCompute.slang`), Stream Tracers (`FluidTracerDebrisCompute.slang`), Debris (`FluidTracerDebrisCompute.slang`), GPU Force Vector visualizations (`ForceFieldOverlayRasterization.slang`), AND JoltPhysics rigid bodies / vehicles (`ForceFieldPhysicsCoupling.cpp`) **all evaluate the exact same ForceModel math**. Building ForceFields once as a pure, zero-dependency, CPU+GPU-identical backbone eliminates all code duplication and guarantees 100% unified behavior across every simulation pillar.

### 1.1 Exact 20 Analytic Force Models (`ForceModelOrdinal` — 1:1 `ForceGeneratorCatalog.js` ↔ `ForceFieldChunk`)

| Code | C++ `ForceModelOrdinal` | HTML Display Name | Exact Formula (`ForceFieldChunk` `L294–L493` 1:1) | Gas Velocity-Saturated? |
| :---: | :--- | :--- | :--- | :---: |
| `0` | `DirectionalWind` | Directional Wind | `axis * strength * (1 + secondary * (ValueNoise(p*0.6 + vec3(t*freq, 0, t*freq*0.7))*2 - 1)) * w` | Yes |
| `1` | `PointAttractor` | Point Attractor | `-dir * strength * w * (dist / (dist + max(secondary, 0.02)*radius + 1e-4))` | Yes |
| `2` | `PointRepulsor` | Point Repulsor | `+dir * strength * w * (dist / (dist + max(secondary, 0.02)*radius + 1e-4))` | Yes |
| `3` | `RotationalColumn` | Vortex Column | `(tangent * strength * core - radialDir * strength * 0.3 * freq + axis * secondary) * w` (`core = radialDist / (radialDist + 0.12*radius)`) | Yes |
| `4` | `CurlNoiseTurbulence` | Curl Noise Turbulence | `CurlNoise(p * freq + vec3(0, t * secondary, t * secondary * 0.31)) * strength * w * 0.35` | Yes |
| `5` | `DragBaffle` | Drag Baffle | `-velocity * strength * w` | **No** (velocity-relative) |
| `6` | `RadialBurst` | Radial Burst | `+dir * strength * PulseEnvelope(freq, secondary, tertiary) * w` | **No** (short-lived pulse) |
| `7` | `ThermalUpdraft` | Thermal Updraft | `axis * strength * Falloff(mode, radialDist / radius)` | Yes |
| `8` | `AxisAttractor` | Axis Attractor | `-radialDir * strength * Falloff(mode, radialDist / radius)` | Yes |
| `9` | `RotationalRing` | Vortex Ring | `ringPoint = centre + radialDir * radius`, `circulation = cross(cross(axis, radialDir), normalize(p - ringPoint)) * strength * Falloff(mode, coreDist / max(freq, 0.05))` | Yes |
| `10` | `Orbit` | Orbit | `(tangent * (radialDist / (radialDist + 0.1*radius)) + toCentre * secondary) * strength * w` | Yes |
| `11` | `Containment` | Containment | `overshoot = dist - radius * (1 - max(secondary, 0.02))`; `-dir * strength * wall²` (`wall = clamp(overshoot / (max(secondary, 0.02)*radius), 0, 1)`) | **No** (boundary constraint) |
| `12` | `ShockwaveShell` | Shockwave Shell | `shell = exp(-((dist - phase*radius) / (max(secondary, 0.02)*radius))²)`; `dir * strength * shell * (1 - phase * 0.7)` | **No** (short-lived pulse) |
| `13` | `Implosion` | Implosion | `-dir * strength * PulseEnvelope(freq, secondary, tertiary) * w` | **No** (short-lived pulse) |
| `14` | `DirectionalBurst` | Directional Burst | `axis * strength * PulseEnvelope(freq, secondary, tertiary) * w` | **No** (short-lived pulse) |
| `15` | `BrownianJitter` | Brownian Jitter | `Hash33(floor(p * freq) + floor(t * max(secondary, 0.1)) * 17.0) * strength * w` | Yes |
| `16` | `DirectedStream` | Flow Stream | `(axis * freq - velocity) * strength * w` | **No** (velocity-relative) |
| `17` | `WindShear` | Wind Shear | `angle = clamp(dot(r, axis)/radius, -1, 1) * secondary * PI`; `(u * cos(angle) + v * sin(angle)) * strength * w` | Yes |
| `18` | `Spin` | Spin | `cross(axis, radial) / radius * strength * Falloff(mode, radialDist / radius)` | Yes |
| `19` | `MagneticDipole` | Magnetic Dipole | `field = 3 * n * dot(axis, n) - axis`; `SafeNormalise(field) * strength * w * mix((core/d)³, 1.0, 0.35)` | Yes |

### 1.2 Exact 7 Falloff Curves (`ForceFalloffOrdinal`, Codes `0..6`)

| Code | `ForceFalloffOrdinal` | Exact Formula (`ForceFieldChunk` `L305–L316`, `d >= 1 => 0` except `3`) |
| :---: | :--- | :--- |
| `0` | `SmoothStep` | `1.0 - smoothstep(0.0, 1.0, d)` |
| `1` | `Linear` | `1.0 - d` |
| `2` | `InverseSquare` | `(1.0 / (1.0 + 16.0 * d * d)) * (1.0 - smoothstep(0.85, 1.0, d))` |
| `3` | `Constant` | `1.0` (infinite range, ignores `radius`) |
| `4` | `Gaussian` | `exp(-d * d * 5.0) * (1.0 - smoothstep(0.9, 1.0, d))` |
| `5` | `Plateau` | `1.0 - smoothstep(0.55, 1.0, d)` |
| `6` | `Cosine` | `0.5 + 0.5 * cos(d * 3.14159265)` |

### 1.3 GPU Buffer Layout (`ForceModelGpuRecord` — 64 Bytes, 16-Byte Aligned)

Replacing WebGL2's `MAX_FORCES = 16` uniform arrays with a Vulkan Storage Buffer (`StructuredBuffer<ForceModelGpuRecord>`) supports up to **`256` simultaneous active force generators** while keeping the exact 4 × `vec4` register layout of `ForceFieldChunk`:

```cpp
struct alignas(16) ForceModelGpuRecord
{
    float PositionRadius[4];         // xyz = World Position (m), w = Influence Radius (m)
    float DirectionStrength[4];      // xyz = Unit Axis Direction, w = Effective Strength (Strength * KeyTrackWeight)
    float TypeFalloffParams[4];      // x = ForceModelOrdinal (0..19), y = ForceFalloffOrdinal (0..6), z = ParamA, w = ParamB
    float TimingChannelMask[4];      // x = PulsePeriod (s), y = PhaseOffset (s), z = TargetChannelBits (1=Gas, 2=Particle, 4=Rigid), w = Flags
};
static_assert(sizeof(ForceModelGpuRecord) == 64);
```

### 1.4 Shared `KeyTrackSampler` & `ForceFieldPhysicsCoupling`
- **`KeyTrackSampler.{h,cpp}`**: 1:1 port of `KeyTracks.js` — supports `Constant (0)`, `Linear (1)`, `Smooth (2, Fritsch–Carlson monotone cubic Hermite)`, `Bezier (3, explicit tangents)`, extrapolations `Hold (0)`, `Cycle (1)`, `PingPong (2)`, linear-light colour interpolation, `KeyTimeEpsilon = 1/480 s`, up to `256` keys per track.
- **JoltPhysics Coupling (`ForceFieldPhysicsCoupling.{h,cpp}`)**: Each `PhysicsThread` tick acquires a zero-lock `ReadingLease` on `PublishedForceFieldReading` and integrates `ForceFieldSampler::EvaluateTotalAcceleration(bodyPos, bodyVel, simTime, TargetChannel::Rigid)` into Jolt dynamic rigid bodies (`AddForce` / `AddTorque`) and `VehiclePhysicsRig` aerodynamic wind vectors!

---

## ✨ Phase 2 — GPU Particle Simulation, GPU Collisions & GPU XPBD Constraints

### 2.1 Exact 1:1 Port of All 19 HTML `ParticleCloud` Layers (`LayerSpecifications.toml` Codes `201..219`)

| Code | Phase | Layer Name (TOML Token) | Exact GPU Implementation (`.slang` / C++) |
| :---: | :--- | :--- | :--- |
| `201` | `Inject` | Spawn (`spawn`) | `ParticleShapeAnchor.slang`: `Emit` / `Cloner` modes; all **16 shapes** (`Point`, `Sphere`, `Box`, `Disc`, `Ring`, `Line`, `Grid`, `Torus`, `SurfaceShell`, `MocapFigure`, `Dome`, `ObjectSurface`, `Spiderweb`, `ClothGrid`, `FloraBed`, `FireworkBurst`); up to `16,384` (HTML parity) or `262,144` (Frontier Vulkan SSBO ceiling) particles per cloud; `19` built-in 3D solids (`UtahTeapot`..`TorusKnot`) + `.GeometrySpace`/`.obj`/`.stl` import; `3` sampling modes (`EvenSurface`, `SurfaceVolume`, `RidgeAccent`); `10` Firework patterns + 3D `SkyText` |
| `202` | `Advance` | Home spring (`home-spring`) | `ParticleUpdateCompute.slang`: cloner anchor return stiffness (`Spring`, `Damping`) + curl-noise anchor `Dissolve` |
| `203` | `Advance` | Gravity (`gravity`) | Vertical gravitational acceleration (`m/s²`) |
| `204` | `Advance` | Drag (`drag`) | Exponential velocity damping `exp(-Drag * dt)` |
| `205` | `Advance` | Noise (`noise`) | 3D curl-noise wander (`Noise`, `NoiseScale`, `NoiseSpeed`) via `ForceFieldNoise.slang` |
| `206` | `Advance` | Field influence (`field-influence`) | Evaluates Phase 1 `ForceModelEvaluation.slang` SSBO scaled by `FieldInfluence` |
| `207` | `Advance` | Gas coupling (`gas-coupling`) | Trilinear 3D sample of Phase 3 Fluid velocity (`SampleVolume(uFluidVelocity, world)`) scaled by `GasInfluence` |
| `208` | `Advance` | Bounce (`bounce`) | Floor plane (`y = GroundHeight`) + analytic sphere collider bounce (`Bounce` restitution) |
| `209` | `Shade` | Colour rule (`colour-rule`) | All **7 modes** (`Life`, `Speed`, `Displacement`, `Height`, `Seed`, `Swatch`, `Cycle`), `8`-stop `ColourSequence` + `ColourRampSampling.slang`, hue/brightness jitter, flicker |
| `210` | `Shade` | Size over life (`size-over-life`) | `World` (meters) or `Pixels`, `SizeStart → SizeEnd` ramp, log-spread `SizeRandom` |
| `211` | `Shade` | Sprite & Object (`sprite`) | `ParticleSpriteRasterization.slang` (**18 analytical cutouts `0..17`**: `Glow`, `Disc`, `Bokeh`, `Star`, `Square`, `Cube3D`, `Butterfly`, `HexTile`, `HoloGlyph`, `SnowCrystal`, `RainDrop`, `LeafBlade`, `BlossomPetal`, `CrystalShard2D`, `ShockRing`, `RadarArc`, `AuroraCurtain`, `CoinDisc`; `Point`/`Streak`; `Alpha`/`Additive`; DOF `FocusDistance`/`Aperture`; halo `Glow`) + `ParticleCarrierSolidRasterization.slang` (`Object3D` instanced PBR carriers with spin/tilt/roughness/metallic/glow + `3` backing surfaces: `ObsidianGlossy`, `BrushedChrome`, `GhostWireframe`) |
| `212` | `Shade` | Trail (`trail`) | `ParticleTrailUpdateCompute.slang` + `ParticleRibbonRasterization.slang`: ring buffer up to `96` samples, `TrailInterval`, `TrailWidth`, `TrailTaper`, `TrailFade`, `1..4` Catmull-Rom subdivisions, `Particle`/`Along` colour, `TrailRatio`, `Linger` fade after death |
| `213` | `Advance` | Dissolve & Derez (`dissolve-derez`) | All **6 modes** (`Off`, `Derez`, `ReverseDerez`, `MeltDissolve`, `ButterflyMorph`, `CellGlitch`), glowing edge band (`DerezEdgeWidth`, `DerezEdgeGlow`, `DerezEdgeColour`), `DerezShatter` velocity kick |
| `214` | `Advance` | Mocap performer (`mocap-rig`) | Procedural articulated skeleton anchor displacement for all **4 clips** (`Sprint`, `Dance`, `Martial`, `Levitate`), `MocapAmplitude`, `MocapSpeed`, `MocapScale`, `MocapTrailKick` |
| `215` | `Advance` | MoGraph effector (`mograph-effector`) | All **4 modes** (`Ripple`, `Orbiters`, `PlaneWave`, `NoiseField`) across `3` axes (`Radial`, `HeightY`, `SurfaceNormal`) driving push, scale boost, and colour shift |
| `216` | `Advance` | Flock & Swarm (`flock-swarm`) | Boids cohesion, orbital swirl, and butterfly/wing-beat flutter (`SwarmStrength`, `SwarmCohesion`, `SwarmOrbit`, `SwarmWingRate`) |
| `217` | `Overlay` | Glitch (`glitch`) | Screen-space post-overlay in `SimulationPostOverlay.slang` (`GlitchAmount`, `GlitchRate`, `GlitchStepJitter`, `GlitchScanlines`, `GlitchChromatic`) |
| `218` | `Overlay` | Distortion & Lensing (`distortion`) | Screen-space post-overlay (`GravitationalLens`, `ShockwaveRipple`, `HeatBubble`, `DistortionHorizon`, `DistortionChromatic`) |
| `219` | `Shade` | Strands, Webs & Arcs (`strands-arcs`) | `ParticleStrandRasterization.slang`: all **11 modes** (`OrbWeaveWeb`, `MessyCobweb`, `CablesChains`, `HangingVines`, `TallReeds`, `WoodlandFern`, `GiantKelp`, `SeaAnemone`, `TeslaArcs`, `PlasmaCage`, `StormWeb`), `Primary`/`Secondary` variants, catenary sag, wind sway, thickness, glow |

### 2.2 Additive Frontier GPU Physics Layers (Zero Divergence on HTML Presets!)

Two opt-in Frontier native GPU compute layers (`Codes 220 & 221` in `LayerSpecifications.toml`) elevate ParticleSpace into a full real-time GPU physics engine without altering a single pixel of the `71` HTML templates:

#### A. Layer `221` — GPU Particle Collisions (`ParticleCollisionCompute.slang` + `ParticleSpatialHashCompute.slang`)

| Collision Channel | Algorithm & GPU Buffer Design | Coupling Direction |
| :--- | :--- | :--- |
| **1. Analytic Spheres & Ground** | Exact 1:1 parity with HTML `ColliderChunk` (4 spheres + `GroundHeight`), always active when Layer `208` (`Bounce`) is enabled | One-way (`Collider → Particle`) |
| **2. JoltPhysics `RigidColliderProxy` SSBO** | Consumes existing 80-byte `RigidColliderProxy` buffer from `BroadphaseSolver` (`Sphere`, `OrientedBox`, `Capsule`, plus `CompoundHull` SDF lookup). Exact signed distance + surface normal + linear/angular surface velocity $\mathbf{v}_{\text{surf}} = \mathbf{v}_b + \boldsymbol{\omega}_b \times (\mathbf{p} - \mathbf{c}_b)$. Computes restitution impulse $j_n$ and Coulomb tangential friction $j_t \le \mu j_n$. | **Two-way**: writes per-body atomic `(impulse.xyz, torque.xyz)` into `RigidImpulseAccumulationBuffer`, read back via `TimelineTransferRegister` and applied on `PhysicsThread`! |
| **3. GPU Spatial-Hash Particle-Particle Self-Collision** | Uniform 3D hash grid (`cellSize = 2.0 * maxParticleRadius`, `65,536` buckets): (1) `CountCells` → (2) `PrefixSum` (Blelloch workgroup scan) → (3) `ScatterSortedIndices` → (4) `Resolve27Neighbors` (checks `3×3×3` neighbor cells, resolves overlapping spheres with positional separation + normal restitution + Coulomb friction + optional granular cohesion). Zero CPU involvement; supports `100,000+` colliding particles at `60+ FPS`. | Particle ↔ Particle (within same cloud or shared global collision group) |

#### B. Layer `220` — GPU Orthotropic XPBD Constraints (`ParticleXpbdCompute.slang`)

Directly ported from Frontier's own verified GPU XPBD formulation (`Experimental/ClothEditor/Engine/WebGpuClothAccelerator.js` — Macklin, Müller & Chentanez, MIG 2016) into Vulkan `.slang` compute:

- **Zero GPU Atomics via Conflict-Colored Batches**: `ParticleXpbdNetworkBuilder.{h,cpp}` partitions Distance edges, Strain triangles (`ClothGrid`), Bending hinges, and Tether pins into independent color batches (matching `packGpuNetworkTables()` header offsets in `WebGpuClothAccelerator.js`). Every dispatch iterates over one conflict-free color batch in parallel with **100% bit-deterministic execution** and zero atomic contention!
- **Persistent Dual Lagrange Multipliers ($\lambda$)**: Maintained across substeps (`defaultSubsteps = 4`) and Gauss-Seidel iterations (`defaultIterations = 12..16`) so stiffness is completely independent of frame rate and iteration count:
  $$\Delta \lambda = \frac{-C(\mathbf{x}) - \tilde{\alpha}\,\lambda}{\sum_i w_i \|\nabla_{\mathbf{x}_i} C\|^2 + \tilde{\alpha}}, \qquad \tilde{\alpha} = \frac{\alpha}{\Delta t_{\text{sub}}^2}, \qquad \Delta \mathbf{x}_i = w_i\,\Delta\lambda\,\nabla_{\mathbf{x}_i} C$$
- **4 Supported Physical Constraint Types** (activating directly on `ClothGrid`, `Spiderweb`, `FloraBed`, `CablesChains`, `HangingVines`, and arbitrary imported `PolygonNetwork` meshes):
  1. **Distance / Strand Stretch** ($\alpha_{\text{stretch}}$ + optional break threshold for tearable webs/cloth).
  2. **Orthotropic Triangle Strain (Warp / Weft / Shear)** using precomputed $D_m^{-1}$ inverse rest-pose matrices (exact `WebGpuClothAccelerator.js` math).
  3. **Dihedral / Isometric Hinge Bending** ($\alpha_{\text{bend}}$) for realistic folds in cloth, leaves, kelp, and ribbons.
  4. **Unilateral Contact Constraints** ($\phi(\mathbf{x}) \ge 0$ against Jolt `RigidColliderProxy` & spatial-hash self-contact) solved inside the same substep loop so cloth/strands drape cleanly over moving vehicles and rigid bodies!
- **Unified Force Coupling**: Inside every XPBD substep prediction (`x_pred = x + dt_sub * (v + dt_sub * a_ext)`), `a_ext` directly samples **Phase 1 `ForceModelEvaluation.slang`** (wind, vortices, explosions) AND **Phase 3 Fluid velocity aerodynamic drag** (`F_aero = C_d * A * |v_rel| * v_rel`)!

---

## 🔥 Phase 3 — 3D Eulerian + Sparse Infinite Fluid Solver & Multiple-Scattering Volume Renderer

### 3.1 Exact 1:1 Port of All 9 `GasVolume` Layers (`LayerSpecifications.toml` Codes `101..109`)

| Code | Phase | Layer Name (TOML Token) | Exact Vulkan `.slang` Compute / Shading Target |
| :---: | :--- | :--- | :--- |
| `101` | `Inject` | Domain (`domain`) | `FluidRegionTypes.h` (`Fixed`, `WorldGrid` multi-tile up to `5×5×5`, `Infinite` sparse tile bank); `Gravity`, `ForceResponse`, `PressureIterations`, `TimeScale`, `SceneLoop`, `Advection` (`SemiLagrangian` / `MacCormack`) |
| `102` | `Inject` | Source (`source`) | `FluidInjectCompute.slang` (`Sphere`, `Disc`, `Box` shapes, noise `Jitter`, `PulsePeriod`, velocity + density/temperature/fuel blend) |
| `103` | `Advance` | Buoyancy (`buoyancy`) | `FluidForcesCompute.slang` (`uBuoyancy * (T - T_amb) - uGravity * density`) + cooling in `FluidThermodynamicsCompute.slang` |
| `104` | `Advance` | Combustion (`combustion`) | `FluidThermodynamicsCompute.slang` (exact ignition `0.9`, `burn = min(fuel, uCombustion * 2.5 * dt * fuel * smoothstep(0.9, 1.9, T))`, writes reaction rate to `state.w`) + `FluidDivergencePressureCompute.slang` (`divergence -= uExpansion * 0.08 * state.w`) |
| `105` | `Advance` | Vorticity (`vorticity`) | `FluidCurlVorticityCompute.slang`: central-difference curl $\boldsymbol{\omega} = \nabla \times \mathbf{v}$, normalized $\mathbf{N} = \nabla |\boldsymbol{\omega}| / \|\nabla |\boldsymbol{\omega}|\|$, confinement $\epsilon h (\mathbf{N} \times \boldsymbol{\omega})$ |
| `106` | `Advance` | Dissipation (`dissipation`) | `FluidAdvectCompute.slang` & `FluidMacCormackCompute.slang` (`Dissipation`, `VelocityDissipation`) |
| `107` | `Shade` | Smoke shading (`smoke-shading`) | `FluidVolumeRaymarch.slang` (`Density`, `Albedo`, `Absorption`, `Anisotropy`, `Ambient`, `LightIntensity`) |
| `108` | `Shade` | Fire shading (`fire-shading`) | `FluidVolumeRaymarch.slang` (`FireIntensity`, `FireThreshold`, `FireSoftness`, `FireOpacity`, `FireEmber`, `FireFlame`, `FireCore`, `FireLight`) |
| `109` | `Overlay` | Heat shimmer (`heat-shimmer`) | `SimulationPostOverlay.slang` (`ShimmerStrength`, `ShimmerFrequency`, `ShimmerSpeed`, `ShimmerChromatic`) |

### 3.2 Native Vulkan 3D Volume Layout & All 3 Grid Topologies

Unlike WebGL2 (which packs 3D grids into 2D tiled slice textures), Frontier Vulkan 1.2 uses native **`VK_IMAGE_TYPE_3D`** `VK_FORMAT_R16G16B16A16_SFLOAT` textures with hardware trilinear filtering — while preserving **exact cell coordinates, boundary flags, and sparse lookup indexing**:

| Grid Topology | Vulkan Storage Layout | Exact Reference Parity |
| :--- | :--- | :--- |
| **`Fixed`** | Single `3D` `RGBA16F` ping-pong pair (`Velocity`, `State`, `PressureLight`, plus scratch `Curl`, `Divergence`, `Forward`, `Backward`) at `[ResX, ResY, ResZ]` | Identical cell indexing `CellFromWorld`, boundary open/closed bitmask (`uBoundaryMode`) |
| **`WorldGrid` (Sliding)** | Multi-tile sliding world box (`1³..5³` tiles) shifted on integer cell boundaries via `FluidGradientShiftCompute.slang` (`ShiftFragment` 1:1) | Zero resampling blur when following moving vehicles or plumes |
| **`Infinite` (Sparse Tile Bank)** | `SparseTileBank` allocates `MaxTiles` (`64..512`) `tileCells³` (`16³` or `24³`) bricks inside a 3D atlas + `64×16×64` `R16UI` lookup texture (`0 = empty`, `slot+1 = allocated`). `FluidOccupancyCompute.slang` computes per-tile mean activity and streams compact occupancy table via `TimelineTransferRegister` so `SparseTileBank::ScheduleTiles()` allocates active/neighbor/source tiles and recycles dormant ones | Exact 1:1 parity with `FluidSolver.js` `configureInfinite()` + `scheduleTiles()` + `OccupancyFragment` |

### 3.3 Exact Per-Step Vulkan Compute Execution Sequence (`FluidGpuSolver::Step`)

1. **Schedule / Relocate** (`WorldGrid` shift OR `Infinite` sparse tile scheduling + `R16UI` lookup upload).
2. **Advect Velocity** (`RK2` or Selle 2008 overshoot-fallback `MacCormack` with Courant `smoothstep(1.0, 2.0, courant)` blend).
3. **Advect State** (`Density`, `Temperature`, `Fuel`, `Reaction`).
4. **Inject Sources** (`FluidInjectCompute.slang` mode `0` velocity, mode `1` state for all active `ReleaseSource` nodes).
5. **Apply Forces & Buoyancy** (`FluidForcesCompute.slang` — evaluates Phase 1 `ForceModelEvaluation.slang` with exact continuous-force velocity saturation `clamp(1.0 - along / (size * uForceResponse), 0.0, 1.0)` + Jolt `RigidColliderProxy` solid masking).
6. **Curl + Vorticity Confinement** (`FluidCurlVorticityCompute.slang`).
7. **Thermodynamics & Outflow Sponge** (`FluidThermodynamicsCompute.slang` — combustion reaction in `state.w`, cooling, open-face outflow sponge `mix(0.82, 1.0, keep)`).
8. **Divergence + Flame Light Source** (`FluidDivergencePressureCompute.slang` — writes `.x = divergence - 0.08 * uExpansion * state.w`, `.y = flameLightSource`).
9. **Warm-Started Jacobi Pressure + Screened-Diffusion Fire Light** (`uPressureIterations` iterations; iteration 0 uses `uRetain = 0.96`; exact formula `.x = (sum * uRetain - source.x * h²) / 6.0`, `.y = (lightSum + source.y * 0.55) / 6.28`).
10. **Pressure Gradient Subtraction & Wall/Solid Enforcement** (`FluidGradientShiftCompute.slang`).
11. **Secondary Simulators**: `StreamTracerSimulation` (`8,192` tracers advected by fluid velocity + force field) & `FluidDebrisSimulation` (`Rubble`, `Ember`, `Paper` with aerodynamic drag + floor bounce).
12. **Checkpoint Ring Commit** (every `0.5 s` simulated time into `192 MiB` GPU ring buffer for instant timeline rewind/scrub).

### 3.4 Exact Volume Raymarcher Parity (`FluidVolumeRaymarch.slang` ↔ `SceneFragment`)

Ported line-for-line from `FluidShaders.js` lines `1129–1384`:
- **Interleaved Gradient Noise** (`StableJitter`) for shimmer-free stepping without temporal ghosting.
- **Wrenninge 3-Octave Multiple Scattering**: `(octavePhase.x * exp(-tau) + 0.5 * octavePhase.y * exp(-tau * 0.5) + 0.25 * octavePhase.z * exp(-tau * 0.25)) / 1.75` with 2-lobe Henyey-Greenstein phase (`mix(HG(-0.25), HG(g), 0.72) * 4π`).
- **Geometric-Growth Sun Optical Depth** (`Growth = 1.35`, up to `16` shadow steps) used both inside the plume and to cast plume shadows onto the floor!
- **Thin-Wisp Powder Darkening**: `1.0 - 0.45 * exp(-sigmaSmoke * 2.2)`.
- **2-Tap Vertical Sky Occlusion** (`+2` and `+6` cells up) + **Screened-Diffusion Fire Light** (`SampleVolume(uPressure, coord).y * uFireLight * uFireIntensity * 0.35`) illuminating both smoke and floor.
- **Energy-Conserving Integration** (`source * (1 - exp(-sigma * step)) / sigma`) + **ACES Filmic Tone Transform** (`uExposure`) + **Transparent Premultiplied Flipbook Bake Mode** (`uTransparent = 1`).

---

## 🖥️ Phase 4 — Unified Workspace UI (1:1 HTML Parity via `Engine/Components/`)

Every panel, tab, drawer, gizmo, and timeline track maps directly from `FluidEditor` (`index.html`, `UI/StudioPanels.js`, `UI/TimelinePanel.js`) onto Frontier's existing Project-Zero `Engine/Components/` without inventing custom parallel UI widgets:

| `FluidEditor` HTML Region | Frontier Native Implementation | 1:1 Features Preserved |
| :--- | :--- | :--- |
| **Left Outliner (`refs.outliner`)** | `Engine/Components/Panes/OutlinerPanel` via `FluidOutlinerAdapter` | 6 typed collections (`Gas Domain`, `Sources`, `Force Fields`, `Particles`, `Colliders`, `Visualizers`), inline visibility eye toggle, enable toggle, inline delete button, keyboard focus & rename |
| **Top 2-Row Toolbar (`refs.toolbar`)** | `ViewportPanel` top capsule strips (`CapsuleButton`, `ExclusiveSelector`) | Row A: `Scenes (54)`, `Flipbook Baker`, Play/Pause/Step/Reset, Grid Topology (`Fixed`/`WorldGrid`/`Infinite`); Row B: `+ Force`, `+ Source`, `+ Particle`, `+ Collider`, Overlay toggles (`Volume`, `Vectors`, `Forces`, `Slice`, `Tracers`, `Debris`, `Gizmos`, `Voxel Preview`) |
| **Center 3D Viewport (`refs.canvas`)** | `Engine/Components/Panes/ViewportPanel` + Vulkan Swapchain | Orbit/Pan/Dolly camera, interactive 3D gizmo drag (`ForceFieldOverlayRasterization.slang` & `FluidOverlayRasterization.slang`), zero CPU `ImDrawList` gizmos (`CLAUDE.md` §2.2), HUD stats badge |
| **Right Inspector (`refs.inspector`)** | `Engine/Components/Panes/InspectorPanel` via `FluidInspectorRouter` | Entity header + inline rename + scope bar; **Paged Lettered Pill Tabs** (`tabByKind` memory per kind + `‹` / `›` steppers + `1 / N` counter); Transform XYZ table (`Vector3CapsuleInput`); **Effect Layer Stack Editor** (phase-ordered `Inject/Advance/Shade/Overlay`, add/remove/reorder/weight/enable layers, selected-layer card); inline keyframe diamond buttons (`KeyTracks`); `ColourRampEditor` & `ColourSwatchPicker` for 8-stop sequences & fire ramps |
| **Bottom Slide Drawer** | `FluidDrawerPanel` docked overlay sheet | 4 tabs: **`Scenes` (`54` presets)**, **`Particles` (`71` templates across 8 categories)**, **`Forces` (`20` models)**, **`Flipbook`** (grid size, frame count, transparent PNG sheet bake via `TimelineTransferRegister`) — with category filter pills & live text search |
| **Bottom Timeline (`TimelinePanel.js`)** | `SimulationTimelineFooter` + `SimulationTimeRuler` + `SimulationPlaybackControls` | Play/Pause/Loop/Scrub, timecode `MM:SS.ff`, checkpoint ring markers, expandable track rows for node & layer parameters, diamond keyframe selection/move/scale, Ease selector (`Constant`/`Linear`/`Smooth`/`Bezier`), Extrapolation (`Hold`/`Cycle`/`PingPong`) |
| **Diagnostics Overlay (`F3`)** | Existing `Engine/Components/Diagnostics/DiagnosticsOverlay` | New `Simulation` tab displaying GPU dispatch timings (ForceFields, Particle Update, Spatial Hash, XPBD, Fluid Advect/Jacobi/Raymarch), active particles/constraints/tiles, checkpoint ring MiB, and Jolt two-way impulse count |

---

## 💾 Phase 5 — Serialization, Cross-Thread Integration & Verification Gates

### 5.1 Serialization (`DocumentContainerCodec` — TOML + Optional Binary Section)

- **`.ForceField`**: TOML node header + `ForceModelOrdinal`, `ForceFalloffOrdinal`, parameters, and `KeyTracks` (zero binary required).
- **`.FluidGrid` / `.VolumetricSpace`**: TOML header (`FluidRegion` + `GasVolume` `EffectLayerStack` + `Sources` + `KeyTracks`) with optional compressed binary `RGBA16F` checkpoint section for saved warm-start states.
- **`.ParticleSpace`**: TOML header (`ParticleCloud` `EffectLayerStack` including additive `Code 220` XPBD & `Code 221` Collision layers + `KeyTracks`) with optional binary section for imported custom `CarrierPolygonNetwork` geometry and pre-colored XPBD constraint topology tables.
- **Zero-Loss Reference Import**: `FluidPresetRegistry` and `ParticleTemplateRegistry` parse all `54` `Presets/*.toml` and `71` `ParticleTemplates/*.toml` files **directly with zero manual conversion** using `tomlpp`!

### 5.2 Exit Criteria & Automated Verification Exhibits

Every phase closes ONLY when its automated C++ validation executable passes `100%` with headless GPU/CPU verification and visual `.png` sign-off:

| Phase | Validation Target | Quantitative & Visual Pass Criteria |
| :---: | :--- | :--- |
| **Phase 0** | `Exhibits/Images/FluidHtmlReference/*.png` + `09_NumericGoldenDump.json` | All `8` HTML PNG captures + `20 × 7 × 16 = 2,240` golden force-vector reference samples committed |
| **Phase 1** | `ForceFieldValidation` (`CTest`) | CPU `ForceFieldSampler` vs. Vulkan `ForceModelEvaluation.slang` vs. JS `09_NumericGoldenDump.json` max error `< 1e-4` across all `20` models × `7` falloffs; `KeyTrackSampler` Hermite/Bezier/Cycle/PingPong exact match; Jolt dynamic body & vehicle wind deflection verified; GPU gizmo PNG renders cleanly |
| **Phase 2** | `ParticleGpuPhysicsValidation` (`CTest`) | All `16` spawn shapes, `19` 3D carrier solids, `18` sprite cutouts, `6` derez modes, `11` strand modes, `10` firework patterns, Catmull-Rom ribbons verified; **GPU `RigidColliderProxy` bounce + two-way impulse readback** verified against live Jolt bodies; **GPU spatial-hash self-collision** zero interpenetration (`10,000+` spheres); **GPU XPBD** cloth drape & spiderweb tension stable across `4` substeps × `16` iterations with zero NaN/divergence; all `71` templates load & step cleanly |
| **Phase 3** | `FluidSolverValidation` (`CTest`) | `Fixed`, `WorldGrid` (`3³` sliding), and `Infinite` sparse tile bank (`allocate/evict` + conservation) pass; Selle MacCormack monotonicity (zero unbounded spin-up over `600` steps); combustion reaction + expansion + warm-started Jacobi (`div < 1e-2`) + fire-light floor glow; Wrenninge 3-octave volume PNG matches `01_Startup_DefaultCampfire.png`; all `54` presets load & step cleanly; flipbook transparent PNG sheet exported |
| **Phase 4 & 5** | `FluidWorkspaceUiValidation` (`CTest`) + `Frontier` Editor Smoke | Headless ImGui PNG captures match Step 0 HTML reference layout (`Outliner`, `Inspector`, `LayerStack`, `Drawer`, `Timeline`, `F3 Simulation` telemetry); `.ForceField` / `.FluidGrid` / `.ParticleSpace` round-trip byte-identical; zero `BannedWordsVocabulary.md` violations; zero `CLAUDE.md` §2.2 CPU viewport gizmos; full `CMake` + `CTest` green |

---

## 📋 6. Strict 1-Slice-Per-Turn Line-Indexed Execution Ledger (`S01`–`S28`)

> **Execution Rule (Locks L1–L6)**: Execute strictly in numeric order `S01 → S28`, **maximum one slice per turn**. Never start `S(N+1)` until `S(N)` clears all 4 exit gates (line audit, reference-generated oracle diff, banned-word check, build + commit + push). Notice the **Hard Parity Wall** between `S23` and `S24`: all `23` slices of pure 1:1 HTML parity lock completely before any additive Frontier GPU Physics (`S24`–`S27`) is touched.

### Stage A — Reference Oracles & Baseline Capture (Phase 0)

| Slice | Exact Upstream Reference (File & Lines) | Native Target(s) | Reference-Generated Oracle & Hard Pass Gate |
| :---: | :--- | :--- | :--- |
| **S01** | `Experimental/FluidEditor/` (`index.html`, `Engine/LayerSpecifications.toml`, `Presets/*.toml`, `ParticleTemplates/*.toml`) | `EngineContent/Simulation/` + `Exhibits/Images/FluidHtmlReference/` (`8` PNGs + golden `.json` dumps) | Run headless JS/Chromium directly on `FluidEditor`: capture `8` baseline PNGs + dump raw JS golden tables for `ForceGeneratorCatalog`, `KeyTracks`, `LayerStack` (`54` presets + `71` templates), `ObjectMeshCatalog` (`19` solids), `FireworkPatterns` (`10` patterns), and `ForceFieldChunk` (`20×7×16` vectors). |

### Stage B — Phase 1: ForceFields Backbone & Shared Layer/Track Engine (Upstream First!)

| Slice | Exact Upstream Reference (File & Lines) | Native Target(s) | Reference-Generated Oracle & Hard Pass Gate |
| :---: | :--- | :--- | :--- |
| **S02** | `Engine/ForceGeneratorCatalog.js` (`L1–L458`) | `Engine/SimulationSpace/ForceFields/{ForceModelTypes.h,ForceModelRegistry.{h,cpp}}` | All `20` `ForceModelOrdinal` (`0..19`), `7` `ForceFalloffOrdinal` (`0..6`), defaults, ranges, and parameter metadata match JS golden JSON `100%` exact. |
| **S03** | `Engine/FluidShaders.js` `NoiseChunk` (`L234–L293`) + `ForceFieldChunk` (`L294–L493`) | `Engine/Shaders/ForceFields/{ForceFieldNoise.slang,ForceModelEvaluation.slang}` + `Engine/SimulationSpace/ForceFields/ForceFieldSampler.{h,cpp}` | CPU `ForceFieldSampler` AND Vulkan `.slang` compute output match JS `ForceFieldChunk` golden table (`20 × 7 × 16 = 2,240` vectors) to `< 1e-4` max abs error. |
| **S04** | `Engine/KeyTracks.js` (`L1–L360`) | `Engine/SimulationSpace/ForceFields/KeyTrackSampler.{h,cpp}` | Matches JS `KeyTracks` golden dump across `Constant`/`Linear`/`Smooth` (Fritsch–Carlson)/`Bezier`, `Hold`/`Cycle`/`PingPong`, linear-light colour easing, and `KeyTimeEpsilon = 1/480` to `< 1e-5`. |
| **S05** | `Engine/LayerSpecifications.toml` (`L1–L2274`) + `Engine/LayerStack.js` (`L1–L441`) + `Engine/LayerSpecificationValidation.js` (`L1–L109`) | `Engine/SimulationSpace/EffectLayers/{EffectLayerSpecification.{h,cpp},EffectLayerStack.{h,cpp}}` | Evaluates all `54` `Presets/*.toml` gas stacks and all `71` `ParticleTemplates/*.toml` particle stacks and matches JS `evaluateLayerStack()` JSON dump field-for-field (`< 1e-5`). |
| **S06** | `Engine/ForceGeneratorCatalog.js` packing + `Engine/FluidShaders.js` `ForceVectorVertex` (`L1423–L1480`) + `Engine/VolumeRenderer.js` `forceGizmo` (`L575–L994`) | `Engine/SimulationSpace/ForceFields/{ForceFieldRegister.{h,cpp},ForceFieldGpuUploader.{h,cpp}}` + `Engine/Shaders/ForceFields/ForceFieldOverlayRasterization.slang` + `Engine/SimulationSpace/PhysicsSpace/ForceFieldPhysicsCoupling.{h,cpp}` + `Exhibits/ForceFieldValidation.cpp` | `ForceFieldValidation` passes `100%`: 64-byte SSBO upload, 100% GPU `.slang` gizmo & vector PNG matches `03_Inspector_ForceModels.png`, and Jolt dynamic body + `VehiclePhysicsRig` wind deflection verified via `PublishedReadingExchange`. |

### Stage C — Phase 2A: Pure 1:1 GPU Particle Simulation & Rendering (Zero Additive Mixing)

| Slice | Exact Upstream Reference (File & Lines) | Native Target(s) | Reference-Generated Oracle & Hard Pass Gate |
| :---: | :--- | :--- | :--- |
| **S07** | `Engine/ParticleCatalog.js` (`L1–L276`) + `Engine/FireworkPatterns.js` (`L1–L623`) | `Engine/ParticleSpace/{ParticleCloudTypes.h,FireworkPatternSampler.{h,cpp},ParticleTemplateRegistry.{h,cpp}}` | All `71` `ParticleTemplates/*.toml` load cleanly; all `10` firework patterns (`ClassicBouquet`..`GrandFinale` + `SkyText`) match JS golden anchor positions to `< 1e-4`. |
| **S08** | `Engine/ObjectMeshCatalog.js` (`L1–L1167`) | `Engine/ParticleSpace/CarrierSolidRegistry.{h,cpp}` | All `19` built-in 3D solids (`UtahTeapot`..`TorusKnot`) + `.GeometrySpace`/`.obj`/`.stl` import + `3` surface sample modes (`EvenSurface`, `SurfaceVolume`, `RidgeAccent`) match JS vertex/normal/anchor golden dumps to `< 1e-4`. |
| **S09** | `Engine/ParticleShaders.js` `ParticleShapeChunk` (`L9–L521`) + `ParticleUpdateFragment` (`L522–L752`) | `Engine/Shaders/ParticleSpace/{ParticleShapeAnchor.slang,ParticleUpdateCompute.slang}` + `Engine/ParticleSpace/ParticleGpuSimulation.{h,cpp}` (core update) | All `16` spawn shapes, `Emit`/`Cloner`, home spring/dissolve, `6` derez modes, `4` mocap clips, `4` MoGraph effectors, Boids/Swarm, ForceField coupling, Gas coupling, and floor/sphere bounce verified against JS golden particle state after `60` steps (`< 1e-3`). |
| **S10** | `Engine/ParticleShaders.js` `ParticleShadeChunk` (`L753–L869`) + `ParticleRenderVertex` (`L870–L966`) + `ParticleFragment` (`L1088–L1340`) | `Engine/Shaders/ParticleSpace/ParticleSpriteRasterization.slang` + `ParticleGpuRenderer.{h,cpp}` (sprites) | All `7` colour modes, `18` analytical sprite cutouts (`0..17`), `Point`/`Streak`, `Alpha`/`Additive`, DOF `FocusDistance`/`Aperture`, and halo glow match reference PNG cutouts. |
| **S11** | `Engine/ParticleShaders.js` `TrailUpdateFragment` (`L967–L993`) + `TrailVertex` (`L994–L1087`) | `Engine/Shaders/ParticleSpace/{ParticleTrailUpdateCompute.slang,ParticleRibbonRasterization.slang}` | Ring buffer up to `96` samples, `1..4` Catmull-Rom subdivisions, `TrailTaper`/`TrailFade`/`TrailRatio`/`Linger` match `05_Particles_RibbonsStrands.png`. |
| **S12** | `Engine/ParticleShaders.js` `ObjectRenderVertex` (`L1341–L1470`) + `ObjectFragment` (`L1471–L1551`) | `Engine/Shaders/ParticleSpace/ParticleCarrierSolidRasterization.slang` | Instanced `Object3D` PBR carriers + all `3` backing surfaces (`ObsidianGlossy`, `BrushedChrome`, `GhostWireframe`) + Derez seam glow match `04_Particles_ObjectDerez.png`. |
| **S13** | `Engine/ParticleShaders.js` `StrandVertex` (`L1552–L1947`) + `StrandFragment` (`L1948–L2073`) + `Engine/ParticleSimulation.js` (`L1–L654`) | `Engine/Shaders/ParticleSpace/ParticleStrandRasterization.slang` + `ParticleGpuRenderer` strands + 1:1 Particle parity suite in `Exhibits/ParticleGpuPhysicsValidation.cpp` | All `11` strand/web/arc modes (`OrbWeaveWeb`..`StormWeb`) × `Primary`/`Secondary` + all `71` templates step and render with zero NaN/errors and pass visual PNG check against `05_Particles_RibbonsStrands.png`. |

### Stage D — Phase 3: Pure 1:1 3D Eulerian + Sparse Infinite Fluid Solver & Volume Renderer

| Slice | Exact Upstream Reference (File & Lines) | Native Target(s) | Reference-Generated Oracle & Hard Pass Gate |
| :---: | :--- | :--- | :--- |
| **S14** | `Engine/FluidShaders.js` `VolumeChunk` (`L22–L97`) + `SparseVolumeChunk` (`L98–L233`) + `ColliderChunk` (`L494–L514`) + `BoundaryChunk` (`L515–L531`) + `Engine/FluidSolver.js` (`L89–L389`) | `Engine/SimulationSpace/Fluid/{FluidRegionTypes.h,SparseTileBank.{h,cpp}}` + `Engine/Shaders/Fluid/FluidVolumeAccess.slang` | Native `VK_IMAGE_TYPE_3D` `RGBA16F` allocation for `Fixed`, `WorldGrid` (`1³..5³`), and `Infinite` (`64×16×64` `R16UI` lookup + occupancy schedule/evict) passes unit coordinate & tile-conservation oracle tests. |
| **S15** | `Engine/FluidShaders.js` `AdvectFragment` (`L532–L553`) + `MacCormackFragment` (`L554–L596`) + `InjectFragment` (`L597–L683`) + `ForcesFragment` (`L684–L736`) | `Engine/Shaders/Fluid/{FluidAdvectCompute.slang,FluidMacCormackCompute.slang,FluidInjectCompute.slang,FluidForcesCompute.slang}` | RK2 + Selle 2008 overshoot-fallback MacCormack monotonicity (`600` steps, zero unbounded spin-up), all `3` source shapes + jitter/pulse, and velocity-saturated force response verified against reference step norms. |
| **S16** | `Engine/FluidShaders.js` `CurlFragment` (`L737–L754`) + `VorticityFragment` (`L755–L779`) + `ThermodynamicsFragment` (`L896–L950`) + `DivergenceFragment` (`L780–L819`) + `PressureFragment` (`L820–L859`) + `GradientFragment` (`L860–L895`) + `ShiftFragment` (`L1000–L1018`) + `OccupancyFragment` (`L1019–L1076`) | `Engine/Shaders/Fluid/{FluidCurlVorticityCompute.slang,FluidThermodynamicsCompute.slang,FluidDivergencePressureCompute.slang,FluidGradientShiftCompute.slang}` + `Engine/SimulationSpace/Fluid/FluidGpuSolver.{h,cpp}` | Ignition `0.9`, expansion `0.08 * state.w`, warm-started Jacobi (`0.96` retain) + screened-diffusion fire light (`6.28`), outflow sponge `mix(0.82, 1.0, keep)`, wall/collider no-penetration (`div < 1e-2`), and `WorldGrid`/`Infinite` relocation pass. |
| **S17** | `Engine/FluidShaders.js` `SceneFragment` (`L1129–L1384`) + `PostOverlayFragment` (`L1758–L1909`) | `Engine/Shaders/Fluid/{FluidVolumeRaymarch.slang,SimulationPostOverlay.slang}` + `Engine/SimulationSpace/Fluid/{FluidVolumeRenderer.{h,cpp},FluidSimulationPostOverlay.{h,cpp}}` | Wrenninge 3-octave scattering, 2-lobe HG, `1.35`-growth sun shadows, powder darkening, fire-lit smoke/floor, ACES tone transform, transparent flipbook bake, plus Heat Shimmer (`109`), Glitch (`217`), and Distortion (`218`) match `01_Startup_DefaultCampfire.png`. |
| **S18** | `Engine/FluidShaders.js` `VectorVertex` (`L1385–L1422`) + `SliceVertex`/`Fragment` (`L1492–L1548`) + `TracerUpdate`/`Render` (`L951–L999`, `L1549–L1588`) + `DebrisUpdate`/`Render` (`L1589–L1740`) + `Engine/VolumeRenderer.js` gizmos (`L995–L1166`) + `Engine/SimulationCheckpoints.js` (`L1–L197`) + `Engine/PresetScenes.js` (`54` presets) | `Engine/Shaders/Fluid/{FluidTracerDebrisCompute.slang,FluidOverlayRasterization.slang}` + `Engine/SimulationSpace/EffectLayers/SimulationCheckpointRing.{h,cpp}` + `Engine/SimulationSpace/Fluid/FluidPresetRegistry.{h,cpp}` + `Exhibits/FluidSolverValidation.cpp` | `FluidSolverValidation` passes `100%`: all `54` `Presets/*.toml` load & step cleanly, `8,192` tracers & debris verified, `192 MiB` checkpoint ring rewind/scrub verified, `06_Fluid_InfiniteSparseGrid.png` parity verified, flipbook transparent PNG sheet exported. |

### Stage E — Phase 4: Pure 1:1 Workspace UI via `Engine/Components/` *(⏸️ Deferred per User Directive — C++ & Headless Tests Only First)*

> **User Scope Directive (`2026-11-23`)**: Stage E UI slices (`S19–S23`) are **paused/deferred** until the user finalizes UI design. Execution proceeds directly from **Stage D (`S18`)** to **Stage F (`S24–S28`)** in pure C++20 + `.slang` + headless validation tests.

| Slice | Exact Upstream Reference (File & Lines) | Native Target(s) | Reference-Generated Oracle & Hard Pass Gate |
| :---: | :--- | :--- | :--- |
| **S19** | `Engine/FluidStudioApp.js` (`L1–L1729`) + `Engine/SceneFolders.js` (`L1–L1110`) | `Engine/Workspaces/Fluid/FluidWorkspaceSession.{h,cpp}` | Full headless scene session (selection, undo/redo checkpoints, playback, preset/template loading, gizmo drag state) compiles and passes session unit assertions. |
| **S20** | `UI/StudioPanels.js` Outliner + Toolbar (`L1–L800`, `L2455–L2645`) | `Engine/Workspaces/Fluid/FluidOutlinerAdapter.{h,cpp}` (drives existing `OutlinerPanel` & `ViewportPanel`) | 6 typed collections, inline eye/enable/delete buttons, 2-row toolbar match `01_Startup_DefaultCampfire.png`. |
| **S21** | `UI/StudioPanels.js` Inspector + Layer Stack Editor (`L801–L2454`) | `Engine/Workspaces/Fluid/FluidInspectorRouter.{h,cpp}` (drives existing `InspectorPanel` + `Engine/Components/Controls/`) | Paged lettered pill tabs (`‹` / `›` + `1 / N`), XYZ transform table, phase-ordered `EffectLayerStack` editor (`101–109`, `201–219`), inline key diamonds, and `ColourRampEditor` match `02_Inspector_GasDomainStack.png` & `03_Inspector_ForceModels.png`. |
| **S22** | `UI/StudioPanels.js` Drawer (`L2646–L3145`) + `UI/PreviewThumbnails.js` (`L1–L956`) + `UI/TimelinePanel.js` (`L1–L539`) | `Engine/Workspaces/Fluid/{FluidDrawerPanel.{h,cpp},FluidTimelineAdapter.{h,cpp},FluidWorkspaceExtension.{h,cpp}}` | Bottom slide drawer (`54` Scenes, `71` Templates, `20` Forces, Flipbook) + Timeline (`SimulationTimelineFooter`, `SimulationTimeRuler`, key diamond edit, Ease/Extrapolation) + `F3 Simulation` telemetry tab match `07_Drawer_PresetsTemplates.png` & `08_Timeline_KeyTracks.png`. |
| **S23** | Full side-by-side HTML vs. Native Parity Sign-Off | `Exhibits/FluidWorkspaceUiValidation.cpp` | **🧱 HARD PARITY WALL**: All `8` Step 0 HTML reference PNGs compared side-by-side against native renders; `ForceFieldValidation`, `ParticleGpuPhysicsValidation` (1:1), `FluidSolverValidation`, and `FluidWorkspaceUiValidation` all `100%` green. |

---

### Stage F — Additive Frontier GPU Physics Extensions (ONLY AFTER `S23` Wall Passes!)

> Strictly opt-in via Frontier additive layer codes **`220` (GPU Orthotropic XPBD)** and **`221` (GPU Particle Collisions)**. Leaving both disabled guarantees bit-identical `1:1` behavior on all `54` HTML presets and `71` HTML templates.

| Slice | Exact Upstream Reference (File & Lines) | Native Target(s) | Reference-Generated Oracle & Hard Pass Gate |
| :---: | :--- | :--- | :--- |
| **S24** | Existing `Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.{h,cpp}` (`80`-byte `RigidColliderProxy` SSBO) + `FluidShaders.js` `ColliderChunk` (`L494–L514`) | `Engine/Shaders/ParticleSpace/ParticleCollisionCompute.slang` + `ForceFieldPhysicsCoupling` two-way impulse readback | Particles bounce with restitution + Coulomb friction + surface velocity off live Jolt `Sphere`, `OrientedBox`, `Capsule`, and baked `CompoundHull` SDFs; atomic impulse readback deflects dynamic Jolt rigid bodies on `PhysicsThread`. |
| **S25** | Uniform 3D Spatial-Hash Grid (`CountCells` → Blelloch `PrefixSum` → `Scatter` → `Resolve27Neighbors`) | `Engine/Shaders/ParticleSpace/ParticleSpatialHashCompute.slang` | `10,000+` colliding spheres settle under gravity in a box with `< 1%` overlap, exact momentum/friction response, and `0` NaNs over `300` steps. |
| **S26** | `Experimental/ClothEditor/Engine/WebGpuClothAccelerator.js` (`L9–L100` `packGpuNetworkTables`, `L201–L435` `predictInertia`/`resetXpbdDuals`/`solveXpbdSeamBatch`/`solveXpbdFaceBatch`/`solveXpbdHingeBatch`, `L609–L653` `finalizeSubstep`) | `Engine/ParticleSpace/ParticleXpbdNetworkBuilder.{h,cpp}` + `Engine/Shaders/ParticleSpace/ParticleXpbdCompute.slang` | Conflict-colored zero-atomic GPU XPBD matches `ClothEditor` reference XPBD vertex positions on a pinned `16×16` orthotropic `ClothGrid` under gravity + wind (`< 1e-3`), plus stable `Spiderweb` / `CablesChains` / `FloraBed` strands over `4` substeps × `16` iterations + unilateral contact against Jolt `RigidColliderProxy`. |
| **S27** | `LayerSpecifications.VolumetricSpace` additive codes `220` & `221` + UI controls in `FluidInspectorRouter` | `EngineContent/Simulation/LayerSpecifications.VolumetricSpace` + `ParticleGpuPhysicsValidation.cpp` (full GPU physics suites) | `ParticleGpuPhysicsValidation` passes `100%` (both all `71` 1:1 HTML templates unchanged AND new XPBD Cloth/Web + Jolt two-way + spatial-hash self-collision suites). |
| **S28** | Phase 5 Codecs & End-to-End Integration | `Engine/Codecs/Simulation/{ForceFieldCodec,FluidGridCodec,ParticleSpaceCodec}.{h,cpp}` + `CMakeLists.txt` + `EditorLifecycle.cpp` | `.ForceField`, `.FluidGrid`, `.VolumetricSpace`, and `.ParticleSpace` round-trip byte-identical; zero `BannedWordsVocabulary.md` / `And` violations across all added files; all 4 new `CTest` validation executables + existing physics/engine suites green. |
