# 🧩 `Engine/Threading/Threads/Streaming/`

> **TL;DR**: Dual-path SSD random-access and HDD elevator-coalesced sequential streaming thread for .Space and ThumbnailSheet pages (2 source/header/shader files under `Engine/Threading/Threads/Streaming/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AssetStreamThread.cpp` | Implementation of dual-path SSD random-access and HDD elevator-coalesced sequential streaming thread |
| `AssetStreamThread.h` | Dual-path SSD random-access and HDD elevator-coalesced sequential streaming thread for .Space and ThumbnailSheet pages |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Threading`
- **Primary Types**: `StorageHardwareTier`, `StreamSliceRequest`, `AssetStreamReading`, `AssetStreamThread`

## Usage

```cpp
#include "Engine/Threading/Threads/Streaming/AssetStreamThread.h"

using namespace Frontier::Threading;
StorageHardwareTier Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
