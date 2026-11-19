# 🧩 `Engine/Lifecycle/` — Application Lifecycles, Launchers & World Editor

> **TL;DR**: `Engine/Lifecycle/` implements the profile-gated lifecycle hierarchy (`RuntimeLifecycle` → `WorkspaceLifecycle` → `EditorLifecycle`), the 12 desktop executables (`Frontier*Launch.cpp`), `DesktopWorkspaceLaunch` (shared workspace switcher bar & drag-drop routing), and the **Frontier World Editor** (`EditorLifecycle`: Cornell-Studio Gallery, Tier 1 GTX / Tier 2 RTX GI, GPU `TransformGizmo`, `OutlinerPanel`, `ViewportPanel`, `InspectorPanel`, `ControlCentrePanel`, and `ContentBrowser`).

---

## 🧩 1. Module Architecture & Desktop Executables

| File | Profile Scope | Role |
| :--- | :--- | :--- |
| `LifecycleSequence.h` | All profiles | Shared lifecycle tick & phase state definitions |
| `RuntimeLifecycle.{h,cpp}` | `Development`, `Shipping`, `Editor` | Base game/project runtime execution |
| `WorkspaceLifecycle.{h,cpp}` | `Editor`, `Development` | Multi-document workspace session & `WorkspaceThread` wiring |
| `EditorLifecycle.{h,cpp}` | `Editor` only | **Frontier World Editor**: `TieredWorldRenderSequence` + `ShowcaseGalleryScene`, GPU `TransformGizmo`, `Outliner`/`Viewport`/`Inspector`/`ControlCentre`/`ContentBrowser` + workspace hosting |
| `DesktopWorkspaceLaunch.{h,cpp}` | `Editor` / Standalone | Shared GLFW/Vulkan/ImGui desktop window host, top workspace switcher pills (`Bake`, `Pattern`, `Stamp`, `UV`, `Fracture`, `Components`, `Projects`), `Shift` drop target |
| `ProjectExecutionRunner.{h,cpp}` | All profiles | Project launch & execution loop |
| `Frontier{Host,Editor,UV,Baking,Pattern,Stamp,Material,Particle,Fracture,ForceField,Fluid,Sketcher}Launch.cpp` | Desktop apps | 12 standalone/host CMake executable entry points |

---

## 🏷️ 2. Frontier World Editor UI Controls (`EditorLifecycle` + `Engine/Components/`)

| Region / Panel | Controls & Tabs | Function |
| :--- | :--- | :--- |
| **Top Launcher Strip (`DesktopWorkspaceLaunch`)** | Workspace pills: `Bake` · `Pattern` · `Stamp` · `UV` · `Fracture` · `Components` · `Projects` | Switch between standalone/hosted workspaces or open `ProjectBrowser` (`Shift` drop routes file to target) |
| **`ControlCentrePanel` (`Ctrl+,`)** | Pages: `Appearance` (`Typography` / `Theme`), `Input` (Shortcut profile, Custom Shortcuts, mouse sensitivity, invert pitch), `Notifications`, `Project` | Configure `.ProjectSpace` preferences, fonts, themes & custom shortcut chords |
| **`OutlinerPanel`** | Scene tree · `+` generic construct dropdown · Settings (`Import...`) · `Visible`/`Hidden` counters · `Search` + `Filter` chips · Showcase folders · 5-slot footer | Organize scene hierarchy, drag-reorder, `Shift` multi-select, right-click directive sheet |
| **`ViewportPanel`** | Scene badge + `PREVIEW` tag · `ExecutionModeStrip` (`Edit`/`Simulate`/`Play` + `Rewind`) · `Construct` sheet · `Split Viewport` · `Projection` · `Header Debug` · `Diagnostics (F3)` · `Viewport Settings` | Toggle execution phase, spawn scene constructs, split panes, inspect telemetry |
| **`InspectorPanel`** | Entity properties, Showcase Folder Inspector, expandable `+ Add notes` / `Notes` | Inspect and edit selected scene entry transform, light, fog, wind, and material properties |
| **`ContentBrowserPanel`** | `All` / `Project` / `Imported` / `Engine` tabs · Search · Layout switcher · Colour filters | Browse & drag-and-drop project assets into World Viewport |

---

## 📝 3. Hotkeys & Mouse Controls (`EditorLifecycle` & `InputSettings`)

### 3.1 World Viewport Navigation & Gizmos (`EditorLifecycle`)

| Input | Action | Notes |
| :--- | :--- | :--- |
| `RMB` Drag + `W`/`A`/`S`/`D`/`Q`/`E` | Fly camera (`Q` down, `E` up; `Shift` boosts fly speed) | Active while holding `RMB` in viewport |
| `Alt+LMB` Drag | Orbit camera around `TargetPoint` | — |
| `MMB` Drag or `Shift+RMB` Drag | Pan camera | — |
| `Mouse Wheel` | Dolly camera toward/away from target | — |
| `W` / `E` / `R` / `U` | `TransformGizmo`: `Translate` / `Rotate` / `Scale` / `Universal` | When not flying `RMB` or typing |
| Hold `Ctrl` (during gizmo drag) | Snap transform to `GizmoSnapSettings` grid | — |
| `F` | Frame selected object in camera (`FrameShowcaseCameraSelection`) | — |
| `LMB` Click | Pick `TransformGizmo` handle or 3D scene object | — |

### 3.2 Global Panel & Editor Shortcuts (`InputSettings` & `Engine/Components/Panes/`)

| Default Chord | Command / Action | Configurable? |
| :--- | :--- | :--- |
| `Ctrl+,` | Toggle `Control Centre` tray | ✔️ (`ControlCentrePanel` -> `Input`) |
| `Ctrl+Alt+F` | Open `Appearance -> Typography` tab | ✔️ |
| `Ctrl+Alt+T` | Open `Appearance -> Theme` tab | ✔️ |
| `Esc` | `Dismiss Panel` / Close active sheets & dropdowns | ✔️ |
| `Ctrl+A` | Toggle `Construct` sheet in `ViewportPanel` | Fixed pane shortcut |
| `Ctrl+G` | Group selected entries into folder in `OutlinerPanel` | Fixed pane shortcut |
| `Ctrl+F` | Focus search in `OutlinerPanel` / `ContentBrowserPanel` | Fixed pane shortcut |
| `F3` | Toggle `Diagnostics` sparkline overlay in `ViewportPanel` | Fixed pane shortcut |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `23` files under `Engine/Lifecycle/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **Prohibited `<K-suffix>`** | **0** | None detected in `Engine/Lifecycle/` | 🟢 |
| **`Frame` / `Record` / `Context`** | 164 | `Frame` (`76`), `Record` (`66`, mostly `RecordEntry`), `Context` (`22`) | `Step`/`Extent`, `RecordEntry`, `Session` |
| **`Is*/Has*` / `Get*`/`Set*` / `Pill` / `Item` / `Map`** | 71 | `Is/Has` (`21`), `Get/Set` (`14`), `Pill` (`14`), `Item` (`11`), `Map` (`11`) | Direct booleans, `Query*`/`Assign*`, `CapsuleButton`, `Entry`, `Register` |
| **`Base` / `Info` / `Flag` / `Catalog` / `Mesh` / `Draw*` / `Emitter` / `Data`** | 35 | `Base` (`9`), `Info` (`7`), `Flag` (`5`), `Catalog` (`5`), `Mesh` (`3`), `Draw*` (`2`), `Emitter` (`2`), `Data` (`1`) | `Primary`, `Metrics`, `DirectiveMask`, `Repository`, `PolygonNetwork`, `Construct*`, `Source`, `Payload` |
