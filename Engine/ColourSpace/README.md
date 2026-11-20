# 🧩 `Engine/ColourSpace/`

> **TL;DR**: Colorimetric transforms between CIE XYZ, Linear Rec.709/sRGB, ACEScg, Rec.2020, Display P3, Oklab/Oklch, HSL, CMYK & Hex strings (22 source/header/shader files under `Engine/ColourSpace/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ChromaticitySpace.cpp` | Implementation of CIE XYZ, Linear sRGB/Rec.709, ACEScg, Rec.2020, Display P3, Oklab/Oklch, HSL, CMYK & Hex conversions |
| `ChromaticitySpace.h` | Colorimetric transforms between CIE XYZ, Linear Rec.709/sRGB, ACEScg, Rec.2020, Display P3, Oklab/Oklch, HSL, CMYK & Hex strings |
| `ColourAliasRepository.cpp` | Implementation of Frontier::ColourSpace::ColourAliasRepository — unified named alias resolution across solid pigments, ramps, blackbody & all colour spaces |
| `ColourAliasRepository.h` | Engine-wide ColourSpace alias codex resolving named colours, gradients, blackbody illuminants & modular ColourSourceProfiles into any colour space |
| `ColourAssetRegister.cpp` | Implementation of ColourAssetRegister lifecycle participant, contiguous GPU LUT cache & WorkspaceExchange live publication |
| `ColourAssetRegister.h` | Designated ColourSpace lifecycle participant managing engine-wide ramp/swatch repository, GPU LUT sheet & WorkspaceExchange publication |
| `ColourDocument.cpp` | Implementation of ColourDocument ramp/swatch indexing and deterministic LUT baking |
| `ColourDocument.h` | Authoritative in-memory .ColourSpace document holding primaries, white point, swatches, modular colour ramps, spectra & baked LUTs |
| `ColourPresetRepository.cpp` | Implementation of built-in Studio/Scientific/Utility ramp presets & runtime ColourPresetRepository |
| `ColourPresetRepository.h` | Engine-wide built-in & project colour ramp presets (all 6 HTML Studio ramps verbatim + scientific/utility sets) & named swatch entries |
| `ColourRampSampler.cpp` | Implementation of modular colour source sampling, 3D lattice noise (HTML bit-parity), perceptual Oklab blending & 64/256-entry LUTs |
| `ColourRampSampler.h` | Arbitrary-stop colour ramp sampling (Linear/Smooth/Stepped, LinearSrgb/Oklab/ACEScg), 3D lattice noise & 64/256-entry GPU LUT packing |
| `ColourSourceTypes.h` | Modular reusable colour sources (SolidPigment, LinearFill, MultiStopGradient, QuantizedRamp, BlackbodyThermal, DiscreteTintSet, SpatialNoiseFill) |
| `ColourTypes.h` | First-class radiometric & perceptual colour types shared across every Frontier workspace (LinearRgba, SrgbRgba8, Oklab, HSL, CMYK, LUTs) |
| `GamutCompression.cpp` | Implementation of smooth hyperbolic gamut compression & perceptual Oklab chroma preservation |
| `GamutCompression.h` | ACES-inspired smooth distance gamut compression & perceptual Oklab chroma clipping for HDR / wide-gamut linear tristimulus colours |
| `SpectralDistribution.cpp` | Implementation of Wyman analytic CIE 1931 2-degree observer curves, spectral integration & Planckian Kelvin radiance |
| `SpectralDistribution.h` | Wavelength-domain spectral power distribution, CIE 1931 2-degree tristimulus integration & Planckian blackbody Kelvin radiance |
| `TransferFunction.cpp` | Implementation of IEC sRGB, Gamma 2.2, ST 2084 PQ, BT.2100 HLG transfer curves & WCAG 2.1 contrast luminance |
| `TransferFunction.h` | Electro-optical & opto-electronic transfer curves (Piecewise sRGB, Pure Gamma 2.2, ST 2084 PQ, HLG) & WCAG 2.1 contrast luminance |
| `WhitePointAdaptation.cpp` | Implementation of Bradford & CAT02 chromatic adaptation transforms (D65, D60, D50) |
| `WhitePointAdaptation.h` | Bradford & CAT02 chromatic adaptation transforms between CIE standard illuminants (D65, D60, D50) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::ColourSpace`
- **Primary Types**: `ColourAliasEntry`, `ColourAliasRepository`, `ColourAssetRegisterMetrics`, `ColourAssetRegister`, `AuthoredColourRamp`, `ColourDocument`, `ColourRampPresetEntry`, `NamedColourSwatchEntry`, `ColourPresetRepository`, `SpatialColourSampleInput`, `ColourSourceCategory`, `ColourWrapBehaviour`, `ColourBlendCurve`, `ColourInterpolationSpace`, `ColourStop`, `SolidPigmentSource`, `LinearFillSource`, `MultiStopGradientSource` (+20 more)

## Usage

```cpp
#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourAliasRepository.h"
#include "Engine/ColourSpace/ColourAssetRegister.h"

using namespace Frontier::ColourSpace;
ColourAliasEntry Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
