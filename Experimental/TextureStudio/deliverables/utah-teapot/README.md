# Utah teapot bake deliverables

## Surface maps: 512 × 512

Open `surface-512/index.html` to inspect all 27 actual maps, or download `surface-512/utah-teapot-512.zip`.

- 32 packed low UV charts; 4,032 low / 36,672 high triangles.
- 64 cosine-weighted AO samples, OpenGL +Y tangent normals and RGB-only four-pixel padding.
- 212,048 covered texels / hits; **zero misses** and zero nearest fallbacks.
- **Zero backwards raw tangent and bevel normals**, all map values finite, all normal families unit-length before PNG quantization.
- Projection selects the nearest eligible high intersection to the low surface, not the first face from the cage origin.
- Height no longer captures an unrelated lid/handle surface; thickness beyond its display range saturates white instead of creating a false black hole.
- `audit.json` contains measured results; `manifest.json` contains map meanings, settings, ranges and UV-island palette.

Bent normals are visibility directions, not ordinary geometric bump normals. There are 151 negative low-tangent-Z directions, but **all are in the projected high-normal hemisphere** (minimum decoded PNG dot = 0.0832); none were wrongly flipped or clamped to manufacture forward normals.

Vertex color is white because this built-in mesh has no vertex colors. Object ID `[1,0,0]` appears nearly black in a color viewer, but is valid 24-bit data. Thickness is opposite-surface distance, not certified porcelain wall thickness; open regions may have no inward hit. Curvature, bevel and weathering masks remain geometry-derived approximations. Mip-safe interchange and certified MikkTSpace parity are not claimed.

## Mesh-volume SDF: 64 × 64 × 64

Open `sdf-64/index.html`, or download `sdf-64/utah-teapot-sdf-64.zip`.

**Approximate only.** The standard teapot has **384 open boundary edges**, so a strict signed solid is rejected. The export contains 262,144 finite Float32 distance samples, 30,991 negative samples and 6,555 three-ray disagreements (2.50% of the whole padded volume). Agreement is not an accuracy percentage or proof of a reliable sign on open/intersecting geometry.

The ZIP contains:

- `distance.f32`: unclamped IEEE754 Float32 **little-endian** distances, in scene units.
- X-fastest layout: `x + nx * (y + ny * z)`.
- `sign-confidence.u8`: 255 = all three rays agree / on surface; 170 = two rays agree; 0 = unsigned/no sign.
- `manifest.json`: bounds, voxel-centre origin, per-axis spacing, dimensions, source/topology audit, algorithm and limitations.
- X/Y/Z midpoint distance and sign-agreement preview PNGs.

Negative = inside, positive = outside, according to the explicitly approximate odd-even three-ray parity classification. Distances are exact closest-point-to-triangle magnitudes at voxel centres; reconstructed fields still have grid-resolution error. The preview PNGs are 8-bit illustrations, not the full-precision volume.

A repaired closed mesh is required for reliable strict signing. Volume resolution is separate from the 512 × 512 map resolution; no 512³ volume is implied.

## Reproduce

```sh
npm run audit:teapot
npm run audit:sdf
npm run test:sdf
```

See `docs/volume-sdf-and-bake-audit.md` for implementation details and limitations.
