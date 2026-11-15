# Frontier

**Frontier** is a cross-platform ISO C++20 engine and multi-document authoring platform targeting **Windows, Linux (X11 & Wayland), and Android**, uniting geometric modeling, surface parameterization, tristimulus color science, **Slang (`.slang`)** GPU rasterization and compute, multiphysics simulation, spatial acoustics, distributed replication, and interactive workspace tooling.

---

## Key Documentation

| Document | Description |
| :--- | :--- |
| **[CLAUDE.md](CLAUDE.md)** · **[AGENTS.md](AGENTS.md)** | Session entry point, non-negotiable GPU/`.slang`/cross-platform mandates, and startup sequence. |
| **[Documentation/FolderStructure.md](Documentation/FolderStructure.md)** | Canonical repository directory tree, file-level module layout, and structural boundaries. |
| **[Documentation/BannedWordsVocabulary.md](Documentation/BannedWordsVocabulary.md)** | Enforced vocabulary blacklist, restricted terms, approved replacements, and chronological rulings. |
| **[Documentation/CodingStandards.md](Documentation/CodingStandards.md)** | ISO C++20 baseline, explicit function prototyping, `concept`-constrained templates, and build profiles. |
| **[Documentation/ImportantNotes.md](Documentation/ImportantNotes.md)** | Hard architectural invariants (GPU-only overlays, ImGui 8-patch stack, `Frontier-OLD` reference rules). |
| **[Documentation/Tracker.md](Documentation/Tracker.md)** · **[Backlog.md](Documentation/Backlog.md)** | Active milestone roadmap (`Input/` + `Editor/` bringup) and deferred items ledger. |
| `Documentation/AgenticInstructions/` | Mandatory skill protocols (`SKILL-CompactOutputProtocol`, `SKILL-Emoji`, `SKILL-Naming`, `SKILL-Formatting`). |
| **[Experimental/README.md](Experimental/README.md)** | Ten staged reference editor UIs and studios (`FrontierEditor`, `ProjectZeroEditor`, `FluidEditor`, `FractureEditor`, `ParticleEditor`, `PhotometricEditor`, `ForceFieldEditor`, `VehicleLab`, `UVEditor`, `TextureStudio`) slated for shared C++20 `Engine/Components/` + `.slang` conversion. |

---

## Dependency & Toolchain Setup

All third-party libraries are pinned by SHA-256 in `Automation/` and extracted into git-ignored `ExternalPackages/` (including automatic application of the mandatory 8-patch ImGui stack `PatchA`–`PatchH`):

```bash
python3 Automation/PrepareDependencies.py
python3 Automation/PrepareShaderToolchain.py
python3 Automation/PrepareFonts.py
```

Configure and run verification across any of the three build profiles (`Editor`, `Development`, `Shipping`):

```bash
cmake -S . -B Build/Editor -G Ninja -DFRONTIER_PROFILE=Editor
cmake --build Build/Editor
ctest --test-dir Build/Editor --output-on-failure
```

---

## Build Profiles (`Editor` · `Development` · `Shipping`)

| Profile | CMake Flag | `Engine/Editor/` | Diagnostics & Assertions | Binary Packaging / CI |
| :--- | :--- | :--- | :--- | :--- |
| **Editor** | `-DFRONTIER_PROFILE=Editor` | Compiled & linked | Enabled | No packaging (runs in-place) |
| **Development** | `-DFRONTIER_PROFILE=Development` | Excluded | Enabled | No packaging (fast local flags) |
| **Shipping** | `-DFRONTIER_PROFILE=Shipping` | Excluded | Stripped | Packaged standalone binary + release CI gate |

---

## Engine Architecture (`Engine/`)

