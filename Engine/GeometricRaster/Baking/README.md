# 🧩 `Engine/GeometricRaster/Baking/`

> **TL;DR**: Dual GTX SAH-BVH4 + optional RTX VK_KHR_acceleration_structure (BLAS + TLAS) upload & synchronization for 17-channel GPU surface baking (10 source/header files under `Engine/GeometricRaster/Baking/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BakingAccelerationStructure.cpp` | Dual GTX BVH4 + hardware RTX VK_KHR_acceleration_structure BLAS/TLAS builder and upload manager implementation |
| `BakingAccelerationStructure.h` | Dual GTX SAH-BVH4 + optional RTX VK_KHR_acceleration_structure (BLAS + TLAS) upload & synchronization for GPU baking |
| `GeometrySpecimenRasterizer.cpp` | 1:1 C++ miniature 3-point OpenPBR studio thumbnail raytracer matching FrontierWorldMirror ModelThumbnailRenderer |
| `GeometrySpecimenRasterizer.h` | Calibrated 3-point OpenPBR studio thumbnail raytracer baking transparent RGBA8 silhouettes with tight world-AABB auto-fit |
| `GpuTextureBaker.cpp` | Implementation of Pure-GPU Vulkan 17-Channel Surface Texture Baker (UV G-Buffer, BVH4 GTX / RayQuery RTX, Dilation & PNG export) |
| `GpuTextureBaker.h` | Pure-GPU Vulkan 17-channel surface texture baking engine (UV G-buffer, GTX BVH4 compute, RTX VK_KHR_ray_query, & gutter dilation) |
| `StudyGeometryBuilder.cpp` | Implementation of High/Low procedural baking study pairs, Wavefront OBJ/STL import, UV-seam island flood & tangent smoothing. |
| `StudyGeometryBuilder.h` | High/Low baking study synthesis, .OBJ/.STL file import, UV-island flood extraction, and GPU triangle/vertex upload preparation |
| `TextureBakerRasterizer.cpp` | Implementation of interactive Vulkan 3D studio PBR + live baked texture + High-Poly wire + Cage overlay rasterizer |
| `TextureBakerRasterizer.h` | Interactive Vulkan 3D studio PBR + live baked target + High-Poly wire + Cage overlay rasterizer for Baking Studio viewports |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster::Baking`, `Frontier::GeometricRaster`
- **Primary Types**: `BakeAccelerationEngine`, `BakingAccelerationStructure`, `SpecimenPartShape`, `SpecimenStripePattern`, `Matrix3x3`, `SpecimenPart`, `GeometrySpecimenArchetype`, `GeometrySpecimenDescriptor`, `SurfaceBakeExecutionSpec`, `BakeCompletionSummary`, `GpuTextureBaker`, `TriangulatedCorner`, `StudyGeometryCategory`, `PreparedStudyPair`, `StudyGeometryBuilder`, `BakeOrbitCamera` (+2 more)

## Usage

```cpp
#include "Engine/GeometricRaster/Baking/BakingAccelerationStructure.h"
#include "Engine/GeometricRaster/Baking/GeometrySpecimenRasterizer.h"
#include "Engine/GeometricRaster/Baking/GpuTextureBaker.h"

using namespace Frontier::GeometricRaster::Baking;
BakeAccelerationEngine Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
