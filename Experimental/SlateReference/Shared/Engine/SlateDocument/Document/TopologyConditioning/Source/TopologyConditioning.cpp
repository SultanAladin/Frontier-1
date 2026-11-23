//============================================================================================================================================
//                                                       TOPOLOGYCONDITIONING.CPP
//============================================================================================================================================
// 🧩 Lattice welding, corner adjacency, orientation consistency, and conservative extents.

#include "SlateDocument/Document/TopologyConditioning/Api/TopologyConditioning.h"

#include "Shared/OrientationClassifier.slang.h"
#include "Foundation/NumericTolerance.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE LATTICE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr std::uint32_t AbsentCorner = 0xFFFFFFFFu;   // [-] - no adjacent corner; never a valid ordinal

// 📝 🔴 Welding is decided on an integer lattice whose spacing is the declared relative tolerance, and the
//    comparison is between integer cells. That is what makes coincidence Exact and deterministic: two positions
//    weld when their lattice displacement is at most one along every axis, which is an integer decision no
//    rounding can reach. Comparing squared distances against a tolerance is the obvious alternative and it is
//    Bounded, so the same file would weld differently on two machines.
// ⚠️ The consequence is that coincidence extends to two lattice spacings in the worst case rather than exactly
//    one tolerance. That is a declared tolerance rather than a discovered one, which is what `38` §2 asks for.
struct LatticeCell
{
    std::int64_t  CellX  = 0;   // [-] - lattice ordinal along the first axis
    std::int64_t  CellY = 0;   // [-] - along the second
    std::int64_t  CellDeep   = 0;   // [-] - along the third
};

LatticeCell Quantise(DocumentPosition Subject, double Spacing)
{
    LatticeCell Cell;
    Cell.CellX  = static_cast<std::int64_t>(std::floor(Subject.PositionX / Spacing));
    Cell.CellY = static_cast<std::int64_t>(std::floor(Subject.PositionY / Spacing));
    Cell.CellDeep   = static_cast<std::int64_t>(std::floor(Subject.PositionZ / Spacing));

    return Cell;
}

std::uint64_t CellIndex(LatticeCell Cell)
{
    // 📐 A mixing of the three lattice ordinals into one search ordinal. Exact equality of the ordinals is
    //    confirmed after a candidate is found, so a collision costs a comparison and never a wrong weld.
    const std::uint64_t X  = static_cast<std::uint64_t>(Cell.CellX)  * 0x9E3779B97F4A7C15ull;
    const std::uint64_t Y = static_cast<std::uint64_t>(Cell.CellY) * 0xC2B2AE3D27D4EB4Full;
    const std::uint64_t Deep   = static_cast<std::uint64_t>(Cell.CellDeep)   * 0x165667B19E3779F9ull;

    return X ^ Y ^ Deep;
}

double MaximumSpan(const std::vector<DocumentPosition>& Positions)
{
    if (Positions.empty())
        return 1.0;

    DocumentPosition Minimum    = Positions[0];
    DocumentPosition Maximum = Positions[0];

    for (const DocumentPosition& Held : Positions)
    {
        Minimum.PositionX    = Held.PositionX < Minimum.PositionX    ? Held.PositionX : Minimum.PositionX;
        Minimum.PositionY    = Held.PositionY < Minimum.PositionY    ? Held.PositionY : Minimum.PositionY;
        Minimum.PositionZ    = Held.PositionZ < Minimum.PositionZ    ? Held.PositionZ : Minimum.PositionZ;
        Maximum.PositionX = Held.PositionX > Maximum.PositionX ? Held.PositionX : Maximum.PositionX;
        Maximum.PositionY = Held.PositionY > Maximum.PositionY ? Held.PositionY : Maximum.PositionY;
        Maximum.PositionZ = Held.PositionZ > Maximum.PositionZ ? Held.PositionZ : Maximum.PositionZ;
    }

    const double SpanX = Maximum.PositionX - Minimum.PositionX;
    const double SpanY = Maximum.PositionY - Minimum.PositionY;
    const double SpanZ = Maximum.PositionZ - Minimum.PositionZ;

    double LargestSpan = SpanX > SpanY ? SpanX : SpanY;
    LargestSpan        = LargestSpan > SpanZ ? LargestSpan : SpanZ;

    return LargestSpan > 0.0 ? LargestSpan : 1.0;
}

