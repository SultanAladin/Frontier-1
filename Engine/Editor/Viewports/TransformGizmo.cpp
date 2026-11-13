/*============================================================================================================================================
                                                            TRANSFORMGIZMO.CPP
============================================================================================================================================*/
// 🧩 Implementation of Slate GizmoPRO 3D & 2D procedural geometry builder and skew-line / ray-plane / Ctrl-snap drag solver

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Viewports/TransformGizmo.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Editor
{

namespace
{

// Exact Slate GizmoPRO palette (SultanAladin/Slate References/Gizmo.html)
constexpr Mathematics::Scalar3D ColorAxisX      { 0.878f, 0.078f, 0.078f }; // #E01414
constexpr Mathematics::Scalar3D ColorAxisY      { 0.071f, 0.831f, 0.039f }; // #12D40A
constexpr Mathematics::Scalar3D ColorAxisZ      { 0.082f, 0.376f, 0.878f }; // #1560E0
constexpr Mathematics::Scalar3D ColorPlaneYZ    { 0.122f, 0.780f, 0.780f }; // #1FC7C7
constexpr Mathematics::Scalar3D ColorPlaneXZ    { 0.784f, 0.118f, 0.784f }; // #C81EC8
constexpr Mathematics::Scalar3D ColorPlaneXY    { 0.878f, 0.804f, 0.071f }; // #E0CD12
constexpr Mathematics::Scalar3D ColorCenterRing { 1.000f, 1.000f, 1.000f }; // #FFFFFF

void AppendTriangle(
    std::vector<GizmoGPUVertex>& OutStream,
    Mathematics::Axis3D          P0,
    Mathematics::Axis3D          P1,
    Mathematics::Axis3D          P2,
    Mathematics::Axis3D          Normal,
    Mathematics::Scalar3D        Color,
    float                        Alpha,
    float                        ShadingWeight,
    GizmoHandleOrdinal           Handle)
{
    const float OrdFloat = static_cast<float>(static_cast<uint32_t>(Handle));
    OutStream.push_back({
        Mathematics::PackGPUAxis4(P0, Alpha),
        Mathematics::PackGPUAxis4(Normal, OrdFloat),
        Mathematics::PackGPUAxis4(Color, ShadingWeight),
    });
    OutStream.push_back({
        Mathematics::PackGPUAxis4(P1, Alpha),
        Mathematics::PackGPUAxis4(Normal, OrdFloat),
        Mathematics::PackGPUAxis4(Color, ShadingWeight),
    });
    OutStream.push_back({
        Mathematics::PackGPUAxis4(P2, Alpha),
        Mathematics::PackGPUAxis4(Normal, OrdFloat),
        Mathematics::PackGPUAxis4(Color, ShadingWeight),
    });
}

void ConstructOrthonormalBasis(
    Mathematics::Axis3D  AxisDir,
    Mathematics::Axis3D& OutTangentU,
    Mathematics::Axis3D& OutTangentV) noexcept
{
    const Mathematics::Axis3D Ref = std::abs(AxisDir.Y) < 0.90f
        ? Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f }
        : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
    OutTangentU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Ref, AxisDir));
    OutTangentV = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(AxisDir, OutTangentU));
}

void AppendConeHandle(
    std::vector<GizmoGPUVertex>& OutStream,
    Mathematics::Axis3D          Center,
    Mathematics::Axis3D          AxisDir,
    float                        Radius,
    float                        Height,
    Mathematics::Scalar3D        Color,
    GizmoHandleOrdinal           Handle)
{
    constexpr uint32_t Segments = 24u;
    Mathematics::Axis3D U {}, V {};
    ConstructOrthonormalBasis(AxisDir, U, V);

    const Mathematics::Axis3D TipPoint       = Center + AxisDir * (Height * 0.5f);
    const Mathematics::Axis3D OriginCenter = Center - AxisDir * (Height * 0.5f);

    for (uint32_t Seg = 0u; Seg < Segments; ++Seg)
    {
        const float θ0 = (static_cast<float>(Seg) / static_cast<float>(Segments)) * Constants::TauFloat;
        const float θ1 = (static_cast<float>(Seg + 1u) / static_cast<float>(Segments)) * Constants::TauFloat;
        const Mathematics::Axis3D R0 = U * std::cos(θ0) + V * std::sin(θ0);
        const Mathematics::Axis3D R1 = U * std::cos(θ1) + V * std::sin(θ1);
        const Mathematics::Axis3D B0 = OriginCenter + R0 * Radius;
        const Mathematics::Axis3D B1 = OriginCenter + R1 * Radius;
        const Mathematics::Axis3D SideNormal = Mathematics::NormalizeAxis(R0 + R1 + AxisDir * (Radius / Height));

        AppendTriangle(OutStream, B0, B1, TipPoint, SideNormal, Color, 1.0f, 1.0f, Handle);
        AppendTriangle(OutStream, OriginCenter, B1, B0, AxisDir * -1.0f, Color, 1.0f, 1.0f, Handle);
    }
}

