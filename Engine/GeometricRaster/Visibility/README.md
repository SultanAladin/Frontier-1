# 🧩 `Engine/GeometricRaster/Visibility/`

> **TL;DR**: Two-Phase GPU/CPU Cluster Visibility Culler: Frustum + Normal-Cone + Continuous LOD Cut + Hi-Z Split (>=4 px HW vs <4 px SW) (8 source/header/shader files under `Engine/GeometricRaster/Visibility/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ClusterCullEncoder.cpp` | Two-Phase GPU/CPU Cluster Visibility Culler: Frustum + Normal-Cone + Continuous LOD Cut + Hi-Z Split (>=4 px HW vs <4 px SW) |
| `ClusterCullEncoder.h` | Two-Phase GPU/CPU Cluster Visibility Culler: Frustum + Normal-Cone + Continuous LOD Cut + Hi-Z Split (>=4 px HW vs <4 px SW) |
| `DepthPyramidSequence.cpp` | Conservative Reversed-Z Hierarchical Depth Pyramid (Hi-Z) builder with odd-dimension 3-tap floor guard & sphere occlusion test |
| `DepthPyramidSequence.h` | Conservative Reversed-Z Hierarchical Depth Pyramid (Hi-Z, Near=1.0, Far=0.0) for Two-Phase GPU/CPU Cluster Occlusion Culling |
| `VisibilityClusterRasterization.cpp` | Exact 64-Bit (8 B/px) Visibility Buffer (ReversedDepth32 \| VisibilityToken32) + HW/SW Micro-Raster + 2-Layer Glass Depth Peel |
| `VisibilityClusterRasterization.h` | Exact 64-Bit (8 B/px) Visibility Buffer (ReversedDepth32 \| VisibilityToken32) + HW/SW Micro-Raster + 2-Layer Glass Depth Peel |
| `VisibilityMaterialResolve.cpp` | Zero-G-Buffer Analytic Ray-Differential Barycentric Resolver, UV Footprint Jacobians, Motion Vectors, & 10-Lobe OpenPBR Shade |
| `VisibilityMaterialResolve.h` | Zero-G-Buffer Analytic Ray-Differential Barycentric Resolver, UV Footprint Jacobians, Motion Vectors, & 10-Lobe OpenPBR Shade |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `RigidClusterInstance`, `VisibleClusterCommand`, `TwoPhaseCullReport`, `ClusterCullSettings`, `ClusterCullEncoder`, `PerspectiveCameraBasis`, `OrthonormalCameraBasis`, `ProjectedScreenRect`, `DepthPyramidLevel`, `DepthPyramidSequence`, `ProjectedVertexScreen`, `UnpackedVisibilityHit`, `VisibilityBufferTargets`, `VisibilityClusterRasterization`, `AnalyticResolvedSurfaceSample`, `VisibilityResolveBuffer`, `VisibilityMaterialResolve`

## Usage

```cpp
#include "Engine/GeometricRaster/Visibility/ClusterCullEncoder.h"
#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"
#include "Engine/GeometricRaster/Visibility/VisibilityClusterRasterization.h"

using namespace Frontier::GeometricRaster;
RigidClusterInstance Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
