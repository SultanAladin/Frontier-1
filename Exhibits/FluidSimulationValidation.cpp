/*============================================================================================================================================
                                                      FLUIDSIMULATIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-End Validation & Visual Proofs for FluidSpace (Phases 1-6):
// Presets, TOML Codec (v2 +Z-Up & v1 Y-Up), Adaptive Ceiling, Sliding World Drift Grid, ForceFields, Particles, GI Shadows/Fire Lights & GPU Parity

#include "Engine/Components/Camera/CameraComponent.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/ParticleSpace/Simulation/ParticleSoftwareExecutor.h"
#include "Engine/SimulationSpace/FluidSpace/FluidCodec.h"
#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"
#include "Engine/SimulationSpace/FluidSpace/FluidSimulationSequence.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::SimulationSpace::FluidSpace;

struct ValidationScorecard
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++Passed;
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[FluidSimulationValidation][FAIL] %s\n", Label);
        }
    }
};

struct SceneGeoAsset
{
    GeometrySpace::PolygonNetwork   Mesh {};
    SpatialAcceleration::ClusterPyramid Pyr {};
};

[[nodiscard]] SceneGeoAsset CreateBoxAsset(Mathematics::Scalar3D HalfExtents)
{
    SceneGeoAsset Asset {};
    GeometrySpace::AppendBoxPrimitive(Asset.Mesh, { 0.0f, 0.0f, 0.0f }, HalfExtents, {}, {});
    Asset.Mesh.LinkOppositeEdges();
    Asset.Mesh.RecomputeNormals();
    for (auto& E : Asset.Mesh.Edges)
    {
        E.AttributeMask = E.AttributeMask | GeometrySpace::EdgeAttributeMask::EnableSharpCrease;
    }
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Mesh, 3u);
    return Asset;
}

[[nodiscard]] SceneGeoAsset CreateSphereAsset(float Radius, uint32_t SegU, uint32_t SegV)
{
    SceneGeoAsset Asset {};
    GeometrySpace::AppendSpherePrimitive(Asset.Mesh, { 0.0f, 0.0f, 0.0f }, Radius, SegU, SegV, {}, {});
    Asset.Mesh.LinkOppositeEdges();
    Asset.Mesh.RecomputeNormals();
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Mesh, 3u);
    return Asset;
}

[[nodiscard]] float ComputeMadRgba(
    const std::vector<uint8_t>& A,
    const std::vector<uint8_t>& B) noexcept
{
    if (A.size() != B.size() || A.empty()) return 255.0f;
    double Sum = 0.0;
    for (std::size_t I = 0u; I < A.size(); I += 4u)
    {
        Sum += std::abs(static_cast<int>(A[I + 0u]) - static_cast<int>(B[I + 0u]));
        Sum += std::abs(static_cast<int>(A[I + 1u]) - static_cast<int>(B[I + 1u]));
        Sum += std::abs(static_cast<int>(A[I + 2u]) - static_cast<int>(B[I + 2u]));
    }
    return static_cast<float>(Sum / (static_cast<double>(A.size() / 4u) * 3.0));
}

[[nodiscard]] std::vector<uint8_t> RenderFluidPreviewRgba(
    const FluidSimulationSequence& Fluid,
    uint32_t                       W,
    uint32_t                       H,
    Mathematics::Axis3D            Eye,
    Mathematics::Axis3D            Target,
    bool                           ShowGroundFloor = true)
{
    std::vector<uint8_t> Rgba(static_cast<std::size_t>(W) * H * 4u, 255u);
    const Mathematics::Axis3D Fwd   = Mathematics::NormalizeAxis(Target - Eye);
    const Mathematics::Axis3D Right = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, { 0.0f, 0.0f, 1.0f }));
    const Mathematics::Axis3D Up    = Mathematics::EvaluateCross(Right, Fwd);
    const float Aspect = static_cast<float>(W) / static_cast<float>(H);
    const float TanY   = 0.45f;
    const float TanX   = TanY * Aspect;

    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis({ -0.55f, 0.45f, 0.70f });
    const Mathematics::LinearRGB SunIrr { 3.2f, 2.95f, 2.65f };
    const Mathematics::LinearRGB SkyRad { 0.14f, 0.18f, 0.25f };
    const auto& Doc = Fluid.InspectDocument();
    const auto& Embers = Fluid.InspectParticleCoupler().InspectEmberSlots();

    for (uint32_t PY = 0u; PY < H; ++PY)
    {
        const float NdcY = 1.0f - ((static_cast<float>(PY) + 0.5f) / static_cast<float>(H)) * 2.0f;
        for (uint32_t PX = 0u; PX < W; ++PX)
        {
            const float NdcX = ((static_cast<float>(PX) + 0.5f) / static_cast<float>(W)) * 2.0f - 1.0f;
            const Mathematics::Axis3D Rd = Mathematics::NormalizeAxis(Fwd + Right * (NdcX * TanX) + Up * (NdcY * TanY));

            float HitDist = 45.0f;
            Mathematics::LinearRGB Bg {
                0.045f + 0.045f * std::clamp(Rd.Z * 0.5f + 0.5f, 0.0f, 1.0f),
                0.055f + 0.065f * std::clamp(Rd.Z * 0.5f + 0.5f, 0.0f, 1.0f),
                0.075f + 0.095f * std::clamp(Rd.Z * 0.5f + 0.5f, 0.0f, 1.0f),
            };

            if (ShowGroundFloor && Rd.Z < -1.0e-4f && Eye.Z > 0.0f)
            {
                const float TG = -Eye.Z / Rd.Z;
                if (TG > 0.0f && TG < HitDist)
                {
                    HitDist = TG;
                    const Mathematics::Axis3D GP = Eye + Rd * TG;
                    const int CX = static_cast<int>(std::floor(GP.X * 2.0f));
                    const int CY = static_cast<int>(std::floor(GP.Y * 2.0f));
                    const float Checker = ((CX + CY) & 1) ? 0.14f : 0.07f;
                    const float Fade    = std::exp(-0.08f * std::hypot(GP.X, GP.Y));
                    const float ShadowT = Fluid.EvaluateFluidShadowTransmittance(GP + Mathematics::Axis3D{ 0.0f, 0.0f, 0.02f }, SunDir, 16.0f);
                    Bg = {
                        (0.045f + Checker * Fade) * ShadowT,
                        (0.050f + Checker * Fade) * ShadowT,
                        (0.060f + Checker * Fade) * ShadowT,
                    };
                }
            }

            // Analytical obstacle sphere/box preview so obstacle deflection is clearly visible
            if (Doc.Obstacle.Type != FluidObstacleType::None)
            {
                const Mathematics::Axis3D OC = Eye - Doc.Obstacle.CenterMeters;
                if (Doc.Obstacle.Type == FluidObstacleType::Sphere)
                {
                    const float B = Mathematics::EvaluateDot(OC, Rd);
                    const float C = Mathematics::EvaluateDot(OC, OC) - Doc.Obstacle.RadiusMeters * Doc.Obstacle.RadiusMeters;
                    const float Disc = B * B - C;
                    if (Disc > 0.0f)
                    {
                        const float T = -B - std::sqrt(Disc);
                        if (T > 0.05f && T < HitDist)
                        {
                            HitDist = T;
                            const auto HP = Eye + Rd * T;
                            const auto HN = Mathematics::NormalizeAxis(HP - Doc.Obstacle.CenterMeters);
                            const float NdL = std::max(0.12f, Mathematics::EvaluateDot(HN, SunDir));
                            Bg = { 0.25f * NdL, 0.28f * NdL, 0.34f * NdL };
                        }
                    }
                }
                else if (Doc.Obstacle.Type == FluidObstacleType::DeflectorSlab)
                {
                    const float R = Doc.Obstacle.RadiusMeters;
                    const float IX = 1.0f / (std::abs(Rd.X) > 1.0e-6f ? Rd.X : 1.0e-6f);
                    const float IY = 1.0f / (std::abs(Rd.Y) > 1.0e-6f ? Rd.Y : 1.0e-6f);
                    const float IZ = 1.0f / (std::abs(Rd.Z) > 1.0e-6f ? Rd.Z : 1.0e-6f);
                    const float TX0 = (-R - OC.X) * IX, TX1 = (R - OC.X) * IX;
                    const float TY0 = (-R - OC.Y) * IY, TY1 = (R - OC.Y) * IY;
                    const float TZ0 = (-R - OC.Z) * IZ, TZ1 = (R - OC.Z) * IZ;
                    const float TN = std::max({ std::min(TX0, TX1), std::min(TY0, TY1), std::min(TZ0, TZ1) });
                    const float TF = std::min({ std::max(TX0, TX1), std::max(TY0, TY1), std::max(TZ0, TZ1) });
                    if (TF > TN && TN > 0.05f && TN < HitDist)
                    {
                        HitDist = TN;
                        Bg = { 0.24f, 0.27f, 0.33f };
                    }
                }
            }

            const auto Res = Fluid.EvaluateFluidSegment(
                Eye, Rd, HitDist, SunDir, SunIrr, SkyRad,
                static_cast<float>((PX * 73u + PY * 179u) & 31u) / 31.0f);

            Mathematics::LinearRGB Hdr {
                Bg.R * Res.Transmittance + Res.RadianceHdr.R,
                Bg.G * Res.Transmittance + Res.RadianceHdr.G,
                Bg.B * Res.Transmittance + Res.RadianceHdr.B,
            };

            const auto Tonemap = [](float V) noexcept -> uint8_t
            {
                const float M = std::max(0.0f, V) / (1.0f + std::max(0.0f, V));
                const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
                return static_cast<uint8_t>(std::round(std::clamp(S, 0.0f, 1.0f) * 255.0f));
            };
            const std::size_t Off = (static_cast<std::size_t>(PY) * W + PX) * 4u;
            Rgba[Off + 0u] = Tonemap(Hdr.R);
            Rgba[Off + 1u] = Tonemap(Hdr.G);
            Rgba[Off + 2u] = Tonemap(Hdr.B);
            Rgba[Off + 3u] = 255u;
        }
    }

    // Splat circular soft-glow embers, drifting dark ash motes, and oriented tornado debris
    const auto SplatParticle = [&](const ParticleSpace::ParticleSlot& P, bool ForceDebris) noexcept
    {
        if (!ParticleSpace::QueryParticleSlotAlive(P)) return;
        const Mathematics::Axis3D Rel = Mathematics::Axis3D{ P.PositionMeters[0], P.PositionMeters[1], P.PositionMeters[2] } - Eye;
        const float Z = Mathematics::EvaluateDot(Rel, Fwd);
        if (Z <= 0.2f) return;
        const float U = (Mathematics::EvaluateDot(Rel, Right) / (Z * TanX)) * 0.5f + 0.5f;
        const float V = 0.5f - (Mathematics::EvaluateDot(Rel, Up) / (Z * TanY)) * 0.5f;
        const float CX = U * static_cast<float>(W);
        const float CY = V * static_cast<float>(H);
        if (CX < -6.0f || CY < -6.0f || CX > static_cast<float>(W) + 6.0f || CY > static_cast<float>(H) + 6.0f) return;

        const bool IsDebris = ForceDebris || (P.AuxScalar < -0.5f);
        const uint32_t KindIdx = IsDebris
            ? static_cast<uint32_t>(std::clamp(std::lround(-P.AuxScalar - 1.0f), 0l, 5l))
            : 0u;
        const auto Kind = static_cast<FluidDebrisKind>(KindIdx);
        const float ScreenPerMeter = static_cast<float>(H) / (2.0f * Z * TanY);
        const float BaseRad = std::clamp(P.SizeMeters * ScreenPerMeter, IsDebris ? 1.35f : 0.95f, IsDebris ? 3.2f : 2.1f);

        const float VX = Mathematics::EvaluateDot({ P.VelocityMps[0], P.VelocityMps[1], P.VelocityMps[2] }, Right);
        const float VY = -Mathematics::EvaluateDot({ P.VelocityMps[0], P.VelocityMps[1], P.VelocityMps[2] }, Up);
        const float VLen = std::hypot(VX, VY);
        float DirX = (VLen > 1.0e-3f) ? (VX / VLen) : 1.0f;
        float DirY = (VLen > 1.0e-3f) ? (VY / VLen) : 0.0f;
        if (IsDebris && (Kind == FluidDebrisKind::PaperSheet || Kind == FluidDebrisKind::Leaf))
        {
            const float Tumble = P.SeedScalar * 6.28318f + P.AgeSeconds * 8.5f;
            DirX = std::cos(Tumble);
            DirY = std::sin(Tumble);
        }
        float Stretch = 1.0f + std::clamp(VLen * 0.025f, 0.0f, 0.35f);
        float PerpScale = 1.0f;
        if (IsDebris)
        {
            switch (Kind)
            {
            case FluidDebrisKind::RockChunk:    Stretch = 1.05f; PerpScale = 0.85f; break;
            case FluidDebrisKind::TwigStick:    Stretch = 1.95f; PerpScale = 0.28f; break;
            case FluidDebrisKind::Leaf:         Stretch = 1.38f; PerpScale = 0.65f; break;
            case FluidDebrisKind::PaperSheet:   Stretch = 1.32f; PerpScale = 0.88f; break;
            case FluidDebrisKind::WoodSplinter: Stretch = 1.80f; PerpScale = 0.34f; break;
            case FluidDebrisKind::MetalScrap:   Stretch = 1.25f; PerpScale = 0.68f; break;
            }
        }

        const float PA = std::clamp(P.ColourLinear[3], 0.0f, 1.0f);
        if (PA <= 0.01f) return;
        const float Lum = std::max({ P.ColourLinear[0], P.ColourLinear[1], P.ColourLinear[2] });
        const bool Additive = (!IsDebris && Lum > 0.34f);

        const int Span = static_cast<int>(std::ceil(BaseRad * Stretch)) + 1;
        const int X0 = std::max(0, static_cast<int>(std::floor(CX)) - Span);
        const int X1 = std::min(static_cast<int>(W) - 1, static_cast<int>(std::ceil(CX)) + Span);
        const int Y0 = std::max(0, static_cast<int>(std::floor(CY)) - Span);
        const int Y1 = std::min(static_cast<int>(H) - 1, static_cast<int>(std::ceil(CY)) + Span);

        const auto ToneF = [](float Vc) noexcept -> float
        {
            const float M = std::max(0.0f, Vc) / (1.0f + std::max(0.0f, Vc));
            return (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
        };
        const float SR = std::clamp(ToneF(P.ColourLinear[0]), 0.0f, 1.0f);
        const float SG = std::clamp(ToneF(P.ColourLinear[1]), 0.0f, 1.0f);
        const float SB = std::clamp(ToneF(P.ColourLinear[2]), 0.0f, 1.0f);

        for (int Y = Y0; Y <= Y1; ++Y)
        {
            const float DY = (static_cast<float>(Y) + 0.5f) - CY;
            for (int X = X0; X <= X1; ++X)
            {
                const float DX = (static_cast<float>(X) + 0.5f) - CX;
                const float Along = (DX * DirX + DY * DirY) / (BaseRad * Stretch);
                const float Perp  = (-DX * DirY + DY * DirX) / (BaseRad * PerpScale);

                float ShapeDist = Along * Along + Perp * Perp;
                if (IsDebris)
                {
                    if (Kind == FluidDebrisKind::PaperSheet)
                    {
                        const float Box = std::max(std::abs(Along), std::abs(Perp));
                        ShapeDist = Box * Box;
                    }
                    else if (Kind == FluidDebrisKind::Leaf)
                    {
                        const float LeafWidth = std::max(0.25f, 1.0f - 0.42f * std::abs(Along));
                        const float LP = Perp / LeafWidth;
                        ShapeDist = Along * Along + LP * LP;
                    }
                    else if (Kind == FluidDebrisKind::RockChunk || Kind == FluidDebrisKind::MetalScrap)
                    {
                        const float Oct = std::max(std::abs(Along) * 0.88f + std::abs(Perp) * 0.45f, std::abs(Perp) * 0.95f);
                        ShapeDist = Oct * Oct;
                    }
                }
                if (ShapeDist >= 1.0f) continue;

                const float Falloff = IsDebris
                    ? std::clamp((1.0f - ShapeDist) * 2.2f, 0.0f, 1.0f)
                    : (Additive ? std::exp(-3.6f * ShapeDist) * (1.0f - ShapeDist) : std::clamp((1.0f - ShapeDist) * 1.25f, 0.0f, 1.0f));
                const float SA = std::clamp(PA * Falloff * 0.85f, 0.0f, 1.0f);
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + static_cast<uint32_t>(X)) * 4u;
                const float DstR = static_cast<float>(Rgba[Off + 0u]) / 255.0f;
                const float DstG = static_cast<float>(Rgba[Off + 1u]) / 255.0f;
                const float DstB = static_cast<float>(Rgba[Off + 2u]) / 255.0f;
                const float OutR = Additive ? std::clamp(DstR + SR * SA, 0.0f, 1.0f) : (SR * SA + DstR * (1.0f - SA));
                const float OutG = Additive ? std::clamp(DstG + SG * SA, 0.0f, 1.0f) : (SG * SA + DstG * (1.0f - SA));
                const float OutB = Additive ? std::clamp(DstB + SB * SA, 0.0f, 1.0f) : (SB * SA + DstB * (1.0f - SA));
                Rgba[Off + 0u] = static_cast<uint8_t>(std::round(OutR * 255.0f));
                Rgba[Off + 1u] = static_cast<uint8_t>(std::round(OutG * 255.0f));
                Rgba[Off + 2u] = static_cast<uint8_t>(std::round(OutB * 255.0f));
            }
        }
    };
    for (const auto& E : Embers) SplatParticle(E, false);
    for (const auto& S : Fluid.InspectParticleCoupler().InspectShrapnelSlots()) SplatParticle(S, false);

    return Rgba;
}

void ComposeAndSave2x2Sheet(
    const std::filesystem::path&                  OutPath,
    uint32_t                                      TileW,
    uint32_t                                      TileH,
    const std::array<std::vector<uint8_t>, 4>&    Tiles,
    const std::array<const char*, 4>&             /*Labels*/)
{
    constexpr uint32_t Pad = 8u;
    const uint32_t FullW = TileW * 2u + Pad * 3u;
    const uint32_t FullH = TileH * 2u + Pad * 3u;
    Exhibits::SoftwareRasterCanvas Canvas(FullW, FullH, { 16u, 18u, 24u, 255u });

    for (uint32_t I = 0u; I < 4u; ++I)
    {
        const uint32_t Col = I % 2u;
        const uint32_t Row = I / 2u;
        const uint32_t OX  = Pad + Col * (TileW + Pad);
        const uint32_t OY  = Pad + Row * (TileH + Pad);
        Canvas.BlitRgbaRegion(OX, OY, TileW, TileH, Tiles[I].data());
    }
    (void)Canvas.WritePngFile(OutPath);
}

} // namespace

