# Important Notes — Architectural & Migration Invariants

Durable engineering rules, non-negotiable architectural invariants, and reference policies for
**Frontier**. Newest and highest-severity mandates sit at the top.

---

## 🔴 1. GPU-ONLY VIEWPORT OVERLAYS, VULKAN 1.3, RIGHT-HAND RULE & `1–5%` PIXEL-DELTA QUALITY GATE (2026-11-10)

- **Vulkan 1.3 & Right-Hand Coordinate Rule (`Engine/Constants/EngineConstants.h`)**:
  - Frontier targets **Vulkan 1.3** (`VulkanTargetMajorVersion = 1`, `VulkanTargetMinorVersion = 3`) with **Reverse-Z `[1.0 -> 0.0]`** clip-space depth and the **Right-Hand Rule** (`RightHandedCoordinateRule = true`: `+X` Forward, `+Y` Left, `+Z` Up; $\hat{\mathbf{X}} \times \hat{\mathbf{Y}} = +\hat{\mathbf{Z}}$).
  - Ray dispatch follows a 3-tier hardware capability policy: **RTX dedicated `VK_KHR_ray_tracing_pipeline` (+ SER)** for divergent secondary bounces/reflections, **inline `VK_KHR_ray_query`** for coherent shadow/visibility rays & RDNA2+/Arc hardware, and **Vulkan 1.3 Subgroup 4-Wide `CWBVH` (`WideBoundingTree`) + Hi-Z** on GTX cards where `VK_KHR_ray_query` is unavailable. *(Active World renderer runs `MatCap` & `OpenPbrDirect`; SDF/GI re-architecture is staged from `Experimental/RemovedSdfGi/`.)*
- **Non-Degrading Speedup Quality Gate (`<= 1–5%` Pixel Delta · Zero Checkerboard)**:
  - 🔴 **NEVER** introduce checkerboard rendering, interleaved half-pixel skipping, or spatial subsampling shortcuts that degrade image quality.
  - Every BVH, path-tracing, reflection, or shader optimization **MUST** verify via before/after `.png` `Exhibits/` comparison that pixel difference stays within **`1%–5%` (`Constants::MaximumOptimizationPixelDelta = 0.05f`)** or strictly improves converged SNR against a high-SPP reference.
- **100% GPU Rasterization for Visual Overlays**: All debug visuals, viewport reference grids (`NavigationOverlay`), 3D/2D transform manipulators (`TransformGizmo`), wireframe diagnostic shapes/lines/rays (`DiagnosticPrimitives`, `SpatialAnnotation`, `VectorAnnotation`), and world/screen callouts (`TextAnnotation`) **MUST be drawn directly on the GPU** via dedicated `.slang` `…Rasterization` units.
- **Never Draw Viewport Overlays on CPU or ImGui**:
  - 🔴 **NEVER** rasterize grids, gizmos, or debug lines on the CPU.
  - 🔴 **NEVER** submit viewport grids, 3D gizmos, or debug wireframes through ImGui (`ImDrawList`). ImGui is strictly for docked editor panels, inspectors, ribbons, and chrome controls.
  - 🔴 **NEVER** scatter ad-hoc raw Vulkan draw boilerplate across editor/engine tools; all GPU draw encoding routes cleanly through `GeometricRaster/` (`RenderDeviceInterface`, `IndirectDrawEncoder`) backed by **Slang (`.slang`)** shaders.
- **Slang (`.slang`) Exclusively**: Every shader in `Engine/Shaders/` is authored in `.slang` and compiled via `slangc` (`Automation/PrepareShaderToolchain.py`). Do not introduce raw `.glsl` or `.hlsl` shader sources.

---

## 🔴 2. CROSS-PLATFORM PORTABILITY MANDATE — WINDOWS · LINUX · ANDROID (2026-11-10)

- **Universal Device Support**: Frontier is architected from day one to compile and run on **Windows** (Win32/DXGI/Vulkan), **Linux** (X11 & Wayland), **Android** (NDK / `ANativeWindow` / ARM64), and **Headless validation** (`PlatformWindowStub`).
- **Platform Isolation**:
  - Keep OS headers strictly inside `PlatformWindow{Win32,X11,Wayland,Android,Stub}.cpp` and hardware-endpoint `.cpp` files.
  - Note that `glfw` covers desktop windowing only; Android uses `PlatformWindowAndroid.cpp` (`ANativeWindow` / `GameActivity`), so `Engine/Input/` and `Engine/Windowing/` expose platform-neutral C++20 interfaces supporting keyboard, mouse, controller, **and multi-touch/stylus** events natively.
  - Never hardcode x86-only SIMD intrinsics without an ARM64 NEON / scalar path.

