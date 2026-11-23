//============================================================================================================================================
//                                                          SOLIDSTRUCTURE.H
//============================================================================================================================================
// 🧩 Initial exact solid incidence declarations — vertices, curves, surfaces, edges, coedges, loops and faces.
//    This is a new CAD topology seam, not a relocation of the current polygon document topology.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Geometry/SurfaceSpecification/Api/SurfaceSpecification.h"

#include <cstdint>
#include <vector>

namespace Slate
{

struct VertexName  { std::uint32_t IssuedIndex = 0u; bool Assigned() const { return IssuedIndex != 0u; } };
struct CurveNameInSolid { std::uint32_t IssuedIndex = 0u; bool Assigned() const { return IssuedIndex != 0u; } };
struct SurfaceNameInSolid { std::uint32_t IssuedIndex = 0u; bool Assigned() const { return IssuedIndex != 0u; } };
struct EdgeName    { std::uint32_t IssuedIndex = 0u; bool Assigned() const { return IssuedIndex != 0u; } };
struct CoedgeName  { std::uint32_t IssuedIndex = 0u; bool Assigned() const { return IssuedIndex != 0u; } };
struct LoopName    { std::uint32_t IssuedIndex = 0u; bool Assigned() const { return IssuedIndex != 0u; } };
struct FaceName    { std::uint32_t IssuedIndex = 0u; bool Assigned() const { return IssuedIndex != 0u; } };
struct SolidName   { std::uint32_t IssuedIndex = 0u; bool Assigned() const { return IssuedIndex != 0u; } };

enum class EdgeOrientation : std::uint32_t
{
    Forward = 0u,
    Reversed = 1u
};

enum class LoopStanding : std::uint32_t
{
    Outer = 0u,
    Inner = 1u
};

struct DeclaredVertex
{
    SpatialPoint Position = {};
};

struct DeclaredCurve
{
    CurveSpecification Geometry = {};
};

struct DeclaredSurface
{
    SurfaceSpecification Geometry = {};
};

struct DeclaredEdge
{
    VertexName StartVertex = {};
    VertexName EndVertex = {};
    CurveNameInSolid SupportingCurve = {};
};

struct DeclaredCoedge
{
    EdgeName TraversedEdge = {};
    EdgeOrientation Orientation = EdgeOrientation::Forward;
};

struct DeclaredLoop
{
    LoopStanding Standing = LoopStanding::Outer;
    std::vector<CoedgeName> Traversal = {};
};

struct DeclaredTrimUse
{
    CurveNameInSolid TraversedCurve = {};
    bool SameSense = true;
};

struct DeclaredFaceLoop
{
    LoopName TraversedLoop = {};
    std::vector<DeclaredTrimUse> TrimSet = {};
};

struct DeclaredFace
{
    SurfaceNameInSolid SupportingSurface = {};
    bool SameSense = true;
    std::vector<DeclaredFaceLoop> LoopSet = {};
};

struct SolidView
{
    const std::vector<DeclaredVertex>* Vertices = nullptr;
    const std::vector<DeclaredCurve>* Curves = nullptr;
    const std::vector<DeclaredSurface>* Surfaces = nullptr;
    const std::vector<DeclaredEdge>* Edges = nullptr;
    const std::vector<DeclaredCoedge>* Coedges = nullptr;
    const std::vector<DeclaredLoop>* Loops = nullptr;
    const std::vector<DeclaredFace>* Faces = nullptr;
};

class SolidStructure
{
public:
    VertexName DeclareVertex(const SpatialPoint& Position);
    CurveNameInSolid DeclareCurve(const CurveSpecification& Declared);
    SurfaceNameInSolid DeclareSurface(const SurfaceSpecification& Declared);
    Deliver<EdgeName> DeclareEdge(VertexName StartVertex,
                                  VertexName EndVertex,
                                  CurveNameInSolid SupportingCurve);
    Deliver<CoedgeName> DeclareCoedge(EdgeName TraversedEdge,
                                      EdgeOrientation Orientation);
    Deliver<LoopName> DeclareLoop(const DeclaredLoop& Incoming);
    Deliver<FaceName> DeclareFace(const DeclaredFace& Incoming);

    SolidView Resolve() const
    {
        return { &HeldVertices, &HeldCurves, &HeldSurfaces, &HeldEdges, &HeldCoedges, &HeldLoops, &HeldFaces };
    }

    bool ResolveCoedgeVertices(CoedgeName Subject,
                               VertexName& StartVertex,
                               VertexName& EndVertex) const;
    bool ResolveLoopVertices(LoopName Subject,
                             std::vector<VertexName>& Traversal) const;
    bool ResolveLoopEdges(LoopName Subject,
                          std::vector<EdgeName>& Traversal) const;

    bool Declared() const;
    std::uint32_t VertexCount() const { return static_cast<std::uint32_t>(HeldVertices.size()); }
    std::uint32_t CurveCount() const { return static_cast<std::uint32_t>(HeldCurves.size()); }
    std::uint32_t SurfaceCount() const { return static_cast<std::uint32_t>(HeldSurfaces.size()); }
    std::uint32_t EdgeCount() const { return static_cast<std::uint32_t>(HeldEdges.size()); }
    std::uint32_t CoedgeCount() const { return static_cast<std::uint32_t>(HeldCoedges.size()); }
    std::uint32_t LoopCount() const { return static_cast<std::uint32_t>(HeldLoops.size()); }
    std::uint32_t FaceCount() const { return static_cast<std::uint32_t>(HeldFaces.size()); }
    void Reclaim();

private:
    const DeclaredEdge* ResolveEdge(EdgeName Subject) const;
    const DeclaredCoedge* ResolveCoedge(CoedgeName Subject) const;
    const DeclaredLoop* ResolveLoop(LoopName Subject) const;

    std::vector<DeclaredVertex> HeldVertices = {};
    std::vector<DeclaredCurve> HeldCurves = {};
    std::vector<DeclaredSurface> HeldSurfaces = {};
    std::vector<DeclaredEdge> HeldEdges = {};
    std::vector<DeclaredCoedge> HeldCoedges = {};
    std::vector<DeclaredLoop> HeldLoops = {};
    std::vector<DeclaredFace> HeldFaces = {};
};

} // namespace Slate
