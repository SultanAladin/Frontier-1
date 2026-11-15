# C++ Project Browser, Auto-Generated Project Structure & Frontier Editor Integration Plan

> **TL;DR:** Double-clicking **`Frontier.exe`** (`FrontierEditor`) boots directly into the native **C++20 + Vulkan 1.2/1.3 Project Browser** (`Frontier::Editor::Explorer::ProjectBrowser`), built strictly out of existing **`PixelSpace`** and **`Engine/Components/Controls/`** primitives. Creating a new project **`<ProjectName>`** (`Frontier::Configuration::ProjectGenerator`) scaffolds a complete user project directory mirroring **`Frontier/`** (`Engine/` with `Engine/UserInterface/`, `Content/`, `Shaders/`, `Binaries/Win64/`, `SessionOutput/`), copies shared **`FrontierEngine.dll`** + **`FrontierWorkspaceUi.dll`** + **`CompiledShaders/`** directly into `<ProjectName>/Binaries/Win64/` (zero `SDK` directories), and generates three C++20 targets (**`<ProjectName>`**, **`<ProjectName>Editor`**, **`<ProjectName>Shipping`**) inside **`namespace <ProjectName>::*`**. Opening a project offers capability-filtered **`Open with Frontier`** (launching **`EditorLifecycle`** + **`WorkspaceDockContainer`** + **`ControlCentrePanel`** + **`OutlinerPanel`** + **`ViewportPanel`** + **`InspectorPanel`** + **`ContentBrowser`** wired live to **`TieredWorldRenderSequence`** + **`ShowcaseGalleryScene`** + **`TransformGizmo`**) and **`Open with UVWorkspace`** (**`UVWorkspaceHost`**).

---

## 1. 🏆 Key Architectural Decisions

*(Legend — Build Modularity / Windows UX / Maintainability: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest; Complexity: ✔️ = lowest/best)*

### 1.1 Engine `.dll` Packaging & Deployment Strategy for User Projects (Zero `SDK` Naming)

| Rank | `.dll` Packaging Option | Build Modularity | Windows UX | Complexity | Summary |
| :---: | :--- | :---: | :---: | :---: | :--- |
| 🏆 🥇 | **Option A — Two Shared Engine DLLs (`FrontierEngine.dll` + `FrontierWorkspaceUi.dll`) + Direct `Engine/` & `Binaries/Win64/` Layout** | 🔴 | 🔴 | ✔️ | `FrontierEngine.dll` exports portable runtime, codecs, math, geometry, and `TieredWorldRenderSequence` rendering (used by all 3 targets including `<ProjectName>Shipping.exe`); `FrontierWorkspaceUi.dll` exports `PixelSpace`, `WorkspaceDockContainer`, `EditorLifecycle`, `UVWorkspaceHost`, `ContentBrowser`, and `ProjectBrowser` (linked only by `Editor` & `Development`, excluded from `Shipping`). Public headers mirror `Engine/` directly and binaries deploy into `<ProjectName>/Binaries/Win64/` with zero `SDK` folders. |
| 🥈 | **Option B — Monolithic Single `Frontier.dll` Across All Profiles** | ✔️ | 🚩 | ✔️ | Mixes Editor UI symbols into the runtime binary or requires duplicate DLL builds per profile, violating the strict `Shipping` Editor-exclusion rule in `CLAUDE.md` §2.4. |
| 🥉 | **Option C — Granular Per-Domain DLLs (10+ `.dll` files)** | 🚩 | 🚩 | 🔴 | Heavy `__declspec(dllexport)` maintenance across every internal class with slower Windows loader startup. |

💡 **Decision:** **Option A (`FrontierEngine.dll` + `FrontierWorkspaceUi.dll` + `ProjectExecutionRunner`)** — gives every generated user project clean, self-contained Windows `.dll` execution while keeping `<ProjectName>Shipping.exe` 100% free of Editor/ImGui code and completely free of `SDK` naming.

