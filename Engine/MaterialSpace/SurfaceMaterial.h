/*============================================================================================================================================
                                                              SURFACEMATERIAL.H
============================================================================================================================================*/
// 🧩 Universal 10-lobe physical material specification unifying OpenPBR Surface 1.1 and constant-time multiscale glinty microflakes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <cstdint>
#include <string>

namespace Frontier::MaterialSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class MaterialPresetCategory : uint8_t
{
    RoughPorousConcrete          = 0, // [-] - L1: Portsmouth 2025 Energy-Preserving Oren-Nayar rough mineral diffuse
    BrushedGoldAlloy             = 1, // [-] - L2: Anisotropic VNDF GGX + F0/F82 edge tint + Turquin multi-scatter
    ClearcoatedCarbonLacquer     = 2, // [-] - L3: Tinted dielectric clearcoat over anisotropic substrate + Beer-Lambert
    HeatBluedTitaniumIridescent  = 3, // [-] - L4: Belcour & Barla 490 nm thin-film wave-optics Airy interference
    MetallicCarPaintGlinty       = 4, // [-] - L5: Kemppinen 2025 constant-time 4D multiscale glinty microflakes + coat
    AbbeCrownGlassDispersion     = 5, // [-] - L6: Rough microfacet GGX dielectric transmission + Abbe Cauchy dispersion
    SubsurfaceJadeOrganic        = 6, // [-] - L7: Christensen-Burley normalized subsurface diffusion + volumetric walk
    AlcantaraVelvetSheen         = 7, // [-] - L8: Zeltner 2022 Linearly Transformed Cosine multiple-scattering sheen
    KeratinStrandFiber           = 8, // [-] - L9: Chiang/Marschner R + TT + TRT dual-scattering hair & fiber BCSDF
    WetPorousGlowingBrakeRotor   = 9, // [-] - L10: Relative-IOR water darkening + Planckian 1350 K blackbody emission
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DiffuseSubstrateParameters
{
    Mathematics::LinearRGB BaseColor        { 0.78f, 0.78f, 0.78f }; // [-] - Linear Rec.709 diffuse reflectance
    float                  BaseWeight       = 1.0f;                  // [-] - Substrate diffuse weight [0, 1]
    float                  DiffuseRoughness = 0.25f;                 // [-] - Oren-Nayar micro-roughness [0, 1]
};

struct SpecularMetalParameters
{
    float                  Metalness        = 0.0f;                  // [-]   - Conductor fraction [0, 1]
    float                  SpecularWeight   = 1.0f;                  // [-]   - Dielectric F0 multiplier [0, 1]
    float                  Roughness        = 0.30f;                 // [-]   - Isotropic GGX alpha root [0.02, 1]
    float                  Anisotropy       = 0.0f;                  // [-]   - Aspect stretch [-1, 1]
    float                  RotationRadians  = 0.0f;                  // [rad] - Tangent-plane brush angle
    float                  DielectricIOR    = 1.50f;                 // [-]   - Exterior-relative refractive index
    Mathematics::LinearRGB EdgeTintF82      { 1.0f, 1.0f, 1.0f };    // [-]   - OpenPBR 82-degree grazing conductor tint
};

struct ClearcoatLacquerParameters
{
    float                  CoatWeight              = 0.0f;                  // [-]  - Coat presence [0, 1]
    float                  CoatRoughness           = 0.06f;                 // [-]  - Coat micro-roughness [0.01, 1]
    float                  CoatAnisotropy          = 0.0f;                  // [-]  - Coat directional stretch [-1, 1]
    float                  CoatIOR                 = 1.52f;                 // [-]  - Coat refractive index [1.0, 2.5]
    Mathematics::LinearRGB CoatAbsorptionTint      { 1.0f, 1.0f, 1.0f };    // [-]  - Unit-thickness transmittance color
    float                  CoatThicknessMillimeter = 0.05f;                 // [mm] - Physical clearcoat thickness
};

struct ThinFilmWaveParameters
{
    float ThinFilmWeight              = 0.0f;   // [-]  - Iridescence blend weight [0, 1]
    float ThinFilmThicknessNanometers = 420.0f; // [nm] - Nanometer optical film thickness [0, 2000]
    float ThinFilmIOR                 = 1.90f;  // [-]  - Thin-film refractive index [1.0, 3.0]
};

struct GlintMicroflakeParameters
{
    float                  FlakeCoverage            = 0.0f;                  // [-]        - Surface area flake coverage [0, 1]
    float                  FlakeIntrinsicRoughness  = 0.045f;                // [-]        - Single-flake mirror width [0.005, 0.5]
    float                  FlakeOrientationSpread   = 0.16f;                 // [-]        - Macroscopic normal standard deviation
    float                  FlakeLogDensityPerSqM    = 7.0f;                  // [log10/m2] - Log10 flake count per m^2 [3.0, 9.5]
    Mathematics::LinearRGB FlakePrimaryTint         { 0.92f, 0.93f, 0.96f }; // [-]        - Direct metallic flake reflectance
    Mathematics::LinearRGB FlakeHorizonTint         { 0.45f, 0.62f, 0.92f }; // [-]        - Pearlescent grazing ramp shift
    float                  FlakeBurialDepth         = 0.35f;                 // [-]        - Normalized pigment burial depth [0, 1]
};

struct DielectricTransmissionParameters
{
    float                  TransmissionWeight       = 0.0f;                  // [-]  - Dielectric refractive fraction [0, 1]
    float                  AbbeDispersionNumber     = 55.0f;                 // [-]  - Fraunhofer V_d dispersion (lower = stronger rainbow)
    float                  VolumeAnisotropyG        = 0.0f;                  // [-]  - Henyey-Greenstein phase g [-0.95, 0.95]
    bool                   ThinWalledSlab           = false;                 // [-]  - True for panes/windshields without ray refraction offset
    Mathematics::LinearRGB AttenuationColor         { 0.94f, 0.98f, 0.96f }; // [-]  - Transmittance color at AttenuationDistanceMeters
    float                  AttenuationDistanceMeter = 0.25f;                 // [m]  - Beer-Lambert reference path length
};

struct SubsurfaceDiffusionParameters
{
    float                  SubsurfaceWeight          = 0.0f;                  // [-]  - Subsurface scattering blend [0, 1]
    Mathematics::LinearRGB SubsurfaceColor           { 0.85f, 0.48f, 0.36f }; // [-]  - Multiple-scatter equilibrium albedo
    Mathematics::Axis3D    MeanFreePathMillimeters   { 3.6f, 1.4f, 0.7f };    // [mm] - Per-channel diffusion radius d
    float                  PhaseAnisotropyG          = 0.0f;                  // [-]  - Forward/backward volumetric scatter [-0.9, 0.9]
};

struct ClothSheenParameters
{
    float                  SheenWeight    = 0.0f;                  // [-] - Grazing microfiber sheen weight [0, 1]
    float                  SheenRoughness = 0.45f;                 // [-] - Fiber distribution width [0.05, 1]
    Mathematics::LinearRGB SheenColor     { 0.92f, 0.92f, 0.95f }; // [-] - Grazing sheen reflectance tint
};

struct FiberStrandParameters
{
    float FiberWeight                = 0.0f;   // [-]   - Dual-scattering fiber BCSDF weight [0, 1]
    float LongitudinalRoughness      = 0.22f;  // [-]   - Beta_M longitudinal highlight width [0.02, 1]
    float AzimuthalRoughness         = 0.28f;  // [-]   - Beta_N azimuthal cross-section width [0.02, 1]
    float CuticleTiltRadians         = 0.035f; // [rad] - Cuticle scale tilt alpha (~2 deg)
    float MelaninConcentration       = 0.45f;  // [-]   - Total pigment concentration [0, 1]
    float PheomelaninRednessFraction = 0.20f;  // [-]   - Red/gold pheomelanin vs brown/black eumelanin [0, 1]
};

struct PorosityEmissionParameters
{
    float                  PorosityFraction           = 0.0f;                  // [-]     - Open micro-pore volume fraction [0, 1]
    float                  SurfaceWetnessFraction     = 0.0f;                  // [-]     - Adsorbed water saturation [0, 1]
    float                  EmissionLuminanceNits      = 0.0f;                  // [cd/m2] - Explicit non-thermal luminance
    float                  BlackbodyTemperatureKelvin = 0.0f;                  // [K]     - Thermal radiator temperature (0 = inactive)
    Mathematics::LinearRGB EmissionTint               { 1.0f, 1.0f, 1.0f };    // [-]     - Spectral filter over emitted radiance
};

struct BindlessChannelBindings
{
    uint16_t BaseColorChannelOrdinal = 0xFFFFu; // [idx] - Optional bindless albedo .TextureSpace slot
    uint16_t NormalChannelOrdinal    = 0xFFFFu; // [idx] - Optional bindless tangent normal .TextureSpace slot
    uint16_t RoughnessChannelOrdinal = 0xFFFFu; // [idx] - Optional bindless roughness/metal .TextureSpace slot
    uint16_t AuxiliaryChannelOrdinal = 0xFFFFu; // [idx] - Optional bindless coat/thickness .TextureSpace slot
};

struct SurfaceMaterial
{
    std::string                      MaterialName  { "StandardOpenPbrSurface" }; // [-] - Human-readable material identifier
    DiffuseSubstrateParameters       Diffuse       {};                           // [-] - L1: Rough & porous diffuse substrate
    SpecularMetalParameters          Specular      {};                           // [-] - L2: Anisotropic GGX & F0/F82 conductor
    ClearcoatLacquerParameters       Clearcoat     {};                           // [-] - L3: Clearcoat, TIR & Beer-Lambert coat
    ThinFilmWaveParameters           ThinFilm      {};                           // [-] - L4: Nanometer Airy thin-film interference
    GlintMicroflakeParameters        GlintFlakes   {};                           // [-] - L5: Constant-time 4D multiscale microflakes
    DielectricTransmissionParameters Transmission  {};                           // [-] - L6: Thin/solid rough GGX refraction & Abbe
    SubsurfaceDiffusionParameters    Subsurface    {};                           // [-] - L7: Burley diffusion & volumetric walk
    ClothSheenParameters             Sheen         {};                           // [-] - L8: Zeltner LTC multiple-scatter sheen
    FiberStrandParameters            Fiber         {};                           // [-] - L9: Chiang/Marschner R+TT+TRT fiber BCSDF
    PorosityEmissionParameters    PorosityLight {};                           // [-] - L10: Relative-IOR wetness & Planckian light
    BindlessChannelBindings          Channels      {};                           // [-] - Optional bindless texture ordinals
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Clamp all authored parameters into physically realizable domains (non-negative albedos <= 1, IOR >= 1, finite roughness)
void SanitizeSurfaceMaterial(SurfaceMaterial& InOutMaterial) noexcept;

// 📝 Verify strict physical admissibility of all 10 optical parameter blocks
[[nodiscard]] bool VerifySurfaceMaterialPhysicalBounds(const SurfaceMaterial& Material) noexcept;

// 📝 Construct any of the 10 canonical physical reference materials (one exercising each optical family L1..L10)
[[nodiscard]] SurfaceMaterial ConstructMaterialPreset(MaterialPresetCategory Preset);

} // namespace Frontier::MaterialSpace
