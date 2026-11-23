# 🧩 `Engine/`

> **TL;DR**: Cross-platform ISO C++20 engine root grouping 22 domain subdirectories across rendering, `.slang` shaders, geometry, B-rep CAD, materials, physics, spatial acoustics, networking, `.Space` codecs, UI components, and 9 authoring workspaces.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`AudioSpace/`](AudioSpace/README.md) | Real-time lock-free 3D spatial acoustics (`AudioExtension`), waveform decoding, VBAP/HRTF/Doppler, BVH4 occlusion, and native OS hardware endpoints |
| [`Codecs/`](Codecs/README.md) | Two-Tier Hybrid `.Space` container encoders and decoders (`SpaceContainerCodec`, `.ProjectSpace`, `.Revision`, `.GeometrySpace`, `.UVSpace`, `.MaterialSpace`, `.TextureSpace`, `.VectorSpace`, `.ColourSpace`, `.FontSpace`, `.Acoustic`, `.ComponentSpace`, `.SketcherSpace`) |
| [`ColourSpace/`](ColourSpace/README.md) | Colorimetric transforms (`CIE XYZ`, `Linear sRGB`, `ACEScg`, `Rec.2020`, `Display P3`, `Oklab`), transfer curves, ramps, and `ColourAliasRepository` |
| [`ComponentClass/`](ComponentClass/README.md) | Modular `ComponentClass` asset descriptors, constraints, light/camera properties, response curves, and `ComponentInstanceStorage` |
| [`Components/`](Components/README.md) | Reusable C++20 UI library (`PixelSpace`, `WorkspaceDockContainer`, `ControlCentrePanel`, `ContentBrowserPanel`, `OutlinerPanel`, `ViewportPanel`, `InspectorPanel`, studio controls, and vector icons) |
| [`Configuration/`](Configuration/README.md) | Compile-time `Editor` / `Development` / `Shipping` profile enforcement (`BuildConfiguration`), `InputSettings`, and `ProjectSpaceRepository` |
| [`Constants/`](Constants/README.md) | Shared compile-time mathematical, physical, acoustic, time, and engine constants (`64 B` container alignment, `215 MiB` / `750 MiB` revision ceilings) |
| [`Editor/`](Editor/README.md) | Editor Explorer (`ProjectBrowser` and `ContentBrowser`) compiled strictly under `FRONTIER_PROFILE_EDITOR=1` |
| [`GeometricRaster/`](GeometricRaster/README.md) | Vulkan 1.3 timeline-semaphore interchange, 64-bit visibility rasterization, `MatCap` & `OpenPbrDirect` world renderers, unified `Atmosphere/` (`PlanetaryClouds`), GPU baking, UV/vector rasterizers, and `TransformGizmo` |
| [`GeometrySpace/`](GeometrySpace/README.md) | Half-edge `PolygonNetwork`, `FacePartitionRegister`, analytic/sweep solids, topological selection, 6-solver fracture (`Fracture/`), and exact B-rep/NURBS kernel (`Parametric/`) |
| [`Input/`](Input/README.md) | Platform-neutral `InputExchange`, `InputExtension` lifecycle participant, and one-way `EditorInputExchange` → ImGui signal forwarder |
| [`Interchange/`](Interchange/README.md) | External industry format boundary: Wavefront OBJ, glTF 2.0, STEP, binary `FCT1` vehicle triangle streams, and CSS/SVG/Adobe/GIMP colour ramps |
| [`Lifecycle/`](Lifecycle/README.md) | Profile-gated lifecycles (`RuntimeLifecycle`, `WorkspaceLifecycle`, `EditorLifecycle` World Editor), `DesktopWorkspaceLaunch`, and 9 standalone desktop executables |
| [`MaterialSpace/`](MaterialSpace/README.md) | Universal 10-lobe OpenPBR 1.1 + Glinty `SurfaceMaterial`, `128 B` bindless `CompactMaterialRegister`, `BounceLayerSummary`, and `DynamicMaterialCompiler` |
| [`Mathematics/`](Mathematics/README.md) | Right-handed `Axis3D` / `Axis2D` coordinates, `SpatialTransform`, and `16 B`-aligned C++20 ↔ `.slang` layout structures |
| [`NetworkingSpace/`](NetworkingSpace/README.md) | Distributed simulation (`NetworkingExtension`), `'FP1'` CRC32C framing, sector-relative 24-bit sub-mm delta encoding, and UDP/STUN/relay transports |
| [`Shaders/`](Shaders/README.md) | Unified **Slang (`.slang`)** shader library across `Atmosphere/` (`PlanetaryClouds`), `Baking/`, `Colour/`, `ComponentEditor/`, `DirectLighting/`, `GeometricRaster/`, `GlobalIllumination/`, `MaterialSpace/`, `Sketcher/`, `Vector/`, and `Visibility/` |
| [`SimulationSpace/`](SimulationSpace/README.md) | JoltPhysics v5.6.0 rigid-body dynamics, breakable constraints, `BroadphaseSolver`, `VehicleDynamicsSolver`, and `FractureSimulationRunner` (`PhysicsSpace/`) |
| [`SpatialAcceleration/`](SpatialAcceleration/README.md) | 128-triangle hierarchical `ClusterPyramid` LODs with seam-locked QEM collapse and `128 B` 4-Wide SAH `WideBoundingTree` (`BVH4`) |
| [`Threading/`](Threading/README.md) | Lock-free synchronization (`SpinLock`, `AtomicQueue`, `CompletionCounter`, `ThreadProgressionTracker`), 4-tier `TaskScheduler` / `ExecutionChannels`, `WorkspaceThread`, and `AudioThread` |
| [`Windowing/`](Windowing/README.md) | Cross-platform window and surface interface (`PlatformWindowDesktop`, `PlatformWindowStub`) with directive masks and fullscreen transitions |
| [`Workspaces/`](Workspaces/README.md) | Multi-document session infrastructure (`WorkspaceCapabilityRegister`, `Revision/`) and 9 registered host workspaces (`FrontierWorld`, `UV`, `Pattern`, `Stamp`, `Baking`, `Fracture`, `Sketcher`, `Material`, `ComponentEditor`) |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's 58 CTest suites under `Exhibits/` (`ctest --test-dir Build/Editor --output-on-failure`).