### 1.2 Startup Transition Flow (`Frontier.exe` → `ProjectBrowser` → `EditorLifecycle` / `UVWorkspaceHost`)

| Rank | Launch Routing Architecture | Instant Handoff | Process Isolation | Complexity | Summary |
| :---: | :--- | :---: | :---: | :---: | :--- |
| 🏆 🥇 | **Option A — Unified Single-Window `Frontier.exe` Booting `ProjectBrowser`, Transitioning In-Place to `EditorLifecycle` or `UVWorkspaceHost`** | 🔴 | 🚩 | ✔️ | Double-clicking `Frontier.exe` opens `ProjectBrowser` immediately on the active Vulkan swapchain; choosing **Open with Frontier** mounts the existing `EditorLifecycle` (`Cornell-Studio Gallery` + `TieredWorldRenderSequence`) and **Open with UVWorkspace** mounts `UVWorkspaceHost` in the same window, with a top-bar return to `ProjectBrowser`. |
| 🥈 | **Option B — Separate Browser Process Spawning Child Processes** | 🚩 | 🔴 | 🚩 | Causes a visible window close/reopen flash and second Vulkan device initialization when opening a project. |

💡 **Decision:** **Option A** (also supporting `--world`, `--uv`, `--project=<Path>`, `--frames=N`, and `--capture-png=<Path>` CLI flags for deterministic CTest / PNG verification).

---

## 2. 🔍 End-to-End Flow & Existing Component Reuse Matrix

### 2.1 Existing C++ Components Reused 1:1 (Zero Invented UI / Zero Replacement Renderer)

| UI / Render Region (HTML Mirror) | Existing C++ Class Reused | Live Wiring & Parity Improvements |
| :--- | :--- | :--- |
| **Application Shell & Docking** | `Lifecycle::EditorLifecycle` + `Components::HeadlessWorkspaceDockContainer` | Fix CMake target definitions so `FrontierEditor` (`Frontier.exe`) always builds with Editor capability active (`FRONTIER_PROFILE_EDITOR=1` on the Editor executable/UI unit) and forwards `DesktopPlatformWindow` input cleanly into `EditorLifecycle`. Retains trapezoidal tabs (`Outliner × +`, `Viewport × +`, `Inspector × +`). |
| **Top Notch & Appearance** | `ControlCentrePanel` + `ThemeRepository` + `TypographyRepository` | Retains top `Frontier` notch shade (OLED / Dark / Light themes, UI scale, `.FontSpace` typography, layout reset) and adds clean project-switching back to `ProjectBrowser`. |
| **Left Scene Outliner (`17 entries`)** | `OutlinerPanel` + `GeometricRaster::ShowcaseGalleryScene` | Preserves the canonical 17-entry `Cornell-Studio Gallery` tree (`Showcase` [10 specimens], `Cameras` → `Editor Camera`, `World` → `Sun`, `Lighting` → `Ceiling Softbox`) matching `ShowcaseInitialRows` in `Experimental/FrontierWorldMirror/ShowcaseGalleryScene.js`, plus unified Construct templates (Box, Sphere, Cylinder, Cone, Torus, Area Quad Light, Scene Camera, and all 19 `ForceModel`s) without clobbering. |
| **Center 3D Viewport & World Renderer** | `ViewportPanel` + `GeometricRaster::TieredWorldRenderSequence` + `Editor::TransformGizmo` + `GizmoRasterization` + `VectorFieldViewportCoordinator` | Renders `Cornell-Studio Gallery` dynamically via `TieredWorldRenderSequence` (`Tier1GtxCalibrated` interactive / `Tier2RtxDeterministic` reference), wired to `ViewportPanel` Z-up (`+Z`) camera navigation (RMB+WASD, Orbit, Pan, Dolly, `F` Frame Selected), 3D ray picking to select `OutlinerPanel` slots, dynamic Construct / delete / transform scene rebuilds, and `TransformGizmo` (`W`/`E`/`R`) manipulation. |
| **Right Inspector** | `InspectorPanel` + `Editor::Simulation::ForceInspectorView` | Retains `InspectorPanel` (`Notes`, `Enabled` / `Cast shadows` / `Dynamic` / `Locked` capability tiles, `Transform` X/Y/Z, Folder Inspector) + `ForceInspectorView` override for ForceField entries, bidirectionally synced with `ShowcaseGalleryScene`. |
| **Bottom Drawer** | `Editor::Explorer::ContentBrowser` (`ContentBrowserPanel` + `ColourCodex` + `ThumbnailSheetRepository`) | Connects `ColourCodex` swatch selection/activation directly to the selected `ShowcaseGalleryScene` specimen material BaseColor and Simulation preset activation to `SpawnForceFieldEntry`. |
| **Startup Project Browser** | `Editor::Explorer::ProjectBrowser` (composed strictly from `PixelSpace`, `CapsuleButton`, `ExclusiveSelector`, `DropdownSelector`, `TextInputField`, `ConfirmationDialogue`, `WorkspaceAppearance`) | Matches `Experimental/ProjectBrowser/` 3-column layout (`204 px` left rail, fluid center tile grid / reader preview with `TieredWorldRenderSequence` preview raster, `302 px` right Details/Notes/Revisions inspector) + `+ New Project` dialogue + `Open with Frontier` / `Open with UVWorkspace`. |

