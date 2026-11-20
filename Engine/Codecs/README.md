# 🧩 `Engine/Codecs/`

> **TL;DR**: Two-Tier Hybrid domain codecs persisting TOML manifests & 64B-aligned binary assets (30 source/header files under `Engine/Codecs/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AcousticCodec.cpp` | .AcousticSpace waveform/acoustics codec implementation |
| `AcousticCodec.h` | .AcousticSpace audio & spatial acoustics encoder & decoder |
| `ColourCodec.cpp` | .ColourSpace, .ColourRamp & .ColourSwatch encoder/decoder implementation with inline manifest helpers for sibling codecs |
| `ColourCodec.h` | .ColourSpace, .ColourRamp & .ColourSwatch encoder/decoder persisting radiometric swatches, modular ramps, spectra & 64B-aligned LUTs |
| `ComponentSpaceCodec.cpp` | Implementation of .ComponentSpace Two-Tier Hybrid TOML manifest + 64B-aligned binary thumbnail & Revision serialization |
| `ComponentSpaceCodec.h` | .ComponentSpace Two-Tier Hybrid encoder & decoder for ComponentClassDefinition hierarchies, physics constraints & behaviours |
| `ControlVehicleSurfaceDecoder.cpp` | FCT1 binary facet archive decoder assembling the 25,068-triangle Controlled Integrator vehicle |
| `ControlVehicleSurfaceDecoder.h` | FCT1 binary facet archive decoder (Body + 4 Rims + 4 Tyres) with 38-deg creased normals |
| `FontCodec.cpp` | .FontSpace encoder & decoder implementation |
| `FontCodec.h` | .FontSpace encoder & decoder persisting vector typography metrics, kerning pairs, and 64B-aligned contour streams |
| `GeometryCodec.cpp` | Implementation of .GeometrySpace Two-Tier Hybrid encoder & decoder (full n-gon PolygonNetwork + legacy VehiclePolygon support) |
| `GeometryCodec.h` | .GeometrySpace Two-Tier Hybrid encoder & decoder for PolygonNetwork, FacePartitionRegister, ClusterPyramid & WideBoundingTree |
| `ImageCodec.cpp` | .ImageSpace encoder & decoder implementation |
| `ImageCodec.h` | .ImageSpace encoder & decoder persisting linear-light .CanvasLayer metadata, .LayerStackOrder, and 64B-aligned pixels |
| `InputCodec.cpp` | Two-Tier Hybrid .InputSpace encoder & decoder implementation (.DeviceProfile + .AxisCalibration + .ChordRegister + 64B binary index) |
| `InputCodec.h` | .InputSpace encoder & decoder persisting chord registers, viewport navigation gestures (Frontier / Blender / Unreal / Custom), and axes |
| `MaterialCodec.cpp` | .MaterialSpace Two-Tier Hybrid container encoder & decoder (TOML manifest + 64B-aligned 128 B CompactMaterialEntry & 32 B BounceSlabEntry) |
| `MaterialCodec.h` | .MaterialSpace encoder & decoder persisting .ShadingModel, .ParameterSlot, 10-lobe SurfaceMaterial, 128 B registers, & 32 B summaries |
| `NetworkCodec.cpp` | .NetworkSpace Two-Tier Hybrid container encoder/decoder (TOML .SessionRouting tier + 64B-aligned .ReplicationPolicy binary tier) |
| `NetworkCodec.h` | .NetworkSpace Two-Tier Hybrid container encoder/decoder (TOML .SessionRouting tier + 64B-aligned .ReplicationPolicy binary tier) |
| `PhotometricProfile.cpp` | ANSI/IESNA LM-63-1995/2002 Type C (.ies) photometric profile parser, symmetry folder, and 128x32 resampler |
| `PhotometricProfile.h` | ANSI/IESNA LM-63-1995/2002 Type C (.ies) photometric profile parser and canonical 128x32 candela grid |
| `ProjectCodec.cpp` | .ProjectSpace encoder & decoder implementation |
| `ProjectCodec.h` | .ProjectSpace encoder & decoder persisting project configuration, startup workspace, and dependency locks |
| `RevisionCodec.cpp` | Binary .Baseline and .Revision section serialization implementation |
| `RevisionCodec.h` | Binary .Baseline and .Revision section encoder & decoder for lossless parametric RevisionTracker persistence |
| `SpaceContainerCodec.cpp` | Two-Tier Hybrid .Space container encoder & decoder implementation (Option 1A + Option 2A, 64B alignment, FNV-1a) |
| `SpaceContainerCodec.h` | Universal Two-Tier Hybrid .Space container encoder & decoder (PascalCase TOML manifest + 64B-aligned binary sections) |
| `TextureCodec.cpp` | .TextureSpace encoder & decoder implementation |
| `TextureCodec.h` | .TextureSpace encoder & decoder persisting .ChannelPack specifications, UDIM .TexelLayer entries, and 64B-aligned texels |
| `UVCodec.cpp` | .UVSpace (.UDIM + .UVTile) encoder & decoder implementation |
| `UVCodec.h` | .UVSpace encoder & decoder persisting .UDIM layout specifications and 64B-aligned .UVTile parameterization streams |
| `VectorCodec.cpp` | .VectorSpace encoder & decoder implementation |
| `VectorCodec.h` | .VectorSpace encoder & decoder persisting .StrokeAppearance, .PathCurve, [VectorWorkspace], [[VectorLayers]], and .SvgXml sections |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Codecs`, `Frontier::Workspaces`
- **Primary Types**: `RadiometricSwatchEntry`, `ColourSpaceDocumentModel`, `ColourEncoder`, `ColourDecoder`, `ColourCodec`, `ComponentSpaceEncoder`, `ComponentSpaceDecoder`, `KerningAdjustmentPair`, `FontSpaceDocumentModel`, `FontEncoder`, `FontDecoder`, `FontCodec`, `GeometrySpaceDocument`, `GeometryEncoder`, `AcousticCodec`, `ImageCodec`, `MaterialCodec`, `UVCodec` (+45 more)

## Usage

```cpp
#include "Engine/Codecs/ColourCodec.h"
#include "Engine/Codecs/ComponentSpaceCodec.h"
#include "Engine/Codecs/FontCodec.h"

using namespace Frontier::Codecs;
RadiometricSwatchEntry Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
