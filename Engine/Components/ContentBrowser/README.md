# 🧩 `Engine/Components/ContentBrowser/`

> **TL;DR**: Named colour alias library supporting HEX/RGB/CMYK/HSL metrics, contrast ink selection, spectrum filtering, and TOML persistence (6 source/header/shader files under `Engine/Components/ContentBrowser/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ColourSwatchBrowserView.cpp` | Implementation of ColourSwatchBrowserView named colour aliases, spectrum categorization, contrast ink selection, and TOML serialization |
| `ColourSwatchBrowserView.h` | Named colour alias library supporting HEX/RGB/CMYK/HSL metrics, contrast ink selection, spectrum filtering, and TOML persistence |
| `ContentBrowserPanel.cpp` | Implementation of ContentBrowser drawer, pre-baked ThumbnailSheet rendering, SSD/HDD streaming, workspace masks & ThemeRepository styling |
| `ContentBrowserPanel.h` | Bottom-edge ContentBrowser drawer with pre-baked ThumbnailSheet, dual-path SSD/HDD streaming, strict workspace masks & ColourSwatchBrowserView |
| `ThumbnailSheetRepository.cpp` | Implementation of pre-baked ThumbnailSheet pages, flat 64-byte ContentIndex directory, workspace masks, and quarantine import intake |
| `ThumbnailSheetRepository.h` | Pre-baked ThumbnailSheet pages, flat 64-byte ContentIndex directory, workspace domain masks, and two-step quarantine import intake |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`
- **Primary Types**: `ColourFilterGroup`, `ColourSortOrder`, `ColourRepositoryLayout`, `ColourCollectionScope`, `ColourSwatchEntry`, `ColourContrastInk`, `ColourSwatchBrowserView`, `StandardRepositoryLayout`, `ContentShelfExtent`, `ColourSlotRegistration`, `ColourRampSlotRegistration`, `MatCapSlotRegistration`, `ContentDropZoneRegistration`, `ContentBrowserPointerInput`, `ContentBrowserReading`, `ContentBrowserPanel`, `BinaryIndexFilePrefix`, `ContentCategory` (+4 more)

## Usage

```cpp
#include "Engine/Components/ContentBrowser/ColourSwatchBrowserView.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/ContentBrowser/ThumbnailSheetRepository.h"

using namespace Frontier;
ColourFilterGroup Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
