/*============================================================================================================================================
                                                         VECTORVIEWPORTINTERACTOR.CPP
============================================================================================================================================*/
// 🧩 Interactive 2D vector viewport interactor driving GPU picking, Planar2D TransformGizmo, Pen/Curve/EditPoints/PointGradient,
//    cursor-anchored zoom/pan, modal Grab/Rotate/Scale, and Pattern/Stamp keyboard shortcuts.

#include "Engine/Workspaces/Texturing/VectorViewportInteractor.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

constexpr float CanonicalDocExtent = 512.0f;

[[nodiscard]] bool QueryToolCreatesMotifShape(PatternStampToolCategory Tool, PatternStampMotifShape& OutShape) noexcept
{
    switch (Tool)
    {
        case PatternStampToolCategory::Square:   OutShape = PatternStampMotifShape::Square;    return true;
        case PatternStampToolCategory::Circle:   OutShape = PatternStampMotifShape::Circle;    return true;
        case PatternStampToolCategory::Diamond:  OutShape = PatternStampMotifShape::Diamond;   return true;
        case PatternStampToolCategory::Triangle: OutShape = PatternStampMotifShape::Triangle;  return true;
        case PatternStampToolCategory::Star:     OutShape = PatternStampMotifShape::Star;      return true;
        case PatternStampToolCategory::Hexagon:  OutShape = PatternStampMotifShape::Hexagon;   return true;
        case PatternStampToolCategory::Stripe:   OutShape = PatternStampMotifShape::StripeBar; return true;
        default: return false;
    }
}

[[nodiscard]] const char* QueryShapeDefaultTitle(PatternStampMotifShape Shape) noexcept
{
    switch (Shape)
    {
        case PatternStampMotifShape::Square:    return "Square Motif";
        case PatternStampMotifShape::Circle:    return "Circle Motif";
        case PatternStampMotifShape::Diamond:   return "Diamond Motif";
        case PatternStampMotifShape::Triangle:  return "Triangle Motif";
        case PatternStampMotifShape::Star:      return "Star Motif";
        case PatternStampMotifShape::Hexagon:   return "Hexagon Motif";
        case PatternStampMotifShape::StripeBar: return "Stripe Bar";
        default:                                return "Vector Path";
    }
}

[[nodiscard]] GeometricRaster::VectorCubicKnot* ResolveMutableKnotByGlobalIndex(
    PatternStampLayerSpec& Layer,
    int32_t                TargetIdx) noexcept
{
    if (TargetIdx < 0)
    {
        return nullptr;
    }
    int32_t Counter = 0;
    for (auto& Sub : Layer.CubicSubpaths)
    {
        for (auto& Knot : Sub.Knots)
        {
            if (Counter == TargetIdx)
            {
                return &Knot;
            }
            ++Counter;
        }
    }
    return nullptr;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

VectorViewportInteractor::VectorViewportInteractor()
{
    Gizmo2D.ConfigureDimensionMode(Editor::GizmoDimensionMode::Planar2D);
    Gizmo2D.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);
}

void VectorViewportInteractor::AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
    ResetActiveTools();
    SynchronizeGizmoAnchor();
}

void VectorViewportInteractor::AttachRasterizer(GeometricRaster::VectorCanvasRasterizer* TargetRasterizer) noexcept
{
    Rasterizer = TargetRasterizer;
}

void VectorViewportInteractor::AssignSvgPasteRequestCallback(std::function<bool(std::string_view, float, float)> Callback)
{
    SvgPasteCallback = std::move(Callback);
}

void VectorViewportInteractor::ResetActiveTools() noexcept
{
    DraggingGizmo2D     = false;
    DraggingDirectLayer = false;
    DraggingKnotControl  = false;
    DraggingPointPin    = false;
    CreatingShape       = false;
    PanningCanvas       = false;
    PenAuthoringActive  = false;
    PenDraggingHandles  = false;
    CreatingLayerId     = 0u;
    PenTargetLayerId    = 0u;
    ActiveKnotGlobalIdx = -1;
    ActivePinSlot       = -1;
    ModalMode           = ModalVectorTransformMode::Inactive;
    ModalConstraint     = VectorAxisConstraint::Unconstrained;
    ModalNumericEntry.clear();
    Gizmo2D.EndDragInteraction();
    if (Session != nullptr)
    {
        Session->MarqueeActive = false;
    }
}

void VectorViewportInteractor::SynchronizeGizmoAnchor() noexcept
{
    Gizmo2D.ConfigureDimensionMode(Editor::GizmoDimensionMode::Planar2D);
    if (Session == nullptr)
    {
        return;
    }
    const PatternStampLayerSpec* Sel = Session->InspectSelectedLayer();
    if (Sel == nullptr || Sel->Category == PatternStampLayerCategory::Folder)
    {
        return;
    }
    const auto Metrics      = GeometricRaster::VectorCanvasRasterizer::ComputeTileLayoutMetrics(LastViewportW, LastViewportH, *Session);
    const float WorldRadius = 72.0f * (CanonicalDocExtent / std::max(32.0f, Metrics.TileSpanPx));
    Gizmo2D.ConfigureAnchor({ Sel->CenterX, -Sel->CenterY, 0.0f }, WorldRadius);
}

float VectorViewportInteractor::ApplyOptionalSnap(float CoordDocPx, bool SnapActive) const noexcept
{
    if (Session == nullptr || !SnapActive || Session->SnapStepPx <= 0)
    {
        return CoordDocPx;
    }
    const float Step = static_cast<float>(Session->SnapStepPx);
    return std::round(CoordDocPx / Step) * Step;
}

void VectorViewportInteractor::CaptureLayerBaseline(const PatternStampLayerSpec& Layer)
{
    Baseline.LayerIdentity = Layer.Identity;
    Baseline.CenterX       = Layer.CenterX;
    Baseline.CenterY       = Layer.CenterY;
    Baseline.WidthPx       = std::max(8.0f, Layer.WidthPx);
    Baseline.HeightPx      = std::max(8.0f, Layer.HeightPx);
    Baseline.RotationDeg   = Layer.RotationDeg;
    Baseline.FontSizePx    = std::max(8.0f, Layer.FontSizePx);
    Baseline.Subpaths      = Layer.CubicSubpaths;

    if (Baseline.Subpaths.empty() &&
        Layer.Category != PatternStampLayerCategory::Folder &&
        Layer.Category != PatternStampLayerCategory::GradientWash &&
        Layer.Category != PatternStampLayerCategory::Typographic)
    {
        PatternStampLayerSpec Temp = Layer;
        PatternStampWorkspaceSession::SeedMotifCubicSubpathsIfEmpty(Temp);
        Baseline.Subpaths = std::move(Temp.CubicSubpaths);
    }
}

