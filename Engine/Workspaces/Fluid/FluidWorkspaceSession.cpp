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
    for (int I = 0; I < 36; ++I)
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
    if (SelectedBurstKind == 1u)
    {
        Blast.Lobes = 8.0f;
        Blast.StrengthMps *= 1.35f;
    }
    else if (SelectedBurstKind == 2u)
    {
        Blast.Lobes = 5.0f;
        Blast.RadiusMeters *= 1.3f;
    }
    else if (SelectedBurstKind == 3u)
    {
        Blast.Lobes = 2.0f;
        Blast.StrengthMps *= 1.25f;
    }
    else if (SelectedBurstKind == 4u)
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
    const float Exposure = std::max(0.25f, Simulation.InspectDocument().Shading.Exposure) * 0.045f;

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
            const float FireLum      = std::max({ Res.RadianceHdr.R, Res.RadianceHdr.G, Res.RadianceHdr.B }) * 0.42f;
            const float Alpha        = std::clamp(std::max(SmokeOpacity * 0.85f, 1.0f - std::exp(-FireLum * 1.15f)), 0.0f, 0.96f);

            const Mathematics::LinearRGB FinalHdr {
                Res.RadianceHdr.R * Exposure,
                Res.RadianceHdr.G * Exposure,
                Res.RadianceHdr.B * Exposure,
            };

            const std::size_t Off = (static_cast<std::size_t>(PY) * PreviewWidthPx + PX) * 4u;
            PreviewRgba8[Off + 0u] = LinearToSrgb8(ToneMapAcesApprox(FinalHdr.R));
            PreviewRgba8[Off + 1u] = LinearToSrgb8(ToneMapAcesApprox(FinalHdr.G));
            PreviewRgba8[Off + 2u] = LinearToSrgb8(ToneMapAcesApprox(FinalHdr.B));
            PreviewRgba8[Off + 3u] = static_cast<uint8_t>(std::round(Alpha * 255.0f));
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
    DocumentTitle  = Parsed.SceneName;
    DocumentDirty  = false;
    Simulation.Configure(Parsed);
    Simulation.ResetSimulation();
    StatusMessage = "Imported TOML: " + Parsed.SceneName;
    return true;
}

} // namespace Frontier::Workspaces
