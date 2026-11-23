# SpatialAcceleration/

Spatial acceleration structures and broadphase queries for the engine — the partitioning that turns
"test against everything" into "test against the few that matter." Built on the world conventions
from `../MetricSpace/` (right-handed, Z-up, metres) and the float types from
`EngineContext/Math/LinearAlgebra_Float32.h`; struct + free-function house style, no glm.

## Contents

| Component | Owns | Status |
|---|---|---|
| `ToroidalClipmapField.{h,cpp}` | Camera-tracked 3D voxel clipmap: radius-doubling level ladder, the toroidal wrap (`PhysicalCell = (WorldCell + Origin) mod Resolution`), per-level scroll evaluation into exposed L-slabs, residency invalidation, relight-ramp stub. Pure math, no Vulkan. | live |
| `SpatialPartition.{h,cpp}` | Octree / BVH broadphase partitioning over `SpatialExtent` regions. | planned |

## Notes

- Headers here are included **pillar-rooted** — `#include "EngineContext/SpatialAcceleration/..."` —
  matching the single `/I Internal` root that `EngineContext\Build.bat` sets.
- `ToroidalClipmapField` is the shared spine the sun-shadow clipmap (P6) and the GI irradiance-probe
  clipmap (P7b) will both stand on. Its `RelightRamp` is a **stub** payload for visualising the
  scroll-in transition — it carries no irradiance.
- Unit gate: `Executables\Validation\ClipmapFieldValidation` (no Vulkan; non-zero exit on regression).
