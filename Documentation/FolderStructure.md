# Frontier Folder Structure

This document defines the canonical directory and source file layout of the **Frontier** repository. All engine domains, `.Space` codecs, external interchange modules, `.slang` shader libraries, default stock repositories, validation suites, build tooling, user projects, and governance documents adhere to this structure.

For identifier, file, folder, and comment naming rules enforced across the repository, see [BannedWordsVocabulary.md](BannedWordsVocabulary.md) and [AgenticInstructions/SKILL-Naming.md](AgenticInstructions/SKILL-Naming.md). For C++20 language and modularity rules, see [CodingStandards.md](CodingStandards.md).

---

## Top-Level Directory Summary

| Path | Purpose |
| :--- | :--- |
| `CLAUDE.md` · `AGENTS.md` | Session-start agent entry point, non-negotiable GPU/`.slang`/cross-platform mandates, and reading order. |
| `CMakeLists.txt` | ISO C++20 build definition enforcing `Editor`, `Development`, and `Shipping` profiles. |
| `EngineContent/` | Engine default stock assets, vector icons, font repositories (`FontRepository.FontSpace`), PBR material definitions, worlds, and graphics content. |
| `EngineDocs/` | Authoritative architectural guides and deep domain references. |
| `Documentation/` | Repository governance, `AgenticInstructions/`, `Plans/`, `FolderStructure.md`, `BannedWordsVocabulary.md`, `CodingStandards.md`, `ImportantNotes.md`, `Backlog.md`, and `Tracker.md`. |
| `Exhibits/` | Validation executables (`*Validation`), `Images/` CPU-rendered `.png` visual snapshots, and `EditorMirror` verification targets. |
| `Experimental/` | HTML/JS-only reference editor UIs (`ProjectZeroEditor`, `FrontierEditor`) and domain studios (`FluidEditor`, `FractureEditor`, `ParticleEditor`, `PhotometricEditor`, `ForceFieldEditor`, `VehicleLab`, `UVEditor`, `TextureStudio`) staged for native C++20 + `.slang` conversion. |
| `Automation/` | Dependency pinning (`Dependencies.json`), ImGui 8-patch stack (`Patches/`), `.slang` toolchain setup, Markdown Studio tooling, CMake modules, and binary packaging. |
| `ExternalPackages/` | Checksum-verified third-party packages (`imgui`, `thorvg`, `tomlpp`, `glfw`, `vulkan-headers`, `volk`, `stb`, `JoltPhysics`, `VulkanCompute/`). Git-ignored; populated by `Automation/`. |
| `_ClaudeScratch/` | Ignored local scratchpad and transient planning notes (excluded from version control). |
| `Projects/` | Container for concrete user projects (such as `SampleProject/`), keeping project assets and logic isolated from engine source. |
| `Engine/` | Portable C++20 engine source tree (Windows, Linux, Android), `.Space` codecs, external interchange, `.slang` shaders, headless workspaces, UI components, and `Editor/`. |

---

## Canonical Repository Tree

