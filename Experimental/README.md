# 🧩 `Experimental/` — Reference Studios & Preserved SDF / GI Redo Baseline

> **TL;DR**: `Experimental/` holds browser HTML/JS/WebGPU reference editor UIs and domain studios that serve as the exact 1:1 visual and algorithmic specification for C++20 + `.slang` conversions (`CLAUDE.md` §2.1), alongside **`RemovedSdfGi/`** and **`SdfLightingDemoCpp/`** preserving the SDF shadow, SDF GI, emissive-GI, and deterministic path-tracer work queued for clean ground-up redo. *(Prior `FluidEditor`, `ParticleEditor`, and `ForceFieldEditor` trees were retired with their simplified C++ counterparts in `daed305` pending un-simplified re-architecture.)*

---

## 🏷️ 1. Reference Editor UIs & Domain Studios

| Directory | Prototype Scope | Native C++20 / `.slang` Target |
| :--- | :--- | :--- |
| **`ProjectZeroEditor/`** | Primary Project-Zero HTML/JS UI specification uniting trapezoidal dock tabs, top Notch, Outliner tree, Construct drawer, and Light/Fog/Wind/Material inspectors. | `Engine/Components/` + `Engine/Lifecycle/EditorLifecycle.*` |
| **`FrontierWorldMirror/`** | Bidirectional 1:1 HTML mirror of the C++20 World Editor rendering the Cornell-Studio Gallery with interactive `EditorCamera` + Outliner/Inspector sync. | `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.{h,cpp}` + `TieredWorldRenderSequence.{h,cpp}` |
| **`ProjectBrowser/`** | Reference HTML launcher for `.ProjectSpace` filtering, sorting, revision display, and workspace launch. | `Engine/Editor/Explorer/ProjectBrowser.{h,cpp}` |
| **`FrontierEditor/`** | Initial HTML/Vite scene inspector (`index.html`), CAD workbench (`cad.html`), and SVG icon sheet (`icons.html`). | `Engine/Components/` + `Engine/Workspaces/` |
| **`ComponentViewer/`** | Modular actor/vehicle component assembly, constraints, response curves, and inspection channel reference. | `Engine/Workspaces/ComponentEditor/` + `Engine/ComponentClass/` |
| **`UVEditor/`** | Synchronized 2D UDIM layout & 3D surface viewport, LSCM conformal unwrap, seam authoring, and concave packing. | `Engine/Workspaces/UV/` + `Engine/GeometricRaster/UV/` |
| **`TextureStudio/`** | Five-workspace texturing suite (`#material`, `#pattern`, `#stamp`, `#paint`, `#bake` surface + volume SDF baking). | `Engine/Workspaces/Texturing/` + `Engine/Workspaces/Material/` + `Engine/GeometricRaster/Baking/` |
| **`FractureEditor/`** | Six fracture & debris solvers: baked Voronoi fracture, fast runtime shattering, convex partitioning, piece SDFs, and rigid dynamics. | `Engine/Workspaces/Fracture/` + `Engine/GeometrySpace/Fracture/` |
| **`PhotometricEditor/`** | LM-63 `.ies` luminaire profiles, automotive headlamp optics, celestial sky scattering, ACES tone mapping, and isolux metering. | `Engine/GeometricRaster/` + `Engine/Shaders/` *(Pending C++20 port)* |
| **`VehicleLab/`** | `ControlVehicle` `FCT1` inspection, VR38DETT torque & turbo spool curves, Pacejka MF6.1 tyre thermal/slip solver, and 10 telemetry channels. | `Engine/SimulationSpace/PhysicsSpace/` + `Engine/Workspaces/` *(Pending C++20 port)* |
| **`ClothEditor/`** | Interactive cloth simulation and draping reference studio. | `Engine/SimulationSpace/` + `Engine/Workspaces/` *(Pending C++20 port)* |
| **`GeometryCompiler/`** | Cluster LOD pyramid, SDF brick compilation, dual contouring, and asset intake prototype. | `Engine/SpatialAcceleration/` + `Engine/GeometrySpace/` *(Pending C++20 port)* |
| **`LevelDesigner/`** | Interactive spatial layout and placement studio prototype. | `Engine/Workspaces/` *(Pending C++20 port)* |
| **`CelestialInspectorPrototype/`** | Celestial ephemeris, solar/lunar, and atmospheric inspector prototype. | `Engine/GeometricRaster/Atmosphere/` |

---

## 🧩 2. Lighting, Raytracing & SDF / GI Redo Baselines

| Directory | Role |
| :--- | :--- |
| **`RemovedSdfGi/`** | **Priority Redo Record** ([`RemovedSdfGi/README.md`](RemovedSdfGi/README.md)): Byte-exact copy of the SDF shadow, SDF GI, and emissive-GI engine units (`Engine/DistanceFieldSpace/`, `VoxelSdfClipmap`, `DeterministicIntegrators`), `ControlVehicleSurfaceLoader`, validation suites (`CleanEmissiveGiValidation`, `CleanEmissiveShadowValidation`, `VoxelSdfClipmapGiValidation`), `DeterministicPathTracer` results, 16+ proof `.png` sheets, and `Restore.py`. |
| **`SdfLightingDemoCpp/`** | Standalone dependency-free C++20 5-engine sphere-traced SDF lighting verification suite (`make render` / `make verify`). |
| **`DeterministicPathTracer/`** | Zero-RNG Sobol' `(0,2)`-net WebGPU compute path tracer on the 25,068-triangle `ControlVehicle` ([`DeterministicPathTracer/README.md`](DeterministicPathTracer/README.md)). |
| **`DeterministicRaytracingLab/`** | WebGPU 4-wide `BVH4` deterministic full-raytracing laboratory (`:8097`). |
| **`GtxReflectionBenchmark/`** | Frozen WebGPU GTX specular reflection & contact shadow benchmark (`:8096`). |
| **`ShadowAlgorithmLab/`** | Solar frustum, contact-PCSS, SMRT, and PCF shadow comparison lab. |

---

## 📝 3. Shared Prototype Assets & Local Preview

| Path | Role |
| :--- | :--- |
| **`Shared/`** | Cross-studio UV/texture bridge (`UvTextureBridge.js`) and shared browser utilities. |
| **`StudioIconSet/`** · `StudioIconSet.html` | Canonical SVG studio icon set and visual viewer. |
| **`VehicleProvingGrounds/{Archive,Source}/`** | Binary `FCT1` `ControlVehicle` body/rim/tyre streams and powertrain/Pacejka sheets loaded by `VehicleLab/`. |
| **`UI_COMPONENT_GUIDELINES.md`** | Shared visual layout, typography, and inspector control specification across all HTML reference studios. |

```bash
# Serve Experimental reference studios from repository root on http://0.0.0.0:8080/Experimental/
python3 Experimental/Serve.py 8080

# Run standalone Node ESM validation suites for active staged studios
node Experimental/FractureEditor/FractureValidation.mjs
node Experimental/PhotometricEditor/PhotometricValidation.mjs
node Experimental/VehicleLab/AssetInspectorValidation.mjs
node Experimental/UVEditor/UVValidation.mjs
node Experimental/TextureStudio/TextureStudioValidation.mjs
```
