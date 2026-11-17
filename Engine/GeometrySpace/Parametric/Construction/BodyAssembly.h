/*====================================================================================================================================
                                                        BODYASSEMBLY.H
====================================================================================================================================*/
// 🧩 Direct assembly of a solid body from a blueprint of vertices, edges with carriers and faces with SP-curved loops

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 💡 A blueprint is the topology of a new body written as index lists, the form both the Boolean selection and the
//    local operations (blends) produce: they decide WHICH faces, edges and vertices exist; assembly makes the records.

struct BlueprintVertex
{
    Vector3 Position  = {};                                                                  // [len] - Point
    double  Precision = ExactPrecision;                                                      // [len] - Local sphere radius (tolerant models)
};

struct BlueprintEdge
{
    CurveGeometry Curve = StraightLine{};                                                    // [-] - Carrier
    double        Low   = 0.0;                                                               // [-] - τ at Start
    double        High  = 1.0;                                                               // [-] - τ at End
    int           Start = -1;                                                                // [-] - BlueprintVertex index
    int           End   = -1;                                                                // [-] - BlueprintVertex index
    double        Precision = ExactPrecision;                                                // [len] - Local tube radius (tolerant models)
};

// 📝 Directed use of an edge by a loop; the SP-curve shares the edge's τ (same-parameter rule).
struct BlueprintUse
{
    int            Edge         = -1;                                                        // [-] - BlueprintEdge index
    Orientation    Sense        = Orientation::Forward;                                      // [-] - Along / against τ
    ParameterCurve SurfaceCurve = ParameterLine{};                                           // [-] - SP-curve on the face
};

struct BlueprintFace
{
    SurfaceGeometry                        Surface = PlaneSurface{};                         // [-] - Carrier
    Orientation                            Sense   = Orientation::Forward;                   // [-] - Outward vs natural normal
    std::vector<std::vector<BlueprintUse>> Loops   = {};                                     // [-] - Uses in loop order
};

struct BodyBlueprint
{
    std::vector<BlueprintVertex> Vertices = {};                                              // [-] - All candidate vertices
    std::vector<BlueprintEdge>   Edges    = {};                                              // [-] - All candidate edges
    std::vector<BlueprintFace>   Faces    = {};                                              // [-] - Faces of the body
    BodyCategory                 Category = BodyCategory::Solid;                             // [-] - Solid, or Sheet (laminar edges allowed)
    std::vector<double>          ShellVolumes = {};                                          // [len³] - Optional: signed volume per shell, from an
                                                                                             //          identical earlier assembly (empty → integrate)
};

struct AssemblyOutcome
{
    BodyToken              Body      = {};                                                   // [-] - New body
    std::string            Refusal   = {};                                                   // [-] - Non-empty when refused
    int                    Solids    = 0;                                                    // [-] - Solid regions
    int                    Shells    = 0;                                                    // [-] - Shells
    std::vector<FaceToken> Faces     = {};                                                   // [-] - Per blueprint face
    std::vector<EdgeToken> Edges     = {};                                                   // [-] - Per blueprint edge (absent if unused)
    std::vector<int>       EdgeOrder = {};                                                   // [-] - Blueprint edges in issue order
    std::vector<double>    ShellVolumes = {};                                                // [len³] - Signed volume per shell, in issue order
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 ① Manifold check: every used edge has exactly two uses, in opposite senses; ② shells = connected components through
//    shared edges; ③ records issued lazily in face → loop → use order (vertices and edges on first use), radial rings of
//    two fins; ④ outer shells (positive enclosed volume) become solid regions, cavities join the smallest solid region
//    containing a witness vertex. Label prefixes refusals ("Boolean", "Blend", …).
[[nodiscard]] AssemblyOutcome AssembleBody(ModelPartition& Partition, const BodyBlueprint& Blueprint, const std::string& Label);

// 💡 SP-curves for every loop of a face whose uses carry only edges (imported or rebuilt topology). Each loop's chart image
//    is tracked continuously along the loop (every projection referenced to the previous one), so circles that cross a
//    seam unwrap by themselves. Where tracking cannot separate the two uses of a seam — both pass through a singular point
//    (sphere pole, cone apex) and land on the same image — the face interior decides: it lies to the left of each use
//    about the outward normal, so the use with the interior on the +𝑢 side sits on the low chart boundary and its mate one
//    period higher. The loop with the widest image fixes the principal window; every other loop is moved by whole periods
//    into it. Each use is then fitted by FitSurfaceCurve at its mid-image anchor.
void FitFaceSurfaceCurves(const BodyBlueprint& Blueprint, BlueprintFace& Face);

} // namespace Frontier::GeometrySpace::Parametric
