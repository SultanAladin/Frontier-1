/*====================================================================================================================================
                                                    CHARTTRIANGULATION.CPP
====================================================================================================================================*/
// 🧩 Constrained Delaunay triangulation of a face chart: Bowyer–Watson insertion, Sloan constraint recovery, parity interior

#include "Engine/GeometrySpace/Parametric/Tessellation/ChartTriangulation.h"

#include "Engine/GeometrySpace/Parametric/Precision/ExactPredicates.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] inline int Next(int Corner) noexcept { return (Corner + 1) % 3; }
    [[nodiscard]] inline int Prior(int Corner) noexcept { return (Corner + 2) % 3; }

    [[nodiscard]] inline int OrientationSign(Vector2 A, Vector2 B, Vector2 C) noexcept { return Orient2D(A, B, C).Sign; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

ChartTriangulation::ChartTriangulation(Vector2 Low, Vector2 High)
{
    const Vector2 Centre = (Low + High) * 0.5;                                               // [-] - Window centre
    const double  Span   = std::max({ High.X - Low.X, High.Y - Low.Y, 1e-9 });               // [-] - Window span
    PointList     = { { Centre.X - 40.0 * Span, Centre.Y - 30.0 * Span },
                      { Centre.X + 40.0 * Span, Centre.Y - 30.0 * Span },
                      { Centre.X,               Centre.Y + 50.0 * Span } };
    ChartTriangle Enclosing;
    Enclosing.Corners = { 0, 1, 2 };
    TriangleList      = { Enclosing };
    PointTriangle     = { 0, 0, 0 };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool ChartTriangulation::InsideCircumcircle(int Triangle, Vector2 Position)
{
    Counters.InCircleTests += 1;
    const auto& Corners = TriangleList[Triangle].Corners;
    return InCircle(PointList[Corners[0]], PointList[Corners[1]], PointList[Corners[2]], Position).Sign > 0;
}

int ChartTriangulation::LocateTriangle(Vector2 Position)
{
    int Current = LastTriangle;
    if (Current < 0 || Current >= static_cast<int>(TriangleList.size()) || !TriangleList[Current].Alive)
    {
        Current = 0;
        while (Current < static_cast<int>(TriangleList.size()) && !TriangleList[Current].Alive) Current += 1;
    }

    // ① Visibility walk with a rotating first side (remembering stochastic walk, Devillers–Pion–Teillaud 2002).
    const int64_t StepLimit = 4 * static_cast<int64_t>(TriangleList.size()) + 64;           // [-] - Cycle guard
    for (int64_t Step = 0; Step < StepLimit; ++Step)
    {
        Counters.LocateSteps += 1;
        const ChartTriangle& Triangle = TriangleList[Current];
        int  Crossing = -1;                                                                  // [-] - Side the point lies beyond
        for (int Offset = 0; Offset < 3; ++Offset)
        {
            const int Side = static_cast<int>((Offset + Step) % 3);
            if (OrientationSign(PointList[Triangle.Corners[Next(Side)]], PointList[Triangle.Corners[Prior(Side)]], Position) < 0)
            {
                Crossing = Side;
                break;
            }
        }
        if (Crossing < 0)
        {
            LastTriangle = Current;
            return Current;
        }
        if (Triangle.Neighbours[Crossing] < 0) return -1;
        Current = Triangle.Neighbours[Crossing];
    }

    // ② Exhaustive fallback.
    Counters.LocateFallbacks += 1;
    for (int Candidate = 0; Candidate < static_cast<int>(TriangleList.size()); ++Candidate)
    {
        const ChartTriangle& Triangle = TriangleList[Candidate];
        if (!Triangle.Alive) continue;
        bool Contained = true;
        for (int Side = 0; Side < 3 && Contained; ++Side)
        {
            Contained = OrientationSign(PointList[Triangle.Corners[Next(Side)]], PointList[Triangle.Corners[Prior(Side)]], Position) >= 0;
        }
        if (Contained)
        {
            LastTriangle = Candidate;
            return Candidate;
        }
    }
    return -1;
}

int ChartTriangulation::FindSide(int From, int To, int& Side)
{
    // Rotate around From through the fan; the side From → To has the triangle on its left. The fan of a hull vertex (an
    // enclosing-triangle corner) is open: on reaching its rim the rotation restarts the other way.
    const int Start = PointTriangle[From];
    for (int Direction = 0; Direction < 2; ++Direction)
    {
        int Current = Start;
        for (int Guard = 0; Guard < 1 << 20; ++Guard)
        {
            const ChartTriangle& Triangle = TriangleList[Current];
            int Corner = 0;
            while (Corner < 3 && Triangle.Corners[Corner] != From) Corner += 1;
            if (Corner == 3) return -1;
            if (Triangle.Corners[Next(Corner)] == To)
            {
                Side = Prior(Corner);
                return Current;
            }
            // Clockwise around From steps across side Prior(Corner) (From → Next); counter-clockwise across Next(Corner).
            const int Across = Triangle.Neighbours[Direction == 0 ? Prior(Corner) : Next(Corner)];
            if (Across == Start) return -1;                                                  // Closed fan, fully visited
            if (Across < 0) break;
            Current = Across;
        }
    }
    return -1;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSERTION
//------------------------------------------------------------------------------------------------------------------------

uint32_t ChartTriangulation::NextStamp()
{
    if (Stamp.size() < TriangleList.size() + 8) Stamp.resize(TriangleList.size() * 2 + 64, 0);
    StampValue += 1;
    return StampValue;
}

std::vector<int> ChartTriangulation::CollectCavity(Vector2 Position, int Container)
{
    std::vector<int> Cavity;
    if (Container < 0) return Cavity;
    const ChartTriangle& Holder = TriangleList[Container];

    // Points on a constrained side are refused; points on an unconstrained side force the neighbour into the cavity.
    std::array<bool, 3> OnSide = { false, false, false };
    for (int Side = 0; Side < 3; ++Side)
    {
        OnSide[Side] = OrientationSign(PointList[Holder.Corners[Next(Side)]], PointList[Holder.Corners[Prior(Side)]], Position) == 0;
        if (OnSide[Side] && Holder.Constrained[Side]) return Cavity;
    }

    const uint32_t Mark   = NextStamp();                                                    // [-] - Cavity membership stamp
    const auto     Member = [&](int Triangle) { return Stamp[Triangle] == Mark; };
    Cavity.push_back(Container);
    Stamp[Container] = Mark;
    for (size_t Cursor = 0; Cursor < Cavity.size(); ++Cursor)
    {
        const int Triangle = Cavity[Cursor];
        for (int Side = 0; Side < 3; ++Side)
        {
            const int Across = TriangleList[Triangle].Neighbours[Side];
            if (Across < 0 || Member(Across) || TriangleList[Triangle].Constrained[Side]) continue;
            const bool Forced = Triangle == Container && OnSide[Side];
            if (Forced || InsideCircumcircle(Across, Position))
            {
                Stamp[Across] = Mark;
                Cavity.push_back(Across);
            }
        }
    }

    // ⚠️ Keep the cavity star-shaped from the point: drop triangles owning a boundary side that does not strictly see it.
    for (bool Changed = true; Changed;)
    {
        Changed = false;
        for (int Triangle : Cavity)
        {
            if (Triangle == Container || !Member(Triangle)) continue;
            for (int Side = 0; Side < 3; ++Side)
            {
                const int Across = TriangleList[Triangle].Neighbours[Side];
                if (Across >= 0 && Member(Across)) continue;
                const auto& Corners = TriangleList[Triangle].Corners;
                if (OrientationSign(PointList[Corners[Next(Side)]], PointList[Corners[Prior(Side)]], Position) <= 0)
                {
                    Stamp[Triangle] = 0;
                    Changed         = true;
                    Counters.CavityRepairs += 1;
                    break;
                }
            }
        }
        if (!Changed) break;

        // Keep only triangles still connected to the container.
        std::vector<int> Connected = { Container };
        std::vector<char> Reached(TriangleList.size(), 0);
        Reached[Container] = 1;
        for (size_t Cursor = 0; Cursor < Connected.size(); ++Cursor)
        {
            for (int Side = 0; Side < 3; ++Side)
            {
                const int Across = TriangleList[Connected[Cursor]].Neighbours[Side];
                if (Across >= 0 && Member(Across) && !Reached[Across] && !TriangleList[Connected[Cursor]].Constrained[Side])
                {
                    Reached[Across] = 1;
                    Connected.push_back(Across);
                }
            }
        }
        for (int Triangle : Cavity) Stamp[Triangle] = Reached[Triangle] ? Mark : 0;
        Cavity = Connected;
    }
    return Cavity;
}

void ChartTriangulation::LinkNeighbour(int Triangle, int Side, int Neighbour)
{
    TriangleList[Triangle].Neighbours[Side] = Neighbour;
    if (Neighbour < 0) return;
    const int From = TriangleList[Triangle].Corners[Next(Side)];
    const int To   = TriangleList[Triangle].Corners[Prior(Side)];
    ChartTriangle& Other = TriangleList[Neighbour];
    for (int OtherSide = 0; OtherSide < 3; ++OtherSide)
    {
        if (Other.Corners[Next(OtherSide)] == To && Other.Corners[Prior(OtherSide)] == From)
        {
            Other.Neighbours[OtherSide] = Triangle;
            return;
        }
    }
}

int ChartTriangulation::CommitCavity(Vector2 Position, const std::vector<int>& Cavity, int Container)
{
    struct BoundarySide
    {
        int  From        = -1;                                                               // [-] - Side start point
        int  To          = -1;                                                               // [-] - Side end point
        int  Outside     = -1;                                                               // [-] - Triangle beyond the side
        bool Constrained = false;                                                            // [-] - Boundary segment flag
    };

    const uint32_t Mark = NextStamp();                                                      // [-] - Cavity membership stamp
    for (int Triangle : Cavity) Stamp[Triangle] = Mark;
    std::vector<BoundarySide> Boundary;
    for (int Triangle : Cavity)
    {
        const ChartTriangle& Owner = TriangleList[Triangle];
        for (int Side = 0; Side < 3; ++Side)
        {
            const int Across = Owner.Neighbours[Side];
            if (Across >= 0 && Stamp[Across] == Mark) continue;
            Boundary.push_back({ Owner.Corners[Next(Side)], Owner.Corners[Prior(Side)], Across, Owner.Constrained[Side] });
        }
    }

    const bool Interior = TriangleList[Container].Interior;                                  // [-] - Inherited classification
    const int  Point    = static_cast<int>(PointList.size());
    PointList.push_back(Position);
    PointTriangle.push_back(-1);

    // Fan of new triangles (From, To, Point); cavity slots are reused first (a disk cavity has |Boundary| = |Cavity| + 2).
    Recent.clear();
    for (size_t Index = 0; Index < Boundary.size(); ++Index)
    {
        int Slot;
        if (Index < Cavity.size()) Slot = Cavity[Index];
        else
        {
            Slot = static_cast<int>(TriangleList.size());
            TriangleList.emplace_back();
        }
        ChartTriangle& Fresh = TriangleList[Slot];
        Fresh             = ChartTriangle{};
        Fresh.Corners     = { Boundary[Index].From, Boundary[Index].To, Point };
        Fresh.Constrained = { false, false, Boundary[Index].Constrained };
        Fresh.Interior    = Interior;
        Recent.push_back(Slot);
    }
    for (size_t Index = Boundary.size(); Index < Cavity.size(); ++Index) TriangleList[Cavity[Index]].Alive = false;

    for (size_t Index = 0; Index < Boundary.size(); ++Index)
    {
        const int Slot = Recent[Index];
        LinkNeighbour(Slot, 2, Boundary[Index].Outside);
        for (size_t Other = 0; Other < Boundary.size(); ++Other)
        {
            if (Boundary[Other].From == Boundary[Index].To) TriangleList[Slot].Neighbours[0] = Recent[Other];
            if (Boundary[Other].To == Boundary[Index].From) TriangleList[Slot].Neighbours[1] = Recent[Other];
        }
        PointTriangle[Boundary[Index].From] = Slot;
        PointTriangle[Boundary[Index].To]   = Slot;
    }
    PointTriangle[Point] = Recent.front();
    LastTriangle         = Recent.front();
    return Point;
}

int ChartTriangulation::InsertPoint(Vector2 Position)
{
    int Container = LocateTriangle(Position);
    if (Container < 0) return -1;
    for (int Corner : TriangleList[Container].Corners)
    {
        if (PointList[Corner].X == Position.X && PointList[Corner].Y == Position.Y) return Corner;
    }
    const std::vector<int> Cavity = CollectCavity(Position, Container);
    if (Cavity.empty()) return -1;
    return CommitCavity(Position, Cavity, Container);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLIPS
//------------------------------------------------------------------------------------------------------------------------

void ChartTriangulation::FlipSide(int Triangle, int Side)
{
    // t1 = (p, u, v) with side u → v opposite p; t2 = (q, v, u). After the flip: t1 = (p, u, q), t2 = (q, v, p).
    const int First  = Triangle;
    const int Second = TriangleList[First].Neighbours[Side];
    if (Second < 0) return;
    int SecondSide = 0;
    while (SecondSide < 3 && TriangleList[Second].Neighbours[SecondSide] != First) SecondSide += 1;
    if (SecondSide == 3) return;

    const ChartTriangle Left  = TriangleList[First];
    const ChartTriangle Right = TriangleList[Second];
    const int P = Left.Corners[Side], U = Left.Corners[Next(Side)], V = Left.Corners[Prior(Side)];
    const int Q = Right.Corners[SecondSide];

    const int  AcrossVP = Left.Neighbours[Next(Side)];                                       // [-] - Side (v, p) of t1
    const bool LockVP   = Left.Constrained[Next(Side)];
    const int  AcrossPU = Left.Neighbours[Prior(Side)];                                      // [-] - Side (p, u) of t1
    const bool LockPU   = Left.Constrained[Prior(Side)];
    const int  AcrossUQ = Right.Neighbours[Next(SecondSide)];                                // [-] - Side (u, q) of t2
    const bool LockUQ   = Right.Constrained[Next(SecondSide)];
    const int  AcrossQV = Right.Neighbours[Prior(SecondSide)];                               // [-] - Side (q, v) of t2
    const bool LockQV   = Right.Constrained[Prior(SecondSide)];

    ChartTriangle& NewFirst  = TriangleList[First];
    ChartTriangle& NewSecond = TriangleList[Second];
    NewFirst.Corners      = { P, U, Q };
    NewFirst.Constrained  = { LockUQ, false, LockPU };
    NewSecond.Corners     = { Q, V, P };
    NewSecond.Constrained = { LockVP, false, LockQV };
    NewFirst.Neighbours[1]  = Second;
    NewSecond.Neighbours[1] = First;
    LinkNeighbour(First, 0, AcrossUQ);
    LinkNeighbour(First, 2, AcrossPU);
    LinkNeighbour(Second, 0, AcrossVP);
    LinkNeighbour(Second, 2, AcrossQV);
    PointTriangle[P] = First;
    PointTriangle[U] = First;
    PointTriangle[Q] = Second;
    PointTriangle[V] = Second;
    LastTriangle     = First;
}

bool ChartTriangulation::ConstrainSegment(int First, int Second)
{
    if (First == Second) return true;
    int Side = 0;
    const int Existing = FindSide(First, Second, Side);
    if (Existing >= 0)
    {
        TriangleList[Existing].Constrained[Side] = true;
        const int Across = TriangleList[Existing].Neighbours[Side];
        if (Across >= 0)
        {
            for (int OtherSide = 0; OtherSide < 3; ++OtherSide)
            {
                if (TriangleList[Across].Neighbours[OtherSide] == Existing) TriangleList[Across].Constrained[OtherSide] = true;
            }
        }
        return true;
    }

    const Vector2 A = PointList[First];
    const Vector2 B = PointList[Second];
    const auto Between = [&](int Point)
    {
        const Vector2 Offset = PointList[Point] - A;
        return OrientationSign(A, B, PointList[Point]) == 0 && Dot(Offset, B - A) > 0.0 && Dot(PointList[Point] - B, A - B) > 0.0;
    };

    // ① Find the triangle around First whose wedge holds the segment.
    int Current = PointTriangle[First];
    int Entry   = -1;                                                                        // [-] - Triangle the segment leaves First through
    const int RingLimit = static_cast<int>(TriangleList.size()) + 4;
    for (int Guard = 0; Current >= 0 && Guard < RingLimit; ++Guard)
    {
        const ChartTriangle& Triangle = TriangleList[Current];
        int Corner = 0;
        while (Corner < 3 && Triangle.Corners[Corner] != First) Corner += 1;
        if (Corner == 3) break;
        const int Ahead = Triangle.Corners[Next(Corner)], Behind = Triangle.Corners[Prior(Corner)];
        if (Between(Ahead)) return ConstrainSegment(First, Ahead) && ConstrainSegment(Ahead, Second);
        if (Between(Behind)) return ConstrainSegment(First, Behind) && ConstrainSegment(Behind, Second);
        if (OrientationSign(A, PointList[Ahead], B) > 0 && OrientationSign(A, PointList[Behind], B) < 0)
        {
            Entry = Current;
            break;
        }
        Current = Triangle.Neighbours[Prior(Corner)];
        if (Current < 0 || Current == PointTriangle[First]) break;
    }
    if (Entry < 0) return false;

    // ② Walk to Second, collecting crossed sides.
    std::deque<std::pair<int, int>> Crossed;
    {
        const ChartTriangle& Triangle = TriangleList[Entry];
        int Corner = 0;
        while (Corner < 3 && Triangle.Corners[Corner] != First) Corner += 1;
        if (Corner == 3 || Triangle.Constrained[Corner]) return false;
        Crossed.push_back({ Triangle.Corners[Next(Corner)], Triangle.Corners[Prior(Corner)] });
        Current = Triangle.Neighbours[Corner];
    }
    const int WalkLimit = 2 * static_cast<int>(TriangleList.size()) + 16;
    bool      ReachedSecond = false;
    for (int Guard = 0; Guard < WalkLimit; ++Guard)
    {
        if (Current < 0) return false;
        const ChartTriangle& Triangle = TriangleList[Current];
        const auto [EnteredFrom, EnteredTo] = Crossed.back();
        int Opposite = 0;
        while (Opposite < 3 && (Triangle.Corners[Opposite] == EnteredFrom || Triangle.Corners[Opposite] == EnteredTo)) Opposite += 1;
        if (Opposite == 3) return false;
        const int Apex = Triangle.Corners[Opposite];
        if (Apex == Second) { ReachedSecond = true; break; }
        if (Between(Apex)) return ConstrainSegment(First, Apex) && ConstrainSegment(Apex, Second);
        // Entered side runs EnteredTo → EnteredFrom in this triangle; leave through (Apex, ·) on the far side of A → B.
        const int  ApexSign = OrientationSign(A, B, PointList[Apex]);
        const int  Keep     = OrientationSign(A, B, PointList[EnteredFrom]) == -ApexSign ? EnteredFrom : EnteredTo; // [-] - Endpoint across A → B
        int ExitSide = 0;
        while (ExitSide < 3 && (Triangle.Corners[ExitSide] == Apex || Triangle.Corners[ExitSide] == Keep)) ExitSide += 1;
        if (ExitSide == 3 || Triangle.Constrained[ExitSide]) return false;
        Crossed.push_back({ Triangle.Corners[Next(ExitSide)], Triangle.Corners[Prior(ExitSide)] });
        Current = Triangle.Neighbours[ExitSide];
    }
    if (!ReachedSecond) return false;

    // ③ Sloan's flips: flip a crossing side whenever its quadrilateral is strictly convex; requeue sides that still cross.
    const auto ProperCrossing = [&](int From, int To)
    {
        const Vector2 P = PointList[From], Q = PointList[To];
        return OrientationSign(A, B, P) * OrientationSign(A, B, Q) < 0 && OrientationSign(P, Q, A) * OrientationSign(P, Q, B) < 0;
    };
    const size_t FlipLimit  = 64 * Crossed.size() * Crossed.size() + 1024;                   // [-] - Termination guard
    size_t       StaleCount = 0;                                                             // [-] - Consecutive unflipped requeues
    for (size_t Attempt = 0; !Crossed.empty() && Attempt < FlipLimit; ++Attempt)
    {
        const auto [From, To] = Crossed.front();
        Crossed.pop_front();
        int CrossSide = 0;
        const int Owner = FindSide(From, To, CrossSide);
        if (Owner < 0) continue;                                                             // Queued twice and already flipped away
        const int Across = TriangleList[Owner].Neighbours[CrossSide];
        if (Across < 0) return false;
        int AcrossSide = 0;
        while (AcrossSide < 3 && TriangleList[Across].Neighbours[AcrossSide] != Owner) AcrossSide += 1;
        if (AcrossSide == 3) return false;
        const int P = TriangleList[Owner].Corners[CrossSide], Q = TriangleList[Across].Corners[AcrossSide];
        if (OrientationSign(PointList[P], PointList[Q], PointList[From]) * OrientationSign(PointList[P], PointList[Q], PointList[To]) >= 0)
        {
            Crossed.push_back({ From, To });
            if (++StaleCount >= Crossed.size()) return false;                                // Full queue pass with zero convex quads
            continue;
        }
        StaleCount = 0;
        FlipSide(Owner, CrossSide);
        Counters.ConstraintFlips += 1;
        if (ProperCrossing(P, Q)) Crossed.push_back({ P, Q });
    }
    if (!Crossed.empty()) return false;
    // The flips leave First → Second as a side; the call below marks it constrained (one level deep when it exists).
    if (FindSide(First, Second, Side) < 0) return false;
    return ConstrainSegment(First, Second);
}

void ChartTriangulation::Legalize()
{
    std::vector<std::pair<int, int>> Pending;
    for (int Triangle = 0; Triangle < static_cast<int>(TriangleList.size()); ++Triangle)
    {
        if (!TriangleList[Triangle].Alive) continue;
        for (int Side = 0; Side < 3; ++Side) Pending.push_back({ Triangle, Side });
    }
    const int64_t LegalizeLimit = std::max<int64_t>(4096, int64_t(32) * static_cast<int64_t>(TriangleList.size()));
    int64_t       Guard         = 0;
    while (!Pending.empty() && Guard++ < LegalizeLimit)
    {
        const auto [Triangle, Side] = Pending.back();
        Pending.pop_back();
        const ChartTriangle& Owner = TriangleList[Triangle];
        if (!Owner.Alive || Owner.Constrained[Side]) continue;
        const int Across = Owner.Neighbours[Side];
        if (Across < 0) continue;
        int AcrossSide = 0;
        while (AcrossSide < 3 && TriangleList[Across].Neighbours[AcrossSide] != Triangle) AcrossSide += 1;
        if (AcrossSide == 3) continue;
        if (!InsideCircumcircle(Triangle, PointList[TriangleList[Across].Corners[AcrossSide]])) continue;
        FlipSide(Triangle, Side);
        Counters.LegalizingFlips += 1;
        for (int Fresh : { Triangle, Across })
        {
            for (int FreshSide = 0; FreshSide < 3; ++FreshSide) Pending.push_back({ Fresh, FreshSide });
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

void ChartTriangulation::ClassifyInterior()
{
    // 0-1 breadth-first search: parity of constrained sides crossed from the enclosing triangle's corners.
    std::vector<int> Level(TriangleList.size(), -1);
    std::deque<int>  Frontier;
    for (int Triangle = 0; Triangle < static_cast<int>(TriangleList.size()); ++Triangle)
    {
        const ChartTriangle& Owner = TriangleList[Triangle];
        if (!Owner.Alive) continue;
        if (Owner.Corners[0] < 3 || Owner.Corners[1] < 3 || Owner.Corners[2] < 3)
        {
            Level[Triangle] = 0;
            Frontier.push_back(Triangle);
        }
    }
    while (!Frontier.empty())
    {
        const int Triangle = Frontier.front();
        Frontier.pop_front();
        for (int Side = 0; Side < 3; ++Side)
        {
            const int Across = TriangleList[Triangle].Neighbours[Side];
            if (Across < 0) continue;
            const int Weight    = TriangleList[Triangle].Constrained[Side] ? 1 : 0;
            const int Candidate = Level[Triangle] + Weight;
            if (Level[Across] >= 0 && Level[Across] <= Candidate) continue;
            Level[Across] = Candidate;
            if (Weight == 0) Frontier.push_front(Across);
            else Frontier.push_back(Across);
        }
    }
    for (int Triangle = 0; Triangle < static_cast<int>(TriangleList.size()); ++Triangle)
    {
        TriangleList[Triangle].Interior = TriangleList[Triangle].Alive && Level[Triangle] >= 0 && (Level[Triangle] % 2) == 1;
    }
}

int ChartTriangulation::CountDelaunayViolations()
{
    int Violations = 0;
    for (int Triangle = 0; Triangle < static_cast<int>(TriangleList.size()); ++Triangle)
    {
        const ChartTriangle& Owner = TriangleList[Triangle];
        if (!Owner.Alive || !Owner.Interior) continue;
        for (int Side = 0; Side < 3; ++Side)
        {
            const int Across = Owner.Neighbours[Side];
            if (Owner.Constrained[Side] || Across < Triangle || !TriangleList[Across].Interior) continue;
            int AcrossSide = 0;
            while (AcrossSide < 3 && TriangleList[Across].Neighbours[AcrossSide] != Triangle) AcrossSide += 1;
            if (AcrossSide == 3) continue;
            Violations += InsideCircumcircle(Triangle, PointList[TriangleList[Across].Corners[AcrossSide]]) ? 1 : 0;
        }
    }
    return Violations;
}

} // namespace Frontier::GeometrySpace::Parametric
