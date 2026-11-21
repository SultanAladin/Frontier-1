# 🧩 `Engine/Workspaces/UV/` — UV Authoring Workspace (`FrontierUV`)

> **TL;DR**: `UVWorkspace` provides synchronized dual 2D UDIM & 3D Surface GPU viewports (`VulkanInterchange` + `.slang`), 12 interactive selection/editing tools, pin-aware LSCM/ABF++ unwrapping, GPU stretch relaxation, concave skyline packing, `.obj`/`.gltf`/`.UVSpace` interchange, and `215 MiB` `RevisionTracker` undo/redo.

---

## 🧩 1. Module Architecture

| Layer | Files | Responsibilities |
| :--- | :--- | :--- |
| **Session & Host** | `UVWorkspaceSession.{h,cpp}`, `UVWorkspaceHost.{h,cpp}`, `UVPaneAppearance.h` | Multi-document session state, `.UVSpace` + OBJ/glTF import/export, OS file drop, docked pane layout |
| **Panes & Interactor** | `UVOutlinerPanel.{h,cpp}`, `UVViewportHost.{h,cpp}`, `UVViewportInteractor.{h,cpp}`, `UVInspectorPanel.{h,cpp}` | `UV Space` outliner, dual/single 2D+3D canvas + 12-slot rail + `Sample Assets` drawer, 5-tab inspector |
| **Operations** | `Operations/UVAuthoringOperations.{h,cpp}` | Mark/clear/weld/brush seams, pin locking, stitch (`V`), island split (`Y`), mirror/align, quad rectification |
| **Solvers** | `Solvers/{ConformalUnwrapSolver,SeamCutGenerator,UVProjectionSolvers,ProgressiveStretchRelaxer,ConcaveSkylinePacker,UVIslandLayoutOperations,TexelDensityEqualizer}.{h,cpp}` | Pin-aware LSCM, ABF++, Smart UV Project, Box/Cylindrical/Spherical/View/Follow-Quads, GPU stretch relax (`.slang`), concave packing, stack/unstack/pocket fill, texel density |

---

## 🏷️ 2. UI Controls

### 2.1 Topbar, Viewport & Floating Shelves (`UVViewportHost`)

| Region | Control | Function |
| :--- | :--- | :--- |
| **Topbar Left** | `Surface` / `Split` / `UV` layout pills | Switch 3D-only, Dual 2D+3D split, or 2D UV-only viewport |
| **Topbar Dropdowns** | `2D UV Debug` · `3D View Projection` | Toggle UV inspection overlays (checker, stretch, distortion, UDIM grid) and 3D projection mode |
| **Topbar Toggles** | `Wire` · `Seams` · `More` | Toggle wireframe, seam overlay, and expandable `More` shelf |
| **`More` Shelf** | `Checker` · `Rail: Left/Btm` · `Relax GPU` · `Avg Scale` · `Smart Seams` · `Stack` · `Unstack` · `Undo` · `Redo` | Toggle UV checker backdrop, dock tool rail Left/Bottom, run GPU relax, average island scale, auto-seams, stack/unstack similar islands, history |
| **12-Slot Tool Rail** | `Select` · `Box` · `Circle` · `Lasso` · `Move` · `Rotate` · `Scale` · `Split` · `Seam Brush` · `Stitch` · `Pin` · `Relax` | Active 2D/3D authoring mode (dockable Left or Bottom) |
| **Bottom Drawer** | `Sample Assets` | Load studio presets (`ControlledIntegrator`, `AeroCoupeGT`, `TacticalCrate`, `ArmoredBust`, `UDIMArchKit`) |
| **Directive Sheet** | Right-click context sheet | Quick access to selection, unwrap, pin, stitch, split, and island actions |

### 2.2 Outliner (`UVOutlinerPanel`) & 5-Tab Inspector (`UVInspectorPanel`)

| Panel | Tabs / Sections | Controls |
| :--- | :--- | :--- |
| **`UVOutlinerPanel`** | `UV Space` scene tree | Mesh part & UV layer list, create/rename/duplicate/delete UV layers, active layer selection |
| **`UVInspectorPanel`** | **`Pack`** | Margin, rotation step, concave skyline pack, stack similar, unstack overlaps, fill empty pockets, texel density equalize |
| **`UVInspectorPanel`** | **`Unwrap`** | LSCM conformal unwrap, ABF++, Smart UV Project (angle/margin), Box/Cylindrical/Spherical/Camera/Follow-Quads projections, live pin-aware unwrap |
| **`UVInspectorPanel`** | **`UDIM`** | UDIM tile grid management, per-UDIM resolution, procedural backdrops (`ColorGrid`, `TrimSheetAtlas`) |
| **`UVInspectorPanel`** | **`Shading`** | 16 procedural MatCap orbs (`MatCapStudioAtlas`), lighting & surface preview toggles |
| **`UVInspectorPanel`** | **`Check`** | Area/angle distortion metrics, overlap/flipped-UV detection, GPU `UVDistortionInspection.slang` diagnostics |

