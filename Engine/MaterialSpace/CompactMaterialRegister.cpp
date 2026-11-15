/*============================================================================================================================================
                                                         COMPACTMATERIALREGISTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of IEEE-754 FP16 packing, 128-byte CompactMaterialEntry compilation, and wave-uniform lobe queries

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/CompactMaterialRegister.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Frontier::MaterialSpace
{

namespace
{

[[nodiscard]] uint8_t PackUnorm8(float Value) noexcept
{
    const float Clamped = std::clamp(Value, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(Clamped * 255.0f));
}

[[nodiscard]] float UnpackUnorm8(uint8_t ByteBits) noexcept
{
    return static_cast<float>(ByteBits) * (1.0f / 255.0f);
}

[[nodiscard]] uint32_t PackUnorm4x8(float A, float B, float C, float D) noexcept
{
    return static_cast<uint32_t>(PackUnorm8(A)) |
           (static_cast<uint32_t>(PackUnorm8(B)) << 8u) |
           (static_cast<uint32_t>(PackUnorm8(C)) << 16u) |
           (static_cast<uint32_t>(PackUnorm8(D)) << 24u);
}

void UnpackUnorm4x8(uint32_t Word, float& OutA, float& OutB, float& OutC, float& OutD) noexcept
{
    OutA = UnpackUnorm8(static_cast<uint8_t>(Word & 0xFFu));
    OutB = UnpackUnorm8(static_cast<uint8_t>((Word >> 8u) & 0xFFu));
    OutC = UnpackUnorm8(static_cast<uint8_t>((Word >> 16u) & 0xFFu));
    OutD = UnpackUnorm8(static_cast<uint8_t>((Word >> 24u) & 0xFFu));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              IEEE-754 FP16 PACKING UTILITIES
//------------------------------------------------------------------------------------------------------------------------

uint16_t ConvertFloat32ToHalf16(float Value) noexcept
{
    if (!std::isfinite(Value))
    {
        return 0u;
    }
    const uint32_t Bits     = std::bit_cast<uint32_t>(Value);
    const uint32_t Sign     = (Bits >> 16u) & 0x8000u;
    const int32_t  Exponent = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    uint32_t       Mantissa = Bits & 0x007FFFFFu;

    if (Exponent <= 0)
    {
        if (Exponent < -10)
        {
            return static_cast<uint16_t>(Sign);
        }
        Mantissa = (Mantissa | 0x00800000u) >> static_cast<uint32_t>(1 - Exponent);
        return static_cast<uint16_t>(Sign | ((Mantissa + 0x00001000u) >> 13u));
    }
    if (Exponent >= 31)
    {
        return static_cast<uint16_t>(Sign | 0x7BFFu);
    }
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exponent) << 10u) | ((Mantissa + 0x00001000u) >> 13u));
}

float ConvertHalf16ToFloat32(uint16_t HalfBits) noexcept
{
    const uint32_t Sign     = (static_cast<uint32_t>(HalfBits) & 0x8000u) << 16u;
    const uint32_t Exponent = (static_cast<uint32_t>(HalfBits) >> 10u) & 0x1Fu;
    uint32_t       Mantissa = static_cast<uint32_t>(HalfBits) & 0x03FFu;

    if (Exponent == 0u)
    {
        if (Mantissa == 0u)
        {
            return std::bit_cast<float>(Sign);
        }
        int32_t SubExp = -14;
        while ((Mantissa & 0x0400u) == 0u)
        {
            Mantissa <<= 1u;
            --SubExp;
        }
        Mantissa &= 0x03FFu;
        const uint32_t FloatBits = Sign | (static_cast<uint32_t>(SubExp + 127) << 23u) | (Mantissa << 13u);
        return std::bit_cast<float>(FloatBits);
    }
    if (Exponent == 31u)
    {
        const uint32_t FloatBits = Sign | 0x7F800000u | (Mantissa << 13u);
        return std::bit_cast<float>(FloatBits);
    }
    const uint32_t FloatBits = Sign | ((Exponent + 112u) << 23u) | (Mantissa << 13u);
    return std::bit_cast<float>(FloatBits);
}

uint32_t PackHalf2x16(float LowValue, float HighValue) noexcept
{
    const uint32_t Lo = ConvertFloat32ToHalf16(LowValue);
    const uint32_t Hi = ConvertFloat32ToHalf16(HighValue);
    return Lo | (Hi << 16u);
}

void UnpackHalf2x16(uint32_t PackedWord, float& OutLowValue, float& OutHighValue) noexcept
{
    OutLowValue  = ConvertHalf16ToFloat32(static_cast<uint16_t>(PackedWord & 0xFFFFu));
    OutHighValue = ConvertHalf16ToFloat32(static_cast<uint16_t>((PackedWord >> 16u) & 0xFFFFu));
}

//------------------------------------------------------------------------------------------------------------------------
//                                        COMPACT REGISTER COMPILER & UNPACKER
//------------------------------------------------------------------------------------------------------------------------

uint32_t DetermineActiveLobeMask(const SurfaceMaterial& Material) noexcept
{
    uint32_t Mask = LobeMaskNone;

    const float DielectricSubstrate = (1.0f - Material.Specular.Metalness) * (1.0f - Material.Transmission.TransmissionWeight);
    if (Material.Diffuse.BaseWeight > 1.0e-4f && DielectricSubstrate * (1.0f - Material.Subsurface.SubsurfaceWeight) > 1.0e-4f &&
        Material.Fiber.FiberWeight < 0.999f)
    {
        Mask |= LobeDiffuseEON;
    }
    if ((Material.Specular.SpecularWeight > 1.0e-4f || Material.Specular.Metalness > 1.0e-4f) &&
        Material.Fiber.FiberWeight < 0.999f)
    {
        Mask |= LobeSpecularMetalGGX;
    }
    if (Material.Clearcoat.CoatWeight > 1.0e-4f)
    {
        Mask |= LobeClearcoat;
    }
    if (Material.ThinFilm.ThinFilmWeight > 1.0e-4f && Material.ThinFilm.ThinFilmThicknessNanometers > 1.0f)
    {
        Mask |= LobeThinFilmAiry;
    }
    if (Material.GlintFlakes.FlakeCoverage > 1.0e-4f)
    {
        Mask |= LobeGlintMicroflakes;
    }
    if (Material.Transmission.TransmissionWeight > 1.0e-4f && Material.Specular.Metalness < 0.999f)
    {
        Mask |= LobeDielectricTransmission;
    }
    if (Material.Subsurface.SubsurfaceWeight > 1.0e-4f && DielectricSubstrate > 1.0e-4f)
    {
        Mask |= LobeSubsurfaceDiffusion;
    }
    if (Material.Sheen.SheenWeight > 1.0e-4f)
    {
        Mask |= LobeClothSheenLTC;
    }
    if (Material.Fiber.FiberWeight > 1.0e-4f)
    {
        Mask |= LobeFiberBCSDF;
    }
    if (Material.PorosityLight.PorosityFraction > 1.0e-4f && Material.PorosityLight.SurfaceWetnessFraction > 1.0e-4f)
    {
        Mask |= LobePorosityWetness;
    }
    if (Material.PorosityLight.EmissionLuminanceNits > 1.0e-3f || Material.PorosityLight.BlackbodyTemperatureKelvin >= 500.0f)
    {
        Mask |= LobeBlackbodyEmission;
    }
    if (Material.Transmission.ThinWalledSlab)
    {
        Mask |= FlagThinWalledSlab;
    }
    if (std::fabs(Material.Specular.Anisotropy) > 1.0e-3f || std::fabs(Material.Clearcoat.CoatAnisotropy) > 1.0e-3f)
    {
        Mask |= FlagAnisotropicFrame;
    }
    return Mask;
}

CompactMaterialEntry CompileCompactMaterialEntry(const SurfaceMaterial& RawMaterial) noexcept
{
    SurfaceMaterial Mat = RawMaterial;
    SanitizeSurfaceMaterial(Mat);

    CompactMaterialEntry Out {};
    Out.ActiveLobeMask              = DetermineActiveLobeMask(Mat);
    Out.BaseColorRgHalf             = PackHalf2x16(Mat.Diffuse.BaseColor.R, Mat.Diffuse.BaseColor.G);
    Out.BaseColorBAndWeightHalf     = PackHalf2x16(Mat.Diffuse.BaseColor.B, Mat.Diffuse.BaseWeight);
    Out.DiffuseSpecRoughnessHalf = PackHalf2x16(Mat.Diffuse.DiffuseRoughness, Mat.Specular.Roughness);

    Out.MetalnessSpecWeightHalf    = PackHalf2x16(Mat.Specular.Metalness, Mat.Specular.SpecularWeight);
    Out.AnisotropyRotationHalf     = PackHalf2x16(Mat.Specular.Anisotropy, Mat.Specular.RotationRadians);
    Out.DielectricIorEdgeTintRHalf = PackHalf2x16(Mat.Specular.DielectricIOR, Mat.Specular.EdgeTintF82.R);
    Out.EdgeTintGbHalf                = PackHalf2x16(Mat.Specular.EdgeTintF82.G, Mat.Specular.EdgeTintF82.B);

    Out.CoatWeightRoughnessHalf = PackHalf2x16(Mat.Clearcoat.CoatWeight, Mat.Clearcoat.CoatRoughness);
    Out.CoatAnisoIorHalf        = PackHalf2x16(Mat.Clearcoat.CoatAnisotropy, Mat.Clearcoat.CoatIOR);
    Out.CoatTintRgHalf             = PackHalf2x16(Mat.Clearcoat.CoatAbsorptionTint.R, Mat.Clearcoat.CoatAbsorptionTint.G);
    Out.CoatTintBAndThicknessHalf  = PackHalf2x16(Mat.Clearcoat.CoatAbsorptionTint.B, Mat.Clearcoat.CoatThicknessMillimeter);

    Out.ThinFilmThicknessNanometers  = Mat.ThinFilm.ThinFilmThicknessNanometers;
    Out.ThinFilmWeightIorHalf     = PackHalf2x16(Mat.ThinFilm.ThinFilmWeight, Mat.ThinFilm.ThinFilmIOR);
    Out.FlakeCoverageRoughHalf         = PackHalf2x16(Mat.GlintFlakes.FlakeCoverage, Mat.GlintFlakes.FlakeIntrinsicRoughness);
    Out.FlakeSpreadLogDensityHalf = PackHalf2x16(Mat.GlintFlakes.FlakeOrientationSpread, Mat.GlintFlakes.FlakeLogDensityPerSqM);

    Out.FlakePrimaryRgHalf              = PackHalf2x16(Mat.GlintFlakes.FlakePrimaryTint.R, Mat.GlintFlakes.FlakePrimaryTint.G);
    Out.FlakePrimaryBAndBurialHalf      = PackHalf2x16(Mat.GlintFlakes.FlakePrimaryTint.B, Mat.GlintFlakes.FlakeBurialDepth);
    Out.FlakeHorizonRgHalf              = PackHalf2x16(Mat.GlintFlakes.FlakeHorizonTint.R, Mat.GlintFlakes.FlakeHorizonTint.G);
    Out.FlakeHorizonBAndTransWeightHalf = PackHalf2x16(Mat.GlintFlakes.FlakeHorizonTint.B, Mat.Transmission.TransmissionWeight);

    Out.AbbeVolumeAnisoHalf       = PackHalf2x16(Mat.Transmission.AbbeDispersionNumber, Mat.Transmission.VolumeAnisotropyG);
    Out.AttenuationRgHalf            = PackHalf2x16(Mat.Transmission.AttenuationColor.R, Mat.Transmission.AttenuationColor.G);
    Out.AttenuationBAndDistHalf      = PackHalf2x16(Mat.Transmission.AttenuationColor.B, Mat.Transmission.AttenuationDistanceMeter);
    Out.SubsurfaceWeightAnisoHalf = PackHalf2x16(Mat.Subsurface.SubsurfaceWeight, Mat.Subsurface.PhaseAnisotropyG);

    Out.SubsurfaceColorRgHalf       = PackHalf2x16(Mat.Subsurface.SubsurfaceColor.R, Mat.Subsurface.SubsurfaceColor.G);
    Out.SubsurfaceColorBAndMfpXHalf = PackHalf2x16(Mat.Subsurface.SubsurfaceColor.B, Mat.Subsurface.MeanFreePathMillimeters.X);
    Out.SubsurfaceMfpYzHalf         = PackHalf2x16(Mat.Subsurface.MeanFreePathMillimeters.Y, Mat.Subsurface.MeanFreePathMillimeters.Z);
    Out.SheenWeightRoughRgHalf   = PackUnorm4x8(
        Mat.Sheen.SheenWeight,
        Mat.Sheen.SheenRoughness,
        Mat.Sheen.SheenColor.R,
        Mat.Sheen.SheenColor.G);

    Out.SheenBAndFiberParamsPacked = PackUnorm4x8(
        Mat.Sheen.SheenColor.B,
        Mat.Fiber.FiberWeight,
        Mat.Fiber.LongitudinalRoughness,
        Mat.Fiber.AzimuthalRoughness);

    const float NormalizedTilt = std::clamp((Mat.Fiber.CuticleTiltRadians + 0.35f) / 0.70f, 0.0f, 1.0f);
    Out.FiberPigmentPorosityPacked = PackUnorm4x8(
        NormalizedTilt,
        Mat.Fiber.MelaninConcentration,
        Mat.Fiber.PheomelaninRednessFraction,
        Mat.PorosityLight.SurfaceWetnessFraction);

    Out.PorosityKelvinHalf = PackHalf2x16(
        Mat.PorosityLight.PorosityFraction,
        Mat.PorosityLight.BlackbodyTemperatureKelvin);

    const uint32_t NitsHalf = ConvertFloat32ToHalf16(Mat.PorosityLight.EmissionLuminanceNits);
    Out.EmissionNitsChannelMask = NitsHalf | (static_cast<uint32_t>(Mat.Channels.BaseColorChannelOrdinal) << 16u);

    return Out;
}

SurfaceMaterial UnpackCompactMaterialEntry(const CompactMaterialEntry& Entry)
{
    SurfaceMaterial Mat {};
    Mat.MaterialName = "UnpackedCompactMaterial";

    UnpackHalf2x16(Entry.BaseColorRgHalf, Mat.Diffuse.BaseColor.R, Mat.Diffuse.BaseColor.G);
    UnpackHalf2x16(Entry.BaseColorBAndWeightHalf, Mat.Diffuse.BaseColor.B, Mat.Diffuse.BaseWeight);
    UnpackHalf2x16(Entry.DiffuseSpecRoughnessHalf, Mat.Diffuse.DiffuseRoughness, Mat.Specular.Roughness);

    UnpackHalf2x16(Entry.MetalnessSpecWeightHalf, Mat.Specular.Metalness, Mat.Specular.SpecularWeight);
    UnpackHalf2x16(Entry.AnisotropyRotationHalf, Mat.Specular.Anisotropy, Mat.Specular.RotationRadians);
    UnpackHalf2x16(Entry.DielectricIorEdgeTintRHalf, Mat.Specular.DielectricIOR, Mat.Specular.EdgeTintF82.R);
    UnpackHalf2x16(Entry.EdgeTintGbHalf, Mat.Specular.EdgeTintF82.G, Mat.Specular.EdgeTintF82.B);

    UnpackHalf2x16(Entry.CoatWeightRoughnessHalf, Mat.Clearcoat.CoatWeight, Mat.Clearcoat.CoatRoughness);
    UnpackHalf2x16(Entry.CoatAnisoIorHalf, Mat.Clearcoat.CoatAnisotropy, Mat.Clearcoat.CoatIOR);
    UnpackHalf2x16(Entry.CoatTintRgHalf, Mat.Clearcoat.CoatAbsorptionTint.R, Mat.Clearcoat.CoatAbsorptionTint.G);
    UnpackHalf2x16(Entry.CoatTintBAndThicknessHalf, Mat.Clearcoat.CoatAbsorptionTint.B, Mat.Clearcoat.CoatThicknessMillimeter);

    Mat.ThinFilm.ThinFilmThicknessNanometers = Entry.ThinFilmThicknessNanometers;
    UnpackHalf2x16(Entry.ThinFilmWeightIorHalf, Mat.ThinFilm.ThinFilmWeight, Mat.ThinFilm.ThinFilmIOR);
    UnpackHalf2x16(Entry.FlakeCoverageRoughHalf, Mat.GlintFlakes.FlakeCoverage, Mat.GlintFlakes.FlakeIntrinsicRoughness);
    UnpackHalf2x16(Entry.FlakeSpreadLogDensityHalf, Mat.GlintFlakes.FlakeOrientationSpread, Mat.GlintFlakes.FlakeLogDensityPerSqM);

    UnpackHalf2x16(Entry.FlakePrimaryRgHalf, Mat.GlintFlakes.FlakePrimaryTint.R, Mat.GlintFlakes.FlakePrimaryTint.G);
    UnpackHalf2x16(Entry.FlakePrimaryBAndBurialHalf, Mat.GlintFlakes.FlakePrimaryTint.B, Mat.GlintFlakes.FlakeBurialDepth);
    UnpackHalf2x16(Entry.FlakeHorizonRgHalf, Mat.GlintFlakes.FlakeHorizonTint.R, Mat.GlintFlakes.FlakeHorizonTint.G);
    UnpackHalf2x16(Entry.FlakeHorizonBAndTransWeightHalf, Mat.GlintFlakes.FlakeHorizonTint.B, Mat.Transmission.TransmissionWeight);
    Mat.Transmission.ThinWalledSlab = (Entry.ActiveLobeMask & FlagThinWalledSlab) != 0u;

    UnpackHalf2x16(Entry.AbbeVolumeAnisoHalf, Mat.Transmission.AbbeDispersionNumber, Mat.Transmission.VolumeAnisotropyG);
    UnpackHalf2x16(Entry.AttenuationRgHalf, Mat.Transmission.AttenuationColor.R, Mat.Transmission.AttenuationColor.G);
    UnpackHalf2x16(Entry.AttenuationBAndDistHalf, Mat.Transmission.AttenuationColor.B, Mat.Transmission.AttenuationDistanceMeter);
    UnpackHalf2x16(Entry.SubsurfaceWeightAnisoHalf, Mat.Subsurface.SubsurfaceWeight, Mat.Subsurface.PhaseAnisotropyG);

    UnpackHalf2x16(Entry.SubsurfaceColorRgHalf, Mat.Subsurface.SubsurfaceColor.R, Mat.Subsurface.SubsurfaceColor.G);
    UnpackHalf2x16(Entry.SubsurfaceColorBAndMfpXHalf, Mat.Subsurface.SubsurfaceColor.B, Mat.Subsurface.MeanFreePathMillimeters.X);
    UnpackHalf2x16(Entry.SubsurfaceMfpYzHalf, Mat.Subsurface.MeanFreePathMillimeters.Y, Mat.Subsurface.MeanFreePathMillimeters.Z);

    UnpackUnorm4x8(
        Entry.SheenWeightRoughRgHalf,
        Mat.Sheen.SheenWeight,
        Mat.Sheen.SheenRoughness,
        Mat.Sheen.SheenColor.R,
        Mat.Sheen.SheenColor.G);

    UnpackUnorm4x8(
        Entry.SheenBAndFiberParamsPacked,
        Mat.Sheen.SheenColor.B,
        Mat.Fiber.FiberWeight,
        Mat.Fiber.LongitudinalRoughness,
        Mat.Fiber.AzimuthalRoughness);

    float NormalizedTilt = 0.5f;
    UnpackUnorm4x8(
        Entry.FiberPigmentPorosityPacked,
        NormalizedTilt,
        Mat.Fiber.MelaninConcentration,
        Mat.Fiber.PheomelaninRednessFraction,
        Mat.PorosityLight.SurfaceWetnessFraction);
    Mat.Fiber.CuticleTiltRadians = NormalizedTilt * 0.70f - 0.35f;

    UnpackHalf2x16(
        Entry.PorosityKelvinHalf,
        Mat.PorosityLight.PorosityFraction,
        Mat.PorosityLight.BlackbodyTemperatureKelvin);

    Mat.PorosityLight.EmissionLuminanceNits = ConvertHalf16ToFloat32(static_cast<uint16_t>(Entry.EmissionNitsChannelMask & 0xFFFFu));
    Mat.Channels.BaseColorChannelOrdinal    = static_cast<uint16_t>((Entry.EmissionNitsChannelMask >> 16u) & 0xFFFFu);

    SanitizeSurfaceMaterial(Mat);
    return Mat;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           COMPACT MATERIAL REGISTER TABLE
//------------------------------------------------------------------------------------------------------------------------

CompactMaterialRegister::CompactMaterialRegister()
{
    ClearRegister();
}

void CompactMaterialRegister::ClearRegister()
{
    CompactEntries.clear();
    AuthoredEntries.clear();

    SurfaceMaterial DefaultClay {};
    DefaultClay.MaterialName = "DefaultStudioClay";
    SanitizeSurfaceMaterial(DefaultClay);
    AuthoredEntries.push_back(DefaultClay);
    CompactEntries.push_back(CompileCompactMaterialEntry(DefaultClay));
}

uint16_t CompactMaterialRegister::RegisterMaterial(const SurfaceMaterial& Material)
{
    if (CompactEntries.size() >= 0xFFFEu)
    {
        return 0u;
    }
    SurfaceMaterial Clean = Material;
    SanitizeSurfaceMaterial(Clean);
    const auto AssignedSlot = static_cast<uint16_t>(CompactEntries.size());
    AuthoredEntries.push_back(Clean);
    CompactEntries.push_back(CompileCompactMaterialEntry(Clean));
    return AssignedSlot;
}

bool CompactMaterialRegister::UpdateMaterialSlot(uint16_t MaterialOrdinal, const SurfaceMaterial& Material)
{
    if (MaterialOrdinal >= CompactEntries.size())
    {
        return false;
    }
    SurfaceMaterial Clean = Material;
    SanitizeSurfaceMaterial(Clean);
    AuthoredEntries[MaterialOrdinal] = Clean;
    CompactEntries[MaterialOrdinal]  = CompileCompactMaterialEntry(Clean);
    return true;
}

uint32_t CompactMaterialRegister::QuerySlotCount() const noexcept
{
    return static_cast<uint32_t>(CompactEntries.size());
}

const CompactMaterialEntry& CompactMaterialRegister::InspectCompactEntry(uint16_t MaterialOrdinal) const noexcept
{
    const std::size_t SafeSlot = (MaterialOrdinal < CompactEntries.size()) ? MaterialOrdinal : 0u;
    return CompactEntries[SafeSlot];
}

const SurfaceMaterial& CompactMaterialRegister::InspectAuthoredMaterial(uint16_t MaterialOrdinal) const noexcept
{
    const std::size_t SafeSlot = (MaterialOrdinal < AuthoredEntries.size()) ? MaterialOrdinal : 0u;
    return AuthoredEntries[SafeSlot];
}

std::span<const CompactMaterialEntry> CompactMaterialRegister::InspectGpuBufferSpan() const noexcept
{
    return CompactEntries;
}

uint32_t CompactMaterialRegister::QueryCombinedWavefrontLobeMask(std::span<const uint16_t> VisibleMaterialSlots) const noexcept
{
    uint32_t CombinedMask = 0u;
    for (const uint16_t Slot : VisibleMaterialSlots)
    {
        const std::size_t SafeSlot = (Slot < CompactEntries.size()) ? Slot : 0u;
        CombinedMask |= CompactEntries[SafeSlot].ActiveLobeMask;
    }
    return CombinedMask;
}

} // namespace Frontier::MaterialSpace
