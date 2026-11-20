# Frontier

**Frontier** is a cross-platform ISO C++20 engine and multi-document authoring platform targeting **Windows, Linux (X11 & Wayland), and Android**, uniting geometric modeling, surface parameterization, tristimulus colour science, **Slang (`.slang`)** GPU rasterization and compute, rigid-body and fracture mechanics, spatial acoustics, distributed replication, and interactive workspace tooling.

---

## Key Documentation

| Document | Description |
| :--- | :--- |
| **[CLAUDE.md](CLAUDE.md)** · **[AGENTS.md](AGENTS.md)** | Session entry point, automatic HTML-to-C++ screenshot & zero-simplification gates, and startup sequence. |
| **[Documentation/README.md](Documentation/README.md)** · **[Plans/README.md](Documentation/Plans/README.md)** | Documentation index and Markdown Studio architectural plans, reports & audits. |
| **[Documentation/FolderStructure.md](Documentation/FolderStructure.md)** | Canonical repository directory tree, file-level module layout, and structural boundaries. |
| **[Documentation/BannedWordsVocabulary.md](Documentation/BannedWordsVocabulary.md)** | Enforced vocabulary blacklist, restricted terms, approved replacements, and chronological rulings. |
| **[Documentation/CodingStandards.md](Documentation/CodingStandards.md)** | ISO C++20 baseline, explicit function prototyping, `concept`-constrained templates, and build profiles. |
| **[Documentation/ImportantNotes.md](Documentation/ImportantNotes.md)** | Hard architectural invariants (automatic HTML parity gate, GPU-only overlays, ImGui 8-patch stack). |
| **[Documentation/Tracker.md](Documentation/Tracker.md)** · **[Backlog.md](Documentation/Backlog.md)** | Active milestone roadmap across 9 registered workspaces, deferred items, and ground-up redo queue. |
| `Documentation/AgenticInstructions/` | Mandatory skill protocols (`SKILL-CompactOutputProtocol`, `SKILL-Emoji`, `SKILL-Naming`, `SKILL-Formatting`). |
| **[Engine/Workspaces/README.md](Engine/Workspaces/README.md)** | Master index of all 9 registered host workspaces (`WorkspaceCapabilityRegister`), UI controls, hotkeys & C++ naming audit. |
| **[Engine/Lifecycle/README.md](Engine/Lifecycle/README.md)** · **[Engine/Editor/README.md](Engine/Editor/README.md)** | Application lifecycles, 9 standalone desktop executables (`Frontier*Launch.cpp`), World Editor & `Explorer/`. |
| **[Experimental/README.md](Experimental/README.md)** · **[RemovedSdfGi/README.md](Experimental/RemovedSdfGi/README.md)** | Staged HTML/JS reference studios and preserved SDF / GI / Emissive-GI / Shadow baseline queued for ground-up redo. |

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
| **`AudioSpace/`** | Audio lifecycle (`AudioExtension`), native OS hardware output (`AudioHardwareOutput`), RIFF WAV/PCM decoding (`WaveformDecoder`), zero-alloc DSP mixing (`AudioBusAccumulator`), and 3D VBAP/HRTF/Doppler acoustics (`SpatialAcoustics`). |
| **`Codecs/`** | Native Two-Tier Hybrid `.Space` container encoders & decoders (`SpaceContainerCodec`, `ProjectCodec`, `RevisionCodec`, `GeometryCodec`, `UVCodec`, `MaterialCodec`, `TextureCodec`, `ImageCodec`, `VectorCodec`, `ColourCodec`, `FontCodec`, `AcousticCodec`, `ComponentSpaceCodec`, `SketcherSpaceCodec`). |
| **`ColourSpace/`** | Tristimulus spaces (`CIE XYZ`, `Linear sRGB`, `ACEScg`, `Rec.2020`, `Display P3`, `Oklab`), transfer curves (`sRGB`, `PQ`, `HLG`), colour ramps, procedural swatches, and `ColourAliasRepository`. |
| **`ComponentClass/`** | Modular component class descriptors, constraints, light/camera properties, response curves, and instance storage. |
| **`Components/`** | Reusable C++20 UI library: `PixelSpace`, `WorkspaceDockContainer`, `ControlCentrePanel`, `ContentBrowserPanel`, `OutlinerPanel`, `ViewportPanel`, `InspectorPanel`, `CapsuleButton`, `ScalarSlider`, `ExclusiveSelector`, `DropdownSelector`, `TextInputField`, `ConfirmationDialogue`, and vector icons. |
| **`Configuration/`** | Compile-time profile policy (`BuildConfiguration`), `InputSettings`, `ProjectSpaceRepository`, `EngineConfiguration`, and `WorkspaceConfiguration`. |
| **`Constants/`** | Shared compile-time mathematical (`MathConstants`), physical (`PhysicsConstants`), acoustic (`AcousticConstants`), time (`TimeConstants`), and engine limits (`EngineConstants`). |
| **`Editor/`** | **Excluded from `Development` and `Shipping` builds.** [`Explorer/`](Engine/Editor/README.md) (`ProjectBrowser`, `ContentBrowser`). |
| **`GeometricRaster/`** | Vulkan 1.3 timeline-semaphore interchange (`VulkanInterchange`), 64-bit visibility rasterization, `SolarShadowSequence`, `SpecularReflectionSequence`, `MatCap` & `OpenPbrDirect` world renderers (`TieredWorldRenderSequence`, `VulkanWorldRenderSequence`), unified [`Atmosphere/`](Engine/GeometricRaster/Atmosphere/README.md) (`PlanetaryClouds`, 3-LUT Sky, Sun/Moons/Stars, Fog, LensFlare), GPU baking, vector/UV rasterizers, and `TransformGizmo`. |
| **`GeometrySpace/`** | Half-edge `PolygonNetwork` (`FacePartitionRegister`), analytic/sweep solids, curvature & stretch analysis, topological selection, 6-solver Voronoi/convex fracture (`Fracture/`), and exact B-rep/NURBS CAD (`Parametric/`, see [Engine/GeometrySpace/Parametric/README.md](Engine/GeometrySpace/Parametric/README.md)). |
| **`Input/`** | Input lifecycle (`InputExtension`), bi-directional OS↔Engine signal/cursor/haptic exchange (`InputExchange`), and one-way ImGui signal forwarding. |
| **`Interchange/`** | External format translation: Wavefront OBJ, glTF 2.0, STEP, binary `FCT1` vehicle streams (`IndexedTriangleStreamInterchange`), and CSS/SVG/Adobe/GIMP colour ramps (`ColourInterchange`). |
| **`Lifecycle/`** | Profile-gated loops (`RuntimeLifecycle`, `WorkspaceLifecycle`, `EditorLifecycle` World Editor), `DesktopWorkspaceLaunch`, and 9 standalone desktop executables (`Frontier`, `FrontierEditor`, `FrontierUV`, `FrontierBaking`, `FrontierPattern`, `FrontierStamp`, `FrontierMaterial`, `FrontierFracture`, `FrontierSketcher`) — see [Engine/Lifecycle/README.md](Engine/Lifecycle/README.md). |
| **`MaterialSpace/`** | Universal 10-lobe OpenPBR 1.1 + Glinty `SurfaceMaterial`, 128 B `CompactMaterialRegister`, `BounceLayerSummary`, and time-varying `DynamicMaterialCompiler`. |
| **`Mathematics/`** | Right-handed `Axis3D`/`Axis2D` coordinates, `SpatialTransform`, and `16 B`-aligned C++20 ↔ `.slang` layouts. |
| **`NetworkingSpace/`** | Distributed simulation (`NetworkingExtension`), non-blocking UDP/STUN/ChaCha20-Poly1305 transport, `'FP1'` CRC32C framing, sector-relative delta encoding, and replication policies. |
| **`Shaders/`** | Unified **Slang (`.slang`)** library across `Atmosphere/` (single-source `PlanetaryClouds`), `Baking/`, `Colour/`, `ComponentEditor/`, `DirectLighting/`, `GeometricRaster/`, `GlobalIllumination/` (`SpecularGlassRaytrace`), `MaterialSpace/`, `Sketcher/`, `Vector/`, and `Visibility/`. |
| **`SimulationSpace/`** | JoltPhysics v5.6.0 rigid-body dynamics (`PhysicsSpace`: `PhysicsExtension`, breakable constraints, `BroadphaseSolver`, `VehicleDynamicsSolver`, `FractureSimulationRunner`). |
| **`SpatialAcceleration/`** | 128-triangle hierarchical `ClusterPyramid` LODs and compressed `128 B` 4-Wide SAH `WideBoundingTree` (`BVH4`). |
| **`Threading/`** | Lock-free synchronization (`SpinLock`, `AtomicQueue`, `CompletionCounter`, `ThreadProgressionTracker`), 4-tier `TaskScheduler` / `ExecutionChannels`, and dedicated `WorkspaceThread` & `AudioThread`. |
| **`Windowing/`** | Cross-platform window & surface abstractions across desktop (`Win32`, `X11`, `Wayland`) and headless validation (`PlatformWindowStub`). |
| **`Workspaces/`** | Multi-document infrastructure (`WorkspaceExtension`, `WorkspaceCapabilityRegister`), branching DAG history ([`Revision/`](Engine/Workspaces/Revision/README.md)), and 7 native studio directories: [`UV/`](Engine/Workspaces/UV/README.md), [`Texturing/`](Engine/Workspaces/Texturing/README.md) (`Pattern` · `Stamp` · `Baking`), [`Fracture/`](Engine/Workspaces/Fracture/README.md), [`ParametricSketcher/`](Engine/Workspaces/ParametricSketcher/README.md), [`Material/`](Engine/Workspaces/Material/README.md), and [`ComponentEditor/`](Engine/Workspaces/ComponentEditor/README.md). |
