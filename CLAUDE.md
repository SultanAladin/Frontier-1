# Claude & Agent Instructions for Frontier

Read this file and the mandatory documents in **§1** at the start of every session before writing
code, shaders, build scripts, or documentation.

---

## 1. Mandatory Session Startup Reading

| Order | Document | Scope |
| ----- | -------- | ----- |
| ① | `Documentation/AgenticInstructions/SKILL-CompactOutputProtocol.md` | Skimmer-first output, mandatory decision tables before choices |
| ② | `Documentation/AgenticInstructions/SKILL-Emoji.md` | Sole authority on permitted emojis (`🌟`/`✨` forbidden) |
| ③ | `Documentation/AgenticInstructions/SKILL-Naming.md` | Mechanism-first PascalCase construction & 6-gate test |
| ④ | `Documentation/AgenticInstructions/SKILL-Formatting.md` | 140-col file banners, 120-col sections, Allman braces, unit tags |
| ⑤ | `Documentation/BannedWordsVocabulary.md` | Canonical blacklisted terminology across code, files, folders, comments |
| ⑥ | `Documentation/CodingStandards.md` | ISO C++20 baseline, explicit function prototypes, `concept` templates |
| ⑦ | `Documentation/FolderStructure.md` | Authoritative repository & `Engine/` module layout |
| ⑧ | `Documentation/ImportantNotes.md` · `Tracker.md` · `Backlog.md` | Hard invariants, active milestones, and deferred work |

---

## 2. Non-Negotiable Architectural Mandates

### 🔴 2.1 All Debug Visuals & Viewport Overlays Render on the GPU via `.slang`
- **Every** viewport overlay — reference grids (`NavigationOverlay`), transform manipulators (`TransformGizmo`), wireframe debug lines/boxes/spheres/rays (`DiagnosticPrimitives`, `SpatialAnnotation`, `VectorAnnotation`), and world/screen text callouts (`TextAnnotation`) — **MUST be drawn directly on the GPU** via dedicated **Slang (`.slang`)** `…Rasterization` units.
- **NEVER** draw grids, gizmos, or debug overlays on the CPU.
- **NEVER** draw viewport grids, 3D gizmos, or debug geometry through ImGui (`ImDrawList`). ImGui is strictly restricted to docked UI panels, inspectors, tool ribbons, and chrome controls.
- **NEVER** scatter inline raw Vulkan draw state across feature/editor code; route all GPU execution through `Engine/GeometricRaster/` (`RenderDeviceInterface`, `IndirectDrawEncoder`) and `.slang` shaders.

### 🔴 2.2 Slang (`.slang`) Shader Standard
- All raster, compute, and image-operation shaders in `Engine/Shaders/` are written in **`.slang`** and compiled via `slangc` (`Automation/PrepareShaderToolchain.py`). Do not add `.glsl` or `.hlsl` files to `Engine/Shaders/`.

### 🔴 2.3 Cross-Platform Portability (Any Device: Windows · Linux · Android)
- Frontier is strictly cross-platform and must work cleanly on **Windows** (Win32 / x64 / ARM64), **Linux** (X11 & Wayland), **Android** (NDK / `ANativeWindow` / ARM64), and **Headless validation** (`PlatformWindowStub`).
- Confine OS-specific headers to `PlatformWindow{Win32,X11,Wayland,Android,Stub}.cpp` and platform device implementation `.cpp` files.
- Always provide portable scalar + ARM64 NEON + x86_64 SSE4.2 paths — never lock math or physics to x86-only intrinsics.