```text
Frontier/
├── CLAUDE.md                                      // Primary agent instructions & non-negotiable mandates
├── AGENTS.md                                      // Universal coding-agent entry point
├── CMakeLists.txt                                 // Root C++20 build & Editor/Development/Shipping profiles
├── README.md                                      // Repository overview and architecture index
│
├── EngineContent/                                 // Engine default assets and stock repositories
│   ├── Geometry/                                  // Default .GeometrySpace assets (ControlledIntegrator.GeometrySpace)
│   ├── Icons/                                     // Vector SVGs and compiled icon repositories
│   ├── FontsArchives/                             // Primary UI and monospace typefaces (FontRepository.FontSpace)
│   ├── Materials/                                 // Microfacet PBR material definitions (.MaterialSpace)
│   ├── Worlds/                                    // Levels and world content
│   └── Graphics/                                  // Image (.ImageSpace) and vector (.VectorSpace) content
│
├── EngineDocs/                                    // Authoritative architectural guides & domain references
│
├── Documentation/                                 // Repository governance, active plans, and engineering tracking
│   ├── AgenticInstructions/                       // Mandatory skill protocols (imported & harmonized)
│   │   ├── SKILL-CompactOutputProtocol.md         // Skimmer-first output & mandatory decision tables
│   │   ├── SKILL-Emoji.md                         // Approved emoji vocabulary & rating markers
│   │   ├── SKILL-Naming.md                        // Mechanism-first naming construction & 6-gate check
│   │   └── SKILL-Formatting.md                    // File banners, section rulers, unit tags, Allman braces
│   ├── Plans/                                     // Architectural & domain implementation plans + Markdown Studio
│   │   ├── DocumentFormatsAndInstancePlan.md      // Two-Tier Hybrid .Space format registry & Instance concepts
│   │   ├── DocumentSpacesAndRevisionPlan.md       // Confirmed .Space codecs, 215/750 MiB revision & workspace tree
│   │   ├── ThreadingArchitecturePlan.md           // Priority-budgeted ExecutionChannels, domain threads & DebugThread watchdog
│   │   ├── ContentBrowserArchitecturePlan.md      // Pre-baked ThumbnailSheet, SSD/HDD streaming, quarantine import & UV masks
│   │   ├── UVWorkspaceArchitecturePlan.md         // Native C++20 & .slang UV Workspace architecture & completion ledger
│   │   ├── UVWorkspaceSyncAndViewportReport.md    // Single-concept naming & reusable ViewportProjectionPane report
│   │   ├── UVWorkspaceFullCompletionReport.md     // 6-pillar UV Workspace completion, desktop executables & verification report
│   │   ├── SessionNotes.md                        // Live Markdown Studio workspace notepad
│   │   └── MarkdownStudioViewer.html              // Standalone/live Frontier-styled Markdown Studio bundle
│   ├── FolderStructure.md                         // Canonical repository folder and source layout
│   ├── BannedWordsVocabulary.md                   // Blacklisted terminology and naming governance
│   ├── CodingStandards.md                         // ISO C++20 standard, prototypes, concepts & templates
│   ├── ImportantNotes.md                          // Hard architectural invariants & reference rules
│   ├── Backlog.md                                 // Deferred engineering items ledger
│   └── Tracker.md                                 // Active milestone roadmap & bringup status
│
├── Exhibits/                                      // Validation test suites, visual snapshots, and EditorMirror
│   ├── Images/                                    // CPU-rendered .png visual snapshots for every validation
│   ├── SoftwareCanvasRasterizer.h                 // CPU ImDrawData & RGBA8 PNG software rasterizer
│   ├── InputValidation.cpp                        // Deterministic Input & BuildConfiguration verification
│   ├── WindowValidation.cpp                       // Splash, windowed, maximized & fullscreen mode verification
│   ├── DockingValidation.cpp                      // Trapezoidal multi-window & intra-app docking verification
│   ├── ControlCentreValidation.cpp                // Control Centre, Controls, Tooltips & .ProjectSpace verification
│   ├── WorkspacePanesValidation.cpp               // Outliner/LayerStack, Showcase Folder/Entry Inspector & Viewport
│   ├── LifecycleValidation.cpp                    // LifecycleSequence, RuntimeLifecycle, WorkspaceLifecycle & EditorLifecycle
│   ├── WorkspaceRevisionValidation.cpp            // .Space codecs, 215/750 MiB revision & priority ExecutionChannels
│   ├── ContentBrowserValidation.cpp               // ContentBrowser, ColourCodex, Colour Filters & Vertical/Horizontal strips
│   ├── GeometryNetworkValidation.cpp              // Half-edge PolygonNetwork, FacePartitionRegister & studio sample presets
│   ├── UVRasterPickingGizmoValidation.cpp         // Vulkan 1.2 .slang 3D/2D UV rasterizer, GPU pick & TransformGizmo
│   ├── UVWorkspaceUIValidation.cpp                // UVOutlinerPanel, 5-tab UVInspectorPanel, ControlCentre & docking
│   ├── UVParameterizationValidation.cpp           // SeamCut, LSCM, GPU StretchRelax, ConcaveSkyline, .UVSpace & Revision
│   └── UVInteractiveAuthoringValidation.cpp       // Interactive 2D/3D authoring, ABF++/Projections, OBJ/glTF & Vulkan swapchain
│
├── Experimental/                                  // HTML/JS reference UIs & studios for C++20 conversion
│   ├── FrontierEditor/                            // Initial HTML/Vite scene inspector, CAD & icon lab UI
│   ├── ProjectZeroEditor/                         // Canonical Project-Zero UI reference + README render
│   ├── FluidEditor/                               // Gas, plume, combustion, and liquid solver prototype
│   ├── FractureEditor/                            // Voronoi, convex partition, SDF, and debris prototype
│   ├── ParticleEditor/                            // GPU compute particle, fibre, and lightning studio
│   ├── PhotometricEditor/                         // LM-63 .ies luminaire, sky, and isolux metering studio
│   ├── ForceFieldEditor/                          // Wind generator and 16 spatial force-field studio
│   ├── VehicleLab/                                // ControlVehicle FCT1, powertrain, and MF6.1 tyre lab
│   ├── UVEditor/                                  // LSCM unwrap, UDIM layout, and importance packer studio
│   └── TextureStudio/                             // 5-workspace material, pattern, stamp, paint & SDF baker
│
├── .github/workflows/                             // GitHub Actions (run only with explicit user approval)
│   ├── uv-workspace-ci.yml                        // Linux C++20 + Vulkan CTest regression across three profiles
│   └── windows-desktop-package.yml                // MSVC x64 package, provenance attestation, optional Authenticode
│
├── Automation/                                    // Build tooling, dependency pinning, patches, and studio server
│   ├── Dependencies.json                          // SHA-256 pinned C/C++ package manifest
│   ├── PrepareDependencies.py                     // Checksummed package fetcher & ImGui patch runner
│   ├── ApplyImGuiPatches.py                       // Stacked PatchA-PatchH applicator and verifier
│   ├── PrepareShaderToolchain.py                  // Slang (.slang) compiler & SwiftShader ICD fetcher
│   ├── PrepareFonts.py                            // OFL font fetcher & PascalCase FontRepository.FontSpace builder
│   ├── BuildMarkdownViewer.py                     // Frontier-styled Markdown Studio HTML bundle generator
│   ├── ServeMarkdownStudio.py                     // Live 0.0.0.0:8080 Markdown Studio & Session Notepad server
│   ├── FontSources.json                           // Pinned OFL typeface manifest
│   ├── FontRequirements.txt                       // Python font tooling pin
│   ├── StageThorVG.py                             // Reproducible ThorVG allocation-operator preparation
│   ├── ThorVGSoftware.cmake                       // Static ThorVG build definition
│   ├── JoltPhysics.cmake                          // Deterministic x86_64/ARM64 JoltPhysics v5.6.0 definition
│   ├── WindowsToolchain.cmake                     // Zig 0.13.0 Windows x64 cross-compile toolchain
│   ├── AuditBannedVocabulary.py                   // BannedWordsVocabulary.md scanner (identifiers, comments, path names)
│   ├── PackageDesktopBinaries.py                  // Relocatable FrontierUV / FrontierEditor folder collector & bundler
│   └── Patches/                                   // Mandatory 8-patch ImGui stack (PatchA through PatchH)
│
├── ExternalPackages/                              // Third-party external dependencies (git-ignored)
│
├── _ClaudeScratch/                                // Ignored scratchpad and transient planning notes
│
├── Projects/                                      // User project workspace container
│   └── SampleProject/                             // Concrete game project
│       ├── Content/                               // Project-specific .Space assets (levels, textures, audio)
│       ├── Source/                                // Project-specific C++ logic
│       └── Saved/                                 // Local project logs and saved sessions
│
└── Engine/                                        // Engine Source Root (Windows · Linux · Android)
    ├── MicroUtils/                                // Foundational allocators and identity tokens
    │   ├── RegionAllocator.{h,cpp}                // Monotonic non-freeing block allocator
    │   ├── TokenIssuer.h                          // Stale-detecting monotonic ID generator
    │   ├── TokenAuthentication.h                  // Validation primitives for issued handles
    │   ├── PackedBits.h                           // Contiguous packed-bit operations and bit-range queries
    │   ├── IndexedSlotRecycler.h                  // Preallocated indexed instance slot recycler
    │   └── Assertion.h                            // Invariant enforcement macros and diagnostics
    │
    ├── Configuration/                             // Startup, build-profile, and .ProjectSpace configurations
    │   ├── BuildConfiguration.h                   // Compile-time Editor / Development / Shipping policy
    │   ├── InputSettings.{h,cpp}                  // Shortcut chords, conflict validator & pointer sensitivity
    │   ├── ProjectSpaceRepository.{h,cpp}         // Atomic PascalCase .ProjectSpace / .projectspace serializer
    │   ├── EngineConfiguration.{h,cpp}            // Startup and hardware ExecutionChannels allocation
    │   ├── RenderConfiguration.{h,cpp}            // Resolution, swapchain, and multi-sample targets
    │   ├── ProjectConfiguration.{h,cpp}           // Project root, dependency manifests, and toggles
    │   └── WorkspaceConfiguration.{h,cpp}         // Document session preferences and layout defaults
    │
    ├── Constants/                                 // Shared compile-time physical, mathematical, engine & time constants
    │   ├── MathConstants.h                        // Pi, Tau, Epsilon, DegToRad, RadToDeg & IEEE-754 bounds
    │   ├── PhysicsConstants.h                     // Gravity, AirDensity, WaterDensity & FrictionLimits
    │   ├── EngineConstants.h                      // MaxPlayers, DefaultTickRate, BufferCapacities & Limits
    │   ├── TimeConstants.h                        // FixedDeltaTime, MicrosecondsPerSecond & WatchdogThresholds
    │   ├── OpticalConstants.h                     // VacuumLightSpeed, PlanckConstant, CIEWhitePoints & IOR
    │   └── AcousticConstants.h                    // ReferenceSoundPressure, DryAirSoundSpeed & OctaveBands
    │
    ├── Threading/                                 // Priority-budgeted execution, lock-free sync & domain threads
    │   ├── Tasks/                                 // Non-blocking task routing, priority channels & cluster
    │   │   ├── TaskScheduler.{h,cpp}              // 4-tier priority & remaining-slice tick budget scheduler
    │   │   ├── TaskQueue.h                        // Cache-aligned lock-free work-stealing task deque
    │   │   ├── TaskChannel.{h,cpp}                // Typed priority intake & continuation routing channel
    │   │   └── ExecutionChannels.{h,cpp}           // Hardware-pinned parallel task execution cluster
    │   ├── Sync/                                  // Lock-free and leaf synchronization primitives
    │   │   ├── SpinLock.h                         // Leaf-only exponential-backoff atomic spin lock
    │   │   ├── AtomicQueue.h                      // Bounded lock-free MPMC / SPSC ring queue
    │   │   ├── CriticalSection.h                  // OS adaptive sleep-capable mutual exclusion primitive
    │   │   ├── CompletionCounter.h                // Wait-free atomic countdown & continuation trigger
    │   │   └── ThreadProgressionTracker.{h,cpp}           // Cache-line-isolated thread liveness & stall detector
    │   └── Threads/                               // Dedicated domain & diagnostic thread loops
    │       ├── Game/
    │       │   └── GameThread.{h,cpp}             // Deterministic world ticks, instance logic & gameplay rules
    │       ├── Workspace/
    │       │   └── WorkspaceThread.{h,cpp}        // Async .Space authoring, RevisionTracker & solver execution
    │       ├── Physics/
    │       │   └── PhysicsThread.{h,cpp}          // Fixed-step rigid dynamics, constraint solver & collisions
    │       ├── Render/
    │       │   └── RenderThread.{h,cpp}           // GPU command encoding, stream upload & VulkanInterchange
    │       ├── Audio/
    │       │   └── AudioThread.{h,cpp}            // Real-time lock-free AudioBusAccumulator & ring streaming
    │       ├── Editor/
    │       │   └── EditorThread.{h,cpp}           // Responsive UI docking, viewport input & tool triggers
    │       ├── Debug/
    │       │   ├── DebugThread.{h,cpp}            // Dedicated crash/hang watchdog, async log sink & terminal
    │       │   └── ProfilerThread.{h,cpp}         // Lock-free cycle metrics, telemetry & memory region tracking
    │       ├── Network/
    │       │   └── NetworkThread.{h,cpp}          // Non-blocking socket poll, replication & packet transport
    │       └── Streaming/
    │           └── AssetStreamThread.{h,cpp}      // Asynchronous disk reads & .Space container decoding
    │
    ├── Mathematics/                               // Shared algebraic, polynomial, sparse-linear & deterministic math
    │   ├── ScalarFunctions.h                      // Branchless clamp, saturate, smoothstep, remap & fast reciprocal/rsqrt
    │   ├── LinearAlgebra.h                        // SIMD-aligned 2D/3D/4D vector and matrix types
    │   ├── VectorAlgebra.{h,cpp}                  // Dot, cross, Gram-Schmidt orthonormalization, reflection & refraction
    │   ├── MatrixOperations.{h,cpp}               // Affine, projective, inverse, transpose & normal-transpose solvers
    │   ├── QuaternionRotation.{h,cpp}             // Unit quaternions, spherical slerp & orthogonal basis conversion
    │   ├── PolynomialCurves.{h,cpp}               // Analytic Bézier, Hermite, Catmull-Rom, B-spline & root solvers
    │   ├── SparseLinearSolver.{h,cpp}             // Conjugate gradient & sparse Cholesky solvers (shared by UV & physics)
    │   ├── TrigonometricFunctions.h               // SIMD minimax sine, cosine, atan2 & spherical coordinate conversions
    │   └── DeterministicRandom.h                  // Seeded PCG / SplitMix64 repeatable generator for parametric replay
    │
    ├── MetricSpace/                               // Coordinate spaces, tolerances, projections & spatial intersections
    │   ├── MetricTolerance.h                      // Linear, angular, UV-weld & watertight sewing tolerances
    │   ├── CoordinateSpace.h                      // Right-handed world, local & parameter-tile basis transforms
    │   ├── CoordinateProjection.h                 // Orthographic, perspective & 2D viewport tile projections
    │   ├── DistanceMetric.h                       // Metric evaluation (Euclidean, Manhattan, Chebyshev, geodesic)
    │   ├── SpatialExtent.h                        // Axis-aligned and oriented bounding extents
    │   └── IntersectionSolver.{h,cpp}             // Ray-facet, ray-volume, plane-cut & volume-volume solvers
    │
    ├── SpatialAcceleration/                       // Broad-phase spatial acceleration structures
    │   ├── BoundingVolumeTree.{h,cpp}             // Area-heuristic binary bounding volume split tree
    │   ├── LooseOctree.{h,cpp}                    // Expanded boundary volumetric spatial tree
    │   ├── DynamicAABBTree.{h,cpp}                // Self-balancing binary tree for dynamic colliders
    │   └── SpatialGridHash.{h,cpp}                // Sparse spatial hashing structure
    │
    ├── DistanceFieldSpace/                        // Volumetric SDFs, implicit geometry, and queries
    │   ├── SignedDistanceField.{h,cpp}            // Voxel-backed signed distance fields
    │   ├── DistanceTransform.{h,cpp}              // Fast sweeping and Maurer distance transforms
    │   ├── VolumetricDistance.{h,cpp}             // Trilinear volume sampling and gradient reconstruction
    │   ├── DistanceQuery.{h,cpp}                  // Ray marching and closest-point queries
    │   └── SDFPrimitiveSolver.{h,cpp}             // Analytic primitive distance solvers
    │
    ├── ColourSpace/                               // Tristimulus spaces, chromaticity, and transfer curves
    │   ├── ChromaticitySpace.{h,cpp}              // CIE XYZ, Linear sRGB, ACEScg, Rec.2020, and Oklab transforms
    │   ├── TransferFunction.{h,cpp}               // Electro-optical and opto-electronic curves (sRGB, PQ, HLG)
    │   ├── WhitePointAdaptation.{h,cpp}           // Bradford and CAT02 chromatic adaptation (D65, D60, D50)
    │   ├── SpectralDistribution.{h,cpp}           // Wavelength tristimulus integration and Planckian Kelvin
    │   └── GamutCompression.{h,cpp}               // Out-of-gamut perceptual chroma compression
    │
    ├── GeometrySpace/                             // Analytic primitives, geometric analysis, selection & UV
    │   ├── Primitives/                            // Parametric solid primitives & watertight sewing
    │   │   ├── AnalyticPrimitives.{h,cpp}         // Box, cylinder, cone, frustum, sphere, torus & capsule
    │   │   ├── RevolutionSweepPrimitives.{h,cpp}  // Lathe revolution, linear extrusion & profile sweeps
    │   │   └── WatertightSewing.{h,cpp}           // Boundary edge welding, facet orientation & solid closure
    │   │
    │   ├── Analysis/
    │   │   ├── CurvatureAnalysis.{h,cpp}          // Discrete mean and Gaussian differential curvature
    │   │   └── StretchMeasurement.{h,cpp}         // Per-facet and island angle/area distortion metrics
    │   │
    │   ├── Selection/
    │   │   ├── GeometrySelection.{h,cpp}          // Component pick resolution (Point, Edge, Facet, Loop)
    │   │   ├── ComponentSelectionMask.{h,cpp}     // Bitmask filters for multi-component picking & paint masks
    │   │   └── IslandFloodSelection.{h,cpp}       // Seam-bounded island flood expansion & UVTile tagging
    │   │
    │   └── UV/                                    // Parameterization & UVTile layout solvers
    │       ├── TileLayout.{h,cpp}                 // Integer [TileU, TileV] UVTile coordinate grid
    │       ├── UDIMTileAddressing.{h,cpp}         // UDIM 1001-series addressing and coordinate conversion
    │       ├── TexelLayout.{h,cpp}                // Target texel density [px/m] and pixel border padding
    │       ├── TileSegmentation.{h,cpp}           // Seam-split UVTile island extraction & boundary loops
    │       ├── SeamRouter.{h,cpp}                 // Dijkstra geodesic shortest-path & dihedral seam router
    │       ├── ConformalFlattening.{h,cpp}        // LSCM and ABF++ linear parameterization solvers
    │       ├── StretchRelaxer.{h,cpp}             // Iterative area-preserving mass-spring UV relaxer
    │       └── IslandLayoutPacker.{h,cpp}         // Scanline bin-packer targeting active UDIM UVTiles
    │
    ├── Codecs/                                    // Native Frontier .Space container Encoders & Decoders
    │   ├── SpaceContainerCodec.{h,cpp}            // Hybrid TOML + 64B-aligned binary section Encoder/Decoder
    │   ├── RevisionCodec.{h,cpp}                  // .Revision parametric operation stream Encoder/Decoder
    │   ├── ProjectCodec.{h,cpp}                   // ProjectEncoder / ProjectDecoder (.ProjectSpace)
    │   ├── FontCodec.{h,cpp}                      // FontEncoder / FontDecoder (.FontSpace)
    │   ├── ColourCodec.{h,cpp}                    // ColourEncoder / ColourDecoder (.ColourSpace)
    │   ├── GeometryCodec.{h,cpp}                  // GeometryEncoder / GeometryDecoder (.GeometrySpace)
    │   ├── UVCodec.{h,cpp}                        // UVEncoder / UVDecoder (.UVSpace & .UVTile)
    │   ├── TextureCodec.{h,cpp}                   // TextureEncoder / TextureDecoder (.TextureSpace)
    │   ├── MaterialCodec.{h,cpp}                  // MaterialEncoder / MaterialDecoder (.MaterialSpace)
    │   ├── ImageCodec.{h,cpp}                     // ImageEncoder / ImageDecoder (.ImageSpace)
    │   ├── VectorCodec.{h,cpp}                    // VectorEncoder / VectorDecoder (.VectorSpace)
    │   ├── SimulationCodec.{h,cpp}                // SimulationEncoder / SimulationDecoder (.SimulationSpace)
    │   ├── VolumetricCodec.{h,cpp}                // VolumetricEncoder / VolumetricDecoder (.VolumetricSpace)
    │   └── ParticleCodec.{h,cpp}                  // ParticleEncoder / ParticleDecoder (.ParticleSpace)
    │
    ├── Interchange/                               // External industry format import/export & OS clipboard exchange
    │   ├── GeometryInterchange.{h,cpp}            // OBJ, glTF 2.0, FBX & STEP external geometry import/export
    │   ├── UVInterchange.{h,cpp}                  // External UDIM layout & UVTile sheet import/export
    │   ├── ImageInterchange.{h,cpp}               // OpenEXR, PNG, Radiance HDR, WebP & KTX2 import/export
    │   ├── TextureInterchange.{h,cpp}             // BCn / ASTC compressed mipchain external interchange
    │   ├── MaterialInterchange.{h,cpp}            // MaterialX & physical shading parameter interchange
    │   ├── VectorInterchange.{h,cpp}              // SVG 1.1 & PDF Bézier contour interchange
    │   ├── FontInterchange.{h,cpp}                // TrueType / OpenType sfnt outline & metric interchange
    │   └── ClipboardExchange.{h,cpp}              // Bi-directional OS ↔ Workspace clipboard exchange
    │
    ├── GeometricRaster/                           // Vulkan interchange, GPU rasterization units & command encoding
    │   ├── Compute/
    │   │   ├── ComputeDispatchEncoder.{h,cpp}     // Generic .slang compute dispatch & storage-buffer encoder
    │   │   └── TimelineTransferRegister.{h,cpp}   // Triple-buffered Vulkan timeline semaphore readback ring
    │   ├── Picking/
    │   │   └── ViewportPickQuery.{h,cpp}          // R32G32B32A32_UINT 1x1 hover/click & GPU marquee/brush reduction
    │   ├── UV/
    │   │   └── UVWorkspaceRasterizer.{h,cpp}      // 3D PBR/MatCap + 2D UDIM grid, backdrops, seams, pins & heatmaps
    │   ├── Shaders/                               // Active Vulkan 1.2 SPIR-V .slang graphics & compute units
    │   │   ├── SurfaceShading.slang               // 3D Cook-Torrance PBR + 16 procedural MatCaps + selection tint
    │   │   ├── MatCapEvaluation.slang             // Procedural 16-sphere MatCap shading & ACES Filmic compression
    │   │   ├── UVTileRasterization.slang          // 2D UDIM grid, procedural backdrops, coral seams & pin beacons
    │   │   ├── GizmoRasterization.slang           // 3D/2D Slate TransformGizmo cones, cylinders, quads & arcs
    │   │   ├── ImGuiOverlayRasterization.slang    // Premultiplied-alpha ImDrawData swapchain/offscreen rasterization
    │   │   ├── SelectionMaskCompute.slang         // GPU face/edge selection bitmask updates
    │   │   ├── ViewportPickReduction.slang        // GPU box-marquee & circle-brush pick reduction
    │   │   ├── UVDistortionInspection.slang       // Parallel stretch, texel density, flip & UDIM overlap reduction
    │   │   ├── StretchRelaxCompute.slang          // Area-preserving GPU spring relaxation (honours EnablePinLock)
    │   │   └── ConcaveSkylineCompute.slang        // Multi-angle island upper/lower skyline evaluation
    │   ├── RenderExtension.{h,cpp}                // Render lifecycle coordinator and device attachment
    │   ├── RenderDeviceInterface.h                // GPU hardware device interface and capability limits
    │   ├── VulkanInterchange.{h,cpp}              // Vulkan 1.2 timeline semaphore device, swapchain & SPIR-V interchange
    │   ├── RenderScheduler.{h,cpp}                // Transient dependency-sorted GPU workload coordinator
    │   ├── IndirectCommandEncoder.{h,cpp}         // Instanced and indirect GPU command encoding
    │   ├── DescriptorSlotRegister.{h,cpp}         // Descriptor layout and push-constant slot register
    │   │
    │   ├── Rasterization/                         // Dedicated GPU rasterization & projection units
    │   │   ├── ImGuiOverlayRasterization.{h,cpp}  // Native .slang ImDrawData swapchain & offscreen RGBA rasterizer
    │   │   ├── DirectionalLitRasterization.{h,cpp}// Fast directional key/hemisphere lit viewport rasterization
    │   │   ├── UVTileRasterization.{h,cpp}        // 2D UVTile grid, boundary lines, UDIM indices & tile fills
    │   │   ├── EdgeHighlightRasterization.{h,cpp} // Seam lines, sharp creases & active wireframe borders
    │   │   ├── GroundPlaneRasterization.{h,cpp}   // Viewport reference plane rasterization
    │   │   ├── WorldAxisGridRasterization.{h,cpp} // 3D coordinate crosshair & origin distance grid
    │   │   └── ShadowDepthProjection.{h,cpp}      // Directional light-space depth projection
    │   │
    │   ├── ExecutionSequence/                     // Stream uploaders & ordered GPU raster sequences
    │   │   ├── GeometryVertexStreamUploader.{h,cpp}// UV vertex and per-island GPU stream upload
    │   │   └── GeometryRasterSequence.{h,cpp}     // Execution order: ShadowDepth → GroundPlane → Model → UVOverlay
    │   │
    │   └── PostProcess/                           // Ordered image operations
    │       ├── PostProcessSequence.{h,cpp}        // Ordered image operation execution sequence
    │       ├── RadiometricToneStack.{h,cpp}       // Exposure, ACES gamut transform, and colour grading stack
    │       ├── VeilingGlareFilter.{h,cpp}         // Multi-scale luminance threshold and aperture glare scatter
    │       └── AmbientOcclusionFilter.{h,cpp}     // Bilateral depth-aware spatial filtering
    │
    ├── Shaders/                                   // GPU Slang (.slang) shader library and compiler bridge
    │   ├── Compiler/
    │   │   ├── ShaderCompiler.{h,cpp}             // Offline and runtime Slang (.slang) to SPIR-V compiler
    │   │   ├── ShaderReflection.{h,cpp}           // Vertex attribute and descriptor layout reflection
    │   │   └── ShaderPermutationIndex.{h,cpp}     // Precompiled variant permutation register
    │   ├── UV/                                    // GPU UVTile, wireframe, ground plane & shadow shaders
    │   │   ├── UVTileRasterization.slang          // 2D UVTile coordinates, checker patterns & stretch heat
    │   │   ├── EdgeHighlightRasterization.slang   // Screen-space anti-aliased wireframes & seam lines
    │   │   ├── GroundPlaneRasterization.slang     // Procedural infinite ground plane & axis shader
    │   │   └── ShadowDepthProjection.slang        // Directional light-space depth vertex projection
    │   ├── Raster/                                // Fast directional lit, PBR microfacet, and depth pre-raster shaders
    │   │   ├── DirectionalLitShading.slang        // Fast viewport directional key/rim/hemisphere lit shader
    │   │   ├── MicrofacetShading.slang            // Physical microfacet PBR shader
    │   │   └── DepthPreRaster.slang               // Early-Z depth pre-rasterization shader
    │   ├── Compute/                               // General GPU compute shaders
    │   │   ├── ParticleIntegration.slang
    │   │   ├── FluidAdvection.slang
    │   │   └── VoxelDistanceSweep.slang
    │   └── ImageOperations/                       // Post-process image filter shaders
    │       ├── DisplayToneTransfer.slang
    │       └── LuminanceGlareDownsample.slang
    │
    ├── SimulationSpace/                           // Physics, dynamics, particles, fluids & motion
    │   ├── PhysicsSpace/                          // Rigid mechanics and constraints
    │   │   ├── PhysicsExtension.{h,cpp}           // Simulation loop coordinator and broad-phase attachment
    │   │   ├── RigidBodyDynamics.{h,cpp}          // Velocity Verlet integration and inertia tensors
    │   │   ├── ConstraintSolver.{h,cpp}           // Projected Gauss-Seidel iterative velocity solver
    │   │   ├── BroadphaseSolver.{h,cpp}           // Sweep-and-prune dynamic bounding query
    │   │   └── CollisionDetection.{h,cpp}         // GJK narrow-phase & EPA contact extraction
    │   ├── ParticleSimulation/                    // High-density particulate dynamics
    │   │   ├── ParticleSpawner.{h,cpp}            // Point, volume, and boundary particle spawning
    │   │   ├── ParticleField.{h,cpp}              // Curl noise, rotational, and gravitational vector fields
    │   │   ├── ParticleIntegrator.{h,cpp}         // High-performance Euler and Verlet particle solvers
    │   │   └── ComputeParticleBuffer.{h,cpp}      // GPU-side direct compute buffer synchronization
    │   ├── FluidSimulation/                       // Multiphase liquids and gases
    │   │   ├── LiquidSolver.{h,cpp}               // Incompressible SPH liquid solver
    │   │   ├── GasDynamics.{h,cpp}                // Eulerian grid smoke, fire, and atmosphere solver
    │   │   ├── VorticityConfinement.{h,cpp}       // Turbulent detail and rotational energy preservation
    │   │   ├── AdvectionSolver.{h,cpp}            // Semi-Lagrangian and MacCormack advection schemes
    │   │   └── FluidBoundarySolver.{h,cpp}        // Dynamic collision interaction with solid geometry
    │   └── Motion/                                // Procedural interpolation, dynamics, and timelines
    │       ├── MotionSolver.{h,cpp}               // Kinetic trajectory & motion resolver
    │       ├── Interpolation/
    │       │   ├── ScalarInterpolation.{h,cpp}
    │       │   ├── VectorInterpolation.{h,cpp}
    │       │   └── ColourInterpolation.{h,cpp}
    │       ├── Easing/
    │       │   ├── EasingProfile.{h,cpp}
    │       │   ├── BezierEasing.{h,cpp}
    │       │   └── EaseRepository.{h,cpp}         // Precomputed response curves
    │       ├── Dynamics/
    │       │   ├── SpringDynamics.{h,cpp}
    │       │   ├── InertiaDynamics.{h,cpp}
    │       │   └── DampingDynamics.{h,cpp}
    │       └── Sequencing/
    │           ├── TransitionDescriptor.h
    │           ├── SequenceAnimator.{h,cpp}
    │           └── TimelineAnimator.{h,cpp}
    │
    ├── Input/                                     // Bi-directional peripheral, touch, and OS feedback exchange
    │   ├── InputExtension.{h,cpp}                 // Input lifecycle coordinator and snapshot consolidation
    │   └── InputExchange.{h,cpp}                  // OS->Engine signal intake & Engine->OS cursor/haptic exchange
    │
    ├── Windowing/                                 // Native cross-platform window presentation
    │   ├── PlatformWindow.h                       // Window attribute mask, presets & mode-switch interface
    │   ├── PlatformWindowDesktop.cpp              // GLFW 3.4 Win32, X11, Wayland & offscreen presentation
    │   ├── PlatformWindowWin32.cpp                // Win32 / DXGI / Vulkan window presentation
    │   ├── PlatformWindowX11.cpp                  // Xlib / Vulkan window presentation
    │   ├── PlatformWindowWayland.cpp              // Wayland XDG window presentation
    │   ├── PlatformWindowAndroid.cpp              // Android NDK ANativeWindow presentation
    │   └── PlatformWindowStub.cpp                 // Headless validation presentation
    │
    ├── Annotation/                                // GPU-rasterized visual annotations, callouts, and debug lines
    │   ├── AnnotationExtension.{h,cpp}            // Annotation lifecycle coordinator and GPU command queueing
    │   ├── VectorAnnotation.{h,cpp}               // GPU 2D/3D vector line, arrow, Bézier, and curve markers
    │   ├── TextAnnotation.{h,cpp}                 // GPU billboarded world-space and screen-space callouts
    │   ├── SpatialAnnotation.{h,cpp}              // GPU 3D debug bounding volumes, coordinate axes, and shapes
    │   └── DiagnosticPrimitives.{h,cpp}           // GPU wireframe boxes, spheres, rays, and cages (never CPU/ImGui)
    │
    ├── Workspaces/                                // Headless multi-document session, revision & domain runtimes
    │   ├── WorkspaceExtension.{h,cpp}             // Workspace lifecycle coordinator & open tab control
    │   ├── WorkspaceDocument.{h,cpp}              // Live editable document identity & codec dispatch
    │   ├── WorkspaceRegistry.{h,cpp}              // Active workspace tracker & extension router
    │   ├── WorkspaceToken.h                       // Stale-detecting token with embedded workspace tag
    │   ├── WorkspaceExchange.{h,cpp}              // Live in-memory cross-workspace sync (Geometry ↔ UV ↔ Texture)
    │   │
    │   ├── Revision/                              // Parametric per-document & global revision engine
    │   │   ├── RevisionExtension.{h,cpp}          // Revision lifecycle coordinator & 750 MiB global ceiling
    │   │   ├── ContentRegion.{h,cpp}              // Monotonic 215 MiB per-document 64B-aligned memory block
    │   │   ├── OperationRecorder.{h,cpp}          // Parametric operation recording & Redo-Purge / Prefix-Fold
    │   │   ├── CursorResolution.{h,cpp}           // Active revision cursor & arbitrary-resolution reconstruction
    │   │   └── NonLinearHistory.{h,cpp}           // Directed acyclic revision history tree
    │   │
    │   ├── UV/                                    // UV workspace session, interactive viewport, panels & solvers
    │   │   ├── UVPaneAppearance.h                 // ControlAppearance theme binder, studio controls & vector icons
    │   │   ├── UVWorkspaceSession.{h,cpp}         // Editable UV layers, UDIM settings, selection, Interchange & Revision
    │   │   ├── UVViewportInteractor.{h,cpp}       // 2D/3D camera orbit/pan/zoom, GPU picking, gizmo drag & G/R/S modal
    │   │   ├── UVViewportHost.{h,cpp}             // Dual/single 2D+3D ViewportProjectionPane host, tool rail & drawer
    │   │   ├── UVOutlinerPanel.{h,cpp}            // UV Space outliner, utilization/stretch gauges, layers & budget bar
    │   │   ├── UVInspectorPanel.{h,cpp}           // 5-tab Inspector (Pack, Unwrap, UDIM, Shading, Check)
    │   │   ├── UVWorkspaceHost.{h,cpp}            // Standalone & FeatureSet UV host, ControlCentre & OS file-drop intake
    │   │   ├── Operations/                        // Direct authoring & connectivity operations
    │   │   │   └── UVAuthoringOperations.{h,cpp}  // Seam mark/weld/brush, EnablePinLock, stitch, split, mirror & rectify
    │   │   └── Solvers/                           // Parallel & GPU UV parameterization and packing solvers
    │   │       ├── SeamCutGenerator.{h,cpp}       // Dihedral sharp-edge, shortest-path & manifold seam cutting
    │   │       ├── ConformalUnwrapSolver.{h,cpp}  // Pin-aware parallel LSCM conformal flattening
    │   │       ├── UVProjectionSolvers.{h,cpp}    // ABF++, Smart UV, Box, Cylindrical, Spherical, CameraView & ActiveQuads
    │   │       ├── ProgressiveStretchRelaxer.{h,cpp}// Pin-aware CPU + GPU (.slang) area-preserving stretch relaxer
    │   │       ├── ConcaveSkylinePacker.{h,cpp}   // Multi-UDIM importance-weighted concave skyline packer
    │   │       ├── UVIslandLayoutOperations.{h,cpp}// Stack similar islands, unstack overlaps & fill empty pockets
    │   │       └── TexelDensityEqualizer.{h,cpp}  // Target texel density [px/m] & UDIM spillover equalizer
    │   │
    │   ├── Geometry/                              // Headless GeometrySpace authoring runtime & operations
    │   ├── Texture/                               // Headless TextureSpace authoring runtime & operations
    │   ├── Material/                              // Headless MaterialSpace authoring runtime & operations
    │   ├── Image/                                 // Headless ImageSpace authoring runtime & operations
    │   ├── Vector/                                // Headless VectorSpace authoring runtime & operations
    │   ├── Simulation/                            // Headless SimulationSpace authoring runtime & operations
    │   ├── Volumetric/                            // Headless VolumetricSpace authoring runtime & operations
    │   └── Particle/                              // Headless ParticleSpace authoring runtime & operations
    │
    ├── Editor/                                    // Tooling, Viewports & Modular UI ONLY (Excluded from Shipping)
    │   ├── Viewports/                             // Shared general-purpose 3D & 2D viewports
    │   │   ├── ProjectionViewport.{h,cpp}         // Shared 3D perspective/orthographic viewport & camera host
    │   │   ├── ViewportShadingCategory.h          // Wireframe, SolidClay, ShadedMaterial, TexelChecker, StretchHeat, Lit
    │   │   ├── VectorCanvas.{h,cpp}               // General-purpose 2D vector painting & graphics canvas
    │   │   ├── TransformGizmo.{h,cpp}             // GPU .slang 3D translation, rotation, and scale manipulators
    │   │   └── NavigationOverlay.{h,cpp}          // GPU .slang reference grid, axes, and orientation indicator
    │   │
    │   ├── Workspaces/                            // Concrete modular editor workspace UI suites
    │   │   ├── UV/                                // Modular UV Editor UI (pairs UVTileCanvas + shared ProjectionViewport)
    │   │   │   ├── UVWorkspaceRegistration.{h,cpp}// Docking layout registration & workspace entry
    │   │   │   ├── UVOutlinerPanel.{h,cpp}        // Island tree, priority groups & UDIM UVTile list
    │   │   │   ├── UVInspectorPanel.{h,cpp}       // Pack budgets, padding & texel density properties
    │   │   │   ├── Viewports/
    │   │   │   │   └── UVTileCanvas.{h,cpp}       // 2D UVTile / UDIM projection viewport (3D view uses ProjectionViewport)
    │   │   │   ├── Toolbars/
    │   │   │   │   ├── UVOperationToolbar.{h,cpp} // Top strip: Unwrap, Pack, Relax & Stitch triggers
    │   │   │   │   ├── UVSeamEditingToolbar.{h,cpp}// Edge cut, path mark, weld & seam mode toggles
    │   │   │   │   ├── UVTransformToolbar.{h,cpp} // Island align, rotate 90°, flip & normalize tools
    │   │   │   │   └── UVDisplayOptionToolbar.{h,cpp}// Grid snap, checkerboard & distortion heatmap toggles
    │   │   │   └── Interactions/
    │   │   │       ├── UVSelectionInteractors.{h,cpp}// Point pick, marquee box & brush select routers
    │   │   │       ├── UVTransformGizmo.{h,cpp}   // 2D/3D interactive translation, rotation & scale handles
    │   │   │       └── UVSeamPickRaycaster.{h,cpp}// 3D ray-facet intersection for live seam cutting & painting
    │   │   ├── Geometry/                          // Modular Geometry Editor UI (panels, toolbars, interactions)
    │   │   ├── Texture/                           // Modular Texture Editor UI (panels, toolbars, interactions)
    │   │   ├── Material/                          // Modular Material Editor UI (panels, toolbars, interactions)
    │   │   ├── Image/                             // Modular Image Editor UI (panels, toolbars, interactions)
    │   │   ├── Vector/                            // Modular Vector Editor UI (panels, toolbars, interactions)
    │   │   ├── Simulation/                        // Modular Simulation Editor UI (panels, toolbars, interactions)
    │   │   ├── Volumetric/                        // Modular Volumetric Editor UI (panels, toolbars, interactions)
    │   │   └── Particle/                          // Modular Particle Editor UI (panels, toolbars, interactions)
    │   │
    │   ├── Explorer/                              // Project structure and browser tools
    │   │   ├── SceneOutliner.{h,cpp}              // Spatial world tree navigator
    │   │   ├── PropertyInspectorHost.{h,cpp}      // Reflected attribute inspector
    │   │   ├── ProjectBrowser.{h,cpp}             // Multi-workspace and .ProjectSpace document explorer
    │   │   ├── ContentBrowser.{h,cpp}             // Asset packages, materials, and runtime contents
    │   │   └── DebugConsole.{h,cpp}               // Interactive command interpreter & diagnostic log
    │   │
    │   └── Diagnostics/                           // Profiling and engine instrumentation
    │       ├── CycleProfiler.{h,cpp}              // CPU/GPU tick profiler
    │       ├── MemoryDebugger.{h,cpp}             // Allocation leak tracker and region visualizer
    │       └── GPUCaptureRecorder.{h,cpp}         // RenderDoc / PIX programmatic capture recorder
    │
    ├── Lifecycle/                                 // Deterministic sequence, game runtime, workspace & desktop launch
    │   ├── LifecycleSequence.h                    // C++20 concept & reverse-order teardown sequence (Editor/Dev/Shipping)
    │   ├── RuntimeLifecycle.{h,cpp}               // Standalone game/runtime loop (Development/Shipping Game)
    │   ├── WorkspaceLifecycle.{h,cpp}             // Multi-document session & revision loop (Editor/Development)
    │   ├── EditorLifecycle.{h,cpp}                // Authoring loop & GPU overlay entry (FRONTIER_PROFILE_EDITOR only)
    │   ├── DesktopWorkspaceLaunch.{h,cpp}         // GLFW + Vulkan 1.2 swapchain/offscreen launch & CLI smoke capture
    │   ├── FrontierUVLaunch.cpp                   // Standalone FrontierUV desktop application entry
    │   └── FrontierEditorLaunch.cpp               // Full FeatureSet FrontierEditor desktop application entry
    │
    ├── Components/                                // Reusable UI controls, themes, typography, motion & panels
    │   ├── PixelSpace.{h,cpp}                     // Logical-to-physical 2D drawing canvas & group transforms
    │   ├── Appearance/                            // Themes, semantic swatches & .FontSpace typography
    │   │   ├── ThemeRepository.{h,cpp}            // 8 studio themes, 10 accents, semantic swatches & type scale
    │   │   └── TypographyRepository.{h,cpp}       // PascalCase FontRepository.FontSpace loader & weight fallback
    │   ├── Motion/                                // Semi-implicit Euler UI spring physics
    │   │   └── SpringIntegrator.{h,cpp}           // Substepped 240 Hz spring-damper solver
    │   ├── Notch/                                 // Top-edge pull-down tab
    │   │   └── PanelExpansionTab.{h,cpp}          // Trapezoidal "Frontier" pull-down notch with flick physics
    │   ├── Docking/                               // Sharp unrounded trapezoidal docking & multi-window host
    │   │   ├── WorkspaceTabStrip.{h,cpp}          // Sharp 0 px unrounded trapezoidal tab strip & notch geometry
    │   │   └── WorkspaceDockContainer.{h,cpp}     // Intra-app multi-window docking & OS window tear-off
    │   ├── Panes/                                 // Reusable workspace Outliner/LayerStack, Inspector & Viewport panes
    │   │   ├── WorkspaceAppearance.{h,cpp}        // Shared studio colours, VisualToken, VectorGraphic, icons, signs & symbols
    │   │   ├── OutlinerPanel.{h,cpp}              // Generic Outliner/LayerStack tree, folders, filters & drag-reorder
    │   │   ├── InspectorPanel.{h,cpp}             // Showcase Folder Inspector, Add Notes editor & capability tiles
    │   │   ├── ViewportPanel.{h,cpp}              // Edit/Simulate/Play-Pause strip, Construct, Split, Diagnostics & Settings
    │   │   └── ViewportProjectionPane.{h,cpp}     // Reusable single/split 2D+3D GPU viewport pane bound to WorkspaceDocument
    │   ├── ControlCentre/                         // Pull-down Control Centre & settings pages
    │   │   └── ControlCentrePanel.{h,cpp}         // Quick Controls, Appearance, Input, Notifications & Workspace
    │   ├── ContentBrowser/                        // Pull-up ContentBrowser drawer & ColourCodex repository
    │   │   ├── ColourCodex.{h,cpp}                // Named colour aliases, .ColourSwatch, Colour Filters & TOML persistence
    │   │   ├── ThumbnailSheetRepository.{h,cpp}   // Pre-baked 1024x1024 ThumbnailSheet, 64B index, quarantine & workspace masks
    │   │   └── ContentBrowserPanel.{h,cpp}        // Bottom-edge ContentBrowser, Grid/Vertical/Horizontal views & colour slots
    │   ├── Controls/                              // Reusable studio UI control primitives
    │   │   ├── ControlAppearance.{h,cpp}          // Shared colour set, OptionalTooltip, smoothstep & section containers
    │   │   ├── ControlTooltip.{h,cpp}             // Light/dark, 4-direction speech tails & diagnostic SplitHeader tooltips
    │   │   ├── ScalarSlider.{h,cpp}               // Capsule slider & split numeric/unit readout
    │   │   ├── ExclusiveSelector.{h,cpp}          // Standard bordered & compact borderless exclusive switches
    │   │   ├── CapsuleButton.{h,cpp}              // Primary/Secondary/Danger buttons, chips, segmented bars & discs
    │   │   ├── DropdownSelector.{h,cpp}           // Rounded dropdown button & overlay option sheet
    │   │   ├── TextInputField.{h,cpp}             // UTF-8 text buffer, selection, caret & input field
    │   │   ├── ThemePreviewTile.{h,cpp}           // 4:3 miniature OS window theme preview tile & colour swatches
    │   │   ├── NotificationSequence.{h,cpp}       // Toast overlay queue & Quick Controls notification stream
    │   │   └── ConfirmationDialogue.{h,cpp}       // Foreground Rounded (28 px) & SemanticHeader (8 px) modals
    │   ├── VectorIcons/                           // SVG 1.1 path decoder & reusable vector icon repository
    │   │   └── VectorIconRepository.{h,cpp}       // Cubic/quadratic/arc flattener & round-cap icon stroker
    │   └── Overlays/                              // Viewport and cursor interaction overlays
    │       ├── ToolShelf.{h,cpp}                  // Viewport-anchored tool row (ToolCluster / UtilityStrip)
    │       ├── ToolCluster.{h,cpp}                // Compact group of related manipulation tools
    │       ├── UtilityStrip.{h,cpp}               // Lightweight secondary tool bar for viewport toggles
    │       ├── ContextDirectiveList.{h,cpp}       // Cursor-anchored contextual directives
    │       └── CommandLauncher.{h,cpp}            // Fuzzy-search directive runner
    │
    ├── NetworkingSpace/                           // Distributed simulation, differential encoding & remote directives
    │   ├── NetworkingExtension.{h,cpp}            // Lifecycle coordinator: polls sockets, ticks sync, flushes buffers
    │   ├── SocketTransport.{h,cpp}                // Non-blocking UDP datagram and TCP stream transport
    │   ├── PacketSerializer.{h,cpp}               // Bit-packed wire encoding, fragmentation, and checksum verification
    │   ├── DifferentialWorldEncoder.{h,cpp}       // Authoritative baseline-relative quantized world delta encoder
    │   ├── RemoteDirectiveRouter.{h,cpp}          // Reliable ordered delivery and execution of remote directives
    │   └── NetworkEndpointRegister.{h,cpp}        // Connected socket endpoints, round-trip latency, and clock alignment
    │
    └── AudioSpace/                                // Spatial acoustics, waveform decoding, and bus accumulation
        ├── AudioExtension.{h,cpp}                 // Lifecycle coordinator: device output, bus ticks, and voice budgets
        ├── AudioHardwareOutput.{h,cpp}            // Native OS audio endpoint intake and lock-free ring buffers
        ├── WaveformDecoder.{h,cpp}                // PCM, WAV, Ogg Vorbis, and FLAC sample stream decoding
        ├── AudioBusAccumulator.{h,cpp}            // Multi-voice bus summing, gain envelopes, biquad filtering & resampling
        └── SpatialAcoustics.{h,cpp}               // 3D HRTF binaural panning, Doppler pitch shift, and occlusion
```

