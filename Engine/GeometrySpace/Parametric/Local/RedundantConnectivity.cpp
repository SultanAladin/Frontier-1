/*====================================================================================================================================
                                                     REDUNDANTTOPOLOGY.CPP
====================================================================================================================================*/
// 🧩 Maximal faces and edges — KEF across one carrier, KEV / KEMR for the slits left behind, KVE across degree-two vertices

#include "Engine/GeometrySpace/Parametric/Local/RedundantConnectivity.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"

#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/EulerEdits.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double AxisSlack = 1.0e-12;                                                    // [-]   - Unit-vector agreement
    constexpr double FullTurn  = 2.0 * std::numbers::pi;                                     // [rad] - Circle period

    enum class CarrierMatch
    {
        Distinct  = 0,                                                                       // Different carriers
        Identical = 1,                                                                       // Same record geometry and sense
        Coplanar  = 2,                                                                       // Same oriented plane, other chart
    };

    [[nodiscard]] bool SameDirection(Vector3 First, Vector3 Second) noexcept { return Length(First - Second) <= AxisSlack; }

    [[nodiscard]] bool SameFrame(const OrthonormalBasis& First, const OrthonormalBasis& Second, double Precision) noexcept
    {
        return Distance(First.Origin, Second.Origin) <= Precision && SameDirection(First.XAxis, Second.XAxis) && SameDirection(First.YAxis, Second.YAxis)
            && SameDirection(First.ZAxis, Second.ZAxis);
    }

    [[nodiscard]] CarrierMatch MatchCarriers(const SurfaceGeometry& First, Orientation FirstSense, const SurfaceGeometry& Second, Orientation SecondSense, double Precision)
    {
        if (First.index() != Second.index()) return CarrierMatch::Distinct;
        if (const auto* PlaneA = std::get_if<PlaneSurface>(&First))
        {
            const auto&   PlaneB  = std::get<PlaneSurface>(Second);
            const Vector3 NormalA = PlaneA->Frame.ZAxis * (FirstSense == Orientation::Forward ? 1.0 : -1.0);   // [-] - Outward normal of A
            const Vector3 NormalB = PlaneB.Frame.ZAxis * (SecondSense == Orientation::Forward ? 1.0 : -1.0);   // [-] - Outward normal of B
            if (Dot(NormalA, NormalB) < 1.0 - AxisSlack) return CarrierMatch::Distinct;
            if (std::fabs(Dot(PlaneB.Frame.Origin - PlaneA->Frame.Origin, NormalA)) > Precision) return CarrierMatch::Distinct;
            return FirstSense == SecondSense && SameFrame(PlaneA->Frame, PlaneB.Frame, Precision) ? CarrierMatch::Identical : CarrierMatch::Coplanar;
        }
        if (FirstSense != SecondSense) return CarrierMatch::Distinct;
        if (const auto* SplineA = std::get_if<NurbsSurface>(&First))
        {
            // 📝 Same chart: equal degrees, counts and knots, poles within precision, equal weights (conservative).
            const NurbsSurfaceDefinition& A = *SplineA->Definition;
            const NurbsSurfaceDefinition& B = *std::get<NurbsSurface>(Second).Definition;
            if (&A == &B) return CarrierMatch::Identical;
            if (A.DegreeU != B.DegreeU || A.DegreeV != B.DegreeV || A.CountU != B.CountU || A.CountV != B.CountV || A.KnotsU != B.KnotsU || A.KnotsV != B.KnotsV) return CarrierMatch::Distinct;
            for (size_t Index = 0; Index < A.Poles.size(); ++Index)
                if (Distance(A.Poles[Index], B.Poles[Index]) > Precision || std::fabs(A.Weights[Index] - B.Weights[Index]) > 1.0e-12 * A.Weights[Index]) return CarrierMatch::Distinct;
            return CarrierMatch::Identical;
        }
        if (const auto* OffsetA = std::get_if<ParallelSurface>(&First))                      // Same basis, same distance
        {
            const ParallelDefinition& A = *OffsetA->Definition;
            const ParallelDefinition& B = *std::get<ParallelSurface>(Second).Definition;
            return A.Basis.Definition == B.Basis.Definition && std::fabs(A.Distance - B.Distance) <= Precision ? CarrierMatch::Identical : CarrierMatch::Distinct;
        }
        if (const auto* BallA = std::get_if<RollingBallSurface>(&First))                     // Same definition only (conservative)
        {
            return BallA->Definition == std::get<RollingBallSurface>(Second).Definition ? CarrierMatch::Identical : CarrierMatch::Distinct;
        }
        const bool Same = std::visit([&](const auto& SurfaceA) -> bool
        {
            using Kind = std::decay_t<decltype(SurfaceA)>;
            const Kind& SurfaceB = std::get<Kind>(Second);
            if constexpr (std::is_same_v<Kind, RollingBallSurface> || std::is_same_v<Kind, NurbsSurface> || std::is_same_v<Kind, ParallelSurface>) return false;   // Compared above
            else
            {
            if (!SameFrame(SurfaceA.Frame, SurfaceB.Frame, Precision)) return false;
            if constexpr (std::is_same_v<Kind, CylinderSurface> || std::is_same_v<Kind, SphereSurface>) return std::fabs(SurfaceA.Radius - SurfaceB.Radius) <= Precision;
            else if constexpr (std::is_same_v<Kind, ConeSurface>)
            {
                return std::fabs(SurfaceA.OriginRadius - SurfaceB.OriginRadius) <= Precision && std::fabs(SurfaceA.RadialSlope - SurfaceB.RadialSlope) <= AxisSlack;
            }
            else if constexpr (std::is_same_v<Kind, TorusSurface>)
            {
                return std::fabs(SurfaceA.MajorRadius - SurfaceB.MajorRadius) <= Precision && std::fabs(SurfaceA.MinorRadius - SurfaceB.MinorRadius) <= Precision;
            }
            else return false;
            }
        }, First);
        return Same ? CarrierMatch::Identical : CarrierMatch::Distinct;
    }

    [[nodiscard]] bool ManifoldFins(const ModelPartition& Partition, EdgeToken Edge, FinToken& First, FinToken& Second)
    {
        if (!Partition.Edges.Authenticate(Edge)) return false;
        First  = Partition.Edges.Resolve(Edge).RadialFin;
        Second = Partition.Fins.Resolve(First).RadialSuccessor;
        return !(First == Second) && Partition.Fins.Resolve(Second).RadialSuccessor == First;
    }

    [[nodiscard]] FaceToken FinFace(const ModelPartition& Partition, FinToken Fin) { return Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face; }

    [[nodiscard]] const SurfaceGeometry& FaceSurface(const ModelPartition& Partition, FaceToken Face)
    {
        return Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface);
    }

    // Re-fit one fin's SP-curve on Surface over its edge's interval; the periodic image nearest Anchor is taken.
    void RefitFin(ModelPartition& Partition, FinToken Fin, const SurfaceGeometry& Surface, Vector2 Anchor)
    {
        const EdgeProfile& Edge = Partition.Edges.Resolve(Partition.Fins.Resolve(Fin).Edge);
        const CurveGeometry& Curve = Partition.Curves.Resolve(Edge.Curve);
        Partition.AttachSurfaceCurve(Fin, FitSurfaceCurve(Surface, Curve, Edge.IntervalLow, Edge.IntervalHigh, { Anchor - Vector2{ 1.0, 1.0 }, Anchor + Vector2{ 1.0, 1.0 } }, Anchor));
    }

    [[nodiscard]] Vector2 FinMidpoint(const ModelPartition& Partition, FinToken Fin)
    {
        const EdgeProfile& Edge = Partition.Edges.Resolve(Partition.Fins.Resolve(Fin).Edge);
        return EvaluateParameterCurve(*Partition.Fins.Resolve(Fin).SurfaceCurve, 0.5 * (Edge.IntervalLow + Edge.IntervalHigh)).Position;
    }

    // ① One KEF across Edge when both faces share the carrier; false when the edge is not redundant.
    [[nodiscard]] bool MergeAcrossEdge(ModelPartition& Partition, EdgeToken Edge, double Precision, RedundancyOutcome& Outcome)
    {
        FinToken Kept = {}, Absorbed = {};                                                   // [-] - KEF keeps the radial fin's face
        if (!ManifoldFins(Partition, Edge, Kept, Absorbed)) return false;
        const FaceToken KeptFace = FinFace(Partition, Kept), AbsorbedFace = FinFace(Partition, Absorbed);
        if (KeptFace == AbsorbedFace) return false;
        const FaceProfile& KeptProfile = Partition.Faces.Resolve(KeptFace);
        const FaceProfile& AbsorbedProfile = Partition.Faces.Resolve(AbsorbedFace);
        if (!KeptProfile.Surface.PresentCondition() || !AbsorbedProfile.Surface.PresentCondition()) return false;
        if (!Partition.Fins.Resolve(Kept).SurfaceCurve || !Partition.Fins.Resolve(Absorbed).SurfaceCurve) return false;
        const CarrierMatch Match = MatchCarriers(FaceSurface(Partition, KeptFace), KeptProfile.Sense, FaceSurface(Partition, AbsorbedFace), AbsorbedProfile.Sense, Precision);
        if (Match == CarrierMatch::Distinct) return false;

        // 📝 Both fins trace the edge on their own chart; on identical periodic carriers they may differ by a period.
        const Vector2 Offset = FinMidpoint(Partition, Kept) - FinMidpoint(Partition, Absorbed);   // [-] - Chart shift absorbed → kept
        const bool    RefitCondition = Match == CarrierMatch::Coplanar || Length(Offset) > 1.0e-9;
        std::vector<std::pair<FinToken, Vector2>> Moving;                                    // [-] - Absorbed fins and their anchors
        if (RefitCondition)
        {
            for (LoopToken Loop : AbsorbedProfile.Loops)
            {
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    if (!(Fin == Absorbed) && Partition.Fins.Resolve(Fin).SurfaceCurve) Moving.push_back({ Fin, FinMidpoint(Partition, Fin) + Offset });
                });
            }
        }
        const EditOutcome<bool> Merge = CollapseEdgeFace(Partition, Edge);
        if (!Merge.SucceededCondition()) return false;
        const SurfaceGeometry KeptSurface = FaceSurface(Partition, KeptFace);               // [-] - Copy (refits attach records)
        for (const auto& [Fin, Anchor] : Moving)
        {
            // Coplanar: the anchor is meaningless on the other chart, but a plane has no periodic image to choose.
            RefitFin(Partition, Fin, KeptSurface, Match == CarrierMatch::Coplanar ? Vector2{} : Anchor);
        }
        ++Outcome.FacesMerged;
        Outcome.FaceMerges.push_back({ AbsorbedFace, KeptFace });
        Outcome.EdgesRemoved.push_back(Edge);
        return true;
    }

    // ② An edge with both fins in one merged face: a spur (consecutive fins) goes by KEV; a separating slit on a plane by
    //    KEMR. ⚠️ A seam also has both fins in one face — a sphere's pole-to-pole seam even forms a two-fin loop that is
    //    topologically a spur — so a slit must trace the SAME chart curve on both sides; a seam's sides differ by a period.
    [[nodiscard]] bool RemoveSlit(ModelPartition& Partition, EdgeToken Edge, const std::vector<FaceToken>& Merged, RedundancyOutcome& Outcome)
    {
        FinToken First = {}, Second = {};
        if (!ManifoldFins(Partition, Edge, First, Second)) return false;
        const FaceToken Face = FinFace(Partition, First);
        if (!(Face == FinFace(Partition, Second))) return false;
        if (std::find(Merged.begin(), Merged.end(), Face) == Merged.end()) return false;
        if (!Partition.Fins.Resolve(First).SurfaceCurve || !Partition.Fins.Resolve(Second).SurfaceCurve) return false;
        if (Length(FinMidpoint(Partition, First) - FinMidpoint(Partition, Second)) > 1.0e-9) return false;   // A seam
        const VertexToken Start = Partition.Edges.Resolve(Edge).Start, End = Partition.Edges.Resolve(Edge).End;
        if (Start == End) return false;
        const auto Remove = [&](VertexToken Tip)
        {
            if (!CollapseEdgeVertex(Partition, Edge, Tip).SucceededCondition()) return false;
            ++Outcome.SlitsRemoved;
            Outcome.EdgesRemoved.push_back(Edge);
            return true;
        };
        if (Partition.Fins.Resolve(First).Successor == Second) return Remove(Partition.FinEnd(First));
        if (Partition.Fins.Resolve(Second).Successor == First) return Remove(Partition.FinEnd(Second));
        if (!(Partition.Fins.Resolve(First).Loop == Partition.Fins.Resolve(Second).Loop)) return false;
        if (!std::holds_alternative<PlaneSurface>(FaceSurface(Partition, Face))) return false;
        if (!CollapseEdgeConstructRing(Partition, Edge).SucceededCondition()) return false;
        ++Outcome.SlitsRemoved;
        Outcome.EdgesRemoved.push_back(Edge);
        return true;
    }

    [[nodiscard]] bool SameCurveCarrier(const CurveGeometry& First, const CurveGeometry& Second, double Precision)
    {
        if (const auto* LineA = std::get_if<StraightLine>(&First))
        {
            const auto* LineB = std::get_if<StraightLine>(&Second);
            if (!LineB || Length(Cross(LineA->Direction, LineB->Direction)) > AxisSlack) return false;
            const Vector3 Offset = LineB->Origin - LineA->Origin;                            // [len] - o₂ − o₁
            return Length(Offset - LineA->Direction * Dot(Offset, LineA->Direction)) <= Precision;
        }
        if (const auto* ArcA = std::get_if<CircularArc>(&First))
        {
            const auto* ArcB = std::get_if<CircularArc>(&Second);
            return ArcB && Distance(ArcA->Frame.Origin, ArcB->Frame.Origin) <= Precision && std::fabs(ArcA->Radius - ArcB->Radius) <= Precision
                && Length(Cross(ArcA->Frame.ZAxis, ArcB->Frame.ZAxis)) <= AxisSlack;
        }
        return false;
    }

    // ③ One KVE at Vertex when its two edges share a line / circle carrier; Kept's interval grows over Absorbed.
    [[nodiscard]] bool JoinAtVertex(ModelPartition& Partition, VertexToken Vertex, EdgeToken Kept, EdgeToken Absorbed, double Precision, RedundancyOutcome& Outcome)
    {
        FinToken KeptFirst = {}, KeptSecond = {}, AbsorbedFirst = {}, AbsorbedSecond = {};
        if (Kept == Absorbed || !ManifoldFins(Partition, Kept, KeptFirst, KeptSecond) || !ManifoldFins(Partition, Absorbed, AbsorbedFirst, AbsorbedSecond)) return false;
        const EdgeProfile KeptEdge = Partition.Edges.Resolve(Kept);                          // [-] - Copy before the edit
        const EdgeProfile AbsorbedEdge = Partition.Edges.Resolve(Absorbed);
        if (!KeptEdge.Curve.PresentCondition() || !AbsorbedEdge.Curve.PresentCondition()) return false;
        if (!std::isnan(KeptEdge.Precision) || !std::isnan(AbsorbedEdge.Precision)) return false;  // Tolerant edges keep their vertex
        const CurveGeometry Curve = Partition.Curves.Resolve(KeptEdge.Curve);
        if (!SameCurveCarrier(Curve, Partition.Curves.Resolve(AbsorbedEdge.Curve), Precision)) return false;
        if (!Partition.Fins.Resolve(KeptFirst).SurfaceCurve || !Partition.Fins.Resolve(KeptSecond).SurfaceCurve) return false;

        const VertexToken Far     = AbsorbedEdge.Start == Vertex ? AbsorbedEdge.End : AbsorbedEdge.Start;   // [-] - Kept's new end
        const bool        AtEnd   = KeptEdge.End == Vertex;                                  // [-] - Kept grows at τ high
        const bool        Closing = Far == (AtEnd ? KeptEdge.Start : KeptEdge.End);          // [-] - Result is a closed edge
        const bool        Periodic = PeriodicCondition(Curve);
        double Parameter = ProjectOntoCurve(Curve, Partition.Vertices.Resolve(Far).Position, KeptEdge.IntervalLow);   // [-] - τ of Far
        double Low = KeptEdge.IntervalLow, High = KeptEdge.IntervalHigh;                     // [-] - New interval
        const double Slack = 1.0e-12;                                                        // [-] - Parameter round-off
        if (Periodic)
        {
            if (Closing) Parameter = AtEnd ? Low + FullTurn : High - FullTurn;
            else if (AtEnd) { while (Parameter <= High + Slack) Parameter += FullTurn; while (Parameter > Low + FullTurn) Parameter -= FullTurn; }
            else            { while (Parameter >= Low - Slack) Parameter -= FullTurn; while (Parameter < High - FullTurn) Parameter += FullTurn; }
        }
        (AtEnd ? High : Low) = Parameter;
        if (!(High > KeptEdge.IntervalHigh - Slack && Low < KeptEdge.IntervalLow + Slack) || High - Low <= KeptEdge.IntervalHigh - KeptEdge.IntervalLow + Slack) return false;
        if (Distance(EvaluateCurve(Curve, Parameter).Position, Partition.Vertices.Resolve(Far).Position) > Precision) return false;

        const Vector2 AnchorFirst = FinMidpoint(Partition, KeptFirst), AnchorSecond = FinMidpoint(Partition, KeptSecond);
        if (!CollapseVertexJoinEdges(Partition, Vertex, Kept, Absorbed).SucceededCondition()) return false;
        EdgeProfile& Grown = Partition.Edges.Resolve(Kept);
        Grown.IntervalLow  = Low;
        Grown.IntervalHigh = High;
        RefitFin(Partition, KeptFirst, FaceSurface(Partition, FinFace(Partition, KeptFirst)), AnchorFirst);
        RefitFin(Partition, KeptSecond, FaceSurface(Partition, FinFace(Partition, KeptSecond)), AnchorSecond);
        ++Outcome.EdgesJoined;
        Outcome.EdgesRemoved.push_back(Absorbed);
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

int CountRedundantEdges(const ModelPartition& Partition, BodyToken Body, double Precision)
{
    int Count = 0;
    for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
    {
        FinToken First = {}, Second = {};
        if (!ManifoldFins(Partition, Edge, First, Second)) continue;
        const FaceToken FirstFace = FinFace(Partition, First), SecondFace = FinFace(Partition, Second);
        if (FirstFace == SecondFace) continue;                                               // Seam of a periodic face
        const FaceProfile& FirstProfile  = Partition.Faces.Resolve(FirstFace);
        const FaceProfile& SecondProfile = Partition.Faces.Resolve(SecondFace);
        if (!FirstProfile.Surface.PresentCondition() || !SecondProfile.Surface.PresentCondition()) continue;
        if (MatchCarriers(FaceSurface(Partition, FirstFace), FirstProfile.Sense, FaceSurface(Partition, SecondFace), SecondProfile.Sense, Precision) != CarrierMatch::Distinct) ++Count;
    }
    return Count;
}

RedundancyOutcome RemoveRedundantConnectivity(ModelPartition& Partition, BodyToken Body, double Precision, const std::function<bool(EdgeToken)>& MergeableEdge)
{
    RedundancyOutcome Outcome;
    for (bool Progress = true; Progress;)
    {
        Progress = false;
        for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
        {
            if (MergeableEdge && !MergeableEdge(Edge)) continue;
            Progress |= MergeAcrossEdge(Partition, Edge, Precision, Outcome);
        }
    }
    if (Outcome.FacesMerged > 0)
    {
        std::vector<FaceToken> Merged;                                                       // [-] - Faces that absorbed others
        for (const auto& [Absorbed, Survivor] : Outcome.FaceMerges) Merged.push_back(Survivor);
        for (bool Progress = true; Progress;)
        {
            Progress = false;
            for (EdgeToken Edge : Partition.CollectInventory(Body).Edges) Progress |= RemoveSlit(Partition, Edge, Merged, Outcome);
        }
    }
    for (bool Progress = true; Progress;)
    {
        Progress = false;
        std::map<uint32_t, std::vector<EdgeToken>> Incidence;                                // [-] - Vertex slot → edge ends
        std::map<uint32_t, VertexToken>            Vertices;                                 // [-] - Vertex slot → token
        for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            for (VertexToken End : { Profile.Start, Profile.End })
            {
                Incidence[End.Slot].push_back(Edge);
                Vertices[End.Slot] = End;
            }
        }
        for (const auto& [Slot, Edges] : Incidence)
        {
            if (Edges.size() != 2) continue;
            if (JoinAtVertex(Partition, Vertices[Slot], Edges[0], Edges[1], Precision, Outcome)) { Progress = true; break; }
        }
    }
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
