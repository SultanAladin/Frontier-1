# 🧩 `Engine/Components/VectorIcons/`

> **TL;DR**: SVG 1.1 path decoder, round-cap stroker, and reusable vector icon repository for engine UI controls and panels (2 source/header/shader files under `Engine/Components/VectorIcons/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `VectorIconRepository.cpp` | SVG 1.1 path flattening, round-cap polyline stroking, and built-in vector icon lookup entries. |
| `VectorIconRepository.h` | SVG 1.1 path decoder, round-cap stroker, and reusable vector icon repository for engine UI controls and panels |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`
- **Primary Types**: `SvgPathScanner`, `NavigationIcon`, `ControlIcon`, `VectorIconPlacement`, `VectorIconEntry`, `VectorIconRepository`

## Usage

```cpp
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

using namespace Frontier;
SvgPathScanner Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
