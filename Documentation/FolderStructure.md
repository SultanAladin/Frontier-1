# Frontier Folder Structure

This document defines the canonical directory and source file layout of the **Frontier** repository. All engine domains, `.slang` shader libraries, default stock archives, validation suites, build tooling, user projects, and governance documents adhere to this structure.

For identifier, file, folder, and comment naming rules enforced across the repository, see [BannedWordsVocabulary.md](BannedWordsVocabulary.md) and [AgenticInstructions/SKILL-Naming.md](AgenticInstructions/SKILL-Naming.md). For C++20 language and modularity rules, see [CodingStandards.md](CodingStandards.md).

---

## Top-Level Directory Summary

| Path | Purpose |
| :--- | :--- |
| `CLAUDE.md` · `AGENTS.md` | Session-start agent entry point, non-negotiable GPU/`.slang`/cross-platform mandates, and reading order. |
| `CMakeLists.txt` | ISO C++20 build definition enforcing `Editor`, `Development`, and `Shipping` profiles. |
| `EngineContent/` | Engine default stock assets, vector icons, font archives, PBR material definitions, worlds, and graphics archives. |
| `EngineDocs/` | Authoritative architectural guides and deep domain references. |
| `Documentation/` | Repository governance, `AgenticInstructions/`, `FolderStructure.md`, `BannedWordsVocabulary.md`, `CodingStandards.md`, `ImportantNotes.md`, `Backlog.md`, and `Tracker.md`. |
| `Exhibits/` | Validation executables (`*Validation`), `Images/` CPU-rendered `.png` visual proofs, and `EditorMirror` verification targets. |
| `Experimental/` | HTML/JS-only reference editor UIs (`ProjectZeroEditor`, `FrontierEditor`) and domain studios (`FluidEditor`, `FractureEditor`, `ParticleEditor`, `PhotometricEditor`, `ForceFieldEditor`, `VehicleLab`, `UVEditor`, `TextureStudio`) staged for native C++20 + `.slang` conversion. |
| `Automation/` | Dependency pinning (`Dependencies.json`), ImGui 8-patch stack (`Patches/`), `.slang` toolchain setup, CMake modules, and binary packaging. |
| `ExternalPackages/` | Checksum-verified third-party packages (`imgui`, `thorvg`, `tomlpp`, `glfw`, `vulkan-headers`, `volk`, `stb`, `JoltPhysics`, `VulkanCompute/`). Git-ignored; populated by `Automation/`. |
| `_ClaudeScratch/` | Ignored local scratchpad and transient planning notes (excluded from version control). |
| `Projects/` | Container for concrete user projects (such as `SampleProject/`), keeping project assets and logic isolated from engine source. |
| `Engine/` | Portable C++20 engine source tree (Windows, Linux, Android), `.slang` shaders, workspaces, UI components, and `Editor/`. |

---

## Canonical Repository Tree

