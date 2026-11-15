# Active Plan: `.Space` Formats, Codecs, Revision & Workspace Folder Structure

> **Confirmed Decisions:**
> - **Option 1A (Natural Domain Mix with Revised `.Space` Set)** + **Option 2A (Case-Insensitive Load, Canonical PascalCase Write)**
> - **`.UVTile`** replaces `.UVChart` everywhere (never `UVChart`, never `Surface`).
> - **`OperationRecorder.{h,cpp}`** replaces `DeltaIntake.{h,cpp}`.
> - **`MetricTolerance.h`** replaces `PrecisionSpecification.h`.
> - **Codecs (`*Codec` = `*Encoder` + `*Decoder`)** handle native `.Space` containers; **`*Interchange`** handles external formats & `VulkanInterchange`; **`*Exchange`** handles live runtime transfer.

---

## 1. Confirmed `.Space` Container Registry (Option 1A + Option 2A)

Every native `.Space` container uses the **Two-Tier Hybrid Framing** (UTF-8 PascalCase TOML manifest at top + `64-byte`-aligned SFNT/TTF binary section directory at bottom with 64-bit FNV-1a checksums). Loaders match extensions **case-insensitively** (e.g. `.projectspace` or `.ProjectSpace`), and encoders write **canonical PascalCase**:

| # | Canonical Extension | Dedicated Codec (`Engine/Codecs/`) | Embedded Sections |
| :---: | :--- | :--- | :--- |
| 1 | **`.ColourSpace`** | `ColourCodec` (`ColourEncoder` / `ColourDecoder`) | `.Primaries` · `.WhitePoint` · `.TransferCurve` · `.Gamut` · `.Spectral` |
| 2 | **`.GeometrySpace`** | `GeometryCodec` (`GeometryEncoder` / `GeometryDecoder`) | `.Primitives` · `.Points` · `.Corners` · `.Facets` · `.Edges` · `.Normals` · `.Tangents` · `.UV0..UV7` · `.Creases` · `.UDIM` · `.UVTile` · `.Revision` |
| 3 | **`.FontSpace`** | `FontCodec` (`FontEncoder` / `FontDecoder`) | `.TypefaceGroup` · `.WeightList` · `.Metrics` |
| 4 | **`.ProjectSpace`** | `ProjectCodec` (`ProjectEncoder` / `ProjectDecoder`) | `.VisualAppearance` · `.FontSpace` · `.WorkspaceLayout` · `.InputSettings` · `.NotificationSettings` |
| 5 | **`.TextureSpace`** | `TextureCodec` (`TextureEncoder` / `TextureDecoder`) | `.ColourSpace` · `.UDIM` · `.UVTile` · `.MipChain` · `.ChannelPack` · `.BlockEncoding` · `.Revision` |
| 6 | **`.MaterialSpace`** | `MaterialCodec` (`MaterialEncoder` / `MaterialDecoder`) | `.ColourSpace` · `.BSDF` · `.Microfacet` · `.Subsurface` · `.ThinFilm` · `.Clearcoat` · `.ClosureNetwork` · `.LayerStack` · `.Revision` |
| 7 | **`.ImageSpace`** | `ImageCodec` (`ImageEncoder` / `ImageDecoder`) | `.ColourSpace` · `.Exposure` · `.Tone` · `.HDRI` · `.LayerStack` · `.Tiles` · `.Revision` |
| 8 | **`.VectorSpace`** | `VectorCodec` (`VectorEncoder` / `VectorDecoder`) | `.Curve` · `.Stencil` · `.LayerStack` · `.ColourSpace` · `.Revision` |
| 9 | **`.SimulationSpace`** | `SimulationCodec` (`SimulationEncoder` / `SimulationDecoder`) | `.RigidDynamics` · `.Constraints` · `.Collision` · `.Fluid` · `.ForceField` · `.Motion` · `.Revision` |
| 10 | **`.VolumetricSpace`** | `VolumetricCodec` (`VolumetricEncoder` / `VolumetricDecoder`) | `.SignedDistance` · `.DensityGrid` · `.Absorption` · `.Scatter` · `.Phase` · `.Cloud` · `.Revision` |
| 11 | **`.ParticleSpace`** | `ParticleCodec` (`ParticleEncoder` / `ParticleDecoder`) | `.SpawnSource` · `.Field` · `.Integration` · `.Fibre` · `.Trail` · `.Revision` |
| 12 | **`.UVSpace`** | `UVCodec` (`UVEncoder` / `UVDecoder`) | `.UDIM` · `.UVTile` · `.TexelDensity` · `.Seams` · `.Revision` |

