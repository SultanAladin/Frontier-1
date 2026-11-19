# 🧩 `Engine/ParticleSpace/Presets/`

> **TL;DR**: Authoritative C++ repository of all 102 Frontier Particle Studio presets (93 active + 9 quarantined across 15 VFX categories) (2 source/header/shader files under `Engine/ParticleSpace/Presets/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ParticlePresetRepository.cpp` | Authoritative C++ repository of all 102 Frontier Particle Studio presets (93 active + 9 quarantined) |
| `ParticlePresetRepository.h` | Authoritative C++ repository of all 102 Frontier Particle Studio presets (93 active + 9 quarantined across 15 VFX categories) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::ParticleSpace`
- **Primary Types**: `ParticlePresetMetadata`

## Usage

```cpp
#include "Engine/ParticleSpace/Presets/ParticlePresetRepository.h"

using namespace Frontier::ParticleSpace;
ParticlePresetMetadata Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
