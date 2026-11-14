/*============================================================================================================================================
                                                         UVVIEWPORTINTERACTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVViewportInteractor driving 2D/3D camera navigation, synchronized GPU picking, TransformGizmo drag & modal G/R/S

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVViewportInteractor.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/CoordinateSpace.h"
#include "Engine/Workspaces/UV/UVPaneAppearance.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] Mathematics::Axis3D TransformObjectPoint(
    Mathematics::Axis3D Point,
    Mathematics::Axis3D Pivot,
    const Mathematics::SpatialTransform& Transform) noexcept
{
    Mathematics::Axis3D P = Point - Pivot;
    P.X *= Transform.Scale.X;
    P.Y *= Transform.Scale.Y;
    P.Z *= Transform.Scale.Z;

    const float Rx = Transform.Rotation.X * Constants::DegToRadFloat;
    const float Ry = Transform.Rotation.Y * Constants::DegToRadFloat;
    const float Rz = Transform.Rotation.Z * Constants::DegToRadFloat;

    const float CosX = std::cos(Rx), SinX = std::sin(Rx);
    P = { P.X, P.Y * CosX - P.Z * SinX, P.Y * SinX + P.Z * CosX };
    const float CosY = std::cos(Ry), SinY = std::sin(Ry);
    P = { P.X * CosY + P.Z * SinY, P.Y, -P.X * SinY + P.Z * CosY };
    const float CosZ = std::cos(Rz), SinZ = std::sin(Rz);
    P = { P.X * CosZ - P.Y * SinZ, P.X * SinZ + P.Y * CosZ, P.Z };
    return Transform.Position + P;
}

[[nodiscard]] bool SpatialTransformChanged(
    const Mathematics::SpatialTransform& A,
    const Mathematics::SpatialTransform& B) noexcept
{
    constexpr float Epsilon = 1.0e-5f;
    return std::abs(A.Position.X - B.Position.X) > Epsilon ||
        std::abs(A.Position.Y - B.Position.Y) > Epsilon ||
        std::abs(A.Position.Z - B.Position.Z) > Epsilon ||
        std::abs(A.Rotation.X - B.Rotation.X) > Epsilon ||
        std::abs(A.Rotation.Y - B.Rotation.Y) > Epsilon ||
        std::abs(A.Rotation.Z - B.Rotation.Z) > Epsilon ||
        std::abs(A.Scale.X - B.Scale.X) > Epsilon ||
        std::abs(A.Scale.Y - B.Scale.Y) > Epsilon ||
        std::abs(A.Scale.Z - B.Scale.Z) > Epsilon;
}

[[nodiscard]] uint32_t QueryOppositeIfCoincident2D(
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             EdgeIndex) noexcept
{
    if (EdgeIndex >= Network.Edges.size())
    {
        return GeometrySpace::InvalidNetworkIndex;
    }
    const auto& Edge = Network.Edges[EdgeIndex];
    const uint32_t OppIdx = Edge.OppositeEdgeIndex;
    if (OppIdx >= Network.Edges.size() ||
        GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut))
    {
        return GeometrySpace::InvalidNetworkIndex;
    }
    const uint32_t NextIdx    = Edge.NextEdgeIndex;
    const uint32_t OppNextIdx = Network.Edges[OppIdx].NextEdgeIndex;
    if (NextIdx >= Network.Edges.size() || OppNextIdx >= Network.Edges.size())
    {
        return GeometrySpace::InvalidNetworkIndex;
    }
    const auto& A0 = Edge.ParameterUV;
    const auto& A1 = Network.Edges[NextIdx].ParameterUV;
    const auto& B0 = Network.Edges[OppNextIdx].ParameterUV;
    const auto& B1 = Network.Edges[OppIdx].ParameterUV;
    if (std::hypot(A0.U - B0.U, A0.V - B0.V) <= 1.0e-4f &&
        std::hypot(A1.U - B1.U, A1.V - B1.V) <= 1.0e-4f)
    {
        return OppIdx;
    }
    return GeometrySpace::InvalidNetworkIndex;
}

struct ScreenProjectionParameters
{
    bool                   PlanarProjectionEnabled = true;
    Rectangle2D            Bounds                  {};
    Mathematics::UVAxes    MinUV                   {};
    Mathematics::UVAxes    MaxUV                   {};
    float                  ViewProj[4][4]          {};
};

[[nodiscard]] ScreenProjectionParameters BuildScreenProjectionParameters(
    const Editor::ViewportProjection&     Proj,
    const GeometricRaster::CameraOrbitRig3D& CamRig,
    Rectangle2D                           Bounds,
    bool                                  Planar) noexcept
{
    ScreenProjectionParameters Params {};
    Params.PlanarProjectionEnabled = Planar;
    Params.Bounds                  = Bounds;
    if (Planar)
    {
        const uint32_t W = static_cast<uint32_t>(std::max(16.0f, Bounds.Width));
        const uint32_t H = static_cast<uint32_t>(std::max(16.0f, Bounds.Height));
        Proj.EvaluateAspectLockedPlanarBounds(W, H, Params.MinUV, Params.MaxUV);
    }
    else
    {
        const float Aspect = std::max(0.01f, Bounds.Width / std::max(1.0f, Bounds.Height));
        const Mathematics::Axis3D Eye = CamRig.TargetPivot + Mathematics::EvaluateOrbitOffsetFromAngles(
            CamRig.DistanceMeters,
            CamRig.YawRadians,
            CamRig.PitchRadians,
            CamRig.Basis);
        Mathematics::Axis3D Right {}, Up {}, Fwd {};
        Mathematics::EvaluateOrthonormalCameraBasis(Eye, CamRig.TargetPivot, CamRig.Basis, Right, Up, Fwd);
        const float FarZ = std::max(250.0f, CamRig.DistanceMeters * 6.0f);
        Mathematics::ClipTransform4x4 Clip {};
        if (CamRig.OrthographicProjection)
        {
            const float TanHalf  = std::tan(std::clamp(CamRig.VerticalFovDeg, 5.0f, 165.0f) * Constants::DegToRadFloat * 0.5f);
            const float VertSpan = std::max(0.10f, 2.0f * CamRig.DistanceMeters * TanHalf);
            Clip = Mathematics::ConstructVulkanOrthographicViewClip(Eye, Right, Up, Fwd, VertSpan, Aspect, -FarZ * 0.5f, FarZ);
        }
        else
        {
            Clip = Mathematics::ConstructVulkanPerspectiveViewClip(Eye, Right, Up, Fwd, CamRig.VerticalFovDeg, Aspect, 0.08f, FarZ);
        }
        std::memcpy(Params.ViewProj, Clip.Rows, sizeof(Clip.Rows));
    }
    return Params;
}

[[nodiscard]] bool ProjectEdgeCornerToLogicalScreen(
    const ScreenProjectionParameters&    Params,
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             EdgeIndex,
    Point2D&                             OutPoint) noexcept
{
    if (EdgeIndex >= Network.Edges.size())
    {
        return false;
    }
    const auto& DE = Network.Edges[EdgeIndex];
    if (Params.PlanarProjectionEnabled)
    {
        const float SpanU = std::max(1.0e-5f, Params.MaxUV.U - Params.MinUV.U);
        const float SpanV = std::max(1.0e-5f, Params.MaxUV.V - Params.MinUV.V);
        const float Nx    = (DE.ParameterUV.U - Params.MinUV.U) / SpanU;
        const float Ny    = (Params.MaxUV.V - DE.ParameterUV.V) / SpanV;
        OutPoint          = { Params.Bounds.X + Nx * Params.Bounds.Width, Params.Bounds.Y + Ny * Params.Bounds.Height };
        return true;
    }

    if (DE.OriginVertexIndex >= Network.Vertices.size())
    {
        return false;
    }
    const auto& P = Network.Vertices[DE.OriginVertexIndex].Axes;
    const float Cx = Params.ViewProj[0][0] * P.X + Params.ViewProj[0][1] * P.Y + Params.ViewProj[0][2] * P.Z + Params.ViewProj[0][3];
    const float Cy = Params.ViewProj[1][0] * P.X + Params.ViewProj[1][1] * P.Y + Params.ViewProj[1][2] * P.Z + Params.ViewProj[1][3];
    const float Cw = Params.ViewProj[3][0] * P.X + Params.ViewProj[3][1] * P.Y + Params.ViewProj[3][2] * P.Z + Params.ViewProj[3][3];
    if (Cw <= 1.0e-5f)
    {
        return false;
    }
    const float NdcX = Cx / Cw;
    const float NdcY = Cy / Cw;
    OutPoint = {
        Params.Bounds.X + (NdcX * 0.5f + 0.5f) * Params.Bounds.Width,
        Params.Bounds.Y + (NdcY * 0.5f + 0.5f) * Params.Bounds.Height,
    };
    return true;
}

[[nodiscard]] uint32_t FindNearestFaceVertexInScreenPixels(
    const ScreenProjectionParameters&    Params,
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             FaceIndex,
    Point2D                              Cursor) noexcept
{
    if (FaceIndex >= Network.Faces.size())
    {
        return GeometrySpace::InvalidNetworkIndex;
    }
    const auto& F         = Network.Faces[FaceIndex];
    uint32_t    BestVert  = GeometrySpace::InvalidNetworkIndex;
    float       BestDist2 = 1.0e30f;
    uint32_t    ECur      = F.BoundaryLoopEdgeIndex;
    for (uint16_t C = 0u; C < F.CornerCount && ECur < Network.Edges.size(); ++C)
    {
        Point2D ScreenPt {};
        if (ProjectEdgeCornerToLogicalScreen(Params, Network, ECur, ScreenPt))
        {
            const float Dx = ScreenPt.X - Cursor.X;
            const float Dy = ScreenPt.Y - Cursor.Y;
            const float D2 = Dx * Dx + Dy * Dy;
            if (D2 < BestDist2)
            {
                BestDist2 = D2;
                BestVert  = Network.Edges[ECur].OriginVertexIndex;
            }
        }
        ECur = Network.Edges[ECur].NextEdgeIndex;
    }
    return BestVert;
}

[[nodiscard]] float PointToSegmentDistSq(Point2D P, Point2D A, Point2D B) noexcept
{
    const float Vx = B.X - A.X;
    const float Vy = B.Y - A.Y;
    const float LenSq = Vx * Vx + Vy * Vy;
    if (LenSq <= 1.0e-6f)
    {
        return (P.X - A.X) * (P.X - A.X) + (P.Y - A.Y) * (P.Y - A.Y);
    }
    const float T  = std::clamp(((P.X - A.X) * Vx + (P.Y - A.Y) * Vy) / LenSq, 0.0f, 1.0f);
    const float Cx = A.X + T * Vx;
    const float Cy = A.Y + T * Vy;
    return (P.X - Cx) * (P.X - Cx) + (P.Y - Cy) * (P.Y - Cy);
}

[[nodiscard]] bool PointInPolygon2D(Point2D Point, const std::vector<Point2D>& Polygon) noexcept
{
    if (Polygon.size() < 3u)
    {
        return false;
    }
    bool Inside = false;
    std::size_t Prev = Polygon.size() - 1u;
    for (std::size_t Cur = 0u; Cur < Polygon.size(); Prev = Cur++)
    {
        const Point2D A = Polygon[Cur];
        const Point2D B = Polygon[Prev];
        const bool CrossesY = ((A.Y > Point.Y) != (B.Y > Point.Y));
        if (CrossesY)
        {
            const float Denom = (B.Y - A.Y);
            if (std::abs(Denom) > 1.0e-6f)
            {
                const float IntersectX = (B.X - A.X) * (Point.Y - A.Y) / Denom + A.X;
                if (Point.X < IntersectX)
                {
                    Inside = !Inside;
                }
            }
        }
    }
    return Inside;
}

[[nodiscard]] bool SegmentsIntersect2D(Point2D A, Point2D B, Point2D C, Point2D D) noexcept
{
    const float MinAx = std::min(A.X, B.X), MaxAx = std::max(A.X, B.X);
    const float MinAy = std::min(A.Y, B.Y), MaxAy = std::max(A.Y, B.Y);
    const float MinCx = std::min(C.X, D.X), MaxCx = std::max(C.X, D.X);
    const float MinCy = std::min(C.Y, D.Y), MaxCy = std::max(C.Y, D.Y);
    if (MaxAx < MinCx || MaxCx < MinAx || MaxAy < MinCy || MaxCy < MinAy)
    {
        return false;
    }
    auto Cross = [](Point2D O, Point2D P, Point2D Q) noexcept -> float
    {
        return (P.X - O.X) * (Q.Y - O.Y) - (P.Y - O.Y) * (Q.X - O.X);
    };
    const float D1 = Cross(C, D, A);
    const float D2 = Cross(C, D, B);
    const float D3 = Cross(A, B, C);
    const float D4 = Cross(A, B, D);
    if (((D1 > 0.0f && D2 < 0.0f) || (D1 < 0.0f && D2 > 0.0f)) &&
        ((D3 > 0.0f && D4 < 0.0f) || (D3 < 0.0f && D4 > 0.0f)))
    {
        return true;
    }
    constexpr float Eps = 1.0e-4f;
    return (std::abs(D1) <= Eps && A.X >= MinCx && A.X <= MaxCx && A.Y >= MinCy && A.Y <= MaxCy) ||
           (std::abs(D2) <= Eps && B.X >= MinCx && B.X <= MaxCx && B.Y >= MinCy && B.Y <= MaxCy) ||
           (std::abs(D3) <= Eps && C.X >= MinAx && C.X <= MaxAx && C.Y >= MinAy && C.Y <= MaxAy) ||
           (std::abs(D4) <= Eps && D.X >= MinAx && D.X <= MaxAx && D.Y >= MinAy && D.Y <= MaxAy);
}

[[nodiscard]] bool SegmentHitsPolygon2D(Point2D A, Point2D B, const std::vector<Point2D>& Polygon) noexcept
{
    if (Polygon.size() < 3u)
    {
        return false;
    }
    if (PointInPolygon2D(A, Polygon) || PointInPolygon2D(B, Polygon))
    {
        return true;
    }
    const Point2D Mid{ 0.5f * (A.X + B.X), 0.5f * (A.Y + B.Y) };
    if (PointInPolygon2D(Mid, Polygon))
    {
        return true;
    }
    std::size_t Prev = Polygon.size() - 1u;
    for (std::size_t Cur = 0u; Cur < Polygon.size(); Prev = Cur++)
    {
        if (SegmentsIntersect2D(A, B, Polygon[Prev], Polygon[Cur]))
        {
            return true;
        }
    }
    return false;
}

} // namespace

void UVViewportInteractor::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session               = TargetSession;
    DraggingGizmo2D       = false;
    DraggingGizmo3D       = false;
    DragCaptureActive     = false;
    DragOrbitActive       = false;
    PendingSecondarySheet = false;
    PendingUnwrapSheet    = false;
    DragBaselineObjectVertices.clear();
    DragTargetCorners.clear();
    LassoPointsLogical.clear();
    ModalMode             = ModalUVTransformMode::Inactive;
    ActiveRegionTool      = (Session != nullptr) ? Session->ActiveRegionTool : RegionSelectTool::Inactive;
    RegionStrokeActive    = false;
    RegionTargetPlanar    = true;
    RegionHoverValid      = false;
}

bool UVViewportInteractor::ConsumeSecondarySheetRequest(Point2D& OutAnchor, bool& OutInPlanar) noexcept
{
    if (!PendingSecondarySheet)
    {
        return false;
    }
    PendingSecondarySheet = false;
    OutAnchor             = SecondarySheetAnchor;
    OutInPlanar           = SecondarySheetInPlanar;
    return true;
}

bool UVViewportInteractor::ConsumeUnwrapSheetRequest(Point2D& OutAnchor, bool& OutInPlanar) noexcept
{
    if (!PendingUnwrapSheet)
    {
        return false;
    }
    PendingUnwrapSheet = false;
    OutAnchor          = UnwrapSheetAnchor;
    OutInPlanar        = UnwrapSheetInPlanar;
    return true;
}

void UVViewportInteractor::ActivateBoxMarquee(bool Active) noexcept
{
    RegionStrokeActive = false;
    RegionHoverValid   = false;
    LassoPointsLogical.clear();
    ActiveRegionTool   = Active ? RegionSelectTool::BoxMarquee : RegionSelectTool::Inactive;
    if (Session != nullptr)
    {
        Session->ActiveRegionTool = ActiveRegionTool;
        if (Active)
        {
            Session->SeamBrushActive = false;
        }
        Session->StatusBannerText = Active
            ? "GPU Box Marquee Active [B]  |  Drag to Select"
            : "Selection Tool Active";
        Session->FramebuffersDirty = true;
    }
}

void UVViewportInteractor::ActivateCircleBrush(bool Active) noexcept
{
    RegionStrokeActive = false;
    RegionHoverValid   = false;
    LassoPointsLogical.clear();
    ActiveRegionTool   = Active ? RegionSelectTool::CircleBrush : RegionSelectTool::Inactive;
    if (Session != nullptr)
    {
        Session->ActiveRegionTool = ActiveRegionTool;
        if (Active)
        {
            Session->SeamBrushActive = false;
        }
        Session->StatusBannerText = Active
            ? "GPU Circle Brush Active [C]  |  Wheel Adjusts Radius"
            : "Selection Tool Active";
        Session->FramebuffersDirty = true;
    }
}

void UVViewportInteractor::ActivateLassoPolygon(bool Active) noexcept
{
    RegionStrokeActive = false;
    RegionHoverValid   = false;
    LassoPointsLogical.clear();
    ActiveRegionTool   = Active ? RegionSelectTool::LassoPolygon : RegionSelectTool::Inactive;
    if (Session != nullptr)
    {
        Session->ActiveRegionTool = ActiveRegionTool;
        if (Active)
        {
            Session->SeamBrushActive = false;
        }
        Session->StatusBannerText = Active
            ? "Freehand Lasso Select Active [Q]  |  Drag Loop to Select"
            : "Selection Tool Active";
        Session->FramebuffersDirty = true;
    }
}

void UVViewportInteractor::ResetActiveTools() noexcept
{
    ActiveRegionTool   = RegionSelectTool::Inactive;
    RegionStrokeActive = false;
    RegionHoverValid   = false;
    LassoPointsLogical.clear();
    if (Session != nullptr)
    {
        Session->ActiveRegionTool = RegionSelectTool::Inactive;
    }
}

Rectangle2D UVViewportInteractor::QueryMarqueeOverlayRect() const noexcept
{
    if (!RegionStrokeActive || ActiveRegionTool != RegionSelectTool::BoxMarquee)
    {
        return {};
    }
    const float MinX = std::min(MarqueeAnchorLocalX, LastRegionCursorLogical.X);
    const float MinY = std::min(MarqueeAnchorLocalY, LastRegionCursorLogical.Y);
    const float MaxX = std::max(MarqueeAnchorLocalX, LastRegionCursorLogical.X);
    const float MaxY = std::max(MarqueeAnchorLocalY, LastRegionCursorLogical.Y);
    return { MinX, MinY, std::max(1.0f, MaxX - MinX), std::max(1.0f, MaxY - MinY) };
}

void UVViewportInteractor::ProcessViewportInput(const ViewportInteractionReading& Interaction, uint64_t HistoryTickSequence)
{
    if (Session == nullptr)
    {
        return;
    }

    const auto&   Input         = Interaction.Input;
    const Point2D Cursor        { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   CursorX       = Cursor.X;
    const float   CursorY       = Cursor.Y;
    const bool    InsidePlanar  = (Interaction.PlanarBounds.Width > 1.0f) && Interaction.PlanarBounds.Contains(Cursor);
    const bool    InsideSpatial = (Interaction.SpatialBounds.Width > 1.0f) && Interaction.SpatialBounds.Contains(Cursor);

    if (ActiveRegionTool != Session->ActiveRegionTool)
    {
        ActiveRegionTool   = Session->ActiveRegionTool;
        RegionStrokeActive = false;
        LassoPointsLogical.clear();
    }

    const uint64_t ResolvedSeq = (HistoryTickSequence != 0u) ? HistoryTickSequence : ++FallbackTickSequence;
    bool WantsUndo = false;
    bool WantsRedo = false;
    UVAppearance::ResolveHistoryShortcuts(Input, WantsUndo, WantsRedo);
    if (ModalMode == ModalUVTransformMode::Inactive &&
        Session->TryConsumeGlobalHistoryShortcut(WantsUndo, WantsRedo, false, ResolvedSeq))
    {
        return;
    }

    auto& Proj = Session->AccessViewportProjection();
    Session->SpatialNavigationActive = false;
    RegionHoverValid = false;

    const auto& Calib     = Session->AxisCalibration;
    const float SensScale = std::clamp(Calib.PointerSensitivityPercent / 50.0f, 0.10f, 4.0f);

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Camera Orbit, Pan & Cursor-Centered Zoom (MMB / Alt+LMB / Mouse Wheel)
    //--------------------------------------------------------------------------------------------------------------------
    // 1. Camera Orbit, Pan & Cursor-Centered Zoom (MMB / Alt+LMB / Mouse Wheel) with Cross-Pane Drag Latch
    //--------------------------------------------------------------------------------------------------------------------
    auto ApplySpatialNavigation = [&]()
    {
        Session->SpatialNavigationActive = true;
        if (std::abs(Input.PointerDeltaX) > 1.0e-4f || std::abs(Input.PointerDeltaY) > 1.0e-4f)
        {
            if (Input.ShiftHeld)
            {
                const float DistScale = Proj.InspectSpatialCamera().DistanceMeters * Calib.PanGainPerPixel * SensScale;
                Proj.PanSpatialCamera(Input.PointerDeltaX * DistScale, Input.PointerDeltaY * DistScale);
            }
            else
            {
                const float PitchSign = Calib.InvertPitchAxis ? -1.0f : 1.0f;
                const float OrbitGain = Calib.OrbitGainRadiansPerPixel * SensScale;
                Proj.OrbitSpatialCamera(
                    -Input.PointerDeltaX * OrbitGain,
                    PitchSign * Input.PointerDeltaY * OrbitGain);
            }
            Session->FramebuffersDirty = true;
        }
    };

    auto ApplyPlanarNavigation = [&]()
    {
        Mathematics::UVAxes VisMin {}, VisMax {};
        Proj.EvaluateAspectLockedPlanarBounds(
            static_cast<uint32_t>(std::max(16.0f, Interaction.PlanarBounds.Width)),
            static_cast<uint32_t>(std::max(16.0f, Interaction.PlanarBounds.Height)),
            VisMin,
            VisMax);
        const float SpanU = (VisMax.U - VisMin.U) / std::max(16.0f, Interaction.PlanarBounds.Width);
        const float SpanV = (VisMax.V - VisMin.V) / std::max(16.0f, Interaction.PlanarBounds.Height);
        Proj.PanPlanarWindow({ -Input.PointerDeltaX * SpanU, Input.PointerDeltaY * SpanV });
        Session->FramebuffersDirty = true;
    };

    if (DragOrbitActive)
    {
        const bool AltDragHeld = Input.AltHeld && Input.PrimaryPointer.Down;
        if (Input.MiddleDown || AltDragHeld)
        {
            if (DragTargetPlanar)
            {
                ApplyPlanarNavigation();
            }
            else
            {
                ApplySpatialNavigation();
            }
            return;
        }
        DragOrbitActive   = false;
        DragCaptureActive = false;
    }

    if (InsideSpatial)
    {
        if (std::abs(Input.WheelDeltaY) > 1.0e-4f)
        {
            if (ActiveRegionTool == RegionSelectTool::CircleBrush)
            {
                BrushRadiusPixels = std::clamp(BrushRadiusPixels + Input.WheelDeltaY * 3.0f, 4.0f, 128.0f);
            }
            else if (Input.ShiftHeld)
            {
                Session->SpatialNavigationActive = true;
                const float PanScale = Proj.InspectSpatialCamera().DistanceMeters * 0.08f * SensScale;
                Proj.PanSpatialCamera(0.0f, Input.WheelDeltaY * PanScale);
                Session->FramebuffersDirty = true;
            }
            else
            {
                Session->SpatialNavigationActive = true;
                const float ZoomStep = std::max(0.02f, Calib.WheelZoomStepExponent * SensScale);
                const float ZoomExponent = std::clamp(-Input.WheelDeltaY * ZoomStep, -1.38629436f, 1.38629436f);
                Proj.DollySpatialCamera(std::exp(ZoomExponent));
                Session->FramebuffersDirty = true;
            }
        }
        const bool AltOrbitMoved = Input.AltHeld && Input.PrimaryPointer.Down && !Input.PrimaryPointer.Pressed &&
            (std::abs(Input.PointerDeltaX) > 1.0e-4f || std::abs(Input.PointerDeltaY) > 1.0e-4f);
        if (Input.MiddleDown || AltOrbitMoved)
        {
            DragOrbitActive   = true;
            DragCaptureActive = true;
            DragTargetPlanar  = false;
            ApplySpatialNavigation();
            return;
        }
    }
    else if (InsidePlanar)
    {
        if (std::abs(Input.WheelDeltaY) > 1.0e-4f)
        {
            if (ActiveRegionTool == RegionSelectTool::CircleBrush)
            {
                BrushRadiusPixels = std::clamp(BrushRadiusPixels + Input.WheelDeltaY * 3.0f, 4.0f, 128.0f);
            }
            else
            {
                const uint32_t W = static_cast<uint32_t>(std::max(16.0f, Interaction.PlanarBounds.Width));
                const uint32_t H = static_cast<uint32_t>(std::max(16.0f, Interaction.PlanarBounds.Height));
                const float Factor = (Input.WheelDeltaY > 0.0f) ? 1.14f : (1.0f / 1.14f);
                Proj.ZoomPlanarAroundPoint(
                    W,
                    H,
                    CursorX - Interaction.PlanarBounds.X,
                    CursorY - Interaction.PlanarBounds.Y,
                    Factor);
                Session->FramebuffersDirty = true;
            }
        }
        const bool AltPanMoved = Input.AltHeld && Input.PrimaryPointer.Down && !Input.PrimaryPointer.Pressed && Input.ShiftHeld &&
            (std::abs(Input.PointerDeltaX) > 1.0e-4f || std::abs(Input.PointerDeltaY) > 1.0e-4f);
        if (Input.MiddleDown || AltPanMoved)
        {
            DragOrbitActive   = true;
            DragCaptureActive = true;
            DragTargetPlanar  = true;
            ApplyPlanarNavigation();
            return;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Modal G / R / S Transform State Machine
    //--------------------------------------------------------------------------------------------------------------------
    if (ModalMode != ModalUVTransformMode::Inactive)
    {
        HandleModalTransformStep(Interaction, InsidePlanar);
        return;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2b. Secondary Click Directive Sheet Request (outside gizmo/region drag)
    //--------------------------------------------------------------------------------------------------------------------
    if (Input.SecondaryPressed && (InsidePlanar || InsideSpatial) &&
        !DraggingGizmo2D && !DraggingGizmo3D && !RegionStrokeActive)
    {
        PendingSecondarySheet  = true;
        SecondarySheetAnchor   = Cursor;
        SecondarySheetInPlanar = InsidePlanar;
        return;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Keyboard Shortcuts (1-4 Modes, F Frame, K Seam Brush, P Pin, V Stitch, Y Split, B/C Region, G/R/S Modal)
    //--------------------------------------------------------------------------------------------------------------------
    if (InsidePlanar || InsideSpatial)
    {
        HandleKeyboardShortcuts(Interaction, InsidePlanar, InsideSpatial);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. GPU Hover, Click, Marquee/Brush & TransformGizmo Drag
    //--------------------------------------------------------------------------------------------------------------------
    HandleGizmoOrSurfaceInteraction(Interaction, InsidePlanar, InsideSpatial);
}

void UVViewportInteractor::HandleKeyboardShortcuts(
    const ViewportInteractionReading& Interaction,
    bool                            InsidePlanar,
    bool                            InsideSpatial)
{
    const auto& Input = Interaction.Input;
    if (Input.EscapePressed)
    {
        ActiveRegionTool          = RegionSelectTool::Inactive;
        Session->ActiveRegionTool = RegionSelectTool::Inactive;
        RegionStrokeActive        = false;
        RegionHoverValid          = false;
        LassoPointsLogical.clear();
        Session->SeamBrushActive  = false;
        Session->Gizmo2D.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);
        Session->Gizmo3D.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);
        Session->StatusBannerText = "Cleared Active Viewport Tool";
        Session->FramebuffersDirty = true;
        return;
    }

    if (Input.ShortcutAll)
    {
        Session->SelectAllInActiveMode();
        return;
    }
    if (Input.ShortcutInvert)
    {
        Session->InvertSelectionInActiveMode();
        return;
    }
    if (Input.ShortcutLinked)
    {
        Session->SelectLinkedIslandInActiveMode();
        return;
    }
    if (Input.ShortcutUnwrap)
    {
        PendingUnwrapSheet    = false;
        PendingSecondarySheet = false;
        Session->QueueDirective(UVCommandDirective::UnwrapConformal);
        return;
    }

    for (const char RawCh : Input.TypedUtf8)
    {
        const char Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(RawCh)));
        switch (Ch)
        {
            case '1':
                Session->AssignSelectionMode(UVSelectionMode::Vertices);
                break;
            case '2':
                Session->AssignSelectionMode(UVSelectionMode::Edges);
                break;
            case '3':
                Session->AssignSelectionMode(UVSelectionMode::Faces);
                break;
            case '4':
                Session->AssignSelectionMode(UVSelectionMode::Islands);
                break;
            case '5':
                Session->AssignSelectionMode(UVSelectionMode::Layers);
                break;
            case 'f':
                Session->FitSelectionOrNetwork();
                Session->StatusBannerText = "Framed Active Selection [F]";
                break;
            case 'k':
                Session->SeamBrushActive  = !Session->SeamBrushActive;
                ActiveRegionTool          = RegionSelectTool::Inactive;
                Session->ActiveRegionTool = RegionSelectTool::Inactive;
                RegionStrokeActive        = false;
                LassoPointsLogical.clear();
                if (Session->SeamBrushActive)
                {
                    Session->WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled = true;
                    Session->RenderOptions2D.SeamOverlayEnabled = true;
                    Session->RenderOptions3D.SeamOverlayEnabled = true;
                }
                Session->FramebuffersDirty = true;
                Session->StatusBannerText = Session->SeamBrushActive
                    ? "Seam Brush Active [K]  |  Click Edges to Mark, Alt+Click to Clear, Ctrl+Click Loop"
                    : "Seam Brush Disabled";
                break;
            case 'b':
                ActivateBoxMarquee(ActiveRegionTool != RegionSelectTool::BoxMarquee);
                break;
            case 'c':
                ActivateCircleBrush(ActiveRegionTool != RegionSelectTool::CircleBrush);
                break;
            case 'q':
                ActivateLassoPolygon(ActiveRegionTool != RegionSelectTool::LassoPolygon);
                break;
            case 'l':
                Session->SelectLinkedIslandInActiveMode();
                break;
            case 'p':
                Session->QueueDirective(Input.AltHeld ? UVCommandDirective::UnpinSelectedUVs : UVCommandDirective::PinSelectedUVs);
                break;
            case 'u':
                if (Input.ControlHeld)
                {
                    PendingUnwrapSheet    = false;
                    PendingSecondarySheet = false;
                    Session->QueueDirective(UVCommandDirective::UnwrapConformal);
                }
                else
                {
                    PendingSecondarySheet = false;
                    PendingUnwrapSheet    = true;
                    UnwrapSheetInPlanar   = InsidePlanar || !InsideSpatial;
                    if (InsidePlanar || InsideSpatial)
                    {
                        UnwrapSheetAnchor = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
                    }
                    else
                    {
                        const auto& AnchorRect = (Interaction.PlanarBounds.Width > 1.0f)
                            ? Interaction.PlanarBounds
                            : Interaction.SpatialBounds;
                        UnwrapSheetAnchor = {
                            AnchorRect.X + AnchorRect.Width * 0.5f,
                            AnchorRect.Y + AnchorRect.Height * 0.35f,
                        };
                    }
                }
                break;
            case 'v':
                Session->QueueDirective(UVCommandDirective::StitchSelectedSeams);
                break;
            case 'y':
                Session->QueueDirective(UVCommandDirective::SplitSelectedIsland);
                break;
            case '7':
                if (InsideSpatial)
                {
                    Session->AccessViewportProjection().ApplySpatialPreset(Editor::SpatialProjectionPreset::Top);
                    Session->FramebuffersDirty = true;
                }
                break;
            case 'g':
            case 'r':
            case 's':
                if (InsidePlanar)
                {
                    DragTargetFaces   = Session->SelectionMask.CollectSelectedFaces();
                    DragTargetCorners = UVAuthoringOperations::CollectTransformableCornerEdges(
                        Session->Asset.Network, Session->SelectionMask);
                    if (!DragTargetCorners.empty())
                    {
                        DragBaselineUVStream = Session->Asset.Network.ExtractParameterizedUVStream();
                        Session->RefreshGizmoAnchorsFromSelection();
                        const auto Anchor  = Session->Gizmo2D.QueryPivotOrigin();
                        DragPivotUV        = Mathematics::UVAxes{ Anchor.X, Anchor.Y };
                        ModalMode          = (Ch == 'g') ? ModalUVTransformMode::GrabTranslate
                                           : (Ch == 'r') ? ModalUVTransformMode::RotateAngle
                                                         : ModalUVTransformMode::ScaleUniform;
                        ModalConstraint    = ModalAxisConstraint::Unconstrained;
                        ModalNumericEntry.clear();
                        ModalStartPointerX = Input.PrimaryPointer.X;
                        ModalStartPointerY = Input.PrimaryPointer.Y;
                    }
                }
                break;
            default:
                break;
        }
    }
}

void UVViewportInteractor::HandleModalTransformStep(
    const ViewportInteractionReading& Interaction,
    bool                            InsidePlanar)
{
    (void)InsidePlanar;
    const auto& Input = Interaction.Input;

    // Cancel on Escape or SecondaryPressed
    if (Input.EscapePressed || Input.SecondaryPressed)
    {
        (void)Session->Asset.Network.ApplyParameterizedUVStream(DragBaselineUVStream);
        Session->RefreshGizmoAnchorsFromSelection();
        if (auto* Rast = Session->AccessRasterizer())
        {
            (void)Rast->UploadPolygonNetwork(Session->Asset.Network, Session->RootInstanceToken.SlotIndex);
        }
        Session->FramebuffersDirty = true;
        ModalMode                  = ModalUVTransformMode::Inactive;
        Session->StatusBannerText  = "Cancelled Modal UV Transform";
        return;
    }

    // Axis constraints & numeric entry
    for (const char RawCh : Input.TypedUtf8)
    {
        const char Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(RawCh)));
        if (Ch == 'x' || Ch == 'u')
        {
            ModalConstraint = (ModalConstraint == ModalAxisConstraint::LockU)
                ? ModalAxisConstraint::Unconstrained
                : ModalAxisConstraint::LockU;
        }
        else if (Ch == 'y' || Ch == 'v')
        {
            ModalConstraint = (ModalConstraint == ModalAxisConstraint::LockV)
                ? ModalAxisConstraint::Unconstrained
                : ModalAxisConstraint::LockV;
        }
        else if (((Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '.') && ModalNumericEntry.size() < 16u)
        {
            const bool IsSign = (Ch == '-');
            const bool IsDot = (Ch == '.');
            if ((!IsSign || ModalNumericEntry.empty()) &&
                (!IsDot || ModalNumericEntry.find('.') == std::string::npos))
            {
                ModalNumericEntry.push_back(Ch);
            }
        }
    }
    if (Input.BackspacePressed && !ModalNumericEntry.empty())
    {
        ModalNumericEntry.pop_back();
    }

    // Restore clean baseline before applying live delta
    (void)Session->Asset.Network.ApplyParameterizedUVStream(DragBaselineUVStream);

    const float DxPx = Input.PrimaryPointer.X - ModalStartPointerX;
    const float DyPx = Input.PrimaryPointer.Y - ModalStartPointerY;
    const float RawParsedVal = ModalNumericEntry.empty() ? 0.0f : std::strtof(ModalNumericEntry.c_str(), nullptr);
    const bool  HasNumeric   = !ModalNumericEntry.empty() && std::isfinite(RawParsedVal);
    const float ParsedVal    = HasNumeric ? std::clamp(RawParsedVal, -10000.0f, 10000.0f) : 0.0f;

    Mathematics::UVAxes   DeltaUV { 0.0f, 0.0f };
    Mathematics::Scalar2D ScaleUV { 1.0f, 1.0f };
    float                 RotRad = 0.0f;

    if (ModalMode == ModalUVTransformMode::GrabTranslate)
    {
        float Du = HasNumeric ? ParsedVal : (DxPx / std::max(32.0f, Interaction.PlanarBounds.Width));
        float Dv = HasNumeric ? ParsedVal : (-DyPx / std::max(32.0f, Interaction.PlanarBounds.Height));
        if (Input.ControlHeld && !HasNumeric)
        {
            Du = std::round(Du * 16.0f) / 16.0f;
            Dv = std::round(Dv * 16.0f) / 16.0f;
        }
        if (ModalConstraint == ModalAxisConstraint::LockU)
        {
            Dv = 0.0f;
        }
        else if (ModalConstraint == ModalAxisConstraint::LockV)
        {
            Du = 0.0f;
        }
        DeltaUV = { Du, Dv };
    }
    else if (ModalMode == ModalUVTransformMode::RotateAngle)
    {
        float Deg = std::clamp(HasNumeric ? ParsedVal : (DxPx * 0.45f), -36000.0f, 36000.0f);
        if (Input.ControlHeld && !HasNumeric)
        {
            Deg = std::round(Deg / 15.0f) * 15.0f;
        }
        RotRad = Deg * 0.0174532925f;
    }
    else if (ModalMode == ModalUVTransformMode::ScaleUniform)
    {
        float Factor = HasNumeric ? std::clamp(ParsedVal, 0.01f, 100.0f) : std::clamp(1.0f + DxPx * 0.005f, 0.05f, 10.0f);
        if (Input.ControlHeld && !HasNumeric)
        {
            Factor = std::round(Factor * 10.0f) / 10.0f;
        }
        ScaleUV = {
            (ModalConstraint == ModalAxisConstraint::LockV) ? 1.0f : Factor,
            (ModalConstraint == ModalAxisConstraint::LockU) ? 1.0f : Factor,
        };
    }

    if (!DragTargetCorners.empty())
    {
        UVAuthoringOperations::TransformSelectedCornerUVs(
            Session->Asset.Network, DragTargetCorners, DragPivotUV, DeltaUV, RotRad, ScaleUV);
    }
    else
    {
        UVAuthoringOperations::TransformSelectedFaceUVs(
            Session->Asset.Network, DragTargetFaces, DragPivotUV, DeltaUV, RotRad, ScaleUV);
    }
    Session->RefreshGizmoAnchorsFromSelection();
    if (auto* Rast = Session->AccessRasterizer())
    {
        (void)Rast->UploadPolygonNetwork(Session->Asset.Network, Session->RootInstanceToken.SlotIndex);
    }
    Session->FramebuffersDirty = true;

    char Hud[192]{};
    std::snprintf(
        Hud,
        sizeof(Hud),
        "Modal %s  |  dUV(%.3f, %.3f)  Rot %.1f deg  Scale(%.2f, %.2f)  [%s]",
        (ModalMode == ModalUVTransformMode::GrabTranslate) ? "Grab [G]"
            : (ModalMode == ModalUVTransformMode::RotateAngle) ? "Rotate [R]" : "Scale [S]",
        DeltaUV.U,
        DeltaUV.V,
        RotRad * 57.2957795f,
        ScaleUV.X,
        ScaleUV.Y,
        ModalNumericEntry.empty() ? "LMB/Enter Confirm, Esc Cancel" : ModalNumericEntry.c_str());
    Session->StatusBannerText = Hud;

    if (Input.PrimaryPointer.Pressed || Input.EnterPressed)
    {
        Session->FinalizeInteractiveUVRevision("ModalUVTransform");
        ModalMode = ModalUVTransformMode::Inactive;
    }
}

void UVViewportInteractor::HandleGizmoOrSurfaceInteraction(
    const ViewportInteractionReading& Interaction,
    bool                            InsidePlanar,
    bool                            InsideSpatial)
{
    auto* Rast = Session->AccessRasterizer();
    if (Rast == nullptr)
    {
        return;
    }

    const auto& Input = Interaction.Input;
    auto&       Proj  = Session->AccessViewportProjection();

    //--------------------------------------------------------------------------------------------------------------------
    // Active 2D Gizmo Drag Continuation
    //--------------------------------------------------------------------------------------------------------------------
    if (DraggingGizmo2D)
    {
        const uint32_t W   = static_cast<uint32_t>(std::max(16.0f, Interaction.PlanarBounds.Width));
        const uint32_t H   = static_cast<uint32_t>(std::max(16.0f, Interaction.PlanarBounds.Height));
        const auto     Ray = Proj.BuildPlanarRay(
            W,
            H,
            Input.PrimaryPointer.X - Interaction.PlanarBounds.X,
            Input.PrimaryPointer.Y - Interaction.PlanarBounds.Y);

        const Configuration::GizmoSnapSettings Snap {};
        const auto Res = Session->Gizmo2D.EvaluateDragInteraction(
            Ray,
            Input.ControlHeld,
            Snap,
            ActiveGizmoTransform);

        if (Res.TransformChanged && (!DragTargetCorners.empty() || !DragTargetFaces.empty()))
        {
            (void)Session->Asset.Network.ApplyParameterizedUVStream(DragBaselineUVStream);
            const Mathematics::UVAxes DeltaUV {
                ActiveGizmoTransform.Position.X - DragPivotUV.U,
                ActiveGizmoTransform.Position.Y - DragPivotUV.V,
            };
            const Mathematics::Scalar2D Scale2D {
                ActiveGizmoTransform.Scale.X,
                ActiveGizmoTransform.Scale.Y,
            };
            if (!DragTargetCorners.empty())
            {
                UVAuthoringOperations::TransformSelectedCornerUVs(
                    Session->Asset.Network,
                    DragTargetCorners,
                    DragPivotUV,
                    DeltaUV,
                    ActiveGizmoTransform.Rotation.Z,
                    Scale2D);
            }
            else
            {
                UVAuthoringOperations::TransformSelectedFaceUVs(
                    Session->Asset.Network,
                    DragTargetFaces,
                    DragPivotUV,
                    DeltaUV,
                    ActiveGizmoTransform.Rotation.Z,
                    Scale2D);
            }

            (void)Rast->UploadPolygonNetwork(Session->Asset.Network, Session->RootInstanceToken.SlotIndex);
            Session->StatusBannerText  = Res.HudReadout;
            Session->FramebuffersDirty = true;
        }

        if (Input.PrimaryPointer.Released || !Input.PrimaryPointer.Down)
        {
            Session->Gizmo2D.EndDragInteraction();
            DraggingGizmo2D = false;
            Session->FinalizeInteractiveUVRevision("Gizmo2DTransform");
        }
        return;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // Active 3D Gizmo Drag Continuation
    //--------------------------------------------------------------------------------------------------------------------
    if (DraggingGizmo3D)
    {
        Session->SpatialNavigationActive = Input.PrimaryPointer.Down;
        const uint32_t W   = static_cast<uint32_t>(std::max(16.0f, Interaction.SpatialBounds.Width));
        const uint32_t H   = static_cast<uint32_t>(std::max(16.0f, Interaction.SpatialBounds.Height));
        const auto     Ray = Proj.BuildSpatialRay(
            W,
            H,
            Input.PrimaryPointer.X - Interaction.SpatialBounds.X,
            Input.PrimaryPointer.Y - Interaction.SpatialBounds.Y);

        const Configuration::GizmoSnapSettings Snap {};
        const auto Res = Session->Gizmo3D.EvaluateDragInteraction(
            Ray,
            Input.ControlHeld,
            Snap,
            ActiveGizmoTransform);
        if (Res.TransformChanged && SpatialTransformChanged(ActiveGizmoTransform, LastAppliedObjectTransform))
        {
            auto& Network = Session->Asset.Network;
            const std::size_t VertexCount = std::min(Network.Vertices.size(), DragBaselineObjectVertices.size());
            for (std::size_t Index = 0u; Index < VertexCount; ++Index)
            {
                Network.Vertices[Index].Axes = TransformObjectPoint(
                    DragBaselineObjectVertices[Index],
                    DragPivot3D,
                    ActiveGizmoTransform);
            }
            Network.RecomputeNormals();
            ++Network.NetworkRevision;
            (void)Rast->UploadPolygonNetwork(Network, Session->RootInstanceToken.SlotIndex);
            LastAppliedObjectTransform = ActiveGizmoTransform;
            Session->StatusBannerText  = Res.HudReadout;
            Session->FramebuffersDirty = true;
        }
        if (Input.PrimaryPointer.Released || !Input.PrimaryPointer.Down)
        {
            Session->Gizmo3D.EndDragInteraction();
            DraggingGizmo3D = false;
            DragBaselineObjectVertices.clear();
            Session->FinalizeInteractiveUVRevision("Gizmo3DTransform");
            Session->FramebuffersDirty = true;
        }
        return;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // Region Marquee [B] & Circle Brush [C] (handles continuation even if cursor drags outside viewport before release)
    //--------------------------------------------------------------------------------------------------------------------
    if (ActiveRegionTool != RegionSelectTool::Inactive &&
        (RegionStrokeActive || InsidePlanar || InsideSpatial))
    {
        const bool UsePlanar = RegionStrokeActive ? RegionTargetPlanar : InsidePlanar;
        const auto& Bounds   = UsePlanar ? Interaction.PlanarBounds : Interaction.SpatialBounds;
        const uint32_t FbW   = UsePlanar
            ? std::max(1u, Session->Framebuffer2DWidth > 0u ? Session->Framebuffer2DWidth : Rast->Query2DWidthPx())
            : std::max(1u, Session->Framebuffer3DWidth > 0u ? Session->Framebuffer3DWidth : Rast->Query3DWidthPx());
        const uint32_t FbH   = UsePlanar
            ? std::max(1u, Session->Framebuffer2DHeight > 0u ? Session->Framebuffer2DHeight : Rast->Query2DHeightPx())
            : std::max(1u, Session->Framebuffer3DHeight > 0u ? Session->Framebuffer3DHeight : Rast->Query3DHeightPx());

        const float ScaleX = static_cast<float>(FbW) / std::max(1.0f, Bounds.Width);
        const float ScaleY = static_cast<float>(FbH) / std::max(1.0f, Bounds.Height);

        auto ToFramebufferX = [&](float LogicalX) noexcept -> uint32_t
        {
            const float Local = std::clamp(LogicalX - Bounds.X, 0.0f, std::max(0.0f, Bounds.Width - 1.0f));
            return static_cast<uint32_t>(std::clamp(Local * ScaleX, 0.0f, static_cast<float>(FbW - 1u)));
        };
        auto ToFramebufferY = [&](float LogicalY) noexcept -> uint32_t
        {
            const float Local = std::clamp(LogicalY - Bounds.Y, 0.0f, std::max(0.0f, Bounds.Height - 1.0f));
            return static_cast<uint32_t>(std::clamp(Local * ScaleY, 0.0f, static_cast<float>(FbH - 1u)));
        };

        enum class ActiveRegionShapeKind : uint8_t
        {
            Circle = 0u,
            Box    = 1u,
            Lasso  = 2u,
        };

        auto ApplyRegionWordsToSession = [&](
            const std::vector<uint32_t>& HitFaceWords,
            ActiveRegionShapeKind        ShapeKind,
            float                        LogicalMinX,
            float                        LogicalMinY,
            float                        LogicalMaxX,
            float                        LogicalMaxY,
            bool                         ClearBeforeApply) -> bool
        {
            const auto& Net = Session->Asset.Network;
            Session->SelectionMask.SynchronizeCapacity(Net);
            const UVSelectionMode ActiveMode = Session->SelectionMode;
            const bool ClearMode = ClearBeforeApply;

            if (ActiveMode == UVSelectionMode::Vertices)
            {
                if (ClearMode)
                {
                    Session->SelectionMask.ClearAll();
                }
                const ScreenProjectionParameters ProjParams = BuildScreenProjectionParameters(
                    Proj, Session->RenderOptions3D.Camera, Bounds, UsePlanar);
                uint32_t AddedCount = 0u;
                auto TestAndAddFaceVertices = [&](uint32_t FIdx)
                {
                    if (FIdx >= Net.Faces.size()) return;
                    const auto& F = Net.Faces[FIdx];
                    uint32_t    E = F.BoundaryLoopEdgeIndex;
                    for (uint16_t C = 0u; C < F.CornerCount && E < Net.Edges.size(); ++C)
                    {
                        Point2D Pt {};
                        if (ProjectEdgeCornerToLogicalScreen(ProjParams, Net, E, Pt))
                        {
                            bool HitVert = false;
                            if (ShapeKind == ActiveRegionShapeKind::Box)
                            {
                                HitVert = (Pt.X >= LogicalMinX && Pt.X <= LogicalMaxX && Pt.Y >= LogicalMinY && Pt.Y <= LogicalMaxY);
                            }
                            else if (ShapeKind == ActiveRegionShapeKind::Lasso)
                            {
                                HitVert = PointInPolygon2D(Pt, LassoPointsLogical);
                            }
                            else
                            {
                                HitVert = ((Pt.X - LastRegionCursorLogical.X) * (Pt.X - LastRegionCursorLogical.X) +
                                           (Pt.Y - LastRegionCursorLogical.Y) * (Pt.Y - LastRegionCursorLogical.Y) <=
                                           BrushRadiusPixels * BrushRadiusPixels);
                            }
                            if (HitVert)
                            {
                                Session->SelectionMask.AssignVertexSelected(Net.Edges[E].OriginVertexIndex, true);
                                ++AddedCount;
                            }
                        }
                        E = Net.Edges[E].NextEdgeIndex;
                    }
                };
                for (uint32_t WordIdx = 0u; WordIdx < static_cast<uint32_t>(HitFaceWords.size()); ++WordIdx)
                {
                    uint32_t W = HitFaceWords[WordIdx];
                    while (W != 0u)
                    {
                        const uint32_t Bit  = static_cast<uint32_t>(std::countr_zero(W));
                        const uint32_t FIdx = (WordIdx << 5u) | Bit;
                        TestAndAddFaceVertices(FIdx);
                        W &= (W - 1u);
                    }
                }
                if (ShapeKind != ActiveRegionShapeKind::Circle && AddedCount == 0u)
                {
                    for (uint32_t FIdx = 0u; FIdx < static_cast<uint32_t>(Net.Faces.size()); ++FIdx)
                    {
                        TestAndAddFaceVertices(FIdx);
                    }
                }
                Session->RenderOptions3D.ActiveLayerOrdinal = UINT16_MAX;
                Session->RenderOptions2D.ActiveLayerOrdinal = UINT16_MAX;
                Session->SpatialSelectionActive = (Session->SelectionMask.CountSelectedVertices() > 0u);
                Session->RefreshGizmoAnchorsFromSelection();
                Session->SynchronizeSelectionToGPU();
                return AddedCount > 0u || Session->SpatialSelectionActive;
            }

            if (ActiveMode == UVSelectionMode::Edges)
            {
                if (ClearMode)
                {
                    Session->SelectionMask.ClearAll();
                }
                const ScreenProjectionParameters ProjParams = BuildScreenProjectionParameters(
                    Proj, Session->RenderOptions3D.Camera, Bounds, UsePlanar);
                uint32_t AddedCount = 0u;
                auto SegmentHitsBox = [&](Point2D A, Point2D B) noexcept -> bool
                {
                    if ((A.X >= LogicalMinX && A.X <= LogicalMaxX && A.Y >= LogicalMinY && A.Y <= LogicalMaxY) ||
                        (B.X >= LogicalMinX && B.X <= LogicalMaxX && B.Y >= LogicalMinY && B.Y <= LogicalMaxY))
                    {
                        return true;
                    }
                    const Point2D Mid{ 0.5f * (A.X + B.X), 0.5f * (A.Y + B.Y) };
                    return Mid.X >= LogicalMinX && Mid.X <= LogicalMaxX && Mid.Y >= LogicalMinY && Mid.Y <= LogicalMaxY;
                };
                auto TestAndAddFaceEdges = [&](uint32_t FIdx)
                {
                    if (FIdx >= Net.Faces.size()) return;
                    const auto& F = Net.Faces[FIdx];
                    uint32_t    E = F.BoundaryLoopEdgeIndex;
                    for (uint16_t C = 0u; C < F.CornerCount && E < Net.Edges.size(); ++C)
                    {
                        const uint32_t ENext = Net.Edges[E].NextEdgeIndex;
                        Point2D P0 {}, P1 {};
                        if (ProjectEdgeCornerToLogicalScreen(ProjParams, Net, E, P0) &&
                            ProjectEdgeCornerToLogicalScreen(ProjParams, Net, ENext, P1))
                        {
                            bool HitEdge = false;
                            if (ShapeKind == ActiveRegionShapeKind::Box)
                            {
                                HitEdge = SegmentHitsBox(P0, P1);
                            }
                            else if (ShapeKind == ActiveRegionShapeKind::Lasso)
                            {
                                HitEdge = SegmentHitsPolygon2D(P0, P1, LassoPointsLogical);
                            }
                            else
                            {
                                HitEdge = (PointToSegmentDistSq(LastRegionCursorLogical, P0, P1) <=
                                           BrushRadiusPixels * BrushRadiusPixels);
                            }
                            if (HitEdge)
                            {
                                const bool IncludeOpp = !UsePlanar ||
                                    (QueryOppositeIfCoincident2D(Net, E) != GeometrySpace::InvalidNetworkIndex);
                                Session->SelectionMask.AssignEdgeSelected(Net, E, true, IncludeOpp);
                                ++AddedCount;
                            }
                        }
                        E = ENext;
                    }
                };
                for (uint32_t WordIdx = 0u; WordIdx < static_cast<uint32_t>(HitFaceWords.size()); ++WordIdx)
                {
                    uint32_t W = HitFaceWords[WordIdx];
                    while (W != 0u)
                    {
                        const uint32_t Bit  = static_cast<uint32_t>(std::countr_zero(W));
                        const uint32_t FIdx = (WordIdx << 5u) | Bit;
                        TestAndAddFaceEdges(FIdx);
                        W &= (W - 1u);
                    }
                }
                if (ShapeKind != ActiveRegionShapeKind::Circle && AddedCount == 0u)
                {
                    for (uint32_t FIdx = 0u; FIdx < static_cast<uint32_t>(Net.Faces.size()); ++FIdx)
                    {
                        TestAndAddFaceEdges(FIdx);
                    }
                }
                Session->RenderOptions3D.ActiveLayerOrdinal = UINT16_MAX;
                Session->RenderOptions2D.ActiveLayerOrdinal = UINT16_MAX;
                Session->SpatialSelectionActive = (Session->SelectionMask.CountSelectedEdges() > 0u);
                Session->RefreshGizmoAnchorsFromSelection();
                Session->SynchronizeSelectionToGPU();
                return AddedCount > 0u || Session->SpatialSelectionActive;
            }

            std::vector<uint32_t> EffectiveFaceWords = HitFaceWords;
            if (ShapeKind == ActiveRegionShapeKind::Lasso)
            {
                const ScreenProjectionParameters ProjParams = BuildScreenProjectionParameters(
                    Proj, Session->RenderOptions3D.Camera, Bounds, UsePlanar);
                std::fill(EffectiveFaceWords.begin(), EffectiveFaceWords.end(), 0u);

                auto TestFaceAgainstLasso = [&](uint32_t FIdx) -> bool
                {
                    if (FIdx >= Net.Faces.size()) return false;
                    const auto& F = Net.Faces[FIdx];
                    uint32_t E = F.BoundaryLoopEdgeIndex;
                    std::vector<Point2D> ProjectedCorners {};
                    ProjectedCorners.reserve(F.CornerCount);
                    Point2D Centroid { 0.0f, 0.0f };
                    for (uint16_t C = 0u; C < F.CornerCount && E < Net.Edges.size(); ++C)
                    {
                        Point2D Pt {};
                        if (ProjectEdgeCornerToLogicalScreen(ProjParams, Net, E, Pt))
                        {
                            ProjectedCorners.push_back(Pt);
                            Centroid.X += Pt.X;
                            Centroid.Y += Pt.Y;
                            if (PointInPolygon2D(Pt, LassoPointsLogical))
                            {
                                return true;
                            }
                        }
                        E = Net.Edges[E].NextEdgeIndex;
                    }
                    if (!ProjectedCorners.empty())
                    {
                        Centroid.X /= static_cast<float>(ProjectedCorners.size());
                        Centroid.Y /= static_cast<float>(ProjectedCorners.size());
                        if (PointInPolygon2D(Centroid, LassoPointsLogical))
                        {
                            return true;
                        }
                        for (std::size_t I = 0u; I < ProjectedCorners.size(); ++I)
                        {
                            const Point2D& P0 = ProjectedCorners[I];
                            const Point2D& P1 = ProjectedCorners[(I + 1u) % ProjectedCorners.size()];
                            if (SegmentHitsPolygon2D(P0, P1, LassoPointsLogical))
                            {
                                return true;
                            }
                        }
                    }
                    return false;
                };

                uint32_t MatchedFaces = 0u;
                for (uint32_t WordIdx = 0u; WordIdx < static_cast<uint32_t>(HitFaceWords.size()); ++WordIdx)
                {
                    uint32_t W = HitFaceWords[WordIdx];
                    while (W != 0u)
                    {
                        const uint32_t Bit  = static_cast<uint32_t>(std::countr_zero(W));
                        const uint32_t FIdx = (WordIdx << 5u) | Bit;
                        if (TestFaceAgainstLasso(FIdx))
                        {
                            if (WordIdx < EffectiveFaceWords.size())
                            {
                                EffectiveFaceWords[WordIdx] |= (1u << Bit);
                                ++MatchedFaces;
                            }
                        }
                        W &= (W - 1u);
                    }
                }
                if (MatchedFaces == 0u && UsePlanar)
                {
                    for (uint32_t FIdx = 0u; FIdx < static_cast<uint32_t>(Net.Faces.size()); ++FIdx)
                    {
                        const uint32_t WordIdx = FIdx >> 5u;
                        const uint32_t Bit     = FIdx & 31u;
                        if (WordIdx < EffectiveFaceWords.size() && TestFaceAgainstLasso(FIdx))
                        {
                            EffectiveFaceWords[WordIdx] |= (1u << Bit);
                        }
                    }
                }
            }

            if (ActiveMode == UVSelectionMode::Islands)
            {
                std::vector<uint32_t> IslandFaces {};
                for (uint32_t WordIdx = 0u; WordIdx < static_cast<uint32_t>(EffectiveFaceWords.size()); ++WordIdx)
                {
                    uint32_t W = EffectiveFaceWords[WordIdx];
                    while (W != 0u)
                    {
                        const uint32_t Bit  = static_cast<uint32_t>(std::countr_zero(W));
                        const uint32_t FIdx = (WordIdx << 5u) | Bit;
                        const auto Island = GeometrySpace::GeometrySelection::EvaluateLinkedIslandFaces(Net, FIdx);
                        IslandFaces.insert(IslandFaces.end(), Island.begin(), Island.end());
                        W &= (W - 1u);
                    }
                }
                Session->SelectionMask.SelectFacesFromList(IslandFaces, !ClearMode);
            }
            else
            {
                if (!ClearMode)
                {
                    const auto PrevWords = Session->SelectionMask.InspectPackedFaceWords();
                    std::vector<uint32_t> Merged = EffectiveFaceWords;
                    const std::size_t OverlapN = std::min(PrevWords.size(), Merged.size());
                    for (std::size_t I = 0u; I < OverlapN; ++I)
                    {
                        Merged[I] |= PrevWords[I];
                    }
                    Session->SelectionMask.LoadPackedFaceWords(Merged);
                }
                else
                {
                    Session->SelectionMask.LoadPackedFaceWords(EffectiveFaceWords);
                }
            }
            if (ActiveMode != UVSelectionMode::Layers)
            {
                Session->RenderOptions3D.ActiveLayerOrdinal = UINT16_MAX;
                Session->RenderOptions2D.ActiveLayerOrdinal = UINT16_MAX;
            }
            Session->SpatialSelectionActive = (Session->SelectionMask.CountSelectedFaces() > 0u);
            Session->RefreshGizmoAnchorsFromSelection();
            Session->SynchronizeSelectionToGPU();
            return Session->SpatialSelectionActive;
        };

        if (ActiveRegionTool == RegionSelectTool::CircleBrush)
        {
            LastRegionCursorLogical = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
            RegionTargetPlanar      = UsePlanar;
            RegionHoverValid        = InsidePlanar || InsideSpatial;

            if (Input.PrimaryPointer.Pressed || (Input.PrimaryPointer.Down && (InsidePlanar || InsideSpatial)))
            {
                const auto SavedFaceSpan = Session->SelectionMask.InspectPackedFaceWords();
                const std::vector<uint32_t> SavedFaceWords(SavedFaceSpan.begin(), SavedFaceSpan.end());
                (void)Rast->DispatchGPUSelection(GeometricRaster::GPUSelectionOperation::ClearAll, 0u, false);

                const float LocalX  = std::clamp(Input.PrimaryPointer.X - Bounds.X, 0.0f, std::max(0.0f, Bounds.Width - 1.0f));
                const float LocalY  = std::clamp(Input.PrimaryPointer.Y - Bounds.Y, 0.0f, std::max(0.0f, Bounds.Height - 1.0f));
                const uint32_t Cx   = ToFramebufferX(Input.PrimaryPointer.X);
                const uint32_t Cy   = ToFramebufferY(Input.PrimaryPointer.Y);
                const uint32_t MinX = static_cast<uint32_t>(std::clamp((LocalX - BrushRadiusPixels) * ScaleX, 0.0f, static_cast<float>(FbW - 1u)));
                const uint32_t MinY = static_cast<uint32_t>(std::clamp((LocalY - BrushRadiusPixels) * ScaleY, 0.0f, static_cast<float>(FbH - 1u)));
                const uint32_t MaxX = static_cast<uint32_t>(std::clamp((LocalX + BrushRadiusPixels) * ScaleX, 0.0f, static_cast<float>(FbW - 1u)));
                const uint32_t MaxY = static_cast<uint32_t>(std::clamp((LocalY + BrushRadiusPixels) * ScaleY, 0.0f, static_cast<float>(FbH - 1u)));
                const uint32_t RadiusPx = static_cast<uint32_t>(std::max(1.0f, BrushRadiusPixels * std::max(ScaleX, ScaleY)));

                const auto Summary = UsePlanar
                    ? Rast->Reduce2DRegionPick(MinX, MinY, MaxX, MaxY, Cx, Cy, RadiusPx)
                    : Rast->Reduce3DRegionPick(MinX, MinY, MaxX, MaxY, Cx, Cy, RadiusPx);
                const std::vector<uint32_t> HitWords = Rast->ReadbackSelectionBitmaskWords();
                Session->SelectionMask.LoadPackedFaceWords(SavedFaceWords);

                if (Summary.HitPixelCount > 0u || Input.PrimaryPointer.Pressed)
                {
                    const bool ClearOnFirstPress = Input.PrimaryPointer.Pressed && !Input.ShiftHeld;
                    (void)ApplyRegionWordsToSession(
                        HitWords, ActiveRegionShapeKind::Circle, 0.0f, 0.0f, 0.0f, 0.0f, ClearOnFirstPress);
                }
                else
                {
                    Session->SynchronizeSelectionToGPU();
                }
                RegionStrokeActive = Input.PrimaryPointer.Down;
            }
            else if (!Input.PrimaryPointer.Down)
            {
                RegionStrokeActive = false;
            }
            return;
        }

        if (ActiveRegionTool == RegionSelectTool::LassoPolygon)
        {
            if (Input.PrimaryPointer.Pressed && (InsidePlanar || InsideSpatial))
            {
                RegionStrokeActive      = true;
                RegionTargetPlanar      = InsidePlanar;
                LastRegionCursorLogical = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
                LassoPointsLogical.clear();
                LassoPointsLogical.push_back(LastRegionCursorLogical);
            }
            else if (RegionStrokeActive && Input.PrimaryPointer.Down)
            {
                LastRegionCursorLogical = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
                if (LassoPointsLogical.empty())
                {
                    LassoPointsLogical.push_back(LastRegionCursorLogical);
                }
                else
                {
                    const Point2D& Tail = LassoPointsLogical.back();
                    const float Dx = LastRegionCursorLogical.X - Tail.X;
                    const float Dy = LastRegionCursorLogical.Y - Tail.Y;
                    if ((Dx * Dx + Dy * Dy) >= 9.0f)
                    {
                        LassoPointsLogical.push_back(LastRegionCursorLogical);
                    }
                }
            }
            else if (RegionStrokeActive && (Input.PrimaryPointer.Released || !Input.PrimaryPointer.Down))
            {
                LastRegionCursorLogical = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
                RegionStrokeActive      = false;
                if (LassoPointsLogical.empty() ||
                    ((LastRegionCursorLogical.X - LassoPointsLogical.back().X) *
                     (LastRegionCursorLogical.X - LassoPointsLogical.back().X) +
                     (LastRegionCursorLogical.Y - LassoPointsLogical.back().Y) *
                     (LastRegionCursorLogical.Y - LassoPointsLogical.back().Y)) >= 2.0f)
                {
                    LassoPointsLogical.push_back(LastRegionCursorLogical);
                }

                if (LassoPointsLogical.size() >= 3u)
                {
                    float LogicalMinX = LassoPointsLogical.front().X;
                    float LogicalMaxX = LassoPointsLogical.front().X;
                    float LogicalMinY = LassoPointsLogical.front().Y;
                    float LogicalMaxY = LassoPointsLogical.front().Y;
                    for (const Point2D& Pt : LassoPointsLogical)
                    {
                        LogicalMinX = std::min(LogicalMinX, Pt.X);
                        LogicalMaxX = std::max(LogicalMaxX, Pt.X);
                        LogicalMinY = std::min(LogicalMinY, Pt.Y);
                        LogicalMaxY = std::max(LogicalMaxY, Pt.Y);
                    }

                    const auto SavedFaceSpan = Session->SelectionMask.InspectPackedFaceWords();
                    const std::vector<uint32_t> SavedFaceWords(SavedFaceSpan.begin(), SavedFaceSpan.end());
                    (void)Rast->DispatchGPUSelection(GeometricRaster::GPUSelectionOperation::ClearAll, 0u, false);

                    const uint32_t MinX = ToFramebufferX(LogicalMinX);
                    const uint32_t MinY = ToFramebufferY(LogicalMinY);
                    const uint32_t MaxX = ToFramebufferX(LogicalMaxX);
                    const uint32_t MaxY = ToFramebufferY(LogicalMaxY);

                    (void)(UsePlanar
                        ? Rast->Reduce2DRegionPick(MinX, MinY, MaxX, MaxY, 0u, 0u, 0u)
                        : Rast->Reduce3DRegionPick(MinX, MinY, MaxX, MaxY, 0u, 0u, 0u));
                    const std::vector<uint32_t> HitWords = Rast->ReadbackSelectionBitmaskWords();
                    Session->SelectionMask.LoadPackedFaceWords(SavedFaceWords);

                    const bool Applied = ApplyRegionWordsToSession(
                        HitWords, ActiveRegionShapeKind::Lasso, LogicalMinX, LogicalMinY, LogicalMaxX, LogicalMaxY, !Input.ShiftHeld);
                    if (!Applied && !Input.ShiftHeld)
                    {
                        Session->ClearViewportSelection();
                    }
                }
                else if (!Input.ShiftHeld)
                {
                    Session->ClearViewportSelection();
                }
                LassoPointsLogical.clear();
            }
            return;
        }

        if (ActiveRegionTool == RegionSelectTool::BoxMarquee)
        {
            if (Input.PrimaryPointer.Pressed && (InsidePlanar || InsideSpatial))
            {
                RegionStrokeActive      = true;
                RegionTargetPlanar      = InsidePlanar;
                MarqueeAnchorLocalX     = Input.PrimaryPointer.X;
                MarqueeAnchorLocalY     = Input.PrimaryPointer.Y;
                LastRegionCursorLogical = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
            }
            else if (RegionStrokeActive && Input.PrimaryPointer.Down)
            {
                LastRegionCursorLogical = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
            }
            else if (RegionStrokeActive && (Input.PrimaryPointer.Released || !Input.PrimaryPointer.Down))
            {
                LastRegionCursorLogical = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
                RegionStrokeActive      = false;

                const auto SavedFaceSpan = Session->SelectionMask.InspectPackedFaceWords();
                const std::vector<uint32_t> SavedFaceWords(SavedFaceSpan.begin(), SavedFaceSpan.end());
                (void)Rast->DispatchGPUSelection(GeometricRaster::GPUSelectionOperation::ClearAll, 0u, false);

                const uint32_t Ax = ToFramebufferX(MarqueeAnchorLocalX);
                const uint32_t Ay = ToFramebufferY(MarqueeAnchorLocalY);
                const uint32_t Bx = ToFramebufferX(Input.PrimaryPointer.X);
                const uint32_t By = ToFramebufferY(Input.PrimaryPointer.Y);
                const uint32_t MinX = std::min(Ax, Bx);
                const uint32_t MinY = std::min(Ay, By);
                const uint32_t MaxX = std::max(Ax, Bx);
                const uint32_t MaxY = std::max(Ay, By);

                const float LogicalMinX = std::min(MarqueeAnchorLocalX, Input.PrimaryPointer.X);
                const float LogicalMinY = std::min(MarqueeAnchorLocalY, Input.PrimaryPointer.Y);
                const float LogicalMaxX = std::max(MarqueeAnchorLocalX, Input.PrimaryPointer.X);
                const float LogicalMaxY = std::max(MarqueeAnchorLocalY, Input.PrimaryPointer.Y);

                const auto Summary = UsePlanar
                    ? Rast->Reduce2DRegionPick(MinX, MinY, MaxX, MaxY, 0u, 0u, 0u)
                    : Rast->Reduce3DRegionPick(MinX, MinY, MaxX, MaxY, 0u, 0u, 0u);
                const std::vector<uint32_t> HitWords = Rast->ReadbackSelectionBitmaskWords();
                Session->SelectionMask.LoadPackedFaceWords(SavedFaceWords);

                const bool Applied = ApplyRegionWordsToSession(
                    HitWords, ActiveRegionShapeKind::Box, LogicalMinX, LogicalMinY, LogicalMaxX, LogicalMaxY, !Input.ShiftHeld);
                if (!Applied && Summary.HitPixelCount == 0u && !Input.ShiftHeld)
                {
                    Session->ClearViewportSelection();
                }
            }
            return;
        }
    }

    if (!InsidePlanar && !InsideSpatial)
    {
        return;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 1x1 GPU Visibility Pick (Synchronized 2D <-> 3D Hover & Click)
    //--------------------------------------------------------------------------------------------------------------------
    GeometricRaster::ViewportPickHit Hit {};
    if (InsidePlanar && Session->Framebuffer2DWidth > 0u && Session->Framebuffer2DHeight > 0u)
    {
        const uint32_t Px = static_cast<uint32_t>(std::clamp(
            (Input.PrimaryPointer.X - Interaction.PlanarBounds.X) *
                (static_cast<float>(Session->Framebuffer2DWidth) / std::max(1.0f, Interaction.PlanarBounds.Width)),
            0.0f,
            static_cast<float>(Session->Framebuffer2DWidth - 1u)));
        const uint32_t Py = static_cast<uint32_t>(std::clamp(
            (Input.PrimaryPointer.Y - Interaction.PlanarBounds.Y) *
                (static_cast<float>(Session->Framebuffer2DHeight) / std::max(1.0f, Interaction.PlanarBounds.Height)),
            0.0f,
            static_cast<float>(Session->Framebuffer2DHeight - 1u)));
        Hit = Rast->Pick2DPixel(Px, Py);
        Session->Gizmo2D.AssignHoveredHandle(static_cast<Editor::GizmoHandleOrdinal>(Hit.GizmoHandleOrdinal));
        Session->Gizmo3D.AssignHoveredHandle(Editor::GizmoHandleOrdinal::None);
    }
    else if (InsideSpatial && Session->Framebuffer3DWidth > 0u && Session->Framebuffer3DHeight > 0u)
    {
        const uint32_t Px = static_cast<uint32_t>(std::clamp(
            (Input.PrimaryPointer.X - Interaction.SpatialBounds.X) *
                (static_cast<float>(Session->Framebuffer3DWidth) / std::max(1.0f, Interaction.SpatialBounds.Width)),
            0.0f,
            static_cast<float>(Session->Framebuffer3DWidth - 1u)));
        const uint32_t Py = static_cast<uint32_t>(std::clamp(
            (Input.PrimaryPointer.Y - Interaction.SpatialBounds.Y) *
                (static_cast<float>(Session->Framebuffer3DHeight) / std::max(1.0f, Interaction.SpatialBounds.Height)),
            0.0f,
            static_cast<float>(Session->Framebuffer3DHeight - 1u)));
        Hit = Rast->Pick3DPixel(Px, Py);
        Session->Gizmo3D.AssignHoveredHandle(static_cast<Editor::GizmoHandleOrdinal>(Hit.GizmoHandleOrdinal));
        Session->Gizmo2D.AssignHoveredHandle(Editor::GizmoHandleOrdinal::None);
    }

    const auto& ActiveBounds = InsidePlanar ? Interaction.PlanarBounds : Interaction.SpatialBounds;
    uint32_t HoveredFace = Hit.HitSurfaceFace() ? Hit.FaceIndex : GeometrySpace::InvalidNetworkIndex;
    uint32_t HoveredEdge = Hit.HitSurfaceFace() ? Hit.DirectedEdgeIndex : GeometrySpace::InvalidNetworkIndex;
    uint32_t HoveredOpp2D = GeometrySpace::InvalidNetworkIndex;
    uint32_t HoveredOpp3D = GeometrySpace::InvalidNetworkIndex;
    uint32_t HoveredVert = GeometrySpace::InvalidNetworkIndex;
    if (Hit.HitSurfaceFace())
    {
        if (HoveredEdge < Session->Asset.Network.Edges.size())
        {
            HoveredOpp3D = Session->Asset.Network.Edges[HoveredEdge].OppositeEdgeIndex;
            HoveredOpp2D = InsidePlanar
                ? QueryOppositeIfCoincident2D(Session->Asset.Network, HoveredEdge)
                : HoveredOpp3D;
        }
        const ScreenProjectionParameters ProjParams = BuildScreenProjectionParameters(
            Proj, Session->RenderOptions3D.Camera, ActiveBounds, InsidePlanar);
        HoveredVert = FindNearestFaceVertexInScreenPixels(
            ProjParams, Session->Asset.Network, Hit.FaceIndex, { Input.PrimaryPointer.X, Input.PrimaryPointer.Y });
    }

    if (Hit.FaceIndex != Session->LastHoverHit.FaceIndex ||
        Hit.DirectedEdgeIndex != Session->LastHoverHit.DirectedEdgeIndex ||
        Hit.GizmoHandleOrdinal != Session->LastHoverHit.GizmoHandleOrdinal ||
        HoveredVert != Session->RenderOptions3D.HoveredVertexIndex ||
        HoveredOpp2D != Session->RenderOptions2D.HoveredOppositeEdgeIdx)
    {
        Session->LastHoverHit                           = Hit;
        Session->RenderOptions2D.HoveredFaceIndex       = HoveredFace;
        Session->RenderOptions3D.HoveredFaceIndex       = HoveredFace;
        Session->RenderOptions2D.HoveredEdgeIndex       = HoveredEdge;
        Session->RenderOptions3D.HoveredEdgeIndex       = HoveredEdge;
        Session->RenderOptions2D.HoveredOppositeEdgeIdx = HoveredOpp2D;
        Session->RenderOptions3D.HoveredOppositeEdgeIdx = HoveredOpp3D;
        Session->RenderOptions2D.HoveredVertexIndex     = HoveredVert;
        Session->RenderOptions3D.HoveredVertexIndex     = HoveredVert;
        Session->FramebuffersDirty                      = true;
    }

    // Seam Brush Stroke (supports drag-painting along edges!)
    if (Session->SeamBrushActive && Input.PrimaryPointer.Down && Hit.DirectedEdgeIndex != GeometrySpace::InvalidNetworkIndex)
    {
        const uint32_t Modified = UVAuthoringOperations::ApplySeamBrushStroke(
            Session->Asset.Network,
            Hit.DirectedEdgeIndex,
            Input.ControlHeld,
            Input.AltHeld);
        if (Modified > 0u)
        {
            Session->WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled = true;
            (void)Session->ApplyLiveUnwrapIfEnabled();
            Session->FinalizeInteractiveUVRevision("SeamBrushStroke");
        }
        return;
    }

    if (!Input.PrimaryPointer.Pressed)
    {
        return;
    }

    // Begin Gizmo Drag if cursor hit a Gizmo handle!
    if (Hit.HitGizmo())
    {
        if (InsidePlanar)
        {
            DragTargetFaces   = Session->SelectionMask.CollectSelectedFaces();
            DragTargetCorners = UVAuthoringOperations::CollectTransformableCornerEdges(
                Session->Asset.Network, Session->SelectionMask);
            if (!DragTargetCorners.empty())
            {
                DragBaselineUVStream = Session->Asset.Network.ExtractParameterizedUVStream();
                const auto Pivot3D   = Session->Gizmo2D.QueryPivotOrigin();
                DragPivotUV          = Mathematics::UVAxes{ Pivot3D.X, Pivot3D.Y };
                ActiveGizmoTransform = Mathematics::SpatialTransform{ Pivot3D, {}, { 1.0f, 1.0f, 1.0f } };

                const auto Ray = Proj.BuildPlanarRay(
                    static_cast<uint32_t>(std::max(16.0f, Interaction.PlanarBounds.Width)),
                    static_cast<uint32_t>(std::max(16.0f, Interaction.PlanarBounds.Height)),
                    Input.PrimaryPointer.X - Interaction.PlanarBounds.X,
                    Input.PrimaryPointer.Y - Interaction.PlanarBounds.Y);
                DraggingGizmo2D = Session->Gizmo2D.BeginDragInteraction(
                    static_cast<Editor::GizmoHandleOrdinal>(Hit.GizmoHandleOrdinal),
                    Ray,
                    ActiveGizmoTransform);
            }
        }
        else if (InsideSpatial)
        {
            Mathematics::Axis3D CamR {}, CamU {}, CamF {};
            Proj.EvaluateCameraBasis(CamR, CamU, CamF);
            const auto Pivot3D   = Session->Gizmo3D.QueryPivotOrigin();
            DragPivot3D          = Pivot3D;
            ActiveGizmoTransform = Mathematics::SpatialTransform{ Pivot3D, {}, { 1.0f, 1.0f, 1.0f } };
            LastAppliedObjectTransform = ActiveGizmoTransform;
            DragBaselineObjectVertices.clear();
            DragBaselineObjectVertices.reserve(Session->Asset.Network.Vertices.size());
            for (const auto& Vertex : Session->Asset.Network.Vertices)
            {
                DragBaselineObjectVertices.push_back(Vertex.Axes);
            }

            const auto Ray = Proj.BuildSpatialRay(
                static_cast<uint32_t>(std::max(16.0f, Interaction.SpatialBounds.Width)),
                static_cast<uint32_t>(std::max(16.0f, Interaction.SpatialBounds.Height)),
                Input.PrimaryPointer.X - Interaction.SpatialBounds.X,
                Input.PrimaryPointer.Y - Interaction.SpatialBounds.Y);
            DraggingGizmo3D = Session->Gizmo3D.BeginDragInteraction(
                static_cast<Editor::GizmoHandleOrdinal>(Hit.GizmoHandleOrdinal),
                Ray,
                ActiveGizmoTransform,
                CamR,
                CamU);
            if (!DraggingGizmo3D)
            {
                DragBaselineObjectVertices.clear();
            }
        }
        return;
    }

    // Small silhouette-margin fallback on Pressed so clicking the outer edge of a 5px vertex dot selects cleanly
    if (!Hit.HitSurfaceFace() &&
        (Session->SelectionMode == UVSelectionMode::Vertices || Session->SelectionMode == UVSelectionMode::Edges))
    {
        const auto& Net = Session->Asset.Network;
        const ScreenProjectionParameters ProjParams = BuildScreenProjectionParameters(
            Proj, Session->RenderOptions3D.Camera, ActiveBounds, InsidePlanar);
        const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
        uint32_t BestFace = GeometrySpace::InvalidNetworkIndex;
        uint32_t BestEdge = GeometrySpace::InvalidNetworkIndex;
        uint32_t BestVert = GeometrySpace::InvalidNetworkIndex;
        float    BestD2   = 49.0f; // 7px outer tolerance for silhouette corner dots/edges
        for (uint32_t FIdx = 0u; FIdx < static_cast<uint32_t>(Net.Faces.size()); ++FIdx)
        {
            const auto& F = Net.Faces[FIdx];
            uint32_t    E = F.BoundaryLoopEdgeIndex;
            for (uint16_t C = 0u; C < F.CornerCount && E < Net.Edges.size(); ++C)
            {
                const uint32_t ENext = Net.Edges[E].NextEdgeIndex;
                Point2D P0 {};
                if (Session->SelectionMode == UVSelectionMode::Vertices)
                {
                    if (ProjectEdgeCornerToLogicalScreen(ProjParams, Net, E, P0))
                    {
                        const float D2 = (P0.X - Cursor.X) * (P0.X - Cursor.X) + (P0.Y - Cursor.Y) * (P0.Y - Cursor.Y);
                        if (D2 < BestD2)
                        {
                            BestD2   = D2;
                            BestFace = FIdx;
                            BestEdge = E;
                            BestVert = Net.Edges[E].OriginVertexIndex;
                        }
                    }
                }
                else
                {
                    Point2D P1 {};
                    if (ProjectEdgeCornerToLogicalScreen(ProjParams, Net, E, P0) &&
                        ProjectEdgeCornerToLogicalScreen(ProjParams, Net, ENext, P1))
                    {
                        const float D2 = PointToSegmentDistSq(Cursor, P0, P1);
                        if (D2 < BestD2)
                        {
                            BestD2   = D2;
                            BestFace = FIdx;
                            BestEdge = E;
                        }
                    }
                }
                E = ENext;
            }
        }
        if (BestFace != GeometrySpace::InvalidNetworkIndex)
        {
            Hit.FaceIndex         = BestFace;
            Hit.DirectedEdgeIndex = BestEdge;
            HoveredVert           = BestVert;
        }
    }

    ApplySurfaceHitSelection(Hit, Input, InsidePlanar, HoveredVert);
}

void UVViewportInteractor::ApplySurfaceHitSelection(
    const GeometricRaster::ViewportPickHit& Hit,
    const PaneInputReading&                 Input,
    bool                                    PlanarMetric,
    uint32_t                                ClickedVertexIndex)
{
    auto& Mask = Session->SelectionMask;
    auto& Net  = Session->Asset.Network;
    Mask.SynchronizeCapacity(Net);

    if (!Hit.HitSurfaceFace())
    {
        if (!Input.ShiftHeld)
        {
            Session->ClearViewportSelection();
        }
        return;
    }

    Session->SpatialSelectionActive = true;
    if (Session->SelectionMode != UVSelectionMode::Layers)
    {
        Session->RenderOptions3D.ActiveLayerOrdinal = UINT16_MAX;
        Session->RenderOptions2D.ActiveLayerOrdinal = UINT16_MAX;
    }
    else
    {
        Session->RenderOptions3D.ActiveLayerOrdinal = Session->SelectedLayerOrdinal;
        Session->RenderOptions2D.ActiveLayerOrdinal = Session->SelectedLayerOrdinal;
    }

    // Alt + Click = Edge Loop (or Vertex Loop in Vertices mode); Ctrl + Alt + Click = Edge Ring
    if (Input.AltHeld && Hit.DirectedEdgeIndex != GeometrySpace::InvalidNetworkIndex)
    {
        const auto LoopOrRing = Input.ControlHeld
            ? GeometrySpace::GeometrySelection::EvaluateEdgeRing(Net, Hit.DirectedEdgeIndex)
            : GeometrySpace::GeometrySelection::EvaluateEdgeLoop(Net, Hit.DirectedEdgeIndex);
        if (Session->SelectionMode == UVSelectionMode::Vertices)
        {
            std::vector<uint32_t> LoopVerts {};
            LoopVerts.reserve(LoopOrRing.size() * 2u);
            for (const uint32_t EIdx : LoopOrRing)
            {
                if (EIdx < Net.Edges.size())
                {
                    LoopVerts.push_back(Net.Edges[EIdx].OriginVertexIndex);
                    if (Net.Edges[EIdx].NextEdgeIndex < Net.Edges.size())
                    {
                        LoopVerts.push_back(Net.Edges[Net.Edges[EIdx].NextEdgeIndex].OriginVertexIndex);
                    }
                }
            }
            if (!Input.ShiftHeld)
            {
                Mask.ClearAll();
            }
            Mask.SelectVerticesFromList(LoopVerts, true);
        }
        else
        {
            if (!Input.ShiftHeld)
            {
                Mask.ClearAll();
            }
            Session->SelectionMode = UVSelectionMode::Edges;
            Mask.SelectEdgesFromList(Net, LoopOrRing, true);
        }
        Session->RefreshGizmoAnchorsFromSelection();
        Session->SynchronizeSelectionToGPU();
        return;
    }

    // Ctrl + Click = Shortest Path (Vertex path in Vertices mode, Edge path otherwise)
    if (Input.ControlHeld)
    {
        if (Session->SelectionMode == UVSelectionMode::Vertices &&
            Mask.ActiveVertexIndex != GeometrySpace::InvalidNetworkIndex &&
            ClickedVertexIndex != GeometrySpace::InvalidNetworkIndex)
        {
            const auto PathVerts = GeometrySpace::GeometrySelection::EvaluateShortestVertexPath(
                Net, Mask.ActiveVertexIndex, ClickedVertexIndex, PlanarMetric);
            Mask.SelectVerticesFromList(PathVerts, true);
            Session->RefreshGizmoAnchorsFromSelection();
            Session->SynchronizeSelectionToGPU();
            return;
        }
        if (Session->SelectionMode == UVSelectionMode::Edges &&
            Mask.ActiveEdgeIndex != GeometrySpace::InvalidNetworkIndex &&
            Hit.DirectedEdgeIndex != GeometrySpace::InvalidNetworkIndex)
        {
            const auto Path = GeometrySpace::GeometrySelection::EvaluateShortestEdgePath(
                Net, Mask.ActiveEdgeIndex, Hit.DirectedEdgeIndex, PlanarMetric);
            Session->SelectionMode = UVSelectionMode::Edges;
            Mask.SelectEdgesFromList(Net, Path, true);
            Session->RefreshGizmoAnchorsFromSelection();
            Session->SynchronizeSelectionToGPU();
            return;
        }
    }

    switch (Session->SelectionMode)
    {
        case UVSelectionMode::Vertices:
        {
            if (!Input.ShiftHeld)
            {
                Mask.ClearAll();
            }
            if (ClickedVertexIndex != GeometrySpace::InvalidNetworkIndex)
            {
                if (Input.ShiftHeld)
                {
                    Mask.ToggleVertexSelected(ClickedVertexIndex);
                }
                else
                {
                    Mask.AssignVertexSelected(ClickedVertexIndex, true);
                }
            }
            Mask.ActiveFaceIndex = Hit.FaceIndex;
            Session->RefreshGizmoAnchorsFromSelection();
            Session->SynchronizeSelectionToGPU();
            break;
        }
        case UVSelectionMode::Edges:
        {
            if (!Input.ShiftHeld)
            {
                Mask.ClearAll();
            }
            if (Hit.DirectedEdgeIndex != GeometrySpace::InvalidNetworkIndex)
            {
                const bool IncludeOpp = !PlanarMetric ||
                    (QueryOppositeIfCoincident2D(Net, Hit.DirectedEdgeIndex) != GeometrySpace::InvalidNetworkIndex);
                if (Input.ShiftHeld)
                {
                    Mask.ToggleEdgeSelected(Net, Hit.DirectedEdgeIndex, IncludeOpp);
                }
                else
                {
                    Mask.AssignEdgeSelected(Net, Hit.DirectedEdgeIndex, true, IncludeOpp);
                }
            }
            Mask.ActiveFaceIndex = Hit.FaceIndex;
            Session->RefreshGizmoAnchorsFromSelection();
            Session->SynchronizeSelectionToGPU();
            break;
        }
        case UVSelectionMode::Layers:
        {
            const uint16_t Ord = Net.Faces[Hit.FaceIndex].PriorityLayerOrdinal;
            Session->SelectPriorityLayer(Ord);
            break;
        }
        case UVSelectionMode::Islands:
        {
            if (!Input.ShiftHeld)
            {
                Mask.ClearAll();
            }
            const auto Island = GeometrySpace::GeometrySelection::EvaluateLinkedIslandFaces(Net, Hit.FaceIndex);
            Mask.SelectFacesFromList(Island, Input.ShiftHeld);
            Session->RefreshGizmoAnchorsFromSelection();
            Session->SynchronizeSelectionToGPU();
            break;
        }
        case UVSelectionMode::Faces:
        default:
        {
            if (!Input.ShiftHeld)
            {
                Mask.ClearAll();
                Mask.AssignFaceSelected(Hit.FaceIndex, true);
            }
            else
            {
                Mask.ToggleFaceSelected(Hit.FaceIndex);
            }
            Session->RefreshGizmoAnchorsFromSelection();
            Session->SynchronizeSelectionToGPU();
            break;
        }
    }
}

} // namespace Frontier::Workspaces
