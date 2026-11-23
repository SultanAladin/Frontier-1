//============================================================================================================================================
//                                                          CHARTPARTITION.CPP
//============================================================================================================================================
// 🧩 Seam-bounded flood fill, boundary chaining, exact fold classification, and subdivision that terminates.

#include "SlateCompute/Compute/ChartPartition/Api/ChartPartition.h"

#include "Shared/IntersectionClassifier.slang.h"
#include "Shared/OrientationClassifier.slang.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      EDGE KEYS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr std::uint32_t AbsentFace = 0xFFFFFFFFu;   // [-] - no face; never a valid ordinal

// 📝 A welded edge as one ordinal, least position in the high half. Seam matching is over welded positions and
//    not over imported vertices, because a format storing a coordinate per corner has already split every
//    vertex at a coordinate seam — and a seam test that missed those would cut the surface at every one of them.
std::uint64_t EdgeKey(std::uint32_t FirstPosition, std::uint32_t SecondPosition)
{
    const std::uint64_t Minimum    = FirstPosition < SecondPosition ? FirstPosition  : SecondPosition;
    const std::uint64_t Maximum = FirstPosition < SecondPosition ? SecondPosition : FirstPosition;

    return (Minimum << 32) | Maximum;
}

bool KeyHeld(const std::vector<std::uint64_t>& Keys, std::uint64_t Sought)
{
    for (const std::uint64_t Held : Keys)
    {
        if (Held == Sought)
            return true;
    }

    return false;
}

// 📝 One chart under consideration. The attempt count bounds the subdivision so a pathological topology cannot
//    make the derivation unbounded; the termination argument below makes the bound a formality rather than a
//    silent truncation.
struct PendingChart
{
    std::vector<std::uint32_t>  Faces    = {};   // [-] - imported face ordinals
    std::uint32_t               Attempts = 0u;   // [-] - subdivisions this lineage has already cost
};

