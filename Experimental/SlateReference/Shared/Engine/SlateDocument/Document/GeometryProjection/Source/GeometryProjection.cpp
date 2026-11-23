//============================================================================================================================================
//                                                     GEOMETRYPROJECTION.CPP
//============================================================================================================================================

#include "SlateDocument/Document/GeometryProjection/Api/GeometryProjection.h"

namespace Slate
{

namespace
{
    bool FaceProjectable(const SolidStructure& Exact,
                         const DeclaredFace& Face,
                         std::vector<std::uint32_t>& CornerVertices)
    {
        CornerVertices.clear();

        if (Face.LoopSet.size() != 1u)
            return false;

        const DeclaredFaceLoop& HeldFaceLoop = Face.LoopSet.front();
        const DeclaredLoop* HeldLoop = Exact.Resolve().Loops != nullptr && HeldFaceLoop.TraversedLoop.Assigned()
                                     && HeldFaceLoop.TraversedLoop.IssuedIndex <= Exact.Resolve().Loops->size()
                                     ? &(*Exact.Resolve().Loops)[HeldFaceLoop.TraversedLoop.IssuedIndex - 1u]
                                     : nullptr;
        if (HeldLoop == nullptr || HeldLoop->Standing != LoopStanding::Outer)
            return false;

        std::vector<VertexName> Traversal;
        if (!Exact.ResolveLoopVertices(HeldFaceLoop.TraversedLoop, Traversal))
            return false;

        for (VertexName Vertex : Traversal)
        {
            if (!Vertex.Assigned() || Vertex.IssuedIndex == 0u)
                return false;
            CornerVertices.push_back(Vertex.IssuedIndex - 1u);
        }

        return CornerVertices.size() >= 3u;
    }
}

Deliver<bool> ProjectSolid(const SolidStructure& Exact,
                           const TessellationSpecification& Requested,
                           TopologyStructure& Projected)
{
    if (!Requested.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the tessellation request is not declared" });
    if (!Exact.Declared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the exact solid is not declared" });

    const SolidView Resolved = Exact.Resolve();
    if (Resolved.Vertices == nullptr || Resolved.Faces == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the exact solid view is unresolved" });

    std::vector<DocumentPosition> Positions;
    Positions.reserve(Resolved.Vertices->size());
    for (const DeclaredVertex& Vertex : *Resolved.Vertices)
        Positions.push_back({ Vertex.Position.Left, Vertex.Position.Up, Vertex.Position.Forward });

    const Deliver<bool> PositionsDeclared = Projected.DeclarePositions(Positions);
    if (!PositionsDeclared)
        return Deliver<bool>::Refuse(PositionsDeclared.Error);

    std::vector<std::uint32_t> CornerVertices;
    for (const DeclaredFace& Face : *Resolved.Faces)
    {
        if (!FaceProjectable(Exact, Face, CornerVertices))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the solid carries a face the polygon projection cannot express" });
        }

        const Deliver<bool> Declared = Projected.DeclareFace(CornerVertices);
        if (!Declared)
            return Deliver<bool>::Refuse(Declared.Error);
    }

    const Deliver<bool> Sealed = Projected.Seal();
    if (!Sealed)
        return Deliver<bool>::Refuse(Sealed.Error);

    return Deliver<bool>::Result(true);
}

} // namespace Slate