### 🔴 2.4 Three-Profile Build Architecture (`Editor` · `Development` · `Shipping`)
- **`Editor` (`-DFRONTIER_PROFILE=Editor` → `FRONTIER_PROFILE_EDITOR=1`)**: Full authoring editor (`Engine/Editor/` compiled & linked), multi-document workspaces, GPU overlays, and diagnostics. Runs in-place without packaging.
- **`Development` (`-DFRONTIER_PROFILE=Development` → `FRONTIER_PROFILE_DEVELOPMENT=1`)**: Standalone project runtime with developer flags, assertions, profiler, and GPU debug annotations active; `Engine/Editor/` excluded; runs directly from build output **without binary packaging or CI workflows**.
- **`Shipping` (`-DFRONTIER_PROFILE=Shipping` → `FRONTIER_PROFILE_SHIPPING=1`)**: Production release binary (`Engine/Editor/` excluded at CMake target level, assertions/diagnostics/annotations stripped, release optimizations enabled). Requires **standalone binary packaging** (`Automation/`) and gates release GitHub Actions (run GitHub Actions **only** with explicit user approval).

### 🔴 2.5 ImGui 8-Patch Stack, `Experimental/` HTML Reference & Visual Proofs in `Exhibits/`
- Third-party packages live in `ExternalPackages/` (git-ignored) and are fetched + SHA-256 verified via `python3 Automation/PrepareDependencies.py`.
- `ExternalPackages/imgui` (`3bae66c`) **requires all 8 stacked patches (`PatchA`–`PatchH`)** applied in order via `Automation/ApplyImGuiPatches.py` and compiled with `ImDrawIdx=unsigned int`.
- **`Experimental/` Prototypes Are HTML/JS Only (No C++)**: All reference studios and editor UIs under `Experimental/` (`ProjectZeroEditor`, `FrontierEditor`, `FluidEditor`, `FractureEditor`, `ParticleEditor`, `PhotometricEditor`, `ForceFieldEditor`, `VehicleLab`, `UVEditor`, `TextureStudio`) are strictly HTML/JS/CSS prototypes. **`Experimental/ProjectZeroEditor/`** is the primary UI design reference for shared C++20 components (`Engine/Components/`, `Engine/Workspaces/`, `Engine/Editor/`).
- **Mandatory Visual Proofs for Every `Exhibits/` Target (`Exhibits/Images/<Domain>/`)**:
  - Every validation executable in `Exhibits/` **MUST** render deterministic `.png` visual proof images into `Exhibits/Images/<Domain>/` via a CPU software rasterizer, SwiftShader offscreen Vulkan, or rendered diagnostic/comparison charts showing what the user would see when running the feature.
  - **Zero Excuses Rule**: Never claim a visual proof cannot be shown because the sandbox runs Linux instead of Windows or lacks a physical GPU — always synthesize and present the CPU/offscreen `.png` visual proof!

### 🔴 2.6 Never Write Plans, Reports, or Architecture Summaries in Chat — Use Markdown Studio
- **NEVER** dump architectural plans, decision tables, implementation reports, or multi-section explanations into chat messages.
- Write all plans, architectural decisions, and turn reports into **`Documentation/Plans/*.md`** (and/or **`Documentation/Plans/SessionNotes.md`**), rebuild **`Documentation/Plans/MarkdownStudioViewer.html`** via `python3 Automation/BuildMarkdownViewer.py`, keep `Automation/ServeMarkdownStudio.py` live on `0.0.0.0:8080`, and open the primary deliverable `.md` or `.png` file in the viewer.
- Keep chat replies strictly to **1–3 short lines** pointing to the updated Markdown Studio document path and verification status.

---

## 3. Initial C++ Bringup Order

1. **Dependencies & Build Profiles** (`Automation/`, `ExternalPackages/`, `Engine/Configuration/BuildConfiguration.h`, `CMakeLists.txt`).
2. **Input & Cross-Platform Windowing** (`Engine/Input/{InputExchange,InputExtension}.{h,cpp}`, `Engine/Windowing/PlatformWindow*`).
3. **Editor & Workspace Groundwork** (`Engine/Workspaces/`, `Engine/Editor/` target gated off `FRONTIER_EDITOR_ENABLED`, patched ImGui docking host, GPU `.slang` viewport overlay hooks).
