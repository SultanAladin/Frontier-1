# Frontier World Mirror — Tier 1 GTX (`C++20 <-> HTML` Bidirectional Mirror)

Duplicated from `Experimental/ProjectZeroEditor/` (which remains untouched as the clean UI shell specification) to serve as the **1:1 live HTML mirror** of the native ISO C++20 editor (`Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.{h,cpp}`, `TieredWorldRenderSequence.{h,cpp}`, `OutlinerPanel`, `ViewportPanel`, and `InspectorPanel`).

## Synchronized Capabilities

- **Exact 17-Entry Cornell-Studio Gallery Outliner**:
  - `Showcase` (`10` OpenPBR specimens: `Gallery Floor`, `Gallery Back Wall`, `Crimson Left Wall`, `Emerald Right Wall`, `Clearcoat Roof Baffle`, `Anisotropic Gold Pedestal`, `Mirror Chrome Sphere`, `Plaster Specimen West`, `Plaster Specimen East`, `Abbe Crown Glass Orb`)
  - `Cameras` (`Editor Camera` at `(0.00, -3.55, 0.42)` targeting `(0.00, 0.55, 0.15)`, `43.0°` vertical FOV)
  - `World` (`Sun` directional solar emitter at `normalize(0.42, -0.55, 0.72)`)
  - `Lighting` (`Ceiling Softbox` clipped-horizon LTC polygonal area quad at `(0.55, 0.35, 1.82)`, `1.70 × 1.30 m`)
- **Calibrated WebGL2 Tier 1 GTX World Renderer (`GtxWorldViewport.jsx`)**:
  - OpenPBR surface scattering (Fujii Oren-Nayar, Anisotropic GGX, Adobe F82 conductor tint, Turquin energy compensation, Dielectric Clearcoat)
  - Solar directional shadow with GTX Contact-Clip + SDF Cone Penumbra (zero VSM)
  - Clipped-horizon Polygonal Quad LTC area light integration
  - 2-scale GTX Ambient & 2.2 m Chassis Occlusion + 6-Cone SDF Diffuse Global Illumination with live Crimson & Emerald wall multi-bounce color bleed
  - Single-bounce gallery specular reflections + 2-layer Abbe (`Abbe = 26`, `IOR = 1.52`) dispersive crown glass peel + ACES filmic tonemap
- **Interactive `EditorCamera` + Outliner/Inspector Sync**:
  - Orbit (`Drag`), Pan (`Shift+Drag` / `Middle-Drag`), Free-Look (`Right-Drag`), Dolly (`Wheel`), Fly (`Right+WASD`/`QE`), Frame Selected (`Focus` button), Click-to-Pick 3D viewport selection, and bidirectional `Position` / `Target` / `Focal Length` editing in the Inspector.

## Rebuild & Serve

```bash
node Experimental/FrontierWorldMirror/Build.mjs
python3 Experimental/FrontierWorldMirror/Serve.py 8098
```