SurfaceDirection Normalise(double DirectionX, double DirectionY, double DirectionZ)
{
    SurfaceDirection Direction;

    const double Length = std::sqrt(DirectionX * DirectionX + DirectionY * DirectionY + DirectionZ * DirectionZ);

    if (Length <= 0.0)
        return Direction;

    Direction.DirectionX = static_cast<float>(DirectionX / Length);
    Direction.DirectionY = static_cast<float>(DirectionY / Length);
    Direction.DirectionZ = static_cast<float>(DirectionZ / Length);

    return Direction;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       WELDING
//------------------------------------------------------------------------------------------------------------------------

void TopologyConditioning::DeriveWelding(const TopologyStructure& Imported)
{
    const std::vector<DocumentPosition>& Positions = Imported.Positions();
    const double                         Spacing   = MaximumSpan(Positions) * WeldTolerance;

    WeldedPositionOfVertex.assign(Positions.size(), 0u);
    DistinctPositionCount = 0u;

    // 📝 One search run per mixed ordinal, holding the imported vertices already welded there. A vertex is
    //    compared against the twenty-seven cells around its own, so a position sitting just across a cell
    //    boundary from its twin still finds it.
    // 📝 ⚠️ DeriveWelding scans RunIndexs linearly for each neighbour cell (quadratic in run count).
    //    Recorded per `38` §5 for large models.
    std::vector<std::uint64_t>                 RunIndexs;
    std::vector<std::vector<std::uint32_t>>    RunVertices;
    std::vector<LatticeCell>                   CellOfVertex(Positions.size());

    for (std::uint32_t VertexIndex = 0u; VertexIndex < Positions.size(); ++VertexIndex)
    {
        const LatticeCell Cell = Quantise(Positions[VertexIndex], Spacing);
        CellOfVertex[VertexIndex] = Cell;

        std::uint32_t Welded = AbsentCorner;

        for (std::int64_t X = -1; X <= 1 && Welded == AbsentCorner; ++X)
        {
            for (std::int64_t Y = -1; Y <= 1 && Welded == AbsentCorner; ++Y)
            {
                for (std::int64_t Deep = -1; Deep <= 1 && Welded == AbsentCorner; ++Deep)
                {
                    LatticeCell Sought;
                    Sought.CellX  = Cell.CellX  + X;
                    Sought.CellY = Cell.CellY + Y;
                    Sought.CellDeep   = Cell.CellDeep   + Deep;

                    const std::uint64_t Index = CellIndex(Sought);

                    for (std::size_t RunIndex = 0u; RunIndex < RunIndexs.size(); ++RunIndex)
                    {
                        if (RunIndexs[RunIndex] != Index)
                            continue;

                        for (const std::uint32_t Candidate : RunVertices[RunIndex])
                        {
                            const LatticeCell Held = CellOfVertex[Candidate];

                            if (Held.CellX  == Sought.CellX
                             && Held.CellY == Sought.CellY
                             && Held.CellDeep   == Sought.CellDeep)
                            {
                                Welded = WeldedPositionOfVertex[Candidate];
                                break;
                            }
                        }

                        break;
                    }
                }
            }
        }

        if (Welded == AbsentCorner)
        {
            Welded = DistinctPositionCount;
            ++DistinctPositionCount;
        }

        WeldedPositionOfVertex[VertexIndex] = Welded;

        const std::uint64_t OwnIndex = CellIndex(Cell);
        std::size_t         Located     = RunIndexs.size();

        for (std::size_t RunIndex = 0u; RunIndex < RunIndexs.size(); ++RunIndex)
        {
            if (RunIndexs[RunIndex] == OwnIndex)
            {
                Located = RunIndex;
                break;
            }
        }

        if (Located == RunIndexs.size())
        {
            RunIndexs.push_back(OwnIndex);
            RunVertices.push_back({});
        }

        RunVertices[Located].push_back(VertexIndex);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ADJACENCY
//------------------------------------------------------------------------------------------------------------------------

void TopologyConditioning::DeriveAdjacency(const TopologyStructure& Imported)
{
    const std::uint32_t CornerSpan = Imported.CornerCount();

    AdjacentCornerOfCorner.assign(CornerSpan, AbsentCorner);
    FirstCornerOfPosition.assign(DistinctPositionCount, AbsentCorner);
    NextCornerOfPosition.assign(CornerSpan, AbsentCorner);

    // 📝 Incidence is over **welded positions**, not over imported vertices. A traversal that followed imported
    //    vertices would stop at every coordinate seam, which is precisely the discontinuity welding exists to
    //    see through.
    for (std::uint32_t CornerIndex = 0u; CornerIndex < CornerSpan; ++CornerIndex)
    {
        const std::uint32_t Position = WeldedPositionOfVertex[Imported.CornerVertex(CornerIndex)];

        NextCornerOfPosition[CornerIndex] = FirstCornerOfPosition[Position];
        FirstCornerOfPosition[Position]     = CornerIndex;
    }

    std::vector<std::uint32_t> IncidenceCountOfEdge;
    std::vector<std::uint32_t> MinimumOfEdge;
    std::vector<std::uint32_t> MaximumOfEdge;
    std::vector<std::uint32_t> FirstCornerOfEdge;
    std::vector<std::uint32_t> SecondCornerOfEdge;

    for (std::uint32_t FaceIndex = 0u; FaceIndex < Imported.FaceCount(); ++FaceIndex)
    {
        const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
        const std::uint32_t CornerSpan_ = Imported.FaceCornerCount(FaceIndex);

        for (std::uint32_t Passed = 0u; Passed < CornerSpan_; ++Passed)
        {
            const std::uint32_t CornerIndex = FirstCorner + Passed;
            const std::uint32_t Following     = FirstCorner + (Passed + 1u) % CornerSpan_;

            const std::uint32_t Opening = WeldedPositionOfVertex[Imported.CornerVertex(CornerIndex)];
            const std::uint32_t Closing = WeldedPositionOfVertex[Imported.CornerVertex(Following)];

            const std::uint32_t Minimum    = Opening < Closing ? Opening : Closing;
            const std::uint32_t Maximum = Opening < Closing ? Closing : Opening;

            std::size_t Located = MinimumOfEdge.size();

            for (std::size_t EdgeIndex = 0u; EdgeIndex < MinimumOfEdge.size(); ++EdgeIndex)
            {
                if (MinimumOfEdge[EdgeIndex] == Minimum && MaximumOfEdge[EdgeIndex] == Maximum)
                {
                    Located = EdgeIndex;
                    break;
                }
            }

            if (Located == MinimumOfEdge.size())
            {
                MinimumOfEdge.push_back(Minimum);
                MaximumOfEdge.push_back(Maximum);
                IncidenceCountOfEdge.push_back(1u);
                FirstCornerOfEdge.push_back(CornerIndex);
                SecondCornerOfEdge.push_back(AbsentCorner);
            }
            else
            {
                ++IncidenceCountOfEdge[Located];

                if (SecondCornerOfEdge[Located] == AbsentCorner)
                    SecondCornerOfEdge[Located] = CornerIndex;
            }
        }
    }

    for (std::size_t EdgeIndex = 0u; EdgeIndex < MinimumOfEdge.size(); ++EdgeIndex)
    {
        // 🔴 Only an edge with exactly two incidences yields an adjacency. An edge with more is non-manifold and
        //    every face it touches is registered, because `68` §4.1 cuts a chart boundary there rather than
        //    choosing one of several continuations arbitrarily.
        if (IncidenceCountOfEdge[EdgeIndex] == 2u)
        {
            const std::uint32_t First  = FirstCornerOfEdge[EdgeIndex];
            const std::uint32_t Second = SecondCornerOfEdge[EdgeIndex];

            AdjacentCornerOfCorner[First]  = Second;
            AdjacentCornerOfCorner[Second] = First;
        }
        else if (IncidenceCountOfEdge[EdgeIndex] > 2u)
        {
            RegisterInterval(RegisteredConditions[static_cast<std::size_t>(DegeneracySubject::NonManifoldEdge)],
                          Imported.CornerFace(FirstCornerOfEdge[EdgeIndex]));

            if (SecondCornerOfEdge[EdgeIndex] != AbsentCorner)
            {
                RegisterInterval(RegisteredConditions[static_cast<std::size_t>(DegeneracySubject::NonManifoldEdge)],
                              Imported.CornerFace(SecondCornerOfEdge[EdgeIndex]));
            }
        }
    }

    for (std::uint32_t Position = 0u; Position < DistinctPositionCount; ++Position)
    {
        if (FirstCornerOfPosition[Position] == AbsentCorner)
            RegisterInterval(RegisteredConditions[static_cast<std::size_t>(DegeneracySubject::IsolatedVertex)], Position);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ORIENTATION
//------------------------------------------------------------------------------------------------------------------------

void TopologyConditioning::DeriveOrientation(const TopologyStructure& Imported)
{
    const std::vector<DocumentPosition>& Positions = Imported.Positions();

    UnorientedFaceCount = 0u;

    for (std::uint32_t FaceIndex = 0u; FaceIndex < Imported.FaceCount(); ++FaceIndex)
    {
        const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
        const std::uint32_t CornerSpan  = Imported.FaceCornerCount(FaceIndex);

        // 📐 The Newell accumulation gives the face's own perpendicular without assuming planarity, and its
        //    dominant axis names the plane the face projects onto without degenerating.
        double NewellX = 0.0;
        double NewellY = 0.0;
        double NewellZ = 0.0;

        bool CornerRepeated = false;

        for (std::uint32_t Passed = 0u; Passed < CornerSpan; ++Passed)
        {
            const std::uint32_t Opening   = Imported.CornerVertex(FirstCorner + Passed);
            const std::uint32_t Closing   = Imported.CornerVertex(FirstCorner + (Passed + 1u) % CornerSpan);
            const DocumentPosition& Alpha = Positions[Opening];
            const DocumentPosition& Beta  = Positions[Closing];

            NewellX += (Alpha.PositionY - Beta.PositionY) * (Alpha.PositionZ + Beta.PositionZ);
            NewellY += (Alpha.PositionZ - Beta.PositionZ) * (Alpha.PositionX + Beta.PositionX);
            NewellZ += (Alpha.PositionX - Beta.PositionX) * (Alpha.PositionY + Beta.PositionY);

            for (std::uint32_t Compared = Passed + 1u; Compared < CornerSpan; ++Compared)
            {
                if (WeldedPositionOfVertex[Opening]
                 == WeldedPositionOfVertex[Imported.CornerVertex(FirstCorner + Compared)])
                {
                    CornerRepeated = true;
                }
            }
        }

        if (CornerRepeated)
        {
            RegisterInterval(RegisteredConditions[static_cast<std::size_t>(DegeneracySubject::RepeatedCorner)],
                          FaceIndex);
        }

        const double XMagnitude  = std::fabs(NewellX);
        const double YMagnitude = std::fabs(NewellY);
        const double DeepMagnitude   = std::fabs(NewellZ);

        std::uint32_t DominantAxis = 2u;

        if (XMagnitude >= YMagnitude && XMagnitude >= DeepMagnitude)
            DominantAxis = 0u;
        else if (YMagnitude >= DeepMagnitude)
            DominantAxis = 1u;

        // 🔴 The signed area's **sign** is taken from `02` §4's exact predicate over the projected corners, not
        //    from the Newell magnitude. `38` §6: a sign error inverts a face, and a face inverted from one camera
        //    angle is a defect the artist reads as a broken import rather than as arithmetic.
        Signed32 OrientationSignum = 0;

        for (std::uint32_t Passed = 1u; Passed + 1u < CornerSpan && OrientationSignum == 0; ++Passed)
        {
            const DocumentPosition& Alpha = Positions[Imported.CornerVertex(FirstCorner)];
            const DocumentPosition& Beta  = Positions[Imported.CornerVertex(FirstCorner + Passed)];
            const DocumentPosition& Gamma = Positions[Imported.CornerVertex(FirstCorner + Passed + 1u)];

            if (DominantAxis == 0u)
            {
                OrientationSignum = ClassifyOrientation(Alpha.PositionY, Alpha.PositionZ,
                                                        Beta.PositionY,  Beta.PositionZ,
                                                        Gamma.PositionY, Gamma.PositionZ);
            }
            else if (DominantAxis == 1u)
            {
                OrientationSignum = ClassifyOrientation(Alpha.PositionZ, Alpha.PositionX,
                                                        Beta.PositionZ,  Beta.PositionX,
                                                        Gamma.PositionZ, Gamma.PositionX);
            }
            else
            {
                OrientationSignum = ClassifyOrientation(Alpha.PositionX, Alpha.PositionY,
                                                        Beta.PositionX,  Beta.PositionY,
                                                        Gamma.PositionX, Gamma.PositionY);
            }
        }

        if (OrientationSignum == 0)
        {
            RegisterInterval(RegisteredConditions[static_cast<std::size_t>(DegeneracySubject::ZeroExtentFace)],
                          FaceIndex);
        }
    }

    // 📝 Consistency is a second pass, because it compares a face against an adjacency the first pass had not
    //    finished deriving. Two faces are consistent when they traverse their shared edge in opposite directions.
    for (std::uint32_t CornerIndex = 0u; CornerIndex < Imported.CornerCount(); ++CornerIndex)
    {
        const std::uint32_t Adjacent = AdjacentCornerOfCorner[CornerIndex];

        if (Adjacent == AbsentCorner)
            continue;

        const std::uint32_t FaceIndex     = Imported.CornerFace(CornerIndex);
        const std::uint32_t AdjacentFace    = Imported.CornerFace(Adjacent);

        const std::uint32_t Opening         = WeldedPositionOfVertex[Imported.CornerVertex(CornerIndex)];
        const std::uint32_t AdjacentOpening = WeldedPositionOfVertex[Imported.CornerVertex(Adjacent)];

        if (Opening == AdjacentOpening)
        {
            // 📝 Both faces open the shared edge at the same position, so both traverse it the same way and
            //    their orientations disagree. Registered and reported; `38` §3 renders it both-sided rather than
            //    reversing the artist's winding.
            const std::size_t Condition = static_cast<std::size_t>(DegeneracySubject::Unoriented);

            if (RegisterInterval(RegisteredConditions[Condition], FaceIndex))
                ++UnorientedFaceCount;

            if (RegisterInterval(RegisteredConditions[Condition], AdjacentFace))
                ++UnorientedFaceCount;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DIRECTION DERIVATION
//------------------------------------------------------------------------------------------------------------------------

void TopologyConditioning::DerivePerpendiculars(const TopologyStructure& Imported)
{
    if (Imported.PerpendicularsSupplied())
    {
        // 🔴 A supplied perpendicular is retained. `38` §7: an imported basis is used as supplied, and the same
        //    reasoning binds the perpendicular — reproducing the author's appearance requires reproducing it.
        DerivedPerpendiculars = Imported.Perpendiculars();
        return;
    }

    const std::vector<DocumentPosition>& Positions = Imported.Positions();

    std::vector<double> AccumulatedX(DistinctPositionCount, 0.0);
    std::vector<double> AccumulatedY(DistinctPositionCount, 0.0);
    std::vector<double> AccumulatedZ(DistinctPositionCount, 0.0);

    for (std::uint32_t FaceIndex = 0u; FaceIndex < Imported.FaceCount(); ++FaceIndex)
    {
        if (FaceRegistered(FaceIndex, DegeneracySubject::ZeroExtentFace))
            continue;

        const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
        const std::uint32_t CornerSpan  = Imported.FaceCornerCount(FaceIndex);

        double NewellX = 0.0;
        double NewellY = 0.0;
        double NewellZ = 0.0;

        for (std::uint32_t Passed = 0u; Passed < CornerSpan; ++Passed)
        {
            const DocumentPosition& Alpha =
                Positions[Imported.CornerVertex(FirstCorner + Passed)];
            const DocumentPosition& Beta =
                Positions[Imported.CornerVertex(FirstCorner + (Passed + 1u) % CornerSpan)];

            NewellX += (Alpha.PositionY - Beta.PositionY) * (Alpha.PositionZ + Beta.PositionZ);
            NewellY += (Alpha.PositionZ - Beta.PositionZ) * (Alpha.PositionX + Beta.PositionX);
            NewellZ += (Alpha.PositionX - Beta.PositionX) * (Alpha.PositionY + Beta.PositionY);
        }

        // 📐 Accumulated unnormalised, so each face contributes in proportion to its own area. Normalising per
        //    face first would give a sliver the same weight as the face beside it, and the shading tilts toward
        //    whichever slivers the source happened to contain.
        for (std::uint32_t Passed = 0u; Passed < CornerSpan; ++Passed)
        {
            const std::uint32_t Position =
                WeldedPositionOfVertex[Imported.CornerVertex(FirstCorner + Passed)];

            AccumulatedX[Position] += NewellX;
            AccumulatedY[Position] += NewellY;
            AccumulatedZ[Position] += NewellZ;
        }
    }

    DerivedPerpendiculars.assign(Positions.size(), SurfaceDirection{});

    for (std::uint32_t VertexIndex = 0u; VertexIndex < Positions.size(); ++VertexIndex)
    {
        const std::uint32_t Position = WeldedPositionOfVertex[VertexIndex];

        DerivedPerpendiculars[VertexIndex] = Normalise(AccumulatedX[Position],
                                                         AccumulatedY[Position],
                                                         AccumulatedZ[Position]);
    }
}

void TopologyConditioning::DeriveTangentBases(const TopologyStructure& Imported)
{
    if (Imported.TangentBasesSupplied())
    {
        DerivedTangentBases = Imported.TangentBases();
        BasesRetained       = true;
        return;
    }

    BasesRetained = false;
    DerivedTangentBases.assign(Imported.VertexCount(), TangentBasis{});

    // 🔴 The basis derives from the **domain** parameterisation — `18` §1.1 — which is what makes a perturbation
    //    authored in `22` and one transferred in `24` agree. With no coordinates there is no domain, so the basis
    //    is marked absent rather than substituted: `18` §1.1's rule is that the perturbation channels are then
    //    not sampled at all, and an orthonormalised substitute would be a fabricated value.
    if (!Imported.CoordinatesSupplied())
        return;

    const std::vector<DocumentPosition>& Positions   = Imported.Positions();
    const std::vector<DomainCoordinate>& Coordinates = Imported.Coordinates();

    std::vector<double> AccumulatedX(Imported.VertexCount(), 0.0);
    std::vector<double> AccumulatedY(Imported.VertexCount(), 0.0);
    std::vector<double> AccumulatedZ(Imported.VertexCount(), 0.0);
    std::vector<double> AccumulatedHandedness(Imported.VertexCount(), 0.0);

    for (std::uint32_t FaceIndex = 0u; FaceIndex < Imported.FaceCount(); ++FaceIndex)
    {
        if (FaceRegistered(FaceIndex, DegeneracySubject::ZeroExtentFace))
            continue;

        const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
        const std::uint32_t CornerSpan  = Imported.FaceCornerCount(FaceIndex);

        for (std::uint32_t Passed = 1u; Passed + 1u < CornerSpan; ++Passed)
        {
            const std::uint32_t AlphaCorner = FirstCorner;
            const std::uint32_t BetaCorner  = FirstCorner + Passed;
            const std::uint32_t GammaCorner = FirstCorner + Passed + 1u;

            const DocumentPosition& Alpha = Positions[Imported.CornerVertex(AlphaCorner)];
            const DocumentPosition& Beta  = Positions[Imported.CornerVertex(BetaCorner)];
            const DocumentPosition& Gamma = Positions[Imported.CornerVertex(GammaCorner)];

            const double FirstSpanX = Beta.PositionX - Alpha.PositionX;
            const double FirstSpanY = Beta.PositionY - Alpha.PositionY;
            const double FirstSpanZ = Beta.PositionZ - Alpha.PositionZ;

            const double SecondSpanX = Gamma.PositionX - Alpha.PositionX;
            const double SecondSpanY = Gamma.PositionY - Alpha.PositionY;
            const double SecondSpanZ = Gamma.PositionZ - Alpha.PositionZ;

            const double FirstX  = static_cast<double>(Coordinates[BetaCorner].CoordinateX)
                                     - static_cast<double>(Coordinates[AlphaCorner].CoordinateX);
            const double FirstY = static_cast<double>(Coordinates[BetaCorner].CoordinateY)
                                     - static_cast<double>(Coordinates[AlphaCorner].CoordinateY);
            const double SecondX  = static_cast<double>(Coordinates[GammaCorner].CoordinateX)
                                      - static_cast<double>(Coordinates[AlphaCorner].CoordinateX);
            const double SecondY = static_cast<double>(Coordinates[GammaCorner].CoordinateY)
                                      - static_cast<double>(Coordinates[AlphaCorner].CoordinateY);

            const double DomainArea = FirstX * SecondY - SecondX * FirstY;

            // 📝 A chart of zero area in the domain contributes nothing rather than a division by it. `18` §1.1
            //    marks the basis absent exactly there, and this is the same condition seen from the derivation.
            if (DomainArea == 0.0)
                continue;

            const double Reciprocal = 1.0 / DomainArea;

            const double TangentX = (SecondY * FirstSpanX - FirstY * SecondSpanX) * Reciprocal;
            const double TangentY = (SecondY * FirstSpanY - FirstY * SecondSpanY) * Reciprocal;
            const double TangentZ = (SecondY * FirstSpanZ - FirstY * SecondSpanZ) * Reciprocal;

            const double YX = (FirstX * SecondSpanX - SecondX * FirstSpanX) * Reciprocal;
            const double YY = (FirstX * SecondSpanY - SecondX * FirstSpanY) * Reciprocal;
            const double YZ = (FirstX * SecondSpanZ - SecondX * FirstSpanZ) * Reciprocal;

            const std::uint32_t Corners[3] = { AlphaCorner, BetaCorner, GammaCorner };

            for (std::uint32_t Index = 0u; Index < 3u; ++Index)
            {
                const std::uint32_t VertexIndex = Imported.CornerVertex(Corners[Index]);

                AccumulatedX[VertexIndex] += TangentX;
                AccumulatedY[VertexIndex] += TangentY;
                AccumulatedZ[VertexIndex] += TangentZ;

                // 📐 The handedness is the sign of the across-direction against the perpendicular crossed with
                //    the tangent. It is accumulated and then taken by sign, so a domain that mirrors across a
                //    seam records the inversion on the side it happens rather than averaging the two away.
                const SurfaceDirection& Perpendicular = DerivedPerpendiculars[VertexIndex];

                const double CrossX = static_cast<double>(Perpendicular.DirectionY) * TangentZ
                                    - static_cast<double>(Perpendicular.DirectionZ) * TangentY;
                const double CrossY = static_cast<double>(Perpendicular.DirectionZ) * TangentX
                                    - static_cast<double>(Perpendicular.DirectionX) * TangentZ;
                const double CrossZ = static_cast<double>(Perpendicular.DirectionX) * TangentY
                                    - static_cast<double>(Perpendicular.DirectionY) * TangentX;

                AccumulatedHandedness[VertexIndex] += CrossX * YX + CrossY * YY + CrossZ * YZ;
            }
        }
    }

    for (std::uint32_t VertexIndex = 0u; VertexIndex < Imported.VertexCount(); ++VertexIndex)
    {
        const SurfaceDirection Tangent = Normalise(AccumulatedX[VertexIndex],
                                                   AccumulatedY[VertexIndex],
                                                   AccumulatedZ[VertexIndex]);

        const bool Degenerate = Tangent.DirectionX == 0.0f
                             && Tangent.DirectionY == 0.0f
                             && Tangent.DirectionZ == 0.0f;

        DerivedTangentBases[VertexIndex].Tangent          = Tangent;
        DerivedTangentBases[VertexIndex].HandednessSignum =
            AccumulatedHandedness[VertexIndex] < 0.0 ? -1.0f : 1.0f;
        DerivedTangentBases[VertexIndex].BasisDeclared    = !Degenerate;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       EXTENTS
//------------------------------------------------------------------------------------------------------------------------

void TopologyConditioning::DeriveExtents(const TopologyStructure& Imported)
{
    const std::vector<DocumentPosition>& Positions = Imported.Positions();

    DerivedFaceExtents.assign(Imported.FaceCount(), ConditionedExtent{});

    bool WholeDeclared = false;

    for (std::uint32_t FaceIndex = 0u; FaceIndex < Imported.FaceCount(); ++FaceIndex)
    {
        const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);
        const std::uint32_t CornerSpan  = Imported.FaceCornerCount(FaceIndex);

        DocumentPosition Minimum    = Positions[Imported.CornerVertex(FirstCorner)];
        DocumentPosition Maximum = Minimum;

        for (std::uint32_t Passed = 1u; Passed < CornerSpan; ++Passed)
        {
            const DocumentPosition& Held = Positions[Imported.CornerVertex(FirstCorner + Passed)];

            Minimum.PositionX    = Held.PositionX < Minimum.PositionX    ? Held.PositionX : Minimum.PositionX;
            Minimum.PositionY    = Held.PositionY < Minimum.PositionY    ? Held.PositionY : Minimum.PositionY;
            Minimum.PositionZ    = Held.PositionZ < Minimum.PositionZ    ? Held.PositionZ : Minimum.PositionZ;
            Maximum.PositionX = Held.PositionX > Maximum.PositionX ? Held.PositionX : Maximum.PositionX;
            Maximum.PositionY = Held.PositionY > Maximum.PositionY ? Held.PositionY : Maximum.PositionY;
            Maximum.PositionZ = Held.PositionZ > Maximum.PositionZ ? Held.PositionZ : Maximum.PositionZ;
        }

        // 🔴 Rounded outward by one representable step on every face of the extent. `38` §6: an inward-rounded
        //    extent excludes a face from traversal, and the artist meets it as a surface with a thin band along
        //    one edge that cannot be selected or textured.
        Minimum.PositionX    = std::nextafter(Minimum.PositionX,    -HUGE_VAL);
        Minimum.PositionY    = std::nextafter(Minimum.PositionY,    -HUGE_VAL);
        Minimum.PositionZ    = std::nextafter(Minimum.PositionZ,    -HUGE_VAL);
        Maximum.PositionX = std::nextafter(Maximum.PositionX,  HUGE_VAL);
        Maximum.PositionY = std::nextafter(Maximum.PositionY,  HUGE_VAL);
        Maximum.PositionZ = std::nextafter(Maximum.PositionZ,  HUGE_VAL);

        DerivedFaceExtents[FaceIndex].Minimum    = Minimum;
        DerivedFaceExtents[FaceIndex].Maximum = Maximum;

        if (!WholeDeclared)
        {
            WholeExtent   = DerivedFaceExtents[FaceIndex];
            WholeDeclared = true;
            continue;
        }

        WholeExtent.Minimum.PositionX    = Minimum.PositionX < WholeExtent.Minimum.PositionX
                                       ? Minimum.PositionX : WholeExtent.Minimum.PositionX;
        WholeExtent.Minimum.PositionY    = Minimum.PositionY < WholeExtent.Minimum.PositionY
                                       ? Minimum.PositionY : WholeExtent.Minimum.PositionY;
        WholeExtent.Minimum.PositionZ    = Minimum.PositionZ < WholeExtent.Minimum.PositionZ
                                       ? Minimum.PositionZ : WholeExtent.Minimum.PositionZ;
        WholeExtent.Maximum.PositionX = Maximum.PositionX > WholeExtent.Maximum.PositionX
                                       ? Maximum.PositionX : WholeExtent.Maximum.PositionX;
        WholeExtent.Maximum.PositionY = Maximum.PositionY > WholeExtent.Maximum.PositionY
                                       ? Maximum.PositionY : WholeExtent.Maximum.PositionY;
        WholeExtent.Maximum.PositionZ = Maximum.PositionZ > WholeExtent.Maximum.PositionZ
                                       ? Maximum.PositionZ : WholeExtent.Maximum.PositionZ;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CONDITIONING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> TopologyConditioning::Condition(const TopologyStructure& Imported)
{
    if (!Imported.Sealed())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "an unsealed topology is not immutable for the run" });
    }

    for (std::size_t Condition = 0u; Condition < DegeneracySpan; ++Condition)
        RegisteredConditions[Condition].clear();

    // 📝 The order is load-bearing rather than incidental. Welding precedes adjacency because incidence is over
    //    welded positions; adjacency precedes orientation because consistency compares against it; orientation
    //    precedes the perpendiculars so a degenerate face contributes nothing to them; and the perpendiculars
    //    precede the bases because handedness is taken against them.
    DeriveWelding(Imported);
    DeriveAdjacency(Imported);
    DeriveOrientation(Imported);
    DerivePerpendiculars(Imported);
    DeriveTangentBases(Imported);
    DeriveExtents(Imported);

    DescribedRevision = Imported.Revision();

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> TopologyConditioning::WeldedPosition(std::uint32_t VertexIndex) const
{
    if (VertexIndex >= WeldedPositionOfVertex.size())
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "no such imported vertex" });
    }

    return Deliver<std::uint32_t>::Result(WeldedPositionOfVertex[VertexIndex]);
}

Deliver<std::uint32_t> TopologyConditioning::AdjacentCorner(std::uint32_t CornerIndex) const
{
    if (CornerIndex >= AdjacentCornerOfCorner.size())
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no such corner" });

    if (AdjacentCornerOfCorner[CornerIndex] == AbsentCorner)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "the edge is a boundary or is non-manifold" });
    }

    return Deliver<std::uint32_t>::Result(AdjacentCornerOfCorner[CornerIndex]);
}

bool TopologyConditioning::FaceRegistered(std::uint32_t FaceIndex, DegeneracySubject Condition) const
{
    return IntervalRegistered(RegisteredConditions[static_cast<std::size_t>(Condition)], FaceIndex);
}

bool TopologyConditioning::VertexIsolated(std::uint32_t VertexIndex) const
{
    if (VertexIndex >= WeldedPositionOfVertex.size())
        return false;

    const std::size_t Condition = static_cast<std::size_t>(DegeneracySubject::IsolatedVertex);

    return IntervalRegistered(RegisteredConditions[Condition], WeldedPositionOfVertex[VertexIndex]);
}

const std::vector<RegisteredInterval>& TopologyConditioning::Registered(DegeneracySubject Condition) const
{
    return RegisteredConditions[static_cast<std::size_t>(Condition)];
}

const std::vector<SurfaceDirection>&  TopologyConditioning::Perpendiculars() const { return DerivedPerpendiculars; }
const std::vector<TangentBasis>&      TopologyConditioning::TangentBases() const   { return DerivedTangentBases;   }
const std::vector<ConditionedExtent>& TopologyConditioning::FaceExtents() const    { return DerivedFaceExtents;    }

ConditionedExtent TopologyConditioning::TopologyExtent() const     { return WholeExtent;           }
std::uint32_t     TopologyConditioning::WeldedCount() const        { return DistinctPositionCount; }
std::uint64_t     TopologyConditioning::ConditionedRevision() const { return DescribedRevision;    }
bool              TopologyConditioning::TangentBasesRetained() const { return BasesRetained;       }
std::uint32_t     TopologyConditioning::UnorientedCount() const     { return UnorientedFaceCount;  }

}   // namespace Slate