---

## 2. Document Revision & Memory Caps (`215 MiB` / `750 MiB`)

- **Per-Document Budget (`215 MiB` default):** `ContentRegion` allocates `64-byte`-aligned blocks capped at `215 MiB` per open workspace document.
- **Global Session Budget (`750 MiB` default):** `RevisionExtension` coordinates all open workspace documents using a monotonic cross-document sequence stamp so both tab-local Undo/Redo and global Undo/Redo stay deterministic.
- **`OperationRecorder` (replaces `DeltaIntake`):** Records compact parametric operations and runs **Redo Purge → Prefix Fold** compaction when approaching `215 MiB` or `750 MiB`, folding oldest operations into the parametric baseline so reconstruction at any target resolution or `MetricTolerance` remains exact.

---

## 3. Canonical `Engine/` Folder Structure (Banned-Word Audited)

All banned terms (`RasterPasses` → `Rasterization`, `Pipeline` → `ExecutionSequence`, `UVActionToolbar` → `UVOperationToolbar`, `DeltaIntake` → `OperationRecorder`, `PrecisionSpecification` → `MetricTolerance`, `UVChart` → `UVTile`, zero `Mesh`/`Surface`/`State`/`Core`/`Hook`) have been normalized:

```text
Engine/
├── Constants/                                     // Shared compile-time physical, mathematical, engine & time constants
│   ├── MathConstants.h                            // Pi, Tau, Epsilon, DegToRad, RadToDeg & IEEE-754 bounds
│   ├── PhysicsConstants.h                         // Gravity, AirDensity, WaterDensity & FrictionLimits
│   ├── EngineConstants.h                          // MaxPlayers, DefaultTickRate, BufferCapacities & Limits
│   ├── TimeConstants.h                            // FixedDeltaTime, MicrosecondsPerSecond & WatchdogThresholds
│   ├── OpticalConstants.h                         // VacuumLightSpeed, PlanckConstant, CIEWhitePoints & IOR
│   └── AcousticConstants.h                        // ReferenceSoundPressure, DryAirSoundSpeed & OctaveBands
│
├── Mathematics/                                   // Shared algebraic, polynomial, sparse-linear & deterministic math
│   ├── ScalarFunctions.h                          // Branchless clamp, saturate, smoothstep, remap & fast reciprocal/rsqrt
│   ├── LinearAlgebra.h                            // SIMD-aligned 2D/3D/4D vector and matrix types
│   ├── VectorAlgebra.{h,cpp}                      // Dot, cross, Gram-Schmidt orthonormalization, reflection & refraction
│   ├── MatrixOperations.{h,cpp}                   // Affine, projective, inverse, transpose & normal-transpose solvers
│   ├── QuaternionRotation.{h,cpp}                 // Unit quaternions, spherical slerp & orthogonal basis conversion
│   ├── PolynomialCurves.{h,cpp}                   // Analytic Bézier, Hermite, Catmull-Rom, B-spline & root solvers
│   ├── SparseLinearSolver.{h,cpp}                 // Conjugate gradient & sparse Cholesky solvers (shared by UV & physics)
│   ├── TrigonometricFunctions.h                   // SIMD minimax sine, cosine, atan2 & spherical coordinate conversions
│   └── DeterministicRandom.h                      // Seeded PCG / SplitMix64 repeatable generator for parametric replay
│
├── MetricSpace/                                   // Coordinate spaces, tolerances, projections & spatial intersections
│   ├── MetricTolerance.h                          // Linear, angular, UV-weld & watertight sewing tolerances
│   ├── CoordinateSpace.h                          // Right-handed world, local & parameter-tile basis transforms
│   ├── CoordinateProjection.h                     // Orthographic, perspective & 2D viewport tile projections
│   ├── DistanceMetric.h                           // Euclidean, Manhattan, Chebyshev & geodesic metrics
│   ├── SpatialExtent.h                            // Axis-aligned and oriented bounding extents
│   └── IntersectionSolver.{h,cpp}                 // Ray-facet, ray-volume, plane-cut & volume intersections
│
├── ColourSpace/                                   // Tristimulus spaces, chromaticity & transfer curves
│   ├── ChromaticitySpace.{h,cpp}                  // CIE XYZ, Linear sRGB, ACEScg, Rec.2020 & Oklab transforms
│   ├── TransferFunction.{h,cpp}                   // Electro-optical & opto-electronic curves (sRGB, PQ, HLG)
│   ├── WhitePointAdaptation.{h,cpp}               // Bradford & CAT02 chromatic adaptation (D65, D60, D50)
│   ├── SpectralDistribution.{h,cpp}               // Wavelength tristimulus integration & Planckian Kelvin
│   └── GamutCompression.{h,cpp}                   // Out-of-gamut perceptual chroma compression
│
├── GeometrySpace/                                 // Analytic primitives, geometric analysis, selection & UV
│   ├── Primitives/                                // Parametric solid primitives & watertight sewing
│   │   ├── AnalyticPrimitives.{h,cpp}             // Box, cylinder, cone, frustum, sphere, torus & capsule
│   │   ├── RevolutionSweepPrimitives.{h,cpp}      // Lathe revolution, linear extrusion & profile sweeps
│   │   └── WatertightSewing.{h,cpp}               // Boundary edge welding, facet orientation & solid closure
│   │
│   ├── Analysis/
│   │   ├── CurvatureAnalysis.{h,cpp}              // Discrete mean and Gaussian differential curvature
│   │   └── StretchMeasurement.{h,cpp}             // Per-facet and island angle/area distortion metrics
│   │
│   ├── Selection/
│   │   ├── GeometrySelection.{h,cpp}              // Component pick resolution (Point, Edge, Facet, Loop)
│   │   ├── ComponentSelectionMask.{h,cpp}         // Bitmask filters for multi-component picking & paint masks
│   │   └── IslandFloodSelection.{h,cpp}           // Seam-bounded island flood expansion & UVTile tagging
│   │
│   └── UV/                                        // Parameterization & UVTile layout solvers
│       ├── TileLayout.{h,cpp}                     // Integer [TileU, TileV] UVTile coordinate grid
│       ├── UDIMTileAddressing.{h,cpp}             // UDIM 1001-series addressing and coordinate conversion
│       ├── TexelLayout.{h,cpp}                    // Target texel density [px/m] and pixel border padding
│       ├── TileSegmentation.{h,cpp}               // Seam-split UVTile island extraction & boundary loops
│       ├── SeamRouter.{h,cpp}                     // Dijkstra geodesic shortest-path & dihedral seam router
│       ├── ConformalFlattening.{h,cpp}            // LSCM and ABF++ linear parameterization solvers
│       ├── StretchRelaxer.{h,cpp}                 // Iterative area-preserving mass-spring UV relaxer
│       └── IslandLayoutPacker.{h,cpp}             // Scanline bin-packer targeting active UDIM UVTiles
│
├── Codecs/                                        // Native Frontier .Space container Encoders & Decoders
│   ├── SpaceContainerCodec.{h,cpp}                // Hybrid TOML + 64B-aligned binary section Encoder/Decoder
│   ├── RevisionCodec.{h,cpp}                      // .Revision parametric operation stream Encoder/Decoder
│   ├── ProjectCodec.{h,cpp}                       // ProjectEncoder / ProjectDecoder (.ProjectSpace)
│   ├── FontCodec.{h,cpp}                          // FontEncoder / FontDecoder (.FontSpace)
│   ├── ColourCodec.{h,cpp}                        // ColourEncoder / ColourDecoder (.ColourSpace)
│   ├── GeometryCodec.{h,cpp}                      // GeometryEncoder / GeometryDecoder (.GeometrySpace)
│   ├── UVCodec.{h,cpp}                            // UVEncoder / UVDecoder (.UVSpace & .UVTile)
│   ├── TextureCodec.{h,cpp}                       // TextureEncoder / TextureDecoder (.TextureSpace)
│   ├── MaterialCodec.{h,cpp}                      // MaterialEncoder / MaterialDecoder (.MaterialSpace)
│   ├── ImageCodec.{h,cpp}                         // ImageEncoder / ImageDecoder (.ImageSpace)
│   ├── VectorCodec.{h,cpp}                        // VectorEncoder / VectorDecoder (.VectorSpace)
│   ├── SimulationCodec.{h,cpp}                    // SimulationEncoder / SimulationDecoder (.SimulationSpace)
│   ├── VolumetricCodec.{h,cpp}                    // VolumetricEncoder / VolumetricDecoder (.VolumetricSpace)
│   └── ParticleCodec.{h,cpp}                      // ParticleEncoder / ParticleDecoder (.ParticleSpace)
│
├── Interchange/                                   // External industry format import/export & OS clipboard exchange
│   ├── GeometryInterchange.{h,cpp}                // OBJ, glTF 2.0, FBX & STEP geometry import/export
│   ├── UVInterchange.{h,cpp}                      // External UDIM layout & UVTile sheet import/export
│   ├── ImageInterchange.{h,cpp}                   // OpenEXR, PNG, Radiance HDR, WebP & KTX2 import/export
│   ├── TextureInterchange.{h,cpp}                 // BCn / ASTC compressed mipchain external interchange
│   ├── MaterialInterchange.{h,cpp}                // MaterialX & physical shading parameter interchange
│   ├── VectorInterchange.{h,cpp}                  // SVG 1.1 & PDF Bézier contour interchange
│   ├── FontInterchange.{h,cpp}                    // TrueType / OpenType sfnt outline & metric interchange
│   └── ClipboardExchange.{h,cpp}                  // Bi-directional OS ↔ Workspace clipboard exchange
│
├── GeometricRaster/                               // Vulkan interchange, GPU rasterization units & command encoding
│   ├── RenderExtension.{h,cpp}                    // Render lifecycle coordinator and device attachment
│   ├── RenderDeviceInterface.h                    // Hardware-agnostic GPU resource & capability limits
│   ├── VulkanInterchange.{h,cpp}                  // Vulkan volk device, swapchain, SPIR-V & semaphore interchange
│   ├── RenderScheduler.{h,cpp}                    // Dependency-sorted transient GPU workload scheduler
│   ├── IndirectCommandEncoder.{h,cpp}             // Instanced & indirect GPU command encoder
│   ├── DescriptorSlotRegister.{h,cpp}             // Descriptor layout and push-constant slot register
│   │
│   ├── Rasterization/                             // Dedicated GPU rasterization & projection units
│   │   ├── DirectionalLitRasterization.{h,cpp}    // Fast directional key/hemisphere lit viewport rasterization
│   │   ├── UVTileRasterization.{h,cpp}            // 2D UVTile grid, boundary lines, UDIM indices & tile fills
│   │   ├── EdgeHighlightRasterization.{h,cpp}     // Seam lines, sharp creases & active wireframe borders
│   │   ├── GroundPlaneRasterization.{h,cpp}       // Viewport reference plane rasterization
│   │   ├── WorldAxisGridRasterization.{h,cpp}     // 3D coordinate crosshair & origin distance grid
│   │   └── ShadowDepthProjection.{h,cpp}          // Directional light-space depth projection
│   │
│   ├── ExecutionSequence/                         // Stream uploaders & ordered GPU raster sequences
│   │   ├── GeometryVertexStreamUploader.{h,cpp}   // UV vertex and per-island GPU stream upload
│   │   └── GeometryRasterSequence.{h,cpp}         // Execution order: ShadowDepth → GroundPlane → Model → UVOverlay
│   │
│   └── PostProcess/
│       ├── PostProcessSequence.{h,cpp}
│       ├── RadiometricToneStack.{h,cpp}
│       ├── VeilingGlareFilter.{h,cpp}
│       └── AmbientOcclusionFilter.{h,cpp}
│
├── Shaders/
│   ├── Compiler/
│   │   ├── ShaderCompiler.{h,cpp}
│   │   ├── ShaderReflection.{h,cpp}
│   │   └── ShaderPermutationIndex.{h,cpp}
│   ├── Raster/
│   │   ├── DirectionalLitShading.slang            // Fast viewport directional key/rim/hemisphere lit shader
│   │   ├── MicrofacetShading.slang                // Physical microfacet PBR shader
│   │   └── DepthPreRaster.slang                   // Early-Z depth pre-rasterization shader
│   └── UV/
│       ├── UVTileRasterization.slang              // 2D UVTile coordinates, checker patterns & stretch heat
│       ├── EdgeHighlightRasterization.slang       // Screen-space anti-aliased wireframes & seam lines
│       ├── GroundPlaneRasterization.slang         // Procedural infinite ground plane & axis shader
│       └── ShadowDepthProjection.slang            // Directional light-space depth vertex projection
│
├── Workspaces/                                    // Headless multi-document session, revision & domain runtimes
│   ├── WorkspaceExtension.{h,cpp}                 // Workspace lifecycle coordinator & open tab registry
│   ├── WorkspaceDocument.{h,cpp}                  // Document identity, dirty tracking & codec dispatch
│   ├── WorkspaceRegistry.{h,cpp}                  // Active workspace lookup & extension routing
│   ├── WorkspaceToken.h                           // Generation-checked token with workspace domain tag
│   ├── WorkspaceExchange.{h,cpp}                  // Live in-memory cross-workspace sync (Geometry ↔ UV ↔ Texture)
│   │
│   ├── Revision/                                  // Parametric per-document & global revision engine
│   │   ├── RevisionExtension.{h,cpp}              // 750 MiB global undo budget & cross-document sequence order
│   │   ├── ContentRegion.{h,cpp}                  // 215 MiB per-document 64B-aligned monotonic block
│   │   ├── OperationRecorder.{h,cpp}              // Parametric operation recording & Redo-Purge / Prefix-Fold
│   │   ├── CursorResolution.{h,cpp}               // Active revision cursor & arbitrary-resolution replay
│   │   └── NonLinearHistory.{h,cpp}               // Directed acyclic revision history tree
│   │
│   ├── UV/                                        // Headless UV workspace runtime (Non-UI)
│   │   ├── UVWorkspaceLifecycle.{h,cpp}           // Headless lifecycle: Initialize → Tick → Finalize
│   │   ├── UVWorkspaceDocument.{h,cpp}            // Editable UV layers, UDIM sets & UVTiles
│   │   ├── RevisionTracker.{h,cpp}                // Selected revision tracker: cursor tracking & parametric replay
│   │   └── Operations/                            // Procedural non-UI execution logic
│   │       ├── UVUnwrapOperations.{h,cpp}         // Conformal unwrap, planar, box & spherical projections
│   │       ├── UVPackOperations.{h,cpp}           // Island packing, rotation alignment & border margins
│   │       ├── UVSeamOperations.{h,cpp}           // Mark, clear, loop-cut, and shortest-path seams
│   │       ├── UVTransformOperations.{h,cpp}      // 2D scale, rotate, translate, mirror & normalize
│   │       └── UVWeldAlignOperations.{h,cpp}      // Island stitch, edge weld & UVTile coordinate alignment
│   │
│   ├── Geometry/                                  // Headless GeometrySpace authoring runtime & operations
│   ├── Texture/                                   // Headless TextureSpace authoring runtime & operations
│   ├── Material/                                  // Headless MaterialSpace authoring runtime & operations
│   ├── Image/                                     // Headless ImageSpace authoring runtime & operations
│   ├── Vector/                                    // Headless VectorSpace authoring runtime & operations
│   ├── Simulation/                                // Headless SimulationSpace authoring runtime & operations
│   ├── Volumetric/                                // Headless VolumetricSpace authoring runtime & operations
│   └── Particle/                                  // Headless ParticleSpace authoring runtime & operations
│
└── Editor/                                        // ImGui Viewports, Panels & Modular UI (Editor Profile Only)
    ├── Viewports/                                 // Shared general-purpose 3D & 2D viewports
    │   ├── ProjectionViewport.{h,cpp}             // General-purpose 3D viewport & camera host (shared across all 3D tools)
    │   ├── ViewportShadingCategory.h              // Wireframe, SolidClay, ShadedMaterial, TexelChecker, StretchHeat, Lit
    │   ├── VectorCanvas.{h,cpp}                   // General-purpose 2D vector & paint canvas
    │   ├── TransformGizmo.{h,cpp}                 // GPU .slang 3D translation, rotation & scale manipulators
    │   └── NavigationOverlay.{h,cpp}              // GPU .slang reference grid, axes & orientation indicator
    │
    └── Workspaces/
        ├── UV/                                    // Modular UV Editor UI (pairs UVTileCanvas with shared 3D ProjectionViewport)
        │   ├── UVWorkspaceRegistration.{h,cpp}    // Docking layout registration & workspace entry
        │   ├── UVOutlinerPanel.{h,cpp}            // Island tree, priority groups & UDIM UVTile list
        │   ├── UVInspectorPanel.{h,cpp}           // Pack budgets, padding & texel density properties
        │   ├── Viewports/
        │   │   └── UVTileCanvas.{h,cpp}           // 2D UVTile / UDIM projection viewport (3D view uses ProjectionViewport)
        │   ├── Toolbars/
        │   │   ├── UVOperationToolbar.{h,cpp}     // Top strip: Unwrap, Pack, Relax & Stitch triggers
        │   │   ├── UVSeamEditingToolbar.{h,cpp}   // Edge cut, path mark, weld & seam mode toggles
        │   │   ├── UVTransformToolbar.{h,cpp}     // Island align, rotate 90°, flip & normalize tools
        │   │   └── UVDisplayOptionToolbar.{h,cpp} // Grid snap, checkerboard & distortion heatmap toggles
        │   └── Interactions/
        │       ├── UVSelectionInteractors.{h,cpp} // Point pick, marquee box & brush select routers
        │       ├── UVTransformGizmo.{h,cpp}       // 2D/3D interactive translation, rotation & scale handles
        │       └── UVSeamPickRaycaster.{h,cpp}    // 3D ray-facet intersection for live seam cutting & painting
        │
        ├── Geometry/                              // Modular Geometry Editor UI (panels, toolbars, interactions)
        ├── Texture/                               // Modular Texture Editor UI (panels, toolbars, interactions)
        ├── Material/                              // Modular Material Editor UI (panels, toolbars, interactions)
        ├── Image/                                 // Modular Image Editor UI (panels, toolbars, interactions)
        ├── Vector/                                // Modular Vector Editor UI (panels, toolbars, interactions)
        ├── Simulation/                            // Modular Simulation Editor UI (panels, toolbars, interactions)
        ├── Volumetric/                            // Modular Volumetric Editor UI (panels, toolbars, interactions)
        └── Particle/                              // Modular Particle Editor UI (panels, toolbars, interactions)
```

