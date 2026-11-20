# Frontier Engineering Backlog (Deferred Items & Ground-Up Redo Queue)

Tracks intentionally deferred tasks, ground-up domain redos, platform follow-ups, and scheduled
refactoring so active milestones stay focused and zero technical debt goes undocumented. Move items
to [Tracker.md](Tracker.md) when scheduled for active implementation.

---

## 1. Ground-Up Redo Queue (Removed Simplifications)

| ID | Domain | Queued Redo Scope | Baseline & Gate |
| -- | ------ | ----------------- | --------------- |
| `BKL-012` | `DistanceFieldSpace/` · `GeometricRaster/` · `Shaders/` · `Exhibits/` | **Priority Redo — Three-Order World Rendering ([`WorldRenderingRedoPlan.md`](Plans/WorldRenderingRedoPlan.md))**: **(Phase 0)** Purge all residual SDF & 3D Volume SDF baking (`GpuSdfBaker`, `VolumeSdf`, `.fvol`) from `Engine/`. **(1st)** Standalone Direct PBR (`MatCap` + `OpenPbrDirect`, `0` GI, `0` SDF). **(2nd)** Real-Time GI (`StokesContourIrradianceSequence` + `SurfelRadiosityLatticeSequence`) using **Shadow Candidate 1** (`HybridConeShadowSequence`) on `GtxLowEnd` and **exact `BVH4` / `RayQuery` shadows (`SolarFrustumShadow` + `PlaneAwareShadowFilter` + GPU Skinned `BVH4` Refit, `0 MB` SDF)** on `HighEnd`. **(3rd)** Unified **Deterministic Path Tracer** (`DeterministicPathTraceSequence`, `1:1` with `Experimental/DeterministicPathTracer/`) for both Games & Actual Scene Rendering via settings alone. | Preserved byte-exact in [`Experimental/RemovedSdfGi/`](../Experimental/RemovedSdfGi/README.md) + `Experimental/SdfLightingDemoCpp/` + `Experimental/DeterministicPathTracer/`. |
| `BKL-013` | `SimulationSpace/` · `Workspaces/` · `Shaders/` | **Un-Simplified 1:1 Redo — `Fluid`, `ForceField` & `Particle`**: Prior C++ ports were removed (`daed305`) because they substituted simplified approximations (e.g. basic sprite billboards instead of exact HTML GPU compute / strand / ribbon / surface solvers), reinvented non-matching UIs, and regressed shared engine components. Redo from scratch when scheduled by the user. | Must pass `CLAUDE.md` §2.1 (automatic Step 0 HTML `.png` capture, 1:1 `Engine/Components/` UI parity, exact solver/shader parity, zero collateral breakage). |

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
