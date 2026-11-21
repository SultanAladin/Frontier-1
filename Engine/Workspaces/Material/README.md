# 🧩 `Engine/Workspaces/Material/` — Dynamic OpenPBR Material Studio (`FrontierMaterial`)

> **TL;DR**: `MaterialWorkspace` authors time-varying, multi-layer OpenPBR materials (`DynamicMaterialRecipe` + `DynamicMaterialCompiler`) rendered via Vulkan 1.3 compute (`MaterialPreviewRenderer`) with live Content Browser thumbnail synchronization and drag-and-drop preset application.

---

## 🧩 1. Module Architecture

| File | Role |
| :--- | :--- |
| `MaterialWorkspaceSession.{h,cpp}` | Multi-tab document recipes, preset repository, layer add/remove/solo/visibility, turntable clock & impact pulse |
| `MaterialWorkspaceHost.{h,cpp}` | Binds `MaterialViewportHost`, `MaterialPreviewRenderer` (`VulkanInterchange`), and `ContentBrowser` (`128×128` live Shaderball preset thumbnails + drop zone `material.viewport`) |
| `MaterialViewportHost.{h,cpp}` | Topbar selectors (`Dynamic Material` preset, `Preview Specimen`, `Viewport Lighting`), `Impact Pulse`, `Play/Pause Clock`, Vulkan/CPU OpenPBR viewport, layer/parameter editing |
| `MaterialPreviewRenderer.{h,cpp}` | Vulkan 1.3 compute preview & CPU OpenPBR 1.1 reference fallback |

---

## 🏷️ 2. UI Controls

| Region | Control | Options / Function |
| :--- | :--- | :--- |
| **Topbar Dropdowns** | **`Dynamic Material`** | Select from built-in `DynamicMaterialRepository` presets |
| **Topbar Dropdowns** | **`Preview Specimen`** | `Shaderball` · `Cloth` · `Rounded Box` · `Ground Plane` |
| **Topbar Dropdowns** | **`Viewport Lighting`** | `Direct OpenPBR (No GI)` · `Real-Time GI` · `Path Tracer` |
| **Topbar Actions** | **`Impact Pulse`** · **`Play / Pause Clock`** | Trigger radial/surface impact ripple on dynamic material; pause/resume procedural clock |
| **`ContentBrowser` Drawer** | `Materials` category grid | Displays Vulkan-rendered `128×128` Shaderball thumbnails; click or drag-and-drop onto viewport (`material.viewport`) to assign |
| **Layer Stack Controls** | Add Procedural / Add Transition / Remove / Solo / Visibility | Edit multi-layer OpenPBR recipe stack |

---

## 📝 3. Hotkeys & Mouse Controls (`MaterialViewportHost`)

| Input | Action |
| :--- | :--- |
| `Space` | Toggle playback clock (`PlaybackRunning`) |
| `LMB` Drag | Orbit preview camera around specimen |
| `LMB` Click | Trigger viewport impact pulse at hit direction (`TriggerViewportImpactPulse`) |
| `Mouse Wheel` | Zoom / dolly preview camera (`OrbitDistanceM`) |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `8` files under `Engine/Workspaces/Material/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **Prohibited `<K-suffix>`** | **0** | None detected in `Engine/Workspaces/Material/` | 🟢 |
| **`Badge` / `Base`** | 25 | `Badge` (`13`), `Base` (`12`) in `MaterialViewportHost.cpp` | `Indicator`, `Primary` |
| **`Get*`/`Set*` / `Flag` / `Map`** | 19 | `Get/Set` (`10`), `Flag` (`5`), `Map` (`4`) | `Query*`/`Assign*`, `DirectiveMask`, `Register` |
| **`Frame` / `Is*/Has*` / `Context` / `Data`** | 4 | `1` hit each | `Step`/`Extent`, direct boolean, `Session`, `Payload` |