```text
Frontier/
├── CLAUDE.md                                  // Primary agent instructions & non-negotiable mandates
├── AGENTS.md                                  // Universal coding-agent entry point
├── CMakeLists.txt                             // Root C++20 build & Editor/Development/Shipping profiles
├── README.md                                  // Repository overview and architecture index
│
├── EngineContent/                             // Engine default assets and stock libraries
│   ├── Icons/                                 // Vector SVGs and compiled icon catalogs
│   ├── FontsArchives/                         // Primary UI and monospace debug typefaces
│   ├── MaterialArchives/                      // Microfacet PBR surface definitions
│   ├── WorldsArchives/                        // Levels and worlds archives
│   └── GraphicArchives/                       // Image formats and SVG archives
│
├── EngineDocs/                                // Authoritative architectural guides & domain references
│
├── Documentation/                             // Repository governance and engineering tracking
│   ├── AgenticInstructions/                   // Mandatory skill protocols (imported & harmonized)
│   │   ├── SKILL-CompactOutputProtocol.md     // Skimmer-first output & mandatory decision tables
│   │   ├── SKILL-Emoji.md                     // Approved emoji vocabulary & rating markers
│   │   ├── SKILL-Naming.md                    // Mechanism-first naming construction & 6-gate check
│   │   └── SKILL-Formatting.md                // File banners, section rulers, unit tags, Allman braces
│   ├── FolderStructure.md                     // Canonical repository folder and module layout
│   ├── BannedWordsVocabulary.md               // Blacklisted terminology and naming governance
│   ├── CodingStandards.md                     // ISO C++20 standard, prototypes, concepts & templates
│   ├── ImportantNotes.md                      // Hard architectural invariants & reference rules
│   ├── Backlog.md                             // Deferred engineering items ledger
│   └── Tracker.md                             // Active milestone roadmap & bringup status
│
├── Exhibits/                                  // Validation test suites, visual snapshots, and EditorMirror
│   ├── Images/                                // CPU-rendered .png visual snapshots for every validation
│   ├── SoftwareCanvasRasterizer.h             // CPU ImDrawData & RGBA8 PNG software rasterizer
│   ├── InputValidation.cpp                    // Deterministic Input & BuildConfiguration verification
│   ├── WindowValidation.cpp                   // Splash, windowed, maximized & fullscreen mode verification
│   ├── DockingValidation.cpp                  // Trapezoidal multi-window & intra-app docking verification
│   └── ControlCentreValidation.cpp            // Control Centre, reusable Controls & .projectspace verification
│
├── Experimental/                              // HTML/JS reference UIs & studios for C++20 conversion
│   ├── FrontierEditor/                        // Initial HTML/Vite scene inspector, CAD & icon lab UI
│   ├── ProjectZeroEditor/                     // Canonical Project-Zero UI reference + README render
│   ├── FluidEditor/                           // Gas, plume, combustion, and vortex solver prototype
│   ├── FractureEditor/                        // Voronoi, convex partition, SDF, and debris prototype
│   ├── ParticleEditor/                        // GPU compute particle, fibre, and lightning studio
│   ├── PhotometricEditor/                     // LM-63 .ies luminaire, sky, and isolux metering studio
│   ├── ForceFieldEditor/                      // Wind generator and 16 spatial force-field studio
│   ├── VehicleLab/                            // ControlVehicle FCT1, powertrain, and MF6.1 tyre lab
│   ├── UVEditor/                              // LSCM unwrap, UDIM layout, and importance packer studio
│   └── TextureStudio/                         // 5-workspace material, pattern, stamp, paint & SDF baker
│
├── Automation/                                // Build tooling, dependency pinning, patches, and packaging
│   ├── Dependencies.json                      // SHA-256 pinned C/C++ package manifest
│   ├── PrepareDependencies.py                 // Checksummed archive fetcher & ImGui patch runner
│   ├── ApplyImGuiPatches.py                   // Stacked PatchA-PatchH applicator and verifier
│   ├── PrepareShaderToolchain.py              // Slang (.slang) compiler & SwiftShader ICD fetcher
│   ├── PrepareFonts.py                        // OFL font fetcher & PascalCase FontCatalogue.FontSpace builder
│   ├── FontSources.json                       // Pinned OFL typeface manifest
│   ├── FontRequirements.txt                   // Python font tooling pin
│   ├── StageThorVG.py                         // Reproducible ThorVG allocation-operator staging
│   ├── ThorVGSoftware.cmake                   // Static ThorVG build module
│   ├── JoltPhysics.cmake                      // Deterministic x86_64/ARM64 JoltPhysics v5.6.0 module
│   ├── WindowsToolchain.cmake                 // Zig 0.13.0 Windows x64 cross-compile toolchain
│   └── Patches/                               // Mandatory 8-patch ImGui stack (PatchA through PatchH)
│
├── ExternalPackages/                          // Third-party external dependencies (git-ignored)
│
├── _ClaudeScratch/                            // Ignored scratchpad and transient planning notes
│
├── Projects/                                  // User project workspace container
│   └── SampleProject/                         // Concrete game project
│       ├── Content/                           // Project-specific assets (levels, textures, audio)
│       ├── Source/                            // Project-specific C++ logic
│       └── Saved/                             // Local project caches, logs, and save archives
│
└── Engine/                                    // Engine Source Root (Windows · Linux · Android)
    ├── MicroUtils/                            // Foundational allocators and identity tokens
    │   ├── RegionAllocator.{h,cpp}            // Monotonic non-freeing block allocator
    │   ├── TokenIssuer.h                      // Stale-detecting monotonic ID generator
    │   ├── TokenAuthentication.h              // Validation primitives for issued handles
    │   ├── PackedBits.h                       // Contiguous packed-bit operations and bit-range queries
    │   ├── IndexedSlotRecycler.h              // Preallocated indexed instance slot recycler
    │   └── Assertion.h                        // Invariant enforcement macros and diagnostics
    │
    ├── Configuration/                         // Startup, build-profile, and .projectspace configurations
    │   ├── BuildConfiguration.h               // Compile-time Editor / Development / Shipping policy
    │   ├── InputSettings.{h,cpp}              // Shortcut chords, conflict validator & pointer sensitivity
    │   ├── ProjectSpaceArchive.{h,cpp}        // Atomic PascalCase .projectspace container serializer
    │   ├── EngineConfiguration.{h,cpp}        // Startup and hardware thread group allocation
    │   ├── RenderConfiguration.{h,cpp}        // Resolution, swapchain, and multi-sample targets
    │   ├── ProjectConfiguration.{h,cpp}       // Project root, dependency manifests, and toggles
    │   └── WorkspaceConfiguration.{h,cpp}     // Document session preferences and layout defaults
    │
    ├── MetricSpace/                           // Coordinate spaces, projections, and spatial math
    │   ├── CoordinateSpace.h                  // Reference-basis transformations and basis vectors
    │   ├── CoordinateProjection.h             // Orthographic, perspective, and screen projections
    │   ├── DistanceMetric.h                   // Metric evaluation (Euclidean, Manhattan, Chebyshev)
    │   ├── SpatialExtent.h                    // Axis-aligned extents and volumetric bounds
    │   ├── LinearAlgebra.h                    // SIMD-aligned vector algebra definitions
    │   ├── VectorAlgebra.{h,cpp}
    │   ├── MatrixOperations.{h,cpp}
    │   ├── QuaternionRotation.{h,cpp}
    │   └── IntersectionSolver.{h,cpp}         // Ray-volume and volume-volume geometric solvers
    │
    ├── SpatialAcceleration/                   // Broad-phase spatial acceleration structures
    │   ├── BoundingVolumeHierarchy.{h,cpp}    // Surface-area heuristic split tree
    │   ├── LooseOctree.{h,cpp}                // Expanded boundary volumetric spatial tree
    │   ├── DynamicAABBTree.{h,cpp}            // Self-balancing binary tree for dynamic colliders
    │   └── SpatialGridHash.{h,cpp}            // Sparse spatial hashing structure
    │
    ├── DistanceFieldSpace/                    // Volumetric SDFs, implicit geometry, and queries
    │   ├── SignedDistanceField.{h,cpp}        // Voxel-backed signed distance representations
    │   ├── DistanceTransform.{h,cpp}          // Fast sweeping and Maurer distance transforms
    │   ├── VolumetricDistance.{h,cpp}         // Trilinear volume evaluation and normal reconstruction
    │   ├── DistanceQuery.{h,cpp}              // Ray marching and closest-point queries
    │   └── SDFPrimitiveSolver.{h,cpp}         // Analytic primitive distance solvers
    │
    ├── GeometryProcessing/                    // Topological surface complexes and discrete operators
    │   ├── PolygonComplex.{h,cpp}             // Indexed polygon collection and topological groupings
    │   ├── HalfEdgeComplex.{h,cpp}            // Directional half-edge connectivity structure
    │   ├── DecimationSolver.{h,cpp}           // Quadric error metric (QEM) edge collapse simplifier
    │   ├── SubdivisionSurface.{h,cpp}         // Catmull-Clark and Loop refinement operators
    │   ├── SurfaceTopology.{h,cpp}            // Non-manifold detection and boundary loop traverser
    │   └── CurvatureAnalysis.{h,cpp}          // Discrete mean and Gaussian curvature calculation
    │
    ├── UVSpace/                               // Surface parameterization, chart routing, and packing
    │   ├── ChartParameterizer.{h,cpp}         // Least Squares Conformal (LSCM) / ARAP solver
    │   ├── UVChartPacker.{h,cpp}              // Scanline UV chart packing
    │   ├── SeamDetector.{h,cpp}               // High-distortion edge loop finder
    │   ├── ConformalProjector.{h,cpp}         // Angle-preserving harmonic coordinate projector
    │   └── DistortionMetrics.{h,cpp}          // Area/angle deformation measurement
    │
    ├── ColorSpace/                            // Tristimulus spaces, chromaticity, and transfer curves
    │   ├── ChromaticitySpace.{h,cpp}          // CIE XYZ, Linear sRGB, ACEScg, Rec.2020, and Oklab transforms
    │   ├── TransferFunction.{h,cpp}           // Electro-optical and opto-electronic curves (sRGB, PQ, HLG)
    │   ├── WhitePointAdaptation.{h,cpp}       // Bradford and CAT02 chromatic adaptation (D65, D60, D50)
    │   ├── SpectralDistribution.{h,cpp}       // Wavelength tristimulus integration and Planckian Kelvin
    │   └── GamutCompression.{h,cpp}           // Out-of-gamut perceptual chroma reduction and clipping solvers
    │
    ├── GeometricRaster/                       // Hardware draw encoding, commands, and GPU resources
    │   ├── RenderExtension.{h,cpp}            // Render lifecycle coordinator and device attachment
    │   ├── RasterExecutionPath.{h,cpp}        // GPU execution sequence
    │   ├── RenderScheduler.{h,cpp}            // Transient dependency-sorted workload coordinator
    │   ├── IndirectDrawEncoder.{h,cpp}        // Draw-call batching, instancing, and indirect encoding
    │   ├── RenderDeviceInterface.h            // GPU hardware device interface and capability limits
    │   ├── DescriptorSlotRegister.{h,cpp}     // Descriptor layout and push-constant slot register
    │   └── PostProcess/                       // Ordered image operations
    │       ├── PostProcessSequence.{h,cpp}    // Ordered image operation execution sequence
    │       ├── RadiometricToneStack.{h,cpp}   // Exposure, ACES gamut transform, and color grading stack
    │       ├── VeilingGlareFilter.{h,cpp}     // Multi-scale luminance threshold and aperture glare scatter
    │       └── AmbientOcclusionFilter.{h,cpp} // Bilateral depth-aware spatial filtering
    │
    ├── Shaders/                               // GPU Slang (.slang) shader library and compiler bridge
    │   ├── Compiler/
    │   │   ├── ShaderCompiler.{h,cpp}         // Offline and runtime Slang (.slang) to SPIR-V compiler
    │   │   ├── ShaderReflection.{h,cpp}       // Vertex attribute and descriptor layout reflection
    │   │   └── ShaderPermutationIndex.{h,cpp} // Precompiled variant permutation register
    │   ├── Raster/                            // Surface, depth, and GPU viewport overlay shaders
    │   │   ├── MicrofacetSurface.slang
    │   │   └── DepthPreRaster.slang
    │   ├── Compute/                           // General GPU compute shaders
    │   │   ├── ParticleIntegration.slang
    │   │   ├── FluidAdvection.slang
    │   │   └── VoxelDistanceSweep.slang
    │   └── ImageOperations/                   // Post-process image filter shaders
    │       ├── DisplayToneTransfer.slang
    │       └── LuminanceGlareDownsample.slang
    │
    ├── SimulationSpace/                       // Physics, dynamics, particles, and fluids
    │   ├── PhysicsSpace/                      // Rigid mechanics and constraints
    │   │   ├── PhysicsExtension.{h,cpp}       // Simulation loop coordinator and broad-phase attachment
    │   │   ├── RigidBodyDynamics.{h,cpp}      // Velocity Verlet integration and inertia tensors
    │   │   ├── ConstraintSolver.{h,cpp}       // Projected Gauss-Seidel iterative velocity solver
    │   │   ├── BroadphaseSolver.{h,cpp}       // Sweep-and-prune dynamic bounding query
    │   │   └── CollisionDetection.{h,cpp}     // GJK narrow-phase & EPA contact extraction
    │   ├── ParticleSimulation/                // High-density particulate dynamics
    │   │   ├── ParticleEmitter.{h,cpp}        // Point, volume, and surface particle spawning
    │   │   ├── ParticleField.{h,cpp}          // Curl noise, rotational, and gravitational vector fields
    │   │   ├── ParticleIntegrator.{h,cpp}     // High-performance Euler and Verlet particle solvers
    │   │   └── ComputeParticleBuffer.{h,cpp}  // GPU-side direct compute buffer synchronization
    │   ├── FluidSimulation/                   // Multiphase liquids and gases
    │   │   ├── LiquidSolver.{h,cpp}           // Incompressible SPH liquid & surface solver (water)
    │   │   ├── GasDynamics.{h,cpp}            // Eulerian grid smoke, fire, and atmosphere solver
    │   │   ├── VorticityConfinement.{h,cpp}   // Turbulent detail and rotational energy preservation
    │   │   ├── AdvectionSolver.{h,cpp}        // Semi-Lagrangian and MacCormack advection schemes
    │   │   └── FluidBoundarySolver.{h,cpp}    // Dynamic collision interaction with solid geometry
    │   └── Motion/                            // Procedural interpolation, dynamics, and timelines
    │       ├── MotionSolver.{h,cpp}           // Kinetic trajectory & motion resolver
    │       ├── Interpolation/
    │       │   ├── ScalarInterpolation.{h,cpp}
    │       │   ├── VectorInterpolation.{h,cpp}
    │       │   └── ColorInterpolation.{h,cpp}
    │       ├── Easing/
    │       │   ├── EasingProfile.{h,cpp}
    │       │   ├── BezierEasing.{h,cpp}
    │       │   └── EaseCatalog.{h,cpp}        // Precomputed response curves
    │       ├── Dynamics/
    │       │   ├── SpringDynamics.{h,cpp}
    │       │   ├── InertiaDynamics.{h,cpp}
    │       │   └── DampingDynamics.{h,cpp}
    │       └── Sequencing/
    │           ├── TransitionDescriptor.h
    │           ├── SequenceAnimator.{h,cpp}
    │           └── TimelineAnimator.{h,cpp}
    │
    ├── Input/                                 // Bi-directional peripheral, touch, and OS feedback exchange
    │   ├── InputExtension.{h,cpp}             // Input lifecycle coordinator and snapshot consolidation
    │   └── InputExchange.{h,cpp}              // OS->Engine signal intake & Engine->OS cursor/haptic exchange
    │
    ├── Windowing/                             // Native cross-platform surface abstractions
    │   ├── PlatformWindow.h                   // Window flags, application presets & mode-switch interface
    │   ├── PlatformWindowDesktop.cpp          // GLFW 3.4 Win32, X11, Wayland & offscreen desktop surface
    │   ├── PlatformWindowWin32.cpp            // Win32 / DXGI / Vulkan surface presentation
    │   ├── PlatformWindowX11.cpp              // Xlib / Vulkan presentation
    │   ├── PlatformWindowWayland.cpp          // Wayland XDG surface presentation
    │   ├── PlatformWindowAndroid.cpp          // Android NDK ANativeWindow presentation
    │   └── PlatformWindowStub.cpp             // Headless validation presentation
    │
    ├── Interchange/                           // Native format asset readers and serializers
    │   ├── ModelInterchange.{h,cpp}           // Scene tree intake coordinator
    │   ├── InterchangeReaders.h               // Format recognition and reader registry
    │   ├── InterchangeWavefrontReader.{h,cpp} // OBJ decoder
    │   ├── InterchangeGltfReader.{h,cpp}      // glTF 2.0 binary and JSON reader
    │   └── InterchangeFilmboxReader.{h,cpp}   // FBX interchange reader
    │
    ├── Annotation/                            // GPU-rasterized visual annotations, callouts, and debug lines
    │   ├── AnnotationExtension.{h,cpp}        // Annotation lifecycle coordinator and GPU draw queueing
    │   ├── VectorAnnotation.{h,cpp}           // GPU 2D/3D vector line, arrow, Bézier, and curve markers
    │   ├── TextAnnotation.{h,cpp}             // GPU billboarded world-space and screen-space callouts
    │   ├── SpatialAnnotation.{h,cpp}          // GPU 3D debug bounding volumes, coordinate axes, and shapes
    │   └── DiagnosticPrimitives.{h,cpp}       // GPU wireframe boxes, spheres, rays, and cages (never CPU/ImGui)
    │
    ├── Workspaces/                            // Multi-document session and authoring engine
    │   ├── WorkspaceExtension.{h,cpp}         // Workspace creation infrastructure & tab control
    │   ├── WorkspaceDocument.{h,cpp}          // Live editable document content
    │   ├── WorkspaceRegistry.{h,cpp}          // Active workspace tracker
    │   ├── WorkspaceToken.h                   // Stale-detecting token with embedded workspace tag
    │   ├── AuthoringWorkspace.{h,cpp}         // Domain binder (links document to target complex)
    │   └── Revision/                          // Branching per-document revision history
    │       ├── RevisionExtension.{h,cpp}      // Revision lifecycle coordinator
    │       ├── ContentRegion.{h,cpp}          // Monotonic document memory block
    │       ├── DeltaIntake.{h,cpp}            // Granular edit delta intake
    │       ├── CursorResolution.{h,cpp}       // Active revision cursor resolution in non-linear history
    │       └── NonLinearHistory.{h,cpp}       // Directed acyclic history tree
    │
    ├── Editor/                                // Tooling & Diagnostics ONLY (Excluded from Shipping Builds)
    │   ├── Workspaces/                        // Concrete editor workspace implementations
    │   │   ├── ModelingWorkspace.{h,cpp}      // Geometry modeling, extrude, bevel, cut
    │   │   ├── UVWorkspace.{h,cpp}            // Seam marking, unwrapping, and relaxation
    │   │   ├── TexturingWorkspace.{h,cpp}     // Projection painting and volumetric stamp brushes
    │   │   └── BakingWorkspace.{h,cpp}        // Ambient occlusion and curvature texture baking
    │   ├── Explorer/                          // Project structure and browser tools
    │   │   ├── SceneOutliner.{h,cpp}          // Spatial scene tree navigator
    │   │   ├── PropertyInspectorHost.{h,cpp}  // Reflected attribute inspector
    │   │   ├── ProjectBrowser.{h,cpp}         // Multi-workspace and project document explorer
    │   │   ├── ContentBrowser.{h,cpp}         // Asset packages, materials, and runtime contents
    │   │   └── DebugConsole.{h,cpp}           // Interactive command interpreter & diagnostic log
    │   ├── Viewports/                         // General-purpose rendering and editing viewports
    │   │   ├── ProjectionViewport.{h,cpp}     // 3D general-purpose scene viewport & camera host
    │   │   ├── VectorCanvas.{h,cpp}           // 2D vector painting & graphics canvas viewport
    │   │   ├── TransformGizmo.{h,cpp}         // GPU .slang translation, rotation, and scale manipulators
    │   │   └── NavigationOverlay.{h,cpp}      // GPU .slang reference grid, axes, and orientation indicator
    │   └── Diagnostics/                       // Profiling and engine instrumentation
    │       ├── CycleProfiler.{h,cpp}          // CPU/GPU tick profiler
    │       ├── MemoryDebugger.{h,cpp}         // Allocation leak tracker and region visualizer
    │       └── GPUCaptureRecorder.{h,cpp}     // RenderDoc / PIX programmatic capture recorder
    │
    ├── Components/                            // Reusable UI controls, themes, typography, motion & panels
    │   ├── PixelSpace.{h,cpp}                 // Logical-to-physical 2D drawing surface & group transforms
    │   ├── Appearance/                        // Themes, semantic swatches & .FontSpace typography
    │   │   ├── ThemeCatalog.{h,cpp}           // 8 surface themes, 10 accents, semantic swatches & type scale
    │   │   └── TypefaceCatalog.{h,cpp}        // PascalCase FontCatalogue.FontSpace loader & weight fallback
    │   ├── Motion/                            // Semi-implicit Euler UI spring physics
    │   │   └── SpringIntegrator.{h,cpp}       // Substepped 240 Hz spring-damper solver
    │   ├── Notch/                             // Top-edge pull-down tab
    │   │   └── PanelExpansionTab.{h,cpp}      // Trapezoidal "Frontier" pull-down notch with flick physics
    │   ├── Docking/                           // Sharp unrounded trapezoidal docking & multi-window host
    │   │   ├── WorkspaceTab.{h,cpp}           // Sharp 0 px unrounded trapezoidal tab & notch geometry
    │   │   └── WorkspaceDockContainer.{h,cpp} // Intra-app multi-window docking & OS window tear-off
    │   ├── ControlCentre/                     // Pull-down Control Centre & settings pages
    │   │   └── ControlCentrePanel.{h,cpp}     // Quick Controls, Appearance, Input, Notifications & Workspace
    │   ├── Controls/                          // Reusable studio UI control primitives
    │   │   ├── ControlStyle.{h,cpp}           // Shared color set, smoothstep cross-fade & section containers
    │   │   ├── ScalarSlider.{h,cpp}           // Capsule slider & split numeric/unit readout
    │   │   ├── BooleanSwitch.{h,cpp}          // Standard bordered & compact borderless switches
    │   │   ├── CapsuleButton.{h,cpp}          // Primary/Secondary/Danger buttons, chips, segmented bars & discs
    │   │   ├── DropdownSelector.{h,cpp}       // Rounded dropdown button & overlay option sheet
    │   │   ├── TextInputField.{h,cpp}         // UTF-8 text buffer, selection, caret & input field
    │   │   ├── ThemePreviewTile.{h,cpp}       // 4:3 miniature OS window theme preview tile & color swatches
    │   │   ├── NotificationSequence.{h,cpp}   // Toast overlay queue & Quick Controls notification stream
    │   │   └── ConfirmationDialogue.{h,cpp}   // Foreground Rounded (28 px) & SemanticHeader (8 px) modals
    │   ├── VectorIcons/                       // SVG 1.1 path decoder & reusable vector icon catalog
    │   │   └── VectorIconCatalog.{h,cpp}      // Cubic/quadratic/arc flattener & round-cap icon stroker
    │   └── Overlays/                          // Viewport and cursor interaction overlays
    │       ├── ToolShelf.{h,cpp}              // Viewport-anchored tool row (ToolCluster / UtilityStrip)
    │       ├── ToolCluster.{h,cpp}            // Compact group of related manipulation tools
    │       ├── UtilityStrip.{h,cpp}           // Lightweight secondary tool bar for viewport toggles
    │       ├── ContextDirectiveList.{h,cpp}   // Cursor-anchored contextual directives
    │       └── CommandLauncher.{h,cpp}        // Fuzzy-search directive runner
    │
    ├── NetworkingSpace/                       // Distributed simulation, replication, and remote directives
    │   ├── NetworkingExtension.{h,cpp}        // Lifecycle coordinator: polls sockets, ticks replication, flushes buffers
    │   ├── SocketTransport.{h,cpp}            // Non-blocking UDP datagram and TCP stream transport
    │   ├── PacketSerializer.{h,cpp}           // Bit-packed wire encoding, fragmentation, and checksum verification
    │   ├── SnapshotReplicator.{h,cpp}         // Authoritative delta compression and quantized world replication
    │   ├── RemoteDirectiveRouter.{h,cpp}      // Reliable ordered delivery and execution of remote directives
    │   └── PeerSessionRegistry.{h,cpp}        // Connected peer endpoints, round-trip telemetry, and clock alignment
    │
    └── AudioSpace/                            // Spatial acoustics, waveform decoding, and hardware mixing
        ├── AudioExtension.{h,cpp}             // Lifecycle coordinator: device output, mix ticks, and voice budgets
        ├── AudioHardwareOutput.{h,cpp}        // Native OS audio endpoint intake and lock-free ring buffers
        ├── WaveformDecoder.{h,cpp}            // PCM, WAV, Ogg Vorbis, and FLAC sample stream decoding
        ├── AcousticMixer.{h,cpp}              // Bus summing, gain envelopes, biquad filtering, and resampling
        └── SpatialAcoustics.{h,cpp}           // 3D HRTF binaural panning, Doppler pitch shift, and occlusion
```

