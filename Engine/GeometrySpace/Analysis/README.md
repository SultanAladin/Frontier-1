# 🧩 `Engine/GeometrySpace/Analysis/`

> **TL;DR**: Discrete Meyer cotangent mean & angle-deficit Gaussian curvature plus Mikkelsen/Duff continuous corner tangent bases (4 source/header/shader files under `Engine/GeometrySpace/Analysis/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CurvatureAnalysis.cpp` | Implementation of Meyer discrete differential curvature and Mikkelsen/Duff continuous corner tangent bases |
| `CurvatureAnalysis.h` | Discrete Meyer cotangent mean & angle-deficit Gaussian curvature plus Mikkelsen/Duff continuous corner tangent bases |
| `StretchMeasurement.cpp` | Implementation of exact 2x2 UV-Jacobian singular value decomposition, Sander stretch, and Symmetric Dirichlet metrics |
| `StretchMeasurement.h` | Exact 2x2 UV-Jacobian singular value decomposition, Sander L2/L-infinity stretch, and Symmetric Dirichlet distortion metrics |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace`
- **Primary Types**: `VertexCurvatureEntry`, `CurvatureAnalysisSummary`, `CornerTangentBasis`, `FaceStretchMetrics`, `StretchMeasurementSummary`

## Usage

```cpp
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"
#include "Engine/GeometrySpace/Analysis/StretchMeasurement.h"

using namespace Frontier::GeometrySpace;
VertexCurvatureEntry Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