| Domain | Responsibilities |
| :--- | :--- |
| **`MicroUtils/`** | Monotonic region allocators (`RegionAllocator`), stale-detecting identity tokens (`TokenIssuer`, `TokenAuthentication`), contiguous packed bits (`PackedBits`), preallocated indexed slot recycling (`IndexedSlotRecycler`), and invariant assertions. |
| **`Configuration/`** | Compile-time profile policy (`BuildConfiguration`), hardware thread group allocation, render swapchain targets, project manifests, and workspace layout defaults. |
| **`MetricSpace/`** | Coordinate spaces, orthographic/perspective/screen projections, distance metrics, spatial extents, SIMD linear algebra, quaternions, and ray/volume intersection solvers. |
| **`SpatialAcceleration/`** | Broad-phase spatial structures: SAH BVH, loose octree, dynamic AABB tree, and sparse spatial grid hashing. |
| **`DistanceFieldSpace/`** | Voxel-backed signed distance fields, fast sweeping & Maurer distance transforms, trilinear evaluation, ray marching, and analytic SDF primitive solvers. |
| **`GeometryProcessing/`** | Polygon and directional half-edge surface complexes, QEM decimation, Catmull-Clark & Loop subdivision, non-manifold/boundary topology analysis, and discrete curvature calculation. |
| **`UVSpace/`** | LSCM/ARAP chart parameterization, scanline UV chart packing (`UVChartPacker`), high-distortion seam detection, harmonic conformal projection, and area/angle distortion metrics. |
| **`ColorSpace/`** | Tristimulus chromaticity spaces (CIE XYZ, Linear sRGB, ACEScg, Rec.2020, Oklab), transfer curves (sRGB, PQ, HLG), Bradford/CAT02 white-point adaptation, spectral distributions, and gamut compression. |
| **`GeometricRaster/`** | Render lifecycle coordination (`RenderExtension`), dependency-sorted workload scheduling, batched indirect draw encoding (`IndirectDrawEncoder`), GPU hardware device interface (`RenderDeviceInterface`), descriptor slot registration (`DescriptorSlotRegister`), and ordered post-processing (`RadiometricToneStack`, `VeilingGlareFilter`, `AmbientOcclusionFilter`). |
| **`Shaders/`** | Offline and runtime **Slang (`.slang`)** compilation (`ShaderCompiler`), descriptor/attribute reflection, variant permutation indexing, and microfacet surface, depth pre-raster, compute (`VoxelDistanceSweep`), and image-operation `.slang` shaders. |
| **`SimulationSpace/`** | Rigid-body mechanics (`PhysicsSpace`: `PhysicsExtension`, Velocity Verlet, PGS constraints, sweep-and-prune, GJK/EPA), GPU/CPU particulate dynamics (`ParticleSimulation`), SPH liquid & Eulerian gas solvers (`FluidSimulation`), and procedural interpolation, easing, spring dynamics, and sequencing (`Motion`). |
| **`Input/`** | Input lifecycle coordination (`InputExtension`) and bi-directional OS↔Engine signal, cursor-lock, and haptic exchange (`InputExchange`). |
| **`Windowing/`** | Cross-platform window & surface abstractions across Win32, X11, Wayland, Android (`ANativeWindow`), and headless validation stubs (`PlatformWindow*`). |
| **`Interchange/`** | Scene tree intake coordination and native format decoders for Wavefront OBJ, glTF 2.0 (JSON/GLB), and Filmbox FBX. |
| **`Annotation/`** | **100% GPU-rasterized** visual annotations (`AnnotationExtension`), 2D/3D vector callouts, billboarded text markers, spatial debug bounds, and wireframe primitives (`DiagnosticPrimitives`) — never drawn on CPU or ImGui. |
| **`Workspaces/`** | Multi-document session infrastructure (`WorkspaceExtension`), stale-detecting workspace tokens, domain authoring binders, and branching DAG document revision history (`Workspaces/Revision`). |
| **`Editor/`** | **Excluded from `Development` and `Shipping` builds.** Concrete authoring workspaces (`Modeling`, `UV`, `Texturing`, `Baking`), scene/project/content browsers & debug console, 3D/2D viewports, GPU `.slang` gizmos/grids (`TransformGizmo`, `NavigationOverlay`), and CPU/GPU/memory diagnostics. |
| **`Components/`** | Reusable UI controls: reflected property panels, tool ribbons & section blocks, strongly typed numerical/vector/enum controls, analytical unit-square vector icon rasterizers, and viewport overlays. |
| **`NetworkingSpace/`** | Distributed simulation (`NetworkingExtension`), non-blocking UDP/TCP socket transport, bit-packed packet serialization, authoritative snapshot replication, ordered remote directive routing, and peer session alignment. |
| **`AudioSpace/`** | Audio lifecycle coordination (`AudioExtension`), native OS audio hardware output (`AudioHardwareOutput`), PCM/WAV/Ogg/FLAC waveform decoding, bus mixing & filtering (`AcousticMixer`), and 3D HRTF spatial acoustics. |