---

## 📝 3. Hotkeys & Mouse Controls (`UVViewportInteractor` + `UVViewportHost`)

### 3.1 Tool & Selection Hotkeys

| Shortcut | Action | Scope |
| :--- | :--- | :--- |
| `1` – `5` | Switch component pick mode (Vertex / Edge / Face / Island / UV) | Viewport |
| `7` | Snap 3D camera to Top view | 3D Viewport |
| `B` / `C` / `Q` | Activate `Box` / `Circle` / `Lasso` selection tool | Tool Rail |
| `G` / `R` / `S` | Activate / enter modal `Move` (`Grab`) / `Rotate` / `Scale` (with `U`/`V` axis lock & numeric entry) | 2D & 3D |
| `Y` | Split selected UV faces/islands (`Split`) | 2D / 3D |
| `K` | Activate `Seam Brush` tool | Tool Rail |
| `V` | Stitch selected boundary edges (`Stitch`) | 2D UV |
| `P` / `Alt+P` | Pin selected UVs / Unpin selected UVs | 2D UV |
| `U` / `Ctrl+U` | Open Unwrap directive sheet / Immediate LSCM Conformal Unwrap | 2D / 3D |
| `L` / `Ctrl+L` | Select linked island under cursor / Expand selection to linked island | 2D & 3D |
| `Ctrl+A` / `Ctrl+I` | Select All / Invert Selection | 2D & 3D |
| `F` | Frame / fit selection or active UDIM tile in view | 2D & 3D |
| `Ctrl+Z` | Undo (`RevisionTracker`) | Global |
| `Ctrl+Shift+Z` / `Ctrl+Y` | Redo (`RevisionTracker`) | Global |
| `Esc` / `Enter` | Cancel modal transform or clear selection / Commit modal transform | Viewport |

### 3.2 Mouse Navigation

| Input | 3D Viewport | 2D UV Viewport |
| :--- | :--- | :--- |
| `LMB` Click / Drag | GPU `R32G32B32A32_UINT` pick / `TransformGizmo` / Box-Circle-Lasso-Seam | Pick UV / `TransformGizmo` / Box-Circle-Lasso |
| `MMB` Drag / `Alt+LMB` | Orbit camera (`Shift+MMB` pan) | Pan 2D UV canvas |
| `Mouse Wheel` | Cursor-centered zoom / dolly | Cursor-centered zoom |
| `RMB` | Open directive sheet (or cancel active modal transform) | Open directive sheet (or cancel active modal) |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `29` files under `Engine/Workspaces/UV/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **Prohibited `<K-suffix>`** | 17 | `UVViewportHost.{h,cpp}` (`ActiveDirectiveSheet<K-suffix>`, `QueryDirectiveSheet<K-suffix>`), `UVViewportInteractor.cpp` (`Shape<K-suffix>`) | `*Category` / `*Mode` / `*Form` |
| **`Frame`** | 160 | Frame timing/bounds parameters across `UVViewportHost.cpp`, `UVViewportInteractor.cpp` | `Step`, `Extent`, `RenderTarget` |
| **`Record`** | 129 | Bare/composite `Record` identifiers in `UVWorkspaceSession.*`, `UVOutlinerPanel.*` | `RecordEntry` / `RecordToken` / `Register` |
| **`Value` / `Popup` / `Card` / `Pill` / `Badge`** | 235 | `Popup` (`54`), `Card` (`50`), `Pill` (`48`), `Badge` (`10`), `Value` (`73`) in `UVInspectorPanel.cpp`, `UVViewportHost.cpp` | `Sheet`, `SectionBlock`, `CapsuleButton`, `Indicator`, `ScalarReading` |
| **`Palette` / `Base` / `Menu` / `Map` / `Data` / `Context`** | 115 | `Palette` (`44`), `Base` (`43`), `Data` (`11`), `Context` (`8`), `Menu` (`8`), `Map` (`6`) | `ColorCodex`, `Primary`, `Payload`, `Session`, `DirectiveSheet`, `Table`→`Register` |
| **`Is*/Has*` & `Get*/Set*` & `Draw*`** | 73 | `Is*/Has*` (`36`), `Get*/Set*` (`33`), `Draw*` (`4`) | `Query*`, `Assign*`, `Configure*`, `Construct*` |
