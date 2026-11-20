/*====================================================================================================================================
                                                        PROFILESWEEP.CPP
====================================================================================================================================*/
// 🧩 Solids from sketch profiles: linear extrude (planes, cylinders, holes) and revolve (plane … torus, poles, NURBS revolutions)

#include "Engine/GeometrySpace/Parametric/Feature/ProfileSweep.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"
#include "Engine/GeometrySpace/Parametric/Feature/SweepBlueprint.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <numbers>
#include <optional>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

using namespace Sweeping;

//------------------------------------------------------------------------------------------------------------------------
//                                                    EXTRUDE
//------------------------------------------------------------------------------------------------------------------------

static SweepOutcome ExtrudeProfileUntraced(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalBasis& Plane, double Extent, bool CapsCondition)
{
    SweepOutcome Refused;
    if (!Profile.Refusal.empty()) { Refused.Refusal = "Extrude: " + Profile.Refusal; return Refused; }
    const double Height = std::fabs(Extent);                                               // [len] - Extent along the plane normal
    if (!(Height > 1.0e-9 * Profile.Scale)) { Refused.Refusal = "Extrude: distance is zero"; return Refused; }
    const double  Floor = std::min(0.0, Extent);                                           // [len] - Low cap height
    const Vector3 Axis  = Plane.ZAxis;
    auto Place = [&](Vector2 Point, double Elevation) { return Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y + Axis * Elevation; };
    auto Lift  = [&](Vector2 Direction) { return Plane.XAxis * Direction.X + Plane.YAxis * Direction.Y; };

    BlueprintWriter Writer;
    struct LoopEdges { std::vector<int> Bottom, Top; std::vector<Orientation> Senses; };
    std::vector<LoopEdges> Rims(Profile.Loops.size());
    for (size_t LoopIndex = 0; LoopIndex < Profile.Loops.size(); ++LoopIndex)
    {
        const ProfileLoop& Loop = Profile.Loops[LoopIndex];
        const int Count = static_cast<int>(Loop.Segments.size());
        std::vector<int> Low(Count), High(Count), Rise(Count);
        for (int Index = 0; Index < Count; ++Index)
        {
            Low[Index]  = Writer.AddVertex(Place(Loop.Segments[Index].Start, Floor));
            High[Index] = Writer.AddVertex(Place(Loop.Segments[Index].Start, Floor + Height));
            Rise[Index] = Writer.AddEdge(StraightLine{ Place(Loop.Segments[Index].Start, Floor), Axis }, 0.0, Height, Low[Index], High[Index]);
        }
        LoopEdges& Rim = Rims[LoopIndex];
        for (int Index = 0; Index < Count; ++Index)
        {
            const ProfileSegment& Segment = Loop.Segments[Index];
            const int Next = (Index + 1) % Count;
            int Bottom = -1, Top = -1;
            Orientation Sense = Orientation::Forward;
            std::optional<NurbsCurve> LowRim, HighRim;
            if (Segment.Kind == ProfileSegmentCategory::Spline)
            {
                LowRim  = LiftSpline(Segment.Spline, [&](Vector2 Point) { return Place(Point, Floor); });
                HighRim = LiftSpline(Segment.Spline, [&](Vector2 Point) { return Place(Point, Floor + Height); });
                if (!LowRim || !HighRim) { Refused.Refusal = "Extrude: " + Segment.Name + " has no valid B-spline form"; return Refused; }
                const double LowTau = Segment.Spline.Knots.front(), HighTau = Segment.Spline.Knots.back();
                Bottom = Writer.AddEdge(*LowRim, LowTau, HighTau, Low[Index], Low[Next]);
                Top    = Writer.AddEdge(*HighRim, LowTau, HighTau, High[Index], High[Next]);
            }
            else if (Segment.Kind == ProfileSegmentCategory::Line)
            {
                const double  Length    = Distance(Segment.Start, Segment.End);
                const Vector3 Direction = Lift(Normalize(Segment.End - Segment.Start));
                Bottom = Writer.AddEdge(StraightLine{ Place(Segment.Start, Floor), Direction }, 0.0, Length, Low[Index], Low[Next]);
                Top    = Writer.AddEdge(StraightLine{ Place(Segment.Start, Floor + Height), Direction }, 0.0, Length, High[Index], High[Next]);
            }
            else
            {
                const bool   Counter = Segment.Sweep > 0.0;
                const double LowTau  = Counter ? Segment.StartAngle : Segment.StartAngle + Segment.Sweep;
                const double HighTau = LowTau + std::fabs(Segment.Sweep);
                Sense = Counter ? Orientation::Forward : Orientation::Reversed;
                const int First = Counter ? Index : Next, Last = Counter ? Next : Index;
                Bottom = Writer.AddEdge(CircularArc{ { Place(Segment.Centre, Floor), Plane.XAxis, Plane.YAxis, Axis }, Segment.Radius }, LowTau, HighTau, Low[First], Low[Last]);
                Top    = Writer.AddEdge(CircularArc{ { Place(Segment.Centre, Floor + Height), Plane.XAxis, Plane.YAxis, Axis }, Segment.Radius }, LowTau, HighTau, High[First], High[Last]);
            }
            Rim.Bottom.push_back(Bottom);
            Rim.Top.push_back(Top);
            Rim.Senses.push_back(Sense);

            // Side face.
            const Vector2 Middle  = EvaluateProfileSegment(Segment, 0.5);
            const Vector3 Probe   = Place(Middle, Floor + 0.5 * Height);
            const Vector3 Outward = Lift(OutwardAt(Segment, 0.5));
            LoopPlan Plan;
            const SweepFace Role{ SweepFacePlacement::Side, Segment.Name, static_cast<int>(LoopIndex) };
            if (Segment.Kind == ProfileSegmentCategory::Line)
            {
                const Vector3 Direction = Lift(Normalize(Segment.End - Segment.Start));
                const PlaneSurface Surface{ ConstructFrame(Place(Segment.Start, Floor), Cross(Direction, Axis), Direction) };
                Plan.Uses = { { Bottom, Sense }, { Rise[Next], Orientation::Forward }, { Top, Opposite(Sense) }, { Rise[Index], Orientation::Reversed } };
                Writer.AddFace(Surface, Probe, Outward, { Plan }, Role);
                continue;
            }
            if (Segment.Kind == ProfileSegmentCategory::Spline)
            {
                // Exact ruled side: poles of the rim at 𝑣 = 0 and 𝑣 = H, degree (p, 1); a closed single spline reuses
                // its one rise as the seam (𝑢 = a and 𝑢 = b), as the circle does.
                const SketchSplineCurve& Spline = Segment.Spline;
                std::vector<Vector3> Poles;
                for (const Vector2& Pole : Spline.Poles) { Poles.push_back(Place(Pole, Floor)); Poles.push_back(Place(Pole, Floor + Height)); }
                const std::optional<NurbsSurface> Surface = ConstructNurbsSurface(Spline.Degree, 1, static_cast<int>(Spline.Poles.size()), 2, Spline.Knots,
                                                                                  { 0.0, 0.0, Height, Height }, std::move(Poles), std::vector<double>(2 * Spline.Poles.size(), 1.0));
                if (!Surface) { Refused.Refusal = "Extrude: " + Segment.Name + " side has no valid NURBS form"; return Refused; }
                const double LowTau = Spline.Knots.front(), HighTau = Spline.Knots.back(), MiddleTau = 0.5 * (LowTau + HighTau);
                Plan.Uses = { { Bottom, Orientation::Forward, Vector2{ MiddleTau, 0.0 } }, { Rise[Next], Orientation::Forward, Vector2{ HighTau, 0.5 * Height } },
                              { Top, Orientation::Reversed, Vector2{ MiddleTau, Height } }, { Rise[Index], Orientation::Reversed, Vector2{ LowTau, 0.5 * Height } } };
                Writer.AddFace(*Surface, Probe, Outward, { Plan }, Role);
                continue;
            }
            if (Segment.Kind == ProfileSegmentCategory::Circle)
            {
                const CylinderSurface Surface{ { Place(Segment.Centre, Floor), Plane.XAxis, Plane.YAxis, Axis }, Segment.Radius };
                const double Up = Sense == Orientation::Forward ? FullTurn : 0.0;            // [rad] - 𝑢 of the upward seam use
                Plan.Uses = { { Bottom, Sense, Vector2{ std::numbers::pi, 0.0 } }, { Rise[Index], Orientation::Forward, Vector2{ Up, 0.5 * Height } },
                              { Top, Opposite(Sense), Vector2{ std::numbers::pi, Height } }, { Rise[Index], Orientation::Reversed, Vector2{ FullTurn - Up, 0.5 * Height } } };
                Writer.AddFace(Surface, Probe, Outward, { Plan }, Role);
                continue;
            }
            // Arc: chart seam opposite the arc midpoint so the whole side lies inside (0, 2π).
            const Vector3 Away = Lift(Normalize(Segment.Centre - Middle));
            const CylinderSurface Surface{ ConstructFrame(Place(Segment.Centre, Floor), Axis, Away), Segment.Radius };
            auto AnchorOf = [&](int Edge) { return ProjectOntoSurface(Surface, Writer.EdgeMidpoint(Edge), { std::numbers::pi, 0.5 * Height }); };
            Plan.Uses = { { Bottom, Sense, AnchorOf(Bottom) }, { Rise[Next], Orientation::Forward, AnchorOf(Rise[Next]) },
                          { Top, Opposite(Sense), AnchorOf(Top) }, { Rise[Index], Orientation::Reversed, AnchorOf(Rise[Index]) } };
            Writer.AddFace(Surface, Probe, Outward, { Plan }, Role);
        }
    }

    // Caps: one pair per outer loop with the holes it directly encloses. Without caps (an unfilled profile) the side walls
    // alone are an open sheet of zero thickness, its rims laminar.
    if (!CapsCondition)
    {
        Writer.Blueprint.Category = BodyCategory::Sheet;
        return Assemble(Partition, Writer, "Extrude");
    }
    for (size_t Outer = 0; Outer < Profile.Loops.size(); ++Outer)
    {
        if (Profile.Loops[Outer].Depth % 2 != 0) continue;
        for (int End = 0; End < 2; ++End)
        {
            const double Elevation = Floor + (End == 1 ? Height : 0.0);
            std::vector<LoopPlan> Plans;
            for (size_t LoopIndex = 0; LoopIndex < Profile.Loops.size(); ++LoopIndex)
            {
                if (LoopIndex != Outer && Profile.Loops[LoopIndex].Enclosure != static_cast<int>(Outer)) continue;
                LoopPlan Plan;
                Plan.Outer = LoopIndex == Outer;
                const LoopEdges& Rim = Rims[LoopIndex];
                for (size_t Index = 0; Index < Rim.Bottom.size(); ++Index) Plan.Uses.push_back({ End == 1 ? Rim.Top[Index] : Rim.Bottom[Index], Rim.Senses[Index] });
                Plans.push_back(Plan);
            }
            const PlaneSurface Surface{ { Place({ 0.0, 0.0 }, Elevation), Plane.XAxis, Plane.YAxis, Axis } };
            const bool OnSketch = std::fabs(Elevation) <= 1.0e-12 * Profile.Scale;           // The cap lying in the sketch plane is Start
            const SweepFace Role{ OnSketch ? SweepFacePlacement::StartCap : SweepFacePlacement::EndCap, {}, static_cast<int>(Outer) };
            Writer.AddFace(Surface, Place(Profile.Loops[Outer].Segments[0].Start, Elevation), Axis * (End == 1 ? 1.0 : -1.0), Plans, Role);
        }
    }
    return Assemble(Partition, Writer, "Extrude");
}

