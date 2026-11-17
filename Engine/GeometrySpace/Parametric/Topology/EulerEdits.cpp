/*====================================================================================================================================
                                                         EULEREDITS.CPP
====================================================================================================================================*/
// 🧩 Euler edits — the only topology mutations; each preserves V − E + 2F − L = 2(S − G)

#include "Engine/GeometrySpace/Parametric/Topology/EulerEdits.h"

#include <algorithm>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void LinkFins(ModelPartition& Partition, FinToken Leading, FinToken Trailing)
    {
        Partition.Fins.Resolve(Leading).Successor    = Trailing;
        Partition.Fins.Resolve(Trailing).Predecessor = Leading;
    }

    // Re-home every fin of the cycle that contains Entry.
    void AssignCycleLoop(ModelPartition& Partition, FinToken Entry, LoopToken Loop)
    {
        FinToken     Cursor     = Entry;                                                     // [-] - Walking fin
        size_t       GuardCount = 0;                                                         // [-] - Breaks corrupt cycles
        const size_t GuardLimit = Partition.Fins.OccupiedCount() + 1;                        // [-] - Upper bound on a cycle
        do
        {
            Partition.Fins.Resolve(Cursor).Loop = Loop;
            Cursor = Partition.Fins.Resolve(Cursor).Successor;
        } while (!(Cursor == Entry) && ++GuardCount < GuardLimit);
    }

    // Issue an edge with its two fins (forward and reversed) already ringed around it.
    struct EdgeIssue
    {
        EdgeToken Edge     = {};                                                             // [-] - New edge
        FinToken  Forward  = {};                                                             // [-] - Fin along increasing τ
        FinToken  Reversed = {};                                                             // [-] - Fin along decreasing τ
    };

    [[nodiscard]] EdgeIssue IssueEdge(ModelPartition& Partition, VertexToken Start, VertexToken End, LoopToken ForwardLoop, LoopToken ReversedLoop)
    {
        EdgeIssue Issue;
        Issue.Edge     = Partition.Edges.Issue({ .Start = Start, .End = End });
        Issue.Forward  = Partition.Fins.Issue({ .Loop = ForwardLoop, .Edge = Issue.Edge, .Sense = Orientation::Forward });
        Issue.Reversed = Partition.Fins.Issue({ .Loop = ReversedLoop, .Edge = Issue.Edge, .Sense = Orientation::Reversed });
        Partition.Fins.Resolve(Issue.Forward).RadialSuccessor  = Issue.Reversed;
        Partition.Fins.Resolve(Issue.Reversed).RadialSuccessor = Issue.Forward;
        Partition.Edges.Resolve(Issue.Edge).RadialFin          = Issue.Forward;
        return Issue;
    }

    void ReclaimEdge(ModelPartition& Partition, EdgeToken Edge, FinToken First, FinToken Second)
    {
        const CurveToken Curve = Partition.Edges.Resolve(Edge).Curve;                        // [-] - Carrier to reclaim
        Partition.Fins.Reclaim(First);
        Partition.Fins.Reclaim(Second);
        Partition.Edges.Reclaim(Edge);
        if (Curve.PresentCondition()) Partition.Curves.Reclaim(Curve);
    }

    // The two fins of a manifold edge; refuses anything else.
    [[nodiscard]] bool ResolveEdgeFins(const ModelPartition& Partition, EdgeToken Edge, FinToken& First, FinToken& Second)
    {
        if (!Partition.Edges.Authenticate(Edge)) return false;
        First  = Partition.Edges.Resolve(Edge).RadialFin;
        Second = Partition.Fins.Resolve(First).RadialSuccessor;
        return !(First == Second) && Partition.Fins.Resolve(Second).RadialSuccessor == First;
    }

    [[nodiscard]] bool CornerValidCondition(const ModelPartition& Partition, LoopCorner Corner)
    {
        if (!Partition.Loops.Authenticate(Corner.Loop)) return false;
        const LoopProfile& Loop = Partition.Loops.Resolve(Corner.Loop);
        if (!Corner.Fin.PresentCondition()) return Loop.IsolatedVertex.PresentCondition();
        return Partition.Fins.Authenticate(Corner.Fin) && Partition.Fins.Resolve(Corner.Fin).Loop == Corner.Loop;
    }

    [[nodiscard]] VertexToken CornerVertex(const ModelPartition& Partition, LoopCorner Corner)
    {
        return Corner.Fin.PresentCondition() ? Partition.FinStart(Corner.Fin) : Partition.Loops.Resolve(Corner.Loop).IsolatedVertex;
    }

    template <typename Token>
    void EraseToken(std::vector<Token>& Sequence, Token Target)
    {
        Sequence.erase(std::remove(Sequence.begin(), Sequence.end(), Target), Sequence.end());
    }

    // 💡 A face split off another face lies on the same carrier: it receives its own copy of the surface and sense.
    void InheritSurface(ModelPartition& Partition, FaceToken Source, FaceToken Target)
    {
        const FaceProfile& Origin = Partition.Faces.Resolve(Source);
        if (!Origin.Surface.PresentCondition()) return;
        const SurfaceGeometry Copy  = Partition.Surfaces.Resolve(Origin.Surface);           // [-] - Carrier copy
        const Orientation     Sense = Origin.Sense;                                          // [-] - Carrier sense
        Partition.AttachSurface(Target, Copy, Sense);
    }

    void ReclaimFace(ModelPartition& Partition, FaceToken Face)
    {
        FaceProfile& Profile = Partition.Faces.Resolve(Face);
        EraseToken(Partition.Shells.Resolve(Profile.Shell).Faces, Face);
        if (Profile.Surface.PresentCondition()) Partition.Surfaces.Reclaim(Profile.Surface);
        Partition.Faces.Reclaim(Face);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTIVE EDITS
//------------------------------------------------------------------------------------------------------------------------

EditOutcome<AcornOutcome> ConstructBodyFaceVertex(ModelPartition& Partition, Vector3 Position)
{
    AcornOutcome Acorn;
    Acorn.Body           = Partition.Bodies.Issue({ .Category = BodyCategory::Acorn });
    Acorn.ExteriorRegion = Partition.Regions.Issue({ .Body = Acorn.Body, .Material = RegionMaterial::Void });
    Acorn.SolidRegion    = Partition.Regions.Issue({ .Body = Acorn.Body, .Material = RegionMaterial::Solid });
    Acorn.Shell          = Partition.Shells.Issue({ .Region = Acorn.SolidRegion });
    Acorn.Face           = Partition.Faces.Issue({ .Shell = Acorn.Shell });
    Acorn.Vertex         = Partition.Vertices.Issue({ .Position = Position });
    Acorn.Loop           = Partition.Loops.Issue({ .Face = Acorn.Face, .IsolatedVertex = Acorn.Vertex });
    Partition.Bodies.Resolve(Acorn.Body).Regions       = { Acorn.ExteriorRegion, Acorn.SolidRegion };
    Partition.Regions.Resolve(Acorn.SolidRegion).Shells = { Acorn.Shell };
    Partition.Shells.Resolve(Acorn.Shell).Faces         = { Acorn.Face };
    Partition.Faces.Resolve(Acorn.Face).Loops           = { Acorn.Loop };
    return { Acorn };
}

EditOutcome<EdgeVertexOutcome> ConstructEdgeVertex(ModelPartition& Partition, LoopCorner Corner, Vector3 Position)
{
    if (!CornerValidCondition(Partition, Corner)) return { {}, "MEV: corner is not a position of the loop" };
    const VertexToken From   = CornerVertex(Partition, Corner);                              // [-] - Existing vertex
    const VertexToken Vertex = Partition.Vertices.Issue({ .Position = Position });           // [-] - New vertex
    const EdgeIssue   Issue  = IssueEdge(Partition, From, Vertex, Corner.Loop, Corner.Loop);
    LoopProfile&      Loop   = Partition.Loops.Resolve(Corner.Loop);
    if (Corner.Fin.PresentCondition())
    {
        // 📝 … → Predecessor(corner) → Outbound → Return → corner → …
        const FinToken Leading = Partition.Fins.Resolve(Corner.Fin).Predecessor;             // [-] - Fin arriving at From
        LinkFins(Partition, Leading, Issue.Forward);
        LinkFins(Partition, Issue.Forward, Issue.Reversed);
        LinkFins(Partition, Issue.Reversed, Corner.Fin);
    }
    else
    {
        LinkFins(Partition, Issue.Forward, Issue.Reversed);
        LinkFins(Partition, Issue.Reversed, Issue.Forward);
        Loop.FirstFin       = Issue.Forward;
        Loop.IsolatedVertex = {};
    }
    return { { Issue.Edge, Vertex, Issue.Forward, Issue.Reversed } };
}

EditOutcome<EdgeFaceOutcome> ConstructEdgeFace(ModelPartition& Partition, LoopCorner From, LoopCorner To)
{
    if (!CornerValidCondition(Partition, From) || !CornerValidCondition(Partition, To)) return { {}, "MEF: corner is not a position of the loop" };
    if (!(From.Loop == To.Loop)) return { {}, "MEF: corners lie in different loops (use MEKR)" };
    const LoopToken       Loop   = From.Loop;                                                // [-] - Loop being split
    const FaceToken       Face   = Partition.Loops.Resolve(Loop).Face;                       // [-] - Face being split
    const SolidShellToken Shell  = Partition.Faces.Resolve(Face).Shell;                      // [-] - Shell gaining the face
    const VertexToken     Start  = CornerVertex(Partition, From);                            // [-] - New edge start
    const VertexToken     End    = CornerVertex(Partition, To);                              // [-] - New edge end
    const FaceToken       NewFace = Partition.Faces.Issue({ .Shell = Shell });               // [-] - Face created by the split
    const LoopToken       NewLoop = Partition.Loops.Issue({ .Face = NewFace });              // [-] - Its loop
    Partition.Faces.Resolve(NewFace).Loops = { NewLoop };
    Partition.Shells.Resolve(Shell).Faces.push_back(NewFace);
    InheritSurface(Partition, Face, NewFace);
    const EdgeIssue Issue = IssueEdge(Partition, Start, End, Loop, NewLoop);

    if (!From.Fin.PresentCondition())
    {
        // ① Isolated-vertex loop: a closed edge; each side is one fin.
        LinkFins(Partition, Issue.Forward, Issue.Forward);
        LinkFins(Partition, Issue.Reversed, Issue.Reversed);
        Partition.Loops.Resolve(Loop).IsolatedVertex = {};
    }
    else if (From.Fin == To.Fin)
    {
        // ② Same corner: a closed edge at one vertex; the forward fin is a loop on its own, the old fins join the new face.
        const FinToken Leading = Partition.Fins.Resolve(From.Fin).Predecessor;               // [-] - Fin arriving at the vertex
        LinkFins(Partition, Issue.Forward, Issue.Forward);
        LinkFins(Partition, Leading, Issue.Reversed);
        LinkFins(Partition, Issue.Reversed, From.Fin);
        AssignCycleLoop(Partition, Issue.Reversed, NewLoop);
    }
    else
    {
        // ③ General split: old loop = Forward → To … ; new loop = Reversed → From … .
        const FinToken LeadingFrom = Partition.Fins.Resolve(From.Fin).Predecessor;           // [-] - Fin arriving at Start
        const FinToken LeadingTo   = Partition.Fins.Resolve(To.Fin).Predecessor;             // [-] - Fin arriving at End
        LinkFins(Partition, LeadingFrom, Issue.Forward);
        LinkFins(Partition, Issue.Forward, To.Fin);
        LinkFins(Partition, LeadingTo, Issue.Reversed);
        LinkFins(Partition, Issue.Reversed, From.Fin);
        AssignCycleLoop(Partition, Issue.Reversed, NewLoop);
    }
    Partition.Loops.Resolve(Loop).FirstFin    = Issue.Forward;
    Partition.Loops.Resolve(NewLoop).FirstFin = Issue.Reversed;
    return { { Issue.Edge, NewFace, NewLoop, Issue.Forward, Issue.Reversed } };
}

EditOutcome<EdgeRingOutcome> ConstructEdgeCollapseRing(ModelPartition& Partition, LoopCorner From, LoopCorner To)
{
    if (!CornerValidCondition(Partition, From) || !CornerValidCondition(Partition, To)) return { {}, "MEKR: corner is not a position of the loop" };
    if (From.Loop == To.Loop) return { {}, "MEKR: corners lie in one loop (use MEF)" };
    const FaceToken Face = Partition.Loops.Resolve(From.Loop).Face;                          // [-] - Face of both loops
    if (!(Partition.Loops.Resolve(To.Loop).Face == Face)) return { {}, "MEKR: loops belong to different faces" };
    const EdgeIssue Issue = IssueEdge(Partition, CornerVertex(Partition, From), CornerVertex(Partition, To), From.Loop, From.Loop);

    // 📝 Merged cycle: … → Predecessor(From) → Forward → To … Predecessor(To) → Reversed → From … ; an isolated loop
    //    contributes no fins, so its side collapses to the new fin pair.
    const FinToken LeadingFrom = From.Fin.PresentCondition() ? Partition.Fins.Resolve(From.Fin).Predecessor : Issue.Reversed;
    const FinToken LeadingTo   = To.Fin.PresentCondition()   ? Partition.Fins.Resolve(To.Fin).Predecessor   : Issue.Forward;
    const FinToken TrailingTo   = To.Fin.PresentCondition()   ? To.Fin   : Issue.Reversed;
    const FinToken TrailingFrom = From.Fin.PresentCondition() ? From.Fin : Issue.Forward;
    LinkFins(Partition, LeadingFrom, Issue.Forward);
    LinkFins(Partition, Issue.Forward, TrailingTo);
    LinkFins(Partition, LeadingTo, Issue.Reversed);
    LinkFins(Partition, Issue.Reversed, TrailingFrom);
    AssignCycleLoop(Partition, Issue.Forward, From.Loop);

    LoopProfile& Merged = Partition.Loops.Resolve(From.Loop);
    Merged.FirstFin       = Issue.Forward;
    Merged.IsolatedVertex = {};
    EraseToken(Partition.Faces.Resolve(Face).Loops, To.Loop);
    Partition.Loops.Reclaim(To.Loop);
    return { { Issue.Edge, Issue.Forward, Issue.Reversed } };
}

EditOutcome<FaceToken> ConstructFaceCollapseRing(ModelPartition& Partition, LoopToken Ring)
{
    if (!Partition.Loops.Authenticate(Ring)) return { {}, "MFKRH: stale loop token" };
    const FaceToken Face = Partition.Loops.Resolve(Ring).Face;                               // [-] - Face losing the ring
    if (Partition.Faces.Resolve(Face).Loops.size() < 2) return { {}, "MFKRH: the face has no ring to release" };
    const SolidShellToken Shell   = Partition.Faces.Resolve(Face).Shell;                     // [-] - Shell gaining the face
    const FaceToken       NewFace = Partition.Faces.Issue({ .Shell = Shell, .Loops = { Ring } });
    Partition.Shells.Resolve(Shell).Faces.push_back(NewFace);
    InheritSurface(Partition, Face, NewFace);
    EraseToken(Partition.Faces.Resolve(Face).Loops, Ring);
    Partition.Loops.Resolve(Ring).Face = NewFace;
    return { NewFace };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLLAPSING EDITS
//------------------------------------------------------------------------------------------------------------------------

EditOutcome<LoopToken> CollapseEdgeConstructRing(ModelPartition& Partition, EdgeToken Edge)
{
    FinToken First  = {};                                                                    // [-] - Forward-ring fin
    FinToken Second = {};                                                                    // [-] - Its mate
    if (!ResolveEdgeFins(Partition, Edge, First, Second)) return { {}, "KEMR: edge is not a manifold two-fin edge" };
    const LoopToken Loop = Partition.Fins.Resolve(First).Loop;                               // [-] - Loop being split
    if (!(Partition.Fins.Resolve(Second).Loop == Loop)) return { {}, "KEMR: the edge's fins lie in different loops (use KEF)" };
    const FaceToken Face = Partition.Loops.Resolve(Loop).Face;                               // [-] - Face gaining the ring

    // 📝 Loop = First · X · Second · Y. X stays in the loop, Y becomes the ring; an empty side leaves an isolated vertex.
    const FinToken    FirstX  = Partition.Fins.Resolve(First).Successor;                     // [-] - Head of X
    const FinToken    LastX   = Partition.Fins.Resolve(Second).Predecessor;                  // [-] - Tail of X
    const FinToken    FirstY  = Partition.Fins.Resolve(Second).Successor;                    // [-] - Head of Y
    const FinToken    LastY   = Partition.Fins.Resolve(First).Predecessor;                   // [-] - Tail of Y
    const bool        EmptyX  = FirstX == Second;                                            // [-] - X has no fins
    const bool        EmptyY  = FirstY == First;                                             // [-] - Y has no fins
    const VertexToken VertexX = Partition.FinEnd(First);                                     // [-] - Vertex X starts and ends at
    const VertexToken VertexY = Partition.FinStart(First);                                   // [-] - Vertex Y starts and ends at
    const LoopToken   Ring    = Partition.Loops.Issue({ .Face = Face });                     // [-] - New ring loop
    Partition.Faces.Resolve(Face).Loops.push_back(Ring);

    if (EmptyX)
    {
        Partition.Loops.Resolve(Loop).FirstFin       = {};
        Partition.Loops.Resolve(Loop).IsolatedVertex = VertexX;
    }
    else
    {
        LinkFins(Partition, LastX, FirstX);
        Partition.Loops.Resolve(Loop).FirstFin = FirstX;
    }
    if (EmptyY)
    {
        Partition.Loops.Resolve(Ring).IsolatedVertex = VertexY;
    }
    else
    {
        LinkFins(Partition, LastY, FirstY);
        AssignCycleLoop(Partition, FirstY, Ring);
        Partition.Loops.Resolve(Ring).FirstFin = FirstY;
    }
    ReclaimEdge(Partition, Edge, First, Second);
    return { Ring };
}

EditOutcome<bool> CollapseFaceConstructRing(ModelPartition& Partition, FaceToken Absorbing, FaceToken Collapsing)
{
    if (!Partition.Faces.Authenticate(Absorbing) || !Partition.Faces.Authenticate(Collapsing)) return { false, "KFMRH: stale face token" };
    if (Absorbing == Collapsing) return { false, "KFMRH: a face cannot absorb itself" };
    FaceProfile& Absorber = Partition.Faces.Resolve(Absorbing);
    FaceProfile& Absorbed = Partition.Faces.Resolve(Collapsing);
    if (!(Absorber.Shell == Absorbed.Shell)) return { false, "KFMRH: faces lie in different shells" };
    if (Absorbed.Loops.size() != 1) return { false, "KFMRH: the collapsing face must have exactly one loop" };
    const LoopToken Ring = Absorbed.Loops.front();                                           // [-] - Loop becoming a ring
    Partition.Loops.Resolve(Ring).Face = Absorbing;
    Absorber.Loops.push_back(Ring);
    Absorbed.Loops.clear();
    ReclaimFace(Partition, Collapsing);
    return { true };
}

EditOutcome<bool> CollapseEdgeVertex(ModelPartition& Partition, EdgeToken Edge, VertexToken Vertex)
{
    FinToken First  = {};                                                                    // [-] - Forward-ring fin
    FinToken Second = {};                                                                    // [-] - Its mate
    if (!ResolveEdgeFins(Partition, Edge, First, Second)) return { false, "KEV: edge is not a manifold two-fin edge" };
    const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
    if (!(Profile.Start == Vertex) && !(Profile.End == Vertex)) return { false, "KEV: vertex is not an end of the edge" };
    if (Profile.Start == Profile.End) return { false, "KEV: closed edge" };
    bool SharedCondition = false;                                                            // [-] - Vertex used by another edge
    Partition.Edges.Enumerate([&](EdgeToken Other)
    {
        const EdgeProfile& OtherProfile = Partition.Edges.Resolve(Other);
        if (!(Other == Edge) && (OtherProfile.Start == Vertex || OtherProfile.End == Vertex)) SharedCondition = true;
    });
    if (SharedCondition) return { false, "KEV: vertex has degree above one" };

    // 📝 The fin arriving at the vertex is immediately followed by the fin leaving it.
    const FinToken    Arriving  = Partition.FinEnd(First) == Vertex ? First : Second;        // [-] - Fin ending at Vertex
    const FinToken    Leaving   = Arriving == First ? Second : First;                        // [-] - Fin starting at Vertex
    const LoopToken   Loop      = Partition.Fins.Resolve(Arriving).Loop;                     // [-] - Loop containing both
    const VertexToken Remaining = Partition.FinStart(Arriving);                              // [-] - Surviving end
    if (!(Partition.Fins.Resolve(Arriving).Successor == Leaving)) return { false, "KEV: fins are not consecutive" };
    const FinToken Leading  = Partition.Fins.Resolve(Arriving).Predecessor;                  // [-] - Fin before the spur
    const FinToken Trailing = Partition.Fins.Resolve(Leaving).Successor;                     // [-] - Fin after the spur
    if (Trailing == Arriving)
    {
        Partition.Loops.Resolve(Loop).FirstFin       = {};
        Partition.Loops.Resolve(Loop).IsolatedVertex = Remaining;
    }
    else
    {
        LinkFins(Partition, Leading, Trailing);
        Partition.Loops.Resolve(Loop).FirstFin = Trailing;
    }
    ReclaimEdge(Partition, Edge, First, Second);
    Partition.Vertices.Reclaim(Vertex);
    return { true };
}

EditOutcome<bool> CollapseEdgeFace(ModelPartition& Partition, EdgeToken Edge)
{
    FinToken First  = {};                                                                    // [-] - Forward-ring fin
    FinToken Second = {};                                                                    // [-] - Its mate
    if (!ResolveEdgeFins(Partition, Edge, First, Second)) return { false, "KEF: edge is not a manifold two-fin edge" };
    const LoopToken KeptLoop    = Partition.Fins.Resolve(First).Loop;                        // [-] - Loop that survives
    const LoopToken MergedLoop  = Partition.Fins.Resolve(Second).Loop;                       // [-] - Loop that is absorbed
    if (KeptLoop == MergedLoop) return { false, "KEF: both fins lie in one loop (use KEMR)" };
    const FaceToken KeptFace    = Partition.Loops.Resolve(KeptLoop).Face;                    // [-] - Face that survives
    const FaceToken MergedFace  = Partition.Loops.Resolve(MergedLoop).Face;                  // [-] - Face that is absorbed
    if (KeptFace == MergedFace) return { false, "KEF: both fins bound one face (use KEMR)" };

    const FinToken LeadingFirst   = Partition.Fins.Resolve(First).Predecessor;               // [-] - Before First
    const FinToken TrailingFirst  = Partition.Fins.Resolve(First).Successor;                 // [-] - After First
    const FinToken LeadingSecond  = Partition.Fins.Resolve(Second).Predecessor;              // [-] - Before Second
    const FinToken TrailingSecond = Partition.Fins.Resolve(Second).Successor;                // [-] - After Second
    const bool     AloneFirst     = TrailingFirst == First;                                  // [-] - First is its loop's only fin
    const bool     AloneSecond    = TrailingSecond == Second;                                // [-] - Second is its loop's only fin
    LoopProfile&   Kept           = Partition.Loops.Resolve(KeptLoop);
    if (AloneFirst && AloneSecond)
    {
        Kept.FirstFin       = {};
        Kept.IsolatedVertex = Partition.FinStart(First);
    }
    else if (AloneFirst)
    {
        LinkFins(Partition, LeadingSecond, TrailingSecond);
        AssignCycleLoop(Partition, TrailingSecond, KeptLoop);
        Kept.FirstFin = TrailingSecond;
    }
    else if (AloneSecond)
    {
        LinkFins(Partition, LeadingFirst, TrailingFirst);
        Kept.FirstFin = TrailingFirst;
    }
    else
    {
        // 📝 Inverse of the MEF general split: … Leading(First) → Trailing(Second) … Leading(Second) → Trailing(First) …
        LinkFins(Partition, LeadingFirst, TrailingSecond);
        LinkFins(Partition, LeadingSecond, TrailingFirst);
        AssignCycleLoop(Partition, TrailingFirst, KeptLoop);
        Kept.FirstFin = TrailingFirst;
    }

    // ① Remaining rings of the absorbed face move to the kept face; ② the absorbed loop and face are reclaimed.
    FaceProfile& Absorbed = Partition.Faces.Resolve(MergedFace);
    for (LoopToken Ring : Absorbed.Loops)
    {
        if (Ring == MergedLoop) continue;
        Partition.Loops.Resolve(Ring).Face = KeptFace;
        Partition.Faces.Resolve(KeptFace).Loops.push_back(Ring);
    }
    Absorbed.Loops.clear();
    Partition.Loops.Reclaim(MergedLoop);
    ReclaimFace(Partition, MergedFace);
    ReclaimEdge(Partition, Edge, First, Second);
    return { true };
}

EditOutcome<bool> CollapseVertexJoinEdges(ModelPartition& Partition, VertexToken Vertex, EdgeToken Kept, EdgeToken Absorbed)
{
    FinToken KeptFirst = {}, KeptSecond = {};                                                // [-] - Kept's two fins
    FinToken AbsorbedFirst = {}, AbsorbedSecond = {};                                        // [-] - Absorbed's two fins
    if (Kept == Absorbed) return { false, "KVE: the two edges are one" };
    if (!ResolveEdgeFins(Partition, Kept, KeptFirst, KeptSecond) || !ResolveEdgeFins(Partition, Absorbed, AbsorbedFirst, AbsorbedSecond))
    {
        return { false, "KVE: edges are not manifold two-fin edges" };
    }
    EdgeProfile&       KeptEdge     = Partition.Edges.Resolve(Kept);
    const EdgeProfile& AbsorbedEdge = Partition.Edges.Resolve(Absorbed);
    if (!(KeptEdge.Start == Vertex) && !(KeptEdge.End == Vertex)) return { false, "KVE: vertex is not an end of the kept edge" };
    if (!(AbsorbedEdge.Start == Vertex) && !(AbsorbedEdge.End == Vertex)) return { false, "KVE: vertex is not an end of the absorbed edge" };
    if (KeptEdge.Start == KeptEdge.End || AbsorbedEdge.Start == AbsorbedEdge.End) return { false, "KVE: closed edge" };
    const VertexToken Far = AbsorbedEdge.Start == Vertex ? AbsorbedEdge.End : AbsorbedEdge.Start;  // [-] - New end of Kept

    // 📝 At a degree-two vertex each Kept fin is adjacent in its loop to one Absorbed fin; that fin is unlinked.
    for (const FinToken KeptFin : { KeptFirst, KeptSecond })
    {
        const FinProfile& Use      = Partition.Fins.Resolve(KeptFin);
        const bool        Arriving = Partition.FinEnd(KeptFin) == Vertex;                    // [-] - Kept fin ends at Vertex
        const FinToken    Adjacent = Arriving ? Use.Successor : Use.Predecessor;             // [-] - Fin across the vertex
        if (!(Partition.Fins.Resolve(Adjacent).Edge == Absorbed)) return { false, "KVE: vertex has degree above two" };
    }
    for (const FinToken KeptFin : { KeptFirst, KeptSecond })
    {
        const FinProfile& Use      = Partition.Fins.Resolve(KeptFin);
        const LoopToken   Loop     = Use.Loop;                                               // [-] - Loop of both fins
        const bool        Arriving = Partition.FinEnd(KeptFin) == Vertex;
        const FinToken    Adjacent = Arriving ? Use.Successor : Use.Predecessor;
        if (Arriving) LinkFins(Partition, KeptFin, Partition.Fins.Resolve(Adjacent).Successor);
        else          LinkFins(Partition, Partition.Fins.Resolve(Adjacent).Predecessor, KeptFin);
        if (Partition.Loops.Resolve(Loop).FirstFin == Adjacent) Partition.Loops.Resolve(Loop).FirstFin = KeptFin;
    }
    (KeptEdge.Start == Vertex ? KeptEdge.Start : KeptEdge.End) = Far;
    ReclaimEdge(Partition, Absorbed, AbsorbedFirst, AbsorbedSecond);
    Partition.Vertices.Reclaim(Vertex);
    return { true };
}

EditOutcome<bool> CollapseBodyFaceVertex(ModelPartition& Partition, BodyToken Body)
{
    if (!Partition.Bodies.Authenticate(Body)) return { false, "KVFS: stale body token" };
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    if (Inventory.Faces.size() != 1 || !Inventory.Edges.empty() || Inventory.Vertices.size() != 1) return { false, "KVFS: body is not an acorn" };
    const FaceToken Face = Inventory.Faces.front();                                          // [-] - The single face
    if (Partition.Faces.Resolve(Face).Surface.PresentCondition()) Partition.Surfaces.Reclaim(Partition.Faces.Resolve(Face).Surface);
    Partition.Loops.Reclaim(Inventory.Loops.front());
    Partition.Faces.Reclaim(Face);
    Partition.Vertices.Reclaim(Inventory.Vertices.front());
    for (SolidShellToken Shell : Inventory.Shells) Partition.Shells.Reclaim(Shell);
    for (RegionToken Region : Inventory.Regions) Partition.Regions.Reclaim(Region);
    Partition.Bodies.Reclaim(Body);
    return { true };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHELL EDITS
//------------------------------------------------------------------------------------------------------------------------

void ReverseShellOrientation(ModelPartition& Partition, SolidShellToken Shell)
{
    for (FaceToken Face : Partition.Shells.Resolve(Shell).Faces)
    {
        FaceProfile& Profile = Partition.Faces.Resolve(Face);
        Profile.Sense = InvertOrientation(Profile.Sense);
        for (LoopToken Loop : Profile.Loops)
        {
            std::vector<FinToken> Cycle;
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin) { Cycle.push_back(Fin); });
            for (FinToken Fin : Cycle)
            {
                FinProfile& Use = Partition.Fins.Resolve(Fin);
                std::swap(Use.Successor, Use.Predecessor);
                Use.Sense = InvertOrientation(Use.Sense);
            }
        }
    }
}

EditOutcome<bool> TransferShell(ModelPartition& Partition, SolidShellToken Shell, RegionToken Destination)
{
    if (!Partition.Shells.Authenticate(Shell) || !Partition.Regions.Authenticate(Destination)) return { false, "TransferShell: stale token" };
    const RegionToken Source     = Partition.Shells.Resolve(Shell).Region;                   // [-] - Region losing the shell
    const BodyToken   SourceBody = Partition.Regions.Resolve(Source).Body;                   // [-] - Body losing the shell
    EraseToken(Partition.Regions.Resolve(Source).Shells, Shell);
    Partition.Regions.Resolve(Destination).Shells.push_back(Shell);
    Partition.Shells.Resolve(Shell).Region = Destination;

    // 📝 A body with no shell left in any region is empty — reclaim it so no unreachable records remain.
    bool EmptyCondition = true;                                                              // [-] - No shells remain
    for (RegionToken Region : Partition.Bodies.Resolve(SourceBody).Regions)
    {
        if (!Partition.Regions.Resolve(Region).Shells.empty()) EmptyCondition = false;
    }
    if (EmptyCondition && !(SourceBody == Partition.Regions.Resolve(Destination).Body))
    {
        for (RegionToken Region : Partition.Bodies.Resolve(SourceBody).Regions) Partition.Regions.Reclaim(Region);
        Partition.Bodies.Reclaim(SourceBody);
    }
    return { true };
}

EditOutcome<BodyToken> DetachShell(ModelPartition& Partition, SolidShellToken Shell)
{
    if (!Partition.Shells.Authenticate(Shell)) return { {}, "DetachShell: stale shell token" };
    const BodyToken   Body     = Partition.Bodies.Issue({ .Category = BodyCategory::Solid });
    const RegionToken Exterior = Partition.Regions.Issue({ .Body = Body, .Material = RegionMaterial::Void });
    const RegionToken Interior = Partition.Regions.Issue({ .Body = Body, .Material = RegionMaterial::Solid });
    Partition.Bodies.Resolve(Body).Regions = { Exterior, Interior };
    const EditOutcome<bool> Transfer = TransferShell(Partition, Shell, Interior);
    if (!Transfer.SucceededCondition()) return { {}, Transfer.Refusal };
    return { Body };
}

} // namespace Frontier::GeometrySpace::Parametric
