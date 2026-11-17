/*====================================================================================================================================
                                                       BODYASSEMBLY.CPP
====================================================================================================================================*/
// 🧩 Direct assembly of a solid body from a blueprint of vertices, edges with carriers and faces with SP-curved loops

#include "Engine/GeometrySpace/Parametric/Construction/BodyAssembly.h"

#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"
#include "Engine/GeometrySpace/Parametric/Containment/WindingNumber.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"

#include <algorithm>
#include <cmath>

#include <functional>
#include <limits>
#include <map>
#include <numeric>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

AssemblyOutcome AssembleBody(ModelPartition& Partition, const BodyBlueprint& Blueprint, const std::string& Label)
{
    AssemblyOutcome Outcome;
    if (Blueprint.Faces.empty()) { Outcome.Refusal = Label + ": empty result"; return Outcome; }

    // ① Manifold check: every used edge is used exactly twice, in opposite directions (a sheet also allows one use: a
    //    laminar edge on its boundary).
    const bool SheetCondition = Blueprint.Category == BodyCategory::Sheet;
    std::vector<int> UseCount(Blueprint.Edges.size(), 0), Balance(Blueprint.Edges.size(), 0);
    for (const BlueprintFace& Face : Blueprint.Faces)
    {
        for (const auto& Loop : Face.Loops)
        {
            for (const BlueprintUse& Use : Loop)
            {
                if (Use.Edge < 0 || Use.Edge >= static_cast<int>(Blueprint.Edges.size()))
                {
                    Outcome.Refusal = Label + ": use of a missing edge " + std::to_string(Use.Edge);
                    return Outcome;
                }
                ++UseCount[Use.Edge];
                Balance[Use.Edge] += Use.Sense == Orientation::Forward ? 1 : -1;
            }
        }
    }
    for (size_t Edge = 0; Edge < Blueprint.Edges.size(); ++Edge)
    {
        const bool Laminar = SheetCondition && UseCount[Edge] == 1;
        if (UseCount[Edge] != 0 && !Laminar && (UseCount[Edge] != 2 || Balance[Edge] != 0))
        {
            Outcome.Refusal = Label + ": non-manifold or open result (pave block " + std::to_string(Edge) + " used " + std::to_string(UseCount[Edge]) + " times)";
            return Outcome;
        }
    }

    // ② Shells = connected components of faces through shared edges.
    std::vector<int> Component(Blueprint.Faces.size());
    std::iota(Component.begin(), Component.end(), 0);
    const std::function<int(int)> Root = [&](int Index) { return Component[Index] == Index ? Index : Component[Index] = Root(Component[Index]); };
    std::map<int, int> FirstUser;
    for (size_t Slot = 0; Slot < Blueprint.Faces.size(); ++Slot)
    {
        for (const auto& Loop : Blueprint.Faces[Slot].Loops)
        {
            for (const BlueprintUse& Use : Loop)
            {
                const auto Found = FirstUser.find(Use.Edge);
                if (Found == FirstUser.end()) FirstUser[Use.Edge] = static_cast<int>(Slot);
                else Component[Root(static_cast<int>(Slot))] = Root(Found->second);
            }
        }
    }

    // ③ Records: body, vertices, edges (carrier copies), shells, faces, loops, fins — issued on first use.
    const BodyToken   Body     = Partition.Bodies.Issue({ Blueprint.Category, {} });
    const RegionToken Exterior = Partition.Regions.Issue({ Body, RegionMaterial::Void, {} });
    std::map<int, VertexToken> Vertices;
    Outcome.Edges.assign(Blueprint.Edges.size(), EdgeToken{});
    const auto IssueVertex = [&](int Index)
    {
        const auto Found = Vertices.find(Index);
        if (Found != Vertices.end()) return Found->second;
        return Vertices[Index] = Partition.Vertices.Issue({ Blueprint.Vertices[Index].Position, Blueprint.Vertices[Index].Precision });
    };
    const auto IssueEdge = [&](int Index)
    {
        if (Outcome.Edges[Index].PresentCondition()) return Outcome.Edges[Index];
        const BlueprintEdge& Block = Blueprint.Edges[Index];
        const EdgeToken      Edge  = Partition.Edges.Issue({ {}, IssueVertex(Block.Start), IssueVertex(Block.End), Block.Low, Block.High, Block.Precision, {} });
        Partition.AttachCurve(Edge, Block.Curve, Block.Low, Block.High);
        Outcome.EdgeOrder.push_back(Index);
        return Outcome.Edges[Index] = Edge;
    };
    std::map<int, SolidShellToken> Shells;
    std::vector<SolidShellToken>   ShellOrder;
    for (size_t Slot = 0; Slot < Blueprint.Faces.size(); ++Slot)
    {
        const int Group = Root(static_cast<int>(Slot));
        if (Shells.count(Group) == 0)
        {
            Shells[Group] = Partition.Shells.Issue({ {}, {} });
            ShellOrder.push_back(Shells[Group]);
        }
        const SolidShellToken SolidShell = Shells[Group];
        const BlueprintFace&  Planned    = Blueprint.Faces[Slot];
        const FaceToken       Face       = Partition.Faces.Issue({ SolidShell, {}, Orientation::Forward, {} });
        Partition.AttachSurface(Face, Planned.Surface, Planned.Sense);
        Partition.Shells.Resolve(SolidShell).Faces.push_back(Face);
        Outcome.Faces.push_back(Face);
        for (const auto& LoopUses : Planned.Loops)
        {
            if (LoopUses.empty()) continue;
            const LoopToken Loop = Partition.Loops.Issue({ Face, {}, {} });
            Partition.Faces.Resolve(Face).Loops.push_back(Loop);
            std::vector<FinToken> Fins;
            for (const BlueprintUse& Use : LoopUses)
            {
                const EdgeToken Edge = IssueEdge(Use.Edge);
                const FinToken  Fin  = Partition.Fins.Issue({ Loop, Edge, Use.Sense, {}, {}, {}, Use.SurfaceCurve });
                EdgeProfile& EdgeRecord = Partition.Edges.Resolve(Edge);
                if (!EdgeRecord.RadialFin.PresentCondition())
                {
                    EdgeRecord.RadialFin                        = Fin;
                    Partition.Fins.Resolve(Fin).RadialSuccessor = Fin;                       // One-fin ring until a mate arrives
                }
                else
                {
                    Partition.Fins.Resolve(Fin).RadialSuccessor                 = EdgeRecord.RadialFin;
                    Partition.Fins.Resolve(EdgeRecord.RadialFin).RadialSuccessor = Fin;
                }
                Fins.push_back(Fin);
            }
            for (size_t Index = 0; Index < Fins.size(); ++Index)
            {
                Partition.Fins.Resolve(Fins[Index]).Successor                      = Fins[(Index + 1) % Fins.size()];
                Partition.Fins.Resolve(Fins[(Index + 1) % Fins.size()]).Predecessor = Fins[Index];
            }
            Partition.Loops.Resolve(Loop).FirstFin = Fins.front();
        }
    }

    // ④ Regions: a sheet's shells bound no volume and live in the exterior void (Parasolid sheet bodies).
    if (SheetCondition)
    {
        Partition.Regions.Resolve(Exterior).Shells = ShellOrder;
        for (SolidShellToken SolidShell : ShellOrder) Partition.Shells.Resolve(SolidShell).Region = Exterior;
        Partition.Bodies.Resolve(Body).Regions = { Exterior };
        Outcome.Body   = Body;
        Outcome.Shells = static_cast<int>(ShellOrder.size());
        return Outcome;
    }

    //    Solid: outer shells (positive volume) → solid regions; cavities → the smallest solid region containing them.
    //    ⚠️ One volume per shell: a procedural carrier's flux is the costliest integral here, so it is never taken twice.
    std::vector<SolidShellToken> OuterShells, CavityShells;
    std::vector<double>          OuterVolumes;
    // 📝 Exact shell volumes cost a Green integration over every SP-curve; a caller that already assembled this very
    //    blueprint (STEP import assembles in a scratch partition first) hands them back instead of paying twice.
    const bool KnownVolumesCondition = Blueprint.ShellVolumes.size() == ShellOrder.size();
    for (size_t Index = 0; Index < ShellOrder.size(); ++Index)
    {
        const SolidShellToken SolidShell = ShellOrder[Index];
        const double Volume = KnownVolumesCondition ? Blueprint.ShellVolumes[Index] : IntegrateShellVolume(Partition, SolidShell);  // [len³] - Signed
        Outcome.ShellVolumes.push_back(Volume);
        if (Volume > 0.0) { OuterShells.push_back(SolidShell); OuterVolumes.push_back(Volume); }
        else CavityShells.push_back(SolidShell);
    }
    std::vector<RegionToken> Regions = { Exterior };
    for (SolidShellToken SolidShell : OuterShells)
    {
        const RegionToken Region = Partition.Regions.Issue({ Body, RegionMaterial::Solid, { SolidShell } });
        Partition.Shells.Resolve(SolidShell).Region = Region;
        Regions.push_back(Region);
    }
    for (SolidShellToken Cavity : CavityShells)
    {
        const FaceToken AnyFace  = Partition.Shells.Resolve(Cavity).Faces.front();
        const FinToken  AnyFin   = Partition.Loops.Resolve(Partition.Faces.Resolve(AnyFace).Loops.front()).FirstFin;
        const Vector3   Witness  = Partition.Vertices.Resolve(Partition.FinStart(AnyFin)).Position;
        int    Owner       = -1;
        double OwnerVolume = std::numeric_limits<double>::max();
        for (size_t Index = 0; Index < OuterShells.size(); ++Index)
        {
            const BodyContainment Containment = PrepareFaceContainment(Partition, Partition.Shells.Resolve(OuterShells[Index]).Faces);
            // 💡 Screened winding: a cavity's owner test ran the strict quadrature (51–176 ms per Boolean once a body has a void).
            if (ClassifySolidPointScreened(Containment, Witness).Membership == PointMembership::Inside && OuterVolumes[Index] < OwnerVolume)
            {
                Owner       = static_cast<int>(Index);
                OwnerVolume = OuterVolumes[Index];
            }
        }
        if (Owner < 0) { Outcome.Refusal = Label + ": a cavity shell lies in no solid region"; return Outcome; }
        const RegionToken Region = Regions[Owner + 1];
        Partition.Regions.Resolve(Region).Shells.push_back(Cavity);
        Partition.Shells.Resolve(Cavity).Region = Region;
    }
    Partition.Bodies.Resolve(Body).Regions = Regions;
    Outcome.Body   = Body;
    Outcome.Solids = static_cast<int>(OuterShells.size());
    Outcome.Shells = static_cast<int>(ShellOrder.size());
    return Outcome;
}