// 📝 One chart's local vertex numbering, its triangulation, and the corners that read it back.
struct ChartLocality
{
    std::vector<DocumentPosition>  Positions       = {};   // [mm] - chart-local
    std::vector<std::uint32_t>     TriangleCorners = {};   // [-]  - three per triangle, chart-local
    std::vector<std::uint32_t>     Corners         = {};   // [-]  - imported corner ordinals
    std::vector<std::uint32_t>     CornerLocals    = {};   // [-]  - parallel; chart-local ordinal per corner
    std::vector<std::uint32_t>     ContourLoop    = {};   // [-]  - ordered, chart-local
    std::uint32_t                  LoopCount       = 0u;   // [-]  - boundary loops chained
};

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHART LOCALITY
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Chart-local vertices are keyed by **welded position**, so two corners of one welded position inside one
//    chart share a domain position while the same welded position on the other side of a seam gets its own.
//    That is the entire mechanism by which a seam becomes a discontinuity in the domain and nowhere else.
ChartLocality BuildLocality(const TopologyStructure&           Imported,
                            const TopologyConditioning&        Conditioned,
                            const std::vector<std::uint32_t>&  Faces,
                            const std::vector<std::uint32_t>&  FaceOfEachFace,
                            const std::vector<std::uint64_t>&  SeamKeys,
                            std::vector<std::uint32_t>&        LocalOfWelded,
                            std::vector<std::uint32_t>&        StampOfWelded,
                            std::uint32_t                      Stamp)
{
    ChartLocality Built;

    for (const std::uint32_t FaceIndex : Faces)
    {
        const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
        const std::uint32_t CornerSpan  = Imported.FaceCornerCount(FaceIndex);

        for (std::uint32_t Passed = 0u; Passed < CornerSpan; ++Passed)
        {
            const std::uint32_t CornerIndex = FirstCorner + Passed;
            const std::uint32_t Welded        =
                Conditioned.WeldedPosition(Imported.CornerVertex(CornerIndex)).Resolve();

            if (StampOfWelded[Welded] != Stamp)
            {
                StampOfWelded[Welded] = Stamp;
                LocalOfWelded[Welded] = static_cast<std::uint32_t>(Built.Positions.size());
                Built.Positions.push_back(Imported.Positions()[Imported.CornerVertex(CornerIndex)]);
            }

            Built.Corners.push_back(CornerIndex);
            Built.CornerLocals.push_back(LocalOfWelded[Welded]);
        }
    }

    // 📝 Fan-triangulated from each face's first corner, matching `38` §4 and `40`'s intersection. Two
    //    triangulations of one n-gon flatten its interior differently along the diagonal, and the tangent basis
    //    and the picking would then disagree about which side of a quad a position is on.
    std::size_t CornerWalk = 0u;

    for (const std::uint32_t FaceIndex : Faces)
    {
        const std::uint32_t CornerSpan = Imported.FaceCornerCount(FaceIndex);

        for (std::uint32_t Fan = 1u; Fan + 1u < CornerSpan; ++Fan)
        {
            Built.TriangleCorners.push_back(Built.CornerLocals[CornerWalk]);
            Built.TriangleCorners.push_back(Built.CornerLocals[CornerWalk + Fan]);
            Built.TriangleCorners.push_back(Built.CornerLocals[CornerWalk + Fan + 1u]);
        }

        CornerWalk += CornerSpan;
    }

    // 📐 A directed boundary edge is one whose opposite face is absent, sits in another chart, or is cut by a
    //    seam. Chaining the directed edges gives the loops; a chart that is a disc has exactly one.
    std::vector<std::uint32_t> Opening;
    std::vector<std::uint32_t> Closing;

    CornerWalk = 0u;

    for (const std::uint32_t FaceIndex : Faces)
    {
        const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
        const std::uint32_t CornerSpan  = Imported.FaceCornerCount(FaceIndex);

        for (std::uint32_t Passed = 0u; Passed < CornerSpan; ++Passed)
        {
            const std::uint32_t CornerIndex = FirstCorner + Passed;
            const std::uint32_t Following     = FirstCorner + (Passed + 1u) % CornerSpan;

            const std::uint32_t OpeningWelded =
                Conditioned.WeldedPosition(Imported.CornerVertex(CornerIndex)).Resolve();
            const std::uint32_t ClosingWelded =
                Conditioned.WeldedPosition(Imported.CornerVertex(Following)).Resolve();

            bool DividerHere = KeyHeld(SeamKeys, EdgeKey(OpeningWelded, ClosingWelded));

            if (!DividerHere)
            {
                const Deliver<std::uint32_t> Adjacent = Conditioned.AdjacentCorner(CornerIndex);

                if (!Adjacent.Resolved)
                    DividerHere = true;
                else
                    DividerHere = FaceOfEachFace[Imported.CornerFace(Adjacent.Resolve())] != FaceOfEachFace[FaceIndex];
            }

            if (DividerHere)
            {
                Opening.push_back(LocalOfWelded[OpeningWelded]);
                Closing.push_back(LocalOfWelded[ClosingWelded]);
            }
        }

        CornerWalk += CornerSpan;
    }

    std::vector<bool> Walked(Opening.size(), false);

    for (std::size_t Index = 0u; Index < Opening.size(); ++Index)
    {
        if (Walked[Index])
            continue;

        ++Built.LoopCount;

        std::vector<std::uint32_t> Loop;

        std::size_t Walking = Index;

        for (std::size_t Passed = 0u; Passed <= Opening.size(); ++Passed)
        {
            if (Walked[Walking])
                break;

            Walked[Walking] = true;
            Loop.push_back(Opening[Walking]);

            const std::uint32_t Sought = Closing[Walking];

            std::size_t Following = Opening.size();

            for (std::size_t Candidate = 0u; Candidate < Opening.size(); ++Candidate)
            {
                if (!Walked[Candidate] && Opening[Candidate] == Sought)
                {
                    Following = Candidate;
                    break;
                }
            }

            if (Following == Opening.size())
                break;

            Walking = Following;
        }

        // 📝 The longest loop is the outer boundary. A chart with more than one is not a disc and is subdivided
        //    by the caller rather than flattened against whichever loop happened to be chained first.
        if (Loop.size() > Built.ContourLoop.size())
            Built.ContourLoop = Loop;
    }

    return Built;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FOLD DETECTION
//------------------------------------------------------------------------------------------------------------------------

// 🔴 `68` §4.1: a fold maps two topology positions to one domain position, so texturing one textures both. It is a
//    **failure** and not a distortion value, which is why it is classified exactly rather than measured.
bool FoldDetected(const ChartLocality& Local, const std::vector<PlanarPosition>& Flattened)
{
    Signed32 Declared = 0;

    const std::size_t TriangleSpan = Local.TriangleCorners.size() / 3u;

    for (std::size_t TriangleIndex = 0u; TriangleIndex < TriangleSpan; ++TriangleIndex)
    {
        const PlanarPosition& Alpha = Flattened[Local.TriangleCorners[TriangleIndex * 3u]];
        const PlanarPosition& Beta  = Flattened[Local.TriangleCorners[TriangleIndex * 3u + 1u]];
        const PlanarPosition& Gamma = Flattened[Local.TriangleCorners[TriangleIndex * 3u + 2u]];

        const Signed32 Winding = ClassifyOrientation(Alpha.PositionX, Alpha.PositionY,
                                                     Beta.PositionX,  Beta.PositionY,
                                                     Gamma.PositionX, Gamma.PositionY);

        if (Winding == 0)
            continue;

        if (Declared == 0)
            Declared = Winding;
        else if (Winding != Declared)
            return true;
    }

    // 📐 Consistent winding does not exclude a boundary that crosses itself, so the boundary is tested too.
    //    `02` §4's exact classification decides it: an approximate overlap test finds folds sometimes, which is
    //    worse than not testing, because the failures that survive are the subtle ones.
    const std::size_t LoopSpan = Local.ContourLoop.size();

    for (std::size_t Earlier = 0u; Earlier + 1u < LoopSpan; ++Earlier)
    {
        const PlanarPosition& AlphaFirst  = Flattened[Local.ContourLoop[Earlier]];
        const PlanarPosition& AlphaSecond = Flattened[Local.ContourLoop[(Earlier + 1u) % LoopSpan]];

        for (std::size_t Later = Earlier + 2u; Later < LoopSpan; ++Later)
        {
            if (Earlier == 0u && Later + 1u == LoopSpan)
                continue;

            const PlanarPosition& BetaFirst  = Flattened[Local.ContourLoop[Later]];
            const PlanarPosition& BetaSecond = Flattened[Local.ContourLoop[(Later + 1u) % LoopSpan]];

            if (ClassifySegmentIntersection(AlphaFirst.PositionX,  AlphaFirst.PositionY,
                                            AlphaSecond.PositionX, AlphaSecond.PositionY,
                                            BetaFirst.PositionX,   BetaFirst.PositionY,
                                            BetaSecond.PositionX,  BetaSecond.PositionY)
             == SlateIntersectionCrossing)
            {
                return true;
            }
        }
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SUBDIVISION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The chart is split by the widest spatial axis of its face centroids, at the median. Deterministic, and it
//    strictly reduces the face count on both sides — which is what makes the whole derivation terminate.
void Subdivide(const TopologyStructure&           Imported,
               const std::vector<std::uint32_t>&  Faces,
               std::vector<std::uint32_t>&        FirstHalf,
               std::vector<std::uint32_t>&        SecondHalf)
{
    std::vector<double> CentroidX(Faces.size(), 0.0);
    std::vector<double> CentroidY(Faces.size(), 0.0);
    std::vector<double> CentroidZ(Faces.size(), 0.0);

    double MinimumX = 0.0, MaximumX = 0.0, MinimumY = 0.0, MaximumY = 0.0, MinimumZ = 0.0, MaximumZ = 0.0;

    for (std::size_t Index = 0u; Index < Faces.size(); ++Index)
    {
        const std::uint32_t FaceIndex = Faces[Index];
        const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
        const std::uint32_t CornerSpan  = Imported.FaceCornerCount(FaceIndex);

        for (std::uint32_t Passed = 0u; Passed < CornerSpan; ++Passed)
        {
            const DocumentPosition& Held = Imported.Positions()[Imported.CornerVertex(FirstCorner + Passed)];

            CentroidX[Index] += Held.PositionX;
            CentroidY[Index] += Held.PositionY;
            CentroidZ[Index] += Held.PositionZ;
        }

        CentroidX[Index] /= static_cast<double>(CornerSpan);
        CentroidY[Index] /= static_cast<double>(CornerSpan);
        CentroidZ[Index] /= static_cast<double>(CornerSpan);

        if (Index == 0u)
        {
            MinimumX = MaximumX = CentroidX[0];
            MinimumY = MaximumY = CentroidY[0];
            MinimumZ = MaximumZ = CentroidZ[0];
            continue;
        }

        MinimumX    = CentroidX[Index] < MinimumX    ? CentroidX[Index] : MinimumX;
        MaximumX = CentroidX[Index] > MaximumX ? CentroidX[Index] : MaximumX;
        MinimumY    = CentroidY[Index] < MinimumY    ? CentroidY[Index] : MinimumY;
        MaximumY = CentroidY[Index] > MaximumY ? CentroidY[Index] : MaximumY;
        MinimumZ    = CentroidZ[Index] < MinimumZ    ? CentroidZ[Index] : MinimumZ;
        MaximumZ = CentroidZ[Index] > MaximumZ ? CentroidZ[Index] : MaximumZ;
    }

    const double SpanX = MaximumX - MinimumX;
    const double SpanY = MaximumY - MinimumY;
    const double SpanZ = MaximumZ - MinimumZ;

    const std::vector<double>* Measured = &CentroidX;
    double                     Middle   = (MinimumX + MaximumX) * 0.5;

    if (SpanY >= SpanX && SpanY >= SpanZ)
    {
        Measured = &CentroidY;
        Middle   = (MinimumY + MaximumY) * 0.5;
    }
    else if (SpanZ >= SpanX && SpanZ >= SpanY)
    {
        Measured = &CentroidZ;
        Middle   = (MinimumZ + MaximumZ) * 0.5;
    }

    for (std::size_t Index = 0u; Index < Faces.size(); ++Index)
    {
        if ((*Measured)[Index] < Middle)
            FirstHalf.push_back(Faces[Index]);
        else
            SecondHalf.push_back(Faces[Index]);
    }

    // 📝 A degenerate split — every centroid on one side — is broken by ordinal so the recursion still shrinks.
    //    Coincident faces are the case that produces it, and they are exactly the case `38` §3 registers.
    if (FirstHalf.empty() || SecondHalf.empty())
    {
        FirstHalf.clear();
        SecondHalf.clear();

        for (std::size_t Index = 0u; Index < Faces.size(); ++Index)
        {
            if (Index * 2u < Faces.size())
                FirstHalf.push_back(Faces[Index]);
            else
                SecondHalf.push_back(Faces[Index]);
        }
    }
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DERIVATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<DerivedPartition> Derive(const TopologyStructure&      Imported,
                                 const TopologyConditioning&   Conditioned,
                                 const SeamSpecification&      Seams,
                                 const PartitionSpecification& Declaring,
                                 const WorkCancellation&       Cancellation,
                                 WorkProgress&                 Progressed)
{
    if (!Imported.Sealed())
    {
        return Deliver<DerivedPartition>::Refuse(
            { RefusalReason::HostDenied, "an unsealed topology is not immutable for the run" });
    }

    if (Conditioned.ConditionedRevision() != Imported.Revision())
    {
        return Deliver<DerivedPartition>::Refuse(
            { RefusalReason::ExtentExhausted, "the conditioning describes another topology revision" });
    }

    DerivedPartition Produced;
    Produced.DescribedRevision = Imported.Revision();
    Produced.CornerCoordinates.assign(Imported.CornerCount(), DomainCoordinate{});

    // 📝 The authored seams are translated to welded pairs once. Derived seams extend the same set as they are
    //    added, so a subdivision made on one pass is respected by the flood fill of the next.
    std::vector<std::uint64_t> SeamKeys;

    for (const SeamEdge& Authored : Seams.Authored())
    {
        const Deliver<std::uint32_t> MinimumWelded    = Conditioned.WeldedPosition(Authored.MinimumVertex);
        const Deliver<std::uint32_t> MaximumWelded = Conditioned.WeldedPosition(Authored.MaximumVertex);

        if (MinimumWelded.Resolved && MaximumWelded.Resolved)
            SeamKeys.push_back(EdgeKey(MinimumWelded.Resolve(), MaximumWelded.Resolve()));
    }

    // 📐 Flood fill over faces across non-seam, manifold adjacency. Every polygon lands in exactly one chart,
    //    which is `68` §3's coverage requirement stated as an algorithm rather than as a hope.
    const std::uint32_t FaceSpan = Imported.FaceCount();

    if (FaceSpan == 0u)
        return Deliver<DerivedPartition>::Refuse({ RefusalReason::ExtentExhausted, "the topology carries no face" });

    std::vector<std::uint32_t> ChartOfFace(FaceSpan, AbsentFace);
    std::vector<PendingChart>  Pending;

    for (std::uint32_t Seed = 0u; Seed < FaceSpan; ++Seed)
    {
        if (ChartOfFace[Seed] != AbsentFace)
            continue;

        const std::uint32_t ChartIndex = static_cast<std::uint32_t>(Pending.size());

        PendingChart Growing;

        std::vector<std::uint32_t> Frontier;
        Frontier.push_back(Seed);
        ChartOfFace[Seed] = ChartIndex;

        while (!Frontier.empty())
        {
            const std::uint32_t FaceIndex = Frontier.back();
            Frontier.pop_back();

            Growing.Faces.push_back(FaceIndex);

            const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
            const std::uint32_t CornerSpan  = Imported.FaceCornerCount(FaceIndex);

            for (std::uint32_t Passed = 0u; Passed < CornerSpan; ++Passed)
            {
                const std::uint32_t CornerIndex = FirstCorner + Passed;
                const std::uint32_t Following     = FirstCorner + (Passed + 1u) % CornerSpan;

                const std::uint32_t OpeningWelded =
                    Conditioned.WeldedPosition(Imported.CornerVertex(CornerIndex)).Resolve();
                const std::uint32_t ClosingWelded =
                    Conditioned.WeldedPosition(Imported.CornerVertex(Following)).Resolve();

                if (KeyHeld(SeamKeys, EdgeKey(OpeningWelded, ClosingWelded)))
                    continue;

                const Deliver<std::uint32_t> Adjacent = Conditioned.AdjacentCorner(CornerIndex);

                if (!Adjacent.Resolved)
                    continue;

                const std::uint32_t AdjacentFace = Imported.CornerFace(Adjacent.Resolve());

                if (ChartOfFace[AdjacentFace] != AbsentFace)
                    continue;

                ChartOfFace[AdjacentFace] = ChartIndex;
                Frontier.push_back(AdjacentFace);
            }
        }

        Pending.push_back(Growing);
    }

    Progressed.DeclareCount(0u, Pending.size());

    std::vector<std::uint32_t> LocalOfWelded(Conditioned.WeldedCount(), 0u);
    std::vector<std::uint32_t> StampOfWelded(Conditioned.WeldedCount(), 0u);
    std::uint32_t              Stamp = 0u;

    std::vector<Chart>                        Accepted;
    std::vector<std::vector<PlanarPosition>>  AcceptedFlattened;
    std::vector<ChartLocality>                AcceptedLocality;

    std::uint64_t Resolved = 0u;

    while (!Pending.empty())
    {
        // 🔴 `34` §5's cooperative point. A cancelled derivation runs to here and releases; a worker simply
        //    never joined leaks its inputs, proportional to how often the artist changes their mind about a seam.
        if (Cancellation.CancellationDeclared())
            return Deliver<DerivedPartition>::Refuse({ RefusalReason::HostDenied, "the derivation was withdrawn" });

        PendingChart Considering = Pending.back();
        Pending.pop_back();

        if (Considering.Faces.empty())
            continue;

        // 📝 The chart identity is the least imported face ordinal it holds — stable where the chart is unchanged.
        std::uint32_t IdentityIndex = Considering.Faces[0];

        for (const std::uint32_t FaceIndex : Considering.Faces)
            IdentityIndex = FaceIndex < IdentityIndex ? FaceIndex : IdentityIndex;

        for (const std::uint32_t FaceIndex : Considering.Faces)
            ChartOfFace[FaceIndex] = IdentityIndex;

        ++Stamp;

        ChartLocality Local = BuildLocality(Imported, Conditioned, Considering.Faces, ChartOfFace,
                                            SeamKeys, LocalOfWelded, StampOfWelded, Stamp);

        const bool SubdivisionReachable = Considering.Faces.size() > 1u
                                       && Considering.Attempts < Declaring.SubdivisionLimit;

        const bool NotADisc = Local.LoopCount != 1u || Local.ContourLoop.size() < 3u;

        // 🔴 A chart with no boundary at all — a closed surface — and a chart with several loops are the same
        //    failure: it is not a disc, so no boundary-first parameterisation exists for it. `68` §4.1's
        //    response to a fold is the response here too, one step earlier.
        if (NotADisc && SubdivisionReachable)
        {
            std::vector<std::uint32_t> FirstHalf;
            std::vector<std::uint32_t> SecondHalf;
            Subdivide(Imported, Considering.Faces, FirstHalf, SecondHalf);

            Pending.push_back({ FirstHalf,  Considering.Attempts + 1u });
            Pending.push_back({ SecondHalf, Considering.Attempts + 1u });

            Progressed.DeclareCount(Resolved, Resolved + Pending.size());
            continue;
        }

        UnwrapSpecification Solving;
        Solving.Positions            = Local.Positions;
        Solving.TriangleCorners      = Local.TriangleCorners;
        Solving.ContourLoop         = Local.ContourLoop;
        Solving.ConvergenceCriterion = Declaring.ConvergenceCriterion;
        Solving.IterationLimit     = Declaring.IterationLimit;

        const Deliver<ConvergentResult<std::vector<PlanarPosition>>> Solved = Solve(Solving);

        if (!Solved.Resolved)
        {
            if (!SubdivisionReachable)
                return Deliver<DerivedPartition>::Refuse(Solved.Error);

            std::vector<std::uint32_t> FirstHalf;
            std::vector<std::uint32_t> SecondHalf;
            Subdivide(Imported, Considering.Faces, FirstHalf, SecondHalf);

            Pending.push_back({ FirstHalf,  Considering.Attempts + 1u });
            Pending.push_back({ SecondHalf, Considering.Attempts + 1u });

            Progressed.DeclareCount(Resolved, Resolved + Pending.size());
            continue;
        }

        const std::vector<PlanarPosition>& Flattened = Solved.Resolve().Approximation;

        if (FoldDetected(Local, Flattened) && SubdivisionReachable)
        {
            ++Produced.Metrics.FoldCount;

            std::vector<std::uint32_t> FirstHalf;
            std::vector<std::uint32_t> SecondHalf;
            Subdivide(Imported, Considering.Faces, FirstHalf, SecondHalf);

            Pending.push_back({ FirstHalf,  Considering.Attempts + 1u });
            Pending.push_back({ SecondHalf, Considering.Attempts + 1u });

            Progressed.DeclareCount(Resolved, Resolved + Pending.size());
            continue;
        }

        Chart Accepting;
        Accepting.IdentityIndex  = IdentityIndex;
        Accepting.Faces            = Considering.Faces;
        Accepting.Cause            = Solved.Resolve().Cause;
        Accepting.ResidualNorm     = Solved.Resolve().ResidualNorm;
        Accepting.IterationCount   = Solved.Resolve().IterationCount;
        Accepting.SubdivisionCount = Considering.Attempts;
        Accepting.Distortion       = Measure(Local.Positions, Local.TriangleCorners, Flattened);

        if (Accepting.Cause == TerminationCause::LimitReached)
            ++Produced.Metrics.LimitTerminationCount;

        Accepted.push_back(Accepting);
        AcceptedFlattened.push_back(Flattened);
        AcceptedLocality.push_back(Local);

        ++Resolved;
        Progressed.DeclareCount(Resolved, Resolved + Pending.size());
    }

    // 📝 Every adjacency crossing two accepted charts is a cut. The authored ones are already declared, so what
    //    remains is exactly the set the partitioner added — which is what `86` reports and what `68` §2 requires
    //    to be reported rather than applied silently.
    for (std::uint32_t FaceIndex = 0u; FaceIndex < FaceSpan; ++FaceIndex)
    {
        const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
        const std::uint32_t CornerSpan  = Imported.FaceCornerCount(FaceIndex);

        for (std::uint32_t Passed = 0u; Passed < CornerSpan; ++Passed)
        {
            const std::uint32_t CornerIndex = FirstCorner + Passed;
            const std::uint32_t Following     = FirstCorner + (Passed + 1u) % CornerSpan;

            const Deliver<std::uint32_t> Adjacent = Conditioned.AdjacentCorner(CornerIndex);

            if (!Adjacent.Resolved)
                continue;

            const std::uint32_t AdjacentFace = Imported.CornerFace(Adjacent.Resolve());

            if (ChartOfFace[AdjacentFace] == ChartOfFace[FaceIndex])
                continue;

            const std::uint32_t OpeningVertex = Imported.CornerVertex(CornerIndex);
            const std::uint32_t ClosingVertex = Imported.CornerVertex(Following);

            const std::uint32_t OpeningWelded = Conditioned.WeldedPosition(OpeningVertex).Resolve();
            const std::uint32_t ClosingWelded = Conditioned.WeldedPosition(ClosingVertex).Resolve();

            if (KeyHeld(SeamKeys, EdgeKey(OpeningWelded, ClosingWelded)))
                continue;

            const SeamEdge Derived = DeclareEdge(OpeningVertex, ClosingVertex);

            bool Recorded = false;

            for (const SeamEdge& Held : Produced.DerivedSeams)
            {
                if (Held.MinimumVertex == Derived.MinimumVertex && Held.MaximumVertex == Derived.MaximumVertex)
                {
                    Recorded = true;
                    break;
                }
            }

            if (!Recorded)
                Produced.DerivedSeams.push_back(Derived);
        }
    }

    // 📐 Each chart's own extent, then one common scale over all of them. `68` §5's default: one texel of domain
    //    covers the same topology area on every chart, so the artist's brush behaves the same everywhere.
    std::vector<ChartExtent> Extents(Accepted.size());
    std::vector<double>      MinimumX(Accepted.size(), 0.0);
    std::vector<double>      MinimumY(Accepted.size(), 0.0);

    for (std::size_t Index = 0u; Index < Accepted.size(); ++Index)
    {
        const std::vector<PlanarPosition>& Flattened = AcceptedFlattened[Index];

        double MaximumX  = Flattened[0].PositionX;
        double MaximumY = Flattened[0].PositionY;

        MinimumX[Index]  = Flattened[0].PositionX;
        MinimumY[Index] = Flattened[0].PositionY;

        for (const PlanarPosition& Held : Flattened)
        {
            MinimumX[Index]  = Held.PositionX < MinimumX[Index]  ? Held.PositionX : MinimumX[Index];
            MinimumY[Index] = Held.PositionY < MinimumY[Index] ? Held.PositionY : MinimumY[Index];
            MaximumX            = Held.PositionX > MaximumX            ? Held.PositionX : MaximumX;
            MaximumY           = Held.PositionY > MaximumY           ? Held.PositionY : MaximumY;
        }

        Extents[Index].Width        = MaximumX  - MinimumX[Index];
        Extents[Index].Height       = MaximumY - MinimumY[Index];
        Extents[Index].ChartIndex = Accepted[Index].IdentityIndex;
    }

    DomainSpace Arranged;

    const Deliver<bool> Packed = Arranged.Arrange(Extents, Declaring.CommonScaleDeclared);

    if (!Packed.Resolved)
        return Deliver<DerivedPartition>::Refuse(Packed.Error);

    for (std::size_t Index = 0u; Index < Accepted.size(); ++Index)
    {
        const ChartPlacement&              Placement = Arranged.Placements()[Index];
        const ChartLocality&               Local     = AcceptedLocality[Index];
        const std::vector<PlanarPosition>& Flattened = AcceptedFlattened[Index];

        for (std::size_t Passed = 0u; Passed < Local.Corners.size(); ++Passed)
        {
            const PlanarPosition& Held = Flattened[Local.CornerLocals[Passed]];

            DomainCoordinate Writing;
            Writing.CoordinateX  = static_cast<float>(Placement.MinimumX
                                                        + (Held.PositionX - MinimumX[Index]) * Placement.Scale);
            Writing.CoordinateY = static_cast<float>(Placement.MinimumY
                                                        + (Held.PositionY - MinimumY[Index]) * Placement.Scale);

            Produced.CornerCoordinates[Local.Corners[Passed]] = Writing;
        }

        if (Accepted[Index].Distortion.MeasureDeclared)
        {
            if (Accepted[Index].Distortion.MaximumAreaRatio > Produced.Metrics.MaximumAreaRatio)
                Produced.Metrics.MaximumAreaRatio = Accepted[Index].Distortion.MaximumAreaRatio;

            if (Accepted[Index].Distortion.MaximumAngleDeviation > Produced.Metrics.MaximumAngleDeviation)
                Produced.Metrics.MaximumAngleDeviation = Accepted[Index].Distortion.MaximumAngleDeviation;
        }
    }

    Produced.Charts                    = Accepted;
    Produced.Metrics.ChartCount        = static_cast<std::uint32_t>(Accepted.size());
    Produced.Metrics.DerivedSeamCount  = static_cast<std::uint32_t>(Produced.DerivedSeams.size());
    Produced.Metrics.Occupancy         = Arranged.Occupancy();

    Progressed.DeclareCount(Resolved, Resolved);

    return Deliver<DerivedPartition>::Result(Produced);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE STANDING PARTITION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ChartPartition::Adopt(const DerivedPartition& Incoming)
{
    if (Incoming.Charts.empty())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a partition carrying no chart" });

    CurrentPartition = Incoming;

    // 🔴 Advanced on adoption and never on derivation. `24` §3 keys a transferred result on this revision and
    //    `20` promotes against it, so a revision that advanced while the previous partition still stood would
    //    invalidate artefacts addressed in a domain nothing had yet replaced.
    ++PartitionRevision;

    return Deliver<bool>::Result(true);
}

const DerivedPartition& ChartPartition::Current() const { return CurrentPartition; }

Deliver<DomainCoordinate> ChartPartition::Coordinate(std::uint32_t CornerIndex) const
{
    if (PartitionRevision == 0u)
    {
        return Deliver<DomainCoordinate>::Refuse(
            { RefusalReason::ContentUnsupported, "no partition stands for this surface" });
    }

    if (CornerIndex >= CurrentPartition.CornerCoordinates.size())
        return Deliver<DomainCoordinate>::Refuse({ RefusalReason::ExtentExhausted, "no such corner" });

    return Deliver<DomainCoordinate>::Result(CurrentPartition.CornerCoordinates[CornerIndex]);
}

bool          ChartPartition::PartitionCurrent() const { return PartitionRevision != 0u; }
std::uint64_t ChartPartition::Revision() const          { return PartitionRevision;       }

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void ChartPartition::Report(ReportSequence& Reporting, MeasureIndex& Measured, TickPoint Sampled) const
{
    if (PartitionRevision == 0u)
        return;

    // 📝 One appended entry per derived seam, carrying the chart it cut. `86` §6 coalesces by subject ordinal as
    //    well as by origin, so twelve distinct cuts present as twelve entries rather than as one with a count.
    for (const Chart& Held : CurrentPartition.Charts)
    {
        if (Held.Cause != TerminationCause::LimitReached)
            continue;

        ReportSpecification Terminated;
        Terminated.Origin         = "68 §4 ChartPartition";
        Terminated.Subject        = "Flattening";
        Terminated.Detail         = "the iteration ceiling terminated the solve; the result is the last iterate";
        Terminated.SubjectIndex = Held.IdentityIndex;
        Terminated.Verdict    = ReportVerdict::Terminated;
        Terminated.Arrival        = Sampled;

        Reporting.Append(Terminated);
    }

    for (const SeamEdge& Held : CurrentPartition.DerivedSeams)
    {
        ReportSpecification Amended;
        Amended.Origin         = "68 §2 ChartPartition";
        Amended.Subject        = "DerivedSeam";
        Amended.Detail         = "the authored seams did not admit a flattening; this edge was cut here";
        Amended.SubjectIndex = (static_cast<std::uint64_t>(Held.MinimumVertex) << 32) | Held.MaximumVertex;
        Amended.Verdict    = ReportVerdict::Amended;
        Amended.Arrival        = Sampled;

        Reporting.Append(Amended);
    }

    // 🔴 Occupancy and distortion overwrite. `86` §2: a measure appended every partition buries the one seam
    //    the artist did not expect under a thousand readings nobody asked for.
    Measured.DeclareMagnitude("68 §5 ChartPartition", "Occupancy", CurrentPartition.Metrics.Occupancy, Sampled);
    Measured.DeclareMagnitude("68 §4 ChartPartition", "AreaDistortion",
                              CurrentPartition.Metrics.MaximumAreaRatio, Sampled);
    Measured.DeclareMagnitude("68 §4 ChartPartition", "AngleDistortion",
                              CurrentPartition.Metrics.MaximumAngleDeviation, Sampled);
    Measured.DeclareCount("68 §3 ChartPartition", "ChartCount",
                          CurrentPartition.Metrics.ChartCount, Sampled);
}

}   // namespace Slate
