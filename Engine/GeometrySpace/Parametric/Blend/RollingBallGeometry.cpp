/*====================================================================================================================================
                                                    ROLLINGBALLGEOMETRY.CPP
====================================================================================================================================*/
// 🧩 Rolling-ball geometry of one edge: spine = offset ∩ offset, contact curves, exact blend carrier, conic sections

#include "Engine/GeometrySpace/Parametric/Blend/RollingBallGeometry.h"

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"

#include "Engine/GeometrySpace/Parametric/Containment/FaceDomain.h"
#include "Engine/GeometrySpace/Parametric/Intersection/SurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IntersectionCurve.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <numbers>
#include <set>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{
    //--------------------------------------------------------------------------------------------------------------------
    //                                                CONSTANTS
    //--------------------------------------------------------------------------------------------------------------------

    constexpr double ResidualAllowance = 1.0e-9;                                             // [len/len] - Relative to max(1, size)
    constexpr double FoldMarginFloor   = 0.02;                                               // [-]       - min(1 − d·κ) a NURBS offset must keep
    constexpr double SupportExtension  = 0.25;                                               // [-]       - NURBS support growth per end (× domain) on the ball-law path
    constexpr int    ResidualSamples   = 24;                                                 // [-]       - Sections sampled per sheet

    //--------------------------------------------------------------------------------------------------------------------
    //                                                HELPERS
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] double OrientationSign(Orientation Sense) noexcept
    {
        return Sense == Orientation::Forward ? 1.0 : -1.0;
    }

    // Outward unit normal of a face at a point on it, using a fin's SP-curve image as the periodic reference.
    [[nodiscard]] Vector3 ResolveOutwardNormal(const SurfaceGeometry& Surface, Orientation Sense, Vector3 Position, Vector2 Reference) noexcept
    {
        return EvaluateSurfaceNormal(Surface, ProjectOntoSurface(Surface, Position, Reference)) * OrientationSign(Sense);
    }

    // 📝 Parameter box for the SSI of an offset support: periodic directions span a full period (no boundary), the
    //    others grow by Margin (converted to chart units on spheres and NURBS / parallel carriers, and clamped to open
    //    NURBS knot domains).
    [[nodiscard]] ParameterBox ExpandWindow(const SurfaceGeometry& Surface, const ParameterWindow& Window, double Margin) noexcept
    {
        const SurfacePeriodicity Period = ResolvePeriodicity(Surface);
        ParameterBox Box{ Window.Low, Window.High };
        double MarginU = Margin, MarginV = Margin;                                           // [-] - Chart growth
        if (const auto* Sphere = std::get_if<SphereSurface>(&Surface)) MarginV = Margin / Sphere->Radius;
        else if (const NurbsSurface* Spline = ResolveNurbsChart(Surface))
        {
            const SurfaceSample Mid = EvaluateSurface(*Spline, (Window.Low + Window.High) * 0.5);
            MarginU = Margin / std::max(Length(Mid.DerivativeU), 1.0e-6);
            MarginV = Margin / std::max(Length(Mid.DerivativeV), 1.0e-6);
        }
        if (Period.PeriodU > 0.0) Box.High.X = Box.Low.X + Period.PeriodU;
        else { Box.Low.X -= MarginU; Box.High.X += MarginU; }
        if (Period.PeriodV > 0.0) Box.High.Y = Box.Low.Y + Period.PeriodV;
        else { Box.Low.Y -= MarginV; Box.High.Y += MarginV; }
        if (std::holds_alternative<SphereSurface>(Surface))
        {
            Box.Low.Y  = std::max(Box.Low.Y, -0.5 * std::numbers::pi);
            Box.High.Y = std::min(Box.High.Y, 0.5 * std::numbers::pi);
        }
        else if (const NurbsSurface* Spline = ResolveNurbsChart(Surface))
        {
            const NurbsDomain DomainU = MeasureNurbsSurfaceDomain(*Spline->Definition, true);
            const NurbsDomain DomainV = MeasureNurbsSurfaceDomain(*Spline->Definition, false);
            if (Period.PeriodU <= 0.0) { Box.Low.X = std::max(Box.Low.X, DomainU.Low); Box.High.X = std::min(Box.High.X, DomainU.High); }
            if (Period.PeriodV <= 0.0) { Box.Low.Y = std::max(Box.Low.Y, DomainV.Low); Box.High.Y = std::min(Box.High.Y, DomainV.High); }
        }
        return Box;
    }

    // 📝 Residual refusal in the user's terms: a ball that no longer sits on both faces to tolerance almost always means
    //    the size outgrew the edge (a contact off its face, a support curvature radius approached). Wider names the face
    //    the mid-section contact has already left (empty when none did).
    [[nodiscard]] std::string DescribeResidualRefusal(const char* Kind, double Residual, double Allowance, double Size, const std::string& Wider)
    {
        char Numbers[160];
        std::snprintf(Numbers, sizeof Numbers, "size %.4g", Size);
        if (!Wider.empty()) return std::string(Numbers) + " is too large: the blend is wider than face " + Wider + " (its contact runs off that face) — try a smaller size";
        char Detail[160];
        std::snprintf(Detail, sizeof Detail, " (%s residual %.2g > allowance %.2g)", Kind, Residual, Allowance);
        return std::string(Numbers) + " is too large for this edge: the rolling ball no longer sits on both faces to tolerance" + Detail + " — try a smaller size";
    }

    // Foot of a spine point on a support as a contact curve sharing the spine parameter.
    [[nodiscard]] std::optional<CurveGeometry> ResolveContactCurve(const CurveGeometry& Spine, const SurfaceGeometry& Support, double Scale)
    {
        if (const auto* Line = std::get_if<StraightLine>(&Spine))
        {
            const Vector3 Foot = ProjectPointOntoSurface(Support, Line->Origin);             // [len] - Foot at τ = 0
            const Vector3 Next = ProjectPointOntoSurface(Support, Line->Origin + Line->Direction); // [len] - Foot at τ = 1
            if (Distance(Next, Foot + Line->Direction) > 1.0e-9 * Scale) return std::nullopt;
            return StraightLine{ Foot, Line->Direction };
        }
        const auto&   Arc     = std::get<CircularArc>(Spine);
        const Vector3 Foot    = ProjectPointOntoSurface(Support, EvaluateCurve(Spine, 0.0).Position);
        const Vector3 Offset  = Foot - Arc.Frame.Origin;                                     // [len] - From the spine centre
        const double  Height  = Dot(Offset, Arc.Frame.ZAxis);                                // [len] - Axial position
        const Vector3 Radial  = Offset - Arc.Frame.ZAxis * Height;                           // [len] - Radial part
        const double  Reach   = Dot(Radial, Arc.Frame.XAxis);                                // [len] - Contact radius ρ
        if (!(Reach > 1.0e-9 * Scale) || Length(Radial - Arc.Frame.XAxis * Reach) > 1.0e-9 * Scale) return std::nullopt;
        OrthonormalFrame Frame = Arc.Frame;
        Frame.Origin = Arc.Frame.Origin + Arc.Frame.ZAxis * Height;
        return CircularArc{ Frame, Reach };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

Vector3 ProjectPointOntoSurface(const SurfaceGeometry& Surface, Vector3 Position) noexcept
{
    return EvaluateSurface(Surface, ProjectOntoSurface(Surface, Position, { 0.0, 0.0 })).Position;
}

bool EdgeBlendSheet::LawCondition() const noexcept
{
    const auto* Traced = std::get_if<IntersectionCurve>(&Spine);
    return Traced && Traced->Trace && Traced->Trace->Law;
}

double MeasureSheetRadius(const EdgeBlendSheet& Sheet, double τ) noexcept
{
    if (Sheet.LawCondition()) return MeasureBallLawRadius(*std::get<IntersectionCurve>(Sheet.Spine).Trace, τ);
    return Sheet.Radius + Sheet.RadiusRate * τ;
}

double MeasureEnvelopeResidual(const EdgeBlendSheet& Sheet, Vector3 Position) noexcept
{
    if (Sheet.LawCondition())
    {
        // 📝 Ball law: min over τ of g(τ) = |X − C(τ)| − r(τ) from the spine and the law alone (not the carrier's chart):
        //    g′(τ) = −(X − C)·C′ / |X − C| − r′(τ) = 0 by the secant method from the spine foot; r′ by central difference.
        const auto Rate = [&](double τ)
        {
            const CurveSample Sample = EvaluateCurve(Sheet.Spine, τ);
            const Vector3     Offset = Position - Sample.Position;
            const double      Step   = 1.0e-6 * (1.0 + std::fabs(τ));
            return -Dot(Offset, Sample.Derivative) / Length(Offset) - (MeasureSheetRadius(Sheet, τ + Step) - MeasureSheetRadius(Sheet, τ - Step)) / (2.0 * Step);
        };
        double Before = ProjectOntoCurve(Sheet.Spine, Position, 0.0), Lower = Rate(Before);
        double τ = Before + 1.0e-4 * (1.0 + std::fabs(Before));
        for (int Iteration = 0; Iteration < 40; ++Iteration)
        {
            const double Current = Rate(τ);
            if (Current == Lower) break;
            const double Next = τ - Current * (τ - Before) / (Current - Lower);
            Before = τ; Lower = Current; τ = Next;
            if (std::fabs(τ - Before) <= 1.0e-14 * (1.0 + std::fabs(τ))) break;
        }
        return Distance(Position, EvaluateCurve(Sheet.Spine, τ).Position) - MeasureSheetRadius(Sheet, τ);
    }
    const double τ     = ProjectOntoCurve(Sheet.Spine, Position, 0.0);                       // [-]   - Foot on the spine
    const double Reach = Distance(Position, EvaluateCurve(Sheet.Spine, τ).Position);         // [len] - ρ
    if (!Sheet.VariableCondition()) return Reach - Sheet.Radius;
    return Reach * std::sqrt(1.0 - Sheet.RadiusRate * Sheet.RadiusRate) - MeasureSheetRadius(Sheet, τ);
}

Vector2 ResolveSheetChart(const EdgeBlendSheet& Sheet, double τ) noexcept
{
    if (Sheet.SpineAxis == 0) return { τ, Sheet.CrossCentre };
    if (Sheet.SpineAxis == 1) return { Sheet.CrossCentre, τ };
    return { 0.0, 0.0 };
}

std::optional<CurveGeometry> SectionCylinderByPlane(const CylinderSurface& Cylinder, Vector3 PlanePoint, Vector3 PlaneNormal) noexcept
{
    const Vector3 Normal = Normalize(PlaneNormal);                                           // [-] - Unit plane normal
    const Vector3 Axis   = Cylinder.Frame.ZAxis;                                             // [-] - Unit cylinder axis
    const double  Cosine = Dot(Normal, Axis);                                                // [-] - cos of the tilt
    if (std::fabs(Cosine) < 1.0e-12) return std::nullopt;
    const Vector3 Centre = Cylinder.Frame.Origin + Axis * (Dot(PlanePoint - Cylinder.Frame.Origin, Normal) / Cosine);
    if (std::fabs(Cosine) > 1.0 - 1.0e-15)
    {
        return CircularArc{ ConstructFrame(Centre, Normal, Cylinder.Frame.XAxis), Cylinder.Radius };
    }
    const Vector3 Minor = Normalize(Cross(Axis, Normal));                                    // [-] - In the plane, ⟂ axis
    const Vector3 Major = Normalize(Cross(Normal, Minor));                                   // [-] - In the plane, along the tilt
    return EllipticalArc{ { Centre, Major, Minor, Cross(Major, Minor) }, Cylinder.Radius / std::fabs(Cosine), Cylinder.Radius };
}

EdgeBlendSheetOutcome ResolveEdgeBlendSheet(const ModelPartition& Partition, EdgeToken Edge, const BlendSettings& Requested, const SupportOverride& Override)
{
    EdgeBlendSheetOutcome Outcome;
    const auto Refuse = [&](const std::string& Reason) { Outcome.Refusal = LabelToken("E", Edge) + ": " + Reason; return Outcome; };
    if (!(Requested.Size > 0.0)) return Refuse("blend size must be positive");
    const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);

    // ⓪ Size anchors: an edge anchored at its end vertex swaps Size and EndSize, so below Size is always at the start.
    BlendSettings Settings = Requested;
    if (!Settings.AnchorSizes.empty())
    {
        // ⓪′ Per-vertex sizes: the edge takes its two vertices' sizes (unlisted: Size), start → end.
        if (Settings.AnchorSizes.size() != Settings.SizeAnchors.size()) return Refuse("per-vertex sizes do not match their vertices");
        const auto SizeAt = [&](VertexToken Vertex)
        {
            const auto Found = std::find(Settings.SizeAnchors.begin(), Settings.SizeAnchors.end(), Vertex);
            return Found == Settings.SizeAnchors.end() ? Requested.Size : Settings.AnchorSizes[size_t(Found - Settings.SizeAnchors.begin())];
        };
        const double StartSize = SizeAt(Profile.Start), EndSize = SizeAt(Profile.End);
        if (!(StartSize > 0.0) || !(EndSize > 0.0)) return Refuse("blend size must be positive at every vertex");
        if (Profile.Start == Profile.End && StartSize != Requested.Size) return Refuse("a per-vertex size on a closed edge (the size would jump at its only vertex)");
        Settings.Size    = StartSize;
        Settings.EndSize = EndSize;
        Settings.SizeAnchors.clear();
        Settings.AnchorSizes.clear();
    }
    else if (Settings.EndSize && !Settings.SizeAnchors.empty())
    {
        const auto Anchored = [&](VertexToken Vertex) { return std::find(Settings.SizeAnchors.begin(), Settings.SizeAnchors.end(), Vertex) != Settings.SizeAnchors.end(); };
        const bool StartAnchored = Anchored(Profile.Start), EndAnchored = Anchored(Profile.End);
        if (StartAnchored == EndAnchored)
        {
            return Refuse(StartAnchored ? "variable size anchored at both ends of the edge (which end takes the start size?)"
                                        : "variable size with neither end of the edge anchored (which end takes the start size?)");
        }
        if (EndAnchored) std::swap(Settings.Size, *Settings.EndSize);
    }

    // ① Supports, convexity and the wedge angle at mid-edge.
    EdgeBlendSheet Sheet;
    Sheet.Edge    = Edge;
    Sheet.Fins[0] = Profile.RadialFin;
    Sheet.Fins[1] = Partition.RadialMate(Profile.RadialFin);
    if (Sheet.Fins[1] == Sheet.Fins[0] || Partition.RadialMate(Sheet.Fins[1]) != Sheet.Fins[0]) return Refuse("not a manifold edge with two faces");
    SurfaceGeometry Supports[2];
    Orientation     Senses[2];
    for (int Side = 0; Side < 2; ++Side)
    {
        Sheet.Faces[Side] = Partition.Loops.Resolve(Partition.Fins.Resolve(Sheet.Fins[Side]).Loop).Face;
        const FaceProfile& Face = Partition.Faces.Resolve(Sheet.Faces[Side]);
        Supports[Side] = Partition.Surfaces.Resolve(Face.Surface);
        Senses[Side]   = Face.Sense;
    }
    if (Sheet.Faces[0] == Sheet.Faces[1]) return Refuse("seam edge (both sides on one face)");
    const CurveGeometry& Carrier = Partition.Curves.Resolve(Profile.Curve);
    const double Middle  = 0.5 * (Profile.IntervalLow + Profile.IntervalHigh);              // [-]   - Mid-edge τ
    const double Extent  = Distance(EvaluateCurve(Carrier, Profile.IntervalLow).Position, EvaluateCurve(Carrier, Middle).Position)
                         + Distance(EvaluateCurve(Carrier, Middle).Position, EvaluateCurve(Carrier, Profile.IntervalHigh).Position);
    const double Scale   = std::max({ 1.0, Settings.Size, Extent });                         // [len] - Tolerance scale
    const auto WedgeAt = [&](double τ, Vector3* Normals)
    {
        const Vector3 Position = EvaluateCurve(Carrier, τ).Position;
        for (int Side = 0; Side < 2; ++Side)
        {
            const FinProfile& Fin = Partition.Fins.Resolve(Sheet.Fins[Side]);
            const Vector2 Reference = Fin.SurfaceCurve ? EvaluateParameterCurve(*Fin.SurfaceCurve, τ).Position : Vector2{ 0.0, 0.0 };
            Normals[Side] = ResolveOutwardNormal(Supports[Side], Senses[Side], Position, Reference);
        }
        return std::acos(std::clamp(-Dot(Normals[0], Normals[1]), -1.0, 1.0));
    };
    Vector3 Normals[2];
    Sheet.Dihedral = WedgeAt(Middle, Normals);
    const Vector3 Position = EvaluateCurve(Carrier, Middle).Position;                        // [len] - Mid-edge point
    const Vector3 Tangent  = Normalize(EvaluateCurve(Carrier, Middle).Derivative);           // [-]   - Edge direction
    const double  Turn     = Dot(Cross(Normals[0], Normals[1]), Tangent * OrientationSign(Partition.Fins.Resolve(Sheet.Fins[0]).Sense));
    if (Length(Cross(Normals[0], Normals[1])) < 1.0e-9) return Refuse("smooth (tangent-continuous) edge: nothing to blend");
    Sheet.ConvexCondition = Turn > 0.0;

    // ② Ball radius; a chamfer on a constant wedge angle keeps r = d·tan(θ/2), a variable angle takes the ball law (②‴).
    Sheet.Radius = Settings.Size;
    bool VariableWedge = false;                                                              // [-] - θ changes along the edge
    if (Settings.Profile == BlendProfile::Chamfer)
    {
        for (int Sample = 0; Sample <= 8 && !VariableWedge; ++Sample)
        {
            Vector3 Probe[2];
            const double Angle = WedgeAt(Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Sample / 8.0, Probe);
            VariableWedge = std::fabs(Angle - Sheet.Dihedral) > 1.0e-9;
        }
        Sheet.Radius = Settings.Size * std::tan(0.5 * Sheet.Dihedral);
    }

    // ②″ Variable size: the size runs linearly from the start vertex to the end vertex. A straight edge between planes
    //     has the closed-form cone / plane below; every other edge takes the ball law (②‴) with a linear size.
    if (Settings.EndSize && !(*Settings.EndSize > 0.0)) return Refuse("blend end size must be positive");
    const bool VariableSize = Settings.EndSize && std::fabs(*Settings.EndSize - Settings.Size) > 1.0e-12 * Scale;   // [-] - Size changes along the edge
    const bool PlanarLine   = std::holds_alternative<StraightLine>(Carrier) && std::holds_alternative<PlaneSurface>(Supports[0]) && std::holds_alternative<PlaneSurface>(Supports[1]);
    if (VariableSize && Override.Side >= 0) return Refuse("variable size with a smooth overflow (🚧)");
    if (VariableSize && PlanarLine)
    {
        // 📝 r(s) linear along the edge makes the ball centres C(s) — r(s) from both planes, in the normal plane of E(s) —
        //    a line, and the envelope of the balls the exact cone tangent to both planes along the contact lines, which
        //    meet at the apex (r = 0) on the edge line. A chamfer (r = d·tan(θ/2), θ constant between planes) is the plane
        //    through the two contact lines. The spine τ is arc length from the start centre; the contact lines are
        //    unit-speed with their own τ (see EdgeBlendSheet::VariableCondition).
        const PlaneSurface* Planes[2] = { std::get_if<PlaneSurface>(&Supports[0]), std::get_if<PlaneSurface>(&Supports[1]) };
        const double  Factor   = Settings.Profile == BlendProfile::Chamfer ? std::tan(0.5 * Sheet.Dihedral) : 1.0;   // [-] - r / size
        const double  Radii[2] = { Settings.Size * Factor, *Settings.EndSize * Factor };     // [len] - r at start, end vertex
        const Vector3 Ends[2]  = { Partition.Vertices.Resolve(Profile.Start).Position, Partition.Vertices.Resolve(Profile.End).Position };
        const Vector3 Along    = Normalize(Ends[1] - Ends[0]);                                // [-]   - Edge direction, start → end
        // Ball centre: n₀·X = ℓ₀, n₁·X = ℓ₁ (offset planes), t·X = t·E (normal plane), by the triple-product inverse.
        const auto CentreAt = [&](Vector3 Point, double Radius)
        {
            double Levels[2];
            for (int Side = 0; Side < 2; ++Side)
            {
                const double Shift = (Sheet.ConvexCondition ? -1.0 : 1.0) * OrientationSign(Senses[Side]) * Radius;   // [len]
                Levels[Side] = Dot(Planes[Side]->Frame.ZAxis, Planes[Side]->Frame.Origin) + Shift;
            }
            const Vector3& A = Planes[0]->Frame.ZAxis;
            const Vector3& B = Planes[1]->Frame.ZAxis;
            return (Cross(B, Along) * Levels[0] + Cross(Along, A) * Levels[1] + Cross(A, B) * Dot(Along, Point)) * (1.0 / Dot(A, Cross(B, Along)));
        };
        const Vector3 Centres[2] = { CentreAt(Ends[0], Radii[0]), CentreAt(Ends[1], Radii[1]) };
        const double  Span       = Distance(Centres[0], Centres[1]);                         // [len] - Spine length
        Sheet.Radius     = Radii[0];
        Sheet.RadiusRate = (Radii[1] - Radii[0]) / Span;
        if (!(std::fabs(Sheet.RadiusRate) < 1.0 - 1.0e-9)) return Refuse("size changes faster than the edge allows (|dr/ds| ≥ 1)");
        const StraightLine Spine{ Centres[0], (Centres[1] - Centres[0]) * (1.0 / Span) };
        Sheet.Spine = Spine;
        Vector3 Feet[2][2];                                                                  // [len] - [side][end] contact points
        for (int Side = 0; Side < 2; ++Side)
        {
            for (int End = 0; End < 2; ++End) Feet[Side][End] = ProjectPointOntoSurface(Supports[Side], Centres[End]);
            Sheet.Contacts[Side] = StraightLine{ Feet[Side][0], Normalize(Feet[Side][1] - Feet[Side][0]) };
            const double Shift = (Sheet.ConvexCondition ? -1.0 : 1.0) * OrientationSign(Senses[Side]) * Radii[0];
            if (const std::optional<SurfaceGeometry> Offset = OffsetSurface(Supports[Side], Shift)) Sheet.Offsets[Side] = *Offset;
        }
        Sheet.Alignment = Dot(Spine.Direction, Along) >= 0.0 ? 1.0 : -1.0;
        const Vector3 Middle  = (Centres[0] + Centres[1]) * 0.5;                              // [len] - Mid-section ball centre
        const Vector3 MiddleA = (Feet[0][0] + Feet[0][1]) * 0.5, MiddleB = (Feet[1][0] + Feet[1][1]) * 0.5;
        const Vector3 Towards = Normalize(MiddleA + MiddleB - Middle * 2.0);                 // [-] - Towards the edge side
        Sheet.SpineAxis = -1;
        if (Settings.Profile == BlendProfile::Fillet)
        {
            // Cone about the spine: half-angle β with sin β = r′, radius at the start centre r₀ / cos β, slope tan β; the
            // frame's X faces the edge so the face sits around 𝑢 = 0 (chart reference (0, 0)).
            const double Cosine = std::sqrt(1.0 - Sheet.RadiusRate * Sheet.RadiusRate);      // [-] - cos β
            const Vector3 Facing = Normalize(Towards - Spine.Direction * Dot(Towards, Spine.Direction));
            Sheet.Surface = ConeSurface{ ConstructFrame(Centres[0], Spine.Direction, Facing), Radii[0] / Cosine, Sheet.RadiusRate / Cosine };
        }
        else
        {
            const Vector3 Normal = Cross(Feet[0][1] - Feet[0][0], Feet[1][0] - Feet[0][0]);   // [len²] - Both contact lines lie in it
            Sheet.Surface = PlaneSurface{ ConstructFrame(Feet[0][0], Normal, Feet[0][1] - Feet[0][0]) };
        }
        // ⑦″ Residuals: feet on their supports at distance r(τ) from the ball, both contact lines on the carrier, the
        //    carrier tangent to the balls (fillet: on the envelope).
        for (int Sample = 0; Sample <= ResidualSamples; ++Sample)
        {
            const double  τ    = Span * Sample / ResidualSamples;
            const Vector3 Ball = EvaluateCurve(Spine, τ).Position;
            for (int Side = 0; Side < 2; ++Side)
            {
                const Vector3 Foot = ProjectPointOntoSurface(Supports[Side], Ball);
                Sheet.MaxResidual = std::max({ Sheet.MaxResidual, std::fabs(Distance(Foot, Ball) - MeasureSheetRadius(Sheet, τ)), Distance(EvaluateCurve(Sheet.Contacts[Side], ProjectOntoCurve(Sheet.Contacts[Side], Foot, 0.0)).Position, Foot),
                                               Distance(ProjectPointOntoSurface(Sheet.Surface, Foot), Foot) });
                if (Settings.Profile == BlendProfile::Fillet) Sheet.MaxResidual = std::max(Sheet.MaxResidual, std::fabs(MeasureEnvelopeResidual(Sheet, Foot)));
            }
        }
        if (Sheet.MaxResidual > ResidualAllowance * Scale) return Refuse(DescribeResidualRefusal("variable rolling-ball", Sheet.MaxResidual, ResidualAllowance * Scale, Settings.Size, {}));
        // ⑧″ Orientation as in ⑧, probed at the mid-section.
        const Vector3 Probe   = Settings.Profile == BlendProfile::Fillet ? ProjectPointOntoSurface(Sheet.Surface, Middle + Towards * MeasureSheetRadius(Sheet, 0.5 * Span))
                                                                         : (MiddleA + MiddleB) * 0.5;
        const Vector3 Natural = EvaluateSurfaceNormal(Sheet.Surface, ProjectOntoSurface(Sheet.Surface, Probe, { 0.0, 0.0 }));
        Sheet.Sense = ((Dot(Natural, Position - Probe) > 0.0) == Sheet.ConvexCondition) ? Orientation::Forward : Orientation::Reversed;
        Outcome.Sheet = Sheet;
        return Outcome;
    }

    // ②′ Smooth overflow: the ball rolls onto the face across the support's tangent far edge.
    if (Override.Side == 0 || Override.Side == 1)
    {
        const FaceProfile& Face = Partition.Faces.Resolve(Override.Face);
        Sheet.Faces[Override.Side] = Override.Face;
        Supports[Override.Side]    = Partition.Surfaces.Resolve(Face.Surface);
        Senses[Override.Side]      = Face.Sense;
        if (Sheet.Faces[0] == Sheet.Faces[1]) return Refuse("smooth overflow onto the other support");
    }

    // ②‴ Ball law: a chamfer on a traced or elliptical edge (or any variable wedge), and any variable size off the planar
    //     line. The spine is the ball-law curve on the exact edge; the carrier is the chord between its contact rails
    //     (chamfer: every ruling is the chamfer of the local wedge with setback d(t)) or the canal of its balls (fillet).
    const bool ConicEdge = std::holds_alternative<StraightLine>(Carrier) || std::holds_alternative<CircularArc>(Carrier);   // [-] - Analytic path applies
    if ((Settings.Profile == BlendProfile::Chamfer && (VariableWedge || !ConicEdge)) || VariableSize)
    {
        if (Override.Side >= 0) return Refuse("chamfer on a curved edge with a smooth overflow (🚧)");
        // ⚠️ Fold guard, as ③: the largest ball along the edge must keep every NURBS support's parallel offset regular
        //    (1 − r·κ ≥ the floor on the face) — past a curvature radius the law's Newton marches into the fold.
        const double Largest = std::max(Settings.Size, Settings.EndSize.value_or(Settings.Size)) * (Settings.Profile == BlendProfile::Chamfer ? std::tan(0.5 * Sheet.Dihedral) : 1.0);   // [len] - Largest r
        for (int Side = 0; Side < 2; ++Side)
        {
            if (!std::holds_alternative<NurbsSurface>(Supports[Side])) continue;
            const std::optional<SurfaceGeometry> Offset = OffsetSurface(Supports[Side], (Sheet.ConvexCondition ? -1.0 : 1.0) * OrientationSign(Senses[Side]) * Largest);
            const auto* Parallel = Offset ? std::get_if<ParallelSurface>(&*Offset) : nullptr;
            if (!Parallel) continue;
            const ParameterWindow Window = DescribeFaceDomain(Partition, Sheet.Faces[Side]).Window;
            const ParallelFold    Fold   = MeasureParallelFold(*Parallel, Window.Low, Window.High);
            if (!(Fold.Margin > FoldMarginFloor)) return Refuse("size exceeds the curvature radius of the NURBS support (fold margin " + std::to_string(Fold.Margin) + ")");
        }
        // 📝 NURBS supports roll on their natural extension (end spans continued, chart unchanged inside): the spine runs
        //    past the edge ends for the cap sections, and there the feet must stay on a smooth surface, not on the
        //    clamped boundary of the patch.
        for (int Side = 0; Side < 2; ++Side)
        {
            if (const auto* Spline = std::get_if<NurbsSurface>(&Supports[Side]))
            {
                const std::optional<NurbsSurface> Extended = ExtendNurbsSurface(*Spline->Definition, SupportExtension);
                if (!Extended) return Refuse("the NURBS support cannot be extended past its edge (a rational end span turns over)");
                Supports[Side] = *Extended;
            }
        }
        const auto* EdgeTrace = std::get_if<IntersectionCurve>(&Carrier);
        const bool  LawEdge   = std::holds_alternative<EllipticalArc>(Carrier) || std::holds_alternative<NurbsCurve>(Carrier) || (EdgeTrace && !EdgeTrace->Trace->Law) || (VariableSize && ConicEdge);
        if (!LawEdge) return Refuse("chamfer on a variable-angle edge of this carrier kind (🚧)");
        if (VariableSize && Profile.Start == Profile.End) return Refuse("variable size on a closed edge (the size would jump at its only vertex)");
        BallLaw Law;
        Law.Edge = Carrier;
        Law.Form = Settings.Profile == BlendProfile::Chamfer ? BallLawForm::WedgeSetback : BallLawForm::Radius;
        Law.Size = Settings.Size;
        for (int Side = 0; Side < 2; ++Side)
        {
            Law.Senses[Side] = OrientationSign(Senses[Side]);
            Law.Signs[Side]  = (Sheet.ConvexCondition ? -1.0 : 1.0) * OrientationSign(Senses[Side]);
        }
        const CarrierRange EdgeRange = MeasureCarrierRange(Carrier);
        double SpineLow = EdgeRange.Low, SpineHigh = EdgeRange.High;                          // [-] - Law spine range (edge τ)
        bool   SpineClosed = EdgeRange.Period > 0.0;
        if (!VariableSize && std::holds_alternative<NurbsCurve>(Carrier) && Profile.Start != Profile.End)
        {
            // 💡 An open NURBS edge: the spine covers the edge plus a margin at both ends — an open carrier continues
            //    polynomially past its ends (like its extended supports), a periodic one (a closed sketch curve cut open)
            //    by its own next turn, of which the spine takes no more than the edge needs (≤ ½ of the free period, as
            //    for variable sizes). An end ruling lying exactly in the third face (an edge meeting it square) is then an
            //    interior crossing, not one on the boundary of the search window.
            const double Span   = Profile.IntervalHigh - Profile.IntervalLow;                 // [-]   - Edge interval
            const double Speed  = std::max(Length(EvaluateCurve(Carrier, Middle).Derivative), 1.0e-300);   // [len] - |E′| at mid-edge
            double       Margin = std::min(0.25 * Span, 8.0 * Settings.Size / Speed);
            if (EdgeRange.Period > 0.0) Margin = std::min(Margin, 0.5 * (0.98 * EdgeRange.Period - Span));
            if (Margin > 0.02 * Span)
            {
                SpineLow    = Profile.IntervalLow - Margin;
                SpineHigh   = Profile.IntervalHigh + Margin;
                SpineClosed = false;
            }
        }
        if (VariableSize)
        {
            // 📝 Size(t) linear in the edge parameter (arc length on lines and circles), Size at the start vertex, EndSize at
            //    the end vertex. The spine runs past both ends: a canal's characteristic circles lean by ρ′ out of the
            //    edge's normal planes (and an oblique end face cuts chord rulings beyond the end), so the cap section needs
            //    carrier beyond the edge. Margin ≤ ¼ span, ≤ 8 ball radii, keeps the size ≥ ¼ of its smaller end value and
            //    leaves 2 % of a period free.
            const Vector3 StartPoint = Partition.Vertices.Resolve(Profile.Start).Position;
            const bool    StartLow   = Distance(EvaluateCurve(Carrier, Profile.IntervalLow).Position, StartPoint) <= Distance(EvaluateCurve(Carrier, Profile.IntervalHigh).Position, StartPoint);
            const double  From = StartLow ? Profile.IntervalLow : Profile.IntervalHigh, To = StartLow ? Profile.IntervalHigh : Profile.IntervalLow;   // [-] - t at start, end vertex
            Law.SizeRate = (*Settings.EndSize - Settings.Size) / (To - From);
            Law.Size     = Settings.Size - Law.SizeRate * From;
            const double Span    = Profile.IntervalHigh - Profile.IntervalLow;                // [-] - Edge interval
            const double Smaller = std::min(Settings.Size, *Settings.EndSize), Larger = std::max(Settings.Size, *Settings.EndSize);
            const double Speed   = std::max(Length(EvaluateCurve(Carrier, Middle).Derivative), 1.0e-300);   // [len] - |E′| at mid-edge
            double Margin = std::min({ 0.25 * Span, 8.0 * Larger / Speed, 0.75 * Smaller / std::fabs(Law.SizeRate) });
            if (EdgeRange.Period > 0.0) Margin = std::min(Margin, 0.5 * (0.98 * EdgeRange.Period - Span));
            if (!(Margin > 0.02 * Span)) return Refuse("variable size on an edge that (nearly) closes on itself: no room for the cap sections");
            SpineLow    = Profile.IntervalLow - Margin;
            SpineHigh   = Profile.IntervalHigh + Margin;
            SpineClosed = false;
        }
        const std::optional<CurveGeometry> Spine = BuildBallLawCurve(Supports[0], Supports[1], Law, SpineLow, SpineHigh, SpineClosed);
        if (!Spine) return Refuse(VariableSize ? "variable size: the ball law has no solution somewhere along the edge (size does not fit the wedge)"
                                               : "chamfer: the ball law has no solution somewhere along the edge (setback does not fit the wedge)");
        Sheet.Spine = *Spine;
        const TracedIntersection& Trace = *std::get<IntersectionCurve>(Sheet.Spine).Trace;
        if (VariableSize) Sheet.Radius = MeasureBallLawRadius(Trace, Middle);                 // r at mid-edge (reference size)
        for (int Side = 0; Side < 2; ++Side)
        {
            Sheet.Contacts[Side] = ConstructContactCurve(Sheet.Spine, Supports[Side]);
            const double Shift = (Sheet.ConvexCondition ? -1.0 : 1.0) * OrientationSign(Senses[Side]) * Sheet.Radius;
            if (const std::optional<SurfaceGeometry> Offset = OffsetSurface(Supports[Side], Shift)) Sheet.Offsets[Side] = *Offset;   // Reference locus only
        }
        Sheet.Alignment   = Dot(EvaluateCurve(Sheet.Spine, Middle).Derivative, Tangent) >= 0.0 ? 1.0 : -1.0;
        Sheet.SpineAxis   = 0;
        const Vector3 BallMiddle = EvaluateCurve(Sheet.Spine, Middle).Position;              // [len] - Mid-edge ball centre
        const Vector3 FeetMiddle = EvaluateCurve(Sheet.Contacts[0], Middle).Position + EvaluateCurve(Sheet.Contacts[1], Middle).Position;
        if (Settings.Profile == BlendProfile::Chamfer)
        {
            Sheet.Surface     = ConstructRollingBallSurface(Sheet.Spine, Sheet.Radius, Supports[0], 1.0, RollingBallSection::Chord, Supports[1]);
            Sheet.CrossCentre = 0.5;
        }
        else
        {
            // Canal of the balls r(𝑢) = Size + SizeRate·𝑢 (𝑢 = spine τ = edge t); 𝑣 = 0 on contact A, the face around the
            // polar angle of the mid-edge bisector.
            for (int Sample = 0; Sample <= ResidualSamples; ++Sample)
            {
                const double τ    = SpineLow + (SpineHigh - SpineLow) * Sample / ResidualSamples;
                const double Rate = std::fabs(Law.SizeRate) / std::max(Length(EvaluateCurve(Sheet.Spine, τ).Derivative), 1.0e-300);   // [-] - |dr/ds|
                if (!(Rate < 1.0 - 1.0e-9)) return Refuse("size changes faster than the edge allows (|dr/ds| ≥ 1)");
            }
            const RollingBallSurface Canal = ConstructRollingBallSurface(Sheet.Spine, Law.Size, Supports[0], 1.0, RollingBallSection::Circle, PlaneSurface{}, Law.SizeRate);
            Sheet.Surface     = Canal;
            Sheet.CrossCentre = ProjectOntoRollingBall(Canal, BallMiddle + Normalize(FeetMiddle - BallMiddle * 2.0) * Sheet.Radius, { Middle, 0.0 }).Y;
        }
        // ⑦‴ Residuals over the edge: the law (feet at r(τ) from the centre, centre in the normal plane of E(τ)), feet on
        //     their supports, contacts on the carrier.
        for (int Sample = 0; Sample <= ResidualSamples; ++Sample)
        {
            const double      τ      = Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Sample / ResidualSamples;
            const Vector3     Ball   = EvaluateCurve(Sheet.Spine, τ).Position;               // [len] - Ball centre
            const CurveSample OnEdge = EvaluateCurve(Carrier, τ);
            const double      Radius = MeasureBallLawRadius(Trace, τ);                       // [len] - r(τ)
            Sheet.MaxResidual = std::max(Sheet.MaxResidual, std::fabs(Dot(Ball - OnEdge.Position, Normalize(OnEdge.Derivative))));
            for (int Side = 0; Side < 2; ++Side)
            {
                const Vector3 Foot = EvaluateCurve(Sheet.Contacts[Side], τ).Position;        // [len] - Contact point
                Sheet.MaxResidual = std::max({ Sheet.MaxResidual, std::fabs(Distance(Foot, Ball) - Radius), Distance(ProjectPointOntoSurface(Supports[Side], Ball), Foot),
                                               Distance(EvaluateSurface(Sheet.Surface, ProjectOntoSurface(Sheet.Surface, Foot, ResolveSheetChart(Sheet, τ))).Position, Foot) });
            }
        }
        if (Sheet.MaxResidual > ResidualAllowance * Scale) return Refuse(DescribeResidualRefusal("ball-law", Sheet.MaxResidual, ResidualAllowance * Scale, Settings.Size, {}));
        // ⑧‴ Orientation as in ⑧, probed at the mid-edge ruling (chamfer) or bisector point (fillet).
        const Vector3 Probe   = Settings.Profile == BlendProfile::Chamfer ? FeetMiddle * 0.5 : BallMiddle + Normalize(FeetMiddle - BallMiddle * 2.0) * Sheet.Radius;
        const Vector3 Natural = EvaluateSurfaceNormal(Sheet.Surface, ProjectOntoSurface(Sheet.Surface, Probe, ResolveSheetChart(Sheet, Middle)));
        Sheet.Sense = ((Dot(Natural, Position - Probe) > 0.0) == Sheet.ConvexCondition) ? Orientation::Forward : Orientation::Reversed;
        Outcome.Sheet = Sheet;
        return Outcome;
    }

    // ③ Offsets towards the ball: inside the material for a convex edge, outside for a concave one.
    for (int Side = 0; Side < 2; ++Side)
    {
        const double Distance = (Sheet.ConvexCondition ? -1.0 : 1.0) * OrientationSign(Senses[Side]) * Sheet.Radius;
        const std::optional<SurfaceGeometry> Offset = OffsetSurface(Supports[Side], Distance);
        if (!Offset) return Refuse(std::string("size exceeds the curvature radius of the ") + SurfaceCategoryLabel(Supports[Side]) + " support");
        SurfaceGeometry ChosenOffset = *Offset;
        // 📝 NURBS support: the offset is the exact parallel carrier S + d·n. It is regular only while 1 − d·κᵢ > 0 on
        //    the face; near a fold its SSI is ill-conditioned, so a ball within 2 % of a local curvature radius is refused.
        if (const auto* Parallel = std::get_if<ParallelSurface>(&*Offset))
        {
            const ParameterWindow Window = DescribeFaceDomain(Partition, Sheet.Faces[Side]).Window;
            const ParallelFold    Fold   = MeasureParallelFold(*Parallel, Window.Low, Window.High);
            if (!(Fold.Margin > FoldMarginFloor)) return Refuse("size exceeds the curvature radius of the NURBS support (fold margin " + std::to_string(Fold.Margin) + ")");
            // Extend open NURBS supports slightly past their clamped boundary (as ②‴ does) when the extended collar stays
            // fold-free and non-inverting, so the spine and contact rail reach oblique end caps cleanly.
            if (const auto* Spline = std::get_if<NurbsSurface>(&Supports[Side]))
            {
                const SurfacePeriodicity Period = ResolveNurbsPeriodicity(*Spline);
                if (Period.PeriodU <= 0.0 || Period.PeriodV <= 0.0)
                {
                    const Vector3 MidNormal = EvaluateNurbsNormal(*Spline, (Window.Low + Window.High) * 0.5);
                    for (double Ext : { SupportExtension, 0.12, 0.05 })
                    {
                        const std::optional<NurbsSurface> Extended = ExtendNurbsSurface(*Spline->Definition, Ext);
                        if (!Extended) continue;
                        const NurbsDomain DomU = MeasureNurbsSurfaceDomain(*Extended->Definition, true);
                        const NurbsDomain DomV = MeasureNurbsSurfaceDomain(*Extended->Definition, false);
                        bool Regular = true;
                        for (int R = 0; R <= 4 && Regular; ++R)
                            for (int C = 0; C <= 4 && Regular; ++C)
                            {
                                const Vector2 At{ DomU.Low + (DomU.High - DomU.Low) * C / 4.0, DomV.Low + (DomV.High - DomV.Low) * R / 4.0 };
                                Regular = Dot(EvaluateNurbsNormal(*Extended, At), MidNormal) > 0.05;
                            }
                        if (!Regular) continue;
                        const std::optional<SurfaceGeometry> ExtOffset = OffsetSurface(*Extended, Distance);
                        const auto* ExtParallel = ExtOffset ? std::get_if<ParallelSurface>(&*ExtOffset) : nullptr;
                        if (!ExtParallel) continue;
                        if (MeasureParallelFold(*ExtParallel, { DomU.Low, DomV.Low }, { DomU.High, DomV.High }).Margin > FoldMarginFloor)
                        {
                            Supports[Side] = *Extended;
                            ChosenOffset   = *ExtOffset;
                            break;
                        }
                    }
                }
            }
        }
        Sheet.Offsets[Side] = ChosenOffset;
    }

    // ④ Spine: the exact SSI branch of the offsets nearest the edge.
    const double Margin = 4.0 * Sheet.Radius / std::max(std::sin(0.5 * Sheet.Dihedral), 0.1); // [len] - Box growth
    const ParameterBox BoxA = ExpandWindow(Sheet.Offsets[0], DescribeFaceDomain(Partition, Sheet.Faces[0]).Window, Margin);
    const ParameterBox BoxB = ExpandWindow(Sheet.Offsets[1], DescribeFaceDomain(Partition, Sheet.Faces[1]).Window, Margin);
    const SurfaceIntersectionOutcome Section = IntersectSurfaces(Sheet.Offsets[0], BoxA, Sheet.Offsets[1], BoxB);
    if (Section.CoincidentCondition) return Refuse("offset supports coincide");
    // 💡 Lines and circles keep their analytic carriers (cylinder, torus, cone, plane); ellipses and traced branches get
    //    the procedural rolling-ball carrier, whose spine is the exact branch itself (traced: Newton onto both offsets).
    double Nearest = std::numeric_limits<double>::max();                                     // [len] - Spine-to-edge distance
    for (const IntersectionBranch& Branch : Section.Branches)
    {
        CurveGeometry Candidate = Branch.Curve;
        if (Branch.Category == BranchCategory::Traced)
        {
            if (Branch.Chart.size() < 2 || Branch.TangentialCondition) continue;
            std::vector<Vector3> Guide;
            for (const IntersectionChartPoint& Point : Branch.Chart) Guide.push_back(Point.Position);
            Candidate = BuildIntersectionCurve(Sheet.Offsets[0], Sheet.Offsets[1], Guide, Branch.ClosedCondition);
        }
        const double Gap = Distance(Position, EvaluateCurve(Candidate, ProjectOntoCurve(Candidate, Position, 0.0)).Position);
        if (Gap < Nearest) { Nearest = Gap; Sheet.Spine = Candidate; }
    }
    if (Nearest == std::numeric_limits<double>::max()) return Refuse("offset supports do not meet (no spine)");
    const bool AnalyticCondition = std::holds_alternative<StraightLine>(Sheet.Spine) || std::holds_alternative<CircularArc>(Sheet.Spine);   // [-] - Analytic carrier
    if (!AnalyticCondition && Settings.Profile == BlendProfile::Chamfer) return Refuse("chamfer whose offset spine is not a line or circle on a conic edge (🚧)");

    // ⑤ Contact curves (same τ as the spine).
    for (int Side = 0; Side < 2; ++Side)
    {
        const std::optional<CurveGeometry> Contact = AnalyticCondition ? ResolveContactCurve(Sheet.Spine, Supports[Side], Scale)
                                                                       : std::optional<CurveGeometry>{ ConstructContactCurve(Sheet.Spine, Supports[Side]) };
        if (!Contact) return Refuse("contact curve degenerates (ball reaches a support axis)");
        Sheet.Contacts[Side] = *Contact;
    }
    const double SpineMiddle = ProjectOntoCurve(Sheet.Spine, Position, 0.0);                 // [-] - Spine τ of the mid-section
    Sheet.Alignment = Dot(EvaluateCurve(Sheet.Spine, SpineMiddle).Derivative, Tangent) >= 0.0 ? 1.0 : -1.0;

    // ⑥ Exact carrier.
    const Vector3 Centre   = EvaluateCurve(Sheet.Spine, 0.0).Position;                       // [len] - Spine at τ = 0
    const Vector3 FootA    = EvaluateCurve(Sheet.Contacts[0], 0.0).Position;                 // [len] - Contact A at τ = 0
    const Vector3 FootB    = EvaluateCurve(Sheet.Contacts[1], 0.0).Position;                 // [len] - Contact B at τ = 0
    const Vector3 Bisector = Normalize((FootA - Centre) + (FootB - Centre));                 // [-]   - Towards the edge side
    if (!AnalyticCondition)
    {
        // 📝 Procedural carrier: 𝑢 is the spine τ (SpineAxis 0), 𝑣 = 0 on contact A; the cross-section centre is the polar
        //    angle of the bisector at the mid-section.
        const RollingBallSurface Carrier = ConstructRollingBallSurface(Sheet.Spine, Sheet.Radius, Supports[0]);
        const Vector3 BallMiddle  = EvaluateCurve(Sheet.Spine, SpineMiddle).Position;          // [len] - Mid-section ball centre
        const Vector3 MiddleA     = EvaluateCurve(Sheet.Contacts[0], SpineMiddle).Position;    // [len] - Mid-section contact A
        const Vector3 MiddleB     = EvaluateCurve(Sheet.Contacts[1], SpineMiddle).Position;    // [len] - Mid-section contact B
        const Vector3 Towards     = BallMiddle + Normalize(MiddleA + MiddleB - BallMiddle * 2.0) * Sheet.Radius;
        Sheet.Surface     = Carrier;
        Sheet.SpineAxis   = 0;
        Sheet.CrossCentre = ProjectOntoRollingBall(Carrier, Towards, { SpineMiddle, 0.0 }).Y;
    }
    else if (const auto* Line = std::get_if<StraightLine>(&Sheet.Spine))
    {
        if (Settings.Profile == BlendProfile::Fillet)
        {
            Sheet.Surface     = CylinderSurface{ ConstructFrame(Line->Origin, Line->Direction, -Bisector), Sheet.Radius };
            Sheet.SpineAxis   = 1;
            Sheet.CrossCentre = std::numbers::pi;
        }
        else
        {
            const PlaneSurface Plane{ ConstructFrame(FootA, Cross(Line->Direction, FootB - FootA), Line->Direction) };
            Sheet.Surface     = Plane;
            Sheet.SpineAxis   = 0;
            Sheet.CrossCentre = 0.5 * Dot(FootB - FootA, Plane.Frame.YAxis);
        }
    }
    else
    {
        const auto& Arc = std::get<CircularArc>(Sheet.Spine);
        if (Settings.Profile == BlendProfile::Fillet)
        {
            if (!(Arc.Radius > Sheet.Radius * (1.0 + 1.0e-9))) return Refuse("blend tube would cross its own axis (spine radius ≤ ball radius)");
            Sheet.Surface     = TorusSurface{ Arc.Frame, Arc.Radius, Sheet.Radius };
            Sheet.SpineAxis   = 0;
            Sheet.CrossCentre = std::atan2(Dot(Bisector, Arc.Frame.ZAxis), Dot(Bisector, Arc.Frame.XAxis));
        }
        else
        {
            const auto&  ArcA = std::get<CircularArc>(Sheet.Contacts[0]);
            const auto&  ArcB = std::get<CircularArc>(Sheet.Contacts[1]);
            const double HeightA = Dot(ArcA.Frame.Origin - Arc.Frame.Origin, Arc.Frame.ZAxis);  // [len] - Axial position of A
            const double HeightB = Dot(ArcB.Frame.Origin - Arc.Frame.Origin, Arc.Frame.ZAxis);  // [len] - Axial position of B
            OrthonormalFrame Frame = Arc.Frame;
            Frame.Origin = ArcA.Frame.Origin;
            if (std::fabs(HeightB - HeightA) <= 1.0e-12 * Scale)
            {
                Sheet.Surface   = PlaneSurface{ Frame };
                Sheet.SpineAxis = -1;
            }
            else if (std::fabs(ArcB.Radius - ArcA.Radius) <= 1.0e-12 * Scale)
            {
                Sheet.Surface     = CylinderSurface{ Frame, ArcA.Radius };
                Sheet.SpineAxis   = 0;
                Sheet.CrossCentre = 0.5 * (HeightB - HeightA);
            }
            else
            {
                Sheet.Surface     = ConeSurface{ Frame, ArcA.Radius, (ArcB.Radius - ArcA.Radius) / (HeightB - HeightA) };
                Sheet.SpineAxis   = 0;
                Sheet.CrossCentre = 0.5 * (HeightB - HeightA);
            }
        }
    }

    // ⑦ Sampled residuals: ball distance |C_i(τ) − S(τ)| = r, feet on the supports, contacts on the carrier.
    const bool   Periodic = PeriodicCondition(Sheet.Spine);
    const double SpineLow = Periodic ? 0.0 : ProjectOntoCurve(Sheet.Spine, EvaluateCurve(Carrier, Profile.IntervalLow).Position, 0.0);
    const double SpineHigh = Periodic ? 2.0 * std::numbers::pi : ProjectOntoCurve(Sheet.Spine, EvaluateCurve(Carrier, Profile.IntervalHigh).Position, 0.0);
    for (int Sample = 0; Sample <= ResidualSamples; ++Sample)
    {
        const double  τ    = SpineLow + (SpineHigh - SpineLow) * Sample / ResidualSamples;
        const Vector3 Ball = EvaluateCurve(Sheet.Spine, τ).Position;                         // [len] - Ball centre
        for (int Side = 0; Side < 2; ++Side)
        {
            const Vector3 Foot = EvaluateCurve(Sheet.Contacts[Side], τ).Position;            // [len] - Contact point
            Sheet.MaxResidual = std::max({ Sheet.MaxResidual,
                                           std::fabs(Distance(Foot, Ball) - Sheet.Radius),
                                           Distance(ProjectPointOntoSurface(Supports[Side], Ball), Foot),
                                           Distance(ProjectPointOntoSurface(Sheet.Surface, Foot), Foot) });
        }
    }
    if (Sheet.MaxResidual > ResidualAllowance * Scale)
    {
        // 📝 Name the face the blend outgrows when its mid-section contact already lies off that face (a fillet deeper
        //    than the pocket wall it rolls on); otherwise the generic size hint.
        std::string Wider;
        for (int Side = 0; Side < 2 && Wider.empty(); ++Side)
        {
            const Vector3 Foot = EvaluateCurve(Sheet.Contacts[Side], SpineMiddle).Position;
            if (ClassifyFacePosition(DescribeFaceDomain(Partition, Sheet.Faces[Side]), Foot, 1.0e-6 * Scale).Membership == PointMembership::Outside) Wider = LabelToken("F", Sheet.Faces[Side]);
        }
        return Refuse(DescribeResidualRefusal("rolling-ball", Sheet.MaxResidual, ResidualAllowance * Scale, Settings.Size, Wider));
    }

    // ⑧ Orientation: outward points towards the old edge for a convex blend (material removed), away for a concave one.
    const Vector3 BallMiddle = EvaluateCurve(Sheet.Spine, SpineMiddle).Position;             // [len] - Mid-section ball centre
    const Vector3 FootMiddleA = EvaluateCurve(Sheet.Contacts[0], SpineMiddle).Position;      // [len] - Mid-section contact A
    const Vector3 FootMiddleB = EvaluateCurve(Sheet.Contacts[1], SpineMiddle).Position;      // [len] - Mid-section contact B
    const Vector3 Probe = Settings.Profile == BlendProfile::Fillet
                        ? BallMiddle + Normalize(FootMiddleA + FootMiddleB - BallMiddle * 2.0) * Sheet.Radius
                        : (FootMiddleA + FootMiddleB) * 0.5;
    const Vector3 Natural = EvaluateSurfaceNormal(Sheet.Surface, ProjectOntoSurface(Sheet.Surface, Probe, ResolveSheetChart(Sheet, SpineMiddle)));
    const double  Facing  = Dot(Natural, Position - Probe);                                  // [len] - > 0: natural faces the edge
    Sheet.Sense = ((Facing > 0.0) == Sheet.ConvexCondition) ? Orientation::Forward : Orientation::Reversed;
    Outcome.Sheet = Sheet;
    return Outcome;
}