void FitFaceSurfaceCurves(const BodyBlueprint& Blueprint, BlueprintFace& Face)
{
    constexpr int            Steps  = 24;
    const SurfacePeriodicity Period = ResolvePeriodicity(Face.Surface);
    const double             Signum = Face.Sense == Orientation::Forward ? 1.0 : -1.0;

    // ① Continuous chart track of every use, in loop order; anchor and chart direction at the middle sample.
    struct UseTrack
    {
        std::vector<Vector2> Samples   = {};                                                 // [-] - Chart images along the use
        Vector2              Anchor    = {};                                                 // [-] - Middle image
        std::optional<ParameterLine> ExactIso = {};
        Vector2              Direction = {};                                                 // [-] - Chart tangent at the middle
    };
    std::vector<std::vector<UseTrack>> Tracks(Face.Loops.size());
    for (size_t Loop = 0; Loop < Face.Loops.size(); ++Loop)
    {
        Vector2 Cursor  = {};
        bool    Started = false;
        for (const BlueprintUse& Use : Face.Loops[Loop])
        {
            const BlueprintEdge& Edge  = Blueprint.Edges[Use.Edge];
            const double         From  = Use.Sense == Orientation::Forward ? Edge.Low : Edge.High;
            const double         Until = Use.Sense == Orientation::Forward ? Edge.High : Edge.Low;
            UseTrack             Track;
            // Recognise an exact (possibly split) NURBS iso-boundary before
            // attempting 2D inversion. Thin imported macro charts can have
            // nearly singular Jacobians: an unconstrained projection may jump
            // off the rim even though the edge is exactly on it.
            if (const auto* Spline = std::get_if<NurbsSurface>(&Face.Surface); Spline && Spline->Definition && Edge.High > Edge.Low)
            {
                const NurbsDomain U = MeasureNurbsSurfaceDomain(*Spline->Definition, true);
                const NurbsDomain V = MeasureNurbsSurfaceDomain(*Spline->Definition, false);
                for (int Side = 0; Side < 4 && !Track.ExactIso; ++Side)
                {
                    const bool FixedU = Side < 2;
                    const double Fixed = FixedU ? (Side == 0 ? U.Low : U.High) : (Side == 2 ? V.Low : V.High);
                    const NurbsCurve Iso = ExtractNurbsIsoCurve(*Spline, FixedU, Fixed);
                    const double Low = ProjectOntoNurbsCurve(*Iso.Definition, EvaluateCurve(Edge.Curve, Edge.Low).Position, 0.0);
                    const double High = ProjectOntoNurbsCurve(*Iso.Definition, EvaluateCurve(Edge.Curve, Edge.High).Position, 0.0);
                    const Vector2 A = FixedU ? Vector2{ Fixed, Low } : Vector2{ Low, Fixed };
                    const Vector2 B = FixedU ? Vector2{ Fixed, High } : Vector2{ High, Fixed };
                    const Vector2 Direction = (B - A) * (1.0 / (Edge.High - Edge.Low));
                    const ParameterLine Candidate{ A - Direction * Edge.Low, Direction };
                    double Worst = 0.0;
                    for (int Sample = 0; Sample <= 32; ++Sample)
                    {
                        const double T = Edge.Low + (Edge.High - Edge.Low) * Sample / 32.0;
                        Worst = std::max(Worst, Distance(EvaluateSurface(Face.Surface, Candidate.Origin + Candidate.Direction * T).Position,
                                                         EvaluateCurve(Edge.Curve, T).Position));
                    }
                    if (Worst <= 1.0e-9) Track.ExactIso = Candidate;
                }
            }
            for (int Step = 0; Step <= Steps; ++Step)
            {
                const double T = From + (Until - From) * Step / Steps;
                Cursor = Track.ExactIso ? Track.ExactIso->Origin + Track.ExactIso->Direction * T
                                        : ProjectOntoSurface(Face.Surface, EvaluateCurve(Edge.Curve, T).Position, Started ? Cursor : Vector2{});
                Started = true;
                Track.Samples.push_back(Cursor);
            }
            Track.Anchor    = Track.Samples[Steps / 2];
            Track.Direction = Track.Samples[Steps / 2 + 1] - Track.Samples[Steps / 2 - 1];
            Tracks[Loop].push_back(std::move(Track));
        }
    }

    // ② Seams: an edge used twice by this face. When both images coincide in the seam coordinate, the interior side
    //    decides which use takes the low boundary.
    std::map<int, std::vector<std::pair<size_t, size_t>>> Repeats;
    for (size_t Loop = 0; Loop < Face.Loops.size(); ++Loop)
        for (size_t Index = 0; Index < Face.Loops[Loop].size(); ++Index) Repeats[Face.Loops[Loop][Index].Edge].push_back({ Loop, Index });
    for (const auto& [Edge, Uses] : Repeats)
    {
        if (Uses.size() != 2 || Uses[0].first != Uses[1].first) continue;
        UseTrack&     First      = Tracks[Uses[0].first][Uses[0].second];
        UseTrack&     Second     = Tracks[Uses[1].first][Uses[1].second];
        const bool    AlongU     = std::fabs(First.Direction.X) <= std::fabs(First.Direction.Y);   // Constant 𝑢: a 𝑢-seam
        const double  SeamPeriod = AlongU ? Period.PeriodU : Period.PeriodV;
        const auto    Coordinate = [AlongU](Vector2 Value) -> double { return AlongU ? Value.X : Value.Y; };
        if (!(SeamPeriod > 0.0) || std::fabs(Coordinate(First.Anchor) - Coordinate(Second.Anchor)) >= 0.5 * SeamPeriod) continue;
        const SurfaceSample Sample  = EvaluateSurface(Face.Surface, First.Anchor);
        const double        Chart   = Dot(Cross(Sample.DerivativeU, Sample.DerivativeV), EvaluateSurfaceNormal(Face.Surface, First.Anchor) * Signum);
        if (Chart == 0.0) continue;
        const double ChartSign = Chart > 0.0 ? 1.0 : -1.0;
        // Interior to the left about the outward normal: (−d𝑣, d𝑢) in a chart that keeps orientation, mirrored otherwise.
        const auto InteriorAbove = [&](const UseTrack& Track)
        {
            return AlongU ? -ChartSign * Track.Direction.Y > 0.0 : ChartSign * Track.Direction.X > 0.0;
        };
        const bool FirstLow = InteriorAbove(First), SecondLow = InteriorAbove(Second);
        if (FirstLow == SecondLow) continue;
        // Boundary values: the rest of the loop spans the chart when it reaches across most of a period.
        double BoundaryLow = 1e300, BoundaryHigh = -1e300;
        const size_t Loop = Uses[0].first;
        for (size_t Index = 0; Index < Tracks[Loop].size(); ++Index)
        {
            if (Index == Uses[0].second || Index == Uses[1].second) continue;
            for (const Vector2& Sample2 : Tracks[Loop][Index].Samples)
            {
                BoundaryLow  = std::min(BoundaryLow, Coordinate(Sample2));
                BoundaryHigh = std::max(BoundaryHigh, Coordinate(Sample2));
            }
        }
        if (!(BoundaryHigh - BoundaryLow > 0.5 * SeamPeriod))
        {
            BoundaryLow  = Coordinate(First.Anchor);
            BoundaryHigh = BoundaryLow + SeamPeriod;
        }
        const auto Place = [AlongU](UseTrack& Track, double Value)
        {
            (AlongU ? Track.Anchor.X : Track.Anchor.Y) = Value;
            for (Vector2& Sample2 : Track.Samples) (AlongU ? Sample2.X : Sample2.Y) = Value;
        };
        Place(First, FirstLow ? BoundaryLow : BoundaryHigh);
        Place(Second, SecondLow ? BoundaryLow : BoundaryHigh);
    }

    // ③ Principal window from the widest loop; other loops moved by whole periods into it.
    std::vector<Vector2> Least(Face.Loops.size(), Vector2{ 1e300, 1e300 }), Most(Face.Loops.size(), Vector2{ -1e300, -1e300 });
    size_t Widest = 0;
    double WidestSpan = -1.0;
    for (size_t Loop = 0; Loop < Face.Loops.size(); ++Loop)
    {
        for (const UseTrack& Track : Tracks[Loop])
            for (const Vector2& Sample : Track.Samples)
            {
                Least[Loop] = { std::min(Least[Loop].X, Sample.X), std::min(Least[Loop].Y, Sample.Y) };
                Most[Loop]  = { std::max(Most[Loop].X, Sample.X), std::max(Most[Loop].Y, Sample.Y) };
            }
        const double Span = (Period.PeriodU > 0.0 ? Most[Loop].X - Least[Loop].X : 0.0) + (Period.PeriodV > 0.0 ? Most[Loop].Y - Least[Loop].Y : 0.0);
        if (Span > WidestSpan) { WidestSpan = Span; Widest = Loop; }
    }
    const auto Fold = [](double Value, double PeriodLength) { return PeriodLength > 0.0 ? -std::floor((Value + 1.0e-9) / PeriodLength) * PeriodLength : 0.0; };
    const Vector2 Origin{ Least[Widest].X + Fold(Least[Widest].X, Period.PeriodU), Least[Widest].Y + Fold(Least[Widest].Y, Period.PeriodV) };
    for (size_t Loop = 0; Loop < Face.Loops.size(); ++Loop)
    {
        Vector2 Shift{};
        if (Loop == Widest) Shift = Origin - Least[Loop];
        else
        {
            const Vector2 Centre = (Least[Loop] + Most[Loop]) * 0.5;
            Shift = { Fold(Centre.X - Origin.X, Period.PeriodU), Fold(Centre.Y - Origin.Y, Period.PeriodV) };
        }
        if (Period.PeriodU <= 0.0) Shift.X = 0.0;
        if (Period.PeriodV <= 0.0) Shift.Y = 0.0;
        for (size_t Index = 0; Index < Face.Loops[Loop].size(); ++Index)
        {
            BlueprintUse&        Use    = Face.Loops[Loop][Index];
            const BlueprintEdge& Edge   = Blueprint.Edges[Use.Edge];
            const Vector2        Anchor = Tracks[Loop][Index].Anchor + Shift;
            if (Tracks[Loop][Index].ExactIso) Use.SurfaceCurve = *Tracks[Loop][Index].ExactIso;
            else Use.SurfaceCurve = FitSurfaceCurve(Face.Surface, Edge.Curve, Edge.Low, Edge.High, { Anchor - Vector2{ 1.0, 1.0 }, Anchor + Vector2{ 1.0, 1.0 } }, Anchor);
        }
    }
}

} // namespace Frontier::GeometrySpace::Parametric