---

## 3. 🧩 Auto-Generated `<ProjectName>` Folder Structure (1:1 Mirror of `Frontier/`)

When the user creates **`<ProjectName>`** (for example, **`VehicleStudio`** — sanitized to PascalCase C++ identifier), `Frontier::Configuration::ProjectGenerator` emits the exact Engine-mirrored directory tree below:

```text
<DestinationRoot>/VehicleStudio/
├── VehicleStudio.projectspace                      // Root project container (ProjectSpaceRepository + capabilities)
├── CMakeLists.txt                                  // Auto-generated C++20 project build (3 profile targets)
├── .gitignore                                      // Ignores SessionOutput/, Build/, and transient symbols
│
├── Engine/                                         // 🧩 USER C++20 SOURCE TREE (mirrors Frontier/Engine/)
│   ├── Configuration/
│   │   ├── VehicleStudioConfiguration.h            // namespace VehicleStudio::Configuration
│   │   └── VehicleStudioConfiguration.cpp
│   ├── Lifecycle/
│   │   ├── VehicleStudioRuntimeLifecycle.h         // namespace VehicleStudio::Lifecycle (Initialize/Tick/Finalize)
│   │   ├── VehicleStudioRuntimeLifecycle.cpp
│   │   ├── VehicleStudioLaunch.cpp                 // Entry point for target: VehicleStudio (Development)
│   │   ├── VehicleStudioEditorLaunch.cpp           // Entry point for target: VehicleStudioEditor (Editor)
│   │   └── VehicleStudioShippingLaunch.cpp         // Entry point for target: VehicleStudioShipping (Shipping)
│   ├── UserInterface/                              // 🧩 CUSTOM USER UI COMPONENTS (kept separate from Engine UI)
│   │   ├── VehicleStudioOverlayPanel.h             // namespace VehicleStudio::UserInterface
│   │   └── VehicleStudioOverlayPanel.cpp
│   ├── GeometrySpace/
│   │   └── VehicleStudioPrimitives.h               // namespace VehicleStudio::GeometrySpace
│   ├── MaterialSpace/
│   │   └── VehicleStudioMaterials.h                // namespace VehicleStudio::MaterialSpace
│   ├── SimulationSpace/
│   │   └── VehicleStudioWorldStep.h                // namespace VehicleStudio::SimulationSpace
│   └── Workspaces/
│       └── VehicleStudioWorkspaceExtension.h       // namespace VehicleStudio::Workspaces (#if FRONTIER_PROFILE_EDITOR)
│
├── Content/                                        // 🧩 USER CONTENT REPOSITORY (mirrors Frontier/EngineContent/)
│   ├── Geometry/
│   ├── Icons/
│   ├── FontsArchives/
│   ├── Materials/
│   ├── Worlds/
│   │   └── MainWorld.WorldSpace                    // Default world scene opened by Frontier
│   ├── UV/
│   │   └── MainSurface.UVSpace                     // Default UV session opened by UVWorkspace
│   └── Graphics/
│
├── Shaders/                                        // 🧩 USER .SLANG SHADERS (mirrors Frontier/Engine/Shaders/)
│   ├── Compute/
│   ├── DirectLighting/
│   ├── GlobalIllumination/
│   ├── MaterialSpace/
│   │   └── VehicleStudioSurface.slang
│   └── Visibility/
│
├── Binaries/                                       // 🧩 DEPLOYED ENGINE DLLS & OUTPUT EXECUTABLES
│   └── Win64/
│       ├── FrontierEngine.dll                      // Core Engine runtime DLL (all profiles)
│       ├── FrontierEngine.lib                      // Import library for MSVC / clang-cl linking
│       ├── FrontierWorkspaceUi.dll                 // Engine UI/Workspace DLL (Editor & Development)
│       ├── FrontierWorkspaceUi.lib                 // Import library for Editor & Development targets
│       ├── CompiledShaders/                        // Compiled SPIR-V (.spv) shaders
│       ├── VehicleStudio.exe                       // Built by target VehicleStudio
│       ├── VehicleStudioEditor.exe                 // Built by target VehicleStudioEditor
│       └── VehicleStudioShipping.exe               // Built by target VehicleStudioShipping
│
└── SessionOutput/                                  // Local diagnostic logs, captures, and generated thumbnails
    ├── Logs/
    └── Captures/
```