void AppendCylinderHandle(
    std::vector<GizmoGPUVertex>& OutStream,
    Mathematics::Axis3D          Center,
    Mathematics::Axis3D          AxisDir,
    float                        Radius,
    float                        Height,
    Mathematics::Scalar3D        Color,
    GizmoHandleOrdinal           Handle)
{
    constexpr uint32_t Segments = 24u;
    Mathematics::Axis3D U {}, V {};
    ConstructOrthonormalBasis(AxisDir, U, V);

    const Mathematics::Axis3D BotCenter = Center - AxisDir * (Height * 0.5f);
    const Mathematics::Axis3D TopCenter = Center + AxisDir * (Height * 0.5f);

    for (uint32_t Seg = 0u; Seg < Segments; ++Seg)
    {
        const float θ0 = (static_cast<float>(Seg) / static_cast<float>(Segments)) * Constants::TauFloat;
        const float θ1 = (static_cast<float>(Seg + 1u) / static_cast<float>(Segments)) * Constants::TauFloat;
        const Mathematics::Axis3D R0 = U * std::cos(θ0) + V * std::sin(θ0);
        const Mathematics::Axis3D R1 = U * std::cos(θ1) + V * std::sin(θ1);

        const Mathematics::Axis3D P00 = BotCenter + R0 * Radius;
        const Mathematics::Axis3D P10 = BotCenter + R1 * Radius;
        const Mathematics::Axis3D P11 = TopCenter + R1 * Radius;
        const Mathematics::Axis3D P01 = TopCenter + R0 * Radius;
        const Mathematics::Axis3D SideNormal = Mathematics::NormalizeAxis(R0 + R1);

        AppendTriangle(OutStream, P00, P10, P11, SideNormal, Color, 1.0f, 1.0f, Handle);
        AppendTriangle(OutStream, P00, P11, P01, SideNormal, Color, 1.0f, 1.0f, Handle);
        AppendTriangle(OutStream, TopCenter, P01, P11, AxisDir, Color, 1.0f, 1.0f, Handle);
        AppendTriangle(OutStream, BotCenter, P10, P00, AxisDir * -1.0f, Color, 1.0f, 1.0f, Handle);
    }
}

void AppendPlaneQuadHandle(
    std::vector<GizmoGPUVertex>& OutStream,
    Mathematics::Axis3D          Pivot,
    Mathematics::Axis3D          AxisU,
    Mathematics::Axis3D          AxisV,
    float                        TipDist,
    float                        HalfSize,
    float                        BorderWidth,
    Mathematics::Scalar3D        Color,
    GizmoHandleOrdinal           Handle)
{
    const Mathematics::Axis3D QuadCenter = Pivot + (AxisU + AxisV) * (TipDist - HalfSize);
    const Mathematics::Axis3D Inner      = QuadCenter - AxisU * HalfSize - AxisV * HalfSize;
    const Mathematics::Axis3D BackU      = QuadCenter + AxisU * HalfSize - AxisV * HalfSize;
    const Mathematics::Axis3D Outer      = QuadCenter + AxisU * HalfSize + AxisV * HalfSize;
    const Mathematics::Axis3D BackV      = QuadCenter - AxisU * HalfSize + AxisV * HalfSize;
    const Mathematics::Axis3D Normal     = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(AxisU, AxisV));

    // Double-sided translucent quad (alpha = 0.28 per Slate Gizmo.html)
    AppendTriangle(OutStream, Inner, BackU, Outer, Normal, Color, 0.28f, 0.0f, Handle);
    AppendTriangle(OutStream, Inner, Outer, BackV, Normal, Color, 0.28f, 0.0f, Handle);
    AppendTriangle(OutStream, Inner, Outer, BackU, Normal * -1.0f, Color, 0.28f, 0.0f, Handle);
    AppendTriangle(OutStream, Inner, BackV, Outer, Normal * -1.0f, Color, 0.28f, 0.0f, Handle);

    // Two opaque outer border edges ONLY: BackU -> Outer and Outer -> BackV
    const Mathematics::Axis3D OffsetN = Normal * (BorderWidth * 0.2f);
    const Mathematics::Axis3D Edge1A  = BackU - AxisU * BorderWidth + OffsetN;
    const Mathematics::Axis3D Edge1B  = Outer - AxisU * BorderWidth + OffsetN;
    AppendTriangle(OutStream, BackU + OffsetN, Outer + OffsetN, Edge1B, Normal, Color, 1.0f, 0.0f, Handle);
    AppendTriangle(OutStream, BackU + OffsetN, Edge1B, Edge1A, Normal, Color, 1.0f, 0.0f, Handle);

    const Mathematics::Axis3D Edge2A  = Outer - AxisV * BorderWidth + OffsetN;
    const Mathematics::Axis3D Edge2B  = BackV - AxisV * BorderWidth + OffsetN;
    AppendTriangle(OutStream, Outer + OffsetN, BackV + OffsetN, Edge2B, Normal, Color, 1.0f, 0.0f, Handle);
    AppendTriangle(OutStream, Outer + OffsetN, Edge2B, Edge2A, Normal, Color, 1.0f, 0.0f, Handle);
}

