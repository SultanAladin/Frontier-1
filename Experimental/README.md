# 🧩 `Experimental/` — Reference Studios Pending Native C++20 Conversion

> **TL;DR**: `Experimental/` stages **ten reference editor shells and interactive web/WebGPU/WebGL2 studios** extracted from prior Frontier prototype branches. None of these prototypes ship directly in production; each serves as the verified UX, component, mathematical, and shader specification for shared **ISO C++20 `Engine/Components/` + `Engine/Workspaces/` + `.slang` GPU** modules (`FRONTIER_PROFILE_EDITOR`).

---

## 🏷️ 1. Staged Reference Editor UIs & Studios

| Studio Directory | Prototype Scope | Provenance | Target Native C++20 / `.slang` Destination |
| :--- | :--- | :--- | :--- |
| **`FrontierEditor/`** | Initial HTML/Vite React editor UI (`index.html`), Automotive CAD workbench (`cad.html`), and SVG icon catalog (`icons.html`) — 100% HTML/JS/CSS. | `c7egoist/Frontier` (`arena/f3437631-frontier` @ `186a7b8`) | `Engine/Components/` + `Engine/Workspaces/` + `Engine/Editor/` |
| **`ProjectZeroEditor/`** | Primary Project-Zero HTML/JS UI specification (with `README.md` render) uniting trapezoidal dock tabs, Notch quick-tools, Outliner tree, Construct drawer, and Light/Fog/Wind/Material inspectors. | `c7egoist/Frontier` (`arena/f3437631-frontier` @ `186a7b8`) | `Engine/Components/Docking/` + `Engine/Editor/Explorer/` + `Engine/Workspaces/` |
| **`FrontierWorldMirror/`** | Bidirectional 1:1 HTML mirror of the C++20 World Editor rendering the canonical Cornell-Studio Gallery via WebGL2 **Tier 1 GTX** (OpenPBR, Contact-Clip/SDF shadows, LTC area quad, 6-cone SDF GI, SSR, 2-layer Abbe glass) with interactive `EditorCamera` + Outliner/Inspector sync. | Duplicated & extended from `ProjectZeroEditor/` | `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.{h,cpp}` + `TieredWorldRenderSequence.{h,cpp}` + `Engine/Components/Panes/` |
| **`FluidEditor/`** | Gaseous plume, combustion, detonation, wildfire, and vortex solver (`WebGPU` + `WebGL2`) with `.gasscene.toml` serialization and flipbook baking. | `Frontier-OLD` @ `438aeaf` | `Engine/SimulationSpace/FluidSimulation/` + `Shaders/Compute/` + `Engine/Editor/` |
| **`FractureEditor/`** | Six fracture & debris solvers: baked Voronoi fracture, fast runtime shattering, convex partitioning, piece SDFs, rigid-body dynamics, and atmospheric drag. | `Frontier-OLD` @ `438aeaf` | `Engine/Geometry/` (Voronoi/convex partitioning) + `Engine/Physics/` (`JoltPhysics` rigid bodies) + `Engine/Editor/` |
| **`ParticleEditor/`** | GPU compute particle emitters, fibres/ribbons, procedural lightning, and live spatial force-field sampling (**Particle Studio**). | `Frontier-OLD` @ `438aeaf` | `Engine/Render/` & `Shaders/` (`.slang` GPU compute simulation & indirect draw) + `Engine/Editor/` |
| **`PhotometricEditor/`** | LM-63 `.ies` luminaire profiles, automotive headlamp optics, celestial sky scattering, ACES tone mapping, and isolux metering (**Photometric Studio**). | `Frontier-OLD` @ `438aeaf` | `Engine/Render/` (photometric light evaluation & sky LUTs in `.slang`) + `Engine/Editor/` |
| **`ForceFieldEditor/`** | Global wind synthesis + 16 bounded spatial force fields with RK2 streamline tracing and GPU vector/bounds overlays (**Force Field Studio**). | `Frontier-OLD` @ `438aeaf` | `Engine/Physics/` & `Shaders/` (shared `.slang` spatial force-field evaluation & GPU debug overlays) |
| **`VehicleLab/`** | `ControlVehicle` `FCT1` mesh inspection, VR38DETT torque & turbo spool curves, Pacejka MF6.1 tyre thermal/slip solver, and 10 telemetry channels. | `Frontier-OLD` @ `438aeaf` | `Engine/Physics/` (`JoltPhysics` wheeled vehicle + Pacejka/thermal solver) + `Engine/Assets/` + `Engine/Editor/` |
| **`UVEditor/`** | Synchronized 2D UDIM layout & 3D mesh viewport, LSCM conformal unwrapping, seam authoring, GPU wireframe overlays, and importance-weighted packing. | `c7egoist/Frontier` @ `3350774` | `Engine/Geometry/` (LSCM unwrap & UDIM packer) + `Shaders/` (GPU UV/wireframe overlays) + `Engine/Editor/` |
| **`TextureStudio/`** | Five-workspace texturing suite: `#material` look-dev, `#pattern` noise-scaled layer stack, `#stamp` decals, `#paint` 3D brush projection, and `#bake` mesh + volume SDF baking. | `c7egoist/Frontier` @ `3350774` | `Engine/Materials/`, `Engine/Assets/`, & `Shaders/` (`.slang` procedural noise, brush projection, AO/curvature/SDF bakers) + `Engine/Editor/` |

---

## 🧩 2. Shared Prototype Support Directories

| Path | Role |
| :--- | :--- |
| **`ForceFieldVisualization/`** | Shared 3D force-field glyph and streamline builder consumed by `ParticleEditor/` and `ForceFieldEditor/`. |
| **`Shared/UvTextureBridge.js`** | Cross-studio UV layout & mesh transfer bridge connecting `UVEditor/` and `TextureStudio/`. |
| **`VehicleProvingGrounds/{Archive,Source}/`** | Binary `FCT1` `ControlVehicle` body/rim/tyre streams and authored powertrain/Pacejka/thermal sheets loaded by `VehicleLab/`. |
| **`UI_COMPONENT_GUIDELINES.md`** | Visual layout, typography, and custom inspector widget specification shared across the reference studios. |

---

## 🔍 3. Local Preview & Automated Verification

```bash
# Serve all 8 Experimental studios simultaneously on http://0.0.0.0:8080/Experimental/
python3 Experimental/Serve.py 8080

# Run standalone Node ESM validation suites across all 8 staged studios
node Experimental/FluidEditor/src/SceneMetrics.mjs
node Experimental/FractureEditor/FractureValidation.mjs
node Experimental/ParticleEditor/Engine/CheckForceFields.mjs
node Experimental/PhotometricEditor/PhotometricValidation.mjs
node Experimental/ForceFieldEditor/ForceFieldValidation.mjs
node Experimental/VehicleLab/AssetInspectorValidation.mjs
node Experimental/UVEditor/UVValidation.mjs
node Experimental/TextureStudio/TextureStudioValidation.mjs
```