---

## 4. 🏁 Implementation & Verification Sequence

1. **Step 1 — Fix `FrontierEditor` Profile & Live World Editor Wiring (`CMakeLists.txt`, `ShowcaseGalleryScene.{h,cpp}`, `EditorLifecycle.{h,cpp}`, `DesktopWorkspaceLaunch.{h,cpp}`)**:
   - Ensure `FrontierEditor` (`Frontier.exe`) always compiles with full `EditorLifecycle` support regardless of default CMake profile, merge Construct templates cleanly (Box, Sphere, Cylinder, Cone, Torus, Area Light, Camera, 19 `ForceModel`s), wire live Z-up camera controls + 3D ray picking + dynamic scene rebuilds + `ColourCodex` material tinting + `TransformGizmo`, and verify 1:1 visual parity against `Experimental/FrontierWorldMirror`.
2. **Step 2 — `WorkspaceCapabilityRegister`, `ProjectGenerator`, & `ProjectExecutionRunner` (`Engine/Workspaces/`, `Engine/Configuration/`, `Engine/Lifecycle/`)**:
   - Implement capability-filtered `Open with Frontier` and `Open with UVWorkspace`, `<ProjectName>/` scaffolding with `SessionOutput/` and `.projectspace`, and ABI-clean `ProjectExecutionRunner` (zero `SDK` naming).
3. **Step 3 — `ProjectBrowser` (`Engine/Editor/Explorer/ProjectBrowser.{h,cpp}`)**:
   - Build the 3-column Project Browser strictly out of `PixelSpace` + `CapsuleButton` + `ExclusiveSelector` + `DropdownSelector` + `TextInputField` + `ConfirmationDialogue` + `TieredWorldRenderSequence` preview thumbnails, and wire seamless in-window transitions between `ProjectBrowser`, `EditorLifecycle` (`Frontier`), and `UVWorkspaceHost` (`UVWorkspace`).
4. **Step 4 — Local CTest + `.png` Visual Proofs & Windows x64 GitHub Actions Verification**:
   - Add `Exhibits/ProjectBrowserValidation.cpp` generating visual `.png` proofs under `Exhibits/Images/ProjectBrowser/` and `Exhibits/Images/GlobalIllumination/`, verify generated `<ProjectName>` 3-target builds, run CTest locally, and run Windows x64 CI packaging `Frontier-Windows-x64.zip`.