void AppendRotationArcHandle(
    std::vector<GizmoGPUVertex>& OutStream,
    Mathematics::Axis3D          Pivot,
    Mathematics::Axis3D          AxisU,
    Mathematics::Axis3D          AxisV,
    float                        ArcRadius,
    float                        HalfBand,
    Mathematics::Scalar3D        Color,
    GizmoHandleOrdinal           Handle)
{
    // Slate Gizmo.html: 31° sweep centered at 45° (pi/4) bisector between +AxisU and +AxisV
    constexpr uint32_t Segments   = 24u;
    constexpr float    CenterAng  = Constants::PiFloat * 0.25f;
    constexpr float    HalfSweep  = 15.5f * Constants::DegToRadFloat;
    const float        InnerR     = ArcRadius - HalfBand;
    const float        OuterR     = ArcRadius + HalfBand;
    const Mathematics::Axis3D Normal = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(AxisU, AxisV));

    for (uint32_t Seg = 0u; Seg < Segments; ++Seg)
    {
        const float T0 = static_cast<float>(Seg) / static_cast<float>(Segments);
        const float T1 = static_cast<float>(Seg + 1u) / static_cast<float>(Segments);
        const float A0 = (CenterAng - HalfSweep) + T0 * (2.0f * HalfSweep);
        const float A1 = (CenterAng - HalfSweep) + T1 * (2.0f * HalfSweep);

        const Mathematics::Axis3D Dir0 = AxisU * std::cos(A0) + AxisV * std::sin(A0);
        const Mathematics::Axis3D Dir1 = AxisU * std::cos(A1) + AxisV * std::sin(A1);

        const Mathematics::Axis3D I0 = Pivot + Dir0 * InnerR;
        const Mathematics::Axis3D O0 = Pivot + Dir0 * OuterR;
        const Mathematics::Axis3D O1 = Pivot + Dir1 * OuterR;
        const Mathematics::Axis3D I1 = Pivot + Dir1 * InnerR;

        AppendTriangle(OutStream, I0, O0, O1, Normal, Color, 1.0f, 0.0f, Handle);
        AppendTriangle(OutStream, I0, O1, I1, Normal, Color, 1.0f, 0.0f, Handle);
        AppendTriangle(OutStream, I0, O1, O0, Normal * -1.0f, Color, 1.0f, 0.0f, Handle);
        AppendTriangle(OutStream, I0, I1, O1, Normal * -1.0f, Color, 1.0f, 0.0f, Handle);
    }
}

