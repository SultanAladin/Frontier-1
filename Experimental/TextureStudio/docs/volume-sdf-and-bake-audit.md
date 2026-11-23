# 3D SDF, corrected projection and the Utah teapot bake audit

## Changes

- The corrugated “tin” request was corrected to **Corrugated Zinc**. Legacy preset links/settings remain readable; saved custom materials are not erased.
- Mesh projection now selects the **nearest eligible high intersection to the original low surface**, not simply the first hit from an extruded cage. High attribute normals facing away from the low target are rejected. Using attribute normals supports mirrored transforms without relying solely on face winding.
- A bounded, orientation-aware nearest fallback is available for ray misses. Every closest-point result is distance-checked, including a BVH with a single root leaf. Rejected backfaces, fallbacks and remaining misses are reported.
- AO uses cosine-weighted low-discrepancy hemisphere samples and world-position rotation, rather than UV-index-based scrambling. This is a diffuse visibility estimate, not baked scene illumination.
- Thickness rays look for the inward opposite surface beyond the display range. Farther hits saturate white; they no longer become false black holes merely because the selected range is short. An open source may still have no opposite hit.
- Bevel-normal averaging remains in the low reference hemisphere. It is a shading approximation, not changed topology.
- Material-patch maps use selectable green tiles, icons/mask swatches and real PNG thumbnails after baking. Selected subsets preserve their actual shader pass indices and channel metadata; previous-settings previews are marked stale.

## Audit: all 27 maps at 512×512

The source is the standard Three.js Utah teapot: **4,032 low triangles**, **36,672 high triangles** and **32 packed Bézier UV charts**. The reproducible audit uses **512×512**, **64 AO samples**, OpenGL +Y tangent normals, ±0.15 projection range and four pixels of RGB-only padding.

The initial double-sided, first-from-cage projection had zero misses but **5,881 backward tangent-normal texels**. That was not a clean bake. Removing backfaces helped, but selecting the nearest eligible intersection also removed inflated height spikes caused by neighbouring lid/handle surfaces being closer to the cage origin than the matching surface.

The final numeric audit checks:

- All **212,048 covered texels** project successfully; no missing high data is disguised by fallback colors.
- Every floating-point map value is finite.
- All seven normal families are unit vectors before 8-bit quantization.
- Raw tangent and bevel normals stay in the low reference hemisphere; backward normal counts are recorded, including bent normals for inspection.
- Bent directions remain in the projected **high-normal** hemisphere. The final bake has 151 negative low-tangent-Z directions but zero high-hemisphere violations (minimum decoded PNG dot 0.0832). Bent normals describe visibility, not bump geometry; forcibly flipping/clamping these directions would be the wrong algorithm.
- Coverage and alpha remain undilated; RGB padding does not turn uncovered UV gutters opaque.
- Chart IDs and object IDs remain separate. UV-island palette has 32 entries; the teapot is one source object.

`audit.json`, `manifest.json`, all actual PNGs and the browsable report accompany the export. These are not illustrative map swatches.

### Interpreting “clean” correctly

Passing these checks rules out the specific projection errors, NaNs, invalid normals and coverage holes tested. It is **not** a claim of Blender/Substance/Toolbag parity or certified MikkTSpace interchange. Curvature is a local signed normal-gradient estimate; dust/dirt/edge wear and bevel are geometry-derived masks/effects. Eight-bit maps still quantize the result, and finite sampling has noise. The visual report exposes every map for inspection rather than hiding limitations.

Vertex color is legitimately white: this built-in source has no vertex colors. Object ID is almost black in a color viewer because the data bytes are `[1,0,0]`. Thickness can be zero where the open teapot has no inward hit; this is an opposite-surface distance, not guaranteed porcelain wall thickness.

## Volume SDF is a separate output

The user explicitly selected **3D mesh-volume SDF**, not a 2D mask distance transform. Volume resolution is therefore independent of the 512×512 texture maps.

In Baking Studio, choose **Volume SDF**:

1. Choose/import the high source. Low UVs are not required for a volume.
2. Select **16, 32, 64, 96 or 128 voxels per axis**; 64³ is the default.
3. Choose strict signed, explicitly approximate signed, or unsigned distance.
4. Bake, inspect X/Y/Z midpoint distance and agreement slices, and download the volume ZIP.

The algorithm is actual **Euclidean closest-point-to-triangle distance**, accelerated by MeshBVH. It is not height along a cage ray, AO, a bounding-box distance or an aliased 2D field. Inside/outside uses the majority of three oblique ray parities, deduplicating coincident triangle hits. The fill rule is **odd-even**, not a constructive union of overlapping solids.

### Topology and honest signs

Tolerance-welded edges are audited for boundaries, non-manifold edges and inconsistent orientation. Welding is diagnostic only: query geometry is not silently capped, remeshed or repaired. Strict mode rejects a non-closed source. The audit cannot certify that an otherwise closed mesh is free of geometric self-intersections.

The stock teapot has **384 boundary edges**. It does not define a reliably closed solid. Consequently:

- A strict signed teapot volume is rejected.
- An unsigned teapot distance volume is valid but has no inside/outside sign.
- The provided **64³ signed teapot example is explicitly approximate**. It contains 262,144 finite samples, with 6,555 ray disagreements (2.50% of the whole padded volume). Agreement is **not an accuracy percentage**; three rays can agree on an incorrect sign around an open/intersecting source.

Supply a repaired, closed, consistently oriented solid for reliable strict signing. No hidden hole-sealing changes the requested teapot.

## Binary format

ZIP schema: **`alloy.volume-sdf.v1`**.

| File / property        | Meaning                                                                                                                                   |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `distance.f32`         | Unclamped IEEE754 Float32, explicitly little-endian; source scene units. Negative inside, zero surface, positive outside in signed modes. |
| Layout                 | X-fastest: `index = x + nx * (y + ny * z)`.                                                                                               |
| Sampling               | Voxel centres, not corners. `voxelCenterOrigin` and per-axis `voxelSpacing` are explicit in `manifest.json`.                              |
| `sign-confidence.u8`   | 255: all three parity rays agree or on surface; 170: only two agree; 0: unsigned mode/no sign. This is agreement, not a probability.      |
| `slice-x/y/z.png`      | 8-bit midpoint previews. Encoding and preview range are recorded; they are not the full-precision volume.                                 |
| `confidence-x/y/z.png` | Green agreement / orange disagreement / gray unsigned previews.                                                                           |
| `manifest.json`        | Dimensions, units, bounds, origin, layout, source/topology diagnostics, algorithm, statistics and limitations.                            |

A 512³ float volume alone is **512 MiB**, before the agreement array, query data and copies. Browser volumes are bounded to 128³ and 250,000 high triangles. Texture maps can still be 512×512. Trilinear reconstruction introduces grid-resolution error and is not guaranteed to be a conservative ray-marching or collision bound.

## Reproduction and tests

```sh
npm run audit:teapot
npm run audit:sdf
npm run test:sdf
npm run test:baking
npm run build
npm run test:standalone
```

Audit intermediates are generated in ignored `bake-audits/`. Requested result archives/previews are retained under `deliverables/`.

Tests include exact analytic cube distances (centre, face and outside corner), voxel-centre/x-fastest layout, strict rejection of the open teapot, finite/zero/resource/cancel behavior, an actual inline-worker Float32 ZIP, explicit approximate GUI output, nearest-facing projection selection, backface rejection, explicit nearest bounds, thickness-range saturation and real surface-map subset exports. The offline standalone suite also exercises the new volume worker without external asset requests.