bool SmoothEdgeCondition(const ModelPartition& Partition, EdgeToken Edge)
{
    const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
    const FinToken     First   = Profile.RadialFin;
    const FinToken     Second  = Partition.RadialMate(First);
    if (Second == First || Partition.RadialMate(Second) != First) return false;
    const CurveGeometry& Carrier = Partition.Curves.Resolve(Profile.Curve);
    const double  Middle   = 0.5 * (Profile.IntervalLow + Profile.IntervalHigh);              // [-] - Mid-edge τ
    const Vector3 Position = EvaluateCurve(Carrier, Middle).Position;
    Vector3 Normals[2];
    int     Side = 0;
    for (FinToken Fin : { First, Second })
    {
        const FinProfile&  Use  = Partition.Fins.Resolve(Fin);
        const FaceProfile& Face = Partition.Faces.Resolve(Partition.Loops.Resolve(Use.Loop).Face);
        const Vector2 Reference = Use.SurfaceCurve ? EvaluateParameterCurve(*Use.SurfaceCurve, Middle).Position : Vector2{ 0.0, 0.0 };
        Normals[Side++] = ResolveOutwardNormal(Partition.Surfaces.Resolve(Face.Surface), Face.Sense, Position, Reference);
    }
    return Length(Cross(Normals[0], Normals[1])) < 1.0e-9 && Dot(Normals[0], Normals[1]) > 0.0;
}

