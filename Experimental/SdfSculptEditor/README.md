# SDF Sculpt Studio (`Experimental/SdfSculptEditor/`)

Real-time **Hybrid 3D Signed Distance Field (SDF) Sculpting Workspace** built in Frontier's **Experimental Remastered** (**Sun Inspector + Fluid Editor + Vehicle Lab**) studio design language.

## Architecture & Capabilities

- **Hybrid Volumetric + Parametric SDF Engine**:
  - **Volumetric Clay Grid (`Engine/SdfVolume.js`)**: `64³`, `96³`, or `128³` metric signed-distance + RGB polypaint lattice over `[-2.0, +2.0]³ m`, packed into hardware-trilinear `RGBA16F` 3D textures via sub-millisecond dirty sub-brick `texSubImage3D` uploads.
  - **Non-Destructive Parametric CSG Stack (`Engine/SdfMath.js`)**: Up to 24 simultaneous analytical SDF primitives (`Sphere`, `Rounded Box`, `Capsule`, `Cylinder`, `Torus`, `Capped Cone`, `Octahedron`, `Hex Prism`, `Ellipsoid`, `Chain Link`, `Blade / Vesica`, `Gyroid TPMS Lattice`) with quaternion orientation, corner bevel rounding, hollow onion shells, Y-axis twist, mirror X/Z, polar radial arrays, and C1 polynomial `Smooth Union`, `Smooth Subtract`, `Smooth Intersect`, `Chamfer`, and `Color Tint`.
- **10 Volumetric 3D Sculpting Brushes**:
  1. `1` **Clay Buildup** — Continuous capsule-segment C1 smooth union clay strips (`Alt` to carve)
  2. `2` **Carve Scoop** — Smooth volumetric cavity subtraction (`Alt` to add)
  3. `3` **Draw / Inflate** — Gradient-aligned zero-isosurface displacement
  4. `4` **Smooth Relax** — Isotropic 3D 6-neighbor Laplacian SDF relaxation (`Shift` in any brush)
  5. `5` **Flatten / Trim** — Planar half-space trim & polish for hard-surface bevels and rock facets
  6. `6` **Pinch / Crease** — Radial distance contraction toward stroke centerline
  7. `7` **Grab / Warp** — Snapshot-backed 3D backward semi-Lagrangian volumetric advection
  8. `8` **Snake Hook** — Incremental volumetric advection + tapered tip deposit for horns and tendrils
  9. `9` **Stamp Primitive** — Interactive surface-aligned 3D analytical SDF stamping
  10. `0` **Polypaint RGB** — Trilinear RGB airbrush directly into the `RGBA16F` volume shell
- **Shading & Diagnostic Lenses (`Engine/SdfRaymarchViewport.js`)**:
  - `PBR Clay Studio` (GGX microfacet + subsurface clay wrap + 28-step soft penumbra shadows + 5-tap SDF ambient occlusion + ACES tone mapping)
  - `MatCap Red Wax`, `MatCap Studio Grey`, `MatCap Jade Rim`
  - `Mean Curvature` (3D Laplacian `∇²d` ridge/crevice analysis)
  - `SDF Field Slice` (live 3D cross-section signed-distance isoline plane + bottom-right 2D slice minimap)
  - `World Normals |∇d|`
- **Meshing & Native Frontier Interchange (`Engine/SdfMesherAndCodec.js`)**:
  - **Dual Surface Nets** iso-surface extractor producing outward-oriented CCW manifold triangles with smooth SDF gradient normals and vertex RGB colors
  - Native **Frontier `.sdfspace` (`SDF1` v1)** 68-byte binary header + IEEE-754 `Float16` payload import & export (compatible with `Frontier::VolumeDistanceArchive`)
  - **Wavefront `.OBJ`**, **Binary `.STL`**, **`.sdfsculpt.toml`**, and re-importable **`.sdfsculpt.json`** bundles

## Quick Start & Verification

```bash
# Start standalone preview server on http://0.0.0.0:8087/
python3 Experimental/SdfSculptEditor/Serve.py

# Run automated engine, codec, mesh & DOM validation suite
node Experimental/SdfSculptEditor/SdfSculptValidation.mjs
```
