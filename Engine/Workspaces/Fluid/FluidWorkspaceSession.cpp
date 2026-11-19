/*============================================================================================================================================
                                                         FLUIDWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidWorkspaceSession: simulation stepping, ForceFields coupling, TOML I/O & transparent-alpha volumetric raymarch

#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] float ToneMapAcesApprox(float X) noexcept
{
    const float V = std::max(0.0f, X);
    return (V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f);
}

[[nodiscard]] uint8_t LinearToSrgb8(float Linear) noexcept
{
    const float C = std::clamp(Linear, 0.0f, 1.0f);
    const float S = (C <= 0.0031308f) ? (C * 12.92f) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::round(std::clamp(S, 0.0f, 1.0f) * 255.0f));
}

} // namespace

FluidWorkspaceSession::FluidWorkspaceSession()
{
    LoadPreset("ue5_pyro_default");
}

void FluidWorkspaceSession::LoadPreset(std::string_view PresetId)
{
    const auto Doc = SimulationSpace::FluidSpace::FluidPresets::ConstructPreset(PresetId);
    ActivePresetId = Doc.PresetKey;
    DocumentTitle  = Doc.SceneName;
    DocumentDirty  = false;
    Simulation.Configure(Doc);
    Simulation.ResetSimulation();

    if (!Doc.Emitter.Enabled)
    {
        TriggerBlastNow();
    }
    for (int I = 0; I < 72; ++I)
    {
        StepSimulation(1.0 / 60.0);
    }
    StatusMessage = "Simulation ready";
}

void FluidWorkspaceSession::ApplyCameraPreset(FluidCameraPreset Preset)
{
    CameraPreset = Preset;
    switch (Preset)
    {
    case FluidCameraPreset::Perspective:
        Camera.YawRadians     = 0.72f;
        Camera.PitchRadians   = 0.36f;
        Camera.DistanceMeters = 4.25f;
        break;
    case FluidCameraPreset::Front:
        Camera.YawRadians     = 0.0f;
        Camera.PitchRadians   = 0.08f;
        Camera.DistanceMeters = 4.10f;
        break;
    case FluidCameraPreset::Side:
        Camera.YawRadians     = 1.5707963f;
        Camera.PitchRadians   = 0.08f;
        Camera.DistanceMeters = 4.10f;
        break;
    case FluidCameraPreset::Top:
        Camera.YawRadians     = 0.0f;
        Camera.PitchRadians   = 1.38f;
        Camera.DistanceMeters = 4.40f;
        break;
    }
}

void FluidWorkspaceSession::TriggerBlastNow()
{
    auto Blast = Simulation.InspectDocument().Blast;
    if (SelectedBurstPattern == 1u)
    {
        Blast.Lobes = 8.0f;
        Blast.StrengthMps *= 1.35f;
    }
    else if (SelectedBurstPattern == 2u)
    {
        Blast.Lobes = 5.0f;
        Blast.RadiusMeters *= 1.3f;
    }
    else if (SelectedBurstPattern == 3u)
    {
        Blast.Lobes = 2.0f;
        Blast.StrengthMps *= 1.25f;
    }
    else if (SelectedBurstPattern == 4u)
    {
        Blast.StrengthMps *= 1.45f;
        Blast.Temperature *= 1.4f;
    }

    SimulationSpace::FluidSpace::FluidStepContext Ctx {};
    Ctx.ForceFieldSource   = &ForceFields;
    Ctx.MutableForceFields = &ForceFields;
    Ctx.WindLatticeCascade = &WindLattice;
    Simulation.TriggerBlast(Blast, Ctx);
    StatusMessage = "Triggered pyro burst";
}

void FluidWorkspaceSession::TriggerReset()
{
    Simulation.ResetSimulation();
    StatusMessage = "Simulation reset";
}

void FluidWorkspaceSession::StepSimulation(double DeltaSeconds)
{
    if (TurntableEnabled && !Simulation.InspectDocument().Domain.Paused)
    {
        Camera.YawRadians += static_cast<float>(DeltaSeconds) * 0.35f;
    }
    const float Dt = std::clamp(static_cast<float>(DeltaSeconds) * PlaybackSpeed, 1.0f / 180.0f, 1.0f / 24.0f);
    SimulationSpace::FluidSpace::FluidStepContext Ctx {};
    Ctx.ForceFieldSource   = &ForceFields;
    Ctx.MutableForceFields = &ForceFields;
    Ctx.WindLatticeCascade = &WindLattice;
    Ctx.WorldFocusPoint    = Simulation.InspectDocument().Domain.OriginMeters;
    Simulation.StepSimulation(Dt, Ctx);
}

void FluidWorkspaceSession::RenderPreviewFramebuffer(uint32_t WidthPx, uint32_t HeightPx)
{
    PreviewWidthPx  = std::clamp(WidthPx, 32u, 480u);
    PreviewHeightPx = std::clamp(HeightPx, 24u, 360u);
    PreviewRgba8.resize(static_cast<std::size_t>(PreviewWidthPx) * PreviewHeightPx * 4u);

    const float CosP = std::cos(Camera.PitchRadians);
    const Mathematics::Axis3D Eye {
        Camera.TargetWorld.X + Camera.DistanceMeters * CosP * std::cos(Camera.YawRadians),
        Camera.TargetWorld.Y + Camera.DistanceMeters * CosP * std::sin(Camera.YawRadians),
        Camera.TargetWorld.Z + Camera.DistanceMeters * std::sin(Camera.PitchRadians),
    };
    const Mathematics::Axis3D Forward = Mathematics::NormalizeAxis(Camera.TargetWorld - Eye);
    const Mathematics::Axis3D Right   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Forward, { 0.0f, 0.0f, 1.0f }));
    const Mathematics::Axis3D Up      = Mathematics::EvaluateCross(Right, Forward);

    const float Aspect = static_cast<float>(PreviewWidthPx) / static_cast<float>(PreviewHeightPx);
    const float TanY   = 0.44f;
    const float TanX   = TanY * Aspect;

    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis({ -0.55f, 0.45f, 0.70f });
    const Mathematics::LinearRGB SunIrr { 3.2f, 2.95f, 2.65f };
    const Mathematics::LinearRGB SkyRad { 0.14f, 0.18f, 0.25f };
    const float Exposure = std::max(0.25f, Simulation.InspectDocument().Shading.Exposure) * 0.58f;

    for (uint32_t PY = 0u; PY < PreviewHeightPx; ++PY)
    {
        const float NdcY = 1.0f - ((static_cast<float>(PY) + 0.5f) / static_cast<float>(PreviewHeightPx)) * 2.0f;
        for (uint32_t PX = 0u; PX < PreviewWidthPx; ++PX)
        {
            const float NdcX = ((static_cast<float>(PX) + 0.5f) / static_cast<float>(PreviewWidthPx)) * 2.0f - 1.0f;
            const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                Forward + Right * (NdcX * TanX) + Up * (NdcY * TanY));

            float HitDist = 28.0f;
            if (RayDir.Z < -1.0e-4f && Eye.Z > 0.0f)
            {
                const float TG = -Eye.Z / RayDir.Z;
                if (TG > 0.0f && TG < HitDist)
                {
                    HitDist = TG;
                }
            }

            const auto Res = Simulation.EvaluateFluidSegment(
                Eye, RayDir, HitDist, SunDir, SunIrr, SkyRad,
                static_cast<float>((PX * 73u + PY * 179u) & 31u) / 31.0f);

            const float SmokeOpacity = std::clamp(1.0f - Res.Transmittance, 0.0f, 1.0f);
            const float FireLum      = std::max({ Res.RadianceHdr.R, Res.RadianceHdr.G, Res.RadianceHdr.B }) * Exposure;
            const float Alpha        = std::clamp(std::max(SmokeOpacity * 0.92f, 1.0f - std::exp(-FireLum * 1.45f)), 0.0f, 0.98f);

            const Mathematics::LinearRGB FinalHdr {
                Res.RadianceHdr.R * Exposure,
                Res.RadianceHdr.G * Exposure,
                Res.RadianceHdr.B * Exposure,
            };

            const std::size_t Off = (static_cast<std::size_t>(PY) * PreviewWidthPx + PX) * 4u;
            // Straight-alpha RGB divided softly by Alpha for dark soot edges without darkening flame cores
            const float NormGain = (Alpha > 0.02f) ? std::min(1.65f, 1.0f / std::max(0.28f, Alpha)) : 1.0f;
            PreviewRgba8[Off + 0u] = LinearToSrgb8(ToneMapAcesApprox(FinalHdr.R * NormGain));
            PreviewRgba8[Off + 1u] = LinearToSrgb8(ToneMapAcesApprox(FinalHdr.G * NormGain));
            PreviewRgba8[Off + 2u] = LinearToSrgb8(ToneMapAcesApprox(FinalHdr.B * NormGain));
            PreviewRgba8[Off + 3u] = static_cast<uint8_t>(std::round(Alpha * 255.0f));
        }
    }

    // Composite circular soft-glow embers, drifting dark ash motes & oriented tornado debris directly into PreviewRgba8
    const auto CompositeParticle = [&](const ParticleSpace::ParticleSlot& P, bool ForceDebris) noexcept
    {
        if (!ParticleSpace::QueryParticleSlotAlive(P)) return;
        const Mathematics::Axis3D WorldPos { P.PositionMeters[0], P.PositionMeters[1], P.PositionMeters[2] };
        const Mathematics::Axis3D Rel = WorldPos - Eye;
        const float Z = Mathematics::EvaluateDot(Rel, Forward);
        if (Z <= 0.25f) return;

        const float U = (Mathematics::EvaluateDot(Rel, Right) / (Z * TanX)) * 0.5f + 0.5f;
        const float V = 0.5f - (Mathematics::EvaluateDot(Rel, Up) / (Z * TanY)) * 0.5f;
        const float CX = U * static_cast<float>(PreviewWidthPx);
        const float CY = V * static_cast<float>(PreviewHeightPx);
        if (CX < -8.0f || CY < -8.0f || CX > static_cast<float>(PreviewWidthPx) + 8.0f || CY > static_cast<float>(PreviewHeightPx) + 8.0f)
        {
            return;
        }

        const bool IsDebris = ForceDebris || (P.AuxScalar < -0.5f);
        const uint32_t CatIdx = IsDebris
            ? static_cast<uint32_t>(std::clamp(std::lround(-P.AuxScalar - 1.0f), 0l, 5l))
            : 0u;
        const auto Category = static_cast<SimulationSpace::FluidSpace::FluidDebrisCategory>(CatIdx);
        const float ScreenPerMeter = static_cast<float>(PreviewHeightPx) / (2.0f * Z * TanY);
        const float BaseRad = std::clamp(P.SizeMeters * ScreenPerMeter * 0.58f, IsDebris ? 0.75f : 0.65f, IsDebris ? 1.75f : 1.55f);

        const float VX = Mathematics::EvaluateDot({ P.VelocityMps[0], P.VelocityMps[1], P.VelocityMps[2] }, Right);
        const float VY = -Mathematics::EvaluateDot({ P.VelocityMps[0], P.VelocityMps[1], P.VelocityMps[2] }, Up);
        const float VLen = std::hypot(VX, VY);
        float DirX = (VLen > 1.0e-3f) ? (VX / VLen) : 1.0f;
        float DirY = (VLen > 1.0e-3f) ? (VY / VLen) : 0.0f;
        if (IsDebris && (Category == SimulationSpace::FluidSpace::FluidDebrisCategory::PaperSheet ||
                         Category == SimulationSpace::FluidSpace::FluidDebrisCategory::Leaf))
        {
            const float Tumble = P.SeedScalar * 6.28318f + P.AgeSeconds * 8.5f;
            DirX = std::cos(Tumble);
            DirY = std::sin(Tumble);
        }
        float Stretch = 1.0f + std::clamp(VLen * 0.025f, 0.0f, 0.35f);
        float PerpScale = 1.0f;
        if (IsDebris)
        {
            switch (Category)
            {
            case SimulationSpace::FluidSpace::FluidDebrisCategory::RockChunk:    Stretch = 1.05f; PerpScale = 0.85f; break;
            case SimulationSpace::FluidSpace::FluidDebrisCategory::TwigStick:    Stretch = 1.95f; PerpScale = 0.28f; break;
            case SimulationSpace::FluidSpace::FluidDebrisCategory::Leaf:         Stretch = 1.38f; PerpScale = 0.65f; break;
            case SimulationSpace::FluidSpace::FluidDebrisCategory::PaperSheet:   Stretch = 1.32f; PerpScale = 0.88f; break;
            case SimulationSpace::FluidSpace::FluidDebrisCategory::WoodSplinter: Stretch = 1.80f; PerpScale = 0.34f; break;
            case SimulationSpace::FluidSpace::FluidDebrisCategory::MetalScrap:   Stretch = 1.25f; PerpScale = 0.68f; break;
            }
        }

        const float R = P.ColourLinear[0];
        const float G = P.ColourLinear[1];
        const float B = P.ColourLinear[2];
        const float PA = std::clamp(P.ColourLinear[3], 0.0f, 1.0f);
        if (PA <= 0.01f) return;

        const float Lum = std::max({ R, G, B });
        const bool  AdditiveGlow = (!IsDebris && Lum > 0.34f);

        const int Span = static_cast<int>(std::ceil(BaseRad * Stretch)) + 1;
        const int X0 = std::max(0, static_cast<int>(std::floor(CX)) - Span);
        const int X1 = std::min(static_cast<int>(PreviewWidthPx) - 1, static_cast<int>(std::ceil(CX)) + Span);
        const int Y0 = std::max(0, static_cast<int>(std::floor(CY)) - Span);
        const int Y1 = std::min(static_cast<int>(PreviewHeightPx) - 1, static_cast<int>(std::ceil(CY)) + Span);

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
                    if (Category == SimulationSpace::FluidSpace::FluidDebrisCategory::PaperSheet)
                    {
                        const float Box = std::max(std::abs(Along), std::abs(Perp));
                        ShapeDist = Box * Box;
                    }
                    else if (Category == SimulationSpace::FluidSpace::FluidDebrisCategory::Leaf)
                    {
                        const float LeafWidth = std::max(0.25f, 1.0f - 0.42f * std::abs(Along));
                        const float LP = Perp / LeafWidth;
                        ShapeDist = Along * Along + LP * LP;
                    }
                    else if (Category == SimulationSpace::FluidSpace::FluidDebrisCategory::RockChunk ||
                             Category == SimulationSpace::FluidSpace::FluidDebrisCategory::MetalScrap)
                    {
                        const float Oct = std::max(std::abs(Along) * 0.88f + std::abs(Perp) * 0.45f, std::abs(Perp) * 0.95f);
                        ShapeDist = Oct * Oct;
                    }
                }
                if (ShapeDist >= 1.0f) continue;

                const float Falloff = IsDebris
                    ? std::clamp((1.0f - ShapeDist) * 2.2f, 0.0f, 1.0f)
                    : (AdditiveGlow ? std::exp(-3.6f * ShapeDist) * (1.0f - ShapeDist) : std::clamp((1.0f - ShapeDist) * 1.25f, 0.0f, 1.0f));
                const float SA = std::clamp(PA * Falloff * (AdditiveGlow ? 0.85f : 0.88f), 0.0f, 1.0f);
                if (SA <= 0.004f) continue;

                const std::size_t Off = (static_cast<std::size_t>(Y) * PreviewWidthPx + static_cast<uint32_t>(X)) * 4u;
                const float OldR = static_cast<float>(PreviewRgba8[Off + 0u]) / 255.0f;
                const float OldG = static_cast<float>(PreviewRgba8[Off + 1u]) / 255.0f;
                const float OldB = static_cast<float>(PreviewRgba8[Off + 2u]) / 255.0f;
                const float OldA = static_cast<float>(PreviewRgba8[Off + 3u]) / 255.0f;

                const float SrcR = std::clamp(ToneMapAcesApprox(R * (AdditiveGlow ? 1.15f : 0.9f)), 0.0f, 1.0f);
                const float SrcG = std::clamp(ToneMapAcesApprox(G * (AdditiveGlow ? 1.15f : 0.9f)), 0.0f, 1.0f);
                const float SrcB = std::clamp(ToneMapAcesApprox(B * (AdditiveGlow ? 1.15f : 0.9f)), 0.0f, 1.0f);

                float NewA = 0.0f, PreR = 0.0f, PreG = 0.0f, PreB = 0.0f;
                if (AdditiveGlow)
                {
                    NewA = std::clamp(OldA + SA, 0.0f, 1.0f);
                    PreR = OldR * OldA + SrcR * SA;
                    PreG = OldG * OldA + SrcG * SA;
                    PreB = OldB * OldA + SrcB * SA;
                }
                else
                {
                    NewA = std::clamp(SA + OldA * (1.0f - SA), 0.0f, 1.0f);
                    PreR = SrcR * SA + OldR * OldA * (1.0f - SA);
                    PreG = SrcG * SA + OldG * OldA * (1.0f - SA);
                    PreB = SrcB * SA + OldB * OldA * (1.0f - SA);
                }

                if (NewA > 1.0e-4f)
                {
                    PreviewRgba8[Off + 0u] = static_cast<uint8_t>(std::round(std::clamp(PreR / NewA, 0.0f, 1.0f) * 255.0f));
                    PreviewRgba8[Off + 1u] = static_cast<uint8_t>(std::round(std::clamp(PreG / NewA, 0.0f, 1.0f) * 255.0f));
                    PreviewRgba8[Off + 2u] = static_cast<uint8_t>(std::round(std::clamp(PreB / NewA, 0.0f, 1.0f) * 255.0f));
                    PreviewRgba8[Off + 3u] = static_cast<uint8_t>(std::round(NewA * 255.0f));
                }
            }
        }
    };

    for (const auto& E : Simulation.InspectParticleCoupler().InspectEmberSlots())
    {
        CompositeParticle(E, false);
    }
    for (const auto& S : Simulation.InspectParticleCoupler().InspectShrapnelSlots())
    {
        CompositeParticle(S, false);
    }
}

bool FluidWorkspaceSession::ExportSceneToml(std::string_view FilePath) const
{
    const std::string Toml = SimulationSpace::FluidSpace::FluidCodec::SerializeSceneToml(
        Simulation.InspectDocument());
    std::ofstream Out(std::string(FilePath), std::ios::binary);
    if (!Out.is_open()) return false;
    Out.write(Toml.data(), static_cast<std::streamsize>(Toml.size()));
    return Out.good();
}

bool FluidWorkspaceSession::ImportSceneToml(std::string_view FilePath)
{
    std::ifstream In(std::string(FilePath), std::ios::binary);
    if (!In.is_open()) return false;
    std::ostringstream Buffer;
    Buffer << In.rdbuf();

    SimulationSpace::FluidSpace::FluidSceneDocument Parsed {};
    std::string Err;
    if (!SimulationSpace::FluidSpace::FluidCodec::DeserializeSceneToml(Buffer.str(), Parsed, &Err))
    {
        StatusMessage = "TOML import failed: " + Err;
        return false;
    }

    ActivePresetId = Parsed.PresetKey;
    DocumentTitle  = Parsed.SceneName;
    DocumentDirty  = false;
    Simulation.Configure(Parsed);
    Simulation.ResetSimulation();
    StatusMessage = "Imported TOML: " + Parsed.SceneName;
    return true;
}

} // namespace Frontier::Workspaces