---

## 4. Mechanism-First Naming Replacements

*(Legend — Mechanism Clarity & 6-Gate Compliance: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest)*

### 4.1 Replacing `UVRevisionJournal.{h,cpp}` (`Engine/Workspaces/UV/`)

| Rank | Candidate | Physical Mechanism | Clarity | Notes |
| :---: | :--- | :--- | :---: | :--- |
| 🏆 🥇 | **`UVRevisionReplay.{h,cpp}`** | Replays parametric UV operations from baseline through active cursor and folds prefixes at any texel target | 🔴 | Applied in folder tree; pairs cleanly with `Workspaces/Revision/OperationRecorder.{h,cpp}` |
| 🥈 | **`UVOperationSequence.{h,cpp}`** | Ordered parametric UV operation sequence bound to `UVWorkspaceDocument` | 🔴 | Uses approved `Sequence` noun |
| 🥉 | **`UVOperationHistory.{h,cpp}`** | Branching UV operation history linked to `RevisionExtension` | 🚩 | Slightly overlaps `NonLinearHistory` |

### 4.2 Replacing `SnapshotReplicator.{h,cpp}` (`Engine/NetworkingSpace/`)

| Rank | Candidate | Physical Mechanism | Clarity | Notes |
| :---: | :--- | :--- | :---: | :--- |
| 🏆 🥇 | **`DifferentialWorldEncoder.{h,cpp}`** | Quantizes and bit-encodes changed world instance properties against each endpoint's acknowledged baseline | 🔴 | Applied in folder tree; aligns with Frontier's `Encoder` terminology |
| 🥈 | **`QuantizedInstanceSync.{h,cpp}`** | Synchronizes quantized instance transforms and simulation readings across endpoints | 🔴 | Direct instance-level focus |
| 🥉 | **`BaselineDeltaSerializer.{h,cpp}`** | Serializes per-tick differential world payloads relative to prior baselines | 🚩 | Overlaps `PacketSerializer` suffix |