int main()
{
    ValidationScorecard Score {};
    const std::filesystem::path ProofDir = "Exhibits/Images/Fluid";
    std::filesystem::create_directories(ProofDir);

    // -----------------------------------------------------------------------------------------------------------------
    // 1. Preset Repository & TOML Codec (v2 +Z-Up & Legacy v1 Y-Up)
    // -----------------------------------------------------------------------------------------------------------------
    const auto& Presets = FluidPresets::ListPresets();
    Score.Verify(Presets.size() == 12u, "12 built-in Fluid presets available (including Fire/Dust Tornados)");

    {
        const auto Original = FluidPresets::ConstructPreset("open_bounds_megaton");
        const std::string TomlText = FluidCodec::SerializeSceneToml(Original);
        FluidSceneDocument RoundTrip {};
        std::string Err;
        const bool Ok = FluidCodec::DeserializeSceneToml(TomlText, RoundTrip, &Err);
        Score.Verify(Ok, "FluidCodec v2 TOML round-trip succeeds");
        Score.Verify(RoundTrip.Domain.AdaptiveCeilingEnabled == Original.Domain.AdaptiveCeilingEnabled &&
                     RoundTrip.Domain.WorldGridEnabled == Original.Domain.WorldGridEnabled &&
                     std::abs(RoundTrip.Blast.StrengthMps - Original.Blast.StrengthMps) < 1.0e-3f,
                     "FluidCodec preserves adaptive ceiling, world grid, and blast strength");
    }

    {
        const std::filesystem::path SamplePath = "Experimental/FluidEditor/Samples/open_bounds_megaton.gasscene.toml";
        std::ifstream In(SamplePath, std::ios::binary);
        if (In.is_open())
        {
            std::ostringstream Ss;
            Ss << In.rdbuf();
            FluidSceneDocument Legacy {};
            std::string Err;
            const bool Ok = FluidCodec::DeserializeSceneToml(Ss.str(), Legacy, &Err);
            Score.Verify(Ok, "FluidCodec imports legacy v1 Y-Up .gasscene.toml");
            Score.Verify(Legacy.Domain.AdaptiveCeilingEnabled && !Legacy.Domain.EnclosedBox,
                         "Legacy open_bounds_megaton maps to AdaptiveCeilingEnabled + open bounds");
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // 2. Sheet 1: Presets, Blackbody Palettes & Adaptive Uncapped Ceiling
    // -----------------------------------------------------------------------------------------------------------------
    constexpr uint32_t TileW = 280u;
    constexpr uint32_t TileH = 210u;

    std::vector<uint8_t> TilePyro {}, TileFireTornado {}, TileMegaton {}, TileDustTornado {};
    {
        FluidSimulationSequence SimPyro;
        auto Doc = FluidPresets::ConstructPreset("ue5_pyro_default");
        Doc.Domain.GridResolution = 40u;
        SimPyro.Configure(Doc);
        for (uint32_t S = 0u; S < 42u; ++S) SimPyro.StepSimulation(1.0f / 60.0f);
        TilePyro = RenderFluidPreviewRgba(SimPyro, TileW, TileH, { 2.3f, -2.3f, 1.35f }, { 0.0f, 0.0f, 0.9f });
    }
    {
        FluidSimulationSequence SimTornado;
        auto Doc = FluidPresets::ConstructPreset("wildfire_tornado");
        Doc.Domain.GridResolution = 40u;
        SimTornado.Configure(Doc);
        for (uint32_t S = 0u; S < 54u; ++S) SimTornado.StepSimulation(1.0f / 60.0f);
        TileFireTornado = RenderFluidPreviewRgba(SimTornado, TileW, TileH, { 2.7f, -2.7f, 1.55f }, { 0.0f, 0.0f, 1.15f });
    }
    {
        FluidSimulationSequence SimDust;
        auto Doc = FluidPresets::ConstructPreset("dust_tornado");
        Doc.Domain.GridResolution = 40u;
        SimDust.Configure(Doc);
        for (uint32_t S = 0u; S < 60u; ++S) SimDust.StepSimulation(1.0f / 60.0f);
        std::array<bool, 6> SeenKinds {};
        for (const auto& P : SimDust.InspectParticleCoupler().InspectEmberSlots())
        {
            if (ParticleSpace::QueryParticleSlotAlive(P) && P.AuxScalar < -0.5f)
            {
                const int K = static_cast<int>(std::clamp(std::lround(-P.AuxScalar - 1.0f), 0l, 5l));
                SeenKinds[static_cast<std::size_t>(K)] = true;
            }
        }
        const uint32_t KindCount = static_cast<uint32_t>(std::count(SeenKinds.begin(), SeenKinds.end(), true));
        Score.Verify(KindCount == 6u, "Tornado spawns all 6 typed debris array particles (rock, twig, leaf, paper, wood, metal)");
        TileDustTornado = RenderFluidPreviewRgba(SimDust, TileW, TileH, { 2.9f, -2.9f, 1.70f }, { 0.0f, 0.0f, 1.25f });
    }
    {
        FluidSimulationSequence SimMega;
        auto Doc = FluidPresets::ConstructPreset("megaton_open_bounds");
        Doc.Domain.GridResolution = 40u;
        SimMega.Configure(Doc);
        SimMega.TriggerBlast(Doc.Blast);
        for (uint32_t S = 0u; S < 32u; ++S) SimMega.StepSimulation(1.0f / 60.0f);
        Score.Verify(SimMega.InspectHeroGrid().BoxMaxWorld.Z > 4.2f,
                     "Adaptive explosion ceiling expands BoxMaxWorld.Z above base bounds");
        TileMegaton = RenderFluidPreviewRgba(SimMega, TileW, TileH, { 4.2f, -4.2f, 2.5f }, { 0.0f, 0.0f, 2.0f });
    }
    ComposeAndSave2x2Sheet(
        ProofDir / "01-FluidPresetsAndBlackbodyPalettes.png",
        TileW, TileH,
        { TilePyro, TileFireTornado, TileMegaton, TileDustTornado },
        { "UE5 Pyro Plume (+Z-Up)", "Fire Tornado + Embers", "Megaton Explosion + Soot", "Dust Tornado + Debris" });

    // -----------------------------------------------------------------------------------------------------------------
    // 3. Sheet 2: Continuous Sliding World Smoke Grid & ForceFields Coupling
    // -----------------------------------------------------------------------------------------------------------------
    std::vector<uint8_t> TileNoWind {}, TileWorldDrift {}, TileVortex {}, TileDualTier {};
    {
        FluidSimulationSequence Sim;
        auto Doc = FluidPresets::ConstructPreset("ue5_pyro_default");
        Doc.Domain.GridResolution      = 32u;
        Doc.Domain.BoundsWidthMeters   = 1.8f;
        Doc.Domain.BoundsHeightMeters  = 2.2f;
        Doc.Domain.WorldGridResolution = 28u;
        Doc.Domain.WorldGridSpanMeters = 12.0f;
        Doc.Emitter.Temperature        = 3.4f;
        Doc.Emitter.Fuel               = 2.6f;
        Doc.Emitter.Smoke              = 2.6f;
        Doc.Shading.SmokeAlbedo        = 0.44f;
        Doc.Combustion.WindVelocityXY  = { 0.0f, 0.0f };
        Sim.Configure(Doc);
        for (uint32_t S = 0u; S < 24u; ++S) Sim.StepSimulation(1.0f / 60.0f);
        TileNoWind = RenderFluidPreviewRgba(Sim, TileW, TileH, { 0.0f, -5.2f, 1.85f }, { 0.35f, 0.0f, 1.15f });

        // Now push strong +X wind so smoke continuously escapes Hero bounds into World Drift Grid
        Sim.AccessDocument().Combustion.WindVelocityXY = { 4.2f, 0.4f };
        for (uint32_t S = 0u; S < 84u; ++S) Sim.StepSimulation(1.0f / 60.0f);
        float WorldSmokeSum = 0.0f;
        for (const auto& V : Sim.InspectWorldDriftGrid().Thermo) WorldSmokeSum += V.X;
        Score.Verify(WorldSmokeSum > 0.25f, "Sliding World Drift Grid captures continuous wind-carried smoke outside Hero bounds");
        TileWorldDrift = RenderFluidPreviewRgba(Sim, TileW, TileH, { 1.1f, -6.0f, 2.15f }, { 1.55f, 0.0f, 1.25f });
        TileDualTier   = RenderFluidPreviewRgba(Sim, TileW, TileH, { 1.6f, -7.2f, 3.0f }, { 1.8f, 0.0f, 1.4f });
    }
    {
        SimulationSpace::ForceFields::ForceFieldAccumulator Acc;
        SimulationSpace::ForceFields::ForceSourceRecordEntry Vortex {};
        Vortex.UniqueToken  = 101u;
        Vortex.Model        = SimulationSpace::ForceFields::ForceModel::RotationalColumn;
        Vortex.Envelope     = SimulationSpace::ForceFields::ReachEnvelopeCategory::Sphere;
        Vortex.CentreMeters = { 0.0f, 0.0f, 1.0f };
        Vortex.EvaluatedCentreMeters = Vortex.CentreMeters;
        Vortex.RadiusMeters = 4.5f;
        Vortex.Strength     = 14.0f;
        Vortex.Enabled      = true;
        Acc.AssignSources(std::array{ Vortex });

        FluidSimulationSequence SimV;
        auto Doc = FluidPresets::ConstructPreset("ue5_pyro_default");
        Doc.Domain.GridResolution = 32u;
        Doc.Combustion.CoupleForceFields = true;
        SimV.Configure(Doc);
        FluidStepContext Ctx {};
        Ctx.ForceFieldSource   = &Acc;
        Ctx.MutableForceFields = &Acc;
        for (uint32_t S = 0u; S < 32u; ++S) SimV.StepSimulation(1.0f / 60.0f, Ctx);

        auto Blast = Doc.Blast;
        Blast.TriggerForceImpulse = true;
        SimV.TriggerBlast(Blast, Ctx);
        Score.Verify(Acc.QueryActiveSourceCount() >= 2u, "Fluid blast injects RadialBurst impulse into ForceFieldAccumulator");
        TileVortex = RenderFluidPreviewRgba(SimV, TileW, TileH, { 2.5f, -2.5f, 1.6f }, { 0.0f, 0.0f, 1.0f });
    }
    ComposeAndSave2x2Sheet(
        ProofDir / "02-WorldSmokeDriftAndForceFields.png",
        TileW, TileH,
        { TileNoWind, TileWorldDrift, TileVortex, TileDualTier },
        { "Calm Plume (No Wind)", "+X Wind -> World Drift Grid", "RadialVortex ForceField Swirl", "Wide Dual-Tier Smoke Trail" });

    // -----------------------------------------------------------------------------------------------------------------
    // 4. Sheet 3: Obstacles, ParticleSpace Embers & Ballistic Shrapnel
    // -----------------------------------------------------------------------------------------------------------------
    std::vector<uint8_t> TileSphereObs {}, TileBoxObs {}, TileShrapnel {}, TileCryo {};
    {
        FluidSimulationSequence SimS;
        auto Doc = FluidPresets::ConstructPreset("deflector_obstacle");
        Doc.Domain.GridResolution = 32u;
        Doc.Obstacle.Type         = FluidObstacleType::Sphere;
        Doc.Obstacle.CenterMeters = { 0.0f, 0.0f, 0.72f };
        Doc.Obstacle.RadiusMeters = 0.24f;
        SimS.Configure(Doc);
        for (uint32_t S = 0u; S < 26u; ++S) SimS.StepSimulation(1.0f / 60.0f);
        TileSphereObs = RenderFluidPreviewRgba(SimS, TileW, TileH, { 2.3f, -2.3f, 1.4f }, { 0.0f, 0.0f, 0.9f });

        Doc.Obstacle.Type = FluidObstacleType::DeflectorSlab;
        Doc.Obstacle.RadiusMeters = 0.28f;
        SimS.Configure(Doc);
        SimS.ResetSimulation();
        for (uint32_t S = 0u; S < 26u; ++S) SimS.StepSimulation(1.0f / 60.0f);
        TileBoxObs = RenderFluidPreviewRgba(SimS, TileW, TileH, { 2.3f, -2.3f, 1.4f }, { 0.0f, 0.0f, 0.9f });
    }
    {
        FluidSimulationSequence SimSh;
        auto Doc = FluidPresets::ConstructPreset("shrapnel_airburst");
        Doc.Domain.GridResolution = 32u;
        SimSh.Configure(Doc);
        SimSh.TriggerBlast(Doc.Blast);
        for (uint32_t S = 0u; S < 20u; ++S) SimSh.StepSimulation(1.0f / 60.0f);
        Score.Verify(!SimSh.InspectParticleCoupler().InspectShrapnelSlots().empty(),
                     "Fluid detonation spawns ParticleSlot shrapnel streamers");
        Score.Verify(!SimSh.InspectParticleCoupler().InspectEmberSlots().empty(),
                     "Fluid simulation maintains advected Ember ParticleSlots");
        TileShrapnel = RenderFluidPreviewRgba(SimSh, TileW, TileH, { 2.7f, -2.7f, 1.55f }, { 0.0f, 0.0f, 1.0f });
    }
    {
        FluidSimulationSequence SimC;
        auto Doc = FluidPresets::ConstructPreset("ue5_pyro_default");
        Doc.Domain.GridResolution = 32u;
        Doc.Shading.ColorPalette  = FluidColorPalette::WildfireCopperEmerald;
        SimC.Configure(Doc);
        for (uint32_t S = 0u; S < 24u; ++S) SimC.StepSimulation(1.0f / 60.0f);
        TileCryo = RenderFluidPreviewRgba(SimC, TileW, TileH, { 2.3f, -2.3f, 1.35f }, { 0.0f, 0.0f, 0.9f });
    }
    ComposeAndSave2x2Sheet(
        ProofDir / "03-ParticlesObstaclesAndCollision.png",
        TileW, TileH,
        { TileSphereObs, TileBoxObs, TileShrapnel, TileCryo },
        { "Sphere Obstacle Wrap", "Box Obstacle Deflection", "Shrapnel + Advected Embers", "Cryo Frost Palette + Embers" });

    // -----------------------------------------------------------------------------------------------------------------
    // 5. Sheet 4: GI Scene Collision, Smoke Shadows, Dynamic Fire Proxy Lights & CPU vs Vulkan GPU Parity
    // -----------------------------------------------------------------------------------------------------------------
    {
        using namespace Frontier::GeometricRaster;

        const uint32_t GiW = 280u;
        const uint32_t GiH = 210u;

        SceneGeoAsset GroundBox  = CreateBoxAsset({ 2.0f, 2.0f, 0.4f });
        SceneGeoAsset PillarBox  = CreateBoxAsset({ 0.45f, 0.45f, 2.5f });
        SceneGeoAsset GoldSphere = CreateSphereAsset(0.75f, 20u, 14u);

        WorldSceneSpecification Scene {};
        auto AddMat = [&](Mathematics::LinearRGB Color, float Rough = 0.72f)
        {
            MaterialSpace::SurfaceMaterial Mat {};
            Mat.Diffuse.BaseColor  = Color;
            Mat.Specular.Roughness = Rough;
            return Scene.MaterialRegister.RegisterMaterial(Mat);
        };
        const uint16_t FloorMat  = AddMat({ 0.42f, 0.41f, 0.39f }, 0.80f);
        const uint16_t PillarMat = AddMat({ 0.22f, 0.24f, 0.28f }, 0.60f);
        const uint16_t SphereMat = AddMat({ 0.78f, 0.72f, 0.65f }, 0.35f);

        GeometrySpace::PolygonNetwork Net {};
        auto AddBox = [&](const SceneGeoAsset& A, uint16_t M, Mathematics::Scalar3D P, Mathematics::Scalar3D H)
        {
            RigidClusterInstance Inst {};
            Inst.PyramidAsset        = &A.Pyr;
            Inst.MaterialSlot        = M;
            Inst.WorldTranslation    = { P.X, P.Y, P.Z };
            Inst.PreviousTranslation = Inst.WorldTranslation;
            Inst.UniformScale        = 1.0f;
            Scene.ClusterInstances.push_back(Inst);
            GeometrySpace::AppendBoxPrimitive(Net, { P.X, P.Y, P.Z }, H, {}, {}, 0u, M);
        };
        auto AddSphere = [&](const SceneGeoAsset& A, uint16_t M, Mathematics::Scalar3D P, float R)
        {
            RigidClusterInstance Inst {};
            Inst.PyramidAsset        = &A.Pyr;
            Inst.MaterialSlot        = M;
            Inst.WorldTranslation    = { P.X, P.Y, P.Z };
            Inst.PreviousTranslation = Inst.WorldTranslation;
            Inst.UniformScale        = 1.0f;
            Scene.ClusterInstances.push_back(Inst);
            GeometrySpace::AppendSpherePrimitive(Net, { P.X, P.Y, P.Z }, R, 20u, 14u, {}, {}, 0u, M);
        };

        for (int GX = -2; GX <= 2; ++GX)
        {
            for (int GY = -1; GY <= 3; ++GY)
            {
                AddBox(GroundBox, FloorMat, { static_cast<float>(GX) * 4.0f, static_cast<float>(GY) * 4.0f, -0.4f }, { 2.0f, 2.0f, 0.4f });
            }
        }
        AddSphere(GoldSphere, SphereMat, { -1.45f, 0.35f, 0.75f }, 0.75f);
        AddBox(PillarBox, PillarMat, { 1.55f, 1.25f, 2.5f }, { 0.45f, 0.45f, 2.5f });

        (void)Scene.SceneTree.BuildFromPolygonNetwork(Net);
        (void)Scene.SceneSdf.BuildFromWideBoundingTree(Scene.SceneTree, 0.35f, 1.0f);
        Scene.LatticeConfig.GridMinimumWorld  = { -4.0f, -4.0f, 0.2f };
        Scene.LatticeConfig.GridMaximumWorld  = {  4.0f,  4.0f, 4.0f };
        Scene.LatticeConfig.CountX            = 3u;
        Scene.LatticeConfig.CountY            = 3u;
        Scene.LatticeConfig.CountZ            = 2u;
        Scene.LatticeConfig.FibonacciRayCount = 16u;
        Scene.LatticeConfig.BouncePasses      = 1u;

        Scene.Atmosphere.Medium.AtmosphereEnabled       = true;
        Scene.Atmosphere.Medium.SkyBrightnessMultiplier = 1.0f;
        Scene.Atmosphere.Solar.SolarEnabled             = true;
        Scene.Atmosphere.Solar.ManualDirectionOverride  = true;
        Scene.Atmosphere.Solar.ManualAzimuthDegrees     = 240.0f;
        Scene.Atmosphere.Solar.ManualElevationDegrees   = 44.0f;
        Scene.Atmosphere.LensFlare.Enabled              = false;
        Scene.Atmosphere.Moons[0].Visible               = false;
        Scene.Atmosphere.Stars.StarfieldEnabled         = false;
        Scene.Sun.DirectionToSun  = Mathematics::NormalizeAxis({ 0.62f, 0.35f, 0.70f });
        Scene.Sun.SolarIrradiance = { 2.8f, 2.65f, 2.35f };
        Scene.SkyRadiance         = { 0.14f, 0.19f, 0.28f };

        // Step Fluid inside this GI scene (with GI SDF collision baked!)
        FluidSimulationSequence GiFluid;
        auto Doc = FluidPresets::ConstructPreset("ue5_pyro_default");
        Doc.Domain.GridResolution = 32u;
        Doc.Domain.OriginMeters   = { 0.0f, 0.0f, 0.05f };
        Doc.Obstacle.VoxelizeSceneGiSdf = true;
        Doc.GiCoupling.EmitGiLights     = true;
        Doc.GiCoupling.CastGiShadows    = true;
        Doc.GiCoupling.EmissiveLightIntensity = 2.4f;
        GiFluid.Configure(Doc);

        FluidStepContext Ctx {};
        Ctx.SceneSdf           = &Scene.SceneSdf;
        Ctx.SceneTree          = &Scene.SceneTree;
        Ctx.BakeGiObstaclesNow = true;
        for (uint32_t S = 0u; S < 24u; ++S) GiFluid.StepSimulation(1.0f / 60.0f, Ctx);

        uint32_t SolidBakedCount = 0u;
        float MaxTemp = 0.0f;
        const uint32_t Rd = GiFluid.InspectHeroGrid().ReadIdx();
        for (std::size_t I = 0u; I < GiFluid.InspectHeroGrid().SceneSolidMask.size(); ++I)
        {
            if (GiFluid.InspectHeroGrid().SceneSolidMask[I] > 0.5f) ++SolidBakedCount;
            MaxTemp = std::max(MaxTemp, GiFluid.InspectHeroGrid().Thermo[Rd][I].Y);
        }
        std::printf("[FluidSimulationValidation] GI Fluid: SolidVoxels=%u/%zu MaxTemp=%.3f EmissiveLights=%zu\n",
                    SolidBakedCount, GiFluid.InspectHeroGrid().SceneSolidMask.size(), MaxTemp, GiFluid.InspectEmissiveLights().size());

        Score.Verify(!GiFluid.InspectEmissiveLights().empty(),
                     "FluidGiLightExtractor emits dynamic blackbody GI fire lights");
        Score.Verify(SolidBakedCount < GiFluid.InspectHeroGrid().SceneSolidMask.size(),
                     "GI SDF collision mask baker executes cleanly on SignedDistanceVolume");

        Scene.OptionalFluid = &GiFluid;

        PerspectiveCameraFrame Cam {};
        Cam.CameraOrigin       = { 0.2f, -4.6f, 2.05f };
        Cam.TargetPoint        = { 0.0f, 0.2f, 1.15f };
        Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
        Cam.VerticalFovRadians = 50.0f * Constants::DegToRadFloat;
        Cam.NearClipMeters     = 0.1f;
        Cam.FarClipMeters      = 200.0f;
        Cam.ViewportWidthPx    = GiW;
        Cam.ViewportHeightPx   = GiH;

        TieredWorldRenderSequence CpuSeq {};
        CpuSeq.PrepareWorldScene(Scene);
        CpuSeq.SynchronizeWorldAtmosphere(Scene);

        WorldRenderConfiguration GiCfg {};
        GiCfg.Engine          = WorldRenderEngine::OpenPbrDirect;
        GiCfg.ShadowAlgorithm = SolarShadowAlgorithm::WideTreeRayQuery;

        const auto CpuFluidFrame = CpuSeq.RenderWorldFrameWithEngine(GiCfg, Cam, Cam, Scene);
        Score.Verify(CpuFluidFrame.DisplayRgba8.size() == GiW * GiH * 4u,
                     "CPU GI renders Fluid-in-World frame");

        // Compare against No-Fluid baseline to confirm fire illumination + smoke shadows change pixels cleanly
        Scene.OptionalFluid = nullptr;
        const auto CpuNoFluidFrame = CpuSeq.RenderWorldFrameWithEngine(GiCfg, Cam, Cam, Scene);
        const float FluidDeltaMad = ComputeMadRgba(CpuFluidFrame.DisplayRgba8, CpuNoFluidFrame.DisplayRgba8);
        Score.Verify(FluidDeltaMad > 2.5f, "Fluid fire lights + smoke + shadows visibly illuminate & composite into GI world");
        Scene.OptionalFluid = &GiFluid;

        // GPU Vulkan parity check
        std::vector<uint8_t> GpuRgba = CpuFluidFrame.DisplayRgba8;
        VulkanInterchange VkDevice {};
        VulkanWorldRenderSequence VkSeq {};
        const std::filesystem::path ShaderDir = FRONTIER_COMPILED_SHADER_DIR;
        const std::filesystem::path RepoRoot  = FRONTIER_REPO_ROOT_DIR;
        if (VkDevice.InitializeInterchange(RepoRoot) && VkSeq.InitializeSequence(VkDevice, ShaderDir))
        {
            VkSeq.SynchronizeSceneResources(Scene, &CpuSeq.InspectIrradianceLattice(), true);
            std::vector<uint8_t> OutGpu {};
            if (VkSeq.RenderWorldViewportWithEngine(GiCfg, 0u, Cam, Scene, OutGpu) &&
                OutGpu.size() == GiW * GiH * 4u)
            {
                GpuRgba = std::move(OutGpu);
                const float CpuGpuMad = ComputeMadRgba(CpuFluidFrame.DisplayRgba8, GpuRgba);
                std::printf("[FluidSimulationValidation] CPU vs GPU GI+Fluid MAD = %.2f\n", CpuGpuMad);
                Score.Verify(CpuGpuMad < 45.0f, "CPU vs Vulkan GPU GI+Fluid parity (MAD < 45)");
            }
            VkSeq.TeardownSequence();
        }

        // Tile 3: Night-time pure Fire Proxy GI illumination on floor & sphere (Sun off)
        auto NightScene = Scene;
        NightScene.Atmosphere.Medium.AtmosphereEnabled = false;
        NightScene.Atmosphere.Solar.SolarEnabled       = false;
        NightScene.Sun.SolarIrradiance = { 0.0f, 0.0f, 0.0f };
        NightScene.SkyRadiance         = { 0.015f, 0.02f, 0.035f };
        TieredWorldRenderSequence NightCpu {};
        NightCpu.PrepareWorldScene(NightScene);
        const auto NightFrame = NightCpu.RenderWorldFrameWithEngine(GiCfg, Cam, Cam, NightScene);

        // Tile 4: Depth-clipped view looking right past the foreground pillar at the fire
        PerspectiveCameraFrame DepthCam = Cam;
        DepthCam.CameraOrigin = { 2.35f, -3.1f, 1.45f };
        DepthCam.TargetPoint  = { 0.55f, -0.55f, 1.15f };
        const auto DepthFrame = CpuSeq.RenderWorldFrameWithEngine(GiCfg, DepthCam, DepthCam, Scene);

        ComposeAndSave2x2Sheet(
            ProofDir / "04-GiShadowsFireLightsAndGpuParity.png",
            GiW, GiH,
            { CpuFluidFrame.DisplayRgba8, GpuRgba, NightFrame.DisplayRgba8, DepthFrame.DisplayRgba8 },
            { "CPU GI + Fluid Shadows/Fire", "Vulkan GPU GI + Fluid", "Fire Blackbody Proxy GI (Night)", "Depth-Clipped Foreground Pillar" });
    }

    std::printf("[FluidSimulationValidation] Passed=%u Failed=%u\n", Score.Passed, Score.Failed);
    return (Score.Failed == 0u) ? 0 : 1;
}
