/*====================================================================================================================================
                                                        MODELPARTITION.H
====================================================================================================================================*/
// 🧩 B-rep topology (Body → Region → SolidShell → Face → Loop → Fin → Edge → Vertex) with attached exact geometry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Precision/PrecisionSpecification.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/SlotRegister.h"

#include <functional>
#include <optional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOKENS
//------------------------------------------------------------------------------------------------------------------------

struct BodyTag;   struct RegionTag;  struct SolidHullTag; struct FaceTag;
struct LoopTag;   struct FinTag;     struct EdgeTag;       struct VertexTag;
struct SurfaceTag; struct CurveTag;

using BodyToken       = RecordToken<BodyTag>;
using RegionToken     = RecordToken<RegionTag>;
using SolidShellToken = RecordToken<SolidHullTag>;
using FaceToken       = RecordToken<FaceTag>;
using LoopToken       = RecordToken<LoopTag>;
using FinToken        = RecordToken<FinTag>;
using EdgeToken       = RecordToken<EdgeTag>;
using VertexToken     = RecordToken<VertexTag>;
using SurfaceToken    = RecordToken<SurfaceTag>;
using CurveToken      = RecordToken<CurveTag>;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Parasolid body categories. Acorn = isolated vertices only; General = non-manifold or mixed dimension.
enum class BodyCategory : uint8_t
{
    Acorn = 0,
    Wire,
    Sheet,
    Solid,
    General,
};

enum class RegionMaterial : uint8_t
{
    Void = 0,
    Solid,
};

// 📝 Sense of a use relative to its geometry: a face whose outward normal equals the surface normal, or a fin that runs
//    along its edge's increasing τ, is Forward.
enum class Orientation : uint8_t
{
    Forward = 0,
    Reversed,
};

