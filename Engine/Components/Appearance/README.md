# 🧩 `Engine/Components/Appearance/`

> **TL;DR**: Built-in interface themes, accent swatches, semantic alert swatches, and typography scale slots (4 source/header/shader files under `Engine/Components/Appearance/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ThemeRepository.cpp` | Exact 1:1 surface themes, accent swatches, semantic alert swatches, and typography scale slots. |
| `ThemeRepository.h` | Built-in interface themes, accent swatches, semantic alert swatches, and typography scale slots |
| `TypographyRepository.cpp` | Discover and load .FontSpace typeface weights into Dear ImGui with CSS weight fallback resolution |
| `TypographyRepository.h` | Discover and load .FontSpace typeface weights into Dear ImGui with CSS weight fallback resolution |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`
- **Primary Types**: `TypeSlotSpecimen`, `ThemePreset`, `AccentPreset`, `FontWeightClass`, `TypeScaleSlot`, `ColorQuad`, `ThemeSurfaceSet`, `AppearanceSettings`, `ThemeRepository`, `TypefaceWeightEntry`, `TypefaceGroup`, `TypographyRepository`

## Usage

```cpp
#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"

using namespace Frontier;
TypeSlotSpecimen Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