void AppendBillboardTorusRing(
    std::vector<GizmoGPUVertex>& OutStream,
    Mathematics::Axis3D          Pivot,
    Mathematics::Axis3D          CamRight,
    Mathematics::Axis3D          CamUp,
    float                        MajorRadius,
    float                        TubeRadius)
{
    constexpr uint32_t RingSegs = 40u;
    constexpr uint32_t TubeSegs = 8u;
    const Mathematics::Axis3D CamNormal = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(CamRight, CamUp));

    for (uint32_t Maj = 0u; Maj < RingSegs; ++Maj)
    {
        const float φ0 = (static_cast<float>(Maj) / static_cast<float>(RingSegs)) * Constants::TauFloat;
        const float φ1 = (static_cast<float>(Maj + 1u) / static_cast<float>(RingSegs)) * Constants::TauFloat;
        const Mathematics::Axis3D Rad0 = CamRight * std::cos(φ0) + CamUp * std::sin(φ0);
        const Mathematics::Axis3D Rad1 = CamRight * std::cos(φ1) + CamUp * std::sin(φ1);

        for (uint32_t Min = 0u; Min < TubeSegs; ++Min)
        {
            const float θ0 = (static_cast<float>(Min) / static_cast<float>(TubeSegs)) * Constants::TauFloat;
            const float θ1 = (static_cast<float>(Min + 1u) / static_cast<float>(TubeSegs)) * Constants::TauFloat;

            const Mathematics::Axis3D P00 = Pivot + Rad0 * (MajorRadius + TubeRadius * std::cos(θ0)) + CamNormal * (TubeRadius * std::sin(θ0));
            const Mathematics::Axis3D P10 = Pivot + Rad1 * (MajorRadius + TubeRadius * std::cos(θ0)) + CamNormal * (TubeRadius * std::sin(θ0));
            const Mathematics::Axis3D P11 = Pivot + Rad1 * (MajorRadius + TubeRadius * std::cos(θ1)) + CamNormal * (TubeRadius * std::sin(θ1));
            const Mathematics::Axis3D P01 = Pivot + Rad0 * (MajorRadius + TubeRadius * std::cos(θ1)) + CamNormal * (TubeRadius * std::sin(θ1));

            AppendTriangle(OutStream, P00, P10, P11, CamNormal, ColorCenterRing, 1.0f, 0.0f, GizmoHandleOrdinal::CenterRing);
            AppendTriangle(OutStream, P00, P11, P01, CamNormal, ColorCenterRing, 1.0f, 0.0f, GizmoHandleOrdinal::CenterRing);
        }
    }
}

