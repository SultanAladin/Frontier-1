# 🧩 `Engine/Editor/Explorer/`

> **TL;DR**: Editor-tier ContentBrowser orchestrator connecting ColourCodex (.ColourSwatch), ThumbnailSheet streaming, and workspace domain masks (4 source/header/shader files under `Engine/Editor/Explorer/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ContentBrowser.cpp` | Implementation of Editor-tier ContentBrowser orchestrator |
| `ContentBrowser.h` | Editor-tier ContentBrowser orchestrator connecting ColourCodex (.ColourSwatch), ThumbnailSheet streaming, and workspace domain masks |
| `ProjectBrowser.cpp` | Implementation of ProjectBrowser matching Experimental/ProjectBrowser 1:1 using PixelSpace, Studio Controls, & TieredWorldRenderSequence |
| `ProjectBrowser.h` | Native C++20 Project Browser built strictly on PixelSpace, Engine/Components/Controls, TieredWorldRenderSequence, & ProjectGenerator |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Editor::Explorer`, `Frontier::GeometricRaster`
- **Primary Types**: `ContentBrowser`, `BrowserSwatchScheme`, `BrowserFilterMode`, `BrowserEntryCategory`, `BrowserInspectorTab`, `BrowserReviewNote`, `BrowserCollectionEntry`, `BrowserWorkspaceLaunchDirective`, `ProjectBrowser`

## Usage

```cpp
#include "Engine/Editor/Explorer/ContentBrowser.h"
#include "Engine/Editor/Explorer/ProjectBrowser.h"

using namespace Frontier::Editor::Explorer;
ContentBrowser Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