[[nodiscard]] constexpr Orientation InvertOrientation(Orientation Sense) noexcept
{
    return Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOPOLOGY PROFILES
//------------------------------------------------------------------------------------------------------------------------

// 💡 Region and SolidShell are the bones Sketcher lacked: a cavity is a second shell bounding the same solid region,
//    a multi-lump body has several solid regions, and Regions[0] is always the infinite exterior void.
struct BodyProfile
{
    BodyCategory             Category = BodyCategory::Acorn;                                 // [-] - Parasolid body category
    std::vector<RegionToken> Regions  = {};                                                  // [-] - [0] is the exterior void
};

struct RegionProfile
{
    BodyToken                    Body     = {};                                              // [-] - Enclosing body
    RegionMaterial               Material = RegionMaterial::Void;                            // [-] - Void or solid
    std::vector<SolidShellToken> Shells   = {};                                              // [-] - Boundary components
};

struct SolidHullProfile
{
    RegionToken            Region = {};                                                      // [-] - Region this shell bounds
    std::vector<FaceToken> Faces  = {};                                                      // [-] - Faces of the shell
};

struct FaceProfile
{
    SolidShellToken        Shell   = {};                                                     // [-] - Enclosing shell
    SurfaceToken           Surface = {};                                                     // [-] - Carrier surface (absent until attached)
    Orientation            Sense   = Orientation::Forward;                                   // [-] - Outward normal vs surface normal
    std::vector<LoopToken> Loops   = {};                                                     // [-] - Boundary loops (no outer/inner order)
};

// 📝 A loop is a cycle of fins, or — immediately after MVFS / KEMR — a single isolated vertex with no fins.
struct LoopProfile
{
    FaceToken   Face           = {};                                                         // [-] - Enclosing face
    FinToken    FirstFin       = {};                                                         // [-] - Any fin of the cycle
    VertexToken IsolatedVertex = {};                                                         // [-] - Present only for a fin-less loop
};

// 💡 A fin is the oriented use of an edge by a loop (Parasolid). Fins of one edge form a radial ring, so a non-manifold
//    edge is one ring of more than two fins rather than a special case. Each fin owns its SP-curve on the face surface.
struct FinProfile
{
    LoopToken                     Loop            = {};                                      // [-] - Enclosing loop
    EdgeToken                     Edge            = {};                                      // [-] - Edge used
    Orientation                   Sense           = Orientation::Forward;                    // [-] - Direction along the edge
    FinToken                      Successor       = {};                                      // [-] - Next fin in the loop
    FinToken                      Predecessor     = {};                                      // [-] - Previous fin in the loop
    FinToken                      RadialSuccessor = {};                                      // [-] - Next fin around the edge
    std::optional<ParameterCurve> SurfaceCurve    = {};                                      // [-] - SP-curve, same τ as the edge
};

struct EdgeProfile
{
    CurveToken  Curve        = {};                                                           // [-]   - Carrier curve (absent until attached)
    VertexToken Start        = {};                                                           // [-]   - Vertex at τ = IntervalLow
    VertexToken End          = {};                                                           // [-]   - Vertex at τ = IntervalHigh
    double      IntervalLow  = 0.0;                                                          // [-]   - Curve parameter at Start
    double      IntervalHigh = 0.0;                                                          // [-]   - Curve parameter at End
    double      Precision    = ExactPrecision;                                               // [len] - Local tube radius (NaN = exact)
    FinToken    RadialFin    = {};                                                           // [-]   - Entry into the radial ring
};

struct VertexProfile
{
    Vector3 Position  = {};                                                                  // [len] - Point
    double  Precision = ExactPrecision;                                                      // [len] - Local sphere radius (NaN = exact)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRAVERSAL RESULTS
//------------------------------------------------------------------------------------------------------------------------

// Deduplicated topology reachable from one body, in deterministic traversal order.
struct BodyInventory
{
    std::vector<RegionToken>     Regions  = {};                                              // [-] - All regions
    std::vector<SolidShellToken> Shells   = {};                                              // [-] - All shells
    std::vector<FaceToken>       Faces    = {};                                              // [-] - All faces
    std::vector<LoopToken>       Loops    = {};                                              // [-] - All loops
    std::vector<FinToken>        Fins     = {};                                              // [-] - All fins
    std::vector<EdgeToken>       Edges    = {};                                              // [-] - All edges
    std::vector<VertexToken>     Vertices = {};                                              // [-] - All vertices
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTITION
//------------------------------------------------------------------------------------------------------------------------

// 💡 A partition owns every topological and geometric record of the bodies modelled together (Parasolid term).
//    Mutation goes through EulerEdits; geometry is attached through the Attach… functions; reads are free.
class ModelPartition
{
public:
    SlotRegister<BodyTag, BodyProfile>             Bodies;
    SlotRegister<RegionTag, RegionProfile>         Regions;
    SlotRegister<SolidHullTag, SolidHullProfile> Shells;
    SlotRegister<FaceTag, FaceProfile>             Faces;
    SlotRegister<LoopTag, LoopProfile>             Loops;
    SlotRegister<FinTag, FinProfile>               Fins;
    SlotRegister<EdgeTag, EdgeProfile>             Edges;
    SlotRegister<VertexTag, VertexProfile>         Vertices;
    SlotRegister<SurfaceTag, SurfaceGeometry>      Surfaces;
    SlotRegister<CurveTag, CurveGeometry>          Curves;

    // ---- fin geometry of use
    [[nodiscard]] VertexToken FinStart(FinToken Fin) const noexcept;
    [[nodiscard]] VertexToken FinEnd(FinToken Fin) const noexcept;
    [[nodiscard]] double      FinParameterStart(FinToken Fin) const noexcept;
    [[nodiscard]] double      FinParameterEnd(FinToken Fin) const noexcept;
    [[nodiscard]] FinToken    RadialMate(FinToken Fin) const noexcept;

    // ---- traversal
    void                       EnumerateLoopFins(LoopToken Loop, const std::function<void(FinToken)>& Visitor) const;
    [[nodiscard]] FinToken     ResolveCorner(LoopToken Loop, VertexToken Vertex) const noexcept;
    [[nodiscard]] BodyInventory CollectInventory(BodyToken Body) const;
    [[nodiscard]] BodyToken    EnclosingBody(FaceToken Face) const noexcept;

    // ---- geometry attachment
    CurveToken   AttachCurve(EdgeToken Edge, CurveGeometry Curve, double IntervalLow, double IntervalHigh);
    SurfaceToken AttachSurface(FaceToken Face, SurfaceGeometry Surface, Orientation Sense);
    void         AttachSurfaceCurve(FinToken Fin, ParameterCurve Curve);

    // ---- tolerant modelling
    void AssignVertexPrecision(VertexToken Vertex, double Precision);
    void AssignEdgePrecision(EdgeToken Edge, double Precision);
};

} // namespace Frontier::GeometrySpace::Parametric