[[nodiscard]] float SnapIncrement(float RawReading, float StepSize) noexcept
{
    if (StepSize <= 1.0e-6f)
    {
        return RawReading;
    }
    return std::round(RawReading / StepSize) * StepSize;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               CONFIGURATION & GEOMETRY STREAM
//------------------------------------------------------------------------------------------------------------------------

void TransformGizmo::ConfigureDimensionMode(GizmoDimensionMode Mode) noexcept
{
    DimensionMode = Mode;
}

void TransformGizmo::ConfigureAnchor(Mathematics::Axis3D PivotOrigin, float ConstantWorldRadius) noexcept
{
    PivotPosition = PivotOrigin;
    WorldRadius   = std::max(0.01f, ConstantWorldRadius);
}

void TransformGizmo::AssignHoveredHandle(GizmoHandleOrdinal Handle) noexcept
{
    HoveredHandle = Handle;
}

std::vector<GizmoGPUVertex> TransformGizmo::BuildTriangleStream(
    Mathematics::Axis3D CameraRight,
    Mathematics::Axis3D CameraUp,
    Mathematics::Axis3D CameraForward) const
{
    (void)CameraForward;
    std::vector<GizmoGPUVertex> Stream;
    Stream.reserve(DimensionMode == GizmoDimensionMode::Spatial3D ? 4200u : 2400u);

    const float L       = WorldRadius;
    const float TipDist = 0.95f * L;
    const float CylDist = TipDist - 0.28f * L; // 0.67 * L

    const Mathematics::Axis3D UnitX { 1.0f, 0.0f, 0.0f };
    const Mathematics::Axis3D UnitY { 0.0f, 1.0f, 0.0f };
    const Mathematics::Axis3D UnitZ { 0.0f, 0.0f, 1.0f };

    // Cohesive Slate axis stems radiating from outside the center ring (0.17 * L) to cone collar (TipDist - 0.09 * L)
    const float StemStart  = 0.17f * L;
    const float StemFinish = TipDist - 0.09f * L;
    const float StemLen    = std::max(0.02f * L, StemFinish - StemStart);
    const float StemMid    = 0.5f * (StemStart + StemFinish);
    AppendCylinderHandle(Stream, PivotPosition + UnitX * StemMid, UnitX, 0.0095f * L, StemLen, ColorAxisX, GizmoHandleOrdinal::TranslateX);
    AppendCylinderHandle(Stream, PivotPosition + UnitY * StemMid, UnitY, 0.0095f * L, StemLen, ColorAxisY, GizmoHandleOrdinal::TranslateY);

    // +X / +U and +Y / +V Translate Cones & Inboard Scale Cylinders (shared by 3D & 2D)
    AppendConeHandle(Stream, PivotPosition + UnitX * TipDist, UnitX, 0.055f * L, 0.18f * L, ColorAxisX, GizmoHandleOrdinal::TranslateX);
    AppendConeHandle(Stream, PivotPosition + UnitY * TipDist, UnitY, 0.055f * L, 0.18f * L, ColorAxisY, GizmoHandleOrdinal::TranslateY);
    AppendCylinderHandle(Stream, PivotPosition + UnitX * CylDist, UnitX, 0.048f * L, 0.12f * L, ColorAxisX, GizmoHandleOrdinal::ScaleX);
    AppendCylinderHandle(Stream, PivotPosition + UnitY * CylDist, UnitY, 0.048f * L, 0.12f * L, ColorAxisY, GizmoHandleOrdinal::ScaleY);

    // XY / UV Plane-Translate Quad & Z / UV 45° Rotation Arc Bar
    AppendPlaneQuadHandle(Stream, PivotPosition, UnitX, UnitY, TipDist, 0.08f * L, 0.011f * L, ColorPlaneXY, GizmoHandleOrdinal::PlaneXY);
    AppendRotationArcHandle(Stream, PivotPosition, UnitX, UnitY, TipDist * 0.62f, 0.034f * L, ColorAxisZ, GizmoHandleOrdinal::RotateZ);

    if (DimensionMode == GizmoDimensionMode::Spatial3D)
    {
        AppendCylinderHandle(Stream, PivotPosition + UnitZ * StemMid, UnitZ, 0.0095f * L, StemLen, ColorAxisZ, GizmoHandleOrdinal::TranslateZ);
        AppendConeHandle(Stream, PivotPosition + UnitZ * TipDist, UnitZ, 0.055f * L, 0.18f * L, ColorAxisZ, GizmoHandleOrdinal::TranslateZ);
        AppendCylinderHandle(Stream, PivotPosition + UnitZ * CylDist, UnitZ, 0.048f * L, 0.12f * L, ColorAxisZ, GizmoHandleOrdinal::ScaleZ);

        AppendPlaneQuadHandle(Stream, PivotPosition, UnitY, UnitZ, TipDist, 0.08f * L, 0.011f * L, ColorPlaneYZ, GizmoHandleOrdinal::PlaneYZ);
        AppendPlaneQuadHandle(Stream, PivotPosition, UnitX, UnitZ, TipDist, 0.08f * L, 0.011f * L, ColorPlaneXZ, GizmoHandleOrdinal::PlaneXZ);

        AppendRotationArcHandle(Stream, PivotPosition, UnitY, UnitZ, TipDist * 0.62f, 0.034f * L, ColorAxisX, GizmoHandleOrdinal::RotateX);
        AppendRotationArcHandle(Stream, PivotPosition, UnitZ, UnitX, TipDist * 0.62f, 0.034f * L, ColorAxisY, GizmoHandleOrdinal::RotateY);
    }

    const Mathematics::Axis3D RightN = DimensionMode == GizmoDimensionMode::Planar2D
        ? UnitX
        : Mathematics::NormalizeAxis(CameraRight, UnitX);
    const Mathematics::Axis3D UpN = DimensionMode == GizmoDimensionMode::Planar2D
        ? UnitY
        : Mathematics::NormalizeAxis(CameraUp, UnitY);
    AppendBillboardTorusRing(Stream, PivotPosition, RightN, UpN, 0.16f * L, 0.008f * L);

    return Stream;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              RAY-AXIS & RAY-PLANE DRAG SOLVER
//------------------------------------------------------------------------------------------------------------------------

float TransformGizmo::EvaluateAxisRayParameter(
    Mathematics::Axis3D  AxisOrigin,
    Mathematics::Axis3D  AxisDirection,
    const ViewportRay3D& Ray) noexcept
{
    const Mathematics::Axis3D W0    = AxisOrigin - Ray.Origin;
    const float               A     = Mathematics::EvaluateDot(AxisDirection, AxisDirection);
    const float               B     = Mathematics::EvaluateDot(AxisDirection, Ray.Direction);
    const float               C     = Mathematics::EvaluateDot(Ray.Direction, Ray.Direction);
    const float               D     = Mathematics::EvaluateDot(AxisDirection, W0);
    const float               E     = Mathematics::EvaluateDot(Ray.Direction, W0);
    const float               Denom = A * C - B * B;
    if (std::abs(Denom) < 1.0e-6f)
    {
        return 0.0f;
    }
    return (B * E - C * D) / Denom;
}

bool TransformGizmo::EvaluatePlaneRayIntersection(
    Mathematics::Axis3D  PlaneOrigin,
    Mathematics::Axis3D  PlaneNormal,
    const ViewportRay3D& Ray,
    Mathematics::Axis3D& OutIntersection) noexcept
{
    const float Denom = Mathematics::EvaluateDot(PlaneNormal, Ray.Direction);
    if (std::abs(Denom) < 1.0e-6f)
    {
        return false;
    }
    const float T = Mathematics::EvaluateDot(PlaneOrigin - Ray.Origin, PlaneNormal) / Denom;
    if (T < 0.0f)
    {
        return false;
    }
    OutIntersection = Ray.Origin + Ray.Direction * T;
    return true;
}

bool TransformGizmo::BeginDragInteraction(
    GizmoHandleOrdinal                   PickedHandle,
    const ViewportRay3D&                 InitialRay,
    const Mathematics::SpatialTransform& BaselineTransform,
    Mathematics::Axis3D                  CameraRight,
    Mathematics::Axis3D                  CameraUp) noexcept
{
    if (PickedHandle == GizmoHandleOrdinal::None)
    {
        return false;
    }

    ActiveDragHandle        = PickedHandle;
    DragStartTransform      = BaselineTransform;
    DragStartPivotPosition  = PivotPosition;

    switch (PickedHandle)
    {
        case GizmoHandleOrdinal::TranslateX:
        case GizmoHandleOrdinal::ScaleX:
            DragAxisPrimary      = { 1.0f, 0.0f, 0.0f };
            DragInitialAxisParam = EvaluateAxisRayParameter(PivotPosition, DragAxisPrimary, InitialRay);
            return true;

        case GizmoHandleOrdinal::TranslateY:
        case GizmoHandleOrdinal::ScaleY:
            DragAxisPrimary      = { 0.0f, 1.0f, 0.0f };
            DragInitialAxisParam = EvaluateAxisRayParameter(PivotPosition, DragAxisPrimary, InitialRay);
            return true;

        case GizmoHandleOrdinal::TranslateZ:
        case GizmoHandleOrdinal::ScaleZ:
            DragAxisPrimary      = { 0.0f, 0.0f, 1.0f };
            DragInitialAxisParam = EvaluateAxisRayParameter(PivotPosition, DragAxisPrimary, InitialRay);
            return true;

        case GizmoHandleOrdinal::PlaneYZ:
        case GizmoHandleOrdinal::RotateX:
            DragAxisPrimary   = { 0.0f, 1.0f, 0.0f };
            DragAxisSecondary = { 0.0f, 0.0f, 1.0f };
            DragPlaneNormal   = { 1.0f, 0.0f, 0.0f };
            break;

        case GizmoHandleOrdinal::PlaneXZ:
        case GizmoHandleOrdinal::RotateY:
            DragAxisPrimary   = { 0.0f, 0.0f, 1.0f };
            DragAxisSecondary = { 1.0f, 0.0f, 0.0f };
            DragPlaneNormal   = { 0.0f, 1.0f, 0.0f };
            break;

        case GizmoHandleOrdinal::PlaneXY:
        case GizmoHandleOrdinal::RotateZ:
            DragAxisPrimary   = { 1.0f, 0.0f, 0.0f };
            DragAxisSecondary = { 0.0f, 1.0f, 0.0f };
            DragPlaneNormal   = { 0.0f, 0.0f, 1.0f };
            break;

        case GizmoHandleOrdinal::CenterRing:
            DragAxisPrimary   = Mathematics::NormalizeAxis(CameraRight, { 1.0f, 0.0f, 0.0f });
            DragAxisSecondary = Mathematics::NormalizeAxis(CameraUp, { 0.0f, 1.0f, 0.0f });
            DragPlaneNormal   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(DragAxisPrimary, DragAxisSecondary));
            break;

        default:
            ActiveDragHandle = GizmoHandleOrdinal::None;
            return false;
    }

    if (!EvaluatePlaneRayIntersection(PivotPosition, DragPlaneNormal, InitialRay, DragInitialPlaneHit))
    {
        ActiveDragHandle = GizmoHandleOrdinal::None;
        return false;
    }

    const Mathematics::Axis3D LocalVec = DragInitialPlaneHit - PivotPosition;
    DragInitialAngleRad = std::atan2(
        Mathematics::EvaluateDot(LocalVec, DragAxisSecondary),
        Mathematics::EvaluateDot(LocalVec, DragAxisPrimary));
    return true;
}

GizmoInteractionDelta TransformGizmo::EvaluateDragInteraction(
    const ViewportRay3D&                    CurrentRay,
    bool                                    SnapModifierActive,
    const Configuration::GizmoSnapSettings& SnapSettings,
    Mathematics::SpatialTransform&          InOutTransform) noexcept
{
    GizmoInteractionDelta Out {};
    if (ActiveDragHandle == GizmoHandleOrdinal::None)
    {
        return Out;
    }

    const float MoveStep = DimensionMode == GizmoDimensionMode::Planar2D
        ? SnapSettings.UVMoveStepUnits
        : SnapSettings.SpatialMoveStepMeters;

    char Buffer[96]{};

    if (ActiveDragHandle == GizmoHandleOrdinal::TranslateX ||
        ActiveDragHandle == GizmoHandleOrdinal::TranslateY ||
        ActiveDragHandle == GizmoHandleOrdinal::TranslateZ)
    {
        float RawDelta = EvaluateAxisRayParameter(DragStartPivotPosition, DragAxisPrimary, CurrentRay) - DragInitialAxisParam;
        if (SnapModifierActive)
        {
            RawDelta = SnapIncrement(RawDelta, MoveStep);
        }

        Out.TranslationDelta    = DragAxisPrimary * RawDelta;
        InOutTransform.Position = DragStartTransform.Position + Out.TranslationDelta;
        PivotPosition           = InOutTransform.Position;
        Out.TransformChanged    = true;

        const char AxisChar = ActiveDragHandle == GizmoHandleOrdinal::TranslateX
            ? (DimensionMode == GizmoDimensionMode::Planar2D ? 'U' : 'X')
            : (ActiveDragHandle == GizmoHandleOrdinal::TranslateY
                ? (DimensionMode == GizmoDimensionMode::Planar2D ? 'V' : 'Y')
                : 'Z');
        std::snprintf(Buffer, sizeof(Buffer), "%c move %+.2f", AxisChar, RawDelta);
        Out.HudReadout = Buffer;
        return Out;
    }

    if (ActiveDragHandle == GizmoHandleOrdinal::ScaleX ||
        ActiveDragHandle == GizmoHandleOrdinal::ScaleY ||
        ActiveDragHandle == GizmoHandleOrdinal::ScaleZ)
    {
        const float RawDelta = (EvaluateAxisRayParameter(DragStartPivotPosition, DragAxisPrimary, CurrentRay) - DragInitialAxisParam) / std::max(WorldRadius, 0.01f);
        float       Factor   = std::max(0.05f, 1.0f + RawDelta);
        if (SnapModifierActive)
        {
            Factor = std::max(0.05f, SnapIncrement(Factor, SnapSettings.ScaleFactorStep));
        }

        Mathematics::Scalar3D Mult { 1.0f, 1.0f, 1.0f };
        const char*           AxisTag = "X";
        if (ActiveDragHandle == GizmoHandleOrdinal::ScaleX) { Mult.X = Factor; AxisTag = DimensionMode == GizmoDimensionMode::Planar2D ? "U" : "X"; }
        if (ActiveDragHandle == GizmoHandleOrdinal::ScaleY) { Mult.Y = Factor; AxisTag = DimensionMode == GizmoDimensionMode::Planar2D ? "V" : "Y"; }
        if (ActiveDragHandle == GizmoHandleOrdinal::ScaleZ) { Mult.Z = Factor; AxisTag = "Z"; }

        Out.ScaleMultiplier  = Mult;
        InOutTransform.Scale = {
            DragStartTransform.Scale.X * Mult.X,
            DragStartTransform.Scale.Y * Mult.Y,
            DragStartTransform.Scale.Z * Mult.Z,
        };
        // Note: WorldRadius intentionally stays constant per Slate GizmoPRO rule!
        Out.TransformChanged = true;
        std::snprintf(Buffer, sizeof(Buffer), "%s scale x%.2f", AxisTag, Factor);
        Out.HudReadout = Buffer;
        return Out;
    }

    Mathematics::Axis3D CurrentHit {};
    if (!EvaluatePlaneRayIntersection(DragStartPivotPosition, DragPlaneNormal, CurrentRay, CurrentHit))
    {
        return Out;
    }

    if (ActiveDragHandle == GizmoHandleOrdinal::PlaneYZ ||
        ActiveDragHandle == GizmoHandleOrdinal::PlaneXZ ||
        ActiveDragHandle == GizmoHandleOrdinal::PlaneXY ||
        ActiveDragHandle == GizmoHandleOrdinal::CenterRing)
    {
        Mathematics::Axis3D Offset = CurrentHit - DragInitialPlaneHit;
        if (SnapModifierActive)
        {
            const float CompU = SnapIncrement(Mathematics::EvaluateDot(Offset, DragAxisPrimary), MoveStep);
            const float CompV = SnapIncrement(Mathematics::EvaluateDot(Offset, DragAxisSecondary), MoveStep);
            Offset = DragAxisPrimary * CompU + DragAxisSecondary * CompV;
        }

        Out.TranslationDelta    = Offset;
        InOutTransform.Position = DragStartTransform.Position + Offset;
        PivotPosition           = InOutTransform.Position;
        Out.TransformChanged    = true;

        const char* PlaneTag = ActiveDragHandle == GizmoHandleOrdinal::PlaneYZ ? "X-plane"
            : (ActiveDragHandle == GizmoHandleOrdinal::PlaneXZ ? "Y-plane"
            : (ActiveDragHandle == GizmoHandleOrdinal::CenterRing ? "Screen" : "Z-plane"));
        std::snprintf(Buffer, sizeof(Buffer), "%s move (%.2f, %.2f)", PlaneTag, Offset.X, Offset.Y);
        Out.HudReadout = Buffer;
        return Out;
    }

    // Rotation Arc Bar drag (atan2 around PivotPosition)
    const Mathematics::Axis3D LocalVec = CurrentHit - DragStartPivotPosition;
    const float CurrentAngleRad = std::atan2(
        Mathematics::EvaluateDot(LocalVec, DragAxisSecondary),
        Mathematics::EvaluateDot(LocalVec, DragAxisPrimary));

    float DeltaDeg = (CurrentAngleRad - DragInitialAngleRad) * Constants::RadToDegFloat;
    while (DeltaDeg > 180.0f)  { DeltaDeg -= 360.0f; }
    while (DeltaDeg < -180.0f) { DeltaDeg += 360.0f; }
    if (SnapModifierActive)
    {
        DeltaDeg = SnapIncrement(DeltaDeg, SnapSettings.RotationStepDegrees);
    }

    const char* RotTag = "Z";
    if (ActiveDragHandle == GizmoHandleOrdinal::RotateX)
    {
        Out.RotationDegrees.X     = DeltaDeg;
        InOutTransform.Rotation.X = DragStartTransform.Rotation.X + DeltaDeg;
        RotTag = "X";
    }
    else if (ActiveDragHandle == GizmoHandleOrdinal::RotateY)
    {
        Out.RotationDegrees.Y     = DeltaDeg;
        InOutTransform.Rotation.Y = DragStartTransform.Rotation.Y + DeltaDeg;
        RotTag = "Y";
    }
    else
    {
        Out.RotationDegrees.Z     = DeltaDeg;
        InOutTransform.Rotation.Z = DragStartTransform.Rotation.Z + DeltaDeg;
        RotTag = DimensionMode == GizmoDimensionMode::Planar2D ? "UV" : "Z";
    }

    Out.TransformChanged = true;
    std::snprintf(Buffer, sizeof(Buffer), "%s rotate %+.1f deg", RotTag, DeltaDeg);
    Out.HudReadout = Buffer;
    return Out;
}

void TransformGizmo::EndDragInteraction() noexcept
{
    ActiveDragHandle = GizmoHandleOrdinal::None;
}

} // namespace Frontier::Editor