---

## 🔴 3. CRUCIAL IMGUI 8-PATCH STACK INVARIANT (`PatchA`–`PatchH`) (2026-11-10)

- `ExternalPackages/imgui` (pinned at `3bae66c735670619baf51391eba7f3d90a25d125`) **MUST** have all eight stacked patches in `Automation/Patches/` applied in exact order `A → H` via `Automation/ApplyImGuiPatches.py`:
  1. `PatchA-TrapezoidalTabs.patch`
  2. `PatchB-TabOverlapZOrder.patch`
  3. `PatchC-RoundTabButtons.patch`
  4. `PatchD-TabAddButton.patch`
  5. `PatchE-NoTabScrollButtons.patch`
  6. `PatchF-TabContentInsets.patch`
  7. `PatchG-AdjacentTabAddButton.patch`
  8. `PatchH-SmoothOverscroll.patch`
- `CMakeLists.txt` runs `python3 Automation/PrepareDependencies.py --verify` at configure time (which verifies all 8 package pins **and** all 8 ImGui patch tags) and defines `ImDrawIdx=unsigned int` on `FrontierImGui` so dense thumbnail/icon draw lists never overflow 16-bit indices.
- Never edit files inside `ExternalPackages/` manually; any third-party adjustment must be a reproducible script/patch in `Automation/` (such as `ApplyImGuiPatches.py` and `StageThorVG.py`).

---

## 🔴 4. REFERENCE-ONLY / ANTI-DUPLICATION POLICY FOR `Frontier-OLD` (2026-11-10)

Reference repository: `https://github.com/egoistm1inbox/Frontier-OLD/tree/arena/d96da802-frontier`

| Scenario When Consulting `Frontier-OLD` | Mandatory Action |
| --------------------------------------- | ---------------- |
| Verified dependency pin, patch, or algorithm needed | Reference `Frontier-OLD` for exact pins/math, then author clean C++20 / `.slang` code conforming to **this** repository's `FolderStructure.md`, `BannedWordsVocabulary.md`, and `CodingStandards.md`. |
| Equivalent capability already exists in `Frontier` | 🔴 **Do NOT duplicate.** Wire callers into the existing `Frontier` implementation. |
| Legacy file in `Frontier-OLD` contains banned vocabulary (`Hub`, `Bloom`, `ObjectReservoir`, `Dispatcher`, `.glsl`, ImGui gizmos) | 🔴 **Never blind-copy.** Refactor to compliant mechanisms, C++20 concepts/templates, and `.slang` GPU units before landing here. |

---

## 🔴 5. THREE-PROFILE BUILD POLICY (`Editor` · `Development` · `Shipping`) (2026-11-10)

- **`FRONTIER_PROFILE_EDITOR`** (`-DFRONTIER_PROFILE=Editor`): Compiles and links `Engine/Editor/`, authoring workspaces, inspectors, console, and GPU `.slang` overlays. Runs directly from the build tree without packaging.
- **`FRONTIER_PROFILE_DEVELOPMENT`** (`-DFRONTIER_PROFILE=Development`): Standalone project runtime with developer compile flags, assertions, profiler, and GPU debug annotations active, while `Engine/Editor/` is unlinked. Runs directly against loose project content **without binary packaging or CI workflows**.
- **`FRONTIER_PROFILE_SHIPPING`** (`-DFRONTIER_PROFILE=Shipping`): Production release binary (`Engine/Editor/` excluded at CMake target level, assertions/diagnostics/annotations stripped, full optimizations enabled). Produces packaged standalone binary archives and gates release GitHub Actions (which require explicit user approval before triggering).

---

## 🔴 6. AUTOMATIC HTML-TO-C++ SCREENSHOT GATE, ZERO SIMPLIFICATIONS & MANDATORY `Exhibits/Images/` PROOFS (2026-11-20)