std::vector<EdgeToken> GrowTangentChain(const ModelPartition& Partition, BodyToken Body, const std::vector<EdgeToken>& Seeds)
{
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    std::map<uint32_t, std::vector<std::pair<EdgeToken, int>>> Star;                         // vertex slot → (edge, end)
    for (EdgeToken Edge : Inventory.Edges)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        Star[Profile.Start.Slot].push_back({ Edge, 0 });
        Star[Profile.End.Slot].push_back({ Edge, 1 });
    }
    // Unit tangent at an edge end, pointing from the vertex into the edge.
    const auto Inward = [&](EdgeToken Edge, int End)
    {
        const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
        const CurveGeometry& Carrier = Partition.Curves.Resolve(Profile.Curve);
        const Vector3 Along = Normalize(EvaluateCurve(Carrier, End == 0 ? Profile.IntervalLow : Profile.IntervalHigh).Derivative);
        return End == 0 ? Along : Along * -1.0;
    };
    std::vector<EdgeToken> Chain;
    std::set<uint32_t>     Chosen;
    for (EdgeToken Seed : Seeds)
    {
        if (Chosen.insert(Seed.Slot).second) Chain.push_back(Seed);
    }
    for (size_t Cursor = 0; Cursor < Chain.size(); ++Cursor)
    {
        const EdgeToken    Edge    = Chain[Cursor];
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        if (Profile.Start == Profile.End) continue;                                          // Closed rim: no ends to cross
        for (int End = 0; End < 2; ++End)
        {
            const VertexToken Vertex = End == 0 ? Profile.Start : Profile.End;
            const Vector3     Leaving = Inward(Edge, End) * -1.0;                            // [-] - Direction through the vertex
            for (const auto& [Other, OtherEnd] : Star[Vertex.Slot])
            {
                if (Chosen.count(Other.Slot) != 0) continue;
                if (Dot(Leaving, Inward(Other, OtherEnd)) < 1.0 - 1.0e-9) continue;
                if (SmoothEdgeCondition(Partition, Other)) continue;
                Chosen.insert(Other.Slot);
                Chain.push_back(Other);
            }
        }
    }
    return Chain;
}

const char* BlendProfileLabel(BlendProfile Profile) noexcept
{
    return Profile == BlendProfile::Fillet ? "Fillet" : "Chamfer";
}

} // namespace Frontier::GeometrySpace::Parametric