### 4.3 Replacing `PeerSessionRegistry.{h,cpp}` (`Engine/NetworkingSpace/`)

| Rank | Candidate | Physical Mechanism | Clarity | Notes |
| :---: | :--- | :--- | :---: | :--- |
| 🏆 🥇 | **`NetworkEndpointRegister.{h,cpp}`** | Contiguous register of connected socket endpoints, round-trip latency readings, and monotonic clock offsets | 🔴 | Applied in folder tree; uses approved `Register` noun and avoids web `Session` / `Peer` |
| 🥈 | **`EndpointClockAlignment.{h,cpp}`** | Tracks remote socket timing windows, packet acknowledgements, and clock-offset alignment | 🔴 | Highlights timing synchronization |
| 🥉 | **`RemoteEndpointDirectory.{h,cpp}`** | Indexed directory of authenticated remote socket addresses and link metrics | 🚩 | Slightly broader than `Register` |

### 4.4 Replacing `AcousticMixer.{h,cpp}` (`Engine/AudioSpace/`)

| Rank | Candidate | Physical Mechanism | Clarity | Notes |
| :---: | :--- | :--- | :---: | :--- |
| 🏆 🥇 | **`AudioBusAccumulator.{h,cpp}`** | Sums polyphonic PCM voice streams across routed buses with gain envelopes, biquad IIR filtering, and resampling | 🔴 | Applied in folder tree; names exact floating-point bus accumulation rather than DJ `Mixer` |
| 🥈 | **`VoiceBusSumming.{h,cpp}`** | Sums active voice channels into output and sub-group buses with per-bus biquad filtering | 🔴 | Direct DSP summing term |
| 🥉 | **`WaveformBusRouter.{h,cpp}`** | Routes and weights decoded waveform channels across filtered output buses | 🚩 | Emphasizes routing over summing |

