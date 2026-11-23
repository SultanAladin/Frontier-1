//============================================================================================================================================
//                                                    SKETCHVIEWPORTOVERLAY.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/SketchViewportOverlay/Api/SketchViewportOverlay.h"

#include "SlateShape/Sketch/ConstraintSolver/Api/ConstraintSolver.h"
#include "SlateShape/Sketch/ProfileArea/Api/ProfileArea.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateWorkspace/Discipline/ConstraintAuthoring/Api/ConstraintAuthoring.h"
#include "SlateWorkspace/Discipline/PlacementCommit/Api/PlacementCommit.h"
#include "SlateWorkspace/Discipline/TransformGizmo/Api/TransformGizmo.h"
#include "SlateWorkspace/Discipline/TransformSequence/Api/TransformSequence.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/DrawableScale.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Slate
{

void RecordViewportOrientationHud(RecordingSurface& Surface,
                                  const PlaneExtent& Extent,
                                  const PointerCondition& Pointer,
                                  ViewportStanding& View,
                                  EditorPanelConfiguration& Configuration,
                                  bool& PointerTaken)
{
    bool& Perspective = Configuration.Perspective;

    // 📝 The widget is drawn from the CAMERA's axes, so the basis handed to it is the view frame about the
    //    world axes — not the sketch plane's basis, which would tilt the cube with the drawing.
    const SpatialBasis WorldBasis = { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
    const ViewFrame Frame = ResolveViewportFrame(WorldBasis, View, Perspective);

    CubeBasis Widget;
    Widget.Right[0]   = Frame.Right.Left;
    Widget.Right[1]   = Frame.Right.Up;
    Widget.Right[2]   = Frame.Right.Forward;
    Widget.Up[0]      = Frame.Up.Left;
    Widget.Up[1]      = Frame.Up.Up;
    Widget.Up[2]      = Frame.Up.Forward;
    Widget.Forward[0] = Frame.Forward.Left;
    Widget.Forward[1] = Frame.Forward.Up;
    Widget.Forward[2] = Frame.Forward.Forward;

    const bool Cad = Configuration.Gizmo == PanelGizmo::Cad;

    if (Pointer.ContactPressed)
    {
        // 🔴 The seven-case conversion switch is gone with the duplicate enumeration. The widget answers a
        //    `ViewportOrientation` directly, and a miss is a refusal rather than a `None` member that
        //    every switch over the enumeration then had to carry.
        const Deliver<ViewportOrientation> Struck =
            HitOrientationWidget(Extent, Widget, Pointer.PositionX, Pointer.PositionY, Cad);

        if (Struck.Resolved)
        {
            const bool Isometric = Struck.Delivered == ViewportOrientation::Isometric;
            ApplyViewportOrientation(View, Struck.Delivered, Isometric);
            Perspective = Isometric;
            PointerTaken = true;
        }
    }

    RecordOrientationWidget(Surface, Extent, Widget, Cad);
}

void RecordViewportStateReadout(RecordingSurface& Surface,
                                const PlaneExtent& Extent,
                                const ViewportStanding& View,
                                bool Perspective,
                                const WorkspaceCadPacket& Packet)
{
    char Detail[192] = {};
    std::snprintf(Detail, sizeof(Detail),
                  "%s • %s • %u segments • %u fills • %u markers",
                  Perspective ? "Perspective" : "Orthographic",
                  OrientationText(View.Orientation),
                  static_cast<unsigned>(Packet.SegmentCount),
                  static_cast<unsigned>(Packet.FillCount),
                  static_cast<unsigned>(Packet.MarkerCount));
    Surface.TextRun(Extent.MinimumX + 16.0f,
                    Extent.MaximumY - 24.0f,
                    Faded(Covering(0xE5E7EBu), 0.75f),
                    Detail, 11.0f);
}

void RecordConstraintGlyphs(RecordingSurface& Surface,
                            const PlaneExtent& Extent,
                            const SpatialBasis& Basis,
                            const SketchStructure& Sketch,
                            const ViewportStanding& View,
                            bool Perspective)
{
    if (!Sketch.Declared())
        return;
    for (const ConstraintSpecification& Constraint : Sketch.Constraints())
    {
        SpatialPoint Anchor = {};
        if (!ResolveConstraintAnchor(Sketch, Constraint.Primary, Anchor))
            continue;

        float X = 0.0f, Y = 0.0f;
        if (!ProjectSpatialPoint(Basis, View, Perspective, Extent, Anchor, X, Y))
            continue;

        Surface.TextRun(X + 6.0f, Y - 6.0f, Covering(0xFBBF24u),
                        ConstraintGlyph(Constraint.Subject), 12.0f, 0.0f, true);
    }
}

void RecordConstraintGlyphs(RecordingSurface& Surface,
                            const PlaneExtent& Extent,
                            const SketchStructure& Sketch,
                            const ViewportStanding& View,
                            bool Perspective)
{
    if (!Sketch.PlaneDeclared())
        return;
    RecordConstraintGlyphs(Surface, Extent, ResolveSketchBasis(Sketch), Sketch, View, Perspective);
}

void RecordProfileAreaOverlay(RecordingSurface& Surface,
                                const PlaneExtent& Extent,
                                const SpatialBasis& Basis,
                                const SketchStructure& Sketch,
                                const ViewportStanding& View,
                                bool Perspective)
{
    if (!Sketch.Declared())
        return;
    const ProfileAreaAnalysis Analysis = AnalyzeProfileAreas(Sketch, 0.05);

    for (const ProfileAreaTriangle& Triangle : Analysis.Triangles)
    {
        float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f, X2 = 0.0f, Y2 = 0.0f;
        if (!ProjectSpatialPoint(Basis, View, Perspective, Extent, Triangle.A, X0, Y0) ||
            !ProjectSpatialPoint(Basis, View, Perspective, Extent, Triangle.B, X1, Y1) ||
            !ProjectSpatialPoint(Basis, View, Perspective, Extent, Triangle.C, X2, Y2))
            continue;
        const float Corners[6] = { X0, Y0, X1, Y1, X2, Y2 };
        Surface.Tongue(Corners, 3u, Partial(0x5B8CFFu, Triangle.Role == ProfileAreaLoopRole::Outer ? 0.12 : 0.04));
    }

    for (const ProfileAreaLoop& Loop : Analysis.Loops)
    {
        const ThemeToken Tone = Loop.SelfIntersecting ? Covering(0xF97316u)
                              : Loop.Role == ProfileAreaLoopRole::Hole ? Covering(0xA78BFAu)
                                                                         : Faded(Covering(0x34D399u), 0.82f);
        for (std::size_t Index = 0u; Index + 1u < Loop.Points.size(); ++Index)
        {
            float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f;
            if (ProjectSpatialPoint(Basis, View, Perspective, Extent, Loop.Points[Index], X0, Y0) &&
                ProjectSpatialPoint(Basis, View, Perspective, Extent, Loop.Points[Index + 1u], X1, Y1))
            {
                const float Xs[2] = { X0, X1 };
                const float Ys[2] = { Y0, Y1 };
                Surface.Polyline(Xs, Ys, 2u, Tone, Loop.Role == ProfileAreaLoopRole::Hole ? 1.3f : 1.8f);
            }
        }
        if (!Loop.Points.empty())
        {
            float X = 0.0f, Y = 0.0f;
            if (ProjectSpatialPoint(Basis, View, Perspective, Extent, Loop.Points[Loop.Points.size() / 2u], X, Y))
                Surface.TextRun(X + 8.0f, Y + 8.0f,
                                Loop.Role == ProfileAreaLoopRole::Hole ? Covering(0xC4B5FDu) : Covering(0xA7F3D0u),
                                Loop.Role == ProfileAreaLoopRole::Hole ? "hole" : "outer", 10.0f);
        }
    }

    for (const ProfileAreaIssue& Issue : Analysis.Issues)
    {
        float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f;
        if (!ProjectSpatialPoint(Basis, View, Perspective, Extent, Issue.Primary, X0, Y0))
            continue;
        const ThemeToken Tone = Issue.Subject == ProfileAreaIssueSubject::SelfIntersection ? Covering(0xF97316u)
                                                                                              : Covering(0xEF4444u);
        if (Issue.Subject == ProfileAreaIssueSubject::Gap &&
            ProjectSpatialPoint(Basis, View, Perspective, Extent, Issue.Secondary, X1, Y1))
        {
            const float Xs[2] = { X0, X1 };
            const float Ys[2] = { Y0, Y1 };
            Surface.Polyline(Xs, Ys, 2u, Tone, 2.4f);
        }
        Surface.Medallion(X0, Y0, Issue.Subject == ProfileAreaIssueSubject::SelfIntersection ? 5.5f : 4.5f, Tone);
        Surface.TextRun(X0 + 8.0f, Y0 - 8.0f, Tone,
                        Issue.Subject == ProfileAreaIssueSubject::SelfIntersection ? "self-intersection" : "profile gap", 10.0f);
    }
}

void RecordProfileAreaOverlay(RecordingSurface& Surface,
                                const PlaneExtent& Extent,
                                const SketchStructure& Sketch,
                                const ViewportStanding& View,
                                bool Perspective)
{
    if (!Sketch.PlaneDeclared())
        return;
    RecordProfileAreaOverlay(Surface, Extent, ResolveSketchBasis(Sketch), Sketch, View, Perspective);
}

void RecordProfileValidationReadout(RecordingSurface& Surface,
                                    const PlaneExtent& Extent,
                                    const SketchStructure& Sketch)
{
    std::uint32_t ValidProfiles = 0u;
    for (const ProfileSpecification& Profile : Sketch.Profiles())
        if (Profile.Declared())
            ++ValidProfiles;

    const ConstraintDisposition Constraints = EvaluateConstraints(Sketch);
    const bool ConstraintWarning = Constraints == ConstraintDisposition::InvalidSketch
                                || Constraints == ConstraintDisposition::UnsupportedConstraint
                                || Constraints == ConstraintDisposition::ConflictingConstraint;
    const ProfileAreaAnalysis Areas = AnalyzeProfileAreas(Sketch, 0.05);
    char Detail[224] = {};
    std::snprintf(Detail, sizeof(Detail), "%u/%u profiles valid • %u areas/%u issues • %u constraints: %s • %s/%s",
                  static_cast<unsigned>(ValidProfiles),
                  static_cast<unsigned>(Sketch.Profiles().size()),
                  static_cast<unsigned>(Areas.Loops.size()),
                  static_cast<unsigned>(Areas.Issues.size()),
                  static_cast<unsigned>(Sketch.Constraints().size()),
                  ConstraintDispositionNaming(Constraints),
                  Areas.Clipper2BackendAvailable ? "clipper2" : "poly fallback",
                  Areas.EarcutBackendAvailable ? "earcut" : "fan preview");
    Surface.TextRun(Extent.MinimumX + 16.0f,
                    Extent.MaximumY - 42.0f,
                    ConstraintWarning ? Covering(0xFBBF24u) : Faded(Covering(0xA7F3D0u), 0.85f),
                    Detail, 11.0f);
}

ThemeToken SnapToneFor(SketchSnapSubject Subject)
{
    switch (Subject)
    {
        case SketchSnapSubject::Endpoint:      return Covering(0xFBBF24u);
        case SketchSnapSubject::Midpoint:      return Covering(0x34D399u);
        case SketchSnapSubject::Centre:        return Covering(0x60A5FAu);
        case SketchSnapSubject::Control:       return Covering(0xA78BFAu);
        case SketchSnapSubject::AlongCurve:    return Covering(0xF472B6u);
        case SketchSnapSubject::Intersection:  return Covering(0xF97316u);
        case SketchSnapSubject::Grid:          return Covering(0xE5E7EBu);
        case SketchSnapSubject::Perpendicular: return Covering(0x22D3EEu);
        case SketchSnapSubject::Tangent:       return Covering(0xFACC15u);
        case SketchSnapSubject::None:
        case SketchSnapSubject::SubjectCount:  return Covering(0x5B8CFFu);
    }
    return Covering(0x5B8CFFu);
}

// 🔴 `RecordPlacementPreview` IS GONE, AND ITS ABSENCE IS THE FIX. It drew the shape under the pointer
//    into ImGui draw lists on the CPU every frame, and it did so through a chain of `else if` branches
//    that each named ONE subject -- so `Hermite`, `BasisSpline` and `RationalSpline` had no branch and
//    previewed as nothing at all, while `Bezier` drew a bare curve with no control points. Both faults
//    were structural: a per-subject chain silently omits whatever nobody remembered to add.
//
// 🔴 `ProjectPlacementPreview` replaces it, appending the preview to the SAME `WorkspaceCadPacket` the
//    committed shapes are rasterised from, so the GPU pass draws both and the per-subject chain is
//    replaced by one call to `ResolvePlacementCurve`. Adding a curve subject now costs nothing here.

void RecordViewportGridOverlay(OverlayGeometry& Overlay,
                               const PlaneExtent& Extent,
                               const SpatialBasis& Basis,
                               const ViewportStanding& View,
                               bool Perspective,
                               const EditorPanelConfiguration& Configuration)
{
    if (Configuration.Lattice == PanelLatticePresentation::None)
        return;
    const double Step = std::max(Configuration.LatticeCellMetres * static_cast<double>(Configuration.LatticeScale), 1.0);
    const std::uint32_t Subdivisions = std::max(Configuration.Subdivisions, 2u);
    const std::int32_t Count = Perspective ? 40 : 80;

    for (std::int32_t Index = -Count; Index <= Count; ++Index)
    {
        const bool Major = (std::abs(Index) % static_cast<std::int32_t>(Subdivisions)) == 0;
        const std::uint32_t Packed = Major ? PackOverlayColour(0xC4u, 0xC8u, 0xD6u, 56u)
                                           : PackOverlayColour(0xC4u, 0xC8u, 0xD6u, 26u);
        const float Weight = Major ? Configuration.LatticeLineWeight + 0.25f : Configuration.LatticeLineWeight;

        float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f;
        if (ProjectViewportPoint(Basis, View, Perspective, Extent, static_cast<double>(Index) * Step, static_cast<double>(-Count) * Step, X0, Y0) &&
            ProjectViewportPoint(Basis, View, Perspective, Extent, static_cast<double>(Index) * Step, static_cast<double>( Count) * Step, X1, Y1))
            Overlay.AddLine(X0, Y0, X1, Y1, Packed, Weight);

        if (ProjectViewportPoint(Basis, View, Perspective, Extent, static_cast<double>(-Count) * Step, static_cast<double>(Index) * Step, X0, Y0) &&
            ProjectViewportPoint(Basis, View, Perspective, Extent, static_cast<double>( Count) * Step, static_cast<double>(Index) * Step, X1, Y1))
            Overlay.AddLine(X0, Y0, X1, Y1, Packed, Weight);
    }

    float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f;
    if (Configuration.AxisX &&
        ProjectViewportPoint(Basis, View, Perspective, Extent, -Count * Step, 0.0, X0, Y0) &&
        ProjectViewportPoint(Basis, View, Perspective, Extent, Count * Step, 0.0, X1, Y1))
        Overlay.AddLine(X0, Y0, X1, Y1, PackOverlayColour(0xFCu, 0x5Au, 0x5Au, 208u), 1.6f);

    if (Configuration.AxisZ &&
        ProjectViewportPoint(Basis, View, Perspective, Extent, 0.0, -Count * Step, X0, Y0) &&
        ProjectViewportPoint(Basis, View, Perspective, Extent, 0.0, Count * Step, X1, Y1))
        Overlay.AddLine(X0, Y0, X1, Y1, PackOverlayColour(0x5Au, 0x8Bu, 0xFCu, 208u), 1.6f);
}

void RecordViewportGridOverlay(OverlayGeometry& Overlay,
                               const PlaneExtent& Extent,
                               const SketchStructure& Sketch,
                               const ViewportStanding& View,
                               bool Perspective,
                               const EditorPanelConfiguration& Configuration)
{
    const SpatialBasis Basis = Sketch.Declared()
        ? ResolveSketchBasis(Sketch)
        : SpatialBasis{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
    RecordViewportGridOverlay(Overlay, Extent, Basis, View, Perspective, Configuration);
}

void AppendOverlayCircle(OverlayGeometry& Overlay,
                         float CentreX,
                         float CentreY,
                         float Radius,
                         std::uint32_t Packed,
                         float Thickness,
                         std::uint32_t SegmentCount)
{
    for (std::uint32_t Index = 0u; Index < SegmentCount; ++Index)
    {
        const double A0 = (static_cast<double>(Index) / static_cast<double>(SegmentCount)) * 2.0 * ProjectionPi;
        const double A1 = (static_cast<double>(Index + 1u) / static_cast<double>(SegmentCount)) * 2.0 * ProjectionPi;
        Overlay.AddLine(CentreX + static_cast<float>(std::cos(A0) * Radius),
                        CentreY + static_cast<float>(std::sin(A0) * Radius),
                        CentreX + static_cast<float>(std::cos(A1) * Radius),
                        CentreY + static_cast<float>(std::sin(A1) * Radius),
                        Packed, Thickness);
    }
}

void RecordViewportSelectionOverlay(OverlayGeometry& Overlay,
                                    const PlaneExtent& Extent,
                                    const SpatialBasis& Basis,
                                    const ViewportStanding& View,
                                    bool Perspective,
                                    const SketchStructure& Sketch,
                                    const WorkspaceRecordStructure& Records,
                                    const SketchPick& Hovered,
                                    const SketchPick& Selected)
{
    // 📝 The overlay once gated what it drew on record visibility and no longer does -- what is picked is
    //    what is outlined. The parameter is kept so the two overloads and every call site stay one shape.
    static_cast<void>(Records);

    const auto RecordCurve = [&](SketchCurveName Curve, std::uint32_t Packed, float Thickness)
    {
        if (!Curve.Assigned() || Curve.IssuedIndex > Sketch.Curves().size())
            return;
        std::vector<SpatialPoint> Polyline;
        AppendCurvePolyline(Sketch.Curves()[Curve.IssuedIndex - 1u].Geometry, Polyline, 48u);
        for (std::size_t Index = 0u; Index + 1u < Polyline.size(); ++Index)
        {
            float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f;
            if (ProjectSpatialPoint(Basis, View, Perspective, Extent, Polyline[Index], X0, Y0) &&
                ProjectSpatialPoint(Basis, View, Perspective, Extent, Polyline[Index + 1u], X1, Y1))
                Overlay.AddLine(X0, Y0, X1, Y1, Packed, Thickness);
        }
    };

    const auto RecordProfile = [&](ProfileNameInFeature Profile, std::uint32_t Packed, float Thickness)
    {
        if (!Profile.Assigned() || Profile.IssuedIndex > Sketch.Profiles().size())
            return;
        for (const ProfileLoop& Loop : Sketch.Profiles()[Profile.IssuedIndex - 1u].HeldLoops())
            for (const ProfileCurveUse& Use : Loop.Traversal)
                RecordCurve({ Use.TraversedCurve.IssuedIndex }, Packed, Thickness);
    };

    const auto RecordProfileFill = [&](ProfileNameInFeature Profile)
    {
        if (!Profile.Assigned() || Profile.IssuedIndex > Sketch.Profiles().size())
            return;
        const std::uint32_t Fill = PackOverlayColour(0x5Bu, 0x8Cu, 0xFFu, 64u);
        for (const ProfileLoop& Loop : Sketch.Profiles()[Profile.IssuedIndex - 1u].HeldLoops())
        {
            std::vector<SpatialPoint> Perimeter;
            for (const ProfileCurveUse& Use : Loop.Traversal)
            {
                if (!Use.TraversedCurve.Assigned() || Use.TraversedCurve.IssuedIndex > Sketch.Curves().size())
                    continue;
                std::vector<SpatialPoint> Segment;
                AppendCurvePolyline(Sketch.Curves()[Use.TraversedCurve.IssuedIndex - 1u].Geometry, Segment, 24u);
                if (!Use.SameSense)
                    std::reverse(Segment.begin(), Segment.end());
                if (!Perimeter.empty() && !Segment.empty())
                    Segment.erase(Segment.begin());
                Perimeter.insert(Perimeter.end(), Segment.begin(), Segment.end());
            }
            if (Perimeter.size() >= 3u)
            {
                for (std::size_t Index = 1u; Index + 1u < Perimeter.size(); ++Index)
                {
                    float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f, X2 = 0.0f, Y2 = 0.0f;
                    if (ProjectSpatialPoint(Basis, View, Perspective, Extent, Perimeter[0], X0, Y0) &&
                        ProjectSpatialPoint(Basis, View, Perspective, Extent, Perimeter[Index], X1, Y1) &&
                        ProjectSpatialPoint(Basis, View, Perspective, Extent, Perimeter[Index + 1u], X2, Y2))
                        Overlay.AddTriangle(X0, Y0, X1, Y1, X2, Y2, Fill);
                }
            }
        }
    };

    // 🔴 CURVE AND PROFILE HIGHLIGHTING MOVED TO THE WORLD-BACKED OVERLAY and these never ran again.
    //    They are kept, unused, because the compatibility arm above still calls this overload and the
    //    two must stay the same shape; deleting them would fork the pair. Only the point highlights
    //    below are still drawn from here.
    static_cast<void>(RecordCurve);
    static_cast<void>(RecordProfile);
    static_cast<void>(RecordProfileFill);

    const auto RecordPoint = [&](const SketchPick& Subject, std::uint32_t FillColour, float Radius = 5.5f)
    {
        float X = 0.0f;
        float Y = 0.0f;
        if (!ProjectSpatialPoint(Basis, View, Perspective, Extent, Subject.Position, X, Y))
            return;
        Overlay.AddDot(X, Y, FillColour, Radius);
    };

    if (Selected.Standing())
    {
        if (Selected.Subject == SketchPickSubject::Control)
        {
            RecordPoint(Selected, PackOverlayColour(0xA8u, 0x55u, 0xF7u, 255u), 6.5f);
        }
        else if (Selected.Subject == SketchPickSubject::Point)
        {
            RecordPoint(Selected, PackOverlayColour(0x10u, 0xB9u, 0x81u, 255u), 6.5f);
        }
    }

    if (Hovered.Standing())
    {
        if (Hovered.Subject == SketchPickSubject::Control)
            RecordPoint(Hovered, PackOverlayColour(0xFBu, 0xBFu, 0x24u, 255u), 6.0f);
        else if (Hovered.Subject == SketchPickSubject::Point)
            RecordPoint(Hovered, PackOverlayColour(0xFBu, 0xBFu, 0x24u, 255u), 6.0f);
    }
}

void RecordViewportSelectionOverlay(OverlayGeometry& Overlay,
                                    const PlaneExtent& Extent,
                                    const SpatialBasis& Basis,
                                    const ViewportStanding& View,
                                    bool Perspective,
                                    const SketchStructure& Sketch,
                                    const WorkspaceRecordStructure& Records,
                                    const SketchPick& Hovered,
                                    const SketchSelectionSet& Selected)
{
    for (const SketchPick& Pick : Selected.Items)
        RecordViewportSelectionOverlay(Overlay, Extent, Basis, View, Perspective,
                                       Sketch, Records, SketchPick{}, Pick);

    if (Hovered.Standing())
        RecordViewportSelectionOverlay(Overlay, Extent, Basis, View, Perspective,
                                       Sketch, Records, Hovered, SketchPick{});
}

void RecordViewportSelectionOverlay(OverlayGeometry& Overlay,
                                    const PlaneExtent& Extent,
                                    const ResolvedCamera& Camera,
                                    const WorldSketchStructure& Declared,
                                    const WorldPick& Hovered,
                                    const WorldPick& Selected)
{
    const auto RecordPoint = [&](const WorldPick& Subject, std::uint32_t FillColour, float Radius = 5.5f)
    {
        float X = 0.0f;
        float Y = 0.0f;
        if (!ProjectFromCamera(Camera, Extent, Subject.Position, X, Y))
            return;
        Overlay.AddDot(X, Y, FillColour, Radius);
    };

    const auto RecordPoints = [&](WorldCurveName Curve)
    {
        std::vector<WorldPointPlacement> Points;
        if (!ResolveWorldSketchPoints(Declared, Curve, Points))
            return;
        for (const WorldPointPlacement& Pt : Points)
        {
            WorldPick Point = {};
            Point.Subject = WorldPickSubject::Point;
            Point.Point = Pt.Name;
            Point.Curve = Pt.SourceCurve;
            Point.Position = Pt.Position;
            RecordPoint(Point, PackOverlayColour(0x10u, 0xB9u, 0x81u, 255u), 5.5f);
        }
    };

    const auto RecordControls = [&](WorldCurveName Curve)
    {
        std::vector<WorldControlPlacement> Controls;
        if (!ResolveWorldSketchControls(Declared, Curve, Controls))
            return;

        for (std::size_t i = 0u; i < Controls.size(); ++i)
        {
            const WorldControlPlacement& Control = Controls[i];
            WorldPick Point = {};
            Point.Subject = WorldPickSubject::Control;
            Point.Control = Control.Name;
            Point.Curve = Control.SourceCurve;
            Point.Position = Control.Position;
            RecordPoint(Point, PackOverlayColour(0xA8u, 0x55u, 0xF7u, 255u), 5.5f);
        }
    };

    if (Selected.Standing())
    {
        if (Selected.Subject == WorldPickSubject::Curve)
        {
            RecordPoints(Selected.Curve);
            RecordControls(Selected.Curve);
        }
        else if (Selected.Subject == WorldPickSubject::Loop)
        {
            const DeclaredWorldLoop* Loop = Declared.Resolve(Selected.Loop);
            if (Loop != nullptr)
                for (const WorldCurveUse& Use : Loop->Traversal)
                {
                    RecordPoints(Use.TraversedCurve);
                    RecordControls(Use.TraversedCurve);
                }
        }
        else if (Selected.Subject == WorldPickSubject::Point)
        {
            RecordPoints(Selected.Curve);
            RecordControls(Selected.Curve);
            RecordPoint(Selected, PackOverlayColour(255u, 255u, 255u, 255u), 7.0f);
        }
        else if (Selected.Subject == WorldPickSubject::Control)
        {
            RecordPoints(Selected.Curve);
            RecordControls(Selected.Curve);
            RecordPoint(Selected, PackOverlayColour(255u, 255u, 255u, 255u), 7.0f);
        }
    }

    if (Hovered.Subject == WorldPickSubject::Point)
        RecordPoint(Hovered, PackOverlayColour(0xFBu, 0xBFu, 0x24u, 255u), 6.0f);
    else if (Hovered.Subject == WorldPickSubject::Control)
        RecordPoint(Hovered, PackOverlayColour(0xFBu, 0xBFu, 0x24u, 255u), 6.0f);
}

void RecordViewportSelectionOverlay(OverlayGeometry& Overlay,
                                    const PlaneExtent& Extent,
                                    const ResolvedCamera& Camera,
                                    const WorldSketchStructure& Declared,
                                    const WorldPick& Hovered,
                                    const WorldSelectionSet& Selected)
{
    for (const WorldPick& Pick : Selected.Items)
        RecordViewportSelectionOverlay(Overlay, Extent, Camera, Declared, WorldPick{}, Pick);

    if (Hovered.Standing())
        RecordViewportSelectionOverlay(Overlay, Extent, Camera, Declared, Hovered, WorldPick{});
}

void RecordViewportGizmo(OverlayGeometry& Overlay,
                         const PlaneExtent& Extent,
                         const SpatialBasis& Basis,
                         const ViewportStanding& View,
                         bool Perspective,
                         const SketchPick& Selected,
                         GizmoHandle HoveredHandle,
                         const TransformSession& Transform,
                         TransformManner Manner)
{
    if (!Selected.Standing())
        return;

    GizmoScreenBasis Screen = {};
    if (!ResolveGizmoScreenBasis(Basis, View, Perspective, Extent, Selected.Position, Screen))
        return;

    // 🔴 EVERY MAGNITUDE BELOW IS A PIXEL COUNT FROM `GizmoMeasure`, CONVERTED TO WORLD HERE.
    const auto Px = [&](double Pixels) { return GizmoWorld(Screen, Pixels); };

    const ViewFrame Frame = ResolveViewportFrame(Basis, View, Perspective);
    const SpatialPoint Pivot = Selected.Position;
    const SpatialDirection AxisX = Basis.Along;
    const SpatialDirection AxisY = Basis.Normal;
    const SpatialDirection AxisZ = Basis.Across;
    const std::uint32_t XPacked = PackOverlayColour(0xE0u, 0x14u, 0x14u, 255u);
    const std::uint32_t YPacked = PackOverlayColour(0x12u, 0xD4u, 0x0Au, 255u);
    const std::uint32_t ZPacked = PackOverlayColour(0x15u, 0x60u, 0xE0u, 255u);
    const std::uint32_t Cyan = PackOverlayColour(0x1Fu, 0xC7u, 0xC7u, 255u);
    const std::uint32_t Magenta = PackOverlayColour(0xC8u, 0x1Eu, 0xC8u, 255u);
    const std::uint32_t Yellow = PackOverlayColour(0xE0u, 0xCDu, 0x12u, 255u);
    const std::uint32_t White = PackOverlayColour(0xFFu, 0xFFu, 0xFFu, 255u);
    const std::uint32_t Highlight = PackOverlayColour(0xFBu, 0xBFu, 0x24u, 255u);
    const std::uint32_t Guide = PackOverlayColour(0xFFu, 0xFFu, 0xFFu, 160u);

    const auto Project = [&](const SpatialPoint& P, float& X, float& Y) -> bool
    {
        return ProjectSpatialPoint(Basis, View, Perspective, Extent, P, X, Y);
    };
    const auto AddWorldLine = [&](const SpatialPoint& A, const SpatialPoint& B, std::uint32_t Packed, float Thickness)
    {
        float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f;
        if (Project(A, X0, Y0) && Project(B, X1, Y1))
            Overlay.AddLine(X0, Y0, X1, Y1, Packed, Thickness);
    };
    const auto AddWorldTriangle = [&](const SpatialPoint& A, const SpatialPoint& B, const SpatialPoint& C, std::uint32_t Packed)
    {
        float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f, X2 = 0.0f, Y2 = 0.0f;
        if (Project(A, X0, Y0) && Project(B, X1, Y1) && Project(C, X2, Y2))
            Overlay.AddTriangle(X0, Y0, X1, Y1, X2, Y2, Packed);
    };

    const auto AddCone = [&](const SpatialDirection& Axis,
                             const SpatialDirection& U,
                             const SpatialDirection& V,
                             std::uint32_t Packed)
    {
        const SpatialPoint Tip = Added(Pivot, Scaled(Axis, Px(GizmoMeasure::AxisEnd)));
        const SpatialPoint Base = Added(Pivot, Scaled(Axis, Px(GizmoMeasure::AxisEnd - GizmoMeasure::ConeLength)));
        const double Radius = Px(GizmoMeasure::ConeRadius);
        const SpatialPoint Centre = Base;

        for (std::uint32_t Segment = 0u; Segment < GizmoMeasure::ConeSegments; ++Segment)
        {
            const double A0 = (static_cast<double>(Segment) / static_cast<double>(GizmoMeasure::ConeSegments)) * 2.0 * ProjectionPi;
            const double A1 = (static_cast<double>(Segment + 1u) / static_cast<double>(GizmoMeasure::ConeSegments)) * 2.0 * ProjectionPi;
            const SpatialPoint P0 = Added(Centre,
                Added(Scaled(U, std::cos(A0) * Radius), Scaled(V, std::sin(A0) * Radius)));
            const SpatialPoint P1 = Added(Centre,
                Added(Scaled(U, std::cos(A1) * Radius), Scaled(V, std::sin(A1) * Radius)));
            AddWorldTriangle(Tip, P0, P1, Packed);
            AddWorldTriangle(Centre, P1, P0, Packed);
        }
    };

    const auto AddCylinder = [&](const SpatialDirection& Axis,
                                 const SpatialDirection& U,
                                 const SpatialDirection& V,
                                 std::uint32_t Packed)
    {
        const SpatialPoint Centre = Added(Pivot, Scaled(Axis, Px(GizmoMeasure::ScaleCentre)));
        const SpatialPoint A = Added(Centre, Scaled(Axis, -Px(GizmoMeasure::ScaleLength * 0.5)));
        const SpatialPoint B = Added(Centre, Scaled(Axis,  Px(GizmoMeasure::ScaleLength * 0.5)));
        const double Radius = Px(GizmoMeasure::ScaleRadius);

        for (std::uint32_t Segment = 0u; Segment < GizmoMeasure::CylinderSegments; ++Segment)
        {
            const double T0 = (static_cast<double>(Segment) / static_cast<double>(GizmoMeasure::CylinderSegments)) * 2.0 * ProjectionPi;
            const double T1 = (static_cast<double>(Segment + 1u) / static_cast<double>(GizmoMeasure::CylinderSegments)) * 2.0 * ProjectionPi;
            const SpatialDirection R0 = Added(Scaled(U, std::cos(T0) * Radius), Scaled(V, std::sin(T0) * Radius));
            const SpatialDirection R1 = Added(Scaled(U, std::cos(T1) * Radius), Scaled(V, std::sin(T1) * Radius));
            const SpatialPoint A0 = Added(A, R0);
            const SpatialPoint A1 = Added(A, R1);
            const SpatialPoint B0 = Added(B, R0);
            const SpatialPoint B1 = Added(B, R1);
            AddWorldTriangle(A0, B0, B1, Packed);
            AddWorldTriangle(A0, B1, A1, Packed);
            AddWorldTriangle(A, A1, A0, Packed);
            AddWorldTriangle(B, B0, B1, Packed);
        }
    };

    const auto AddPlaneHandle = [&](const SpatialDirection& U,
                                    const SpatialDirection& V,
                                    std::uint32_t FillPacked,
                                    std::uint32_t EdgePacked)
    {
        const double Half = Px(GizmoMeasure::PlaneHalf);
        const SpatialPoint Centre = Added(Pivot,
            Added(Scaled(U, Px(GizmoMeasure::PlaneCentre)),
                  Scaled(V, Px(GizmoMeasure::PlaneCentre))));

        const SpatialPoint P0 = Added(Centre, Added(Scaled(U, -Half), Scaled(V, -Half)));
        const SpatialPoint P1 = Added(Centre, Added(Scaled(U,  Half), Scaled(V, -Half)));
        const SpatialPoint P2 = Added(Centre, Added(Scaled(U,  Half), Scaled(V,  Half)));
        const SpatialPoint P3 = Added(Centre, Added(Scaled(U, -Half), Scaled(V,  Half)));
        AddWorldTriangle(P0, P1, P2, FillPacked);
        AddWorldTriangle(P0, P2, P3, FillPacked);

        const SpatialPoint Outer = Added(Centre, Added(Scaled(U, Half), Scaled(V, Half)));
        const SpatialPoint BackU = Added(Outer, Scaled(U, -Half * 2.0));
        const SpatialPoint BackV = Added(Outer, Scaled(V, -Half * 2.0));
        AddWorldLine(BackU, Outer, EdgePacked, 1.0f);
        AddWorldLine(Outer, BackV, EdgePacked, 1.0f);
    };

    const auto AddArcBar = [&](const SpatialDirection& U,
                               const SpatialDirection& V,
                               std::uint32_t Packed)
    {
        const double Inner = Px(GizmoMeasure::RotateRadius - GizmoMeasure::RotateHalfWidth);
        const double Outer = Px(GizmoMeasure::RotateRadius + GizmoMeasure::RotateHalfWidth);
        const double Start = ProjectionPi * 0.25 - GizmoMeasure::RotateSweepRadians * 0.5;
        for (std::uint32_t Segment = 0u; Segment < GizmoMeasure::RotateSegments; ++Segment)
        {
            const double A0 = Start + GizmoMeasure::RotateSweepRadians
                                    * static_cast<double>(Segment)
                                    / static_cast<double>(GizmoMeasure::RotateSegments);
            const double A1 = Start + GizmoMeasure::RotateSweepRadians
                                    * static_cast<double>(Segment + 1u)
                                    / static_cast<double>(GizmoMeasure::RotateSegments);
            const SpatialPoint I0 = Added(Pivot,
                Added(Scaled(U, std::cos(A0) * Inner), Scaled(V, std::sin(A0) * Inner)));
            const SpatialPoint O0 = Added(Pivot,
                Added(Scaled(U, std::cos(A0) * Outer), Scaled(V, std::sin(A0) * Outer)));
            const SpatialPoint I1 = Added(Pivot,
                Added(Scaled(U, std::cos(A1) * Inner), Scaled(V, std::sin(A1) * Inner)));
            const SpatialPoint O1 = Added(Pivot,
                Added(Scaled(U, std::cos(A1) * Outer), Scaled(V, std::sin(A1) * Outer)));
            AddWorldTriangle(I0, O0, I1, Packed);
            AddWorldTriangle(O0, O1, I1, Packed);
        }
    };

    const auto AddBillboardTorus = [&](std::uint32_t Packed)
    {
        const double Major = Px(GizmoMeasure::CentreRingRadius);
        const double Minor = Px(GizmoMeasure::CentreRingTube);

        const auto TorusPoint = [&](std::uint32_t MajorIndex, std::uint32_t MinorIndex)
        {
            const double UAngle = (static_cast<double>(MajorIndex) / static_cast<double>(GizmoMeasure::CentreRingTubularSegments)) * 2.0 * ProjectionPi;
            const double VAngle = (static_cast<double>(MinorIndex) / static_cast<double>(GizmoMeasure::CentreRingRadialSegments)) * 2.0 * ProjectionPi;
            const SpatialDirection Radial = Normalize(Added(Scaled(Frame.Right, std::cos(UAngle)),
                                                           Scaled(Frame.Up, std::sin(UAngle))));
            const SpatialPoint RingCentre = Added(Pivot, Scaled(Radial, Major));
            return Added(RingCentre,
                         Added(Scaled(Radial, std::cos(VAngle) * Minor),
                               Scaled(Frame.Forward, std::sin(VAngle) * Minor)));
        };

        for (std::uint32_t MajorIndex = 0u; MajorIndex < GizmoMeasure::CentreRingTubularSegments; ++MajorIndex)
            for (std::uint32_t MinorIndex = 0u; MinorIndex < GizmoMeasure::CentreRingRadialSegments; ++MinorIndex)
            {
                const std::uint32_t NextMajor = (MajorIndex + 1u) % GizmoMeasure::CentreRingTubularSegments;
                const std::uint32_t NextMinor = (MinorIndex + 1u) % GizmoMeasure::CentreRingRadialSegments;
                const SpatialPoint P00 = TorusPoint(MajorIndex, MinorIndex);
                const SpatialPoint P10 = TorusPoint(NextMajor, MinorIndex);
                const SpatialPoint P11 = TorusPoint(NextMajor, NextMinor);
                const SpatialPoint P01 = TorusPoint(MajorIndex, NextMinor);
                AddWorldTriangle(P00, P10, P11, Packed);
                AddWorldTriangle(P00, P11, P01, Packed);
            }
    };

    const TransformManner ActiveManner = Transform.Engaged() ? Transform.Manner() : Manner;
    const bool DrawMove = ActiveManner == TransformManner::Move;
    const bool DrawRotate = ActiveManner == TransformManner::Rotate;
    const bool DrawScale = ActiveManner == TransformManner::Scale;
    const auto Visible = [](float X, float Y)
    {
        return X * X + Y * Y > 0.01f;
    };
    const bool ShowX = Visible(Screen.AlongX, Screen.AlongY);
    const bool ShowY = Visible(Screen.NormalX, Screen.NormalY);
    const bool ShowZ = Visible(Screen.AcrossX, Screen.AcrossY);
    const bool ShowYZ = ShowY && ShowZ;
    const bool ShowXZ = ShowX && ShowZ;
    const bool ShowXY = ShowX && ShowY;

    if (DrawMove)
    {
        if (ShowYZ)
            AddPlaneHandle(AxisY, AxisZ,
                           HoveredHandle == GizmoHandle::MoveYZ ? Highlight : PackOverlayColour(0x1Fu, 0xC7u, 0xC7u, 71u),
                           HoveredHandle == GizmoHandle::MoveYZ ? Highlight : Cyan);
        if (ShowXZ)
            AddPlaneHandle(AxisX, AxisZ,
                           (HoveredHandle == GizmoHandle::MoveFree || HoveredHandle == GizmoHandle::MoveXZ) ? Highlight : PackOverlayColour(0xC8u, 0x1Eu, 0xC8u, 71u),
                           (HoveredHandle == GizmoHandle::MoveFree || HoveredHandle == GizmoHandle::MoveXZ) ? Highlight : Magenta);
        if (ShowXY)
            AddPlaneHandle(AxisX, AxisY,
                           HoveredHandle == GizmoHandle::MoveXY ? Highlight : PackOverlayColour(0xE0u, 0xCDu, 0x12u, 71u),
                           HoveredHandle == GizmoHandle::MoveXY ? Highlight : Yellow);
    }

    if (DrawRotate)
    {
        if (ShowYZ) AddArcBar(AxisY, AxisZ, HoveredHandle == GizmoHandle::Rotate ? Highlight : XPacked);
        if (ShowXZ) AddArcBar(AxisX, AxisZ, HoveredHandle == GizmoHandle::Rotate ? Highlight : YPacked);
        if (ShowXY) AddArcBar(AxisX, AxisY, HoveredHandle == GizmoHandle::Rotate ? Highlight : ZPacked);
    }

    if (DrawScale)
    {
        if (ShowX) AddCylinder(AxisX, AxisY, AxisZ, HoveredHandle == GizmoHandle::ScaleX ? Highlight : XPacked);
        if (ShowY) AddCylinder(AxisY, AxisX, AxisZ, HoveredHandle == GizmoHandle::ScaleY ? Highlight : YPacked);
        if (ShowZ) AddCylinder(AxisZ, AxisX, AxisY, HoveredHandle == GizmoHandle::ScaleZ ? Highlight : ZPacked);
    }

    if (DrawMove)
    {
        if (ShowX) AddCone(AxisX, AxisY, AxisZ, HoveredHandle == GizmoHandle::MoveX ? Highlight : XPacked);
        if (ShowY) AddCone(AxisY, AxisX, AxisZ, HoveredHandle == GizmoHandle::MoveY ? Highlight : YPacked);
        if (ShowZ) AddCone(AxisZ, AxisX, AxisY, HoveredHandle == GizmoHandle::MoveZ ? Highlight : ZPacked);
    }

    AddBillboardTorus((HoveredHandle == GizmoHandle::MoveFree
                    || HoveredHandle == GizmoHandle::ScaleFree
                    || HoveredHandle == GizmoHandle::Rotate)
                        ? Highlight
                        : White);

    if (Transform.Engaged())
    {
        SpatialDirection GuideAxis = {};
        bool Guided = false;
        if (Transform.Restriction() == TransformRestriction::AxisX)
        {
            GuideAxis = AxisX;
            Guided = true;
        }
        else if (Transform.Restriction() == TransformRestriction::AxisY)
        {
            GuideAxis = AxisY;
            Guided = true;
        }
        else if (Transform.Restriction() == TransformRestriction::AxisZ)
        {
            GuideAxis = AxisZ;
            Guided = true;
        }
        else if (Transform.Restriction() == TransformRestriction::Curve)
        {
            GuideAxis = Transform.CurveDirection;
            Guided = true;
        }
        if (Guided)
            AddWorldLine(Added(Pivot, Scaled(GuideAxis, -1000.0)), Added(Pivot, Scaled(GuideAxis, 1000.0)), Guide, 1.5f);
    }
}

void RecordViewportGizmo(OverlayGeometry& Overlay,
                         const PlaneExtent& Extent,
                         const ResolvedCamera& Camera,
                         const WorldPick& Selected,
                         GizmoHandle HoveredHandle,
                         const WorldSketchTransformSession& Transform,
                         TransformManner Manner)
{
    if (!Selected.Standing())
        return;

    GizmoScreenBasis Screen = {};
    if (!ResolveGizmoScreenBasis(Camera, Extent, Selected.Position, Screen))
        return;

    const auto Px = [&](double Pixels) { return GizmoWorld(Screen, Pixels); };

    const ViewFrame& Frame = Camera.Frame;
    const SpatialPoint Pivot = Selected.Position;
    const SpatialDirection AxisX = Camera.Basis.Along;
    const SpatialDirection AxisY = Camera.Basis.Normal;
    const SpatialDirection AxisZ = Camera.Basis.Across;
    const std::uint32_t XPacked = PackOverlayColour(0xE0u, 0x14u, 0x14u, 255u);
    const std::uint32_t YPacked = PackOverlayColour(0x12u, 0xD4u, 0x0Au, 255u);
    const std::uint32_t ZPacked = PackOverlayColour(0x15u, 0x60u, 0xE0u, 255u);
    const std::uint32_t Cyan = PackOverlayColour(0x1Fu, 0xC7u, 0xC7u, 255u);
    const std::uint32_t Magenta = PackOverlayColour(0xC8u, 0x1Eu, 0xC8u, 255u);
    const std::uint32_t Yellow = PackOverlayColour(0xE0u, 0xCDu, 0x12u, 255u);
    const std::uint32_t White = PackOverlayColour(0xFFu, 0xFFu, 0xFFu, 255u);
    const std::uint32_t Highlight = PackOverlayColour(0xFBu, 0xBFu, 0x24u, 255u);
    const std::uint32_t Guide = PackOverlayColour(0xFFu, 0xFFu, 0xFFu, 160u);

    const auto Project = [&](const SpatialPoint& P, float& X, float& Y) -> bool
    {
        return ProjectFromCamera(Camera, Extent, P, X, Y);
    };
    const auto AddWorldLine = [&](const SpatialPoint& A, const SpatialPoint& B, std::uint32_t Packed, float Thickness)
    {
        float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f;
        if (Project(A, X0, Y0) && Project(B, X1, Y1))
            Overlay.AddLine(X0, Y0, X1, Y1, Packed, Thickness);
    };
    const auto AddWorldTriangle = [&](const SpatialPoint& A, const SpatialPoint& B, const SpatialPoint& C, std::uint32_t Packed)
    {
        float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f, X2 = 0.0f, Y2 = 0.0f;
        if (Project(A, X0, Y0) && Project(B, X1, Y1) && Project(C, X2, Y2))
            Overlay.AddTriangle(X0, Y0, X1, Y1, X2, Y2, Packed);
    };

    const auto AddCone = [&](const SpatialDirection& Axis,
                             const SpatialDirection& U,
                             const SpatialDirection& V,
                             std::uint32_t Packed)
    {
        const SpatialPoint Tip = Added(Pivot, Scaled(Axis, Px(GizmoMeasure::AxisEnd)));
        const SpatialPoint Base = Added(Pivot, Scaled(Axis, Px(GizmoMeasure::AxisEnd - GizmoMeasure::ConeLength)));
        const double Radius = Px(GizmoMeasure::ConeRadius);
        const SpatialPoint Centre = Base;

        for (std::uint32_t Segment = 0u; Segment < GizmoMeasure::ConeSegments; ++Segment)
        {
            const double A0 = (static_cast<double>(Segment) / static_cast<double>(GizmoMeasure::ConeSegments)) * 2.0 * ProjectionPi;
            const double A1 = (static_cast<double>(Segment + 1u) / static_cast<double>(GizmoMeasure::ConeSegments)) * 2.0 * ProjectionPi;
            const SpatialPoint P0 = Added(Centre,
                Added(Scaled(U, std::cos(A0) * Radius), Scaled(V, std::sin(A0) * Radius)));
            const SpatialPoint P1 = Added(Centre,
                Added(Scaled(U, std::cos(A1) * Radius), Scaled(V, std::sin(A1) * Radius)));
            AddWorldTriangle(Tip, P0, P1, Packed);
            AddWorldTriangle(Centre, P1, P0, Packed);
        }
    };

    const auto AddCylinder = [&](const SpatialDirection& Axis,
                                 const SpatialDirection& U,
                                 const SpatialDirection& V,
                                 std::uint32_t Packed)
    {
        const SpatialPoint Centre = Added(Pivot, Scaled(Axis, Px(GizmoMeasure::ScaleCentre)));
        const SpatialPoint A = Added(Centre, Scaled(Axis, -Px(GizmoMeasure::ScaleLength * 0.5)));
        const SpatialPoint B = Added(Centre, Scaled(Axis,  Px(GizmoMeasure::ScaleLength * 0.5)));
        const double Radius = Px(GizmoMeasure::ScaleRadius);

        for (std::uint32_t Segment = 0u; Segment < GizmoMeasure::CylinderSegments; ++Segment)
        {
            const double T0 = (static_cast<double>(Segment) / static_cast<double>(GizmoMeasure::CylinderSegments)) * 2.0 * ProjectionPi;
            const double T1 = (static_cast<double>(Segment + 1u) / static_cast<double>(GizmoMeasure::CylinderSegments)) * 2.0 * ProjectionPi;
            const SpatialDirection R0 = Added(Scaled(U, std::cos(T0) * Radius), Scaled(V, std::sin(T0) * Radius));
            const SpatialDirection R1 = Added(Scaled(U, std::cos(T1) * Radius), Scaled(V, std::sin(T1) * Radius));
            const SpatialPoint A0 = Added(A, R0);
            const SpatialPoint A1 = Added(A, R1);
            const SpatialPoint B0 = Added(B, R0);
            const SpatialPoint B1 = Added(B, R1);
            AddWorldTriangle(A0, B0, B1, Packed);
            AddWorldTriangle(A0, B1, A1, Packed);
            AddWorldTriangle(A, A1, A0, Packed);
            AddWorldTriangle(B, B0, B1, Packed);
        }
    };

    const auto AddPlaneHandle = [&](const SpatialDirection& U,
                                    const SpatialDirection& V,
                                    std::uint32_t FillPacked,
                                    std::uint32_t EdgePacked)
    {
        const double Half = Px(GizmoMeasure::PlaneHalf);
        const SpatialPoint Centre = Added(Pivot,
            Added(Scaled(U, Px(GizmoMeasure::PlaneCentre)),
                  Scaled(V, Px(GizmoMeasure::PlaneCentre))));

        const SpatialPoint P0 = Added(Centre, Added(Scaled(U, -Half), Scaled(V, -Half)));
        const SpatialPoint P1 = Added(Centre, Added(Scaled(U,  Half), Scaled(V, -Half)));
        const SpatialPoint P2 = Added(Centre, Added(Scaled(U,  Half), Scaled(V,  Half)));
        const SpatialPoint P3 = Added(Centre, Added(Scaled(U, -Half), Scaled(V,  Half)));
        AddWorldTriangle(P0, P1, P2, FillPacked);
        AddWorldTriangle(P0, P2, P3, FillPacked);

        const SpatialPoint Outer = Added(Centre, Added(Scaled(U, Half), Scaled(V, Half)));
        const SpatialPoint BackU = Added(Outer, Scaled(U, -Half * 2.0));
        const SpatialPoint BackV = Added(Outer, Scaled(V, -Half * 2.0));
        AddWorldLine(BackU, Outer, EdgePacked, 1.0f);
        AddWorldLine(Outer, BackV, EdgePacked, 1.0f);
    };

    const auto AddArcBar = [&](const SpatialDirection& U,
                               const SpatialDirection& V,
                               std::uint32_t Packed)
    {
        const double Inner = Px(GizmoMeasure::RotateRadius - GizmoMeasure::RotateHalfWidth);
        const double Outer = Px(GizmoMeasure::RotateRadius + GizmoMeasure::RotateHalfWidth);
        const double Start = ProjectionPi * 0.25 - GizmoMeasure::RotateSweepRadians * 0.5;
        for (std::uint32_t Segment = 0u; Segment < GizmoMeasure::RotateSegments; ++Segment)
        {
            const double A0 = Start + GizmoMeasure::RotateSweepRadians
                                    * static_cast<double>(Segment)
                                    / static_cast<double>(GizmoMeasure::RotateSegments);
            const double A1 = Start + GizmoMeasure::RotateSweepRadians
                                    * static_cast<double>(Segment + 1u)
                                    / static_cast<double>(GizmoMeasure::RotateSegments);
            const SpatialPoint I0 = Added(Pivot,
                Added(Scaled(U, std::cos(A0) * Inner), Scaled(V, std::sin(A0) * Inner)));
            const SpatialPoint O0 = Added(Pivot,
                Added(Scaled(U, std::cos(A0) * Outer), Scaled(V, std::sin(A0) * Outer)));
            const SpatialPoint I1 = Added(Pivot,
                Added(Scaled(U, std::cos(A1) * Inner), Scaled(V, std::sin(A1) * Inner)));
            const SpatialPoint O1 = Added(Pivot,
                Added(Scaled(U, std::cos(A1) * Outer), Scaled(V, std::sin(A1) * Outer)));
            AddWorldTriangle(I0, O0, I1, Packed);
            AddWorldTriangle(O0, O1, I1, Packed);
        }
    };

    const auto AddBillboardTorus = [&](std::uint32_t Packed)
    {
        const double Major = Px(GizmoMeasure::CentreRingRadius);
        const double Minor = Px(GizmoMeasure::CentreRingTube);

        const auto TorusPoint = [&](std::uint32_t MajorIndex, std::uint32_t MinorIndex)
        {
            const double UAngle = (static_cast<double>(MajorIndex) / static_cast<double>(GizmoMeasure::CentreRingTubularSegments)) * 2.0 * ProjectionPi;
            const double VAngle = (static_cast<double>(MinorIndex) / static_cast<double>(GizmoMeasure::CentreRingRadialSegments)) * 2.0 * ProjectionPi;
            const SpatialDirection Radial = Normalize(Added(Scaled(Frame.Right, std::cos(UAngle)),
                                                           Scaled(Frame.Up, std::sin(UAngle))));
            const SpatialPoint RingCentre = Added(Pivot, Scaled(Radial, Major));
            return Added(RingCentre,
                         Added(Scaled(Radial, std::cos(VAngle) * Minor),
                               Scaled(Frame.Forward, std::sin(VAngle) * Minor)));
        };

        for (std::uint32_t MajorIndex = 0u; MajorIndex < GizmoMeasure::CentreRingTubularSegments; ++MajorIndex)
            for (std::uint32_t MinorIndex = 0u; MinorIndex < GizmoMeasure::CentreRingRadialSegments; ++MinorIndex)
            {
                const std::uint32_t NextMajor = (MajorIndex + 1u) % GizmoMeasure::CentreRingTubularSegments;
                const std::uint32_t NextMinor = (MinorIndex + 1u) % GizmoMeasure::CentreRingRadialSegments;
                const SpatialPoint P00 = TorusPoint(MajorIndex, MinorIndex);
                const SpatialPoint P10 = TorusPoint(NextMajor, MinorIndex);
                const SpatialPoint P11 = TorusPoint(NextMajor, NextMinor);
                const SpatialPoint P01 = TorusPoint(MajorIndex, NextMinor);
                AddWorldTriangle(P00, P10, P11, Packed);
                AddWorldTriangle(P00, P11, P01, Packed);
            }
    };

    // 🔴 The active transform family is the only family drawn. Idle selection shows Move as the
    //    neutral default; a live G/R/S session selects exactly one corresponding manipulator.
    const TransformManner ActiveManner = Transform.Engaged() ? Transform.Manner() : Manner;
    const bool DrawMove = ActiveManner == TransformManner::Move;
    const bool DrawRotate = ActiveManner == TransformManner::Rotate;
    const bool DrawScale = ActiveManner == TransformManner::Scale;
    const auto Visible = [](float X, float Y)
    {
        return X * X + Y * Y > 0.01f;
    };
    const bool ShowX = Visible(Screen.AlongX, Screen.AlongY);
    const bool ShowY = Visible(Screen.NormalX, Screen.NormalY);
    const bool ShowZ = Visible(Screen.AcrossX, Screen.AcrossY);
    const bool ShowYZ = ShowY && ShowZ;
    const bool ShowXZ = ShowX && ShowZ;
    const bool ShowXY = ShowX && ShowY;

    if (DrawMove)
    {
        if (ShowYZ)
            AddPlaneHandle(AxisY, AxisZ,
                           HoveredHandle == GizmoHandle::MoveYZ ? Highlight : PackOverlayColour(0x1Fu, 0xC7u, 0xC7u, 71u),
                           HoveredHandle == GizmoHandle::MoveYZ ? Highlight : Cyan);
        if (ShowXZ)
            AddPlaneHandle(AxisX, AxisZ,
                           (HoveredHandle == GizmoHandle::MoveFree || HoveredHandle == GizmoHandle::MoveXZ) ? Highlight : PackOverlayColour(0xC8u, 0x1Eu, 0xC8u, 71u),
                           (HoveredHandle == GizmoHandle::MoveFree || HoveredHandle == GizmoHandle::MoveXZ) ? Highlight : Magenta);
        if (ShowXY)
            AddPlaneHandle(AxisX, AxisY,
                           HoveredHandle == GizmoHandle::MoveXY ? Highlight : PackOverlayColour(0xE0u, 0xCDu, 0x12u, 71u),
                           HoveredHandle == GizmoHandle::MoveXY ? Highlight : Yellow);
    }

    if (DrawRotate)
    {
        if (ShowYZ) AddArcBar(AxisY, AxisZ, HoveredHandle == GizmoHandle::Rotate ? Highlight : XPacked);
        if (ShowXZ) AddArcBar(AxisX, AxisZ, HoveredHandle == GizmoHandle::Rotate ? Highlight : YPacked);
        if (ShowXY) AddArcBar(AxisX, AxisY, HoveredHandle == GizmoHandle::Rotate ? Highlight : ZPacked);
    }

    if (DrawScale)
    {
        if (ShowX) AddCylinder(AxisX, AxisY, AxisZ, HoveredHandle == GizmoHandle::ScaleX ? Highlight : XPacked);
        if (ShowY) AddCylinder(AxisY, AxisX, AxisZ, HoveredHandle == GizmoHandle::ScaleY ? Highlight : YPacked);
        if (ShowZ) AddCylinder(AxisZ, AxisX, AxisY, HoveredHandle == GizmoHandle::ScaleZ ? Highlight : ZPacked);
    }

    if (DrawMove)
    {
        if (ShowX) AddCone(AxisX, AxisY, AxisZ, HoveredHandle == GizmoHandle::MoveX ? Highlight : XPacked);
        if (ShowY) AddCone(AxisY, AxisX, AxisZ, HoveredHandle == GizmoHandle::MoveY ? Highlight : YPacked);
        if (ShowZ) AddCone(AxisZ, AxisX, AxisY, HoveredHandle == GizmoHandle::MoveZ ? Highlight : ZPacked);
    }

    AddBillboardTorus((HoveredHandle == GizmoHandle::MoveFree
                    || HoveredHandle == GizmoHandle::ScaleFree
                    || HoveredHandle == GizmoHandle::Rotate)
                        ? Highlight
                        : White);

    if (Transform.Engaged())
    {
        SpatialDirection GuideAxis = {};
        bool Guided = false;
        if (Transform.Restriction() == TransformRestriction::AxisX)
        {
            GuideAxis = AxisX;
            Guided = true;
        }
        else if (Transform.Restriction() == TransformRestriction::AxisY)
        {
            GuideAxis = AxisY;
            Guided = true;
        }
        else if (Transform.Restriction() == TransformRestriction::AxisZ)
        {
            GuideAxis = AxisZ;
            Guided = true;
        }
        else if (Transform.Restriction() == TransformRestriction::Curve)
        {
            GuideAxis = Transform.AxisDirection;
            Guided = true;
        }
        if (Guided)
            AddWorldLine(Added(Pivot, Scaled(GuideAxis, -1000.0)), Added(Pivot, Scaled(GuideAxis, 1000.0)), Guide, 1.5f);
    }
}

void RecordViewportTransformReadout(RecordingSurface& Surface,
                                    const PlaneExtent& Extent,
                                    const TransformSession& Transform)
{
    if (!Transform.Engaged())
        return;

    char Detail[160] = {};
    char Command[64] = {};
    FormatTransformCommand(Transform.Standing, Command, sizeof(Command));

    if (Transform.Manner() == TransformManner::Rotate)
        std::snprintf(Detail, sizeof(Detail), "%s • %.1f° • %s",
                      Command,
                      Transform.PreviewValue,
                      Transform.SlideAlongCurve() ? "curve slide" : TransformMannerText(Transform.Manner()));
    else if (Transform.Manner() == TransformManner::Scale)
        std::snprintf(Detail, sizeof(Detail), "%s • %.3fx • %s",
                      Command,
                      Transform.PreviewValue,
                      TransformMannerText(Transform.Manner()));
    else
        std::snprintf(Detail, sizeof(Detail), "%s • %.2f • %s",
                      Command,
                      Transform.PreviewValue,
                      Transform.SlideAlongCurve() ? "curve slide" : TransformMannerText(Transform.Manner()));

    const float Width = Surface.MeasureRun(Detail, 11.0f, 0.0f);
    Surface.TextRun(Extent.MinimumX + (Extent.Width() - Width) * 0.5f,
                    Extent.MinimumY + 42.0f,
                    Covering(0xE5E7EBu),
                    Detail, 11.0f, 0.0f, true);
}

}   // namespace Slate
