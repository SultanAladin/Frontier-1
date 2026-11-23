/*============================================================================================================================================
                                                 DETERMINISTICPATHTRACESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Order 3 unified deterministic Sobol' (0,2) Vulkan 1.3 path tracer shared 1:1 by GameRealtime and SceneFinalRender

#include "Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.h"

#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Frontier::GeometricRaster
{

namespace
{

constexpr float    PiConstant                = 3.14159265358979323846f;
constexpr uint32_t TracingUniformByteSize    = 304u; // 19 x float4
constexpr uint32_t TracingSlotStrideBytes    = 512u; // >= max minUniformBufferOffsetAlignment (256 B)
constexpr uint32_t TracingSlotTotal          = 3u;   // 0 = Integrate, 1 = Reproject, 2 = Features
constexpr uint32_t ReconstructSlotStride     = 256u; // 6 slots: Gather + 5 Atrous steps
constexpr uint32_t ReconstructStepTotal      = 6u;
constexpr uint32_t DisplayUniformByteSize    = 32u;  // 2 x float4
constexpr uint32_t RasterUniformByteSize     = 80u;  // 5 x float4
constexpr uint32_t AccumulationRegionTotal   = 4u;   // [0] exact sums, [1..2] ping-pong history, [3] luminance moments
constexpr uint32_t ReconstructionRegionTotal = 4u;   // [0] gather, [1..2] ping-pong atrous, [3] resolved display rgb
constexpr uint32_t ThinSheetPackedBit       = 0x80000000u;
constexpr uint32_t MaterialFloatStride       = 24u;  // 6 x float4 per PathSurfaceMaterial
constexpr uint32_t SphereFloatStride         = 8u;   // 2 x float4 per analytic sphere
constexpr uint32_t AreaLightFloatStride      = 16u;  // 4 x float4 per rectangular softbox
constexpr float    StudioFloorRadiusMeters   = 60.0f;
constexpr float    NightSkyDimFactor         = 0.02f;
constexpr float    DenoiseFullUntilSpp       = 4.0f;
constexpr float    DenoiseFadedAtSpp         = 64.0f;
constexpr std::array<uint32_t, 5> AtrousStepWidths { 1u, 2u, 4u, 8u, 16u };

enum class GpuSurfaceCategory : uint32_t
{
    Layered         = 0u,
    Metallic       = 1u,
    SolidDielectric = 2u,
    ThinDielectric  = 3u,
};

struct CpuMaterialDefinition
{
    GpuSurfaceCategory   Category      = GpuSurfaceCategory::Layered;
    std::array<float, 3> Albedo        { 0.5f, 0.5f, 0.5f };
    float                Roughness     = 0.5f;
    float                CoatWeight    = 0.0f;
    float                CoatRoughness = 0.0f;
    std::array<float, 3> Exitance      { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> Eta           { 1.0f, 1.0f, 1.0f };
    std::array<float, 3> Kappa         { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> Absorption    { 0.0f, 0.0f, 0.0f };
    float                Ior           = 1.5f;
    float                SpecularF0    = 0.04f;
};

[[nodiscard]] uint32_t BitReverse32(uint32_t X) noexcept
{
    X = ((X >> 1u) & 0x55555555u) | ((X & 0x55555555u) << 1u);
    X = ((X >> 2u) & 0x33333333u) | ((X & 0x33333333u) << 2u);
    X = ((X >> 4u) & 0x0f0f0f0fu) | ((X & 0x0f0f0f0fu) << 4u);
    X = ((X >> 8u) & 0x00ff00ffu) | ((X & 0x00ff00ffu) << 8u);
    return (X >> 16u) | (X << 16u);
}

[[nodiscard]] uint32_t HostAvalancheKey(uint32_t SeedInput) noexcept
{
    uint32_t Mixed = SeedInput;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7feb352du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846ca68bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

[[nodiscard]] uint32_t HostLaineKarrasPermutation(uint32_t SeedInput, const uint32_t ScrambleKey) noexcept
{
    uint32_t Permuted = SeedInput + ScrambleKey;
    Permuted ^= Permuted * 0x6c50b47cu;
    Permuted ^= Permuted * 0xb82f1e52u;
    Permuted ^= Permuted * 0xc7afe638u;
    Permuted ^= Permuted * 0x8d22f6e6u;
    return Permuted;
}

[[nodiscard]] uint32_t HostNestedDigitPermutation(const uint32_t SeedInput, const uint32_t ScrambleKey) noexcept
{
    return BitReverse32(HostLaineKarrasPermutation(BitReverse32(SeedInput), ScrambleKey));
}

[[nodiscard]] uint32_t HostSobolSecondDimension(const uint32_t SampleIndex) noexcept
{
    uint32_t Bits = SampleIndex;
    Bits ^= (Bits & 0xaaaaaaaau) >> 1u;
    Bits ^= (Bits & 0xccccccccu) >> 2u;
    Bits ^= (Bits & 0xf0f0f0f0u) >> 4u;
    Bits ^= (Bits & 0xff00ff00u) >> 8u;
    Bits ^= (Bits & 0xffff0000u) >> 16u;
    return BitReverse32(Bits);
}

[[nodiscard]] CpuMaterialDefinition ConstructLayeredSurface(
    const std::array<float, 3> Albedo,
    const float                Roughness,
    const float                CoatWeight    = 0.0f,
    const float                CoatRoughness = 0.0f,
    const std::array<float, 3> Exitance      = { 0.0f, 0.0f, 0.0f },
    const float                SpecularF0    = 0.04f) noexcept
{
    CpuMaterialDefinition Mat {};
    Mat.Category      = GpuSurfaceCategory::Layered;
    Mat.Albedo        = Albedo;
    Mat.Roughness     = Roughness;
    Mat.CoatWeight    = CoatWeight;
    Mat.CoatRoughness = CoatRoughness;
    Mat.Exitance      = Exitance;
    Mat.SpecularF0    = SpecularF0;
    return Mat;
}

[[nodiscard]] CpuMaterialDefinition ConstructMetallicSurface(
    const std::array<float, 3> Eta,
    const std::array<float, 3> Kappa,
    const float                Roughness) noexcept
{
    CpuMaterialDefinition Mat {};
    Mat.Category  = GpuSurfaceCategory::Metallic;
    Mat.Albedo    = { 1.0f, 1.0f, 1.0f };
    Mat.Roughness = Roughness;
    Mat.Eta       = Eta;
    Mat.Kappa     = Kappa;
    return Mat;
}

[[nodiscard]] CpuMaterialDefinition ConstructSolidDielectric(
    const float                Ior,
    const std::array<float, 3> Absorption) noexcept
{
    CpuMaterialDefinition Mat {};
    Mat.Category   = GpuSurfaceCategory::SolidDielectric;
    Mat.Albedo     = { 1.0f, 1.0f, 1.0f };
    Mat.Roughness  = 0.0f;
    Mat.Absorption = Absorption;
    Mat.Ior        = Ior;
    return Mat;
}

[[nodiscard]] CpuMaterialDefinition ConstructThinDielectric(
    const float                Ior,
    const std::array<float, 3> Tint) noexcept
{
    CpuMaterialDefinition Mat {};
    Mat.Category  = GpuSurfaceCategory::ThinDielectric;
    Mat.Albedo    = Tint;
    Mat.Roughness = 0.0f;
    Mat.Ior       = Ior;
    return Mat;
}

[[nodiscard]] std::array<float, 3> HueToRgb(const float Hue, const float Saturation, const float ScalarReading) noexcept
{
    const auto Channel = [&](const float Offset) {
        const float K = std::fmod(Offset + Hue * 6.0f, 6.0f);
        return ScalarReading * (1.0f - Saturation * std::max(0.0f, std::min({ K, 4.0f - K, 1.0f })));
    };
    return { Channel(5.0f), Channel(3.0f), Channel(1.0f) };
}

[[nodiscard]] CpuMaterialDefinition ConstructGulbrandsenMetal(
    const std::array<float, 3> Reflectivity,
    const std::array<float, 3> EdgeTint,
    const float                Roughness) noexcept
{
    std::array<float, 3> Eta {};
    std::array<float, 3> Kappa {};
    for (std::size_t C = 0u; C < 3u; ++C)
    {
        const float R       = std::clamp(Reflectivity[C], 0.001f, 0.99f);
        const float G       = EdgeTint[C];
        const float Minimum = (1.0f - R) / (1.0f + R);
        const float Maximum = (1.0f + std::sqrt(R)) / (1.0f - std::sqrt(R));
        const float N       = G * Minimum + (1.0f - G) * Maximum;
        Eta[C]              = N;
        const float Numer   = R * (N + 1.0f) * (N + 1.0f) - (N - 1.0f) * (N - 1.0f);
        Kappa[C]            = std::sqrt(std::max(0.0f, Numer / (1.0f - R)));
    }
    return ConstructMetallicSurface(Eta, Kappa, Roughness);
}

[[nodiscard]] std::vector<CpuMaterialDefinition> ConstructStudioMaterialSlots(const DeterministicTracerConfiguration& Config)
{
    std::array<float, 3> PaintAlbedo { 0.680005f * 0.78f, 0.800458f * 0.78f, 0.0f };
    switch (Config.PaintPreset)
    {
        case StudioPaintPreset::AuthoredLime: PaintAlbedo = { 0.680005f * 0.78f, 0.800458f * 0.78f, 0.0f }; break;
        case StudioPaintPreset::RacingRed:    PaintAlbedo = { 0.62f, 0.025f, 0.018f }; break;
        case StudioPaintPreset::MidnightBlue: PaintAlbedo = { 0.012f, 0.035f, 0.16f }; break;
        case StudioPaintPreset::PearlWhite:   PaintAlbedo = { 0.80f, 0.79f, 0.76f }; break;
        case StudioPaintPreset::Graphite:     PaintAlbedo = { 0.035f, 0.037f, 0.040f }; break;
    }

    CpuMaterialDefinition FloorMat = ConstructLayeredSurface({ 0.16f, 0.16f, 0.17f }, 0.12f, 1.0f, 0.035f, { 0.0f, 0.0f, 0.0f }, 0.04f);
    switch (Config.FloorPreset)
    {
        case StudioFloorPreset::PolishedEpoxy:
            FloorMat = ConstructLayeredSurface({ 0.16f, 0.16f, 0.17f }, 0.12f, 1.0f, 0.035f, { 0.0f, 0.0f, 0.0f }, 0.04f);
            break;
        case StudioFloorPreset::MatteConcrete:
            FloorMat = ConstructLayeredSurface({ 0.30f, 0.29f, 0.28f }, 0.70f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.04f);
            break;
        case StudioFloorPreset::BlackMirror:
            FloorMat = ConstructLayeredSurface({ 0.01f, 0.01f, 0.01f }, 0.03f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.04f);
            break;
        case StudioFloorPreset::Lambert50:
            FloorMat = ConstructLayeredSurface({ 0.50f, 0.50f, 0.50f }, 1.00f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f);
            break;
    }

    constexpr std::array<float, 3> GoldEta     { 0.143f, 0.374f, 1.442f };
    constexpr std::array<float, 3> GoldKappa   { 3.983f, 2.385f, 1.603f };
    constexpr std::array<float, 3> CopperEta   { 0.200f, 0.924f, 1.102f };
    constexpr std::array<float, 3> CopperKappa { 3.912f, 2.452f, 2.142f };
    constexpr std::array<float, 3> AlumEta     { 1.657f, 0.880f, 0.521f };
    constexpr std::array<float, 3> AlumKappa   { 9.224f, 6.270f, 4.837f };
    constexpr std::array<float, 3> ChromEta    { 3.105f, 3.181f, 2.330f };
    constexpr std::array<float, 3> ChromKappa  { 3.330f, 3.330f, 3.150f };
    constexpr std::array<float, 3> SilverEta   { 0.155f, 0.117f, 0.138f };
    constexpr std::array<float, 3> SilverKappa { 4.828f, 3.122f, 2.147f };

    const float LampScale = Config.LampExitance;
    const float TailScale = std::max(LampScale, 0.5f);

    std::vector<CpuMaterialDefinition> Slots {};
    Slots.reserve(72u);
    Slots.push_back(ConstructLayeredSurface(PaintAlbedo, Config.PaintRoughness, 1.0f, 0.03f));                                          //  0 BodyCoat
    Slots.push_back(ConstructThinDielectric(1.52f, { 0.28f, 0.30f, 0.31f }));                                                    //  1 VehicleGlass
    Slots.push_back(ConstructLayeredSurface({ 0.012f, 0.014f, 0.018f }, 0.50f));                                                        //  2 VehiclePlastic
    Slots.push_back(ConstructLayeredSurface({ 0.03741f, 0.03741f, 0.03741f }, 0.76087f));                                               //  3 TyreRubber
    Slots.push_back(ConstructMetallicSurface({ 1.10f, 0.95f, 0.55f }, { 3.2f, 3.6f, 4.9f }, 0.22f));                                   //  4 WheelCapAccent
    Slots.push_back(ConstructMetallicSurface(ChromEta, ChromKappa, 0.29f));                                                            //  5 BrakeDisc
    Slots.push_back(ConstructLayeredSurface({ 0.80f, 0.80f, 0.80f }, 0.50f));                                                           //  6 VehicleTrim
    Slots.push_back(ConstructLayeredSurface({ 0.80f, 0.80f, 0.80f }, 0.10f, 0.0f, 0.0f, { 48.699997f * LampScale, 48.699997f * LampScale, 48.699997f * LampScale })); // 7 Headlight
    Slots.push_back(ConstructLayeredSurface({ 0.80f, 0.80f, 0.80f }, 0.10f, 0.0f, 0.0f, { 6.5f * TailScale, 0.0f, 6.5f * 0.016442f * TailScale }));                   // 8 Taillight
    Slots.push_back(ConstructMetallicSurface(AlumEta, AlumKappa, 0.30f));                                                              //  9 RimAlloy
    Slots.push_back(FloorMat);                                                                                              // 10 StudioFloor
    Slots.push_back(ConstructMetallicSurface(GoldEta, GoldKappa, 0.18f));                                                              // 11 PolishedGold
    Slots.push_back(ConstructMetallicSurface(CopperEta, CopperKappa, 0.38f));                                                          // 12 BrushedCopper
    Slots.push_back(ConstructMetallicSurface(SilverEta, SilverKappa, 0.0f));                                                           // 13 MirrorChrome
    Slots.push_back(ConstructSolidDielectric(1.50f, { 0.02f, 0.02f, 0.02f }));                                                   // 14 ClearGlass
    Slots.push_back(ConstructSolidDielectric(1.57f, { 2.60f, 0.35f, 1.90f }));                                                   // 15 EmeraldGlass
    Slots.push_back(ConstructLayeredSurface({ 0.72f, 0.07f, 0.05f }, 0.35f, 1.0f, 0.02f));                                              // 16 GlazedCeramic
    Slots.push_back(ConstructLayeredSurface({ 0.82f, 0.82f, 0.80f }, 0.90f));                                                           // 17 MatteChalk
    Slots.push_back(ConstructLayeredSurface({ 0.03741f, 0.03741f, 0.03741f }, 0.35f, 1.0f, 0.03f));                                     // 18 CanopyGloss

    // 49 golden-ratio fleet finishes (slots 19..67) matching StudioScenePresets.js::ConstructFleetFinishes(49)
    for (uint32_t Index = 0u; Index < 49u; ++Index)
    {
        const float                Hue    = std::fmod(0.08f + static_cast<float>(Index) * 0.6180339887f, 1.0f);
        const uint32_t             FinishGroup = (Index * 5u) % 6u;
        const bool                 Dark   = (Index % 7u == 3u);
        const bool                 Light  = (Index % 7u == 5u);
        const std::array<float, 3> Colour = Light ? HueToRgb(Hue, 0.12f, 0.80f) : HueToRgb(Hue, Dark ? 0.55f : 0.88f, Dark ? 0.07f : 0.55f);

        if (FinishGroup == 0u)      Slots.push_back(ConstructLayeredSurface(Colour, 0.30f, 1.0f, 0.025f));
        else if (FinishGroup == 1u) Slots.push_back(ConstructLayeredSurface({ Colour[0] * 0.6f, Colour[1] * 0.6f, Colour[2] * 0.6f }, 0.16f, 1.0f, 0.03f, { 0.0f, 0.0f, 0.0f }, 0.22f));
        else if (FinishGroup == 2u) Slots.push_back(ConstructLayeredSurface(Colour, 0.62f));
        else if (FinishGroup == 3u) Slots.push_back(ConstructGulbrandsenMetal({ 0.25f + 0.7f * Colour[0], 0.25f + 0.7f * Colour[1], 0.25f + 0.7f * Colour[2] },
                                                                        { 0.5f + 0.5f * Colour[0], 0.5f + 0.5f * Colour[1], 0.5f + 0.5f * Colour[2] }, 0.32f));
        else if (FinishGroup == 4u) Slots.push_back(ConstructLayeredSurface(HueToRgb(Hue, 0.10f, 0.82f), 0.22f, 1.0f, 0.02f, { 0.0f, 0.0f, 0.0f }, 0.07f));
        else                   Slots.push_back(ConstructGulbrandsenMetal({ 0.15f + 0.8f * Colour[0], 0.15f + 0.8f * Colour[1], 0.15f + 0.8f * Colour[2] },
                                                                        { 0.9f, 0.9f, 0.9f }, 0.07f));
    }

    // 4 fleet wheel rim finishes (slots 68..71)
    Slots.push_back(ConstructMetallicSurface(ChromEta, ChromKappa, 0.06f));
    Slots.push_back(ConstructLayeredSurface({ 0.02f, 0.02f, 0.022f }, 0.30f, 1.0f, 0.04f));
    Slots.push_back(ConstructMetallicSurface(GoldEta, GoldKappa, 0.24f));
    Slots.push_back(ConstructMetallicSurface(AlumEta, AlumKappa, 0.45f));

    return Slots;
}

[[nodiscard]] std::vector<float> PackGpuMaterialFloats(std::span<const CpuMaterialDefinition> Slots)
{
    std::vector<float> Packed(Slots.size() * MaterialFloatStride, 0.0f);
    for (std::size_t I = 0u; I < Slots.size(); ++I)
    {
        const CpuMaterialDefinition& S = Slots[I];
        const std::size_t            O = I * MaterialFloatStride;
        Packed[O + 0u]  = S.Albedo[0];
        Packed[O + 1u]  = S.Albedo[1];
        Packed[O + 2u]  = S.Albedo[2];
        Packed[O + 3u]  = S.Roughness;
        Packed[O + 4u]  = S.Eta[0];
        Packed[O + 5u]  = S.Eta[1];
        Packed[O + 6u]  = S.Eta[2];
        Packed[O + 7u]  = static_cast<float>(static_cast<uint32_t>(S.Category));
        Packed[O + 8u]  = S.Kappa[0];
        Packed[O + 9u]  = S.Kappa[1];
        Packed[O + 10u] = S.Kappa[2];
        Packed[O + 11u] = S.CoatWeight;
        Packed[O + 12u] = S.Exitance[0];
        Packed[O + 13u] = S.Exitance[1];
        Packed[O + 14u] = S.Exitance[2];
        Packed[O + 15u] = S.CoatRoughness;
        Packed[O + 16u] = S.Absorption[0];
        Packed[O + 17u] = S.Absorption[1];
        Packed[O + 18u] = S.Absorption[2];
        Packed[O + 19u] = S.Ior;
        Packed[O + 20u] = S.SpecularF0;
    }
    return Packed;
}

[[nodiscard]] std::vector<float> ConstructShowcaseSphereFloats(std::array<float, 4>& OutBoundingSphere)
{
    constexpr float                  Radius = 0.46f;
    constexpr std::array<uint32_t, 7> MaterialSlots { 11u, 12u, 13u, 14u, 15u, 16u, 17u };
    std::vector<float>               Packed(MaterialSlots.size() * SphereFloatStride, 0.0f);

    float MinX = 1.0e30f, MinY = 1.0e30f, MinZ = 1.0e30f;
    float MaxX = -1.0e30f, MaxY = -1.0e30f, MaxZ = -1.0e30f;
    for (std::size_t Index = 0u; Index < MaterialSlots.size(); ++Index)
    {
        const float       HalfSpan = static_cast<float>(MaterialSlots.size() - 1u) * 0.5f;
        const float       ArcParam = (static_cast<float>(Index) - HalfSpan) / HalfSpan;
        const float       X        = ArcParam * 3.7f;
        const float       Z        = 2.75f + 0.55f * ArcParam * ArcParam;
        const std::size_t O        = Index * SphereFloatStride;
        Packed[O + 0u]             = X;
        Packed[O + 1u]             = Radius;
        Packed[O + 2u]             = Z;
        Packed[O + 3u]             = Radius;
        Packed[O + 4u]             = static_cast<float>(MaterialSlots[Index]);
        MinX = std::min(MinX, X - Radius);
        MaxX = std::max(MaxX, X + Radius);
        MinY = std::min(MinY, 0.0f);
        MaxY = std::max(MaxY, Radius * 2.0f);
        MinZ = std::min(MinZ, Z - Radius);
        MaxZ = std::max(MaxZ, Z + Radius);
    }

    const float Cx = 0.5f * (MinX + MaxX);
    const float Cy = 0.5f * (MinY + MaxY);
    const float Cz = 0.5f * (MinZ + MaxZ);
    float       BoundRadius = 0.0f;
    for (std::size_t Index = 0u; Index < MaterialSlots.size(); ++Index)
    {
        const std::size_t O    = Index * SphereFloatStride;
        const float       Dist = std::sqrt((Packed[O] - Cx) * (Packed[O] - Cx) + (Packed[O + 1u] - Cy) * (Packed[O + 1u] - Cy) + (Packed[O + 2u] - Cz) * (Packed[O + 2u] - Cz));
        BoundRadius            = std::max(BoundRadius, Dist + Packed[O + 3u]);
    }
    OutBoundingSphere = { Cx, Cy, Cz, BoundRadius + 1.0e-3f };
    return Packed;
}

[[nodiscard]] std::vector<float> ConstructSoftboxLightFloats(
    const DeterministicTracerConfiguration& Config,
    uint32_t&                               OutActiveCount)
{
    OutActiveCount = 0u;
    if (Config.NightRigActive)
    {
        return std::vector<float>(AreaLightFloatStride, 0.0f);
    }

    struct SoftboxSpec
    {
        std::array<float, 3> Centre;
        std::array<float, 3> AimPoint;
        float                Width;
        float                Height;
        std::array<float, 3> Tint;
        float                Scale;
    };

    const std::array<SoftboxSpec, 3> Specs {{
        { {  0.4f, 5.8f,  0.6f }, { 0.4f, 0.0f, 0.6f }, 5.6f, 2.6f, { 1.00f, 0.98f, 0.95f }, Config.KeyIntensity },
        { { -1.5f, 2.6f, -6.0f }, { 0.0f, 0.8f, 0.0f }, 7.0f, 0.8f, { 0.92f, 0.96f, 1.00f }, Config.RimIntensity },
        { {  5.8f, 6.2f,  6.4f }, { 0.0f, 0.6f, 0.6f }, 3.2f, 2.2f, { 1.00f, 0.93f, 0.85f }, Config.FillIntensity },
    }};

    std::vector<float> Packed {};
    for (const SoftboxSpec& Spec : Specs)
    {
        if (Spec.Scale <= 0.0f)
        {
            continue;
        }
        const float Dx     = Spec.AimPoint[0] - Spec.Centre[0];
        const float Dy     = Spec.AimPoint[1] - Spec.Centre[1];
        const float Dz     = Spec.AimPoint[2] - Spec.Centre[2];
        const float InvLen = 1.0f / std::max(std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz), 1.0e-12f);
        const std::array<float, 3> Normal { Dx * InvLen, Dy * InvLen, Dz * InvLen };

        std::array<float, 3> Lateral { 1.0f, 0.0f, 0.0f };
        if (std::abs(Normal[1]) <= 0.99f)
        {
            // cross([0,1,0], Normal) = (Normal.z, 0, -Normal.x)
            const float Lx   = Normal[2];
            const float Lz   = -Normal[0];
            const float InvL = 1.0f / std::max(std::hypot(Lx, Lz), 1.0e-12f);
            Lateral          = { Lx * InvL, 0.0f, Lz * InvL };
        }
        // Upright = cross(Normal, Lateral)
        const std::array<float, 3> Upright {
            Normal[1] * Lateral[2] - Normal[2] * Lateral[1],
            Normal[2] * Lateral[0] - Normal[0] * Lateral[2],
            Normal[0] * Lateral[1] - Normal[1] * Lateral[0],
        };
        const std::array<float, 3> EdgeU { Lateral[0] * Spec.Width, Lateral[1] * Spec.Width, Lateral[2] * Spec.Width };
        const std::array<float, 3> EdgeV { Upright[0] * Spec.Height, Upright[1] * Spec.Height, Upright[2] * Spec.Height };

        const float Cx      = EdgeU[1] * EdgeV[2] - EdgeU[2] * EdgeV[1];
        const float Cy      = EdgeU[2] * EdgeV[0] - EdgeU[0] * EdgeV[2];
        const float Cz      = EdgeU[0] * EdgeV[1] - EdgeU[1] * EdgeV[0];
        const float Area    = std::sqrt(Cx * Cx + Cy * Cy + Cz * Cz);
        const float InvArea = (Area > 0.0f) ? (1.0f / Area) : 0.0f;

        const std::size_t O = Packed.size();
        Packed.resize(O + AreaLightFloatStride, 0.0f);
        Packed[O + 0u]  = Spec.Centre[0];
        Packed[O + 1u]  = Spec.Centre[1];
        Packed[O + 2u]  = Spec.Centre[2];
        Packed[O + 3u]  = Area;
        Packed[O + 4u]  = EdgeU[0];
        Packed[O + 5u]  = EdgeU[1];
        Packed[O + 6u]  = EdgeU[2];
        Packed[O + 7u]  = Cx * InvArea;
        Packed[O + 8u]  = EdgeV[0];
        Packed[O + 9u]  = EdgeV[1];
        Packed[O + 10u] = EdgeV[2];
        Packed[O + 11u] = Cy * InvArea;
        Packed[O + 12u] = Spec.Tint[0] * Spec.Scale;
        Packed[O + 13u] = Spec.Tint[1] * Spec.Scale;
        Packed[O + 14u] = Spec.Tint[2] * Spec.Scale;
        Packed[O + 15u] = Cz * InvArea;
        ++OutActiveCount;
    }

    if (Packed.empty())
    {
        Packed.resize(AreaLightFloatStride, 0.0f);
    }
    return Packed;
}

[[nodiscard]] uint32_t ResolveVehicleSlotOrdinal(const Codecs::VehicleSurfaceSlot SurfaceSlot) noexcept
{
    switch (SurfaceSlot)
    {
        case Codecs::VehicleSurfaceSlot::BodyCoat:       return 0u;
        case Codecs::VehicleSurfaceSlot::VehicleGlass:   return 1u;
        case Codecs::VehicleSurfaceSlot::VehiclePlastic: return 2u;
        case Codecs::VehicleSurfaceSlot::TyreRubber:     return 3u;
        case Codecs::VehicleSurfaceSlot::WheelCapAccent:      return 4u;
        case Codecs::VehicleSurfaceSlot::BrakeDisc:      return 5u;
        case Codecs::VehicleSurfaceSlot::VehicleTrim:    return 6u;
        case Codecs::VehicleSurfaceSlot::HeadlightLens:  return 7u;
        case Codecs::VehicleSurfaceSlot::TaillightLens:  return 8u;
        case Codecs::VehicleSurfaceSlot::RimAlloy:       return 9u;
        case Codecs::VehicleSurfaceSlot::CanopyGloss:    return 18u;
        default:                                                 return 6u;
    }
}

[[nodiscard]] DeviceBufferResource UploadHostVisibleStorageBuffer(
    const VulkanInterchange& Vulkan,
    const void*              MappedBytes,
    const std::size_t        ByteSize)
{
    const uint64_t AllocBytes = std::max<uint64_t>(16ull, static_cast<uint64_t>(ByteSize));
    DeviceBufferResource Buf  = Vulkan.AllocateBuffer(
        AllocBytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (Buf.HostAddress != nullptr && ByteSize > 0u && MappedBytes != nullptr)
    {
        std::memcpy(Buf.HostAddress, MappedBytes, ByteSize);
    }
    return Buf;
}

void EncodeFullMemoryBarrier(const VkCommandBuffer Cmd) noexcept
{
    VkMemoryBarrier Barrier {};
    Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0u,
        1u,
        &Barrier,
        0u,
        nullptr,
        0u,
        nullptr);
}

void TransitionImageLayout(
    const VkCommandBuffer    Cmd,
    const VkImage            Image,
    const VkImageAspectFlags AspectMask,
    const VkImageLayout      OldLayout,
    const VkImageLayout      NewLayout) noexcept
{
    VkImageMemoryBarrier Barrier {};
    Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.oldLayout                       = OldLayout;
    Barrier.newLayout                       = NewLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = Image;
    Barrier.subresourceRange.aspectMask     = AspectMask;
    Barrier.subresourceRange.baseMipLevel   = 0u;
    Barrier.subresourceRange.levelCount     = 1u;
    Barrier.subresourceRange.baseArrayLayer = 0u;
    Barrier.subresourceRange.layerCount     = 1u;
    Barrier.srcAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT;
    Barrier.dstAccessMask                   = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0u,
        0u,
        nullptr,
        0u,
        nullptr,
        1u,
        &Barrier);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    STATIC ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

DeterministicPathTraceSequence::~DeterministicPathTraceSequence()
{
    Teardown();
}

void DeterministicPathTraceSequence::ApplyRenderProfile(
    const DeterministicRenderProfile  Profile,
    DeterministicTracerConfiguration& InOutConfig) noexcept
{
    InOutConfig.Profile = Profile;
    if (Profile == DeterministicRenderProfile::GameRealtime)
    {
        InOutConfig.TraceScale             = 2u;
        InOutConfig.SampleBatch            = 1u;
        InOutConfig.PointPattern           = QuadraturePointPattern::BlueNoise;
        InOutConfig.VisibilityRasterActive = false;
        InOutConfig.ReprojectionActive     = true;
        InOutConfig.HistoryCapacity        = 32u;
        InOutConfig.HistorySpan            = 8.0f;
        InOutConfig.DenoiseActive          = true;
        InOutConfig.DenoiseLuminanceSigma  = 4.0f;
        InOutConfig.LampSampling           = LampSamplingRule::SplitEight;
        InOutConfig.BounceLimit            = 2u;
        InOutConfig.DepthLimit             = 6u;
        InOutConfig.TargetSamples          = 64u;
    }
    else if (Profile == DeterministicRenderProfile::SceneFinalRender)
    {
        InOutConfig.TraceScale             = 1u;
        InOutConfig.SampleBatch            = 1u;
        InOutConfig.PointPattern           = QuadraturePointPattern::Shared;
        InOutConfig.VisibilityRasterActive = false;
        InOutConfig.ReprojectionActive     = false;
        InOutConfig.DenoiseActive          = false;
        InOutConfig.LampSampling           = LampSamplingRule::SplitEight;
        InOutConfig.BounceLimit            = 2u;
        InOutConfig.DepthLimit             = 10u;
        InOutConfig.ThroughputCutoff       = 0.002f;
        InOutConfig.TargetSamples          = 1024u;
    }
}

DeterministicOrbitCamera DeterministicPathTraceSequence::QueryCameraPreset(const StudioCameraPreset Preset) noexcept
{
    switch (Preset)
    {
        case StudioCameraPreset::Hero:
            return { 0.78f, 0.13f, 9.6f, { 0.10f, 0.70f, 1.00f }, 36.0f };
        case StudioCameraPreset::Profile:
            return { 0.0f, 0.06f, 12.5f, { 0.0f, 0.80f, 0.0f }, 34.0f };
        case StudioCameraPreset::Front:
            return { 1.5708f, 0.10f, 11.0f, { 0.0f, 0.75f, 0.60f }, 30.0f };
        case StudioCameraPreset::Spheres:
            return { 0.0f, 0.20f, 6.8f, { 0.0f, 0.50f, 2.90f }, 42.0f };
        case StudioCameraPreset::Rear:
            return { 3.95f, 0.16f, 11.8f, { 0.0f, 0.80f, 0.0f }, 34.0f };
        case StudioCameraPreset::Fleet:
            return { 0.62f, 0.42f, 52.0f, { 0.0f, 0.60f, -14.0f }, 44.0f };
    }
    return { 0.78f, 0.13f, 9.6f, { 0.10f, 0.70f, 1.00f }, 36.0f };
}

DeterministicCameraBasis DeterministicPathTraceSequence::ResolveCameraBasis(
    const DeterministicOrbitCamera& Camera,
    const float                     AspectRatio) noexcept
{
    const float CosPitch = std::cos(Camera.PitchRadians);
    const std::array<float, 3> Origin {
        Camera.Target[0] + Camera.DistanceMeters * CosPitch * std::sin(Camera.YawRadians),
        Camera.Target[1] + Camera.DistanceMeters * std::sin(Camera.PitchRadians),
        Camera.Target[2] + Camera.DistanceMeters * CosPitch * std::cos(Camera.YawRadians),
    };
    const auto Normalize3 = [](const std::array<float, 3>& V) noexcept -> std::array<float, 3> {
        const float L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
        if (L <= 1.0e-12f)
        {
            return { 0.0f, 0.0f, -1.0f };
        }
        return { V[0] / L, V[1] / L, V[2] / L };
    };
    const auto Cross3 = [](const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept -> std::array<float, 3> {
        return {
            A[1] * B[2] - A[2] * B[1],
            A[2] * B[0] - A[0] * B[2],
            A[0] * B[1] - A[1] * B[0],
        };
    };
    const std::array<float, 3> Forward     = Normalize3({ Camera.Target[0] - Origin[0], Camera.Target[1] - Origin[1], Camera.Target[2] - Origin[2] });
    const std::array<float, 3> UnitRight   = Normalize3(Cross3(Forward, { 0.0f, 1.0f, 0.0f }));
    const std::array<float, 3> UnitUp      = Cross3(UnitRight, Forward);
    const float                TangentHalf = std::tan(Camera.FieldOfViewDegrees * PiConstant / 360.0f);

    DeterministicCameraBasis Basis {};
    Basis.Origin  = Origin;
    Basis.Forward = Forward;
    Basis.Right   = { UnitRight[0] * TangentHalf * AspectRatio, UnitRight[1] * TangentHalf * AspectRatio, UnitRight[2] * TangentHalf * AspectRatio };
    Basis.Up      = { UnitUp[0] * TangentHalf, UnitUp[1] * TangentHalf, UnitUp[2] * TangentHalf };
    return Basis;
}

std::array<float, 2> DeterministicPathTraceSequence::EvaluateHostQuadraturePair(
    const uint32_t                SampleIndex,
    const uint32_t                PairIndex,
    const uint32_t                PixelKey,
    const std::array<uint32_t, 2> DigitShift) noexcept
{
    const uint32_t PairKey       = HostAvalancheKey(PairIndex * 0x9e3779b9u + 0x85ebca6bu) ^ PixelKey;
    const uint32_t ShuffledIndex = HostNestedDigitPermutation(SampleIndex, PairKey);
    const uint32_t FirstDigits   = HostNestedDigitPermutation(BitReverse32(ShuffledIndex), HostAvalancheKey(PairKey ^ 0xa511e9b3u));
    const uint32_t SecondDigits  = HostNestedDigitPermutation(HostSobolSecondDimension(ShuffledIndex), HostAvalancheKey(PairKey ^ 0x63d83595u));
    return {
        static_cast<float>((FirstDigits ^ DigitShift[0]) >> 8u) * (1.0f / 16777216.0f),
        static_cast<float>((SecondDigits ^ DigitShift[1]) >> 8u) * (1.0f / 16777216.0f),
    };
}

std::string DeterministicPathTraceSequence::DigestFloatSpan(std::span<const float> Floats)
{
    const auto*       Bytes     = reinterpret_cast<const uint8_t*>(Floats.data());
    const std::size_t ByteCount = Floats.size() * sizeof(float);
    uint32_t          Digest    = 0x811c9dc5u;
    for (std::size_t I = 0u; I < ByteCount; ++I)
    {
        Digest ^= Bytes[I];
        Digest *= 0x01000193u;
    }
    char Hex[16] {};
    std::snprintf(Hex, sizeof(Hex), "%08x", Digest);
    return std::string(Hex);
}

float DeterministicPathTraceSequence::ResolveDenoiseStrength(const float EffectivePixelSamples) noexcept
{
    const float LogSamples = std::log2(std::max(EffectivePixelSamples, 1.0f));
    const float LogFull    = std::log2(DenoiseFullUntilSpp);
    const float LogFaded   = std::log2(DenoiseFadedAtSpp);
    const float Position   = std::clamp((LogSamples - LogFull) / (LogFaded - LogFull), 0.0f, 1.0f);
    return 1.0f - Position * Position * (3.0f - 2.0f * Position);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               INITIALIZATION & TEARDOWN
//------------------------------------------------------------------------------------------------------------------------

bool DeterministicPathTraceSequence::Initialize(
    VulkanInterchange&           DeviceInterchange,
    const std::filesystem::path& CompiledShaderDirectory,
    const std::filesystem::path& RepositoryRootDirectory,
    std::string* const           OutDiagnostic)
{
    Teardown();
    if (!DeviceInterchange.Ready())
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = "VulkanInterchange is not initialized.";
        }
        return false;
    }

    Vulkan   = &DeviceInterchange;
    RepoRoot = RepositoryRootDirectory;

    TracingUniformBuffer = Vulkan->AllocateBuffer(
        TracingSlotStrideBytes * TracingSlotTotal,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ReconstructUniformBuf = Vulkan->AllocateBuffer(
        ReconstructSlotStride * ReconstructStepTotal,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    DisplayUniformBuffer = Vulkan->AllocateBuffer(
        DisplayUniformByteSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    RasterUniformBuffer = Vulkan->AllocateBuffer(
        RasterUniformByteSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (!LoadStaticAssets(RepositoryRootDirectory, OutDiagnostic))
    {
        Teardown();
        return false;
    }
    if (!ConstructShaderPrograms(CompiledShaderDirectory, OutDiagnostic))
    {
        Teardown();
        return false;
    }

    Configuration.Camera = QueryCameraPreset(StudioCameraPreset::Hero);
    ProgramsReady       = true;
    return true;
}

void DeterministicPathTraceSequence::Teardown() noexcept
{
    if (Vulkan != nullptr && Vulkan->Ready())
    {
        VkDevice DeviceHandle = Vulkan->AccessDevice();
        vkDeviceWaitIdle(DeviceHandle);

        if (VisibilityFramebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(DeviceHandle, VisibilityFramebuffer, nullptr); VisibilityFramebuffer = VK_NULL_HANDLE; }
        if (VisibilityRasterPipe != VK_NULL_HANDLE)  { vkDestroyPipeline(DeviceHandle, VisibilityRasterPipe, nullptr);     VisibilityRasterPipe  = VK_NULL_HANDLE; }
        if (VisibilityAttachmentTarget != VK_NULL_HANDLE)  { vkDestroyRenderPass(DeviceHandle, VisibilityAttachmentTarget, nullptr);   VisibilityAttachmentTarget  = VK_NULL_HANDLE; }
        if (DisplayProgram != VK_NULL_HANDLE)       { vkDestroyPipeline(DeviceHandle, DisplayProgram, nullptr);          DisplayProgram       = VK_NULL_HANDLE; }
        if (AtrousProgram != VK_NULL_HANDLE)        { vkDestroyPipeline(DeviceHandle, AtrousProgram, nullptr);           AtrousProgram        = VK_NULL_HANDLE; }
        if (GatherProgram != VK_NULL_HANDLE)        { vkDestroyPipeline(DeviceHandle, GatherProgram, nullptr);           GatherProgram        = VK_NULL_HANDLE; }
        if (QuadratureProgram != VK_NULL_HANDLE)    { vkDestroyPipeline(DeviceHandle, QuadratureProgram, nullptr);       QuadratureProgram    = VK_NULL_HANDLE; }
        if (FeaturesProgram != VK_NULL_HANDLE)      { vkDestroyPipeline(DeviceHandle, FeaturesProgram, nullptr);         FeaturesProgram      = VK_NULL_HANDLE; }
        if (ReprojectProgram != VK_NULL_HANDLE)     { vkDestroyPipeline(DeviceHandle, ReprojectProgram, nullptr);        ReprojectProgram     = VK_NULL_HANDLE; }
        if (IntegrateProgram != VK_NULL_HANDLE)     { vkDestroyPipeline(DeviceHandle, IntegrateProgram, nullptr);        IntegrateProgram     = VK_NULL_HANDLE; }

        if (RasterPipeLayout != VK_NULL_HANDLE)      { vkDestroyPipelineLayout(DeviceHandle, RasterPipeLayout, nullptr);      RasterPipeLayout      = VK_NULL_HANDLE; }
        if (DisplayPipeLayout != VK_NULL_HANDLE)     { vkDestroyPipelineLayout(DeviceHandle, DisplayPipeLayout, nullptr);     DisplayPipeLayout     = VK_NULL_HANDLE; }
        if (ReconstructPipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(DeviceHandle, ReconstructPipeLayout, nullptr); ReconstructPipeLayout = VK_NULL_HANDLE; }
        if (IntegrationPipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(DeviceHandle, IntegrationPipeLayout, nullptr); IntegrationPipeLayout = VK_NULL_HANDLE; }

        if (DescriptorAllocator != VK_NULL_HANDLE)   { vkDestroyDescriptorPool(DeviceHandle, DescriptorAllocator, nullptr);   DescriptorAllocator   = VK_NULL_HANDLE; }
        if (RasterSetLayout != VK_NULL_HANDLE)       { vkDestroyDescriptorSetLayout(DeviceHandle, RasterSetLayout, nullptr);       RasterSetLayout       = VK_NULL_HANDLE; }
        if (DisplaySetLayout != VK_NULL_HANDLE)      { vkDestroyDescriptorSetLayout(DeviceHandle, DisplaySetLayout, nullptr);      DisplaySetLayout      = VK_NULL_HANDLE; }
        if (ReconstructSetLayout != VK_NULL_HANDLE)  { vkDestroyDescriptorSetLayout(DeviceHandle, ReconstructSetLayout, nullptr);  ReconstructSetLayout  = VK_NULL_HANDLE; }
        if (IntegrationSetLayout != VK_NULL_HANDLE)  { vkDestroyDescriptorSetLayout(DeviceHandle, IntegrationSetLayout, nullptr);  IntegrationSetLayout  = VK_NULL_HANDLE; }

        Vulkan->ReleaseImage2D(VisibilityDepthTexture);
        Vulkan->ReleaseImage2D(VisibilityIdTexture);
        Vulkan->ReleaseImage2D(BlueNoiseTexture);

        Vulkan->ReleaseBuffer(DisplayRgba8Buffer);
        Vulkan->ReleaseBuffer(SurfaceFeatureBuffer);
        Vulkan->ReleaseBuffer(ReconstructionBuffer);
        Vulkan->ReleaseBuffer(AccumulationBuffer);
        Vulkan->ReleaseBuffer(SphereRegisterBuffer);
        Vulkan->ReleaseBuffer(LightRegisterBuffer);
        Vulkan->ReleaseBuffer(MaterialRegisterBuffer);
        Vulkan->ReleaseBuffer(TriangleNormBuffer);
        Vulkan->ReleaseBuffer(TrianglePosBuffer);
        Vulkan->ReleaseBuffer(BoundsEntryBuffer);
        Vulkan->ReleaseBuffer(RasterUniformBuffer);
        Vulkan->ReleaseBuffer(DisplayUniformBuffer);
        Vulkan->ReleaseBuffer(ReconstructUniformBuf);
        Vulkan->ReleaseBuffer(TracingUniformBuffer);
    }

    IntegrationSets.fill(VK_NULL_HANDLE);
    ReconstructionSets.fill(VK_NULL_HANDLE);
    DisplaySet             = VK_NULL_HANDLE;
    RasterSet              = VK_NULL_HANDLE;
    AccumulationWidth      = 0u;
    AccumulationHeight     = 0u;
    UploadedVehicleCount   = 0u;
    SampleIndexCursor      = 0u;
    ProgramsReady         = false;
    Vulkan                 = nullptr;
}

bool DeterministicPathTraceSequence::LoadStaticAssets(
    const std::filesystem::path& RepositoryRootDirectory,
    std::string* const           OutDiagnostic)
{
    // 1. Load 64x64 r32uint BlueNoiseRanks.bin
    const std::filesystem::path BlueNoisePath = RepositoryRootDirectory / "EngineContent/Optics/BlueNoiseRanks.bin";
    std::ifstream               BlueStream(BlueNoisePath, std::ios::binary);
    std::vector<uint32_t>       BlueRanks(4096u, 0u);
    if (!BlueStream || !BlueStream.read(reinterpret_cast<char*>(BlueRanks.data()), static_cast<std::streamsize>(BlueRanks.size() * sizeof(uint32_t))))
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = "Failed to load EngineContent/Optics/BlueNoiseRanks.bin";
        }
        return false;
    }

    BlueNoiseTexture = Vulkan->AllocateImage2D(
        64u,
        64u,
        VK_FORMAT_R32_UINT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    DeviceBufferResource Staging = Vulkan->AllocateBuffer(
        BlueRanks.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(Staging.HostAddress, BlueRanks.data(), BlueRanks.size() * sizeof(uint32_t));

    VkCommandBuffer Cmd = Vulkan->BeginSingleTimeCommands();
    TransitionImageLayout(Cmd, BlueNoiseTexture.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.layerCount = 1u;
    CopyRegion.imageExtent                 = { 64u, 64u, 1u };
    vkCmdCopyBufferToImage(Cmd, Staging.BufferHandle, BlueNoiseTexture.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyRegion);
    TransitionImageLayout(Cmd, BlueNoiseTexture.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const bool UploadedNoise = AwaitDispatchedCommands(Cmd);
    Vulkan->ReleaseBuffer(Staging);
    if (!UploadedNoise)
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = "Failed to upload BlueNoiseRanks texture.";
        }
        return false;
    }

    // 2. Load 3 LM-63-2002 .ies photometry profiles ([0] LowBeamHeadlamp, [1] TailLamp, [2] CarParkDownlight)
    std::string PhotometryDiag {};
    if (!Codecs::PhotometricProfileCodec::LoadCanonicalPhotometrySuite(
            RepositoryRootDirectory,
            PhotometryProfiles,
            PhotometryDiag))
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = PhotometryDiag;
        }
        return false;
    }

    // 3. Decode ControlVehicle (25,068 tris in Studio Y-up coordinates) and build prototype CompressedWideTree once
    Codecs::AssembledControlVehicle Assembled {};
    std::string                     VehicleDiag {};
    if (!Codecs::ControlVehicleSurfaceDecoder::DecodeAssembledVehicle(
            RepositoryRootDirectory,
            Codecs::VehicleCoordinateConvention::StudioPathYUp,
            Assembled,
            VehicleDiag))
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = VehicleDiag;
        }
        return false;
    }

    std::vector<SpatialAcceleration::RayTrianglePrimitive>  Primitives(Assembled.Triangles.size());
    std::vector<SpatialAcceleration::TriangleSmoothNormals> SmoothNormals(Assembled.Triangles.size());
    PrototypeVehicle.LocalBounds = { 1.0e30f, 1.0e30f, 1.0e30f, -1.0e30f, -1.0e30f, -1.0e30f };

    for (std::size_t I = 0u; I < Assembled.Triangles.size(); ++I)
    {
        const auto&    Tri        = Assembled.Triangles[I];
        const uint16_t StudioSlot = static_cast<uint16_t>(ResolveVehicleSlotOrdinal(Tri.SurfaceSlot));
        const bool     ThinSheet  = (Tri.SurfaceSlot == Codecs::VehicleSurfaceSlot::VehicleGlass);

        Primitives[I].Corner0          = Tri.CornerA;
        Primitives[I].Edge01           = Tri.CornerB - Tri.CornerA;
        Primitives[I].Edge02           = Tri.CornerC - Tri.CornerA;
        Primitives[I].MaterialOrdinal  = StudioSlot;
        Primitives[I].OpaqueMask       = ThinSheet ? 0u : 1u;
        Primitives[I].PrimitiveOrdinal = static_cast<uint32_t>(I);

        SmoothNormals[I].Normal0 = Tri.NormalA;
        SmoothNormals[I].Normal1 = Tri.NormalB;
        SmoothNormals[I].Normal2 = Tri.NormalC;

        for (const Mathematics::Axis3D& Corner : { Tri.CornerA, Tri.CornerB, Tri.CornerC })
        {
            PrototypeVehicle.LocalBounds[0] = std::min(PrototypeVehicle.LocalBounds[0], Corner.X);
            PrototypeVehicle.LocalBounds[1] = std::min(PrototypeVehicle.LocalBounds[1], Corner.Y);
            PrototypeVehicle.LocalBounds[2] = std::min(PrototypeVehicle.LocalBounds[2], Corner.Z);
            PrototypeVehicle.LocalBounds[3] = std::max(PrototypeVehicle.LocalBounds[3], Corner.X);
            PrototypeVehicle.LocalBounds[4] = std::max(PrototypeVehicle.LocalBounds[4], Corner.Y);
            PrototypeVehicle.LocalBounds[5] = std::max(PrototypeVehicle.LocalBounds[5], Corner.Z);
        }
    }

    if (!PrototypeVehicle.WideTree.BuildFromPrimitives(Primitives, SmoothNormals, 0u, SpatialAcceleration::CompressedLeafPrimCeiling))
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = "Failed to build ControlVehicle CompressedWideTree.";
        }
        return false;
    }

    const std::size_t ReorderedCount = PrototypeVehicle.WideTree.Primitives.size();
    PrototypeVehicle.PositionFloats.assign(ReorderedCount * 12u, 0.0f);
    PrototypeVehicle.NormalFloats.assign(ReorderedCount * 12u, 0.0f);
    for (std::size_t Slot = 0u; Slot < ReorderedCount; ++Slot)
    {
        const auto&       Prim       = PrototypeVehicle.WideTree.Primitives[Slot];
        const auto&       Norm       = PrototypeVehicle.WideTree.PrimitiveNormals[Slot];
        const std::size_t O          = Slot * 12u;
        const uint32_t    PackedSlot = static_cast<uint32_t>(Prim.MaterialOrdinal) | ((Prim.OpaqueMask == 0u) ? ThinSheetPackedBit : 0u);

        PrototypeVehicle.PositionFloats[O + 0u]  = Prim.Corner0.X;
        PrototypeVehicle.PositionFloats[O + 1u]  = Prim.Corner0.Y;
        PrototypeVehicle.PositionFloats[O + 2u]  = Prim.Corner0.Z;
        std::memcpy(&PrototypeVehicle.PositionFloats[O + 3u], &PackedSlot, sizeof(uint32_t));
        PrototypeVehicle.PositionFloats[O + 4u]  = Prim.Edge01.X;
        PrototypeVehicle.PositionFloats[O + 5u]  = Prim.Edge01.Y;
        PrototypeVehicle.PositionFloats[O + 6u]  = Prim.Edge01.Z;
        PrototypeVehicle.PositionFloats[O + 8u]  = Prim.Edge02.X;
        PrototypeVehicle.PositionFloats[O + 9u]  = Prim.Edge02.Y;
        PrototypeVehicle.PositionFloats[O + 10u] = Prim.Edge02.Z;

        PrototypeVehicle.NormalFloats[O + 0u]  = Norm.Normal0.X;
        PrototypeVehicle.NormalFloats[O + 1u]  = Norm.Normal0.Y;
        PrototypeVehicle.NormalFloats[O + 2u]  = Norm.Normal0.Z;
        PrototypeVehicle.NormalFloats[O + 4u]  = Norm.Normal1.X;
        PrototypeVehicle.NormalFloats[O + 5u]  = Norm.Normal1.Y;
        PrototypeVehicle.NormalFloats[O + 6u]  = Norm.Normal1.Z;
        PrototypeVehicle.NormalFloats[O + 8u]  = Norm.Normal2.X;
        PrototypeVehicle.NormalFloats[O + 9u]  = Norm.Normal2.Y;
        PrototypeVehicle.NormalFloats[O + 10u] = Norm.Normal2.Z;
    }
    PrototypeVehicle.SourceTriCount = static_cast<uint32_t>(Assembled.Triangles.size());
    PrototypeVehicle.Built          = true;
    return true;
}

bool DeterministicPathTraceSequence::ConstructShaderPrograms(
    const std::filesystem::path& CompiledShaderDirectory,
    std::string* const           OutDiagnostic)
{
    VkDevice DeviceHandle = Vulkan->AccessDevice();

    // 1. Descriptor set layouts
    {
        std::array<VkDescriptorSetLayoutBinding, 11> LayoutSlots {};
        LayoutSlots[0]  = { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        for (uint32_t Slot = 1u; Slot <= 7u; ++Slot)
        {
            LayoutSlots[Slot] = { Slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        }
        LayoutSlots[8]  = { 8u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        LayoutSlots[9]  = { 9u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        LayoutSlots[10] = { 10u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

        VkDescriptorSetLayoutCreateInfo SetLayoutSpec {};
        SetLayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        SetLayoutSpec.bindingCount = static_cast<uint32_t>(LayoutSlots.size());
        SetLayoutSpec.pBindings    = LayoutSlots.data();
        if (vkCreateDescriptorSetLayout(DeviceHandle, &SetLayoutSpec, nullptr, &IntegrationSetLayout) != VK_SUCCESS)
        {
            return false;
        }
    }
    {
        std::array<VkDescriptorSetLayoutBinding, 4> LayoutSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        }};
        VkDescriptorSetLayoutCreateInfo SetLayoutSpec {};
        SetLayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        SetLayoutSpec.bindingCount = static_cast<uint32_t>(LayoutSlots.size());
        SetLayoutSpec.pBindings    = LayoutSlots.data();
        if (vkCreateDescriptorSetLayout(DeviceHandle, &SetLayoutSpec, nullptr, &ReconstructSetLayout) != VK_SUCCESS ||
            vkCreateDescriptorSetLayout(DeviceHandle, &SetLayoutSpec, nullptr, &DisplaySetLayout) != VK_SUCCESS)
        {
            return false;
        }
    }
    {
        std::array<VkDescriptorSetLayoutBinding, 3> LayoutSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        }};
        VkDescriptorSetLayoutCreateInfo SetLayoutSpec {};
        SetLayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        SetLayoutSpec.bindingCount = static_cast<uint32_t>(LayoutSlots.size());
        SetLayoutSpec.pBindings    = LayoutSlots.data();
        if (vkCreateDescriptorSetLayout(DeviceHandle, &SetLayoutSpec, nullptr, &RasterSetLayout) != VK_SUCCESS)
        {
            return false;
        }
    }

    const auto CreatePipeLayout = [&](VkDescriptorSetLayout SetLayout, VkPipelineLayout* OutLayout) {
        VkPipelineLayoutCreateInfo CreateSpec {};
        CreateSpec.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        CreateSpec.setLayoutCount = 1u;
        CreateSpec.pSetLayouts    = &SetLayout;
        return vkCreatePipelineLayout(DeviceHandle, &CreateSpec, nullptr, OutLayout) == VK_SUCCESS;
    };
    if (!CreatePipeLayout(IntegrationSetLayout, &IntegrationPipeLayout) ||
        !CreatePipeLayout(ReconstructSetLayout, &ReconstructPipeLayout) ||
        !CreatePipeLayout(DisplaySetLayout, &DisplayPipeLayout) ||
        !CreatePipeLayout(RasterSetLayout, &RasterPipeLayout))
    {
        return false;
    }

    // 2. Descriptor pool & sets
    std::array<VkDescriptorPoolSize, 3> CapacityCounts {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 24u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 96u },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  16u },
    }};
    VkDescriptorPoolCreateInfo CapacitySpec {};
    CapacitySpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    CapacitySpec.maxSets       = 16u;
    CapacitySpec.poolSizeCount = static_cast<uint32_t>(CapacityCounts.size());
    CapacitySpec.pPoolSizes    = CapacityCounts.data();
    if (vkCreateDescriptorPool(DeviceHandle, &CapacitySpec, nullptr, &DescriptorAllocator) != VK_SUCCESS)
    {
        return false;
    }

    {
        std::array<VkDescriptorSetLayout, 3> Layouts { IntegrationSetLayout, IntegrationSetLayout, IntegrationSetLayout };
        VkDescriptorSetAllocateInfo Alloc {};
        Alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        Alloc.descriptorPool     = DescriptorAllocator;
        Alloc.descriptorSetCount = 3u;
        Alloc.pSetLayouts        = Layouts.data();
        if (vkAllocateDescriptorSets(DeviceHandle, &Alloc, IntegrationSets.data()) != VK_SUCCESS)
        {
            return false;
        }
    }
    {
        std::array<VkDescriptorSetLayout, 6> Layouts {};
        Layouts.fill(ReconstructSetLayout);
        VkDescriptorSetAllocateInfo Alloc {};
        Alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        Alloc.descriptorPool     = DescriptorAllocator;
        Alloc.descriptorSetCount = 6u;
        Alloc.pSetLayouts        = Layouts.data();
        if (vkAllocateDescriptorSets(DeviceHandle, &Alloc, ReconstructionSets.data()) != VK_SUCCESS)
        {
            return false;
        }
    }
    {
        std::array<VkDescriptorSetLayout, 2> Layouts { DisplaySetLayout, RasterSetLayout };
        std::array<VkDescriptorSet, 2>       Sets {};
        VkDescriptorSetAllocateInfo Alloc {};
        Alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        Alloc.descriptorPool     = DescriptorAllocator;
        Alloc.descriptorSetCount = 2u;
        Alloc.pSetLayouts        = Layouts.data();
        if (vkAllocateDescriptorSets(DeviceHandle, &Alloc, Sets.data()) != VK_SUCCESS)
        {
            return false;
        }
        DisplaySet = Sets[0];
        RasterSet  = Sets[1];
    }

    // 3. Compute shader programs
    const auto ConstructComputeProgram = [&](const char* SpvName, const char* EntryName, VkPipelineLayout Layout, VkPipeline* OutPipe) -> bool {
        VkShaderModule ShaderMod = Vulkan->LoadShaderBinary(CompiledShaderDirectory / SpvName);
        if (ShaderMod == VK_NULL_HANDLE)
        {
            if (OutDiagnostic != nullptr)
            {
                *OutDiagnostic = std::string("Missing compiled shader: ") + SpvName;
            }
            return false;
        }
        VkComputePipelineCreateInfo CreateSpec {};
        CreateSpec.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        CreateSpec.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        CreateSpec.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
        CreateSpec.stage.module       = ShaderMod;
        CreateSpec.stage.pName        = EntryName;
        CreateSpec.layout             = Layout;
        const VkResult Res      = vkCreateComputePipelines(DeviceHandle, VK_NULL_HANDLE, 1u, &CreateSpec, nullptr, OutPipe);
        vkDestroyShaderModule(DeviceHandle, ShaderMod, nullptr);
        if (Res != VK_SUCCESS && OutDiagnostic != nullptr)
        {
            *OutDiagnostic = std::string("Failed to create compute pipeline: ") + SpvName;
        }
        return Res == VK_SUCCESS;
    };

    if (!ConstructComputeProgram("DeterministicQuadratureExtract.comp.spv",   "ExtractQuadrature",      IntegrationPipeLayout, &QuadratureProgram) ||
        !ConstructComputeProgram("DisplayReconstructionGather.comp.spv",      "GatherEstimate",         ReconstructPipeLayout, &GatherProgram) ||
        !ConstructComputeProgram("DisplayReconstructionAtrous.comp.spv",      "FilterAtrous",           ReconstructPipeLayout, &AtrousProgram) ||
        !ConstructComputeProgram("DisplayToneTransform.comp.spv",             "ComputeEntry",           DisplayPipeLayout,     &DisplayProgram) ||
        !ConstructComputeProgram("DeterministicSurfaceFeatures.comp.spv",     "ResolveSurfaceFeatures", IntegrationPipeLayout, &FeaturesProgram) ||
        !ConstructComputeProgram("DeterministicHistoryReproject.comp.spv",    "ReprojectHistory",       IntegrationPipeLayout, &ReprojectProgram) ||
        !ConstructComputeProgram("DeterministicRadianceIntegration.comp.spv", "IntegrateRadiance",      IntegrationPipeLayout, &IntegrateProgram))
    {
        return false;
    }

    // 4. Visibility triangle-ID raster attachment target & pipeline (r32uint + d32_sfloat)
    std::array<VkAttachmentDescription, 2> Attachments {};
    Attachments[0].format         = VK_FORMAT_R32_UINT;
    Attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[0].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    Attachments[1].format         = VK_FORMAT_D32_SFLOAT;
    Attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ColorRef { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference DepthRef { 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription  Subpass {};
    Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount    = 1u;
    Subpass.pColorAttachments       = &ColorRef;
    Subpass.pDepthStencilAttachment = &DepthRef;

    VkRenderPassCreateInfo AttachmentSpec {};
    AttachmentSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    AttachmentSpec.attachmentCount = static_cast<uint32_t>(Attachments.size());
    AttachmentSpec.pAttachments    = Attachments.data();
    AttachmentSpec.subpassCount    = 1u;
    AttachmentSpec.pSubpasses      = &Subpass;
    if (vkCreateRenderPass(DeviceHandle, &AttachmentSpec, nullptr, &VisibilityAttachmentTarget) != VK_SUCCESS)
    {
        return false;
    }

    VkShaderModule VertMod = Vulkan->LoadShaderBinary(CompiledShaderDirectory / "VisibilityTriangleRasterization.vert.spv");
    VkShaderModule FragMod = Vulkan->LoadShaderBinary(CompiledShaderDirectory / "VisibilityTriangleRasterization.frag.spv");
    if (VertMod == VK_NULL_HANDLE || FragMod == VK_NULL_HANDLE)
    {
        if (VertMod != VK_NULL_HANDLE) vkDestroyShaderModule(DeviceHandle, VertMod, nullptr);
        if (FragMod != VK_NULL_HANDLE) vkDestroyShaderModule(DeviceHandle, FragMod, nullptr);
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = "Missing VisibilityTriangleRasterization SPIR-V shaders.";
        }
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> ShaderEntries {};
    ShaderEntries[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderEntries[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    ShaderEntries[0].module = VertMod;
    ShaderEntries[0].pName  = "VertexEntry";
    ShaderEntries[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderEntries[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    ShaderEntries[1].module = FragMod;
    ShaderEntries[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo   VertexInput { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0u, 0u, nullptr, 0u, nullptr };
    VkPipelineInputAssemblyStateCreateInfo InputAsm    { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0u, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo      ViewportSt  { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0u, 1u, nullptr, 1u, nullptr };

    VkPipelineRasterizationStateCreateInfo RasterSt {};
    RasterSt.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RasterSt.polygonMode = VK_POLYGON_MODE_FILL;
    RasterSt.cullMode    = VK_CULL_MODE_NONE;
    RasterSt.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RasterSt.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DepthSt {};
    DepthSt.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DepthSt.depthTestEnable  = VK_TRUE;
    DepthSt.depthWriteEnable = VK_TRUE;
    DepthSt.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState BlendAtt {};
    BlendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo ColorBlend {};
    ColorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColorBlend.attachmentCount = 1u;
    ColorBlend.pAttachments    = &BlendAtt;

    std::array<VkDynamicState, 2>    ViewportScissorSpec { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicSpec {};
    DynamicSpec.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynamicSpec.dynamicStateCount = static_cast<uint32_t>(ViewportScissorSpec.size());
    DynamicSpec.pDynamicStates    = ViewportScissorSpec.data();

    VkGraphicsPipelineCreateInfo ProgramSpec {};
    ProgramSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ProgramSpec.stageCount          = static_cast<uint32_t>(ShaderEntries.size());
    ProgramSpec.pStages             = ShaderEntries.data();
    ProgramSpec.pVertexInputState   = &VertexInput;
    ProgramSpec.pInputAssemblyState = &InputAsm;
    ProgramSpec.pViewportState      = &ViewportSt;
    ProgramSpec.pRasterizationState = &RasterSt;
    ProgramSpec.pMultisampleState   = &Multisample;
    ProgramSpec.pDepthStencilState  = &DepthSt;
    ProgramSpec.pColorBlendState    = &ColorBlend;
    ProgramSpec.pDynamicState       = &DynamicSpec;
    ProgramSpec.layout              = RasterPipeLayout;
    ProgramSpec.renderPass          = VisibilityAttachmentTarget;
    ProgramSpec.subpass             = 0u;

    const VkResult PipeRes = vkCreateGraphicsPipelines(DeviceHandle, VK_NULL_HANDLE, 1u, &ProgramSpec, nullptr, &VisibilityRasterPipe);
    vkDestroyShaderModule(DeviceHandle, VertMod, nullptr);
    vkDestroyShaderModule(DeviceHandle, FragMod, nullptr);
    return PipeRes == VK_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SCENE & ACCUMULATION SETUP
//------------------------------------------------------------------------------------------------------------------------

bool DeterministicPathTraceSequence::ConfigureStudioScene(const DeterministicTracerConfiguration& NextConfiguration)
{
    if (!ProgramsReady || !PrototypeVehicle.Built)
    {
        return false;
    }

    const auto SceneStart     = std::chrono::steady_clock::now();
    const bool WasCustomScene = CustomSceneActive;
    Configuration             = NextConfiguration;
    CustomSceneActive         = false;

    if (WasCustomScene)
    {
        Vulkan->ReleaseBuffer(TrianglePosBuffer);
        Vulkan->ReleaseBuffer(TriangleNormBuffer);
        Vulkan->ReleaseBuffer(BoundsEntryBuffer);
        UploadedVehicleCount = 0u;
    }

    const std::vector<CpuMaterialDefinition> Materials    = ConstructStudioMaterialSlots(Configuration);
    const std::vector<float>                 PackedMats   = PackGpuMaterialFloats(Materials);
    const std::vector<float>                 SphereFloats = ConstructShowcaseSphereFloats(ShowcaseSphereBounds);
    uint32_t                                 SoftboxCount = 0u;
    const std::vector<float>                 AreaFloats   = ConstructSoftboxLightFloats(Configuration, SoftboxCount);

    const uint32_t VehicleCount = std::clamp(Configuration.VehicleCount, 1u, 50u);
    if (TrianglePosBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        TrianglePosBuffer  = UploadHostVisibleStorageBuffer(*Vulkan, PrototypeVehicle.PositionFloats.data(), PrototypeVehicle.PositionFloats.size() * sizeof(float));
        TriangleNormBuffer = UploadHostVisibleStorageBuffer(*Vulkan, PrototypeVehicle.NormalFloats.data(),   PrototypeVehicle.NormalFloats.size() * sizeof(float));
    }

    const std::vector<VehicleBayPlacement> Bays = ResolveStudioParkingBays(VehicleCount, 19u, 68u);
    if (BoundsEntryBuffer.BufferHandle == VK_NULL_HANDLE || UploadedVehicleCount != VehicleCount)
    {
        const uint32_t VehicleWideCount = static_cast<uint32_t>(PrototypeVehicle.WideTree.WideEntries.size());
        std::vector<SpatialAcceleration::RayTrianglePrimitive> BayProxies(Bays.size());

        struct BayTransform2D
        {
            float Cosine = 1.0f;
            float Sine   = 0.0f;
            float X      = 0.0f;
            float Z      = 0.0f;
        };
        std::vector<BayTransform2D> BayTransforms(Bays.size());

        for (std::size_t I = 0u; I < Bays.size(); ++I)
        {
            float C = std::cos(Bays[I].YawRadians);
            float S = std::sin(Bays[I].YawRadians);
            if (std::abs(Bays[I].YawRadians) < 1.0e-6f) { C = 1.0f; S = 0.0f; }
            else if (std::abs(Bays[I].YawRadians - PiConstant) < 1.0e-5f) { C = -1.0f; S = 0.0f; }
            BayTransforms[I] = { C, S, Bays[I].BayX, Bays[I].BayZ };

            std::array<float, 3> Lower { 1.0e30f, 1.0e30f, 1.0e30f };
            std::array<float, 3> Upper { -1.0e30f, -1.0e30f, -1.0e30f };
            const auto&          B = PrototypeVehicle.LocalBounds;
            for (uint32_t Corner = 0u; Corner < 8u; ++Corner)
            {
                const float X = (Corner & 1u) ? B[3] : B[0];
                const float Y = (Corner & 2u) ? B[4] : B[1];
                const float Z = (Corner & 4u) ? B[5] : B[2];
                const std::array<float, 3> World {
                    C * X + S * Z + Bays[I].BayX,
                    Y,
                    -S * X + C * Z + Bays[I].BayZ,
                };
                for (std::size_t Axis = 0u; Axis < 3u; ++Axis)
                {
                    Lower[Axis] = std::min(Lower[Axis], World[Axis]);
                    Upper[Axis] = std::max(Upper[Axis], World[Axis]);
                }
            }
            for (std::size_t Axis = 0u; Axis < 3u; ++Axis)
            {
                const float Margin = 1.0e-5f * std::max({ 1.0f, std::abs(Lower[Axis]), std::abs(Upper[Axis]) });
                Lower[Axis] -= Margin;
                Upper[Axis] += Margin;
            }
            const Mathematics::Axis3D MinPt { Lower[0], Lower[1], Lower[2] };
            const Mathematics::Axis3D MaxPt { Upper[0], Upper[1], Upper[2] };
            const Mathematics::Axis3D MidPt = (MinPt + MaxPt) * 0.5f;
            BayProxies[I].Corner0 = MinPt;
            BayProxies[I].Edge01  = MaxPt - MinPt;
            BayProxies[I].Edge02  = MidPt - MinPt;
        }

        SpatialAcceleration::CompressedWideTree InstanceWideTree {};
        if (!InstanceWideTree.BuildFromPrimitives(BayProxies, {}, VehicleWideCount, 1u))
        {
            return false;
        }

        const uint32_t InstanceWideCount = static_cast<uint32_t>(InstanceWideTree.WideEntries.size());
        const uint32_t PlacementStartWords  = (VehicleWideCount + InstanceWideCount) * 16u;
        std::vector<uint32_t> CombinedWords(PlacementStartWords + static_cast<uint32_t>(Bays.size()) * 8u, 0u);
        std::memcpy(
            CombinedWords.data(),
            PrototypeVehicle.WideTree.WideEntries.data(),
            static_cast<std::size_t>(VehicleWideCount) * sizeof(SpatialAcceleration::CompressedWideEntry64));
        std::memcpy(
            CombinedWords.data() + static_cast<std::size_t>(VehicleWideCount) * 16u,
            InstanceWideTree.WideEntries.data(),
            static_cast<std::size_t>(InstanceWideCount) * sizeof(SpatialAcceleration::CompressedWideEntry64));

        for (std::size_t Slot = 0u; Slot < Bays.size(); ++Slot)
        {
            const uint32_t        SourceBay = (Slot < InstanceWideTree.SourceRemapOrder.size()) ? InstanceWideTree.SourceRemapOrder[Slot] : static_cast<uint32_t>(Slot);
            const auto&           Bay       = Bays[SourceBay];
            const BayTransform2D& T         = BayTransforms[SourceBay];
            const std::size_t     O         = PlacementStartWords + Slot * 8u;
            const std::array<float, 4> Row0 { T.Cosine, T.Sine, T.X, T.Z };
            std::memcpy(&CombinedWords[O], Row0.data(), sizeof(Row0));
            CombinedWords[O + 4u] = 0u; // Vehicle BLAS root
            CombinedWords[O + 5u] = 0u | (static_cast<uint32_t>(Bay.PaintSlotSwap) << 16u);
            CombinedWords[O + 6u] = 9u | (static_cast<uint32_t>(Bay.RimSlotSwap) << 16u);
            CombinedWords[O + 7u] = 0u;
        }

        Vulkan->ReleaseBuffer(BoundsEntryBuffer);
        BoundsEntryBuffer    = UploadHostVisibleStorageBuffer(*Vulkan, CombinedWords.data(), CombinedWords.size() * sizeof(uint32_t));
        InstanceRootEntry    = VehicleWideCount;
        PlacementStartVec4   = PlacementStartWords / 4u;
        UploadedVehicleCount = VehicleCount;
        RasterReferenceTris  = static_cast<uint32_t>(PrototypeVehicle.WideTree.Primitives.size());

        Summary.TriangleCount        = PrototypeVehicle.SourceTriCount * VehicleCount;
        Summary.VehicleTriangleCount = PrototypeVehicle.SourceTriCount;
        Summary.VehicleWideEntries   = VehicleWideCount;
        Summary.InstanceWideEntries  = InstanceWideCount;
        Summary.BoundsEntryCount     = VehicleWideCount + InstanceWideCount;
        Summary.BoundsDepthTier      = PrototypeVehicle.WideTree.MaxWideDepth + InstanceWideTree.MaxWideDepth;
        Summary.BoundsByteSize       = static_cast<uint32_t>(CombinedWords.size() * sizeof(uint32_t));
        Summary.VehicleCount         = VehicleCount;
    }

    const auto LampStart = std::chrono::steady_clock::now();
    const std::vector<VehicleBayPlacement> ActiveBays = Configuration.VehiclePresent ? Bays : std::vector<VehicleBayPlacement> {};
    const std::vector<PracticalLampPlacement> Lamps = ConstructFleetPracticalLamps(
        Configuration.PracticalMode,
        ActiveBays,
        PhotometryProfiles,
        Configuration.PracticalIntensity);
    uint32_t                             MaxLightDepth = 0u;
    const std::vector<LightClusterEntry> LightTree     = ConstructPhotometricLightTree(Lamps, &MaxLightDepth);
    const std::vector<float>             LightFloats   = PackUnifiedLightRegister(
        AreaFloats,
        SoftboxCount,
        Lamps,
        LightTree,
        PhotometryProfiles,
        &LightLayout);
    const auto LampEnd = std::chrono::steady_clock::now();

    Vulkan->ReleaseBuffer(MaterialRegisterBuffer);
    Vulkan->ReleaseBuffer(LightRegisterBuffer);
    Vulkan->ReleaseBuffer(SphereRegisterBuffer);
    MaterialRegisterBuffer = UploadHostVisibleStorageBuffer(*Vulkan, PackedMats.data(),   PackedMats.size() * sizeof(float));
    LightRegisterBuffer   = UploadHostVisibleStorageBuffer(*Vulkan, LightFloats.data(),  LightFloats.size() * sizeof(float));
    SphereRegisterBuffer   = UploadHostVisibleStorageBuffer(*Vulkan, SphereFloats.data(), SphereFloats.size() * sizeof(float));

    StudioFloorSlot               = 10u;
    Summary.SphereCount           = Configuration.ShowcaseSpheresPresent ? 7u : 0u;
    Summary.AreaLightCount        = SoftboxCount;
    Summary.PracticalLampCount    = LightLayout.LampCount;
    Summary.LightTreeClusterCount = LightLayout.ClusterCount;
    Summary.LightTreeDepth        = MaxLightDepth;
    Summary.MaterialSlotCount     = static_cast<uint32_t>(Materials.size());
    Summary.LightTreeBuildMillis  = std::chrono::duration<double, std::milli>(LampEnd - LampStart).count();
    Summary.SceneBuildMillis      = std::chrono::duration<double, std::milli>(LampEnd - SceneStart).count();

    if (AccumulationBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        if (!RebuildDescriptorSets())
        {
            return false;
        }
        ResetAccumulation(false);
    }
    return true;
}

bool DeterministicPathTraceSequence::ConfigureCustomWorldScene(
    const WorldSceneSpecification&          WorldScene,
    const DeterministicTracerConfiguration& TemplateConfiguration)
{
    if (!ProgramsReady)
    {
        return false;
    }

    Configuration     = TemplateConfiguration;
    CustomSceneActive = true;
    UploadedVehicleCount = 0u;

    const auto ToStudioPos = [](const Mathematics::Axis3D& P) noexcept -> Mathematics::Axis3D {
        return { P.X, P.Z, -P.Y };
    };
    const auto ToStudioDir = [](const Mathematics::Axis3D& D) noexcept -> Mathematics::Axis3D {
        return Mathematics::NormalizeAxis(Mathematics::Axis3D { D.X, D.Z, -D.Y }, { 0.0f, 1.0f, 0.0f });
    };

    std::vector<CpuMaterialDefinition>                      Materials {};
    std::vector<SpatialAcceleration::RayTrianglePrimitive>  Primitives {};
    std::vector<SpatialAcceleration::TriangleSmoothNormals> Normals {};

    const uint32_t RegisteredCount = std::max(1u, WorldScene.MaterialRegister.QuerySlotCount());
    Materials.reserve(RegisteredCount + 1u);
    for (uint32_t Slot = 0u; Slot < RegisteredCount; ++Slot)
    {
        const auto& SrcMat = WorldScene.MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Slot));
        CpuMaterialDefinition GpuMat {};
        if (SrcMat.Transmission.TransmissionWeight > 0.5f)
        {
            if (SrcMat.Transmission.ThinWalledSlab)
            {
                GpuMat = ConstructThinDielectric(
                    std::max(SrcMat.Specular.DielectricIOR, 1.05f),
                    { SrcMat.Transmission.AttenuationColor.R, SrcMat.Transmission.AttenuationColor.G, SrcMat.Transmission.AttenuationColor.B });
            }
            else
            {
                GpuMat = ConstructSolidDielectric(
                    std::max(SrcMat.Specular.DielectricIOR, 1.05f),
                    { 1.0f - SrcMat.Transmission.AttenuationColor.R, 1.0f - SrcMat.Transmission.AttenuationColor.G, 1.0f - SrcMat.Transmission.AttenuationColor.B });
            }
        }
        else if (SrcMat.Specular.Metalness > 0.5f)
        {
            GpuMat = ConstructGulbrandsenMetal(
                { SrcMat.Diffuse.BaseColor.R, SrcMat.Diffuse.BaseColor.G, SrcMat.Diffuse.BaseColor.B },
                { SrcMat.Specular.EdgeTintF82.R, SrcMat.Specular.EdgeTintF82.G, SrcMat.Specular.EdgeTintF82.B },
                std::clamp(SrcMat.Specular.Roughness, 0.02f, 1.0f));
        }
        else
        {
            std::array<float, 3> Exitance { 0.0f, 0.0f, 0.0f };
            if (SrcMat.PorosityLight.EmissionLuminanceNits > 0.0f)
            {
                const float EmScale = SrcMat.PorosityLight.EmissionLuminanceNits * 0.01f;
                Exitance            = { SrcMat.PorosityLight.EmissionTint.R * EmScale, SrcMat.PorosityLight.EmissionTint.G * EmScale, SrcMat.PorosityLight.EmissionTint.B * EmScale };
            }
            GpuMat = ConstructLayeredSurface(
                { SrcMat.Diffuse.BaseColor.R, SrcMat.Diffuse.BaseColor.G, SrcMat.Diffuse.BaseColor.B },
                std::clamp(SrcMat.Specular.Roughness, 0.02f, 1.0f),
                SrcMat.Clearcoat.CoatWeight,
                SrcMat.Clearcoat.CoatRoughness,
                Exitance);
        }
        Materials.push_back(GpuMat);
    }
    StudioFloorSlot = static_cast<uint32_t>(Materials.size());
    Materials.push_back(ConstructLayeredSurface({ 0.22f, 0.22f, 0.23f }, 0.65f));

    const auto& SrcPrims   = WorldScene.SceneTree.Primitives;
    const auto& SrcNormals = WorldScene.SceneTree.PrimitiveNormals;
    Primitives.reserve(SrcPrims.size());
    Normals.reserve(SrcPrims.size());
    for (std::size_t I = 0u; I < SrcPrims.size(); ++I)
    {
        const auto&               Src = SrcPrims[I];
        const Mathematics::Axis3D A   = ToStudioPos(Src.Corner0);
        const Mathematics::Axis3D B   = ToStudioPos(Src.Corner0 + Src.Edge01);
        const Mathematics::Axis3D C   = ToStudioPos(Src.Corner0 + Src.Edge02);
        const Mathematics::Axis3D E1  = B - A;
        const Mathematics::Axis3D E2  = C - A;
        const Mathematics::Axis3D N   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(E1, E2), { 0.0f, 1.0f, 0.0f });
        SpatialAcceleration::RayTrianglePrimitive Prim {};
        Prim.Corner0          = A;
        Prim.Edge01           = E1;
        Prim.Edge02           = E2;
        Prim.MaterialOrdinal  = static_cast<uint16_t>(std::min<uint32_t>(Src.MaterialOrdinal, RegisteredCount - 1u));
        Prim.OpaqueMask       = Src.OpaqueMask;
        Prim.PrimitiveOrdinal = static_cast<uint32_t>(I);
        Primitives.push_back(Prim);
        if (I < SrcNormals.size())
        {
            Normals.push_back(SpatialAcceleration::TriangleSmoothNormals {
                ToStudioDir(SrcNormals[I].Normal0),
                ToStudioDir(SrcNormals[I].Normal1),
                ToStudioDir(SrcNormals[I].Normal2),
            });
        }
        else
        {
            Normals.push_back(SpatialAcceleration::TriangleSmoothNormals { N, N, N });
        }
    }

    if (Primitives.empty())
    {
        SpatialAcceleration::RayTrianglePrimitive Dummy {};
        Dummy.Corner0    = { 0.0f, -100.0f, 0.0f };
        Dummy.Edge01     = { 0.01f, 0.0f, 0.0f };
        Dummy.Edge02     = { 0.0f, 0.0f, 0.01f };
        Dummy.OpaqueMask = 1u;
        Primitives.push_back(Dummy);
        const Mathematics::Axis3D UpN { 0.0f, 1.0f, 0.0f };
        Normals.push_back(SpatialAcceleration::TriangleSmoothNormals { UpN, UpN, UpN });
    }

    SpatialAcceleration::CompressedWideTree CustomTree {};
    if (!CustomTree.BuildFromPrimitives(Primitives, Normals, 0u, SpatialAcceleration::CompressedLeafPrimCeiling))
    {
        return false;
    }

    const std::size_t  ReorderedCount = CustomTree.Primitives.size();
    std::vector<float> PosFloats(ReorderedCount * 12u, 0.0f);
    std::vector<float> NormFloats(ReorderedCount * 12u, 0.0f);
    for (std::size_t Slot = 0u; Slot < ReorderedCount; ++Slot)
    {
        const auto&       Prim       = CustomTree.Primitives[Slot];
        const auto&       Norm       = CustomTree.PrimitiveNormals[Slot];
        const std::size_t O          = Slot * 12u;
        const uint32_t    PackedSlot = static_cast<uint32_t>(Prim.MaterialOrdinal) | ((Prim.OpaqueMask == 0u) ? ThinSheetPackedBit : 0u);
        PosFloats[O + 0u]            = Prim.Corner0.X;
        PosFloats[O + 1u]            = Prim.Corner0.Y;
        PosFloats[O + 2u]            = Prim.Corner0.Z;
        std::memcpy(&PosFloats[O + 3u], &PackedSlot, sizeof(uint32_t));
        PosFloats[O + 4u]            = Prim.Edge01.X;
        PosFloats[O + 5u]            = Prim.Edge01.Y;
        PosFloats[O + 6u]            = Prim.Edge01.Z;
        PosFloats[O + 8u]            = Prim.Edge02.X;
        PosFloats[O + 9u]            = Prim.Edge02.Y;
        PosFloats[O + 10u]           = Prim.Edge02.Z;

        NormFloats[O + 0u]     = Norm.Normal0.X;
        NormFloats[O + 1u]     = Norm.Normal0.Y;
        NormFloats[O + 2u]     = Norm.Normal0.Z;
        NormFloats[O + 4u]     = Norm.Normal1.X;
        NormFloats[O + 5u]     = Norm.Normal1.Y;
        NormFloats[O + 6u]     = Norm.Normal1.Z;
        NormFloats[O + 8u]     = Norm.Normal2.X;
        NormFloats[O + 9u]     = Norm.Normal2.Y;
        NormFloats[O + 10u]    = Norm.Normal2.Z;
    }

    const uint32_t        WideCount = static_cast<uint32_t>(CustomTree.WideEntries.size());
    std::vector<uint32_t> CombinedWords(WideCount * 16u + 8u, 0u);
    std::memcpy(CombinedWords.data(), CustomTree.WideEntries.data(), static_cast<std::size_t>(WideCount) * 64u);
    const std::array<float, 4> IdentityRow { 1.0f, 0.0f, 0.0f, 0.0f };
    std::memcpy(&CombinedWords[WideCount * 16u], IdentityRow.data(), sizeof(IdentityRow));
    CombinedWords[WideCount * 16u + 5u] = 0xFFFFFFFFu;
    CombinedWords[WideCount * 16u + 6u] = 0xFFFFFFFFu;

    std::vector<float> AreaFloats {};
    uint32_t           AreaCount = 0u;
    for (const PolygonalAreaQuadLight& Quad : WorldScene.AreaLights)
    {
        const Mathematics::Axis3D C     = ToStudioPos(Quad.CenterWorld);
        const Mathematics::Axis3D EdgeU = ToStudioDir(Quad.RightTangent) * (Quad.HalfWidthMeters * 2.0f);
        const Mathematics::Axis3D EdgeV = ToStudioDir(Quad.UpBitangent) * (Quad.HalfHeightMeters * 2.0f);
        const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(EdgeU, EdgeV);
        const float               Area  = Mathematics::EvaluateLength(Cross);
        if (Area <= 1.0e-6f)
        {
            continue;
        }
        const Mathematics::Axis3D UnitN = Cross * (1.0f / Area);
        const std::size_t         O     = AreaFloats.size();
        AreaFloats.resize(O + AreaLightFloatStride, 0.0f);
        AreaFloats[O + 0u]  = C.X;
        AreaFloats[O + 1u]  = C.Y;
        AreaFloats[O + 2u]  = C.Z;
        AreaFloats[O + 3u]  = Area;
        AreaFloats[O + 4u]  = EdgeU.X;
        AreaFloats[O + 5u]  = EdgeU.Y;
        AreaFloats[O + 6u]  = EdgeU.Z;
        AreaFloats[O + 7u]  = UnitN.X;
        AreaFloats[O + 8u]  = EdgeV.X;
        AreaFloats[O + 9u]  = EdgeV.Y;
        AreaFloats[O + 10u] = EdgeV.Z;
        AreaFloats[O + 11u] = UnitN.Y;
        AreaFloats[O + 12u] = Quad.EmittedRadiance.R;
        AreaFloats[O + 13u] = Quad.EmittedRadiance.G;
        AreaFloats[O + 14u] = Quad.EmittedRadiance.B;
        AreaFloats[O + 15u] = UnitN.Z;
        ++AreaCount;
    }
    if (AreaFloats.empty())
    {
        uint32_t SoftboxCount = 0u;
        AreaFloats = ConstructSoftboxLightFloats(Configuration, SoftboxCount);
        AreaCount  = SoftboxCount;
    }

    const std::vector<float> PackedMats = PackGpuMaterialFloats(Materials);
    const std::vector<float> DummySphere(SphereFloatStride, 0.0f);
    ShowcaseSphereBounds = { 0.0f, 0.0f, 0.0f, 0.0f };

    std::vector<PracticalLampPlacement> EmptyLamps {};
    std::vector<LightClusterEntry>      EmptyTree {};
    const std::vector<float> LightFloats = PackUnifiedLightRegister(AreaFloats, AreaCount, EmptyLamps, EmptyTree, PhotometryProfiles, &LightLayout);

    Vulkan->ReleaseBuffer(TrianglePosBuffer);
    Vulkan->ReleaseBuffer(TriangleNormBuffer);
    Vulkan->ReleaseBuffer(BoundsEntryBuffer);
    Vulkan->ReleaseBuffer(MaterialRegisterBuffer);
    Vulkan->ReleaseBuffer(LightRegisterBuffer);
    Vulkan->ReleaseBuffer(SphereRegisterBuffer);

    TrianglePosBuffer   = UploadHostVisibleStorageBuffer(*Vulkan, PosFloats.data(),     PosFloats.size() * sizeof(float));
    TriangleNormBuffer  = UploadHostVisibleStorageBuffer(*Vulkan, NormFloats.data(),    NormFloats.size() * sizeof(float));
    BoundsEntryBuffer   = UploadHostVisibleStorageBuffer(*Vulkan, CombinedWords.data(), CombinedWords.size() * sizeof(uint32_t));
    MaterialRegisterBuffer = UploadHostVisibleStorageBuffer(*Vulkan, PackedMats.data(),    PackedMats.size() * sizeof(float));
    LightRegisterBuffer   = UploadHostVisibleStorageBuffer(*Vulkan, LightFloats.data(),   LightFloats.size() * sizeof(float));
    SphereRegisterBuffer   = UploadHostVisibleStorageBuffer(*Vulkan, DummySphere.data(),   DummySphere.size() * sizeof(float));

    InstanceRootEntry          = 0u;
    PlacementStartVec4         = WideCount * 4u;
    UploadedVehicleCount       = 1u;
    RasterReferenceTris        = static_cast<uint32_t>(ReorderedCount);
    Configuration.VehicleCount = 1u;
    Configuration.ShowcaseSpheresPresent = false;

    Summary.TriangleCount      = static_cast<uint32_t>(ReorderedCount);
    Summary.BoundsEntryCount   = WideCount;
    Summary.BoundsByteSize     = static_cast<uint32_t>(CombinedWords.size() * sizeof(uint32_t));
    Summary.VehicleCount       = 1u;
    Summary.SphereCount        = 0u;
    Summary.AreaLightCount     = AreaCount;
    Summary.PracticalLampCount = 0u;
    Summary.MaterialSlotCount  = static_cast<uint32_t>(Materials.size());

    if (AccumulationBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        if (!RebuildDescriptorSets())
        {
            return false;
        }
        ResetAccumulation(false);
    }
    return true;
}

bool DeterministicPathTraceSequence::ResizeAccumulation(const uint32_t WidthPx, const uint32_t HeightPx)
{
    if (!ProgramsReady || WidthPx == 0u || HeightPx == 0u)
    {
        return false;
    }
    if (WidthPx == AccumulationWidth && HeightPx == AccumulationHeight && AccumulationBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        return true;
    }

    VkDevice DeviceHandle = Vulkan->AccessDevice();
    vkDeviceWaitIdle(DeviceHandle);

    if (VisibilityFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(DeviceHandle, VisibilityFramebuffer, nullptr);
        VisibilityFramebuffer = VK_NULL_HANDLE;
    }
    Vulkan->ReleaseImage2D(VisibilityDepthTexture);
    Vulkan->ReleaseImage2D(VisibilityIdTexture);
    Vulkan->ReleaseBuffer(DisplayRgba8Buffer);
    Vulkan->ReleaseBuffer(SurfaceFeatureBuffer);
    Vulkan->ReleaseBuffer(ReconstructionBuffer);
    Vulkan->ReleaseBuffer(AccumulationBuffer);

    AccumulationWidth  = WidthPx;
    AccumulationHeight = HeightPx;
    const uint64_t PixelCount  = static_cast<uint64_t>(WidthPx) * static_cast<uint64_t>(HeightPx);
    const uint64_t RegionBytes = PixelCount * 16ull;

    AccumulationBuffer = Vulkan->AllocateBuffer(
        RegionBytes * AccumulationRegionTotal,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    ReconstructionBuffer = Vulkan->AllocateBuffer(
        RegionBytes * ReconstructionRegionTotal,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    SurfaceFeatureBuffer = Vulkan->AllocateBuffer(
        RegionBytes * 2ull,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    DisplayRgba8Buffer = Vulkan->AllocateBuffer(
        PixelCount * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

    VisibilityIdTexture = Vulkan->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R32_UINT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    VisibilityDepthTexture = Vulkan->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    VkCommandBuffer Cmd = Vulkan->BeginSingleTimeCommands();
    TransitionImageLayout(Cmd, VisibilityIdTexture.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!AwaitDispatchedCommands(Cmd))
    {
        return false;
    }

    if (!RebuildVisibilityFramebuffer())
    {
        return false;
    }
    AccumulatedCameraValid = false;
    if (MaterialRegisterBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        if (!RebuildDescriptorSets())
        {
            return false;
        }
    }
    ResetAccumulation(false);
    return true;
}

bool DeterministicPathTraceSequence::RebuildVisibilityFramebuffer()
{
    std::array<VkImageView, 2> Views { VisibilityIdTexture.ViewHandle, VisibilityDepthTexture.ViewHandle };
    VkFramebufferCreateInfo    TargetBufferSpec {};
    TargetBufferSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    TargetBufferSpec.renderPass      = VisibilityAttachmentTarget;
    TargetBufferSpec.attachmentCount = static_cast<uint32_t>(Views.size());
    TargetBufferSpec.pAttachments    = Views.data();
    TargetBufferSpec.width           = AccumulationWidth;
    TargetBufferSpec.height          = AccumulationHeight;
    TargetBufferSpec.layers          = 1u;
    return vkCreateFramebuffer(Vulkan->AccessDevice(), &TargetBufferSpec, nullptr, &VisibilityFramebuffer) == VK_SUCCESS;
}

bool DeterministicPathTraceSequence::RebuildDescriptorSets()
{
    if (AccumulationBuffer.BufferHandle == VK_NULL_HANDLE || MaterialRegisterBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    VkDevice DeviceHandle = Vulkan->AccessDevice();
    const std::array<VkBuffer, 7> StorageHandles {
        BoundsEntryBuffer.BufferHandle,
        TrianglePosBuffer.BufferHandle,
        TriangleNormBuffer.BufferHandle,
        MaterialRegisterBuffer.BufferHandle,
        LightRegisterBuffer.BufferHandle,
        SphereRegisterBuffer.BufferHandle,
        AccumulationBuffer.BufferHandle,
    };

    for (uint32_t Slot = 0u; Slot < 3u; ++Slot)
    {
        VkDescriptorBufferInfo                 UniformDesc { TracingUniformBuffer.BufferHandle, Slot * TracingSlotStrideBytes, TracingUniformByteSize };
        std::array<VkDescriptorBufferInfo, 7>  StorageDesc {};
        for (std::size_t I = 0u; I < StorageHandles.size(); ++I)
        {
            StorageDesc[I] = { StorageHandles[I], 0ull, VK_WHOLE_SIZE };
        }
        VkDescriptorImageInfo  VisImageDesc   { VK_NULL_HANDLE, VisibilityIdTexture.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo  NoiseImageDesc { VK_NULL_HANDLE, BlueNoiseTexture.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorBufferInfo FeatureDesc    { SurfaceFeatureBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };

        std::array<VkWriteDescriptorSet, 11> Writes {};
        Writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, IntegrationSets[Slot], 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &UniformDesc, nullptr };
        for (uint32_t B = 1u; B <= 7u; ++B)
        {
            Writes[B] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, IntegrationSets[Slot], B, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &StorageDesc[B - 1u], nullptr };
        }
        Writes[8]  = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, IntegrationSets[Slot], 8u,  0u, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  &VisImageDesc,   nullptr, nullptr };
        Writes[9]  = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, IntegrationSets[Slot], 9u,  0u, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  &NoiseImageDesc, nullptr, nullptr };
        Writes[10] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, IntegrationSets[Slot], 10u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &FeatureDesc, nullptr };
        vkUpdateDescriptorSets(DeviceHandle, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);
    }

    for (uint32_t Slot = 0u; Slot < ReconstructStepTotal; ++Slot)
    {
        VkDescriptorBufferInfo UboDesc   { ReconstructUniformBuf.BufferHandle, Slot * ReconstructSlotStride, 64ull };
        VkDescriptorBufferInfo AccumDesc { AccumulationBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo FeatDesc  { SurfaceFeatureBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo EstDesc   { ReconstructionBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        std::array<VkWriteDescriptorSet, 4> Writes {{
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ReconstructionSets[Slot], 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &UboDesc,   nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ReconstructionSets[Slot], 1u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &AccumDesc, nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ReconstructionSets[Slot], 2u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &FeatDesc,  nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ReconstructionSets[Slot], 3u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &EstDesc,   nullptr },
        }};
        vkUpdateDescriptorSets(DeviceHandle, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);
    }

    {
        VkDescriptorBufferInfo UboDesc   { DisplayUniformBuffer.BufferHandle, 0ull, DisplayUniformByteSize };
        VkDescriptorBufferInfo AccumDesc { AccumulationBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo ReconDesc { ReconstructionBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo OutDesc   { DisplayRgba8Buffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        std::array<VkWriteDescriptorSet, 4> Writes {{
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DisplaySet, 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &UboDesc,   nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DisplaySet, 1u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &AccumDesc, nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DisplaySet, 2u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &ReconDesc, nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DisplaySet, 3u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &OutDesc,   nullptr },
        }};
        vkUpdateDescriptorSets(DeviceHandle, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);
    }

    {
        VkDescriptorBufferInfo UboDesc    { RasterUniformBuffer.BufferHandle, 0ull, RasterUniformByteSize };
        VkDescriptorBufferInfo PosDesc    { TrianglePosBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo BoundsDesc { BoundsEntryBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        std::array<VkWriteDescriptorSet, 3> Writes {{
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, RasterSet, 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &UboDesc,    nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, RasterSet, 1u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &PosDesc,    nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, RasterSet, 2u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &BoundsDesc, nullptr },
        }};
        vkUpdateDescriptorSets(DeviceHandle, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);
    }

    FeaturesStale = true;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              TRACING, REPROJECTION & RESOLVE
//------------------------------------------------------------------------------------------------------------------------

void DeterministicPathTraceSequence::ResetAccumulation(const bool CameraMoved)
{
    SampleIndexCursor    = 0u;
    Timings.ElapsedSeconds = 0.0;
    if (AccumulationBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return;
    }

    if (CameraMoved && Configuration.ReprojectionActive && AccumulatedCameraValid)
    {
        ReprojectionPending = true;
        return;
    }

    ReprojectionPending    = false;
    HistoryActive          = false;
    const float Aspect     = static_cast<float>(AccumulationWidth) / static_cast<float>(std::max(AccumulationHeight, 1u));
    AccumulatedCameraBasis = ResolveCameraBasis(Configuration.Camera, Aspect);
    AccumulatedCameraValid = true;
    FeaturesStale          = true;

    const std::size_t RegionBytes = static_cast<std::size_t>(AccumulationWidth) * static_cast<std::size_t>(AccumulationHeight) * 16u;
    auto*             RawBytes    = static_cast<uint8_t*>(AccumulationBuffer.HostAddress);
    if (RawBytes != nullptr)
    {
        std::memset(RawBytes, 0, RegionBytes);
        std::memset(RawBytes + 3u * RegionBytes, 0, RegionBytes);
    }
}

void DeterministicPathTraceSequence::UpdateCamera(const DeterministicOrbitCamera& NextCamera, const bool AllowReprojection)
{
    Configuration.Camera = NextCamera;
    ResetAccumulation(AllowReprojection);
}

uint32_t DeterministicPathTraceSequence::QueryActiveTraceScale() const noexcept
{
    return (Configuration.TraceScale == 2u || Configuration.TraceScale == 4u) ? Configuration.TraceScale : 1u;
}

float DeterministicPathTraceSequence::QueryEffectivePixelSamples() const noexcept
{
    const uint32_t Scale = QueryActiveTraceScale();
    return static_cast<float>(SampleIndexCursor) / static_cast<float>(Scale * Scale);
}

bool DeterministicPathTraceSequence::RasterizedVisibilityActive() const noexcept
{
    return Configuration.VisibilityRasterActive &&
           Configuration.ApertureRadius <= 0.0f &&
           Configuration.VehiclePresent &&
           QueryActiveTraceScale() == 1u;
}

bool DeterministicPathTraceSequence::QueryReconstructionActive() const noexcept
{
    const float DenoiseStrength = Configuration.DenoiseActive ? ResolveDenoiseStrength(QueryEffectivePixelSamples()) : 0.0f;
    return HistoryActive || (DenoiseStrength > 0.0f) || (QueryActiveTraceScale() > 1u);
}

std::string_view DeterministicPathTraceSequence::QueryAdapterName() const noexcept
{
    return (Vulkan != nullptr) ? Vulkan->QueryAdapterName() : std::string_view("Uninitialized");
}

void DeterministicPathTraceSequence::WriteTracingUniformSlot(
    const uint32_t                        FirstSample,
    const uint32_t                        SampleBatch,
    const DeterministicCameraBasis* const PreviousCamera,
    const uint32_t                        HistoryReadOffsetPx,
    const uint32_t                        HistoryWriteOffsetPx,
    const uint32_t                        SlotIndex)
{
    const float                    Aspect  = static_cast<float>(AccumulationWidth) / static_cast<float>(std::max(AccumulationHeight, 1u));
    const DeterministicCameraBasis Current = ResolveCameraBasis(Configuration.Camera, Aspect);
    const DeterministicCameraBasis Prev    = (PreviousCamera != nullptr) ? *PreviousCamera : Current;
    const float                    FocusD  = (Configuration.FocusDistance > 0.0f) ? Configuration.FocusDistance : Configuration.Camera.DistanceMeters;

    uint32_t LampCutLimit = 1u;
    switch (Configuration.LampSampling)
    {
        case LampSamplingRule::SingleTree: LampCutLimit = 1u; break;
        case LampSamplingRule::SplitFour:  LampCutLimit = 4u; break;
        case LampSamplingRule::SplitEight: LampCutLimit = 8u; break;
        case LampSamplingRule::AllLamps:   LampCutLimit = 1u; break;
    }

    std::array<uint32_t, 76> Words {};
    const auto SetFloat4 = [&](const std::size_t WordOffset, const float X, const float Y, const float Z, const float W) {
        const std::array<float, 4> V { X, Y, Z, W };
        std::memcpy(&Words[WordOffset], V.data(), sizeof(V));
    };
    const auto SetUint4 = [&](const std::size_t WordOffset, const uint32_t X, const uint32_t Y, const uint32_t Z, const uint32_t W) {
        Words[WordOffset + 0u] = X;
        Words[WordOffset + 1u] = Y;
        Words[WordOffset + 2u] = Z;
        Words[WordOffset + 3u] = W;
    };

    const float EnvIntensity = Configuration.EnvironmentIntensity * (Configuration.NightRigActive ? NightSkyDimFactor : 1.0f);
    SetFloat4(0u,  Current.Origin[0],  Current.Origin[1],  Current.Origin[2],  Configuration.ApertureRadius);
    SetFloat4(4u,  Current.Forward[0], Current.Forward[1], Current.Forward[2], FocusD);
    SetFloat4(8u,  Current.Right[0],   Current.Right[1],   Current.Right[2],   0.0f);
    SetFloat4(12u, Current.Up[0],      Current.Up[1],      Current.Up[2],      0.0f);
    SetFloat4(16u, 1.0f,               1.0f,               1.0f,               EnvIntensity);
    SetUint4(20u,  AccumulationWidth,  AccumulationHeight, FirstSample,        SampleBatch);
    SetUint4(24u,  Configuration.BounceLimit, Configuration.DepthLimit, Summary.AreaLightCount, Configuration.ShowcaseSpheresPresent ? Summary.SphereCount : 0u);
    SetUint4(28u,  StudioFloorSlot, static_cast<uint32_t>(Configuration.EnvironmentPreset), Summary.BoundsEntryCount, Configuration.VehiclePresent ? 1u : 0u);
    SetFloat4(32u, Configuration.ThroughputCutoff, StudioFloorRadiusMeters, Configuration.LightsVisibleToCamera ? 1.0f : 0.0f, 0.0f);
    SetFloat4(36u, Prev.Origin[0],  Prev.Origin[1],  Prev.Origin[2],  0.0f);
    SetFloat4(40u, Prev.Forward[0], Prev.Forward[1], Prev.Forward[2], 0.0f);
    SetFloat4(44u, Prev.Right[0],   Prev.Right[1],   Prev.Right[2],   0.0f);
    SetFloat4(48u, Prev.Up[0],      Prev.Up[1],      Prev.Up[2],      0.0f);
    SetUint4(52u,  static_cast<uint32_t>(Configuration.PointPattern), RasterizedVisibilityActive() ? 1u : 0u, HistoryReadOffsetPx, HistoryWriteOffsetPx);
    SetFloat4(56u, static_cast<float>(Configuration.HistoryCapacity), HistoryActive ? 1.0f : 0.0f, 0.0f, 0.0f);
    SetUint4(60u,  InstanceRootEntry, PlacementStartVec4, UploadedVehicleCount, QueryActiveTraceScale());
    SetFloat4(64u, 0.0f, 0.0f, (Configuration.LampSampling == LampSamplingRule::AllLamps) ? 1.0f : 0.0f, static_cast<float>(LampCutLimit));
    SetUint4(68u,  LightLayout.LampCount, LightLayout.ClusterSlotStart, LightLayout.LampSlotStart, LightLayout.ProfileSlotStart);
    SetFloat4(72u, ShowcaseSphereBounds[0], ShowcaseSphereBounds[1], ShowcaseSphereBounds[2], ShowcaseSphereBounds[3]);

    auto* Dst = static_cast<uint8_t*>(TracingUniformBuffer.HostAddress) + SlotIndex * TracingSlotStrideBytes;
    std::memcpy(Dst, Words.data(), TracingUniformByteSize);
}

void DeterministicPathTraceSequence::EncodeSumClear(const VkCommandBuffer Cmd)
{
    const VkDeviceSize RegionBytes = static_cast<VkDeviceSize>(AccumulationWidth) * static_cast<VkDeviceSize>(AccumulationHeight) * 16ull;
    vkCmdFillBuffer(Cmd, AccumulationBuffer.BufferHandle, 0ull, RegionBytes, 0u);
    vkCmdFillBuffer(Cmd, AccumulationBuffer.BufferHandle, 3ull * RegionBytes, RegionBytes, 0u);
    EncodeFullMemoryBarrier(Cmd);
}

void DeterministicPathTraceSequence::EncodePendingReprojection(const VkCommandBuffer Cmd)
{
    if (!ReprojectionPending)
    {
        return;
    }

    const uint32_t PixelCount   = AccumulationWidth * AccumulationHeight;
    const uint32_t TargetRegion = 3u - HistoryRegionIndex;
    WriteTracingUniformSlot(0u, 1u, &AccumulatedCameraBasis, HistoryRegionIndex * PixelCount, TargetRegion * PixelCount, 1u);

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ReprojectProgram);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, IntegrationPipeLayout, 0u, 1u, &IntegrationSets[1], 0u, nullptr);
    vkCmdDispatch(Cmd, (AccumulationWidth + 7u) / 8u, (AccumulationHeight + 7u) / 8u, 1u);
    EncodeFullMemoryBarrier(Cmd);

    EncodeSumClear(Cmd);
    FeaturesStale          = false;
    HistoryRegionIndex     = TargetRegion;
    HistoryActive          = true;
    ReprojectionPending    = false;
    const float Aspect     = static_cast<float>(AccumulationWidth) / static_cast<float>(std::max(AccumulationHeight, 1u));
    AccumulatedCameraBasis = ResolveCameraBasis(Configuration.Camera, Aspect);
    AccumulatedCameraValid = true;
}

void DeterministicPathTraceSequence::EncodeVisibilityRaster(const VkCommandBuffer Cmd, const uint32_t SampleIndex)
{
    const float                    Aspect = static_cast<float>(AccumulationWidth) / static_cast<float>(std::max(AccumulationHeight, 1u));
    const DeterministicCameraBasis Cam    = ResolveCameraBasis(Configuration.Camera, Aspect);

    std::array<uint32_t, 20> Words {};
    const auto SetF4 = [&](const std::size_t O, const float X, const float Y, const float Z, const float W) {
        const std::array<float, 4> V { X, Y, Z, W };
        std::memcpy(&Words[O], V.data(), sizeof(V));
    };
    SetF4(0u,  Cam.Origin[0],  Cam.Origin[1],  Cam.Origin[2],  0.0f);
    SetF4(4u,  Cam.Forward[0], Cam.Forward[1], Cam.Forward[2], 0.0f);
    SetF4(8u,  Cam.Right[0],   Cam.Right[1],   Cam.Right[2],   0.0f);
    SetF4(12u, Cam.Up[0],      Cam.Up[1],      Cam.Up[2],      0.0f);
    Words[16] = AccumulationWidth;
    Words[17] = AccumulationHeight;
    Words[18] = SampleIndex;
    Words[19] = PlacementStartVec4;
    std::memcpy(RasterUniformBuffer.HostAddress, Words.data(), RasterUniformByteSize);

    std::array<VkClearValue, 2> Clears {};
    Clears[0].color.uint32[0]     = 0u;
    Clears[1].depthStencil.depth  = 1.0f;

    VkRenderPassBeginInfo AttachmentBegin {};
    AttachmentBegin.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    AttachmentBegin.renderPass               = VisibilityAttachmentTarget;
    AttachmentBegin.framebuffer              = VisibilityFramebuffer;
    AttachmentBegin.renderArea.extent.width  = AccumulationWidth;
    AttachmentBegin.renderArea.extent.height = AccumulationHeight;
    AttachmentBegin.clearValueCount          = static_cast<uint32_t>(Clears.size());
    AttachmentBegin.pClearValues             = Clears.data();

    vkCmdBeginRenderPass(Cmd, &AttachmentBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport Viewport { 0.0f, 0.0f, static_cast<float>(AccumulationWidth), static_cast<float>(AccumulationHeight), 0.0f, 1.0f };
    VkRect2D   Scissor  { { 0, 0 }, { AccumulationWidth, AccumulationHeight } };
    vkCmdSetViewport(Cmd, 0u, 1u, &Viewport);
    vkCmdSetScissor(Cmd, 0u, 1u, &Scissor);
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VisibilityRasterPipe);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterPipeLayout, 0u, 1u, &RasterSet, 0u, nullptr);
    vkCmdDraw(Cmd, RasterReferenceTris * 3u, UploadedVehicleCount, 0u, 0u);
    vkCmdEndRenderPass(Cmd);
    EncodeFullMemoryBarrier(Cmd);
}

void DeterministicPathTraceSequence::EncodeDisplayReconstruction(const VkCommandBuffer Cmd)
{
    const uint32_t PixelCount = AccumulationWidth * AccumulationHeight;
    const uint32_t GroupsX    = (AccumulationWidth + 7u) / 8u;
    const uint32_t GroupsY    = (AccumulationHeight + 7u) / 8u;

    if (FeaturesStale)
    {
        WriteTracingUniformSlot(SampleIndexCursor, 0u, nullptr, 0u, 0u, 2u);
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, FeaturesProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, IntegrationPipeLayout, 0u, 1u, &IntegrationSets[2], 0u, nullptr);
        vkCmdDispatch(Cmd, GroupsX, GroupsY, 1u);
        EncodeFullMemoryBarrier(Cmd);
        FeaturesStale = false;
    }

    const float Strength      = Configuration.DenoiseActive ? ResolveDenoiseStrength(QueryEffectivePixelSamples()) : 0.0f;
    const bool  RunAtrous     = (Strength > 0.0f);
    const float Footprint     = 2.0f * std::tan(Configuration.Camera.FieldOfViewDegrees * PiConstant / 360.0f) / static_cast<float>(std::max(AccumulationHeight, 1u));
    const uint32_t HistoryOff = HistoryRegionIndex * PixelCount;

    const auto WriteReconSlot = [&](
                                    const uint32_t Slot,
                                    const uint32_t Step,
                                    const uint32_t ModeBits,
                                    const uint32_t ReadOffset,
                                    const uint32_t WriteOffset) {
        std::array<uint32_t, 16> Words {};
        Words[0] = AccumulationWidth;
        Words[1] = AccumulationHeight;
        Words[2] = Step;
        Words[3] = ModeBits;
        Words[4] = ReadOffset;
        Words[5] = WriteOffset;
        Words[6] = HistoryOff;
        Words[7] = SampleIndexCursor;

        const std::array<float, 8> Floats {
            Configuration.HistoryClampGamma,
            Configuration.HistorySpan,
            Strength,
            Configuration.DenoiseLuminanceSigma / std::max(QueryEffectivePixelSamples(), 1.0f),
            Footprint,
            64.0f,
            2.0f,
            static_cast<float>(QueryActiveTraceScale()),
        };
        std::memcpy(&Words[8], Floats.data(), sizeof(Floats));
        auto* Dst = static_cast<uint8_t*>(ReconstructUniformBuf.HostAddress) + Slot * ReconstructSlotStride;
        std::memcpy(Dst, Words.data(), sizeof(Words));
    };

    const uint32_t HistoryBit = HistoryActive ? 1u : 0u;
    WriteReconSlot(0u, 0u, HistoryBit | 4u | (RunAtrous ? 0u : 2u), 0u, RunAtrous ? 0u : (3u * PixelCount));

    if (RunAtrous)
    {
        uint32_t ReadRegion = 0u;
        for (std::size_t I = 0u; I < AtrousStepWidths.size(); ++I)
        {
            const bool     FinalStep   = (I + 1u == AtrousStepWidths.size());
            const uint32_t WriteRegion = FinalStep ? 3u : ((ReadRegion == 1u) ? 2u : 1u);
            WriteReconSlot(static_cast<uint32_t>(1u + I), AtrousStepWidths[I], FinalStep ? 2u : 0u, ReadRegion * PixelCount, WriteRegion * PixelCount);
            ReadRegion = WriteRegion;
        }
    }

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, GatherProgram);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ReconstructPipeLayout, 0u, 1u, &ReconstructionSets[0], 0u, nullptr);
    vkCmdDispatch(Cmd, GroupsX, GroupsY, 1u);
    EncodeFullMemoryBarrier(Cmd);

    if (RunAtrous)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, AtrousProgram);
        for (std::size_t I = 0u; I < AtrousStepWidths.size(); ++I)
        {
            vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ReconstructPipeLayout, 0u, 1u, &ReconstructionSets[1u + I], 0u, nullptr);
            vkCmdDispatch(Cmd, GroupsX, GroupsY, 1u);
            EncodeFullMemoryBarrier(Cmd);
        }
    }
}

bool DeterministicPathTraceSequence::AwaitDispatchedCommands(const VkCommandBuffer Cmd) const
{
    const uint64_t Tick = Vulkan->DispatchCommandBufferAsync(Cmd);
    if (Tick == 0ull)
    {
        return false;
    }
    const bool Ok = Vulkan->AwaitTimelineTick(Tick, 120000000000ull);
    if (!Ok)
    {
        (void)vkDeviceWaitIdle(Vulkan->AccessDevice());
    }
    Vulkan->FreeCommandBuffer(Cmd);
    return Ok;
}

bool DeterministicPathTraceSequence::AdvanceSamples(const uint32_t SampleBatch)
{
    if (!ProgramsReady || SampleBatch == 0u || AccumulationBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    const auto     DispatchStart = std::chrono::steady_clock::now();
    const bool     RasterVis     = RasterizedVisibilityActive();
    const uint32_t MaxChunk      = RasterVis ? 1u : 4u;
    const uint32_t Scale         = QueryActiveTraceScale();
    const uint32_t TilesX        = (AccumulationWidth + Scale - 1u) / Scale;
    const uint32_t TilesY        = (AccumulationHeight + Scale - 1u) / Scale;
    const uint32_t GroupsX       = (TilesX + 7u) / 8u;
    const uint32_t GroupsY       = (TilesY + 7u) / 8u;

    for (uint32_t Submitted = 0u; Submitted < SampleBatch; Submitted += MaxChunk)
    {
        const uint32_t FirstSample = SampleIndexCursor + Submitted;
        const uint32_t ChunkCount  = std::min(MaxChunk, SampleBatch - Submitted);

        VkCommandBuffer Cmd = Vulkan->BeginSingleTimeCommands();
        EncodePendingReprojection(Cmd);
        WriteTracingUniformSlot(FirstSample, ChunkCount, nullptr, 0u, 0u, 0u);
        if (RasterVis)
        {
            EncodeVisibilityRaster(Cmd, FirstSample);
        }
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, IntegrateProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, IntegrationPipeLayout, 0u, 1u, &IntegrationSets[0], 0u, nullptr);
        vkCmdDispatch(Cmd, GroupsX, GroupsY, 1u);
        EncodeFullMemoryBarrier(Cmd);
        if (!AwaitDispatchedCommands(Cmd))
        {
            return false;
        }
    }

    SampleIndexCursor += SampleBatch;
    const auto   DispatchEnd   = std::chrono::steady_clock::now();
    const double DispatchMs    = std::chrono::duration<double, std::milli>(DispatchEnd - DispatchStart).count();
    Timings.LastDispatchMillis = DispatchMs;
    Timings.TraceMillis        = DispatchMs;
    Timings.ElapsedSeconds    += DispatchMs * 0.001;
    return true;
}

bool DeterministicPathTraceSequence::ResolveDisplayRgba8(
    std::vector<uint8_t>& OutRgba8,
    const float           ExposureStopsOverride)
{
    if (!ProgramsReady || AccumulationBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    const auto     ResolveStart = std::chrono::steady_clock::now();
    VkCommandBuffer Cmd         = Vulkan->BeginSingleTimeCommands();
    EncodePendingReprojection(Cmd);

    const bool     Reconstructed = QueryReconstructionActive();
    const uint32_t PixelCount    = AccumulationWidth * AccumulationHeight;
    if (Reconstructed)
    {
        EncodeDisplayReconstruction(Cmd);
    }

    const float Stops        = (ExposureStopsOverride < 1.0e8f) ? ExposureStopsOverride : Configuration.ExposureStops;
    const float ExposureMult = std::pow(2.0f, Stops);
    std::array<uint32_t, 8> DisplayWords {};
    DisplayWords[0] = AccumulationWidth;
    DisplayWords[1] = AccumulationHeight;
    DisplayWords[2] = 0u; // ACES fitted
    DisplayWords[3] = Reconstructed ? (3u * PixelCount) : 0u;
    const std::array<float, 4> ExposureVec { ExposureMult, 0.0f, Reconstructed ? 1.0f : 0.0f, 0.0f };
    std::memcpy(&DisplayWords[4], ExposureVec.data(), sizeof(ExposureVec));
    std::memcpy(DisplayUniformBuffer.HostAddress, DisplayWords.data(), DisplayUniformByteSize);

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DisplayProgram);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DisplayPipeLayout, 0u, 1u, &DisplaySet, 0u, nullptr);
    vkCmdDispatch(Cmd, (AccumulationWidth + 7u) / 8u, (AccumulationHeight + 7u) / 8u, 1u);
    EncodeFullMemoryBarrier(Cmd);
    if (!AwaitDispatchedCommands(Cmd))
    {
        return false;
    }

    const std::size_t ByteCount = static_cast<std::size_t>(PixelCount) * 4u;
    OutRgba8.resize(ByteCount);
    std::memcpy(OutRgba8.data(), DisplayRgba8Buffer.HostAddress, ByteCount);

    const auto ResolveEnd  = std::chrono::steady_clock::now();
    Timings.DisplayMillis  = std::chrono::duration<double, std::milli>(ResolveEnd - ResolveStart).count();
    return true;
}

bool DeterministicPathTraceSequence::RetrieveRawAccumulation(std::vector<float>& OutRawAccumulation)
{
    if (!ProgramsReady || AccumulationBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }
    if (ReprojectionPending)
    {
        VkCommandBuffer Cmd = Vulkan->BeginSingleTimeCommands();
        EncodePendingReprojection(Cmd);
        if (!AwaitDispatchedCommands(Cmd))
        {
            return false;
        }
    }
    const std::size_t FloatCount = static_cast<std::size_t>(AccumulationWidth) * static_cast<std::size_t>(AccumulationHeight) * 4u;
    OutRawAccumulation.resize(FloatCount);
    std::memcpy(OutRawAccumulation.data(), AccumulationBuffer.HostAddress, FloatCount * sizeof(float));
    return true;
}

bool DeterministicPathTraceSequence::RetrieveDisplayLinearRgb(std::vector<float>& OutLinearRgb)
{
    if (!ProgramsReady || AccumulationBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }
    const uint32_t PixelCount    = AccumulationWidth * AccumulationHeight;
    const bool     Reconstructed = QueryReconstructionActive();
    if (ReprojectionPending || Reconstructed)
    {
        VkCommandBuffer Cmd = Vulkan->BeginSingleTimeCommands();
        EncodePendingReprojection(Cmd);
        if (Reconstructed)
        {
            EncodeDisplayReconstruction(Cmd);
        }
        if (!AwaitDispatchedCommands(Cmd))
        {
            return false;
        }
    }

    OutLinearRgb.resize(static_cast<std::size_t>(PixelCount) * 3u);
    if (Reconstructed)
    {
        const auto* Src = static_cast<const float*>(ReconstructionBuffer.HostAddress) + static_cast<std::size_t>(3u * PixelCount) * 4u;
        for (uint32_t P = 0u; P < PixelCount; ++P)
        {
            OutLinearRgb[P * 3u + 0u] = Src[P * 4u + 0u];
            OutLinearRgb[P * 3u + 1u] = Src[P * 4u + 1u];
            OutLinearRgb[P * 3u + 2u] = Src[P * 4u + 2u];
        }
    }
    else
    {
        const auto* Src = static_cast<const float*>(AccumulationBuffer.HostAddress);
        for (uint32_t P = 0u; P < PixelCount; ++P)
        {
            const float InvW          = 1.0f / std::max(Src[P * 4u + 3u], 1.0f);
            OutLinearRgb[P * 3u + 0u] = Src[P * 4u + 0u] * InvW;
            OutLinearRgb[P * 3u + 1u] = Src[P * 4u + 1u] * InvW;
            OutLinearRgb[P * 3u + 2u] = Src[P * 4u + 2u] * InvW;
        }
    }
    return true;
}

std::string DeterministicPathTraceSequence::ComputeAccumulationDigest()
{
    std::vector<float> Raw {};
    if (!RetrieveRawAccumulation(Raw))
    {
        return "00000000";
    }
    return DigestFloatSpan(Raw);
}

bool DeterministicPathTraceSequence::VerifyBitExactRepeat(
    const uint32_t     SampleTotal,
    const uint32_t     AlternateBatch,
    std::string* const OutPrimaryDigest,
    std::string* const OutRepeatDigest)
{
    if (!ProgramsReady || SampleTotal == 0u)
    {
        return false;
    }

    ResetAccumulation(false);
    for (uint32_t S = 0u; S < SampleTotal; ++S)
    {
        if (!AdvanceSamples(1u))
        {
            return false;
        }
    }
    const std::string PrimaryDigest = ComputeAccumulationDigest();

    ResetAccumulation(false);
    const uint32_t Step = std::max(AlternateBatch, 1u);
    for (uint32_t Done = 0u; Done < SampleTotal; Done += Step)
    {
        if (!AdvanceSamples(std::min(Step, SampleTotal - Done)))
        {
            return false;
        }
    }
    const std::string RepeatDigest = ComputeAccumulationDigest();

    if (OutPrimaryDigest != nullptr) *OutPrimaryDigest = PrimaryDigest;
    if (OutRepeatDigest != nullptr)  *OutRepeatDigest  = RepeatDigest;
    return PrimaryDigest == RepeatDigest;
}

std::vector<float> DeterministicPathTraceSequence::ExtractDeviceQuadrature(
    const uint32_t SampleTotal,
    const uint32_t PairTotal)
{
    const uint32_t TotalPairs = SampleTotal * PairTotal;
    if (!ProgramsReady || TotalPairs == 0u || AccumulationBuffer.ByteCapacity < static_cast<uint64_t>(TotalPairs) * 16ull)
    {
        return {};
    }

    std::array<uint32_t, 76> Words {};
    Words[20] = SampleTotal;
    Words[21] = PairTotal;
    std::memcpy(TracingUniformBuffer.HostAddress, Words.data(), TracingUniformByteSize);

    VkCommandBuffer Cmd = Vulkan->BeginSingleTimeCommands();
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, QuadratureProgram);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, IntegrationPipeLayout, 0u, 1u, &IntegrationSets[0], 0u, nullptr);
    vkCmdDispatch(Cmd, (TotalPairs + 63u) / 64u, 1u, 1u);
    EncodeFullMemoryBarrier(Cmd);
    if (!AwaitDispatchedCommands(Cmd))
    {
        return {};
    }

    std::vector<float> Points(static_cast<std::size_t>(TotalPairs) * 4u, 0.0f);
    std::memcpy(Points.data(), AccumulationBuffer.HostAddress, Points.size() * sizeof(float));
    ResetAccumulation(false);
    return Points;
}

} // namespace Frontier::GeometricRaster
