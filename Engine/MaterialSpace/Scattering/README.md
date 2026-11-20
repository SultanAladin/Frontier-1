# 🧩 `Engine/MaterialSpace/Scattering/`

> **TL;DR**: Lobe 10B: Planckian spectral blackbody thermal emission radiance integrated into linear Rec.709 output (12 source/header/shader files under `Engine/MaterialSpace/Scattering/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BlackbodyEmission.h` | Lobe 10B: Planckian spectral blackbody thermal emission radiance integrated into linear Rec.709 output |
| `ClearcoatAbsorption.h` | Lobe 3: Dielectric clearcoat reflection, OpenPBR underlying roughness widening, TIR darkening, & Beer-Lambert medium absorption |
| `DielectricTransmission.h` | Lobe 6: Walter Rough Microfacet GGX BTDF, OpenPBR ThinWalledSlab glazing, Beer-Lambert attenuation, & Abbe Cauchy dispersion |
| `EnergyPreservingOrenNayar.h` | Lobe 1: Portsmouth, Kutz & Hill (2025) / OpenPBR 1.1 Energy-Preserving Oren-Nayar (EON) rough porous diffuse BRDF & sampler |
| `FiberScattering.h` | Lobe 9: Chiang et al. (2016) / Marschner Dual-Scattering Hair & Technical Fiber BCSDF (R + TT + TRT lobes + melanin absorption) |
| `GlintMicroflakeScattering.h` | Lobe 5: Kemppinen et al. (SIGGRAPH Asia 2025) Constant-Time 4D Multiscale Glinty Microflake NDFs (O(1) across 10^3..10^9 flakes/m^2) |
| `LinearlyTransformedSheen.h` | Lobe 8: Zeltner, Burley & Chiang (SIGGRAPH 2022) Linearly Transformed Cosine (LTC) multiple-scattering cloth & velvet sheen |
| `MetallicFresnelTint.h` | Lobe 2: Anisotropic Spherical-Cap VNDF GGX, exact dielectric Fresnel, OpenPBR F0/F82 edge-tint metal, & Turquin multi-scattering |
| `OpenPbrSurfaceScattering.h` | Master 10-Lobe OpenPBR 1.1 + Constant-Time Glinty BSDF evaluation, VNDF/Cosine mixture PDF, & White-Furnace hemisphere integrator |
| `PorosityScattering.h` | Lobe 10A: Angstrom/Lekner relative-IOR porous wetness darkening and adsorbed liquid water film Fresnel specular |
| `SubsurfaceDiffusion.h` | Lobe 7: Christensen-Burley (2015) Normalized Diffusion BSSRDF radial profile, thin translucency, & Owen-Sobol volumetric walk |
| `ThinFilmInterference.h` | Lobe 4: Belcour & Barla (SIGGRAPH 2017) nanometer Airy multi-beam wave-optics thin-film interference (spectral & RGB modes) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::MaterialSpace::Scattering`
- **Primary Types**: `ClearcoatInteractionResult`, `TextureFootprintJacobian`, `SheenInteractionResult`, `AnisotropicAlphaPair`, `SurfaceShadingBasis`, `BsdfDirectionSample`, `WhiteFurnaceAuditReport`, `PorosityWetnessAdjustment`

## Usage

```cpp
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"
#include "Engine/MaterialSpace/Scattering/ClearcoatAbsorption.h"
#include "Engine/MaterialSpace/Scattering/DielectricTransmission.h"

using namespace Frontier::MaterialSpace::Scattering;
ClearcoatInteractionResult Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