void VectorViewportInteractor::ApplyBaselineTransform(
    PatternStampLayerSpec& Layer,
    float                  DeltaX,
    float                  DeltaY,
    float                  ScaleX,
    float                  ScaleY,
    float                  DeltaRotDeg) const
{
    const float SafeScaleX = std::max(0.08f, ScaleX);
    const float SafeScaleY = std::max(0.08f, ScaleY);

    Layer.CenterX     = Baseline.CenterX + DeltaX;
    Layer.CenterY     = Baseline.CenterY + DeltaY;
    Layer.WidthPx     = std::clamp(Baseline.WidthPx * SafeScaleX, 8.0f, 1024.0f);
    Layer.HeightPx    = std::clamp(Baseline.HeightPx * SafeScaleY, 8.0f, 1024.0f);
    Layer.RotationDeg = Baseline.RotationDeg + DeltaRotDeg;

    if (Layer.Category == PatternStampLayerCategory::Typographic)
    {
        const float UniformScale = 0.5f * (SafeScaleX + SafeScaleY);
        Layer.FontSizePx = std::clamp(Baseline.FontSizePx * UniformScale, 8.0f, 256.0f);
    }
    else if (!Baseline.Subpaths.empty() && (std::fabs(SafeScaleX - 1.0f) > 1.0e-4f || std::fabs(SafeScaleY - 1.0f) > 1.0e-4f))
    {
        Layer.CubicSubpaths = Baseline.Subpaths;
        for (auto& Sub : Layer.CubicSubpaths)
        {
            for (auto& Knot : Sub.Knots)
            {
                Knot.AnchorX    *= SafeScaleX;
                Knot.AnchorY    *= SafeScaleY;
                Knot.InHandleX  *= SafeScaleX;
                Knot.InHandleY  *= SafeScaleY;
                Knot.OutHandleX *= SafeScaleX;
                Knot.OutHandleY *= SafeScaleY;
            }
        }
    }
}

int32_t VectorViewportInteractor::FindNearestKnot(
    const PatternStampLayerSpec& Layer,
    float                        DocX,
    float                        DocY,
    float                        MaxDistancePx,
    KnotControlPart&            OutTarget) const noexcept
{
    int32_t BestIdx    = -1;
    float   BestDistSq = MaxDistancePx * MaxDistancePx;
    OutTarget          = KnotControlPart::AnchorPoint;

    int32_t GlobalIdx = 0;
    for (const auto& Sub : Layer.CubicSubpaths)
    {
        for (const auto& Knot : Sub.Knots)
        {
            const float Ax = Layer.CenterX + Knot.AnchorX;
            const float Ay = Layer.CenterY + Knot.AnchorY;
            const float DistAnchorSq = (DocX - Ax) * (DocX - Ax) + (DocY - Ay) * (DocY - Ay);
            if (DistAnchorSq <= BestDistSq)
            {
                BestDistSq = DistAnchorSq;
                BestIdx    = GlobalIdx;
                OutTarget  = KnotControlPart::AnchorPoint;
            }

            const float Ox = Layer.CenterX + Knot.OutHandleX;
            const float Oy = Layer.CenterY + Knot.OutHandleY;
            const float DistOutSq = (DocX - Ox) * (DocX - Ox) + (DocY - Oy) * (DocY - Oy);
            if (DistOutSq < BestDistSq)
            {
                BestDistSq = DistOutSq;
                BestIdx    = GlobalIdx;
                OutTarget  = KnotControlPart::OutTangent;
            }

            const float Ix = Layer.CenterX + Knot.InHandleX;
            const float Iy = Layer.CenterY + Knot.InHandleY;
            const float DistInSq = (DocX - Ix) * (DocX - Ix) + (DocY - Iy) * (DocY - Iy);
            if (DistInSq < BestDistSq)
            {
                BestDistSq = DistInSq;
                BestIdx    = GlobalIdx;
                OutTarget  = KnotControlPart::InTangent;
            }
            ++GlobalIdx;
        }
    }
    return BestIdx;
}

int32_t VectorViewportInteractor::FindNearestPointPin(
    const PatternStampLayerSpec& Layer,
    float                        DocX,
    float                        DocY,
    float                        MaxDistancePx) const noexcept
{
    const float NormX = std::clamp(DocX / CanonicalDocExtent, 0.0f, 1.0f);
    const float NormY = std::clamp(DocY / CanonicalDocExtent, 0.0f, 1.0f);
    const float MaxNormSq = (MaxDistancePx / CanonicalDocExtent) * (MaxDistancePx / CanonicalDocExtent);

    int32_t BestPin    = -1;
    float   BestDistSq = MaxNormSq;
    for (int32_t P = 0; P < 12; ++P)
    {
        const auto& Pin = Layer.PointFieldPins[static_cast<std::size_t>(P)];
        if (!Pin.Active)
        {
            continue;
        }
        const float Dx = NormX - Pin.NormX;
        const float Dy = NormY - Pin.NormY;
        const float D2 = Dx * Dx + Dy * Dy;
        if (D2 <= BestDistSq)
        {
            BestDistSq = D2;
            BestPin    = P;
        }
    }
    return BestPin;
}

