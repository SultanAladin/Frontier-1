/*============================================================================================================================================
                                                         FLUIDWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidWorkspaceSession: simulation stepping, ForceFields coupling, TOML I/O & volumetric preview rendering

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

[[nodiscard]] float ToneMapReinhard(float X) noexcept
{
    const float V = std::max(0.0f, X);
    return V / (1.0f + V);
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
    Simulation.Configure(Doc);
    Simulation.ResetSimulation();

    // Seed an initial warm step or blast if the preset disables continuous emitter
    if (!Doc.Emitter.Enabled)
    {
        TriggerBlastNow();
    }
    StepSimulation(1.0 / 60.0);
    StatusMessage = "Loaded preset: " + Doc.SceneName;
}

void FluidWorkspaceSession::TriggerBlastNow()
{
    const auto& Doc = Simulation.InspectDocument();
    SimulationSpace::FluidSpace::FluidStepContext Ctx {};
    Ctx.ForceFieldSource   = &ForceFields;
    Ctx.MutableForceFields = &ForceFields;
    Ctx.WindLatticeCascade = &WindLattice;
    Simulation.TriggerBlast(Doc.Blast, Ctx);
    StatusMessage = "Detonated multi-lobe Rayleigh-Taylor blast";
}

void FluidWorkspaceSession::TriggerReset()
{
    Simulation.ResetSimulation();
    StatusMessage = "Simulation buffers cleared";
}

void FluidWorkspaceSession::StepSimulation(double DeltaSeconds)
{
    const float Dt = std::clamp(static_cast<float>(DeltaSeconds), 1.0f / 120.0f, 1.0f / 30.0f);
    SimulationSpace::FluidSpace::FluidStepContext Ctx {};
    Ctx.ForceFieldSource   = &ForceFields;
    Ctx.MutableForceFields = &ForceFields;
    Ctx.WindLatticeCascade = &WindLattice;
    Ctx.WorldFocusPoint    = Simulation.InspectDocument().Domain.OriginMeters;
    Simulation.StepSimulation(Dt, Ctx);
}

void FluidWorkspaceSession::RenderPreviewFramebuffer(uint32_t WidthPx, uint32_t HeightPx)
{
    PreviewWidthPx  = std::clamp(WidthPx, 32u, 320u);
    PreviewHeightPx = std::clamp(HeightPx, 24u, 240u);
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

    for (uint32_t PY = 0u; PY < PreviewHeightPx; ++PY)
    {
        const float NdcY = 1.0f - ((static_cast<float>(PY) + 0.5f) / static_cast<float>(PreviewHeightPx)) * 2.0f;
        for (uint32_t PX = 0u; PX < PreviewWidthPx; ++PX)
        {
            const float NdcX = ((static_cast<float>(PX) + 0.5f) / static_cast<float>(PreviewWidthPx)) * 2.0f - 1.0f;
            const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                Forward + Right * (NdcX * TanX) + Up * (NdcY * TanY));

            float HitDist = 45.0f;
            Mathematics::LinearRGB Bg {
                0.045f + 0.055f * std::clamp(RayDir.Z * 0.5f + 0.5f, 0.0f, 1.0f),
                0.055f + 0.075f * std::clamp(RayDir.Z * 0.5f + 0.5f, 0.0f, 1.0f),
                0.075f + 0.105f * std::clamp(RayDir.Z * 0.5f + 0.5f, 0.0f, 1.0f),
            };

            // Ground floor plane at Z = 0
            if (RayDir.Z < -1.0e-4f && Eye.Z > 0.0f)
            {
                const float TG = -Eye.Z / RayDir.Z;
                if (TG > 0.0f && TG < HitDist)
                {
                    HitDist = TG;
                    const Mathematics::Axis3D GP = Eye + RayDir * TG;
                    const int CX = static_cast<int>(std::floor(GP.X * 2.0f));
                    const int CY = static_cast<int>(std::floor(GP.Y * 2.0f));
                    const float Checker = ((CX + CY) & 1) ? 0.12f : 0.08f;
                    const float Fade    = std::exp(-0.08f * std::hypot(GP.X, GP.Y));
                    const float ShadowT = Simulation.EvaluateFluidShadowTransmittance(GP + Mathematics::Axis3D{ 0.0f, 0.0f, 0.02f }, SunDir, 18.0f);
                    Bg = {
                        (0.05f + Checker * Fade) * ShadowT,
                        (0.055f + Checker * Fade) * ShadowT,
                        (0.065f + Checker * Fade) * ShadowT,
                    };
                }
            }

            const auto Res = Simulation.EvaluateFluidSegment(
                Eye, RayDir, HitDist, SunDir, SunIrr, SkyRad,
                static_cast<float>((PX * 73u + PY * 179u) & 31u) / 31.0f);

            const Mathematics::LinearRGB FinalHdr {
                Bg.R * Res.Transmittance + Res.RadianceHdr.R,
                Bg.G * Res.Transmittance + Res.RadianceHdr.G,
                Bg.B * Res.Transmittance + Res.RadianceHdr.B,
            };

            const std::size_t Off = (static_cast<std::size_t>(PY) * PreviewWidthPx + PX) * 4u;
            PreviewRgba8[Off + 0u] = LinearToSrgb8(ToneMapReinhard(FinalHdr.R));
            PreviewRgba8[Off + 1u] = LinearToSrgb8(ToneMapReinhard(FinalHdr.G));
            PreviewRgba8[Off + 2u] = LinearToSrgb8(ToneMapReinhard(FinalHdr.B));
            PreviewRgba8[Off + 3u] = 255u;
        }
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
    Simulation.Configure(Parsed);
    Simulation.ResetSimulation();
    StatusMessage = "Imported TOML: " + Parsed.SceneName;
    return true;
}

} // namespace Frontier::Workspaces
