# 🧩 `Engine/GeometrySpace/Deformation/`

> **TL;DR**: Solver-agnostic high-poly geometry deformation, tetrahedral/quad cage embedding, and `RGBA32F`/`RGBA16F` Vertex Motion Atlas (VAT) baking, 0-CPU playback, and PNG/JSON export.

---

## Submodules

| File | Responsibility |
| :--- | :--- |
| `CageWeightTypes.h` | 84-byte GPU vertex struct (`CageBoundVertex`), high-poly surface arrays, and live deformed output buffers |
| `TireSurfaceBuilder.{h,cpp}` | Shared `7,552`-vertex / `12,544`-triangle sculpted automotive performance tire (4 rain grooves, 320 beveled chevron lugs, 64 sidewall pads) |
| `TetrahedralCageEmbedder.{h,cpp}` | Bakes exact 3D barycentric coordinates ($\mathbf{D}_m^{-1}$) onto `DeformableBody` tetrahedra & evaluates Nanson cofactor normals ($\operatorname{cof}(\mathbf{F})$) |
| `SurfaceQuadCageEmbedder.{h,cpp}` | Bakes bilinear quad-cage weights, signed normal elevation offset, and tangent-frame normals for pneumatic XPBD shells |
| `VertexMotionAtlasTypes.h` | 256-column tiled `RGBA32F`/`RGBA16F` VAT descriptors, displacement bounds, and per-slice axle/collider poses |
| `VertexMotionAtlasBaker.{h,cpp}` | Offscreen deterministic baker running 3D FEM or XPBD through a rolling curb-impact cycle into VAT texels |
| `VertexMotionAtlasPlayer.{h,cpp}` | `0.00 ms` CPU playback cursor, looping, sub-slice linear interpolation, and synchronized rigid axle poses |
| `VertexMotionAtlasExporter.{h,cpp}` | Unreal/Unity-ready quantized `RGBA8` PNG + JSON displacement bounds exporter |
