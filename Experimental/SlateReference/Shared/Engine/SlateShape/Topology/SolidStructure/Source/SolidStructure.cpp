//============================================================================================================================================
//                                                        SOLIDSTRUCTURE.CPP
//============================================================================================================================================

#include "SlateShape/Topology/SolidStructure/Api/SolidStructure.h"

namespace Slate
{

namespace
{
    bool SameVertex(VertexName LeftVertex, VertexName RightVertex)
    {
        return LeftVertex.IssuedIndex == RightVertex.IssuedIndex;
    }
}

VertexName SolidStructure::DeclareVertex(const SpatialPoint& Position)
{
    HeldVertices.push_back({ Position });
    return { static_cast<std::uint32_t>(HeldVertices.size()) };
}

CurveNameInSolid SolidStructure::DeclareCurve(const CurveSpecification& Declared)
{
    HeldCurves.push_back({ Declared });
    return { static_cast<std::uint32_t>(HeldCurves.size()) };
}

SurfaceNameInSolid SolidStructure::DeclareSurface(const SurfaceSpecification& Declared)
{
    HeldSurfaces.push_back({ Declared });
    return { static_cast<std::uint32_t>(HeldSurfaces.size()) };
}

Deliver<EdgeName> SolidStructure::DeclareEdge(VertexName StartVertex,
                                              VertexName EndVertex,
                                              CurveNameInSolid SupportingCurve)
{
    if (!StartVertex.Assigned() || !EndVertex.Assigned() || !SupportingCurve.Assigned())
    {
        return Deliver<EdgeName>::Refuse(
            { RefusalReason::ContentUnsupported, "edge requires two vertices and one supporting curve" });
    }

    if (StartVertex.IssuedIndex > HeldVertices.size() || EndVertex.IssuedIndex > HeldVertices.size())
        return Deliver<EdgeName>::Refuse({ RefusalReason::ContentUnsupported, "edge names an absent vertex" });
    if (SupportingCurve.IssuedIndex > HeldCurves.size())
        return Deliver<EdgeName>::Refuse({ RefusalReason::ContentUnsupported, "edge names an absent curve" });
    if (SameVertex(StartVertex, EndVertex))
        return Deliver<EdgeName>::Refuse({ RefusalReason::ContentUnsupported, "edge endpoints must differ" });

    HeldEdges.push_back({ StartVertex, EndVertex, SupportingCurve });
    return Deliver<EdgeName>::Result({ static_cast<std::uint32_t>(HeldEdges.size()) });
}

Deliver<CoedgeName> SolidStructure::DeclareCoedge(EdgeName TraversedEdge,
                                                  EdgeOrientation Orientation)
{
    if (!TraversedEdge.Assigned() || TraversedEdge.IssuedIndex > HeldEdges.size())
        return Deliver<CoedgeName>::Refuse({ RefusalReason::ContentUnsupported, "coedge names an absent edge" });

    HeldCoedges.push_back({ TraversedEdge, Orientation });
    return Deliver<CoedgeName>::Result({ static_cast<std::uint32_t>(HeldCoedges.size()) });
}

Deliver<LoopName> SolidStructure::DeclareLoop(const DeclaredLoop& Incoming)
{
    if (Incoming.Traversal.empty())
        return Deliver<LoopName>::Refuse({ RefusalReason::ContentUnsupported, "loop requires at least one coedge" });

    for (CoedgeName Traversed : Incoming.Traversal)
        if (!Traversed.Assigned() || Traversed.IssuedIndex > HeldCoedges.size())
            return Deliver<LoopName>::Refuse({ RefusalReason::ContentUnsupported, "loop names an absent coedge" });

    std::vector<VertexName> TraversalVertices;
    VertexName PreviousEnd = {};
    VertexName FirstStart = {};
    for (std::size_t Index = 0; Index < Incoming.Traversal.size(); ++Index)
    {
        VertexName StartVertex = {};
        VertexName EndVertex = {};
        if (!ResolveCoedgeVertices(Incoming.Traversal[Index], StartVertex, EndVertex))
            return Deliver<LoopName>::Refuse({ RefusalReason::ContentUnsupported, "loop names an unresolved coedge" });
        if (Index == 0u)
            FirstStart = StartVertex;
        else if (!SameVertex(PreviousEnd, StartVertex))
            return Deliver<LoopName>::Refuse({ RefusalReason::ContentUnsupported, "loop traversal does not connect" });
        PreviousEnd = EndVertex;
    }
    if (!SameVertex(PreviousEnd, FirstStart))
        return Deliver<LoopName>::Refuse({ RefusalReason::ContentUnsupported, "loop traversal does not close" });

    HeldLoops.push_back(Incoming);
    return Deliver<LoopName>::Result({ static_cast<std::uint32_t>(HeldLoops.size()) });
}

Deliver<FaceName> SolidStructure::DeclareFace(const DeclaredFace& Incoming)
{
    if (!Incoming.SupportingSurface.Assigned())
        return Deliver<FaceName>::Refuse({ RefusalReason::ContentUnsupported, "face requires one supporting surface" });
    if (Incoming.SupportingSurface.IssuedIndex > HeldSurfaces.size())
        return Deliver<FaceName>::Refuse({ RefusalReason::ContentUnsupported, "face names an absent surface" });
    if (Incoming.LoopSet.empty())
        return Deliver<FaceName>::Refuse({ RefusalReason::ContentUnsupported, "face requires at least one loop" });

    bool OuterDeclared = false;
    for (const DeclaredFaceLoop& HeldFaceLoop : Incoming.LoopSet)
    {
        const DeclaredLoop* HeldLoop = ResolveLoop(HeldFaceLoop.TraversedLoop);
        if (HeldLoop == nullptr)
            return Deliver<FaceName>::Refuse({ RefusalReason::ContentUnsupported, "face names an absent loop" });
        if (HeldLoop->Standing == LoopStanding::Outer)
        {
            if (OuterDeclared)
                return Deliver<FaceName>::Refuse({ RefusalReason::ContentUnsupported, "face declares more than one outer loop" });
            OuterDeclared = true;
        }
        if (HeldFaceLoop.TrimSet.size() != HeldLoop->Traversal.size())
            return Deliver<FaceName>::Refuse({ RefusalReason::ContentUnsupported, "face trim count does not match loop traversal" });
        for (const DeclaredTrimUse& Trim : HeldFaceLoop.TrimSet)
            if (!Trim.TraversedCurve.Assigned() || Trim.TraversedCurve.IssuedIndex > HeldCurves.size())
                return Deliver<FaceName>::Refuse({ RefusalReason::ContentUnsupported, "face names an absent trim curve" });
    }

    if (!OuterDeclared)
        return Deliver<FaceName>::Refuse({ RefusalReason::ContentUnsupported, "face requires one outer loop" });

    HeldFaces.push_back(Incoming);
    return Deliver<FaceName>::Result({ static_cast<std::uint32_t>(HeldFaces.size()) });
}

bool SolidStructure::ResolveCoedgeVertices(CoedgeName Subject,
                                           VertexName& StartVertex,
                                           VertexName& EndVertex) const
{
    StartVertex = {};
    EndVertex = {};

    const DeclaredCoedge* HeldCoedge = ResolveCoedge(Subject);
    if (HeldCoedge == nullptr)
        return false;

    const DeclaredEdge* HeldEdge = ResolveEdge(HeldCoedge->TraversedEdge);
    if (HeldEdge == nullptr)
        return false;

    if (HeldCoedge->Orientation == EdgeOrientation::Forward)
    {
        StartVertex = HeldEdge->StartVertex;
        EndVertex = HeldEdge->EndVertex;
    }
    else
    {
        StartVertex = HeldEdge->EndVertex;
        EndVertex = HeldEdge->StartVertex;
    }

    return true;
}

bool SolidStructure::ResolveLoopVertices(LoopName Subject,
                                         std::vector<VertexName>& Traversal) const
{
    Traversal.clear();

    const DeclaredLoop* HeldLoop = ResolveLoop(Subject);
    if (HeldLoop == nullptr)
        return false;

    VertexName PreviousEnd = {};
    VertexName FirstStart = {};
    for (std::size_t Index = 0; Index < HeldLoop->Traversal.size(); ++Index)
    {
        VertexName StartVertex = {};
        VertexName EndVertex = {};
        if (!ResolveCoedgeVertices(HeldLoop->Traversal[Index], StartVertex, EndVertex))
            return false;
        if (Index == 0u)
            FirstStart = StartVertex;
        else if (!SameVertex(PreviousEnd, StartVertex))
            return false;

        Traversal.push_back(StartVertex);
        PreviousEnd = EndVertex;
    }

    return SameVertex(PreviousEnd, FirstStart);
}

bool SolidStructure::ResolveLoopEdges(LoopName Subject,
                                      std::vector<EdgeName>& Traversal) const
{
    Traversal.clear();

    const DeclaredLoop* HeldLoop = ResolveLoop(Subject);
    if (HeldLoop == nullptr)
        return false;

    Traversal.reserve(HeldLoop->Traversal.size());
    for (CoedgeName TraversedCoedge : HeldLoop->Traversal)
    {
        const DeclaredCoedge* HeldCoedge = ResolveCoedge(TraversedCoedge);
        if (HeldCoedge == nullptr)
            return false;
        Traversal.push_back(HeldCoedge->TraversedEdge);
    }

    return true;
}

bool SolidStructure::Declared() const
{
    if (HeldFaces.empty())
        return false;

    for (const DeclaredCurve& HeldCurve : HeldCurves)
        if (!HeldCurve.Geometry.Declared())
            return false;

    for (const DeclaredSurface& HeldSurface : HeldSurfaces)
        if (!HeldSurface.Geometry.Declared())
            return false;

    for (const DeclaredEdge& HeldEdge : HeldEdges)
    {
        if (!HeldEdge.StartVertex.Assigned() || !HeldEdge.EndVertex.Assigned() || !HeldEdge.SupportingCurve.Assigned())
            return false;
        if (HeldEdge.StartVertex.IssuedIndex > HeldVertices.size() || HeldEdge.EndVertex.IssuedIndex > HeldVertices.size())
            return false;
        if (HeldEdge.SupportingCurve.IssuedIndex > HeldCurves.size())
            return false;
        if (SameVertex(HeldEdge.StartVertex, HeldEdge.EndVertex))
            return false;
    }

    for (std::uint32_t CoedgeIndex = 1u; CoedgeIndex <= HeldCoedges.size(); ++CoedgeIndex)
    {
        VertexName StartVertex = {};
        VertexName EndVertex = {};
        if (!ResolveCoedgeVertices({ CoedgeIndex }, StartVertex, EndVertex))
            return false;
        if (SameVertex(StartVertex, EndVertex))
            return false;
    }

    for (std::uint32_t LoopIndex = 1u; LoopIndex <= HeldLoops.size(); ++LoopIndex)
    {
        std::vector<VertexName> Traversal;
        if (!ResolveLoopVertices({ LoopIndex }, Traversal))
            return false;
        if (Traversal.empty())
            return false;
    }

    for (const DeclaredFace& HeldFace : HeldFaces)
    {
        if (!HeldFace.SupportingSurface.Assigned() || HeldFace.LoopSet.empty())
            return false;
        if (HeldFace.SupportingSurface.IssuedIndex > HeldSurfaces.size())
            return false;

        bool OuterDeclared = false;
        for (const DeclaredFaceLoop& HeldFaceLoop : HeldFace.LoopSet)
        {
            const DeclaredLoop* HeldLoop = ResolveLoop(HeldFaceLoop.TraversedLoop);
            if (HeldLoop == nullptr)
                return false;
            if (HeldLoop->Standing == LoopStanding::Outer)
            {
                if (OuterDeclared)
                    return false;
                OuterDeclared = true;
            }
            if (HeldFaceLoop.TrimSet.size() != HeldLoop->Traversal.size())
                return false;
            for (const DeclaredTrimUse& Trim : HeldFaceLoop.TrimSet)
                if (!Trim.TraversedCurve.Assigned() || Trim.TraversedCurve.IssuedIndex > HeldCurves.size())
                    return false;
        }

        if (!OuterDeclared)
            return false;
    }

    return true;
}

const DeclaredEdge* SolidStructure::ResolveEdge(EdgeName Subject) const
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldEdges.size())
        return nullptr;
    return &HeldEdges[Subject.IssuedIndex - 1u];
}

const DeclaredCoedge* SolidStructure::ResolveCoedge(CoedgeName Subject) const
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldCoedges.size())
        return nullptr;
    return &HeldCoedges[Subject.IssuedIndex - 1u];
}

const DeclaredLoop* SolidStructure::ResolveLoop(LoopName Subject) const
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldLoops.size())
        return nullptr;
    return &HeldLoops[Subject.IssuedIndex - 1u];
}

void SolidStructure::Reclaim()
{
    HeldVertices.clear();
    HeldCurves.clear();
    HeldSurfaces.clear();
    HeldEdges.clear();
    HeldCoedges.clear();
    HeldLoops.clear();
    HeldFaces.clear();
}

} // namespace Slate