std::string VectorViewportInteractor::QueryModalStatusSummary() const
{
    if (ModalMode == ModalVectorTransformMode::Inactive)
    {
        return {};
    }
    const char* ModeLabel =
        (ModalMode == ModalVectorTransformMode::GrabTranslate) ? "Grab"
        : ((ModalMode == ModalVectorTransformMode::RotateAngle) ? "Rotate" : "Scale");
    const char* AxisLabel =
        (ModalConstraint == VectorAxisConstraint::LockX) ? " [X]"
        : ((ModalConstraint == VectorAxisConstraint::LockY) ? " [Y]" : "");
    std::string Out = std::string(ModeLabel) + AxisLabel;
    if (!ModalNumericEntry.empty())
    {
        Out += " (" + ModalNumericEntry + ")";
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 PRIMARY TICK DISPATCH
//------------------------------------------------------------------------------------------------------------------------

void VectorViewportInteractor::ProcessViewportInput(const ViewportInteractionReading& Reading)
{
    if (Session == nullptr)
    {
        return;
    }

    const uint32_t ViewW = static_cast<uint32_t>(std::max(1.0f, std::round(Reading.PlanarBounds.Width)));
    const uint32_t ViewH = static_cast<uint32_t>(std::max(1.0f, std::round(Reading.PlanarBounds.Height)));
    LastViewportW = ViewW;
    LastViewportH = ViewH;

    const Point2D Cursor { Reading.Input.PrimaryPointer.X, Reading.Input.PrimaryPointer.Y };
    const bool InsidePlanar =
        (Reading.PlanarBounds.Width > 8.0f && Reading.PlanarBounds.Height > 8.0f && Reading.PlanarBounds.Contains(Cursor));

    RouteKeyboardShortcuts(Reading, InsidePlanar, ViewW, ViewH);

    if (ModalMode != ModalVectorTransformMode::Inactive)
    {
        AdvanceModalTransformStep(Reading, ViewW, ViewH);
    }
    else
    {
        DispatchPointerInteraction(Reading, ViewW, ViewH);
    }

    SynchronizeGizmoAnchor();
}

void VectorViewportInteractor::RouteKeyboardShortcuts(
    const ViewportInteractionReading& Reading,
    bool                              InsidePlanar,
    uint32_t                          ViewW,
    uint32_t                          ViewH)
{
    if (Session == nullptr)
    {
        return;
    }

    const auto& Snapshot = Input::InputExtension::InspectActiveEditorSnapshot();
    const bool  CmdOrCtrl = Snapshot.ControlHeld || Snapshot.SuperHeld;
    const bool  HoveredOrModal = InsidePlanar || QueryDragCaptureActive() || Reading.Input.KeyboardFocused;

    // Undo / Redo / Duplicate / Paste / Reset View
    if (CmdOrCtrl)
    {
        if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Z, false))
        {
            if (Snapshot.ShiftHeld)
            {
                if (Session->ApplyRedo())
                {
                    Session->StatusNotice = "Redo vector step";
                }
            }
            else if (Session->ApplyUndo())
            {
                Session->StatusNotice = "Undo vector step";
            }
            return;
        }
        if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Y, false))
        {
            if (Session->ApplyRedo())
            {
                Session->StatusNotice = "Redo vector step";
            }
            return;
        }
        if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::D, false))
        {
            if (Session->DuplicateSelectedLayer())
            {
                Session->StatusNotice = "Duplicated selected layer";
            }
            return;
        }
        if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Digit0, false))
        {
            Session->ZoomFraction = 1.0f;
            Session->PanOffsetU   = 0.0f;
            Session->PanOffsetV   = 0.0f;
            Session->StatusNotice = "Zoom fit 100%";
            return;
        }
        if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::V, false))
        {
            const char* ClipText = (ImGui::GetCurrentContext() != nullptr) ? ImGui::GetClipboardText() : nullptr;
            if (ClipText != nullptr && ClipText[0] != '\0')
            {
                float DropDocX = 256.0f;
                float DropDocY = 256.0f;
                if (InsidePlanar)
                {
                    const auto Metrics = GeometricRaster::VectorCanvasRasterizer::ComputeTileLayoutMetrics(ViewW, ViewH, *Session);
                    GeometricRaster::VectorCanvasRasterizer::ConvertScreenToDocumentPx(
                        Metrics,
                        Reading.Input.PrimaryPointer.X - Reading.PlanarBounds.X,
                        Reading.Input.PrimaryPointer.Y - Reading.PlanarBounds.Y,
                        DropDocX,
                        DropDocY);
                }
                if (SvgPasteCallback)
                {
                    (void)SvgPasteCallback(ClipText, DropDocX, DropDocY);
                }
                else
                {
                    std::string PasteDiag {};
                    if (Session->ImportSvgIntoSession(ClipText, false, DropDocX, DropDocY, "Clipboard SVG", 0u, &PasteDiag))
                    {
                        Session->StatusNotice = "Pasted clipboard SVG vector";
                    }
                }
            }
            return;
        }
    }

    if (!HoveredOrModal || ModalMode != ModalVectorTransformMode::Inactive)
    {
        return;
    }

    // Escape finishes open Pen contour or falls back to Select
    if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Escape, false))
    {
        if (PenAuthoringActive)
        {
            PenAuthoringActive = false;
            PenDraggingHandles = false;
            PenTargetLayerId   = 0u;
            Session->StatusNotice = "Finished vector curve path";
        }
        else
        {
            Session->ActiveTool          = PatternStampToolCategory::Select;
            Session->ActiveKnotIndex     = -1;
            Session->ActivePointPinIndex = -1;
            ResetActiveTools();
        }
        return;
    }

    if ((Snapshot.QueryKeyPressed(Input::InputLogicalKey::Enter, false) ||
         Snapshot.QueryKeyPressed(Input::InputLogicalKey::KeypadEnter, false)) &&
        PenAuthoringActive)
    {
        PenAuthoringActive = false;
        PenDraggingHandles = false;
        PenTargetLayerId   = 0u;
        Session->StatusNotice = "Completed Bezier contour";
        return;
    }

    // Delete / Backspace deletes active Bezier knot in EditPoints or deletes selected layer
    if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Delete, false) ||
        Snapshot.QueryKeyPressed(Input::InputLogicalKey::Backspace, false))
    {
        PatternStampLayerSpec* Sel = Session->InspectSelectedLayer();
        if (Session->ActiveTool == PatternStampToolCategory::EditPoints && Sel != nullptr && Session->ActiveKnotIndex >= 0)
        {
            int32_t Counter = 0;
            bool DeletedKnot = false;
            for (auto& Sub : Sel->CubicSubpaths)
            {
                for (std::size_t K = 0u; K < Sub.Knots.size(); ++K)
                {
                    if (Counter == Session->ActiveKnotIndex && Sub.Knots.size() > 2u)
                    {
                        Session->PushUndoSnapshot();
                        Sub.Knots.erase(Sub.Knots.begin() + static_cast<std::ptrdiff_t>(K));
                        Session->ActiveKnotIndex = -1;
                        Session->BumpRevision();
                        DeletedKnot = true;
                        break;
                    }
                    ++Counter;
                }
                if (DeletedKnot) break;
            }
            if (DeletedKnot)
            {
                return;
            }
        }
        (void)Session->DeleteSelectedLayer();
        return;
    }

    // Shift+R -> Modal Rotate, Shift+S -> Modal Scale, M -> Modal Grab
    if (Snapshot.ShiftHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::R, false))
    {
        if (const auto* Sel = Session->InspectSelectedLayer(); Sel != nullptr && Sel->Category != PatternStampLayerCategory::Folder)
        {
            Session->PushUndoSnapshot();
            CaptureLayerBaseline(*Sel);
            ModalMode       = ModalVectorTransformMode::RotateAngle;
            ModalConstraint = VectorAxisConstraint::Unconstrained;
            ModalNumericEntry.clear();
            const auto Metrics = GeometricRaster::VectorCanvasRasterizer::ComputeTileLayoutMetrics(ViewW, ViewH, *Session);
            GeometricRaster::VectorCanvasRasterizer::ConvertScreenToDocumentPx(
                Metrics,
                Reading.Input.PrimaryPointer.X - Reading.PlanarBounds.X,
                Reading.Input.PrimaryPointer.Y - Reading.PlanarBounds.Y,
                ModalStartDocX,
                ModalStartDocY);
        }
        return;
    }
    if (Snapshot.ShiftHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::S, false))
    {
        if (const auto* Sel = Session->InspectSelectedLayer(); Sel != nullptr && Sel->Category != PatternStampLayerCategory::Folder)
        {
            Session->PushUndoSnapshot();
            CaptureLayerBaseline(*Sel);
            ModalMode       = ModalVectorTransformMode::ScaleUniform;
            ModalConstraint = VectorAxisConstraint::Unconstrained;
            ModalNumericEntry.clear();
            const auto Metrics = GeometricRaster::VectorCanvasRasterizer::ComputeTileLayoutMetrics(ViewW, ViewH, *Session);
            GeometricRaster::VectorCanvasRasterizer::ConvertScreenToDocumentPx(
                Metrics,
                Reading.Input.PrimaryPointer.X - Reading.PlanarBounds.X,
                Reading.Input.PrimaryPointer.Y - Reading.PlanarBounds.Y,
                ModalStartDocX,
                ModalStartDocY);
        }
        return;
    }

    if (!CmdOrCtrl &&
        ((!Snapshot.ShiftHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::M, false)) ||
         (Snapshot.ShiftHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::G, false))))
    {
        if (const auto* Sel = Session->InspectSelectedLayer(); Sel != nullptr && Sel->Category != PatternStampLayerCategory::Folder)
        {
            Session->PushUndoSnapshot();
            CaptureLayerBaseline(*Sel);
            ModalMode       = ModalVectorTransformMode::GrabTranslate;
            ModalConstraint = VectorAxisConstraint::Unconstrained;
            ModalNumericEntry.clear();
            const auto Metrics = GeometricRaster::VectorCanvasRasterizer::ComputeTileLayoutMetrics(ViewW, ViewH, *Session);
            GeometricRaster::VectorCanvasRasterizer::ConvertScreenToDocumentPx(
                Metrics,
                Reading.Input.PrimaryPointer.X - Reading.PlanarBounds.X,
                Reading.Input.PrimaryPointer.Y - Reading.PlanarBounds.Y,
                ModalStartDocX,
                ModalStartDocY);
        }
        return;
    }

    // Unmodified Single-Key Tool Shortcuts (1:1 parity with InspectToolDescriptors & HTML Studio)
    if (!CmdOrCtrl && !Snapshot.AltHeld && !Snapshot.ShiftHeld)
    {
        struct ToolKeyPair { Input::InputLogicalKey Key; PatternStampToolCategory Tool; };
        static constexpr ToolKeyPair ToolShortcuts[] = {
            { Input::InputLogicalKey::V, PatternStampToolCategory::Select        },
            { Input::InputLogicalKey::U, PatternStampToolCategory::Curve         },
            { Input::InputLogicalKey::P, PatternStampToolCategory::Pen           },
            { Input::InputLogicalKey::A, PatternStampToolCategory::EditPoints    },
            { Input::InputLogicalKey::F, PatternStampToolCategory::PointGradient },
            { Input::InputLogicalKey::R, PatternStampToolCategory::Square        },
            { Input::InputLogicalKey::O, PatternStampToolCategory::Circle        },
            { Input::InputLogicalKey::D, PatternStampToolCategory::Diamond       },
            { Input::InputLogicalKey::T, PatternStampToolCategory::Triangle      },
            { Input::InputLogicalKey::S, PatternStampToolCategory::Star          },
            { Input::InputLogicalKey::G, PatternStampToolCategory::Hexagon       },
            { Input::InputLogicalKey::L, PatternStampToolCategory::Stripe        },
            { Input::InputLogicalKey::X, PatternStampToolCategory::TextBlock     },
            { Input::InputLogicalKey::H, PatternStampToolCategory::HandPan       },
        };
        for (const auto& Entry : ToolShortcuts)
        {
            if (Snapshot.QueryKeyPressed(Entry.Key, false))
            {
                if (PenAuthoringActive && Entry.Tool != PatternStampToolCategory::Pen && Entry.Tool != PatternStampToolCategory::Curve)
                {
                    PenAuthoringActive = false;
                    PenTargetLayerId   = 0u;
                }
                Session->ActiveTool = Entry.Tool;
                return;
            }
        }

        if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Z, false))
        {
            Session->ZoomFraction = 1.0f;
            Session->PanOffsetU   = 0.0f;
            Session->PanOffsetV   = 0.0f;
        }
        else if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Digit1, false))
        {
            Session->ViewChannel = PatternStampViewChannel::VectorSVG;
        }
        else if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Digit2, false))
        {
            Session->ViewChannel = PatternStampViewChannel::Roughness;
        }
        else if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Digit3, false))
        {
            Session->ViewChannel = PatternStampViewChannel::Seamless2x2;
        }
    }
}

