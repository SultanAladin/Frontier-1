# MetricSpace

The metric foundation of the engine — the one place that fixes **which way is up**, **what a length
means**, and **how a placed frame maps to the world**. Header-only, POD, no link unit; built on the
float precision from `../Math/LinearAlgebra_Float32.h` (`Vector3f` / `Quaternionf`), never glm.

Everything downstream that reasons about world position — the orbit camera, the ground grid,
picking, spatial partitioning, distance/colour field bakes — reads its conventions from here instead
of hardcoding them.

## Files

| File | Owns |
|---|---|
| `CoordinateSpace.h` | The world reference frame: right-handed, **Z-up**, distances in **metres**. Reference axes (`ReferenceUpAxis` +Z, `ReferenceRightAxis` +X, `ReferenceForwardAxis` +Y) and ground-plane helpers (height along Up, XY ground coordinate). The ground plane is `z = 0`. |
| `CoordinateProjection.h` | A placed local frame (position in metres + unit-quaternion orientation) and the world↔local transforms: `WorldToLocal`/`LocalToWorld` (positions, translation applies), `WorldDirectionToLocal`/`LocalDirectionToWorld` (directions, rotation only), and the frame's own `RightAxis`/`UpAxis`/`ForwardAxis`. |
| `DistanceMetric.h` | The **metre standard**. `CentimetresPerMetre` is the single numeric bridge; `MetresFromCentimetres` / `CentimetresFromMetres` (scalar and `Vector3f` overloads) convert legacy centimetre lengths at exactly one boundary so a centimetre literal never leaks into the metric world. |
| `SpatialExtent.h` | An axis-aligned metric region on a regular voxel lattice: `RegionOrigin` + `RegionSpan` (metres) fix it in the world frame, `VoxelResolution` (unsigned per-axis) fixes the sampling. Helpers: `ResolveVoxelSize` (span / resolution, zero-safe), `ResolveRegionFarCorner`. |

## Conventions (do not diverge)

- **Handedness / up:** right-handed, +Z up. `Right (+X) × Forward (+Y) = Up (+Z)`.
- **Units:** metres everywhere. Centimetres exist only at legacy edges and are converted through
  `DistanceMetric.h`.
- **Vulkan clip-space handedness** is handled at the projection matrix (`LinearAlgebra_Float32.h`
  `ConstructPerspective`/`ConstructOrthographic`), **not** here. This directory fixes the *world*
  convention only.
