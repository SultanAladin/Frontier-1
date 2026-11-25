# 🧩 `Engine/Workspaces/UV/Operations/`

> **TL;DR**: Direct UV authoring operations: Seam Mark/Clear/Brush, UV Pinning, Edge Stitch, Split Island, Mirror, Align & Rectify Quads (2 source/header/shader files under `Engine/Workspaces/UV/Operations/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `UVAuthoringOperations.cpp` | Implementation of Seam Brush, UV Pinning, Island Stitch, Split Island, 2D UV Transforms, Align & Quad Rectification |
| `UVAuthoringOperations.h` | Direct UV authoring operations: Seam Mark/Clear/Brush, UV Pinning, Edge Stitch, Split Island, Mirror, Align & Rectify Quads |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Workspaces`
- **Primary Types**: `UVAlignAxis`, `UVAuthoringOperations`

## Usage

```cpp
#include "Engine/Workspaces/UV/Operations/UVAuthoringOperations.h"

using namespace Frontier::Workspaces;
UVAlignAxis Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