// 📝 Traced entry (Diagnostics/OperationTrace.h): the request on entry, the outcome before the duration line.

SweepOutcome ExtrudeProfile(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalBasis& Plane, double Extent, bool CapsCondition)
{
    FRONTIER_TRACE_SCOPE("Sweep", "Extrude · " + std::to_string(Profile.Loops.size()) + " loop(s) · extent " + TraceNumber(Extent) + (CapsCondition ? "" : " · walls"));
    SweepOutcome Outcome = ExtrudeProfileUntraced(Partition, Profile, Plane, Extent, CapsCondition);
    FRONTIER_TRACE("Sweep", Outcome.Refusal.empty() ? std::to_string(Outcome.Faces.size()) + " face(s)" : "refused: " + Outcome.Refusal);
    return Outcome;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    REVOLVE
//------------------------------------------------------------------------------------------------------------------------

static SweepOutcome RevolveProfileUntraced(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalBasis& Plane, Vector2 AxisPoint, Vector2 AxisDirection, double Angle)
{
    SweepOutcome Refused;
    if (!Profile.Refusal.empty()) { Refused.Refusal = "Revolve: " + Profile.Refusal; return Refused; }
    if (!(Angle > 1.0e-9) || Angle > FullTurn + 1.0e-12) { Refused.Refusal = "Revolve: sweep angle must lie in (0, 2π]"; return Refused; }
    const double Tolerance = 1.0e-9 * Profile.Scale;                                         // [len] - On-axis distance
    const Vector2 Along = Normalize(AxisDirection);
    Vector2 Radial = { -Along.Y, Along.X };                                                  // [-] - Sketch direction away from the axis

    // ① The profile must lie on one side of the axis (touching allowed).
    double Lowest = 1e300, Highest = -1e300;
    for (const ProfileLoop& Loop : Profile.Loops)
    {
        for (const ProfileSegment& Segment : Loop.Segments)
        {
            for (int Index = 0; Index <= 32; ++Index)
            {
                const double Offset = Dot(EvaluateProfileSegment(Segment, Index / 32.0) - AxisPoint, Radial);
                Lowest = std::min(Lowest, Offset); Highest = std::max(Highest, Offset);
            }
        }
    }
    if (Lowest < -Tolerance && Highest > Tolerance) { Refused.Refusal = "Revolve: profile crosses the axis"; return Refused; }
    if (Highest <= Tolerance) Radial = Radial * -1.0;
    const double Handedness = Along.X * Radial.Y - Along.Y * Radial.X > 0.0 ? -1.0 : 1.0;    // [-] - (r, z) vs sketch (x, y) orientation

    // ② Revolution frame: Z along the axis, X toward the profile (𝑢 = 0 holds the profile itself).
    auto Lift = [&](Vector2 Direction) { return Plane.XAxis * Direction.X + Plane.YAxis * Direction.Y; };
    const Vector3 Origin = Plane.Origin + Lift(AxisPoint);
    const Vector3 AxisZ  = Lift(Along);
    const Vector3 AxisX  = Lift(Radial);
    const Vector3 AxisY  = Cross(AxisZ, AxisX);
    auto Meridian = [&](Vector2 Point) { return Vector2{ Dot(Point - AxisPoint, Radial), Dot(Point - AxisPoint, Along) }; };   // (r, z)
    auto Place    = [&](Vector2 Point) { const Vector2 RZ = Meridian(Point); return Origin + AxisX * RZ.X + AxisZ * RZ.Y; };
    auto Circle   = [&](double Radius, double Elevation) { return CircularArc{ { Origin + AxisZ * Elevation, AxisX, AxisY, AxisZ }, Radius }; };

    // ②b Partial revolve: rotation by the sweep angle about the axis (the u = Angle copies of the profile).
    const bool   Partial     = Angle < FullTurn - 1.0e-12;                                   // [-] - Caps needed
    const double TurnCosine  = std::cos(Angle), TurnSine = std::sin(Angle);                  // [-] - Rotation by Angle
    auto TurnDirection = [&](Vector3 Direction)
    {
        const double X = Dot(Direction, AxisX), Y = Dot(Direction, AxisY), Z = Dot(Direction, AxisZ);
        return AxisX * (X * TurnCosine - Y * TurnSine) + AxisY * (X * TurnSine + Y * TurnCosine) + AxisZ * Z;
    };
    auto TurnPoint = [&](Vector3 Point) { return Origin + TurnDirection(Point - Origin); };
    auto TurnCurve = [&](const CurveGeometry& Curve) -> CurveGeometry
    {
        if (const auto* Line = std::get_if<StraightLine>(&Curve)) return StraightLine{ TurnPoint(Line->Origin), TurnDirection(Line->Direction) };
        if (const auto* Spline = std::get_if<NurbsCurve>(&Curve)) return MapNurbsCurve(*Spline->Definition, TurnPoint);   // Rigid: exact
        const auto& Arc = std::get<CircularArc>(Curve);
        return CircularArc{ { TurnPoint(Arc.Frame.Origin), TurnDirection(Arc.Frame.XAxis), TurnDirection(Arc.Frame.YAxis), TurnDirection(Arc.Frame.ZAxis) }, Arc.Radius };
    };
    const double TrackMiddle = Partial ? 0.5 * Angle : std::numbers::pi;                     // [rad] - 𝑢 of a track midpoint
    const double FarSeam     = Partial ? Angle : FullTurn;                                   // [rad] - 𝑢 of the far profile copy

    BlueprintWriter Writer;
    std::vector<std::vector<LoopUse>> StartRims(Profile.Loops.size()), EndRims(Profile.Loops.size());   // [-] - Cap boundaries (partial)
    for (size_t LoopIndex = 0; LoopIndex < Profile.Loops.size(); ++LoopIndex)
    {
        const ProfileLoop& Loop = Profile.Loops[LoopIndex];
        const int Count = static_cast<int>(Loop.Segments.size());

        // ③ Full circle off the axis → torus (closed: equator + meridian through one vertex, chart [0, 2π]²; partial:
        //    meridians at 𝑢 = 0 and 𝑢 = Angle joined by the equator track, which is the 𝑣-seam).
        if (Count == 1 && Loop.Segments[0].Kind == ProfileSegmentCategory::Circle)
        {
            const ProfileSegment& Segment = Loop.Segments[0];
            const Vector2 Centre = Meridian(Segment.Centre);
            if (Centre.X - Segment.Radius <= Tolerance) { Refused.Refusal = "Revolve: circle " + Segment.Name + " touches or crosses the axis"; return Refused; }
            const TorusSurface Surface{ { Origin + AxisZ * Centre.Y, AxisX, AxisY, AxisZ }, Centre.X, Segment.Radius };
            const Vector3 Equatorial = Origin + AxisZ * Centre.Y + AxisX * (Centre.X + Segment.Radius);   // [len] - 𝑣 = 0 point at 𝑢 = 0
            const int StartVertex = Writer.AddVertex(Equatorial);
            const int EndVertex   = Partial ? Writer.AddVertex(TurnPoint(Equatorial)) : StartVertex;
            const int Equator = Writer.AddEdge(Circle(Centre.X + Segment.Radius, Centre.Y), 0.0, Partial ? Angle : FullTurn, StartVertex, EndVertex);
            const CircularArc Meridional{ { Origin + AxisZ * Centre.Y + AxisX * Centre.X, AxisX, AxisZ, Cross(AxisX, AxisZ) }, Segment.Radius };
            const int Ring    = Writer.AddEdge(Meridional, 0.0, FullTurn, StartVertex, StartVertex);
            const int FarRing = Partial ? Writer.AddEdge(TurnCurve(Meridional), 0.0, FullTurn, EndVertex, EndVertex) : Ring;
            LoopPlan Plan;
            const double Pi = std::numbers::pi;
            Plan.Uses = { { Equator, Orientation::Forward, Vector2{ TrackMiddle, 0.0 } }, { FarRing, Orientation::Forward, Vector2{ FarSeam, Pi } },
                          { Equator, Orientation::Reversed, Vector2{ TrackMiddle, FullTurn } }, { Ring, Orientation::Reversed, Vector2{ 0.0, Pi } } };
            const Vector2 Outward = OutwardAt(Segment, 0.0);
            Writer.AddFace(Surface, Place(Segment.Start), Lift(Outward), { Plan }, { SweepFacePlacement::Side, Segment.Name, static_cast<int>(LoopIndex) });
            StartRims[LoopIndex] = { { Ring, Orientation::Forward } };
            EndRims[LoopIndex]   = { { FarRing, Orientation::Forward } };
            continue;
        }

        // ④ Vertices (every profile vertex; on-axis ones are poles / apexes) and their tracks: closed circles for a
        //    full turn, arcs [0, Angle] from the profile vertex to its turned copy otherwise.
        std::vector<int> Vertex(Count), FarVertex(Count), Ring(Count, -1);
        std::vector<Vector2> RZ(Count);
        for (int Index = 0; Index < Count; ++Index)
        {
            RZ[Index] = Meridian(Loop.Segments[Index].Start);
            const bool OnAxis = RZ[Index].X <= Tolerance;
            Vertex[Index]    = Writer.AddVertex(OnAxis ? Origin + AxisZ * RZ[Index].Y : Place(Loop.Segments[Index].Start));
            FarVertex[Index] = Partial && !OnAxis ? Writer.AddVertex(TurnPoint(Place(Loop.Segments[Index].Start))) : Vertex[Index];
            if (!OnAxis) Ring[Index] = Writer.AddEdge(Circle(RZ[Index].X, RZ[Index].Y), 0.0, Partial ? Angle : FullTurn, Vertex[Index], FarVertex[Index]);
        }

        // ⑤ One face per segment not lying on the axis.
        for (int Index = 0; Index < Count; ++Index)
        {
            const ProfileSegment& Segment = Loop.Segments[Index];
            const int     Next  = (Index + 1) % Count;
            const Vector2 Start = RZ[Index], End = RZ[Next];
            const SweepFace Role{ SweepFacePlacement::Side, Segment.Name, static_cast<int>(LoopIndex) };
            const Vector2 Middle  = EvaluateProfileSegment(Segment, 0.5);
            const Vector3 Probe   = Place(Middle);
            const Vector3 Outward = Lift(OutwardAt(Segment, 0.5));
            const bool    OnAxisLine = Segment.Kind == ProfileSegmentCategory::Line && Start.X <= Tolerance && End.X <= Tolerance;   // Sweeps nothing
            const bool    Flat       = Segment.Kind == ProfileSegmentCategory::Line && !OnAxisLine && std::fabs(Start.Y - End.Y) <= Tolerance;  // Plane ⟂ axis

            // Profile copy at 𝑢 = 0 (the seam of a full turn) and its turned copy at 𝑢 = Angle.
            SurfaceGeometry Surface;
            CurveGeometry   SeamCurve;
            double SeamLow = 0.0, SeamHigh = 0.0, StartV = 0.0, EndV = 0.0;
            Orientation SeamSense = Orientation::Forward;                                    // Seam use running Start → End
            if (Segment.Kind == ProfileSegmentCategory::Line)
            {
                const double Length = Distance(Segment.Start, Segment.End);
                SeamCurve = StraightLine{ OnAxisLine ? Origin + AxisZ * Start.Y : Place(Segment.Start), Lift(Normalize(Segment.End - Segment.Start)) };
                SeamLow = 0.0; SeamHigh = Length;
                if (OnAxisLine || Flat) {}
                else if (std::fabs(Start.X - End.X) <= Tolerance)
                {
                    Surface = CylinderSurface{ { Origin, AxisX, AxisY, AxisZ }, Start.X };
                    StartV = Start.Y; EndV = End.Y;
                }
                else
                {
                    // Chart origin at the wide end, so an apex (r = 0) is never the frame origin.
                    const double Wide = Start.X >= End.X ? Start.Y : End.Y;                  // [len] - Axial height of the wide end
                    Surface = ConeSurface{ { Origin + AxisZ * Wide, AxisX, AxisY, AxisZ }, std::max(Start.X, End.X), (End.X - Start.X) / (End.Y - Start.Y) };
                    StartV = Start.Y - Wide; EndV = End.Y - Wide;
                }
            }
            else if (Segment.Kind == ProfileSegmentCategory::Spline)
            {
                // 📝 Spline meridian: the spline lifted at 𝑢 = 0 swept by exact rational circles (ConstructNurbsRevolution),
                //    𝑢 the turn over [0, Angle], 𝑣 the spline's own τ (stored in traversal order, so the seam runs Start → End).
                const std::optional<NurbsCurve> Lifted = LiftSpline(Segment.Spline, Place);
                if (!Lifted) { Refused.Refusal = "Revolve: " + Segment.Name + " has no valid B-spline form"; return Refused; }
                Surface   = ConstructNurbsRevolution(*Lifted->Definition, { Origin, AxisX, AxisY, AxisZ }, 0.0, Partial ? Angle : FullTurn);
                SeamCurve = *Lifted;
                SeamLow   = Segment.Spline.Knots.front();
                SeamHigh  = Segment.Spline.Knots.back();
                StartV = SeamLow; EndV = SeamHigh;
            }
            else
            {
                const Vector2 Centre = Meridian(Segment.Centre);
                const double  First  = std::atan2(Start.Y - Centre.Y, Start.X - Centre.X);   // [rad] - (r, z) angle at Start
                const double  Sweep  = Segment.Sweep * Handedness;                            // [rad] - Signed (r, z) sweep
                if (Centre.X <= Tolerance)
                {
                    Surface = SphereSurface{ { Origin + AxisZ * Centre.Y, AxisX, AxisY, AxisZ }, Segment.Radius };
                }
                else
                {
                    if (Centre.X - Segment.Radius < -Tolerance) { Refused.Refusal = "Revolve: arc " + Segment.Name + " would sweep a self-intersecting torus"; return Refused; }
                    Surface = TorusSurface{ { Origin + AxisZ * Centre.Y, AxisX, AxisY, AxisZ }, Centre.X, Segment.Radius };
                }
                SeamCurve = CircularArc{ { Origin + AxisX * Centre.X + AxisZ * Centre.Y, AxisX, AxisZ, Cross(AxisX, AxisZ) }, Segment.Radius };
                SeamLow   = Sweep > 0.0 ? First : First + Sweep;
                SeamHigh  = SeamLow + std::fabs(Sweep);
                SeamSense = Sweep > 0.0 ? Orientation::Forward : Orientation::Reversed;
                StartV = First; EndV = First + Sweep;
                if (Centre.X <= Tolerance && (std::max(StartV, EndV) > 0.5 * std::numbers::pi + 1.0e-9 || std::min(StartV, EndV) < -0.5 * std::numbers::pi - 1.0e-9))
                {
                    Refused.Refusal = "Revolve: arc " + Segment.Name + " crosses the axis"; return Refused;
                }
            }
            // A full turn needs the copy only as the seam of a curved face; a partial one needs both copies for the caps.
            int Seam = -1, FarCopy = -1;                                                     // [-] - Copies at 𝑢 = 0 and 𝑢 = Angle
            if (Partial || !(OnAxisLine || Flat))
            {
                const bool Along = SeamSense == Orientation::Forward;                        // [-] - Edge runs Index → Next
                Seam    = Writer.AddEdge(SeamCurve, SeamLow, SeamHigh, Along ? Vertex[Index] : Vertex[Next], Along ? Vertex[Next] : Vertex[Index]);
                FarCopy = !Partial || OnAxisLine ? Seam
                        : Writer.AddEdge(TurnCurve(SeamCurve), SeamLow, SeamHigh, Along ? FarVertex[Index] : FarVertex[Next], Along ? FarVertex[Next] : FarVertex[Index]);
                StartRims[LoopIndex].push_back({ Seam, SeamSense });
                EndRims[LoopIndex].push_back({ FarCopy, SeamSense });
            }
            if (OnAxisLine) continue;

            // Plane ⟂ axis: disc / annulus (full turn) or annular sector (partial) bounded by the tracks.
            if (Flat)
            {
                const PlaneSurface Surface{ { Origin + AxisZ * Start.Y, AxisX, AxisY, AxisZ } };
                std::vector<LoopPlan> Plans;
                const int Larger = Start.X > End.X ? Index : Next, Smaller = Start.X > End.X ? Next : Index;
                if (!Partial)
                {
                    Plans.push_back({ { { Ring[Larger], Orientation::Forward } }, true });
                    if (Ring[Smaller] >= 0) Plans.push_back({ { { Ring[Smaller], Orientation::Forward } }, false });
                }
                else
                {
                    const Orientation Outbound = Larger == Index ? SeamSense : Opposite(SeamSense);   // Copy use Larger → Smaller
                    LoopPlan Plan;
                    Plan.Uses.push_back({ Ring[Larger], Orientation::Forward });
                    Plan.Uses.push_back({ FarCopy, Outbound });
                    if (Ring[Smaller] >= 0) Plan.Uses.push_back({ Ring[Smaller], Orientation::Reversed });
                    Plan.Uses.push_back({ Seam, Opposite(Outbound) });
                    Plans.push_back(Plan);
                }
                Writer.AddFace(Surface, Probe, Outward, Plans, Role);
                continue;
            }

            // Curved face: bounded by the two tracks and the two profile copies (one seam edge used twice when full).
            const double MiddleV = 0.5 * (StartV + EndV);
            LoopPlan Plan;
            if (Ring[Index] >= 0) Plan.Uses.push_back({ Ring[Index], Orientation::Forward, Vector2{ TrackMiddle, StartV } });
            Plan.Uses.push_back({ FarCopy, SeamSense, Vector2{ FarSeam, MiddleV } });
            if (Ring[Next] >= 0) Plan.Uses.push_back({ Ring[Next], Orientation::Reversed, Vector2{ TrackMiddle, EndV } });
            Plan.Uses.push_back({ Seam, Opposite(SeamSense), Vector2{ 0.0, MiddleV } });
            const Vector2 Seed = std::holds_alternative<NurbsSurface>(Surface) ? Vector2{ TrackMiddle, MiddleV } : Vector2{ std::numbers::pi, 0.0 };
            Writer.AddFace(Surface, Probe, Outward, { Plan }, Role, Seed);
        }
    }

    // ⑥ Partial: planar caps at 𝑢 = 0 and 𝑢 = Angle, one pair per outer loop with the holes it directly encloses.
    if (Partial)
    {
        const Vector3 FarX = TurnDirection(AxisX), FarY = TurnDirection(AxisY);              // [-] - Frame of the far cap
        for (size_t Outer = 0; Outer < Profile.Loops.size(); ++Outer)
        {
            if (Profile.Loops[Outer].Depth % 2 != 0) continue;
            for (int End = 0; End < 2; ++End)
            {
                std::vector<LoopPlan> Plans;
                for (size_t LoopIndex = 0; LoopIndex < Profile.Loops.size(); ++LoopIndex)
                {
                    if (LoopIndex != Outer && Profile.Loops[LoopIndex].Enclosure != static_cast<int>(Outer)) continue;
                    Plans.push_back({ End == 0 ? StartRims[LoopIndex] : EndRims[LoopIndex], LoopIndex == Outer });
                }
                const Vector3 CapX = End == 0 ? AxisX : FarX;
                const PlaneSurface Surface{ { Origin, CapX, AxisZ, Cross(CapX, AxisZ) } };
                const Vector3 Probe = End == 0 ? Place(Profile.Loops[Outer].Segments[0].Start) : TurnPoint(Place(Profile.Loops[Outer].Segments[0].Start));
                const SweepFace Role{ End == 0 ? SweepFacePlacement::StartCap : SweepFacePlacement::EndCap, {}, static_cast<int>(Outer) };
                Writer.AddFace(Surface, Probe, End == 0 ? AxisY * -1.0 : FarY, Plans, Role);
            }
        }
    }
    return Assemble(Partition, Writer, "Revolve");
}

SweepOutcome RevolveProfile(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalBasis& Plane, Vector2 AxisPoint, Vector2 AxisDirection, double Angle)
{
    FRONTIER_TRACE_SCOPE("Sweep", "Revolve · " + std::to_string(Profile.Loops.size()) + " loop(s) · angle " + TraceNumber(Angle));
    SweepOutcome Outcome = RevolveProfileUntraced(Partition, Profile, Plane, AxisPoint, AxisDirection, Angle);
    FRONTIER_TRACE("Sweep", Outcome.Refusal.empty() ? std::to_string(Outcome.Faces.size()) + " face(s)" : "refused: " + Outcome.Refusal);
    return Outcome;
}


std::string SweepRoleName(const SweepFace& Role)
{
    switch (Role.Role)
    {
    case SweepFacePlacement::Side:     return "Side(" + Role.Curve + ")";
    case SweepFacePlacement::StartCap: return "Cap(Start)";
    case SweepFacePlacement::EndCap:   return "Cap(End)";
    }
    return "?";
}

} // namespace Frontier::GeometrySpace::Parametric
