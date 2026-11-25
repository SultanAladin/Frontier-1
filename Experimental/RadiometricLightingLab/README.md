# 🧩 `Experimental/RadiometricLightingLab/` — Unified Many-Light Photometric Reference Lab

> **TL;DR**: Interactive WebGL2 3D automotive reference demonstration for [`Documentation/Plans/RadiometricLightingArchitecturePlan.md`](../../Documentation/Plans/RadiometricLightingArchitecturePlan.md), uniting all **7 luminaire classes**, **Conty Estevez & Kulla (2018) Spatial-Orientation Partition Cut**, **4 render engines**, and **live A/B solver comparisons** across all 3 `AutoSpace` lobby environments up to **96 active luminaires**.

---

## 🏷️ 1. Covered Luminaire Classes & Solvers

| Capability | Live Implementation |
| :--- | :--- |
| **7 Luminaire Classes (`64 B` Unified Layout)** | `PointSphere` (`0`), `AnalyticSpot` (`1`), `PhotometricIes` (`2` — `128x32` LM-63 Type-C + 4-leaf barn-door clip), `AutomotiveEce` (`3` — ECE R112 `-1.0°` asymmetric kink), `PolygonalAreaLtc` (`4` — Dual-Lobe Basecoat + Clearcoat Clipped Heitz LTC), `LinearTubeSegment` (`5` — Picott diffuse + Karis segment GGX), `EmissiveSurface` (`6` — Stokes spherical-polygon contour), `EnvironmentPortal` (`7` — Pit-Archway LTC sky window) |
| **3 `AutoSpace` Lobby Rigs** | **Lobby 1 · Cyclorama**, **Lobby 2 · Forest LED Volume Stage**, **Lobby 3 · Racing Pit Garage** (open track portal, linear ceiling LEDs, live hydraulic 4-post lift) |
| **4 Render Modes** | `0 · MatCap`, `1 · OpenPBR Direct`, `2 · Hybrid GI (SDF Visibility + Analytic BRDF)`, `3 · Progressive Sobol Path Trace` |
| **4 Many-Light Solvers + Split Wipe** | `⭐ Analytic + Partition Cut` vs. `Clustered 3D Froxels` (with froxel overlap heatmap) vs. `1-spp Spatiotemporal ReSTIR DI` vs. `Pure SDF Voxel Lighting` |

---

## 📝 2. Local Launch & Validation

```bash
# Run standalone Node ESM validation
node Experimental/RadiometricLightingLab/RadiometricLightingValidation.mjs

# Launch live preview on http://0.0.0.0:8088/
python3 Experimental/RadiometricLightingLab/Serve.py --port 8088
```
