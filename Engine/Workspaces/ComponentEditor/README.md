# 🧩 `Engine/Workspaces/ComponentEditor/` — Component Class & Assembly Workspace

> **TL;DR**: `ComponentEditor` is Frontier's Unreal-inspired actor/vehicle class assembly workspace (`ComponentClassDefinition`, `ComponentStorage`, trapezoidal multi-class tabs, `Components`/`Content` left dock, 7-channel Vulkan preview + `Assembly Sequence`, `Details` inspector, and collapsible `Compiled payload` / `Response curves` bottom drawer).

---

## 🧩 1. Module Architecture

| File | Role |
| :--- | :--- |
| `ComponentWorkspaceSession.{h,cpp}` | Multi-tab class documents, `ComponentCatalog`, compile state (`COMPILED`/`DIRTY`/`ERRORS`), simulation toggle, selection/duplication |
| `ComponentWorkspaceHost.{h,cpp}` | 3-dock host: Left (`Components`/`Content` tabs), Center (trapezoidal `WorkspaceTabStrip`, `ComponentViewportHost`, collapsible `ComponentDrawerPanel`), Right (`ComponentDetailsPanel`) |
| `ComponentTreePanel.{h,cpp}` | `Components` / `Triangles` metric tiles, `+ Add` component button, `ROOT` hierarchical component tree |
| `ComponentContentTray.{h,cpp}` | Component catalog browser, `+ Add`, `New Class`, `Compile` controls |
| `ComponentViewportHost.{h,cpp}` | `Viewport` / `Assembly Sequence` switcher, 7 inspection channels, `Simulate`/`Stop`, `Focus`, `Grid`, Vulkan `RenderClassFrame` + pixel pick |
| `ComponentDetailsPanel.{h,cpp}` | `CLASS DEFAULTS`, `COMPILER RESULTS`, `TRANSFORM`, `CONSTRAINT`, `LIGHT`, `VEHICLE DYNAMICS`, `RENDERING` |
| `ComponentDrawerPanel.{h,cpp}` | Collapsible bottom drawer (`Compiled payload` & `Response curves` with `Linear`/`Smooth`/`Ease`/`Step` interpolation) |

---

## 🏷️ 2. UI Controls

| Region | Controls | Function |
| :--- | :--- | :--- |
| **Left Dock (`Components` / `Content`)** | `Components` & `Triangles` tiles · `+ Add` · `ROOT` hierarchy · `New Class` · `Compile` | Browse/add mesh, light, physics constraint (`Slider`, etc.), or vehicle components; create & compile classes |
| **Center Top Strip** | Trapezoidal class tabs · `COMPILED`/`DIRTY`/`ERRORS` chip · `Viewport | Assembly Sequence` · `Compile` | Switch class documents, inspect assembly sequence, trigger compile |
| **Center Toolbar** | `Lit` · `Unlit` · `Density` · `Normals` · `UV checker` · `LOD clusters` · `Collision` · `Simulate`/`Stop` · `Focus` · `Grid` | Inspect rendering/collision channels, run live class simulation (`Throttle`/`Brake` chips when vehicle active), frame class, toggle floor grid |
| **Right `Details` Dock** | `CLASS DEFAULTS`, `COMPILER RESULTS`, `TRANSFORM`, `CONSTRAINT`, `LIGHT`, `VEHICLE DYNAMICS`, `RENDERING` | Edit component properties and inspect compile diagnostics |
| **Bottom Drawer** | `v` collapse · `Compiled payload` · `Response curves` (`Linear`/`Smooth`/`Ease`/`Step`) | Inspect compiled class memory/slot payload and edit response curves |

---

## 📝 3. Hotkeys & Mouse Controls (`ComponentWorkspaceHost` & `ComponentViewportHost`)

| Input | Action |
| :--- | :--- |
| `Ctrl+D` | Duplicate selected component (`Session.DuplicateSelectedComponent()`) |
| `Delete` | Delete selected component |
| `LMB` Click | GPU pixel pick component in Vulkan viewport (`QueryPickAtPixel`) |
| `LMB` Double-Click | Pick component and frame in camera (`FrameSelectedOrClass`) |
| `LMB` Drag | Orbit 3D viewport camera (`!AltHeld`) |
| `RMB` Drag or `MMB` Drag | Pan 3D camera |
| `Mouse Wheel` | Zoom / dolly camera (`1.2 m – 45.0 m`) |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `14` files under `Engine/Workspaces/ComponentEditor/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **Prohibited `<K-suffix>`** | 41 | `ComponentContentTray.cpp`, `ComponentDetailsPanel.cpp`, `ComponentTreePanel.cpp` (`PhysicsConstraint<K-suffix>`, `LightEmitter<K-suffix>`, `Con<K-suffix>`), `ComponentDrawerPanel.cpp` (`CurveInterpolation<K-suffix>`) | `*Category` / `*Variant` / `CurveInterpolationForm` |
| **`Catalog`** | 28 | `ComponentCatalog` references across `ComponentWorkspaceSession.*`, `ComponentContentTray.cpp` | `ComponentRepository` / `ComponentRegistry` |
| **`Draw*` / `Is*/Has*` / `Get*`/`Set*` / `Data` / `Frame`** | 52 | `Draw*` (`13`), `Is/Has` (`11`), `Get/Set` (`10`), `Data` (`10`), `Frame` (`8`) | `Construct*`, direct booleans, `Query*`/`Assign*`, `Payload`, `Extent`/`Step` |
| **`Flag` / `Base` / `State` / `Node` / `Emitter`** | 20 | `Flag` (`6`), `Base` (`5`), `State` (`4`), `Node` (`4`), `Emitter` (`1`) | `DirectiveMask`, `Primary`, concrete status enum, `Slot`, `LightSource` |
