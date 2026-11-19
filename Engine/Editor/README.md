# 🧩 `Engine/Editor/` — Editor Explorer & ForceField Simulation Workspace

> **TL;DR**: `Engine/Editor/` (compiled **only** under `FRONTIER_PROFILE_EDITOR=1`; excluded from `Development` and `Shipping`) houses **`Explorer/`** (`ProjectBrowser` and `ContentBrowser`) and **`Workspaces/Simulation/`** (`ForceFieldWorkspace` / `FrontierForceField` with 4-tab `ForceInspectorView`, `ForcePresetBrowserPanel`, and `VectorFieldViewportSession`).

---

## 🧩 1. Module Architecture

| Directory | Files | Responsibilities |
| :--- | :--- | :--- |
| **`Explorer/`** | `ProjectBrowser.{h,cpp}`, `ContentBrowser.{h,cpp}` | Launcher/project hub (`ProjectBrowser`: theme toggle, search, sort, revision/zoom/fit bar, project thumbnails) and editor wrapper around `ContentBrowserPanel` |
| **`Workspaces/Simulation/`** | `VectorFieldViewportSession.{h,cpp}`, `ForceInspectorView.{h,cpp}`, `ForcePresetBrowserPanel.{h,cpp}` | `ForceFieldWorkspace`: global wind + 16 bounded spatial force models, GPU vector/streamline overlays (`OverlaySequence`), live anemometer probe telemetry, 4-tab force inspector, pull-up preset/formula browser |

---

## 🏷️ 2. UI Controls

### 2.1 `Explorer/` (`ProjectBrowser` & `ContentBrowser`)

| Module | Controls | Function |
| :--- | :--- | :--- |
| **`ProjectBrowser`** | Theme segmented toggle · `TextInputField` search · `DropdownSelector` sort · View / Revision / Zoom / Fit bar · Project tiles | Create, open, filter, sort, and inspect `.ProjectSpace` projects |
| **`ContentBrowser` / `ContentBrowserPanel`** | Origin Scope tabs (`All` / `Project` / `Imported` / `Engine`) · Category rail · Search · Layout switcher · Thumbnail size slider · Colour filters · Drag-and-drop zones | Browse `.Space` assets, materials, force presets & formulas |

### 2.2 `ForceFieldWorkspace` (`VectorFieldViewportSession`, `ForceInspectorView`, `ForcePresetBrowserPanel`, `ViewportPanel`)

| Panel | Controls & Tabs | Function |
| :--- | :--- | :--- |
| **`ViewportPanel`** *(Force Overlay active)* | `ExecutionModeStrip` (`Edit`/`Simulate`/`Play` + `Rewind`) · Projection dropdown · Force Overlay settings sheet (`Thickness`, `Emissive`, `Opacity` sliders) · `Construct` sheet (`Ctrl+A`, category tabs: `All`, `Presets`, `VectorFieldModels`, `StreamTransportModels`, `TurbulenceModels`, `SceneFolders`) · `Diagnostics (F3)` | Control simulation playback, streamline ribbon appearance, force placement, and real-time diagnostics |
| **`VectorFieldViewportSession`** | Dual/Single viewport panes · Live anemometer probe HUD (`Probe (x,y,z) m · m/s · Bft · |curl| · div · q Pa`) · Force circle & motion-waypoint picking | Pick force sources/waypoints, drag anemometer probe, orbit & zoom |
| **`ForceInspectorView` (4 Tabs)** | **`Field`** · **`Formula`** (`VectorFieldLang`) · **`Layers`** · **`Cache`** | Edit spatial force model parameters, custom math formulas, layer blends, and lattice cache |
| **`ForcePresetBrowserPanel`** | Pull-up browser tabs (`All Presets & Laws`, `Force Presets` [`.ForceModel`], `Formulas` [`VectorFieldLang`]) · Right detail pane (`ASSET DETAILS`, `+ Construct in Scene`) | Browse and instantiate force presets & `VectorFieldLang` formulas |

---

## 📝 3. Hotkeys & Mouse Controls

| Context | Input | Action |
| :--- | :--- | :--- |
| **`ProjectBrowser`** | `Esc` / `Enter` | Dismiss modal / Open selected project |
| **`ContentBrowserPanel`** | `Ctrl+Space` / `Ctrl+F` / `Esc` | Toggle drawer / Focus search / Clear search or close |
| **`ContentBrowserPanel`** | `Left` / `Right` / `Home` / `End` / `Enter` / `Del` / `Backspace` | Navigate & open/delete assets |
| **`ViewportPanel`** | `Ctrl+A` / `F3` / `Esc` | Toggle `Construct` sheet / Toggle `Diagnostics` / Close overlays |
| **`VectorFieldViewportSession`** | `LMB` Click | Pick force source circle or motion waypoint |
| **`VectorFieldViewportSession`** | `LMB` Drag | Orbit 3D vector-field camera (`YawRadians` / `PitchRadians`) |
| **`VectorFieldViewportSession`** | `Shift+LMB` Drag | Reposition live anemometer probe (`AnemometerWorldMeters` X/Y) |
| **`VectorFieldViewportSession`** | `Mouse Wheel` | Zoom perspective distance (`12–180 m`) & ortho half-height (`8–120 m`) |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `10` files under `Engine/Editor/` (`6` in `Workspaces/Simulation/`, `4` in `Explorer/`). **Zero C++ edits were made.**

| Directory | Prohibited `<K-suffix>` | Offending Counts & Examples | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **`Workspaces/Simulation/`** | 0 | `Record` (`25`, `RecordEntry` composites), `State` (`5`), `Get/Set` (`4`), `Is/Has` (`3`), `Map` (`3`), `Data` (`2`), `Mesh` (`2`), `Base`/`Context`/`Frame` (`1` each) — plus `ForceInspectorView.h:95` review & `"All Presets & Laws"` UI label (`Law` is banned per `BannedWordsVocabulary.md` §3.1!) | Replace `"Laws"` UI copy with `"Formulas"` / `"Models"`; replace `State`/`Get`/`Set`/`Data`/`Mesh` |
| **`Explorer/`** | 0 | `Get/Set` (`17`), `Palette` (`15`), `Data` (`13`), `Item` (`6`), `Frame` (`6`), `Context` (`1`) | `Query*`/`Assign*`, `ColorCodex`, `Payload`, `Entry`, `Extent`, `Session` |
