# 🧩 `Engine/Workspaces/Fracture/` — Fracture & Shatter Workspace (`FrontierFracture`)

> **TL;DR**: `FractureWorkspace` provides interactive brittle/ductile partitioning across 6 fracture solvers (`Clustered Voronoi`, `Uniform Voronoi`, `Radial`, `Planar slices`, `Brick`, `Recursive split`), 2-layer exterior/interior materials, energy/stress fractography views, and `JoltPhysics v5.6.0` rigid-body impact playback.

---

## 🧩 1. Module Architecture

| File | Role |
| :--- | :--- |
| `FractureWorkspaceSession.{h,cpp}` | Document state, 6-solver partitioning, primitive shapes, material repository, impactor setup, `JoltPhysics` sim runner, `DiagnosticsOverlay` telemetry |
| `FractureWorkspaceHost.{h,cpp}` | Top-level workspace dock host combining Outliner, Viewport & Inspector |
| `FractureOutlinerPanel.{h,cpp}` | `Released` / `Held` census filters, `Ctrl+F` search, fragment tree & collapse-sliders control |
| `FractureViewportHost.{h,cpp}` | `ExecutionModeStrip` (`Edit`/`Sim`/`Play`), `Simulate`/`Play`, `Frame`, `Impact`, `Shading`/`Debug` dropdowns, `Explode` slider, `Diag`/`Wire`/`Seeds`/`Grid`, Energy LOG legend, `F3` diagnostics |
| `FractureViewportRenderer.{h,cpp}` | CPU/GPU viewport rasterizer, energy color ramp & surface ray-pick (`PickViewportSurface`) |
| `FractureInspectorPanel.{h,cpp}` | Context-sensitive property cards (`FRACTURE METHOD`, `SHAPE`, `MATERIAL`, `IMPACTOR`, `PATTERN`, `QUALITY`, `COLLISION`, plus fragment `RELEASE`/`GEOMETRY`) |
| `FractureIcons.h` | Custom vector icons for solver cards, material spheres, and checkboxes |

---

## 🏷️ 2. UI Controls

### 2.1 `FractureViewportHost` Toolbar & Overlays

| Control | Options / Range | Function |
| :--- | :--- | :--- |
| **`ExecutionModeStrip`** | `Edit` · `Sim` · `Play` | Switch authoring, live physics simulation, or playback |
| **Primary Action Pill** | `Simulate [Shift+2]` / `Play [Shift+3]` | Run fracture simulation or play baked/live timeline |
| **`Frame` · `Impact`** | Buttons | Frame specimen in camera; arm click-on-surface impactor tool |
| **`Shading` Dropdown** | `Lit` / `Fragments` / `Energy` / `Stress` (4 modes) | Switch viewport visualization; `Energy` reveals bottom-left `LOG` energy share ramp |
| **`Explode` Slider** | `0%` – `100%` | Non-destructive exploded preview of Voronoi/BSP fragments |
| **`Debug` Dropdown** | `Collision shapes` (`Exact`/`Convex`), `Mesh wireframe`, `Breakable bonds`, `Wavefronts & seeds`, `Studio ground grid`, `Diagnostics (F3)` | Toggle individual viewport debug overlays |
| **Quick Toggles** | `Diag [F3]` · `Wire` · `Seeds` · `Grid` | One-click debug pills on toolbar right |
| **Footer Timeline** | Rewind / Play / Scrub (`Frame` / `Bake` / `Fracture` / `Kinetic` channels) | Scrub and inspect per-frame fracture & kinetic energy telemetry |

### 2.2 `FractureOutlinerPanel` & `FractureInspectorPanel`

| Panel | Section / Control | Function |
| :--- | :--- | :--- |
| **`FractureOutlinerPanel`** | `Released` / `Held` census tiles · `Search [Ctrl+F]` · Fragment list | Filter & select individual fragments; toggle section collapse |
| **`FractureInspectorPanel`** *(Fragment)* | `RELEASE` (`Per-fragment override`) · `GEOMETRY` (`Closed convex cell`) | Override per-fragment release state & inspect cell metrics |
| **`FractureInspectorPanel`** *(Global)* | `FRACTURE METHOD` (6 solver cards) | Choose `Clustered Voronoi`, `Uniform Voronoi`, `Radial`, `Planar slices`, `Brick`, or `Recursive split` |
| **`FractureInspectorPanel`** *(Global)* | `SHAPE` · `MATERIAL` (Outside & Interior pickers) | Select specimen shape & outside/inside fracture materials |
| **`FractureInspectorPanel`** *(Global)* | `IMPACTOR` · `PATTERN` · `QUALITY` · `COLLISION` | Configure impact energy/size/direction, pattern density, cell quality, and `Exact`/`Convex` collision shapes |

---

## 📝 3. Hotkeys & Mouse Controls (`FractureViewportHost` & `FractureOutlinerPanel`)

| Input | Action | Notes |
| :--- | :--- | :--- |
| `Shift+2` | `Simulate` (enter simulation phase) | Shown on toolbar action pill |
| `Shift+3` | `Play` (play/pause fracture playback) | Shown on toolbar action pill |
| `Space` | Play / Pause timeline | Shown in footer prompt |
| `F3` | Toggle floating `Diagnostics` overlay (`96 samples`) | Toggleable via `Diag` pill or `Debug` menu |
| `Ctrl+F` | Focus Outliner `Search` field | `FractureOutlinerPanel` |
| `LMB` Click | Select fragment (or trigger viewport impact when `Impact` tool is armed) | Ray-picks fractured surface |
| `LMB` Drag | Orbit 3D studio camera | — |
| `RMB` Drag | Pan camera | Viewport hint bar |
| `Mouse Wheel` | Zoom camera (`1.3 m – 8.5 m`) | — |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `13` files under `Engine/Workspaces/Fracture/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **Prohibited `<K-suffix>`** | 11 | `FractureInspectorPanel.cpp` (`Glyph<K-suffix>`), `FractureOutlinerPanel.cpp` (`Icon<K-suffix>`), `FractureViewportHost.cpp` (`Glyph<K-suffix>`) | `IconCategory` / `IconVariant` |
| **`Draw*` / `Glyph`** | 262 | `DrawSectionHeader`, `DrawCensusCard`, `DrawMaterialSphereIcon`, `DrawCheckGlyph` across `FractureIcons.h` & panels | `Construct*`, `Icon` / `Symbol` |
| **`Card` / `Badge` / `Pill` / `Item` / `Menu`** | 250 | `Card` (`120`), `Item` (`53`), `Badge` (`34`), `Pill` (`31`), `Menu` (`12`) | `SectionBlock`, `Entry`, `Indicator`, `CapsuleButton`, `DirectiveSheet` |
| **`Is*/Has*` / `Flag` / `Frame` / `Base` / `Data` / `Map`** | 191 | `Is/Has` (`61`), `Frame` (`40`), `Flag` (`33`), `Base` (`25`), `Data` (`17`), `Map` (`15`) | `Query*`/direct booleans, `Extent`/`Step`, `DirectiveMask`, `Primary`, `Payload`, `Register` |
| **`Mesh` / `Make*` / `State` / `Get*`/`Set*` / `Value`** | 25 | `Mesh` (`9`), `Make*` (`7`), `State` (`4`), `Get/Set` (`3`), `Value` (`2`) | `PolygonNetwork`, `Construct*`, concrete mode enum, `Query*`/`Assign*`, `ScalarReading` |
