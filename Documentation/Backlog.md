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
| `BKL-003` | `Automation/` · `.github/workflows/` | Linux x64 and Android ARM64 binary packaging workflows, and a `Shipping`-profile desktop executable. Windows x64 `Editor`-profile packaging is delivered (`windows-desktop-package.yml`, `PackageDesktopBinaries.py`). | First `SampleProject` `Shipping` milestone. |
| `BKL-010` | `.github/workflows/` | Trusted Authenticode signing step (SignPath Foundation, Azure Artifact Signing, or cloud-HSM certificate) replacing the `.pfx` `signtool` call; add an open-source licence file first. See `Plans/UVWorkspaceAuditAndWindowsPackaging.md` §4. | User obtains a signing identity. |
| `BKL-011` | `Engine/Codecs/` · `Engine/Workspaces/Revision/` | Rule on `SpaceExtensionCategory` / `ExtensionCategory` and `RecordParametricOperation` (barred `extension` / `Record`), then extend `AuditBannedVocabulary.py` gating beyond the UV workspace (526 findings repo-wide). | Next vocabulary ruling. |
| `BKL-012` | `Engine/Lifecycle/` | First launch of the packaged Windows executables on a physical Vulkan GPU (CI runners have no Vulkan driver). | Windows test machine available. |

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
| `BKL-008` | `Experimental/` → `Engine/` | Native ISO C++20 + `.slang` conversion of the 8 reference studios staged under `Experimental/` (`FluidEditor`, `FractureEditor`, `ParticleEditor`, `PhotometricEditor`, `ForceFieldEditor`, `VehicleLab`, `UVEditor`, `TextureStudio`) into `SimulationSpace/`, `UVSpace/`, `Editor/Workspaces/`, and `Shaders/`. | Staged after `GeometricRaster/` & `Shaders/Compiler/` bringup (`M2`–`M4`). |
| `BKL-008` | `AudioSpace/` | Android `AAudio` low-latency exclusive stream path alongside Windows `WASAPI` and Linux `PipeWire`/`ALSA` inside `AudioHardwareOutput`. | `AudioExtension` bringup. |
| `BKL-009` | `NetworkingSpace/` | Optional DTLS / encrypted datagram framing over `SocketTransport` for WAN `Shipping` sessions. | `NetworkingExtension` Phase 2. |
