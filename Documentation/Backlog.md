# Frontier Engineering Backlog (Deferred Items & Ground-Up Redo Queue)

Tracks intentionally deferred tasks, ground-up domain redos, platform follow-ups, and scheduled
refactoring so active milestones stay focused and zero technical debt goes undocumented. Move items
to [Tracker.md](Tracker.md) when scheduled for active implementation.

---

## 1. Ground-Up Redo Queue (Removed Simplifications)

| ID | Domain | Queued Redo Scope | Baseline & Gate |
| -- | ------ | ----------------- | --------------- |
| `BKL-012` | `DistanceFieldSpace/` · `GeometricRaster/` · `Shaders/` · `Exhibits/` | **Completed (`2026-11-20`) — Three-Order World Rendering ([`WorldRenderingRedoPlan.md`](Plans/WorldRenderingRedoPlan.md) · [`WorldRenderingRedoReport.md`](Plans/WorldRenderingRedoReport.md))**: Phase 0 SDF/`.fvol` purge (`757b5c6`), Order 1 Standalone Direct PBR + `.ies` photometry (`4294a70`), Order 2 `GtxLowEnd` (`<= 3.7 MB` local `C0` SDF) + `HighEnd` (`0 B` SDF `BVH4` refit) Real-Time GI (`18b3329`), and Order 3 Unified Vulkan 1.3 + Slang Deterministic Path Tracer (`GameRealtime` & `SceneFinalRender`) + `DeterministicPathTracerPane` (`DeterministicPathTracerValidation`). | Moved to [`Tracker.md`](Tracker.md) Phase 6. |
| `BKL-013` | `SimulationSpace/` · `Workspaces/` · `Shaders/` | **Complete End-to-End 5-Domain Native Engine Stack + Phase 2 GPU Pipeline Merge-Prep + Phase G/H/I/J/K/L/M GPU Compute, Checkpoints, Governor, Profiler, Interaction, Triggers, Scene Library, Event Timeline, Spatial Query, Replay Recorder, Acoustic Bridge, Aero-Buoyancy, Parallel Task Dispatcher, `.VertexAtlas` Fleet Table & Fracture Coupling Shipped (`D01`–`D35` + `U01`–`U06` + `G01`–`G04` + `H01`–`H04` + `I01`–`I04` + `J01`–`J04` + `K01`–`K04` + `L01`–`L04` + `M01`–`M04`, `702/702` PASS); Standalone Sim Studio UI Deferred (`S19`–`S23`)**: Native C++20 + `.slang` `ForceFields`, `Fluid` (3D Eulerian + Sparse Infinite + `54` `.FluidGrid` presets), `ParticleSpace`, and `Deformable` XPBD/Neo-Hookean FEM/Cage/VAT (`D01`–`D35`, `9` `.SoftBody` presets, `2` shipped `.VertexAtlas` assets, `SimulationCoordinatorExtension`, `SimulationSceneDocument`, `ComponentClass` simulation PODs + validator + ComponentEditor UI/Play preview + `ComponentClassSimulationAssembler` + `WorldSimulationSession` wired into `EditorLifecycle`, all `37` `.slang` shaders wired in `CMakeLists.txt`), Phase 2 GPU Pipeline Merge-Prep (`U01`–`U06`: real async Vulkan 1.2 compute via `VulkanInterchange` + isolated `ComputeDispatchEncoder` + `DispatchCommandBufferAsync` across Deformable/Fluid/Particle executors, 3-slot non-blocking `AsyncGpuReadbackRing` via `QueryTimelineReached`, `0` mid-frame GPU stalls, `0` per-tick heap allocations proven via instrumented `operator new`, 6 contiguous SoA `DualLoopSimulationPools` with `0.00 ms` fast-skip & `#if FRONTIER_PROFILE_EDITOR` Shipping split, and full `WorldSimulationSession`/`ComponentSimulationPreviewDriver` GPU interchange propagation), Phase G (`G01`–`G04`: all `15` additive ParticleSpace Layer 220/221 `.comp.spv` physics programs + Fluid MacCormack/Tracer/Debris compute programs wired, async `VertexMotionAtlasGpuBaker`, and zero-allocation `SimulationGpuRenderFeedPacket` for direct World Renderer consumption), Phase H (`H01`–`H04`: zero-copy GPU Fluid→Particle 3D velocity image binding, FNV-1a-verified zero-alloc `SimulationCheckpointBuffer` with bit-identical rewind/replay & binary disk IO, and hysteresis-stabilized `SimulationBudgetGovernor`), Phase I (`I01`–`I04`: full 7-pass Layer 220 GPU XPBD dispatch + 3-pass Selle et al. Fluid MacCormack + Tracer/Debris async dispatch, zero-alloc `SimulationTelemetryProfiler` p50/p95/p99 percentiles, and integrated `WorldSimulationSession` BudgetGovernor + CheckpointRing + `StepSingleTick`), Phase J (`J01`–`J04`: zero-alloc `SimulationInteractionBridge` SoftBody ray-pick/grab/drag/release + Fluid brush stroke + coupled Fluid+SoftBody radial blast impulse, zero-alloc `SimulationTriggerRouter` Enter/Stay/Exit edge routing over `TriggerDomainPool`, and 4 shipped `.VolumetricSpace` scenes enumerated via `SimulationSceneCodec::EnumerateShippedScenes`), Phase K (`K01`–`K04`: zero-alloc `SimulationEventTimeline` deterministic multi-domain action sequencer with FNV-1a binary persistence & rewind replay, zero-alloc `SimulationSpatialQuery` Fluid/ForceField/SoftBody point/sphere/bounds probes, and full `WorldSimulationSession` integration), Phase L (`L01`–`L04`: zero-alloc `SimulationReplayRecorder` deterministic interactive capture & FNV-1a binary `.SimReplay` stream with bit-identical rewind playback, zero-alloc `SimulationAcousticBridge` spatial wind/shockwave/tear acoustic + haptic cue packet synthesis, and zero-alloc `SimulationAeroBuoyancyBridge` Fluid drag + thermal buoyancy + ForceField rigid-body integration onto `PhysicsDomainPool`), and Phase M (`M01`–`M04`: zero-alloc `SimulationParallelDispatcher` fanning 4 `DualLoopSimulationPools` across `ExecutionChannels` with 64/64-bit serial FNV-1a parity, zero-alloc `VertexAtlasFleetTable` 16-clip/256-instance GPU-ready `.VertexAtlas` fleet with distance LOD culling & CPU sampling, and zero-alloc `SimulationFractureCouplingBridge` shattering Fluid dust + golden-angle ballistic shards + SoftBody shock + acoustic blast cues). Only Stage E standalone simulation studio UI panels (`S19`–`S23`) remain deferred pending user UI design. | See [`UnifiedFluidParticleForceFieldPortPlan.md`](Plans/UnifiedFluidParticleForceFieldPortPlan.md) & [`DeformableFemVatArchitecturePlan.md`](Plans/DeformableFemVatArchitecturePlan.md). |

