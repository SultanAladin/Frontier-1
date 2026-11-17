/*====================================================================================================================================
                                                         PATHSWEEP.CPP
====================================================================================================================================*/
// 🧩 Path verbs — each recognised case is rewritten as the exact primitive or sweep that produces the same solid

#include "Engine/GeometrySpace/Parametric/Feature/PathSweep.h"
#include "Engine/GeometrySpace/Parametric/Feature/SweepBlueprint.h"

#include "Engine/GeometrySpace/Parametric/Construction/PrimitiveConstruction.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/PanelAssembly.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/SectionLoft.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double AxisSlack = 1.0e-12;                                                    // [-] - Parallel test

    [[nodiscard]] SweepOutcome FromPrimitive(const EditOutcome<BodyToken>& Primitive, const char* Label)
    {
        SweepOutcome Outcome;
        if (!Primitive.SucceededCondition()) { Outcome.Refusal = std::string(Label) + ": " + Primitive.Refusal; return Outcome; }
        Outcome.Body = Primitive.Value;
        return Outcome;
    }

    [[nodiscard]] Vector3 AnyPerpendicular(Vector3 Axis) noexcept { return std::fabs(Axis.Z) > 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 0.0, 1.0 }; }

    [[nodiscard]] SweepOutcome Refuse(std::string Reason)
    {
        SweepOutcome Outcome;
        Outcome.Refusal = std::move(Reason);
        return Outcome;
    }

    constexpr int    PathSamples     = 4096;                                                 // [-] - Dense samples for length, bend and frames
    constexpr int    DeviationAround = 16;                                                   // [-] - Samples around each mid-station circle
    constexpr int    FootIterations  = 30;                                                   // [-] - Newton steps for the path foot

    // 📝 Dense path table: parameter, point, unit tangent, rotation-minimising normal and cumulative length.
    struct PathTable
    {
        std::vector<double>  Parameters = {};                                                // [-]
        std::vector<Vector3> Points     = {};                                                // [len]
        std::vector<Vector3> Tangents   = {};                                                // [-]
        std::vector<Vector3> Normals    = {};                                                // [-]
        std::vector<double>  Lengths    = {};                                                // [len]
        double               Bend       = 0.0;                                               // [1/len] - κmax
    };

    [[nodiscard]] PathTable TabulatePath(const NurbsCurveDefinition& Path)
    {
        PathTable Table;
        const NurbsDomain Domain = MeasureNurbsCurveDomain(Path);
        for (int Sample = 0; Sample <= PathSamples; ++Sample)
        {
            const double Parameter = Domain.Low + (Domain.High - Domain.Low) * Sample / PathSamples;
            const NurbsCurveDerivatives Derivatives = EvaluateNurbsCurveDerivatives(Path, Parameter);
            const double Speed = Length(Derivatives.First);
            Table.Parameters.push_back(Parameter);
            Table.Points.push_back(Derivatives.Position);
            Table.Tangents.push_back(Speed > 0.0 ? Derivatives.First * (1.0 / Speed) : Vector3{});
            if (Speed > 0.0) Table.Bend = std::max(Table.Bend, Length(Cross(Derivatives.First, Derivatives.Second)) / (Speed * Speed * Speed));
            Table.Lengths.push_back(Sample == 0 ? 0.0 : Table.Lengths.back() + Distance(Table.Points[Sample - 1], Derivatives.Position));
        }
        // ② Double reflection (Wang, Jüttler, Zheng, Liu 2008): reflect across the chord bisector, then across the
        //    plane swapping the reflected tangent into the next one — fourth-order rotation-minimising frames.
        const Vector3 First = Table.Tangents.front();
        const Vector3 Seed  = std::fabs(First.Z) < 0.9 ? Vector3{ 0.0, 0.0, 1.0 } : Vector3{ 1.0, 0.0, 0.0 };
        Table.Normals.push_back(Normalize(Seed - First * Dot(Seed, First)));
        for (int Sample = 0; Sample < PathSamples; ++Sample)
        {
            const Vector3 Chord = Table.Points[Sample + 1] - Table.Points[Sample];
            const double  ChordSquare = Dot(Chord, Chord);
            Vector3 Normal = Table.Normals.back(), Tangent = Table.Tangents[Sample];
            if (ChordSquare > 0.0)
            {
                Normal  = Normal - Chord * (2.0 / ChordSquare * Dot(Chord, Normal));
                Tangent = Tangent - Chord * (2.0 / ChordSquare * Dot(Chord, Tangent));
            }
            const Vector3 Swap = Table.Tangents[Sample + 1] - Tangent;
            const double  SwapSquare = Dot(Swap, Swap);
            if (SwapSquare > 1.0e-300) Normal = Normal - Swap * (2.0 / SwapSquare * Dot(Swap, Normal));
            const Vector3 Next = Table.Tangents[Sample + 1];
            Table.Normals.push_back(Normalize(Normal - Next * Dot(Normal, Next)));
        }
        return Table;
    }

    // Path parameter and frame normal at arc length Along (linear in the table, then the normal re-squared to the exact tangent).
    void LocateStation(const PathTable& Table, double Along, double& Parameter, Vector3& Normal)
    {
        const auto Upper = std::lower_bound(Table.Lengths.begin(), Table.Lengths.end(), Along);
        const size_t High = std::clamp<size_t>(static_cast<size_t>(Upper - Table.Lengths.begin()), 1, Table.Lengths.size() - 1), Low = High - 1;
        const double Span = Table.Lengths[High] - Table.Lengths[Low];
        const double Blend = Span > 0.0 ? std::clamp((Along - Table.Lengths[Low]) / Span, 0.0, 1.0) : 0.0;
        Parameter = Table.Parameters[Low] + (Table.Parameters[High] - Table.Parameters[Low]) * Blend;
        Normal    = Table.Normals[Low] * (1.0 - Blend) + Table.Normals[High] * Blend;
    }

    // Foot of Point on the path by Newton on (C(τ) − X)·C′(τ) = 0 from Seed, clamped to the domain.
    [[nodiscard]] double FindPathFoot(const NurbsCurveDefinition& Path, Vector3 Point, double Seed)
    {
        const NurbsDomain Domain = MeasureNurbsCurveDomain(Path);
        double Parameter = Seed;
        for (int Iteration = 0; Iteration < FootIterations; ++Iteration)
        {
            const NurbsCurveDerivatives Derivatives = EvaluateNurbsCurveDerivatives(Path, Parameter);
            const Vector3 Offset = Derivatives.Position - Point;
            const double  Slope  = Dot(Derivatives.First, Derivatives.First) + Dot(Offset, Derivatives.Second);
            if (!(std::fabs(Slope) > 0.0)) break;
            const double Step = Dot(Offset, Derivatives.First) / Slope;
            Parameter = std::clamp(Parameter - Step, Domain.Low, Domain.High);
            if (std::fabs(Step) <= 1.0e-15 * (Domain.High - Domain.Low)) break;
        }
        return Parameter;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SweepOutcome PipeAlongPath(ModelPartition& Partition, const CurveGeometry& Path, double Low, double High, double Radius)
{
    if (!(Radius > 0.0)) return Refuse("Pipe: radius must be positive");
    if (!(High > Low)) return Refuse("Pipe: empty path range");
    if (const auto* Line = std::get_if<StraightLine>(&Path))
    {
        const Vector3 Start = Line->Origin + Line->Direction * Low;                          // [len] - Path start
        return FromPrimitive(ConstructCylinder(Partition, ConstructFrame(Start, Line->Direction, AnyPerpendicular(Line->Direction)), Radius, High - Low), "Pipe");
    }
    if (const auto* Arc = std::get_if<CircularArc>(&Path))
    {
        if (Radius >= Arc->Radius) return Refuse("Pipe: section radius reaches the arc axis (self-intersecting torus)");
        if (High - Low > 2.0 * std::numbers::pi + 1.0e-12) return Refuse("Pipe: arc range exceeds a full turn");
        // 📝 Sketch plane through the arc centre spanned by the radial direction at Low (x) and the arc axis (y): the
        //    section is the circle at (R, 0), revolved about the sketch y-axis — the revolve turns toward Z × radial,
        //    which is the arc's direction of increasing τ.
        const Vector3 Radial = Arc->Frame.XAxis * std::cos(Low) + Arc->Frame.YAxis * std::sin(Low);   // [-] - Centre → path start
        const OrthonormalFrame Plane{ Arc->Frame.Origin, Radial, Arc->Frame.ZAxis, Cross(Radial, Arc->Frame.ZAxis) };
        SketchProfile Section;
        ProfileSegment Circle;
        Circle.Kind   = ProfileSegmentKind::Circle;
        Circle.Centre = { Arc->Radius, 0.0 };
        Circle.Radius = Radius;
        Circle.Sweep  = 2.0 * std::numbers::pi;
        Circle.Start  = Circle.End = { Arc->Radius + Radius, 0.0 };
        Circle.Name   = "Section";
        ProfileLoop Loop;
        Loop.Segments   = { Circle };
        Loop.SignedArea = std::numbers::pi * Radius * Radius;
        Section.Loops    = { Loop };
        Section.Area     = Loop.SignedArea;
        Section.Centroid = Circle.Centre;
        Section.Scale    = Arc->Radius + Radius;
        SweepOutcome Outcome = RevolveProfile(Partition, Section, Plane, { 0.0, 0.0 }, { 0.0, 1.0 }, High - Low);
        if (!Outcome.Refusal.empty()) Outcome.Refusal = "Pipe: " + Outcome.Refusal;
        return Outcome;
    }
    return Refuse(std::string("Pipe: 🚧 path kind ") + CurveCategoryLabel(Path) + " (lines and arcs are exact)");
}

PipeOutcome PipeAlongCurve(ModelPartition& Partition, const NurbsCurve& Path, double Radius, const PipeSettings& Settings)
{
    PipeOutcome Outcome;
    if (!Path.Definition) { Outcome.Refusal = "Pipe: the path has no carrier"; return Outcome; }
    if (!(Radius > 0.0)) { Outcome.Refusal = "Pipe: radius must be positive"; return Outcome; }
    const NurbsCurveDefinition& Definition = *Path.Definition;
    const PathTable Table = TabulatePath(Definition);
    Outcome.Length = Table.Lengths.back();
    Outcome.Bend   = Radius * Table.Bend;
    if (!(Outcome.Length > 0.0)) { Outcome.Refusal = "Pipe: the path has no length"; return Outcome; }
    if (Distance(Table.Points.front(), Table.Points.back()) <= 1.0e-9 * Outcome.Length) { Outcome.Refusal = "Pipe: the path is closed (🚧 closed tubes; open the curve or pipe a part of it)"; return Outcome; }
    for (const Vector3& Tangent : Table.Tangents) if (!(Length(Tangent) > 0.0)) { Outcome.Refusal = "Pipe: the path has a cusp (zero speed)"; return Outcome; }
    if (Outcome.Bend >= 1.0)
    {
        Outcome.Refusal = "Pipe: radius " + std::to_string(Radius) + " exceeds the tightest bend radius " + std::to_string(1.0 / Table.Bend) + " (the tube would fold over itself)";
        return Outcome;
    }
    const double Tolerance = Settings.ToleranceRatio * Radius;                               // [len] - Allowed radial deviation

    for (int Count = std::max(Settings.FirstSections, 2); Count <= Settings.SectionLimit; Count = 2 * Count - 1)
    {
        // ① Section circles at equal arc-length stations, frames from the rotation-minimising table.
        std::vector<NurbsCurve> Sections;
        std::vector<double>     Stations;
        for (int Station = 0; Station < Count; ++Station)
        {
            double  Parameter = 0.0;
            Vector3 Normal;
            LocateStation(Table, Outcome.Length * Station / (Count - 1), Parameter, Normal);
            const NurbsCurveDerivatives Derivatives = EvaluateNurbsCurveDerivatives(Definition, Parameter);
            const Vector3 Tangent = Normalize(Derivatives.First);
            Normal = Normalize(Normal - Tangent * Dot(Normal, Tangent));
            const Vector3 Binormal = Cross(Tangent, Normal);
            Sections.push_back(ConvertConicToNurbs({ Derivatives.Position, Normal * Radius, Binormal * Radius, Tangent }, 0.0, 2.0 * std::numbers::pi));
            Stations.push_back(Parameter);
        }
        LoftSettings Loft;
        Loft.DegreeAcross   = 3;
        Loft.AlignCondition = false;
        const LoftSurfaceOutcome Skin = LoftSectionSurface(Sections, Loft);
        if (!Skin.Refusal.empty()) { Outcome.Refusal = "Pipe: " + Skin.Refusal; return Outcome; }

        // ② Radial deviation midway between stations (where the skin is furthest from its interpolated circles).
        double Worst = 0.0;
        const NurbsDomain AroundDomain = MeasureNurbsSurfaceDomain(*Skin.Surface.Definition, true);
        for (int Station = 0; Station + 1 < Count; ++Station)
        {
            const double Across = 0.5 * (Skin.SectionParameters[Station] + Skin.SectionParameters[Station + 1]);
            const double Seed   = 0.5 * (Stations[Station] + Stations[Station + 1]);
            for (int Around = 0; Around < DeviationAround; ++Around)
            {
                const double  AroundParameter = AroundDomain.Low + (AroundDomain.High - AroundDomain.Low) * (Around + 0.5) / DeviationAround;
                const Vector3 Point = EvaluateNurbsSurface(Skin.Surface, { AroundParameter, Across }).Position;
                const double  Foot  = FindPathFoot(Definition, Point, Seed);
                Worst = std::max(Worst, std::fabs(Distance(Point, EvaluateNurbsCurveDerivatives(Definition, Foot).Position) - Radius));
            }
        }
        Outcome.Sections  = Count;
        Outcome.Deviation = Worst;
        Outcome.Surface   = Skin.Surface;
        if (Worst > Tolerance) continue;

        // ③ One NURBS side, plane caps on the two rim circles → closed solid.
        PanelAssemblySettings Assembly;
        Assembly.CapCondition = true;
        const PanelAssemblyOutcome Assembled = AssemblePanels(Partition, { SurfacePanel{ Skin.Surface } }, Assembly, "Pipe");
        if (!Assembled.Refusal.empty()) { Outcome.Refusal = "Pipe: " + Assembled.Refusal; return Outcome; }
        if (!Assembled.SolidCondition || Assembled.Faces.size() != 1 || Assembled.Caps.size() != 2) { Outcome.Refusal = "Pipe: the tube did not close (" + std::to_string(Assembled.LaminarEdges) + " open rim edges)"; return Outcome; }
        Outcome.Body = Assembled.Body;
        Outcome.Side = Assembled.Faces.front();
        Outcome.Caps = Assembled.Caps;
        return Outcome;
    }
    Outcome.Refusal = "Pipe: " + std::to_string(Settings.SectionLimit) + " sections still deviate " + std::to_string(Outcome.Deviation) + " from the true tube (allowed " + std::to_string(Tolerance) + ")";
    return Outcome;
}

EvolutionOutcome EvolveProfile(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalFrame& Plane, const NurbsCurve& Path, const EvolutionSettings& Settings)
{
    using namespace Sweeping;
    EvolutionOutcome Outcome;
    const auto Refused = [&](const std::string& Reason) { Outcome.Refusal = "Evolution: " + Reason; return Outcome; };
    if (!Profile.Refusal.empty()) return Refused(Profile.Refusal);
    if (!Path.Definition) return Refused("the path has no carrier");
    const NurbsCurveDefinition& Definition = *Path.Definition;
    const PathTable Table = TabulatePath(Definition);
    Outcome.Length = Table.Lengths.back();
    if (!(Outcome.Length > 0.0)) return Refused("the path has no length");
    if (Distance(Table.Points.front(), Table.Points.back()) <= 1.0e-9 * Outcome.Length) return Refused("the path is closed (🚧 closed sweeps; open the curve or sweep along a part of it)");
    for (const Vector3& Tangent : Table.Tangents) if (!(Length(Tangent) > 0.0)) return Refused("the path has a cusp (zero speed)");
    const auto Lift3 = [&](Vector2 Point) { return Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y; };
    const auto Lift  = [&](Vector2 Direction) { return Plane.XAxis * Direction.X + Plane.YAxis * Direction.Y; };

    // ① Rigid frame map at an arc length: x ↦ C(s) + F(s)·F(0)ᵀ·(x − C(0)), F = [T N B] from the rotation-minimising table.
    struct FrameMap { Vector3 Point, Tangent, Normal, Binormal; };
    const auto FrameAt = [&](double Along)
    {
        double  Parameter = 0.0;
        Vector3 Normal;
        LocateStation(Table, Along, Parameter, Normal);
        const NurbsCurveDerivatives Derivatives = EvaluateNurbsCurveDerivatives(Definition, Parameter);
        const Vector3 Tangent = Normalize(Derivatives.First);
        Normal = Normalize(Normal - Tangent * Dot(Normal, Tangent));
        return FrameMap{ Derivatives.Position, Tangent, Normal, Cross(Tangent, Normal) };
    };
    const FrameMap Start = FrameAt(0.0);
    const auto Turn  = [&](const FrameMap& Frame, Vector3 Direction)
    {
        return Frame.Tangent * Dot(Direction, Start.Tangent) + Frame.Normal * Dot(Direction, Start.Normal) + Frame.Binormal * Dot(Direction, Start.Binormal);
    };
    const auto Carry = [&](const FrameMap& Frame, Vector3 Point) { return Frame.Point + Turn(Frame, Point - Start.Point); };

    // ② The path must leave the profile plane, and no profile point may reach the tightest bend radius.
    const double Facing = Dot(Start.Tangent, Plane.ZAxis);                                   // [-] - cos(path, plane normal)
    if (std::fabs(Facing) < 1.0e-3) return Refused("the path starts along the profile plane (it must leave the plane)");
    double Reach = 0.0;                                                                      // [len] - Farthest profile point from the path start
    for (const ProfileLoop& Loop : Profile.Loops)
    {
        for (const ProfileSegment& Segment : Loop.Segments)
        {
            for (int Index = 0; Index <= 32; ++Index) Reach = std::max(Reach, Distance(Lift3(EvaluateProfileSegment(Segment, Index / 32.0)), Start.Point));
        }
    }
    Outcome.Bend = Reach * Table.Bend;
    if (Outcome.Bend >= 1.0)
    {
        return Refused("the profile reaches " + std::to_string(Reach) + " from the path start, beyond the tightest bend radius " + std::to_string(1.0 / Table.Bend) + " (the sweep would fold over itself)");
    }

    // ③ Every profile curve as an exact NURBS section in traversal order (clockwise arcs: mirrored minor axis).
    struct SideSource { const ProfileSegment* Segment; NurbsCurve Section; };
    std::vector<std::vector<SideSource>> Sources(Profile.Loops.size());
    for (size_t LoopIndex = 0; LoopIndex < Profile.Loops.size(); ++LoopIndex)
    {
        for (const ProfileSegment& Segment : Profile.Loops[LoopIndex].Segments)
        {
            std::optional<NurbsCurve> Section;
            if (Segment.Kind == ProfileSegmentKind::Line)
            {
                const double Span = Distance(Segment.Start, Segment.End);
                Section = ConvertLineToNurbs(StraightLine{ Lift3(Segment.Start), Lift(Normalize(Segment.End - Segment.Start)) }, 0.0, Span);
            }
            else if (Segment.Kind == ProfileSegmentKind::Spline) Section = LiftSpline(Segment.Spline, Lift3);
            else
            {
                const double Sign = Segment.Sweep > 0.0 ? 1.0 : -1.0;
                const ConicForm Conic{ Lift3(Segment.Centre), Plane.XAxis * Segment.Radius, Plane.YAxis * (Sign * Segment.Radius), Plane.ZAxis * Sign };
                const double Low = Sign * Segment.StartAngle;
                Section = ConvertConicToNurbs(Conic, Low, Low + std::fabs(Segment.Sweep));
            }
            if (!Section || !Section->Definition) return Refused(Segment.Name + " has no valid NURBS form");
            Sources[LoopIndex].push_back({ &Segment, *Section });
        }
    }
    const double Tolerance = Settings.ToleranceRatio * Profile.Scale;                       // [len] - Allowed skin deviation

    for (int Count = std::max(Settings.FirstSections, 2); Count <= Settings.SectionLimit; Count = 2 * Count - 1)
    {
        // ④ Stations at equal arc length; one fixed 𝑣-list for every side so shared rails coincide.
        std::vector<FrameMap> Frames;
        std::vector<double>   Parameters;
        for (int Station = 0; Station < Count; ++Station)
        {
            Frames.push_back(FrameAt(Outcome.Length * Station / (Count - 1)));
            Parameters.push_back(Station == Count - 1 ? 1.0 : static_cast<double>(Station) / (Count - 1));
        }
        LoftSettings Loft;
        Loft.DegreeAcross   = 3;
        Loft.AlignCondition = false;
        Loft.Parameters     = Parameters;
        std::vector<std::vector<NurbsSurface>> Skins(Profile.Loops.size());
        double Worst = 0.0;
        for (size_t LoopIndex = 0; LoopIndex < Sources.size(); ++LoopIndex)
        {
            for (const SideSource& Source : Sources[LoopIndex])
            {
                std::vector<NurbsCurve> Sections;
                for (const FrameMap& Frame : Frames) Sections.push_back(MapNurbsCurve(*Source.Section.Definition, [&](Vector3 Point) { return Carry(Frame, Point); }));
                const LoftSurfaceOutcome Skin = LoftSectionSurface(Sections, Loft);
                if (!Skin.Refusal.empty()) return Refused(Source.Segment->Name + ": " + Skin.Refusal);
                Skins[LoopIndex].push_back(Skin.Surface);
                // ⑤ Deviation midway between stations against the rigidly carried section (𝑢 maps linearly onto τ).
                const NurbsDomain Domain = MeasureNurbsCurveDomain(*Source.Section.Definition);
                for (int Station = 0; Station + 1 < Count; ++Station)
                {
                    const FrameMap Middle = FrameAt(Outcome.Length * (Station + 0.5) / (Count - 1));
                    const double   Across = 0.5 * (Parameters[Station] + Parameters[Station + 1]);
                    for (int Around = 0; Around <= 8; ++Around)
                    {
                        const double U = Around / 8.0;
                        const Vector3 Exact = Carry(Middle, EvaluateNurbsCurve(*Source.Section.Definition, Domain.Low + U * (Domain.High - Domain.Low)).Position);
                        Worst = std::max(Worst, Distance(EvaluateNurbsSurface(Skin.Surface, { U, Across }).Position, Exact));
                    }
                }
            }
        }
        Outcome.Sections  = Count;
        Outcome.Deviation = Worst;
        if (Worst > Tolerance) continue;

        // ⑥ Topology: per profile vertex a start / end vertex and a rail (the 𝑢 = 0 iso of the side it starts), per
        //    curve a bottom (𝑣 = 0) and top (𝑣 = 1) edge; a closed single curve uses its rail twice as the seam.
        const FrameMap Last = Frames.back();
        BlueprintWriter Writer;
        struct LoopEdges { std::vector<int> Bottom, Top; };
        std::vector<LoopEdges> Rims(Profile.Loops.size());
        for (size_t LoopIndex = 0; LoopIndex < Profile.Loops.size(); ++LoopIndex)
        {
            const ProfileLoop& Loop = Profile.Loops[LoopIndex];
            const int Pieces = static_cast<int>(Loop.Segments.size());
            std::vector<int> Low(Pieces), High(Pieces), Rail(Pieces);
            for (int Index = 0; Index < Pieces; ++Index)
            {
                const Vector3 Corner = Lift3(Loop.Segments[Index].Start);
                Low[Index]  = Writer.AddVertex(Corner);
                High[Index] = Writer.AddVertex(Carry(Last, Corner));
                Rail[Index] = Writer.AddEdge(ExtractNurbsIsoCurve(Skins[LoopIndex][Index], true, 0.0), 0.0, 1.0, Low[Index], High[Index]);
            }
            for (int Index = 0; Index < Pieces; ++Index)
            {
                const ProfileSegment& Segment = Loop.Segments[Index];
                const NurbsSurface&   Skin    = Skins[LoopIndex][Index];
                const int Next = (Index + 1) % Pieces;
                const int Bottom = Writer.AddEdge(ExtractNurbsIsoCurve(Skin, false, 0.0), 0.0, 1.0, Low[Index], Low[Next]);
                const int Top    = Writer.AddEdge(ExtractNurbsIsoCurve(Skin, false, 1.0), 0.0, 1.0, High[Index], High[Next]);
                Rims[LoopIndex].Bottom.push_back(Bottom);
                Rims[LoopIndex].Top.push_back(Top);
                LoopPlan Plan;
                Plan.Uses = { { Bottom, Orientation::Forward, Vector2{ 0.5, 0.0 } }, { Rail[Next], Orientation::Forward, Vector2{ 1.0, 0.5 } },
                              { Top, Orientation::Reversed, Vector2{ 0.5, 1.0 } }, { Rail[Index], Orientation::Reversed, Vector2{ 0.0, 0.5 } } };
                const FrameMap Middle  = FrameAt(0.5 * Outcome.Length);
                const Vector3  Outward = Turn(Middle, Lift(OutwardAt(Segment, 0.5)));
                Writer.AddFace(Skin, EvaluateNurbsSurface(Skin, { 0.5, 0.5 }).Position, Outward, { Plan }, { SweepFaceRole::Side, Segment.Name, static_cast<int>(LoopIndex) }, Vector2{ 0.5, 0.5 });
            }
        }

        // ⑦ Caps: the profile plane and its rigid image, one pair per outer loop with the holes it directly encloses.
        const double Ahead = Facing > 0.0 ? 1.0 : -1.0;                                     // [-] - Path side of the plane
        for (size_t Outer = 0; Outer < Profile.Loops.size(); ++Outer)
        {
            if (Profile.Loops[Outer].Depth % 2 != 0) continue;
            for (int End = 0; End < 2; ++End)
            {
                std::vector<LoopPlan> Plans;
                for (size_t LoopIndex = 0; LoopIndex < Profile.Loops.size(); ++LoopIndex)
                {
                    if (LoopIndex != Outer && Profile.Loops[LoopIndex].Enclosure != static_cast<int>(Outer)) continue;
                    LoopPlan Plan;
                    Plan.Outer = LoopIndex == Outer;
                    for (int Edge : End == 0 ? Rims[LoopIndex].Bottom : Rims[LoopIndex].Top) Plan.Uses.push_back({ Edge, Orientation::Forward });
                    Plans.push_back(Plan);
                }
                const OrthonormalFrame CapFrame = End == 0 ? Plane : OrthonormalFrame{ Carry(Last, Plane.Origin), Turn(Last, Plane.XAxis), Turn(Last, Plane.YAxis), Turn(Last, Plane.ZAxis) };
                const Vector3 Corner = Lift3(Profile.Loops[Outer].Segments[0].Start);
                const Vector3 Probe  = End == 0 ? Corner : Carry(Last, Corner);
                const SweepFace Role{ End == 0 ? SweepFaceRole::StartCap : SweepFaceRole::EndCap, {}, static_cast<int>(Outer) };
                Writer.AddFace(PlaneSurface{ CapFrame }, Probe, CapFrame.ZAxis * (End == 0 ? -Ahead : Ahead), Plans, Role);
            }
        }
        Outcome.Sweep = Assemble(Partition, Writer, "Evolution");
        if (!Outcome.Sweep.Refusal.empty()) return Refused(Outcome.Sweep.Refusal);
        return Outcome;
    }
    return Refused(std::to_string(Settings.SectionLimit) + " stations still deviate " + std::to_string(Outcome.Deviation) + " from the true sweep (allowed " + std::to_string(Tolerance) + ")");
}

SweepOutcome SweepProfileAlongLine(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalFrame& Plane, Vector3 Displacement)
{
    const double Distance = Length(Displacement);                                            // [len] - |d|
    if (!(Distance > 0.0)) return Refuse("Sweep: zero displacement");
    if (Length(Cross(Displacement * (1.0 / Distance), Plane.ZAxis)) > AxisSlack) return Refuse("Sweep: 🚧 oblique sweep (path not ⟂ the profile plane)");
    SweepOutcome Outcome = ExtrudeProfile(Partition, Profile, Plane, Dot(Displacement, Plane.ZAxis));
    if (!Outcome.Refusal.empty()) Outcome.Refusal = "Sweep: " + Outcome.Refusal;
    return Outcome;
}

SweepOutcome LoftCircles(ModelPartition& Partition, Vector3 FirstCentre, Vector3 FirstNormal, double FirstRadius, Vector3 SecondCentre, Vector3 SecondNormal, double SecondRadius)
{
    const Vector3 Join = SecondCentre - FirstCentre;                                         // [len] - Centre line
    const double  Height = Length(Join);                                                     // [len] - Loft length
    if (!(Height > 0.0)) return Refuse("Loft: coincident section centres");
    const Vector3 Axis = Join * (1.0 / Height);
    if (Length(Cross(Normalize(FirstNormal), Axis)) > AxisSlack || Length(Cross(Normalize(SecondNormal), Axis)) > AxisSlack)
    {
        return Refuse("Loft: 🚧 non-coaxial sections need a skinned surface");
    }
    const OrthonormalFrame Frame = ConstructFrame(FirstCentre, Axis, AnyPerpendicular(Axis));
    if (std::fabs(FirstRadius - SecondRadius) <= 1.0e-12 * std::max(FirstRadius, SecondRadius)) return FromPrimitive(ConstructCylinder(Partition, Frame, FirstRadius, Height), "Loft");
    return FromPrimitive(ConstructFrustum(Partition, Frame, FirstRadius, SecondRadius, Height), "Loft");
}

} // namespace Frontier::GeometrySpace::Parametric
