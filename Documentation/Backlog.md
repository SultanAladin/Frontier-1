# Frontier Engineering Backlog (Deferred Items)

Tracks intentionally deferred tasks, platform follow-ups, and future optimizations so active
milestones stay focused and zero technical debt goes undocumented. Move items to
[Tracker.md](Tracker.md) when scheduled for implementation.

---

## 1. Platform & Packaging Follow-Ups

| ID | Domain | Deferred Item | Target Trigger |
| -- | ------ | ------------- | -------------- |
| `BKL-001` | `Windowing/` · `Automation/` | Android NDK `GameActivity` / `ANativeWindow` packaging script (`PackageAndroidApk.py`) and Gradle-free APK/AAB bundler for `Shipping` builds. | After desktop + headless `PlatformWindow` & Vulkan surface validation complete. |
| `BKL-002` | `Windowing/` | Native Wayland `pointer-constraints-unstable-v1` and `relative-pointer-unstable-v1` cursor lock for fly-camera viewport navigation in `PlatformWindowWayland.cpp`. | Phase 2 Viewport Camera Navigation. |
| `BKL-003` | `Automation/Workflows/` | Cross-platform `Shipping` binary packaging workflow templates (Windows x64, Linux x64, Android ARM64). Keep retained under `Automation/Workflows/` (inactive) until explicit GitHub Actions approval. | First `SampleProject` `Shipping` milestone. |

---

## 2. Rendering, `.slang` Shaders & GPU Overlays

| ID | Domain | Deferred Item | Target Trigger |
| -- | ------ | ------------- | -------------- |
| `BKL-004` | `Shaders/Compiler/` | Live file-modification timestamp polling for `.slang` hot-recompilation in `Editor` and `Development` profiles (compiled out in `Shipping`). | Phase 3 `ShaderCompiler` implementation. |
| `BKL-005` | `Annotation/` · `Editor/Viewports/` | Dedicated `.slang` anti-aliased infinite-grid shader (`NavigationOverlay`), analytical 3D gizmo shader (`TransformGizmo`), and GPU SDF glyph callout rasterizer (`TextAnnotation`). | Phase 2–3 GPU Viewport Overlay bringup. |
| `BKL-006` | `Components/VectorIcons/` | GPU-resident analytical curve/icon sheet rasterizer (`VectorRasterizer`) superseding offline CPU ThorVG icon baking where dynamic resolution scaling is required. | Phase 3 UI & VectorIcon bringup. |

---

## 3. Input, Audio & Networking

| ID | Domain | Deferred Item | Target Trigger |
| -- | ------ | ------------- | -------------- |
| `BKL-007` | `Input/` | Multi-finger gesture chord recognizer (pinch-zoom, two-finger pan/orbit) over `InputExchange` touch streams for Android and touchscreen laptops. | After core `InputExtension` desktop/touch intake lands. |
| `BKL-008` | `Experimental/` → `Engine/` | Native ISO C++20 + `.slang` conversion of remaining reference studios (`PhotometricEditor`, `VehicleLab`) into `SimulationSpace/`, `GeometricRaster/`, and `Workspaces/`. (`UV`, `Pattern`, `Stamp`, `Baking`, `Fracture`, `Fluid`, `Material`, `Particle`, `ForceField`, `ParametricSketcher`, `ComponentEditor` are already landed in `Engine/Workspaces/` & `Engine/Editor/`.) | Dedicated studio conversion milestones. |
| `BKL-009` | `AudioSpace/` | Completed — `AudioHardwareOutput` ships Windows `WASAPI`, runtime-loaded Linux `PulseAudio`/`ALSA`, Android `AAudio`, and deterministic `HeadlessBuffer` WAV export (`AudioSpaceValidation`). | Moved to `Tracker.md`. |
| `BKL-010` | `NetworkingSpace/` | Optional DTLS / encrypted datagram framing over `SocketTransport` for WAN `Shipping` sessions. | `NetworkingExtension` Phase 2. |
| `BKL-011` | `Engine/Workspaces/` · `Engine/Editor/` · `Engine/Lifecycle/` | Remediate audited C++ banned-vocabulary violations catalogued in workspace `README.md` files (notably prohibited `<K-suffix>` symbols in `ParametricSketcher/`, `ComponentEditor/`, `Particle/`, `UV/`, `Fracture/`, `Texturing/`, plus `State`/`Frame`/`Card`/`Pill`/`Badge`/`Draw*`/`Get*`/`Set*` identifiers and `"All Presets & Laws"` UI copy in `ForcePresetBrowserPanel.cpp`). | Dedicated naming-compliance refactor passes. |