void VectorViewportInteractor::AdvanceModalTransformStep(
    const ViewportInteractionReading& Reading,
    uint32_t                          ViewW,
    uint32_t                          ViewH)
{
    if (Session == nullptr)
    {
        return;
    }

    PatternStampLayerSpec* Sel = Session->FindLayerById(Baseline.LayerIdentity);
    if (Sel == nullptr)
    {
        ModalMode = ModalVectorTransformMode::Inactive;
        return;
    }

    const auto& Snapshot = Input::InputExtension::InspectActiveEditorSnapshot();
    const bool  Cancel =
        Snapshot.QueryKeyPressed(Input::InputLogicalKey::Escape, false) || Reading.Input.SecondaryPressed;
    if (Cancel)
    {
        ApplyBaselineTransform(*Sel, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
        Session->BumpRevision();
        Session->RecomputeSummaryMetrics();
        ModalMode = ModalVectorTransformMode::Inactive;
        ModalNumericEntry.clear();
        Session->StatusNotice = "Cancelled transform";
        return;
    }

    if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::X, false))
    {
        ModalConstraint = (ModalConstraint == VectorAxisConstraint::LockX) ? VectorAxisConstraint::Unconstrained : VectorAxisConstraint::LockX;
    }
    else if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Y, false))
    {
        ModalConstraint = (ModalConstraint == VectorAxisConstraint::LockY) ? VectorAxisConstraint::Unconstrained : VectorAxisConstraint::LockY;
    }

    const std::string_view Typed = Snapshot.QueryTypedUtf8();
    for (const char Ch : Typed)
    {
        if ((Ch >= '0' && Ch <= '9') || Ch == '.' || (Ch == '-' && ModalNumericEntry.empty()))
        {
            ModalNumericEntry.push_back(Ch);
        }
    }
    if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Backspace, true) && !ModalNumericEntry.empty())
    {
        ModalNumericEntry.pop_back();
    }

    const auto Metrics = GeometricRaster::VectorCanvasRasterizer::ComputeTileLayoutMetrics(ViewW, ViewH, *Session);
    float CurDocX = ModalStartDocX;
    float CurDocY = ModalStartDocY;
    GeometricRaster::VectorCanvasRasterizer::ConvertScreenToDocumentPx(
        Metrics,
        Reading.Input.PrimaryPointer.X - Reading.PlanarBounds.X,
        Reading.Input.PrimaryPointer.Y - Reading.PlanarBounds.Y,
        CurDocX,
        CurDocY);

    float ParsedScalar = 0.0f;
    const bool NumericValid = !ModalNumericEntry.empty() && ModalNumericEntry != "-" && ModalNumericEntry != "." &&
        (std::sscanf(ModalNumericEntry.c_str(), "%f", &ParsedScalar) == 1);

    if (ModalMode == ModalVectorTransformMode::GrabTranslate)
    {
        float Dx = CurDocX - ModalStartDocX;
        float Dy = CurDocY - ModalStartDocY;
        if (NumericValid)
        {
            if (ModalConstraint == VectorAxisConstraint::LockY)
            {
                Dx = 0.0f;
                Dy = ParsedScalar;
            }
            else
            {
                Dx = ParsedScalar;
                if (ModalConstraint == VectorAxisConstraint::LockX) Dy = 0.0f;
            }
        }
        else
        {
            if (ModalConstraint == VectorAxisConstraint::LockX) Dy = 0.0f;
            if (ModalConstraint == VectorAxisConstraint::LockY) Dx = 0.0f;
        }
        if (Snapshot.ControlHeld)
        {
            Dx = ApplyOptionalSnap(Baseline.CenterX + Dx, true) - Baseline.CenterX;
            Dy = ApplyOptionalSnap(Baseline.CenterY + Dy, true) - Baseline.CenterY;
        }
        ApplyBaselineTransform(*Sel, Dx, Dy, 1.0f, 1.0f, 0.0f);
    }
    else if (ModalMode == ModalVectorTransformMode::RotateAngle)
    {
        float DeltaDeg = NumericValid
            ? ParsedScalar
            : ((CurDocX - ModalStartDocX) * 0.65f);
        if (Snapshot.ControlHeld)
        {
            DeltaDeg = std::round(DeltaDeg / 15.0f) * 15.0f;
        }
        ApplyBaselineTransform(*Sel, 0.0f, 0.0f, 1.0f, 1.0f, DeltaDeg);
    }
    else if (ModalMode == ModalVectorTransformMode::ScaleUniform)
    {
        const float Factor = NumericValid
            ? std::clamp(ParsedScalar, 0.08f, 8.0f)
            : std::clamp(1.0f + (CurDocX - ModalStartDocX) / 140.0f, 0.08f, 8.0f);
        const float Sx = (ModalConstraint == VectorAxisConstraint::LockY) ? 1.0f : Factor;
        const float Sy = (ModalConstraint == VectorAxisConstraint::LockX) ? 1.0f : Factor;
        ApplyBaselineTransform(*Sel, 0.0f, 0.0f, Sx, Sy, 0.0f);
    }

    Session->BumpRevision();
    Session->RecomputeSummaryMetrics();
    Session->StatusNotice = QueryModalStatusSummary();

    if (Reading.Input.PrimaryPointer.Pressed ||
        Snapshot.QueryKeyPressed(Input::InputLogicalKey::Enter, false) ||
        Snapshot.QueryKeyPressed(Input::InputLogicalKey::KeypadEnter, false))
    {
        ModalMode = ModalVectorTransformMode::Inactive;
        ModalNumericEntry.clear();
        Session->StatusNotice = "Applied transform";
    }
}