- **Automatic Step 0 HTML Screenshot & 1:1 Fidelity Gate (Run Unprompted Before Any Code)**:
  - 🔴 **Capture HTML Screenshots Automatically First**: Before authoring or editing any C++ or `.slang` for an HTML-to-C++ conversion or UI/renderer sync, serve the HTML prototype (`Experimental/*`), capture live browser `.png` screenshots across every relevant tab, drawer, inspector section, and viewport mode, and inspect them via `read_file` alongside the HTML/JS/CSS/shader source. Never wait for the user to ask for HTML screenshots.
  - 🔴 **Zero Reinvented UI**: Reproduce the exact HTML layout, proportions, headers, icons, tabs, and controls using `Engine/Components/` (`PixelSpace`, `WorkspaceDockContainer`, `ControlCentrePanel`, `ContentBrowserPanel`, `OutlinerPanel`, `ViewportPanel`, `InspectorPanel`, `CapsuleButton`, `ExclusiveSelector`, `DropdownSelector`, `ScalarSlider`, `TextInputField`, `ConfirmationDialogue`). Never invent divergent layouts or raw `ImGui::*` substitutes.
  - 🔴 **Zero Simplified Solvers & Zero Rendering Deception**: Port the **exact** mathematical, simulation, and GPU shader pipeline of the HTML source — never substitute simplified approximations (such as basic billboard sprites in place of full GPU compute / strand / ribbon / surface solvers), never break shared engine components, and never misrepresent or falsify what is actually rendering on screen.
- **Every `Exhibits/` Target Produces `.png` Visual Proofs**:
  - When an `Exhibits/*Validation` executable runs, it **MUST** write deterministic `.png` visual proof images into `Exhibits/Images/<Domain>/` — either a CPU software mirror of the UI/window/surface or a visual comparison/telemetry chart showing exactly what the user would see when running it.
  - **"It compiles" is never proof, and OS/GPU excuses are forbidden**: Never tell the user that a visual proof cannot be rendered because the environment is Linux rather than Windows or lacks a hardware GPU. Always render the visual proof on CPU / SwiftShader and present the resulting `.png` image(s).
  - Note the strict separation: **Engine runtime & Editor viewports** draw overlays 100% on GPU via `.slang` (§1), whereas **`Exhibits/` headless verification binaries** include a self-contained CPU rasterizer / image encoder solely to emit `.png` visual proofs into `Exhibits/Images/<Domain>/`.
- **`Experimental/` Holds HTML/JS Reference Prototypes & Preserved Redo Records**:
  - Interactive prototypes under `Experimental/` are browser HTML/JS/CSS/WebGPU specifications; native C++20 production implementations belong exclusively in `Engine/` and `Exhibits/` (with `Experimental/RemovedSdfGi/` and `Experimental/SdfLightingDemoCpp/` preserving the SDF/GI redo baseline).
  - **`Experimental/ProjectZeroEditor/`** is the starting visual & structural reference for shared UI components (`Engine/Components/`). Note DM Sans metrics when rasterizing in ImGui: CSS `font-size` scales the `1000 em` box while ImGui scales `ascent - descent`, requiring a `1.302` multiplier (`hhea` ascent `992`, descent `-310`) so ImGui text matches browser pixel sizing.

---

## 🔴 7. WORKSPACE DOCUMENTATION & NAMING-AUDIT INVARIANT (2026-11-15)

- **Every Workspace Maintains a `README.md` (`Engine/Workspaces/*/README.md`, `Engine/Editor/README.md`, `Engine/Lifecycle/README.md`)**:
  - Each workspace README documents module architecture, every UI control, every keyboard/mouse shortcut, and a read-only C++ naming-compliance audit against [BannedWordsVocabulary.md](BannedWordsVocabulary.md), linked from [CLAUDE.md](../CLAUDE.md) and [Documentation/README.md](README.md).
- **Prohibited `<K-suffix>` Ban Enforcement**:
  - Never introduce the prohibited `K`-word suffix in new C++, `.slang`, or Markdown prose/tables; existing legacy occurrences in `Engine/Workspaces/` are catalogued in each workspace README (`BKL-011`) for dedicated refactoring.

---

## 🔴 8. MANDATORY REUSE-FIRST CHECK BEFORE CREATING OR REWRITING FILES (2026-11-15)

- **Inspect Existing Codebase First**: Before creating a new file, class, shader, solver, or UI component — or rewriting an existing one — search `Engine/`, `Exhibits/`, `Automation/`, [FolderStructure.md](FolderStructure.md), and the workspace `README.md` files to verify whether equivalent functionality already exists.
- **Reuse Over Recreation**: Wire into, extend, or refine existing modules (`Engine/Components/`, `Engine/GeometricRaster/`, `Engine/GeometrySpace/`, `Engine/SimulationSpace/`, `Engine/Workspaces/`, `Engine/Codecs/`, `Engine/Shaders/`) instead of inventing duplicate files or parallel implementations.
- **New Files Only for Uncovered Responsibilities**: Add a new source/header/shader file only after confirming no existing unit already owns that mechanism, and place it strictly according to [FolderStructure.md](FolderStructure.md).
