# 🧩 `Engine/Interchange/`

> **TL;DR**: External colour & gradient interchange: HTML Studio JSON/TOML colour blocks, CSS linear-gradient, SVG <linearGradient>, GIMP .gpl/.ggr, Adobe .ase/.aco & .cube 1D LUT (8 source/header/shader files under `Engine/Interchange/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ColourInterchange.cpp` | Implementation of external colour & gradient interchange (HTML Studio JSON, CSS, SVG, GIMP .gpl/.ggr, Adobe .ase/.aco, .cube 1D LUT) |
| `ColourInterchange.h` | External colour & gradient interchange: HTML Studio JSON/TOML colour blocks, CSS linear-gradient, SVG <linearGradient>, GIMP .gpl/.ggr, Adobe .ase/.aco & .cube 1D LUT |
| `GeometryInterchange.cpp` | Implementation of Wavefront .obj & glTF 2.0 .gltf multi-UDIM PolygonNetwork + FacePartitionRegister import and export |
| `GeometryInterchange.h` | External Wavefront .obj and glTF 2.0 .gltf geometry interchange importing/exporting multi-UDIM PolygonNetworks & FacePartitionRegisters |
| `IndexedTriangleStreamInterchange.cpp` | Implementation of FCT1 binary stream decoding, Z-up -> Y-up axis conversion, and CourseMaterial slot mapping |
| `IndexedTriangleStreamInterchange.h` | Binary FCT1 stream importer converting Z-up vehicle triangle streams & CourseMaterial slots into Y-up PolygonNetworks |
| `ParticleInterchange.cpp` | Implementation of ParticleInterchange delegating colour to ColourInterchange and 3D surfaces to GeometryInterchange |
| `ParticleInterchange.h` | External particle interchange: HTML Particle Studio JSON snapshots, Workspace Handoff packets, & 3D .obj/.gltf surface anchor intake |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Interchange`
- **Primary Types**: `ExternalColourFormat`, `ColourInterchange`, `ExternalGeometryFormat`, `GeometryInterchange`, `CourseMaterialDescriptor`, `IndexedTriangleStreamInterchange`, `ExternalParticleFormat`, `ParticleInterchange`

## Usage

```cpp
#include "Engine/Interchange/ColourInterchange.h"
#include "Engine/Interchange/GeometryInterchange.h"
#include "Engine/Interchange/IndexedTriangleStreamInterchange.h"

using namespace Frontier::Interchange;
ExternalColourFormat Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