void VectorViewportInteractor::DispatchPointerInteraction(
    const ViewportInteractionReading& Reading,
    uint32_t                          ViewW,
    uint32_t                          ViewH)
{
    if (Session == nullptr)
    {
        return;
    }

    const auto& Snapshot = Input::InputExtension::InspectActiveEditorSnapshot();
    const bool  ShiftHeld = Snapshot.ShiftHeld;
    const bool  AltHeld   = Snapshot.AltHeld;
    const bool  CtrlHeld  = Snapshot.ControlHeld;
    const bool  SpaceHeld = Snapshot.QueryKeyHeld(Input::InputLogicalKey::Space);

    const Point2D Cursor { Reading.Input.PrimaryPointer.X, Reading.Input.PrimaryPointer.Y };
    const bool InsidePlanar = Reading.PlanarBounds.Contains(Cursor);

    const float LocalPxX = Reading.Input.PrimaryPointer.X - Reading.PlanarBounds.X;
    const float LocalPxY = Reading.Input.PrimaryPointer.Y - Reading.PlanarBounds.Y;
    const int32_t RasterPxX = std::clamp(static_cast<int32_t>(std::round(LocalPxX)), 0, static_cast<int32_t>(ViewW) - 1);
    const int32_t RasterPxY = std::clamp(static_cast<int32_t>(std::round(LocalPxY)), 0, static_cast<int32_t>(ViewH) - 1);

    const auto Metrics = GeometricRaster::VectorCanvasRasterizer::ComputeTileLayoutMetrics(ViewW, ViewH, *Session);
    float DocX = 256.0f;
    float DocY = 256.0f;
    GeometricRaster::VectorCanvasRasterizer::ConvertScreenToDocumentPx(Metrics, LocalPxX, LocalPxY, DocX, DocY);

    // 1. Cursor-anchored wheel zoom inside canvas
    if (InsidePlanar && std::fabs(Reading.Input.WheelDeltaY) > 1.0e-4f)
    {
        const float PrevZoom = std::clamp(Session->ZoomFraction, 0.25f, 4.0f);
        const float NextZoom = std::clamp(PrevZoom * (Reading.Input.WheelDeltaY > 0.0f ? 1.12f : 0.89f), 0.25f, 4.0f);
        if (std::fabs(NextZoom - PrevZoom) > 1.0e-5f)
        {
            const float ReferenceSpan = std::min(static_cast<float>(ViewW) * 0.78f, static_cast<float>(ViewH) * 0.84f);
            const float NextSpan = std::max(32.0f, ReferenceSpan * NextZoom);
            const float RelDocX  = DocX / CanonicalDocExtent;
            const float RelDocY  = DocY / CanonicalDocExtent;
            const float NextMinX = LocalPxX - RelDocX * NextSpan;
            const float NextMinY = LocalPxY - RelDocY * NextSpan;

            Session->ZoomFraction = NextZoom;
            Session->PanOffsetU   = NextMinX - (static_cast<float>(ViewW) - NextSpan) * 0.5f;
            Session->PanOffsetV   = NextMinY - (static_cast<float>(ViewH) - NextSpan) * 0.5f;
        }
    }

    // 2. Active Middle-Drag / Hand / Space+Left Panning
    if (PanningCanvas)
    {
        if (Reading.Input.MiddleDown || (Reading.Input.PrimaryPointer.Down && (SpaceHeld || Session->ActiveTool == PatternStampToolCategory::HandPan)))
        {
            Session->PanOffsetU = PanStartU + (Reading.Input.PrimaryPointer.X - PanStartCursorX);
            Session->PanOffsetV = PanStartV + (Reading.Input.PrimaryPointer.Y - PanStartCursorY);
            return;
        }
        PanningCanvas = false;
    }

    if (InsidePlanar && (Reading.Input.MiddlePressed ||
        (Reading.Input.PrimaryPointer.Pressed && (SpaceHeld || Session->ActiveTool == PatternStampToolCategory::HandPan))))
    {
        PanningCanvas   = true;
        PanStartU       = Session->PanOffsetU;
        PanStartV       = Session->PanOffsetV;
        PanStartCursorX = Reading.Input.PrimaryPointer.X;
        PanStartCursorY = Reading.Input.PrimaryPointer.Y;
        return;
    }

    // 3. Continue active drags (Gizmo2D, Direct Layer Move, Knot/Tangent Edit, PointPin Edit, Shape Draw, Marquee)
    if (DraggingGizmo2D)
    {
        if (Reading.Input.PrimaryPointer.Down)
        {
            PatternStampLayerSpec* Sel = Session->FindLayerById(Baseline.LayerIdentity);
            if (Sel != nullptr)
            {
                const Editor::ViewportRay3D DragRay { { DocX, -DocY, 5.0f }, { 0.0f, 0.0f, -1.0f } };
                Configuration::GizmoSnapSettings SnapCfg {};
                SnapCfg.UVMoveStepUnits     = (Session->SnapStepPx > 0) ? static_cast<float>(Session->SnapStepPx) : 16.0f;
                SnapCfg.RotationStepDegrees = 15.0f;
                SnapCfg.ScaleFactorStep     = 0.10f;

                const auto Delta = Gizmo2D.EvaluateDragInteraction(
                    DragRay,
                    CtrlHeld,
                    SnapCfg,
                    ActiveGizmoTransform);

                if (Delta.TransformChanged)
                {
                    const float Dx       = ActiveGizmoTransform.Position.X - Baseline.CenterX;
                    const float Dy       = (-ActiveGizmoTransform.Position.Y) - Baseline.CenterY;
                    const float Sx       = ActiveGizmoTransform.Scale.X;
                    const float Sy       = ActiveGizmoTransform.Scale.Y;
                    const float DeltaRot = -ActiveGizmoTransform.Rotation.Z * Constants::RadToDegFloat;
                    ApplyBaselineTransform(*Sel, Dx, Dy, Sx, Sy, DeltaRot);
                    Session->BumpRevision();
                    Session->RecomputeSummaryMetrics();
                    if (!Delta.HudReadout.empty())
                    {
                        Session->StatusNotice = Delta.HudReadout;
                    }
                }
            }
            return;
        }
        DraggingGizmo2D = false;
        Gizmo2D.EndDragInteraction();
        return;
    }

    if (DraggingDirectLayer)
    {
        if (Reading.Input.PrimaryPointer.Down)
        {
            if (PatternStampLayerSpec* Sel = Session->FindLayerById(Baseline.LayerIdentity))
            {
                float Dx = DocX - DragStartDocX;
                float Dy = DocY - DragStartDocY;
                if (ShiftHeld)
                {
                    if (std::fabs(Dx) >= std::fabs(Dy)) Dy = 0.0f;
                    else Dx = 0.0f;
                }
                if (CtrlHeld)
                {
                    Dx = ApplyOptionalSnap(Baseline.CenterX + Dx, true) - Baseline.CenterX;
                    Dy = ApplyOptionalSnap(Baseline.CenterY + Dy, true) - Baseline.CenterY;
                }
                ApplyBaselineTransform(*Sel, Dx, Dy, 1.0f, 1.0f, 0.0f);
                Session->BumpRevision();
            }
            return;
        }
        DraggingDirectLayer = false;
        return;
    }

    if (DraggingKnotControl)
    {
        if (Reading.Input.PrimaryPointer.Down)
        {
            if (PatternStampLayerSpec* Sel = Session->InspectSelectedLayer())
            {
                if (auto* Knot = ResolveMutableKnotByGlobalIndex(*Sel, ActiveKnotGlobalIdx))
                {
                    const float SnappedDocX = ApplyOptionalSnap(DocX, CtrlHeld);
                    const float SnappedDocY = ApplyOptionalSnap(DocY, CtrlHeld);
                    const float RelDocX     = SnappedDocX - Sel->CenterX;
                    const float RelDocY     = SnappedDocY - Sel->CenterY;
                    const float RotRad      = Sel->RotationDeg * Constants::DegToRadFloat;
                    const float CosR        = std::cos(RotRad);
                    const float SinR        = std::sin(RotRad);
                    const float LocalX      =  RelDocX * CosR + RelDocY * SinR;
                    const float LocalY      = -RelDocX * SinR + RelDocY * CosR;

                    if (ActiveKnotTarget == KnotControlPart::AnchorPoint)
                    {
                        const float Dx = LocalX - Knot->AnchorX;
                        const float Dy = LocalY - Knot->AnchorY;
                        Knot->AnchorX    += Dx;
                        Knot->AnchorY    += Dy;
                        Knot->InHandleX  += Dx;
                        Knot->InHandleY  += Dy;
                        Knot->OutHandleX += Dx;
                        Knot->OutHandleY += Dy;
                    }
                    else if (ActiveKnotTarget == KnotControlPart::OutTangent)
                    {
                        Knot->OutHandleX = LocalX;
                        Knot->OutHandleY = LocalY;
                        if (AltHeld) Knot->Smooth = false;
                        if (Knot->Smooth)
                        {
                            Knot->InHandleX = Knot->AnchorX - (LocalX - Knot->AnchorX);
                            Knot->InHandleY = Knot->AnchorY - (LocalY - Knot->AnchorY);
                        }
                    }
                    else
                    {
                        Knot->InHandleX = LocalX;
                        Knot->InHandleY = LocalY;
                        if (AltHeld) Knot->Smooth = false;
                        if (Knot->Smooth)
                        {
                            Knot->OutHandleX = Knot->AnchorX - (LocalX - Knot->AnchorX);
                            Knot->OutHandleY = Knot->AnchorY - (LocalY - Knot->AnchorY);
                        }
                    }
                    Session->BumpRevision();
                }
            }
            return;
        }
        DraggingKnotControl = false;
        return;
    }

    if (DraggingPointPin)
    {
        if (Reading.Input.PrimaryPointer.Down)
        {
            if (PatternStampLayerSpec* Sel = Session->InspectSelectedLayer();
                Sel != nullptr && ActivePinSlot >= 0 && ActivePinSlot < 12)
            {
                auto& Pin = Sel->PointFieldPins[static_cast<std::size_t>(ActivePinSlot)];
                Pin.Active = true;
                Pin.NormX  = std::clamp(DocX / CanonicalDocExtent, 0.0f, 1.0f);
                Pin.NormY  = std::clamp(DocY / CanonicalDocExtent, 0.0f, 1.0f);
                Session->BumpRevision();
            }
            return;
        }
        DraggingPointPin = false;
        return;
    }

    if (PenDraggingHandles)
    {
        if (Reading.Input.PrimaryPointer.Down)
        {
            if (PatternStampLayerSpec* PenLayer = Session->FindLayerById(PenTargetLayerId);
                PenLayer != nullptr && !PenLayer->CubicSubpaths.empty() && !PenLayer->CubicSubpaths.back().Knots.empty())
            {
                auto& Knot = PenLayer->CubicSubpaths.back().Knots.back();
                const float LocalX = DocX - PenLayer->CenterX;
                const float LocalY = DocY - PenLayer->CenterY;
                const float Dx = LocalX - Knot.AnchorX;
                const float Dy = LocalY - Knot.AnchorY;
                if (std::hypot(Dx, Dy) > 3.0f)
                {
                    Knot.Smooth     = true;
                    Knot.OutHandleX = Knot.AnchorX + Dx;
                    Knot.OutHandleY = Knot.AnchorY + Dy;
                    Knot.InHandleX  = Knot.AnchorX - Dx;
                    Knot.InHandleY  = Knot.AnchorY - Dy;
                    Session->BumpRevision();
                }
            }
            return;
        }
        PenDraggingHandles = false;
        return;
    }

    if (CreatingShape)
    {
        if (Reading.Input.PrimaryPointer.Down)
        {
            if (PatternStampLayerSpec* Created = Session->FindLayerById(CreatingLayerId))
            {
                const float X0 = CreateOriginDocX;
                const float Y0 = CreateOriginDocY;
                float X1 = ApplyOptionalSnap(DocX, CtrlHeld);
                float Y1 = ApplyOptionalSnap(DocY, CtrlHeld);
                if (ShiftHeld)
                {
                    const float Span = std::max(std::fabs(X1 - X0), std::fabs(Y1 - Y0));
                    X1 = X0 + (X1 >= X0 ? Span : -Span);
                    Y1 = Y0 + (Y1 >= Y0 ? Span : -Span);
                }
                if (AltHeld)
                {
                    const float HalfW = std::max(8.0f, std::fabs(X1 - X0));
                    const float HalfH = std::max(8.0f, std::fabs(Y1 - Y0));
                    Created->CenterX  = X0;
                    Created->CenterY  = Y0;
                    Created->WidthPx  = std::clamp(HalfW * 2.0f, 16.0f, 512.0f);
                    Created->HeightPx = std::clamp(HalfH * 2.0f, 16.0f, 512.0f);
                }
                else
                {
                    Created->CenterX  = 0.5f * (X0 + X1);
                    Created->CenterY  = 0.5f * (Y0 + Y1);
                    Created->WidthPx  = std::clamp(std::fabs(X1 - X0), 16.0f, 512.0f);
                    Created->HeightPx = std::clamp(std::fabs(Y1 - Y0), 16.0f, 512.0f);
                }
                Created->CubicSubpaths.clear();
                PatternStampWorkspaceSession::SeedMotifCubicSubpathsIfEmpty(*Created);
                Session->BumpRevision();
                Session->RecomputeSummaryMetrics();
            }
            return;
        }
        if (PatternStampLayerSpec* Created = Session->FindLayerById(CreatingLayerId))
        {
            if (Created->WidthPx < 20.0f && Created->HeightPx < 20.0f)
            {
                Created->WidthPx  = 128.0f;
                Created->HeightPx = 128.0f;
                Created->CubicSubpaths.clear();
                PatternStampWorkspaceSession::SeedMotifCubicSubpathsIfEmpty(*Created);
                Session->BumpRevision();
                Session->RecomputeSummaryMetrics();
            }
        }
        CreatingShape   = false;
        CreatingLayerId = 0u;
        return;
    }

    if (Session->MarqueeActive)
    {
        if (Reading.Input.PrimaryPointer.Down)
        {
            Session->MarqueeEndX = DocX;
            Session->MarqueeEndY = DocY;
            return;
        }
        Session->MarqueeActive = false;
        if (Rasterizer != nullptr)
        {
            const uint32_t MinX = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(std::min(DragStartDocX, LocalPxX)), 0, static_cast<int32_t>(ViewW) - 1));
            const uint32_t MinY = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(std::min(DragStartDocY, LocalPxY)), 0, static_cast<int32_t>(ViewH) - 1));
            const uint32_t MaxX = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(std::max(DragStartDocX, LocalPxX)), 0, static_cast<int32_t>(ViewW) - 1));
            const uint32_t MaxY = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(std::max(DragStartDocY, LocalPxY)), 0, static_cast<int32_t>(ViewH) - 1));
            if (MaxX > MinX + 3u && MaxY > MinY + 3u)
            {
                const auto Summary = Rasterizer->Reduce2DRegionPick(MinX, MinY, MaxX, MaxY);
                if (Summary.HitPixelCount > 0u && Summary.FirstInstanceSlot != 0u)
                {
                    const uint32_t PickedId = Summary.FirstInstanceSlot;
                    Session->SelectLayer(PickedId);
                    Session->StatusNotice = "Marquee selected layer #" + std::to_string(PickedId);
                }
            }
        }
        return;
    }

    // 4. Hover GPU picking (Gizmo handle + layer hover highlight) when inside planar viewport
    if (!InsidePlanar)
    {
        Session->HoveredLayerId = 0u;
        Gizmo2D.AssignHoveredHandle(Editor::GizmoHandleOrdinal::None);
        return;
    }

    GeometricRaster::ViewportPickHit HoverHit {};
    if (Rasterizer != nullptr)
    {
        HoverHit = Rasterizer->Pick2DPixel(RasterPxX, RasterPxY);
    }

    const auto PickedGizmoHandle = (Session->ActiveTool == PatternStampToolCategory::Select && HoverHit.GizmoHandleOrdinal > 0u && HoverHit.GizmoHandleOrdinal <= 13u)
        ? static_cast<Editor::GizmoHandleOrdinal>(HoverHit.GizmoHandleOrdinal)
        : Editor::GizmoHandleOrdinal::None;
    Gizmo2D.AssignHoveredHandle(PickedGizmoHandle);
    Session->HoveredLayerId = (PickedGizmoHandle == Editor::GizmoHandleOrdinal::None) ? HoverHit.InstanceSlot : 0u;

    if (!Reading.Input.PrimaryPointer.Pressed)
    {
        return;
    }

    // 5. Left-button press dispatch per ActiveTool
    PatternStampMotifShape CreationShape = PatternStampMotifShape::Circle;
    if (QueryToolCreatesMotifShape(Session->ActiveTool, CreationShape))
    {
        if (Session->Document.Layers.size() < PatternStampWorkspaceSession::MaxLayerLimit)
        {
            Session->AddLayer(PatternStampLayerCategory::Motif);
            if (PatternStampLayerSpec* Created = Session->InspectSelectedLayer())
            {
                CreateOriginDocX    = ApplyOptionalSnap(DocX, CtrlHeld);
                CreateOriginDocY    = ApplyOptionalSnap(DocY, CtrlHeld);
                Created->Name       = QueryShapeDefaultTitle(CreationShape);
                Created->MotifShape = CreationShape;
                Created->CenterX    = CreateOriginDocX;
                Created->CenterY    = CreateOriginDocY;
                Created->WidthPx    = 8.0f;
                Created->HeightPx   = 8.0f;
                if (CreationShape == PatternStampMotifShape::LineStroke)
                {
                    Created->FillEnabled   = false;
                    Created->StrokeWidthPx = 6.0f;
                    Created->StrokeTint    = Created->FillTint;
                }
                Created->CubicSubpaths.clear();
                PatternStampWorkspaceSession::SeedMotifCubicSubpathsIfEmpty(*Created);
                CreatingShape   = true;
                CreatingLayerId = Created->Identity;
                Session->BumpRevision();
                Session->RecomputeSummaryMetrics();
            }
        }
        return;
    }

    if (Session->ActiveTool == PatternStampToolCategory::TextBlock)
    {
        if (Session->Document.Layers.size() < PatternStampWorkspaceSession::MaxLayerLimit)
        {
            Session->AddLayer(PatternStampLayerCategory::Typographic);
            if (PatternStampLayerSpec* Txt = Session->InspectSelectedLayer())
            {
                Txt->CenterX = ApplyOptionalSnap(DocX, CtrlHeld);
                Txt->CenterY = ApplyOptionalSnap(DocY, CtrlHeld);
                Session->BumpRevision();
                Session->RecomputeSummaryMetrics();
                Session->StatusNotice = "Placed typographic block";
            }
        }
        return;
    }

    if (Session->ActiveTool == PatternStampToolCategory::Pen || Session->ActiveTool == PatternStampToolCategory::Curve)
    {
        PatternStampLayerSpec* PenLayer = Session->FindLayerById(PenTargetLayerId);
        if (!PenAuthoringActive || PenLayer == nullptr || PenLayer->Category != PatternStampLayerCategory::Motif)
        {
            if (Session->Document.Layers.size() >= PatternStampWorkspaceSession::MaxLayerLimit)
            {
                return;
            }
            Session->AddLayer(PatternStampLayerCategory::Motif);
            PenLayer = Session->InspectSelectedLayer();
            if (PenLayer == nullptr)
            {
                return;
            }
            PenLayer->Name          = "Bezier Curve";
            PenLayer->MotifShape    = PatternStampMotifShape::VectorPath;
            PenLayer->CenterX       = DocX;
            PenLayer->CenterY       = DocY;
            PenLayer->WidthPx       = 128.0f;
            PenLayer->HeightPx      = 128.0f;
            PenLayer->FillEnabled   = false;
            PenLayer->StrokeWidthPx = 4.0f;
            PenLayer->StrokeTint    = PenLayer->FillTint;
            PenLayer->CubicSubpaths.clear();
            PenLayer->CubicSubpaths.push_back(GeometricRaster::VectorCubicSubpath{ false, {} });
            PenTargetLayerId   = PenLayer->Identity;
            PenAuthoringActive = true;
        }

        auto& Sub = PenLayer->CubicSubpaths.back();
        const float LocalX = ApplyOptionalSnap(DocX, CtrlHeld) - PenLayer->CenterX;
        const float LocalY = ApplyOptionalSnap(DocY, CtrlHeld) - PenLayer->CenterY;

        // Close loop if clicking near first knot
        if (Sub.Knots.size() >= 3u)
        {
            const auto& First = Sub.Knots.front();
            if (std::hypot(LocalX - First.AnchorX, LocalY - First.AnchorY) <= 12.0f)
            {
                Sub.ClosedLoop            = true;
                PenLayer->ClosedCurveLoop = true;
                PenLayer->FillEnabled     = true;
                PenAuthoringActive        = false;
                PenDraggingHandles        = false;
                PenTargetLayerId          = 0u;
                Session->BumpRevision();
                Session->StatusNotice = "Closed Bezier contour";
                return;
            }
        }

        const bool SmoothKnotCurve = (Session->ActiveTool == PatternStampToolCategory::Curve);
        GeometricRaster::VectorCubicKnot NewKnot { LocalX, LocalY, LocalX, LocalY, LocalX, LocalY, SmoothKnotCurve };
        if (SmoothKnotCurve && !Sub.Knots.empty())
        {
            const auto& Prev = Sub.Knots.back();
            const float Dx = (LocalX - Prev.AnchorX) * 0.25f;
            const float Dy = (LocalY - Prev.AnchorY) * 0.25f;
            NewKnot.InHandleX  = LocalX - Dx;
            NewKnot.InHandleY  = LocalY - Dy;
            NewKnot.OutHandleX = LocalX + Dx;
            NewKnot.OutHandleY = LocalY + Dy;
        }
        Sub.Knots.push_back(NewKnot);
        Session->ActiveKnotIndex = static_cast<int32_t>(Sub.Knots.size()) - 1;
        PenDraggingHandles       = true;
        Session->BumpRevision();
        return;
    }

    if (Session->ActiveTool == PatternStampToolCategory::EditPoints)
    {
        PatternStampLayerSpec* Sel = Session->InspectSelectedLayer();
        if (Sel == nullptr || Sel->Category == PatternStampLayerCategory::Folder || Sel->Category == PatternStampLayerCategory::Typographic)
        {
            if (HoverHit.InstanceSlot != 0u)
            {
                Session->SelectLayer(HoverHit.InstanceSlot);
            }
            return;
        }
        if (Sel->CubicSubpaths.empty())
        {
            PatternStampWorkspaceSession::SeedMotifCubicSubpathsIfEmpty(*Sel);
        }

        KnotControlPart PickedPart = KnotControlPart::AnchorPoint;
        const int32_t FoundIdx = FindNearestKnot(*Sel, DocX, DocY, 16.0f / std::clamp(Session->ZoomFraction, 0.25f, 4.0f), PickedPart);
        if (FoundIdx >= 0)
        {
            Session->PushUndoSnapshot();
            Session->ActiveKnotIndex = FoundIdx;
            ActiveKnotGlobalIdx      = FoundIdx;
            ActiveKnotTarget         = PickedPart;
            DraggingKnotControl       = true;
        }
        else if (HoverHit.InstanceSlot != 0u && HoverHit.InstanceSlot != Sel->Identity)
        {
            Session->SelectLayer(HoverHit.InstanceSlot);
            Session->ActiveKnotIndex = -1;
        }
        return;
    }

    if (Session->ActiveTool == PatternStampToolCategory::PointGradient)
    {
        PatternStampLayerSpec* Sel = Session->InspectSelectedLayer();
        if (Sel == nullptr || Sel->Category == PatternStampLayerCategory::Folder)
        {
            return;
        }
        Session->PushUndoSnapshot();
        Sel->FillSource = PatternStampFillSourceMode::PointField;
        PatternStampWorkspaceSession::InitializeDefaultPointFieldPins(*Sel);
        int32_t PinSlot = FindNearestPointPin(*Sel, DocX, DocY, 20.0f / std::clamp(Session->ZoomFraction, 0.25f, 4.0f));
        if (PinSlot < 0)
        {
            for (int32_t P = 0; P < 12; ++P)
            {
                if (!Sel->PointFieldPins[static_cast<std::size_t>(P)].Active)
                {
                    PinSlot = P;
                    Sel->PointFieldPins[static_cast<std::size_t>(P)].Active = true;
                    Sel->PointFieldPins[static_cast<std::size_t>(P)].NormX  = std::clamp(DocX / CanonicalDocExtent, 0.0f, 1.0f);
                    Sel->PointFieldPins[static_cast<std::size_t>(P)].NormY  = std::clamp(DocY / CanonicalDocExtent, 0.0f, 1.0f);
                    Sel->PointFieldPins[static_cast<std::size_t>(P)].Weight = 1.0f;
                    Sel->PointFieldPins[static_cast<std::size_t>(P)].Tint   = Sel->FillTint;
                    break;
                }
            }
        }
        if (PinSlot >= 0)
        {
            Session->ActivePointPinIndex = PinSlot;
            ActivePinSlot                = PinSlot;
            DraggingPointPin             = true;
            Session->BumpRevision();
        }
        return;
    }

    // Select tool (default)
    if (PickedGizmoHandle != Editor::GizmoHandleOrdinal::None)
    {
        if (const PatternStampLayerSpec* Sel = Session->InspectSelectedLayer();
            Sel != nullptr && Sel->Category != PatternStampLayerCategory::Folder && !Sel->Locked)
        {
            Session->PushUndoSnapshot();
            CaptureLayerBaseline(*Sel);
            ActiveGizmoTransform          = Mathematics::SpatialTransform{};
            ActiveGizmoTransform.Position = { Sel->CenterX, -Sel->CenterY, 0.0f };
            ActiveGizmoTransform.Rotation = { 0.0f, 0.0f, 0.0f };
            ActiveGizmoTransform.Scale    = { 1.0f, 1.0f, 1.0f };

            const Editor::ViewportRay3D InitialRay { { DocX, -DocY, 5.0f }, { 0.0f, 0.0f, -1.0f } };
            if (Gizmo2D.BeginDragInteraction(PickedGizmoHandle, InitialRay, ActiveGizmoTransform))
            {
                DraggingGizmo2D = true;
                return;
            }
        }
    }

    if (HoverHit.InstanceSlot != 0u)
    {
        Session->SelectLayer(HoverHit.InstanceSlot);
        if (const PatternStampLayerSpec* Sel = Session->InspectSelectedLayer();
            Sel != nullptr && Sel->Category != PatternStampLayerCategory::Folder && !Sel->Locked)
        {
            Session->PushUndoSnapshot();
            CaptureLayerBaseline(*Sel);
            DragStartDocX       = DocX;
            DragStartDocY       = DocY;
            DraggingDirectLayer = true;
        }
        return;
    }

    // Empty background drag -> Marquee box selection
    DragStartDocX          = LocalPxX;
    DragStartDocY          = LocalPxY;
    Session->MarqueeActive = true;
    Session->MarqueeStartX = DocX;
    Session->MarqueeStartY = DocY;
    Session->MarqueeEndX   = DocX;
    Session->MarqueeEndY   = DocY;
}

} // namespace Frontier::Workspaces
