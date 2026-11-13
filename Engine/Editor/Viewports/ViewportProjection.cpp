/*============================================================================================================================================
                                                          VIEWPORTPROJECTION.CPP
============================================================================================================================================*/
// 🧩 Implementation of ViewportProjection camera presets, orbit/pan/frame math, 3D/2D picking rays, and aspect-locked planar bounds

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Viewports/ViewportProjection.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Editor
{

void ViewportProjection::ConfigureSplitMode(ViewportSplitMode Mode) noexcept
{
    SplitMode = Mode;
}

void ViewportProjection::ConfigureSplitRatio(float NormalizedRatio) noexcept
{
    SplitRatio = std::clamp(NormalizedRatio, 0.20f, 0.80f);
}

void ViewportProjection::ConfigureSpatialCamera(const SpatialCameraRig& Rig) noexcept
{
    SpatialCamera = Rig;
}

void ViewportProjection::ConfigurePlanarWindow(const PlanarWindowRig& Rig) noexcept
{
    PlanarWindow = Rig;
}

void ViewportProjection::ConfigurePlanarDomainSpan(float SpanU, float SpanV) noexcept
{
    PlanarWindow.SpanU = std::max(0.1f, SpanU);
    PlanarWindow.SpanV = std::max(0.1f, SpanV);
}

void ViewportProjection::ApplySpatialPreset(SpatialProjectionPreset Preset) noexcept
{
    SpatialCamera.Preset = Preset;
    switch (Preset)
    {
        case SpatialProjectionPreset::Perspective:
            SpatialCamera.YawRadians     = 0.68f;
            SpatialCamera.PitchRadians   = 0.38f;
            SpatialCamera.VerticalFovDeg = 42.0f;
            break;
        case SpatialProjectionPreset::Orthographic:
            SpatialCamera.VerticalFovDeg = 18.0f;
            break;
        case SpatialProjectionPreset::Top:
            SpatialCamera.YawRadians   = 0.0f;
            SpatialCamera.PitchRadians = 1.48f;
            break;
        case SpatialProjectionPreset::Front:
            SpatialCamera.YawRadians   = 0.0f;
            SpatialCamera.PitchRadians = 0.0f;
            break;
        case SpatialProjectionPreset::Right:
            SpatialCamera.YawRadians   = 0.5f * Constants::PiFloat;
            SpatialCamera.PitchRadians = 0.0f;
            break;
    }
}

void ViewportProjection::OrbitSpatialCamera(float DeltaYawRad, float DeltaPitchRad) noexcept
{
    SpatialCamera.Preset       = SpatialProjectionPreset::Perspective;
    SpatialCamera.YawRadians  += DeltaYawRad;
    SpatialCamera.PitchRadians = std::clamp(SpatialCamera.PitchRadians + DeltaPitchRad, -1.48f, 1.48f);
}

void ViewportProjection::PanSpatialCamera(float DeltaRightMeters, float DeltaUpMeters) noexcept
{
    Mathematics::Axis3D Right   {};
    Mathematics::Axis3D Up      {};
    Mathematics::Axis3D Forward {};
    EvaluateCameraBasis(Right, Up, Forward);

    SpatialCamera.TargetPivot.X += Right.X * DeltaRightMeters + Up.X * DeltaUpMeters;
    SpatialCamera.TargetPivot.Y += Right.Y * DeltaRightMeters + Up.Y * DeltaUpMeters;
    SpatialCamera.TargetPivot.Z += Right.Z * DeltaRightMeters + Up.Z * DeltaUpMeters;
}

void ViewportProjection::DollySpatialCamera(float DistanceMultiplier) noexcept
{
    SpatialCamera.DistanceMeters = std::clamp(
        SpatialCamera.DistanceMeters * std::max(0.1f, DistanceMultiplier),
        0.35f,
        250.0f);
}

void ViewportProjection::FitSpatialExtents(Mathematics::Axis3D MinBounds, Mathematics::Axis3D MaxBounds) noexcept
{
    SpatialCamera.TargetPivot = {
        0.5f * (MinBounds.X + MaxBounds.X),
        0.5f * (MinBounds.Y + MaxBounds.Y),
        0.5f * (MinBounds.Z + MaxBounds.Z),
    };
    const float Radius  = 0.5f * Mathematics::EvaluateDistance(MinBounds, MaxBounds);
    const float HalfFov = 0.5f * std::max(12.0f, SpatialCamera.VerticalFovDeg) * Constants::DegToRadFloat;
    SpatialCamera.DistanceMeters = std::clamp((std::max(0.25f, Radius) / std::tan(HalfFov)) * 1.28f, 0.8f, 220.0f);
}

void ViewportProjection::PanPlanarWindow(Mathematics::UVAxes DeltaUV) noexcept
{
    PlanarWindow.PanOffsetUV.U += DeltaUV.U;
    PlanarWindow.PanOffsetUV.V += DeltaUV.V;
}

void ViewportProjection::ZoomPlanarWindow(float ZoomMultiplier) noexcept
{
    PlanarWindow.ZoomScale = std::clamp(
        PlanarWindow.ZoomScale * std::max(0.1f, ZoomMultiplier),
        0.15f,
        16.0f);
}

void ViewportProjection::ZoomPlanarAroundPoint(
    uint32_t CanvasWidthPx,
    uint32_t CanvasHeightPx,
    float    LocalPixelX,
    float    LocalPixelY,
    float    ZoomMultiplier) noexcept
{
    const Mathematics::UVAxes BeforeUV = ProjectPixelToPlanarUV(CanvasWidthPx, CanvasHeightPx, LocalPixelX, LocalPixelY);
    ZoomPlanarWindow(ZoomMultiplier);
    const Mathematics::UVAxes AfterUV  = ProjectPixelToPlanarUV(CanvasWidthPx, CanvasHeightPx, LocalPixelX, LocalPixelY);
    PlanarWindow.PanOffsetUV.U += (BeforeUV.U - AfterUV.U);
    PlanarWindow.PanOffsetUV.V += (BeforeUV.V - AfterUV.V);
}

void ViewportProjection::FitPlanarExtents(Mathematics::UVAxes MinBounds, Mathematics::UVAxes MaxBounds) noexcept
{
    const float BoxW = std::max(0.05f, MaxBounds.U - MinBounds.U);
    const float BoxH = std::max(0.05f, MaxBounds.V - MinBounds.V);
    const float DomU = PlanarWindow.SpanU + 2.0f * PlanarWindow.PaddingUV;
    const float DomV = PlanarWindow.SpanV + 2.0f * PlanarWindow.PaddingUV;

    PlanarWindow.ZoomScale     = std::clamp(std::min(DomU / (BoxW * 1.16f), DomV / (BoxH * 1.16f)), 0.2f, 12.0f);
    PlanarWindow.PanOffsetUV.U = 0.5f * (MinBounds.U + MaxBounds.U) - 0.5f * PlanarWindow.SpanU;
    PlanarWindow.PanOffsetUV.V = 0.5f * (MinBounds.V + MaxBounds.V) - 0.5f * PlanarWindow.SpanV;
}

Mathematics::Axis3D ViewportProjection::EvaluateCameraEyePosition() const noexcept
{
    return SpatialCamera.TargetPivot + Mathematics::EvaluateOrbitOffsetFromAngles(
                                           SpatialCamera.DistanceMeters,
                                           SpatialCamera.YawRadians,
                                           SpatialCamera.PitchRadians,
                                           SpatialCamera.Basis);
}

void ViewportProjection::EvaluateCameraBasis(
    Mathematics::Axis3D& OutRight,
    Mathematics::Axis3D& OutUp,
    Mathematics::Axis3D& OutForward) const noexcept
{
    const auto Eye = EvaluateCameraEyePosition();
    Mathematics::EvaluateOrthonormalCameraBasis(
        Eye,
        SpatialCamera.TargetPivot,
        SpatialCamera.Basis,
        OutRight,
        OutUp,
        OutForward);
}

ViewportRay3D ViewportProjection::BuildSpatialRay(
    uint32_t ViewportWidthPx,
    uint32_t ViewportHeightPx,
    float    LocalPixelX,
    float    LocalPixelY) const noexcept
{
    Mathematics::Axis3D Right   {};
    Mathematics::Axis3D Up      {};
    Mathematics::Axis3D Forward {};
    EvaluateCameraBasis(Right, Up, Forward);

    const float SafeW   = static_cast<float>(std::max(1u, ViewportWidthPx));
    const float SafeH   = static_cast<float>(std::max(1u, ViewportHeightPx));
    const float Aspect  = SafeW / SafeH;
    const float NdcX    = (LocalPixelX / SafeW) * 2.0f - 1.0f;
    const float NdcY    = 1.0f - (LocalPixelY / SafeH) * 2.0f;
    const float TanHalf = std::tan(0.5f * SpatialCamera.VerticalFovDeg * Constants::DegToRadFloat);

    if (SpatialCamera.Preset == SpatialProjectionPreset::Orthographic)
    {
        const float HalfSpanH = std::max(0.05f, SpatialCamera.DistanceMeters * TanHalf);
        const float HalfSpanW = HalfSpanH * Aspect;
        const Mathematics::Axis3D Eye = EvaluateCameraEyePosition();
        const Mathematics::Axis3D Origin = {
            Eye.X + Right.X * (NdcX * HalfSpanW) + Up.X * (NdcY * HalfSpanH),
            Eye.Y + Right.Y * (NdcX * HalfSpanW) + Up.Y * (NdcY * HalfSpanH),
            Eye.Z + Right.Z * (NdcX * HalfSpanW) + Up.Z * (NdcY * HalfSpanH),
        };
        return { Origin, Forward };
    }

    const Mathematics::Axis3D Dir = Mathematics::NormalizeAxis({
        Forward.X + Right.X * (NdcX * Aspect * TanHalf) + Up.X * (NdcY * TanHalf),
        Forward.Y + Right.Y * (NdcX * Aspect * TanHalf) + Up.Y * (NdcY * TanHalf),
        Forward.Z + Right.Z * (NdcX * Aspect * TanHalf) + Up.Z * (NdcY * TanHalf),
    });
    return { EvaluateCameraEyePosition(), Dir };
}

void ViewportProjection::EvaluateAspectLockedPlanarBounds(
    uint32_t             CanvasWidthPx,
    uint32_t             CanvasHeightPx,
    Mathematics::UVAxes& OutMinUV,
    Mathematics::UVAxes& OutMaxUV) const noexcept
{
    const float InvZoom      = 1.0f / std::max(0.15f, PlanarWindow.ZoomScale);
    const float DesiredSpanU = (PlanarWindow.SpanU + 2.0f * PlanarWindow.PaddingUV) * InvZoom;
    const float DesiredSpanV = (PlanarWindow.SpanV + 2.0f * PlanarWindow.PaddingUV) * InvZoom;
    const float Aspect       = (CanvasHeightPx > 0u)
        ? (static_cast<float>(std::max(1u, CanvasWidthPx)) / static_cast<float>(CanvasHeightPx))
        : 1.0f;

    float EffectiveU = DesiredSpanU;
    float EffectiveV = DesiredSpanV;
    if ((DesiredSpanU / DesiredSpanV) > Aspect)
    {
        EffectiveV = DesiredSpanU / Aspect;
    }
    else
    {
        EffectiveU = DesiredSpanV * Aspect;
    }

    const float CenterU = 0.5f * PlanarWindow.SpanU + PlanarWindow.PanOffsetUV.U;
    const float CenterV = 0.5f * PlanarWindow.SpanV + PlanarWindow.PanOffsetUV.V;
    OutMinUV = { CenterU - 0.5f * EffectiveU, CenterV - 0.5f * EffectiveV };
    OutMaxUV = { CenterU + 0.5f * EffectiveU, CenterV + 0.5f * EffectiveV };
}

Mathematics::UVAxes ViewportProjection::ProjectPixelToPlanarUV(
    uint32_t CanvasWidthPx,
    uint32_t CanvasHeightPx,
    float    LocalPixelX,
    float    LocalPixelY) const noexcept
{
    Mathematics::UVAxes MinUV {};
    Mathematics::UVAxes MaxUV {};
    EvaluateAspectLockedPlanarBounds(CanvasWidthPx, CanvasHeightPx, MinUV, MaxUV);

    const float NormX = LocalPixelX / static_cast<float>(std::max(1u, CanvasWidthPx));
    const float NormY = 1.0f - (LocalPixelY / static_cast<float>(std::max(1u, CanvasHeightPx)));
    return {
        MinUV.U + NormX * (MaxUV.U - MinUV.U),
        MinUV.V + NormY * (MaxUV.V - MinUV.V),
    };
}

ViewportRay3D ViewportProjection::BuildPlanarRay(
    uint32_t CanvasWidthPx,
    uint32_t CanvasHeightPx,
    float    LocalPixelX,
    float    LocalPixelY) const noexcept
{
    const Mathematics::UVAxes UV = ProjectPixelToPlanarUV(CanvasWidthPx, CanvasHeightPx, LocalPixelX, LocalPixelY);
    return { { UV.U, UV.V, 5.0f }, { 0.0f, 0.0f, -1.0f } };
}

} // namespace Frontier::Editor