---

## 2. Platform & Packaging Follow-Ups

| ID | Domain | Deferred Item | Target Trigger |
| -- | ------ | ------------- | -------------- |
| `BKL-001` | `Windowing/` · `Automation/` | Android NDK `GameActivity` / `ANativeWindow` packaging script (`PackageAndroidApk.py`) and Gradle-free APK/AAB bundler for `Shipping` builds. | After desktop + headless `PlatformWindow` & Vulkan surface validation complete. |
| `BKL-002` | `Windowing/` | Native Wayland `pointer-constraints-unstable-v1` and `relative-pointer-unstable-v1` cursor lock for fly-camera viewport navigation in `PlatformWindowWayland.cpp`. | Phase 2 Viewport Camera Navigation. |
| `BKL-003` | `Automation/Workflows/` | Cross-platform `Shipping` binary packaging workflow templates (Windows x64, Linux x64, Android ARM64). Keep retained under `Automation/Workflows/` (inactive) until explicit GitHub Actions approval. | First `SampleProject` `Shipping` milestone. |

---

## 3. Rendering, `.slang` Shaders & GPU Overlays

| ID | Domain | Deferred Item | Target Trigger |
| -- | ------ | ------------- | -------------- |
| `BKL-004` | `Shaders/Compiler/` | Live file-modification timestamp polling for `.slang` hot-recompilation in `Editor` and `Development` profiles (compiled out in `Shipping`). | Phase 3 `ShaderCompiler` implementation. |
| `BKL-005` | `Annotation/` · `GeometricRaster/Gizmo/` | Dedicated `.slang` anti-aliased infinite-grid shader (`NavigationOverlay`), analytical 3D gizmo shader (`TransformGizmo`), and GPU SDF text callout rasterizer (`TextAnnotation`). | Phase 2–3 GPU Viewport Overlay bringup. |
| `BKL-006` | `Components/VectorIcons/` | GPU-resident analytical curve/icon sheet rasterizer (`VectorRasterizer`) superseding offline CPU ThorVG icon baking where dynamic resolution scaling is required. | Phase 3 UI & VectorIcon bringup. |

---

## 4. Input, Audio, Networking & Naming Remediation

| ID | Domain | Deferred Item | Target Trigger |
| -- | ------ | ------------- | -------------- |
| `BKL-007` | `Input/` | Multi-finger gesture chord recognizer (pinch-zoom, two-finger pan/orbit) over `InputExchange` touch streams for Android and touchscreen laptops. | After core `InputExtension` desktop/touch intake lands. |
| `BKL-008` | `Experimental/` → `Engine/` | Native ISO C++20 + `.slang` conversion of remaining staged reference studios (`PhotometricEditor`, `VehicleLab`, `ClothEditor`, `GeometryCompiler`, `LevelDesigner`). *(Active 9 host workspaces in `Engine/Workspaces/` & `Engine/Lifecycle/`: `FrontierWorld`, `UV`, `Pattern`, `Stamp`, `Baking`, `Fracture`, `Sketcher`, `Material`, `ComponentEditor`.)* | Dedicated studio conversion milestones under `CLAUDE.md` §2.1. |
| `BKL-009` | `AudioSpace/` | Completed — `AudioHardwareOutput` ships Windows `WASAPI`, runtime-loaded Linux `PulseAudio`/`ALSA`, Android `AAudio`, and deterministic `HeadlessBuffer` WAV export (`AudioSpaceValidation`). | Moved to `Tracker.md`. |
| `BKL-010` | `NetworkingSpace/` | Optional DTLS / encrypted datagram framing over `SocketTransport` for WAN `Shipping` sessions. | `NetworkingExtension` Phase 2. |
| `BKL-011` | `Engine/Workspaces/` · `Engine/Editor/` · `Engine/Lifecycle/` | Remediate audited C++ banned-vocabulary violations catalogued in workspace `README.md` files (notably prohibited `<K-suffix>` symbols in `ParametricSketcher/`, `ComponentEditor/`, `UV/`, `Fracture/`, `Texturing/`, plus `State`/`Frame`/`Card`/`Pill`/`Badge`/`Draw*`/`Get*`/`Set*` identifiers). | Dedicated naming-compliance refactor passes. |