---

## Structural Boundaries & Conventions

1. **Three-Profile Build Separation (`Editor` · `Development` · `Shipping`)**:
   - `Engine/Editor/` compiles and links **only** when `FRONTIER_PROFILE_EDITOR=1` (`-DFRONTIER_PROFILE=Editor`).
   - `Development` builds run standalone project logic with developer assertions, console, and GPU `.slang` debug overlays active **without binary packaging**.
   - `Shipping` builds completely exclude `Engine/Editor/` and strip diagnostics to produce standalone packaged binaries.
2. **Codecs vs. Interchange vs. Exchange**:
   - `Engine/Codecs/` (`*Codec` = `*Encoder` + `*Decoder`) reads and writes native Frontier `.Space` hybrid containers (Option 2A case-insensitive extension load, canonical PascalCase write).
   - `Engine/Interchange/` (`*Interchange`) and `VulkanInterchange` translate external file formats and GPU API boundaries.
   - `*Exchange` (`InputExchange`, `ClipboardExchange`, `WorkspaceExchange`) performs live bi-directional runtime transfer across OS or open workspaces.
3. **100% GPU `.slang` Viewport Overlays**: Grids (`NavigationOverlay`), gizmos (`TransformGizmo`, `UVTransformGizmo`), UVTile overlays (`UVTileRasterization`), and debug shapes/lines (`Engine/Annotation/*`) render exclusively on the GPU via `.slang` shaders — never on CPU and never via ImGui lists.
4. **Cross-Platform Portability**: All `Engine/` modules target **Windows**, **Linux (X11/Wayland)**, **Android (NDK)**, and **Headless (`PlatformWindowStub`)**.
5. **Mandatory `Exhibits/Images/` Visual Snapshots**: Every `Exhibits/*Validation` target renders deterministic CPU `.png` visual snapshots into `Exhibits/Images/<Domain>/`.
