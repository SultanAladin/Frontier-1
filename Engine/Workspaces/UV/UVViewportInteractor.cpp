/*============================================================================================================================================
                                                         UVVIEWPORTINTERACTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVViewportInteractor driving 2D/3D camera navigation, synchronized GPU picking, TransformGizmo drag & modal G/R/S

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVViewportInteractor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Frontier::Workspaces
{

void UVViewportInteractor::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session            = TargetSession;
    DraggingGizmo2D    = false;
    DraggingGizmo3D    = false;
    ModalMode          = ModalUVTransformMode::Inactive;
    ActiveRegionTool   = RegionSelectTool::Inactive;
    RegionStrokeActive = false;
}

void UVViewportInteractor::ProcessViewportInput(const ViewportInteractionReading& Interaction)
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

    auto& Proj = Session->AccessViewportProjection();

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Camera Orbit, Pan & Cursor-Centered Zoom (MMB / Alt+LMB / Mouse Wheel)
    //--------------------------------------------------------------------------------------------------------------------
    if (InsideSpatial)
    {
        if (std::abs(Input.WheelDeltaY) > 1.0e-4f)
        {
            if (ActiveRegionTool == RegionSelectTool::CircleBrush)
            {
                BrushRadiusPixels = std::clamp(BrushRadiusPixels + Input.WheelDeltaY * 3.0f, 4.0f, 128.0f);
            }
            else
            {
                Proj.DollySpatialCamera(1.0f - Input.WheelDeltaY * 0.12f);
                Session->FramebuffersDirty = true;
            }
        }
        if (Input.MiddleDown || (Input.AltHeld && Input.PrimaryPointer.Down && !Input.PrimaryPointer.Pressed))
        {
            if (Input.ShiftHeld)
            {
                const float DistScale = Proj.InspectSpatialCamera().DistanceMeters * 0.0018f;
                Proj.PanSpatialCamera(Input.PointerDeltaX * DistScale, Input.PointerDeltaY * DistScale);
            }
            else
            {
                Proj.OrbitSpatialCamera(-Input.PointerDeltaX * 0.0065f, -Input.PointerDeltaY * 0.0065f);
            }
            Session->FramebuffersDirty = true;
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
        if (Input.MiddleDown || (Input.AltHeld && Input.PrimaryPointer.Down && !Input.PrimaryPointer.Pressed && Input.ShiftHeld))
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
            return;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Modal G / R / S Transform Mode Sequence
    //--------------------------------------------------------------------------------------------------------------------
    if (ModalMode != ModalUVTransformMode::Inactive)
    {
        HandleModalTransformStep(Interaction, InsidePlanar);
        return;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Keyboard Shortcuts (1-4 Modes, F Fit View, K Seam Brush, P Pin, V Stitch, Y Split, B/C Region, G/R/S Modal)
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
        Session->SeamBrushActive  = false;
        Session->StatusBannerText = "Cleared Active Viewport Tool";
        return;
    }

    if (Input.ControlHeld && !Input.AltHeld)
    {
        for (const char Ch : Input.TypedUtf8)
        {
            if (Ch == 'z' || Ch == 'Z')
            {
                if (Input.ShiftHeld)
                {
                    (void)Session->RedoParameterization();
                }
                else
                {
                    (void)Session->UndoParameterization();
                }
                return;
            }
        }
    }

    for (const char RawCh : Input.TypedUtf8)
    {
        const char Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(RawCh)));
        switch (Ch)
        {
            case '1':
                Session->SelectionMode = UVSelectionMode::Faces;
                Session->SynchronizeSelectionToGPU();
                Session->StatusBannerText = "Selection Mode: Faces [1]";
                break;
            case '2':
                Session->SelectionMode = UVSelectionMode::Edges;
                Session->SynchronizeSelectionToGPU();
                Session->StatusBannerText = "Selection Mode: Edges [2]";
                break;
            case '3':
                Session->SelectionMode = UVSelectionMode::Islands;
                Session->SynchronizeSelectionToGPU();
                Session->StatusBannerText = "Selection Mode: UV Islands [3]";
                break;
            case '4':
                Session->SelectionMode = UVSelectionMode::Layers;
                Session->SynchronizeSelectionToGPU();
                Session->StatusBannerText = "Selection Mode: Priority Layers [4]";
                break;
            case 'f':
                Session->FitSelectionOrNetwork();
                Session->StatusBannerText = "Framed Active Selection [F]";
                break;
            case 'k':
                Session->SeamBrushActive  = !Session->SeamBrushActive;
                ActiveRegionTool          = RegionSelectTool::Inactive;
                Session->StatusBannerText = Session->SeamBrushActive
                    ? "Seam Brush Active [K]  |  Click Edges to Mark, Alt+Click to Clear, Ctrl+Click Loop"
                    : "Seam Brush Disabled";
                break;
            case 'b':
                ActiveRegionTool = (ActiveRegionTool == RegionSelectTool::BoxMarquee)
                    ? RegionSelectTool::Inactive
                    : RegionSelectTool::BoxMarquee;
                Session->SeamBrushActive  = false;
                Session->StatusBannerText = (ActiveRegionTool == RegionSelectTool::BoxMarquee)
                    ? "GPU Box Marquee Active [B]  |  Drag to Select"
                    : "Box Marquee Disabled";
                break;
            case 'c':
                ActiveRegionTool = (ActiveRegionTool == RegionSelectTool::CircleBrush)
                    ? RegionSelectTool::Inactive
                    : RegionSelectTool::CircleBrush;
                Session->SeamBrushActive  = false;
                Session->StatusBannerText = (ActiveRegionTool == RegionSelectTool::CircleBrush)
                    ? "GPU Circle Brush Active [C]  |  Wheel Adjusts Radius"
                    : "Circle Brush Disabled";
                break;
            case 'l':
                if (Session->LastHoverHit.HitSurfaceFace())
                {
                    const auto Linked = GeometrySpace::GeometrySelection::EvaluateLinkedIslandFaces(
                        Session->Asset.Network, Session->LastHoverHit.FaceIndex);
                    Session->SelectionMode = UVSelectionMode::Islands;
                    Session->SelectionMask.SelectFacesFromList(Linked, Input.ShiftHeld);
                    Session->RefreshGizmoAnchorsFromSelection();
                    Session->SynchronizeSelectionToGPU();
                }
                break;
            case 'p':
                Session->QueueDirective(Input.AltHeld ? UVCommandDirective::UnpinSelectedUVs : UVCommandDirective::PinSelectedUVs);
                break;
            case 'u':
                Session->QueueDirective(UVCommandDirective::UnwrapConformal);
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
                    DragTargetFaces = Session->SelectionMask.CollectSelectedFaces();
                    if (!DragTargetFaces.empty())
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
            const bool NegativeSignTyped = (Ch == '-');
            const bool DecimalPointTyped = (Ch == '.');
            if ((!NegativeSignTyped || ModalNumericEntry.empty()) &&
                (!DecimalPointTyped || ModalNumericEntry.find('.') == std::string::npos))
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
    const float RawParsedVal        = ModalNumericEntry.empty() ? 0.0f : std::strtof(ModalNumericEntry.c_str(), nullptr);
    const bool  NumericEntryPresent = !ModalNumericEntry.empty() && std::isfinite(RawParsedVal);
    const float ParsedVal           = NumericEntryPresent ? std::clamp(RawParsedVal, -10000.0f, 10000.0f) : 0.0f;

    Mathematics::UVAxes   DeltaUV { 0.0f, 0.0f };
    Mathematics::Scalar2D ScaleUV { 1.0f, 1.0f };
    float                 RotRad = 0.0f;

    if (ModalMode == ModalUVTransformMode::GrabTranslate)
    {
        float Du = NumericEntryPresent ? ParsedVal : (DxPx / std::max(32.0f, Interaction.PlanarBounds.Width));
        float Dv = NumericEntryPresent ? ParsedVal : (-DyPx / std::max(32.0f, Interaction.PlanarBounds.Height));
        if (Input.ControlHeld && !NumericEntryPresent)
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
        float Deg = std::clamp(NumericEntryPresent ? ParsedVal : (DxPx * 0.45f), -36000.0f, 36000.0f);
        if (Input.ControlHeld && !NumericEntryPresent)
        {
            Deg = std::round(Deg / 15.0f) * 15.0f;
        }
        RotRad = Deg * 0.0174532925f;
    }
    else if (ModalMode == ModalUVTransformMode::ScaleUniform)
    {
        float Factor = NumericEntryPresent ? std::clamp(ParsedVal, 0.01f, 100.0f) : std::clamp(1.0f + DxPx * 0.005f, 0.05f, 10.0f);
        if (Input.ControlHeld && !NumericEntryPresent)
        {
            Factor = std::round(Factor * 10.0f) / 10.0f;
        }
        ScaleUV = {
            (ModalConstraint == ModalAxisConstraint::LockV) ? 1.0f : Factor,
            (ModalConstraint == ModalAxisConstraint::LockU) ? 1.0f : Factor,
        };
    }

    UVAuthoringOperations::TransformSelectedFaceUVs(
        Session->Asset.Network, DragTargetFaces, DragPivotUV, DeltaUV, RotRad, ScaleUV);
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

        if (Res.TransformChanged && !DragTargetFaces.empty())
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
            UVAuthoringOperations::TransformSelectedFaceUVs(
                Session->Asset.Network,
                DragTargetFaces,
                DragPivotUV,
                DeltaUV,
                ActiveGizmoTransform.Rotation.Z,
                Scale2D);

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
        if (Res.TransformChanged)
        {
            Session->StatusBannerText  = Res.HudReadout;
            Session->FramebuffersDirty = true;
        }
        if (Input.PrimaryPointer.Released || !Input.PrimaryPointer.Down)
        {
            Session->Gizmo3D.EndDragInteraction();
            DraggingGizmo3D = false;
        }
        return;
    }

    if (!InsidePlanar && !InsideSpatial)
    {
        return;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // Region Marquee [B] & Circle Brush [C]
    //--------------------------------------------------------------------------------------------------------------------
    if (ActiveRegionTool != RegionSelectTool::Inactive)
    {
        const auto& Bounds = InsidePlanar ? Interaction.PlanarBounds : Interaction.SpatialBounds;
        const float LocalX = std::clamp(Input.PrimaryPointer.X - Bounds.X, 0.0f, Bounds.Width - 1.0f);
        const float LocalY = std::clamp(Input.PrimaryPointer.Y - Bounds.Y, 0.0f, Bounds.Height - 1.0f);

        if (ActiveRegionTool == RegionSelectTool::CircleBrush && Input.PrimaryPointer.Down)
        {
            const uint32_t MinX = static_cast<uint32_t>(std::max(0.0f, LocalX - BrushRadiusPixels));
            const uint32_t MinY = static_cast<uint32_t>(std::max(0.0f, LocalY - BrushRadiusPixels));
            const uint32_t MaxX = static_cast<uint32_t>(LocalX + BrushRadiusPixels);
            const uint32_t MaxY = static_cast<uint32_t>(LocalY + BrushRadiusPixels);
            const auto Summary  = InsidePlanar
                ? Rast->Reduce2DRegionPick(MinX, MinY, MaxX, MaxY, static_cast<uint32_t>(BrushRadiusPixels), true)
                : Rast->Reduce3DRegionPick(MinX, MinY, MaxX, MaxY, static_cast<uint32_t>(BrushRadiusPixels), true);
            if (Summary.HitPixelCount > 0u)
            {
                Session->SelectionMode     = UVSelectionMode::Faces;
                Session->FramebuffersDirty = true;
            }
            return;
        }

        if (ActiveRegionTool == RegionSelectTool::BoxMarquee)
        {
            if (Input.PrimaryPointer.Pressed)
            {
                RegionStrokeActive  = true;
                MarqueeAnchorLocalX = LocalX;
                MarqueeAnchorLocalY = LocalY;
            }
            else if (RegionStrokeActive && Input.PrimaryPointer.Released)
            {
                RegionStrokeActive  = false;
                const uint32_t MinX = static_cast<uint32_t>(std::min(MarqueeAnchorLocalX, LocalX));
                const uint32_t MinY = static_cast<uint32_t>(std::min(MarqueeAnchorLocalY, LocalY));
                const uint32_t MaxX = static_cast<uint32_t>(std::max(MarqueeAnchorLocalX, LocalX));
                const uint32_t MaxY = static_cast<uint32_t>(std::max(MarqueeAnchorLocalY, LocalY));
                (void)(InsidePlanar
                    ? Rast->Reduce2DRegionPick(MinX, MinY, MaxX, MaxY, 0u, Input.ShiftHeld)
                    : Rast->Reduce3DRegionPick(MinX, MinY, MaxX, MaxY, 0u, Input.ShiftHeld));
                Session->SelectionMode     = UVSelectionMode::Faces;
                Session->FramebuffersDirty = true;
            }
            return;
        }
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

    if (Hit.FaceIndex != Session->LastHoverHit.FaceIndex ||
        Hit.GizmoHandleOrdinal != Session->LastHoverHit.GizmoHandleOrdinal)
    {
        Session->LastHoverHit                   = Hit;
        Session->RenderOptions2D.HoveredFaceIndex = Hit.HitSurfaceFace() ? Hit.FaceIndex : GeometrySpace::InvalidNetworkIndex;
        Session->RenderOptions3D.HoveredFaceIndex = Hit.HitSurfaceFace() ? Hit.FaceIndex : GeometrySpace::InvalidNetworkIndex;
        Session->FramebuffersDirty              = true;
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
            DragTargetFaces = Session->SelectionMask.CollectSelectedFaces();
            if (!DragTargetFaces.empty())
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
            ActiveGizmoTransform = Mathematics::SpatialTransform{ Pivot3D, {}, { 1.0f, 1.0f, 1.0f } };

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
        }
        return;
    }

    ApplySurfaceHitSelection(Hit, Input, InsidePlanar);
}

void UVViewportInteractor::ApplySurfaceHitSelection(
    const GeometricRaster::ViewportPickHit& Hit,
    const PaneInputReading&                 Input,
    bool                                    PlanarMetric)
{
    auto& Mask = Session->SelectionMask;
    auto& Net  = Session->Asset.Network;
    Mask.SynchronizeCapacity(Net);

    if (!Hit.HitSurfaceFace())
    {
        if (!Input.ShiftHeld)
        {
            Mask.ClearAll();
            Session->SynchronizeSelectionToGPU();
        }
        return;
    }

    // Alt + Click = Edge Loop; Ctrl + Alt + Click = Edge Ring
    if (Input.AltHeld && Hit.DirectedEdgeIndex != GeometrySpace::InvalidNetworkIndex)
    {
        const auto LoopOrRing = Input.ControlHeld
            ? GeometrySpace::GeometrySelection::EvaluateEdgeRing(Net, Hit.DirectedEdgeIndex)
            : GeometrySpace::GeometrySelection::EvaluateEdgeLoop(Net, Hit.DirectedEdgeIndex);
        Session->SelectionMode = UVSelectionMode::Edges;
        Mask.SelectEdgesFromList(Net, LoopOrRing, Input.ShiftHeld);
        for (const uint32_t EIdx : LoopOrRing)
        {
            if (EIdx < Net.Edges.size() && Net.Edges[EIdx].EnclosingFaceIndex != GeometrySpace::InvalidNetworkIndex)
            {
                Mask.AssignFaceSelected(Net.Edges[EIdx].EnclosingFaceIndex, true);
            }
        }
        Session->RefreshGizmoAnchorsFromSelection();
        Session->SynchronizeSelectionToGPU();
        return;
    }

    // Ctrl + Click in Edge mode = Dijkstra Shortest Edge Path
    if (Input.ControlHeld &&
        Mask.ActiveEdgeIndex != GeometrySpace::InvalidNetworkIndex &&
        Hit.DirectedEdgeIndex != GeometrySpace::InvalidNetworkIndex)
    {
        const auto Path = GeometrySpace::GeometrySelection::EvaluateShortestEdgePath(
            Net, Mask.ActiveEdgeIndex, Hit.DirectedEdgeIndex, PlanarMetric);
        Session->SelectionMode = UVSelectionMode::Edges;
        Mask.SelectEdgesFromList(Net, Path, true);
        for (const uint32_t EIdx : Path)
        {
            if (EIdx < Net.Edges.size() && Net.Edges[EIdx].EnclosingFaceIndex != GeometrySpace::InvalidNetworkIndex)
            {
                Mask.AssignFaceSelected(Net.Edges[EIdx].EnclosingFaceIndex, true);
            }
        }
        Session->RefreshGizmoAnchorsFromSelection();
        Session->SynchronizeSelectionToGPU();
        return;
    }

    switch (Session->SelectionMode)
    {
        case UVSelectionMode::Layers:
        {
            const uint16_t Ord = Net.Faces[Hit.FaceIndex].PriorityLayerOrdinal;
            Session->SelectPriorityLayer(Ord);
            break;
        }
        case UVSelectionMode::Islands:
        {
            const auto Island = GeometrySpace::GeometrySelection::EvaluateLinkedIslandFaces(Net, Hit.FaceIndex);
            Mask.SelectFacesFromList(Island, Input.ShiftHeld);
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
                Mask.AssignEdgeSelected(Net, Hit.DirectedEdgeIndex, true);
            }
            Mask.AssignFaceSelected(Hit.FaceIndex, true);
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
