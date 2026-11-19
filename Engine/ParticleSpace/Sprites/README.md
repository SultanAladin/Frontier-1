# 🧩 `Engine/ParticleSpace/Sprites/`

> **TL;DR**: Deterministic multi-channel procedural sprite, crystal, wing, and dissolve-mask synthesizer shared by ParticleSpace and Pattern Studio (2 source/header/shader files under `Engine/ParticleSpace/Sprites/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ProceduralSpriteSynthesizer.cpp` | Deterministic multi-channel procedural sprite, crystal, wing, and dissolve-mask synthesizer implementation |
| `ProceduralSpriteSynthesizer.h` | Deterministic multi-channel procedural sprite, crystal, wing, and dissolve-mask synthesizer shared by ParticleSpace and Pattern Studio |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`
- **Primary Types**: `VoronoiDistancePair`, `ProceduralSpriteGroup`, `ProceduralSpriteArchetype`, `ProceduralSpriteSpec`, `ProceduralSpriteChannelSet`, `ProceduralSpriteSample`

## Usage

```cpp
#include "Engine/ParticleSpace/Sprites/ProceduralSpriteSynthesizer.h"

using namespace Frontier;
VoronoiDistancePair Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