---

## Structural Boundaries & Conventions

1. **Three-Profile Build Separation (`Editor` · `Development` · `Shipping`)**:
   - `Engine/Editor/` compiles and links **only** when `FRONTIER_PROFILE_EDITOR=1` (`-DFRONTIER_PROFILE=Editor`).
   - `Development` builds run standalone project logic with developer assertions, console, and GPU `.slang` debug overlays active **without binary packaging**.
   - `Shipping` builds completely exclude `Engine/Editor/` and strip diagnostics to produce standalone packaged binaries.
2. **100% GPU `.slang` Viewport Overlays**: Grids (`NavigationOverlay`), gizmos (`TransformGizmo`), and debug shapes/lines (`Engine/Annotation/*`) render exclusively on the GPU via `.slang` shaders — never on CPU and never via ImGui draw lists.
3. **Cross-Platform Portability**: All `Engine/` modules target **Windows**, **Linux (X11/Wayland)**, **Android (NDK)**, and **Headless (`PlatformWindowStub`)**.
4. **Project & Archive Isolation**: Concrete user titles reside under `Projects/<ProjectName>/`, stock archives reside under `EngineContent/`, and checksummed third-party libraries reside under git-ignored `ExternalPackages/`.
5. **Mandatory `Exhibits/Images/` Visual Proofs**: Every `Exhibits/*Validation` target renders deterministic CPU `.png` visual proofs into `Exhibits/Images/<Domain>/` demonstrating the runtime behavior without hardware GPU or host OS preconditions.
