/*============================================================================================================================================
                                                              WATERTIGHTSEWING.H
============================================================================================================================================*/
// 🧩 Spatial-hashed boundary vertex welding, consistent outward winding unification, planar hole closure, and solid volume audit

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WatertightAuditSummary
{
    uint32_t VertexCount                     = 0u;    // [count] - Total live vertices in PolygonNetwork
    uint32_t UniqueUndirectedEdgeCount       = 0u;    // [count] - Distinct undirected edges (paired + open)
    uint32_t FaceCount                       = 0u;    // [count] - Total polygonal faces
    uint32_t OpenBoundaryEdgeCount           = 0u;    // [count] - Unpaired half-edges on open borders
    uint32_t NonManifoldEdgeCount            = 0u;    // [count] - Undirected edges shared by 3 or more half-edges
    uint32_t OrientationConflictEdgeCount    = 0u;    // [count] - Paired edges running in identical direction
    int32_t  EulerCharacteristic             = 0;     // [-]     - V - E + F topological invariant (2 for sphere, 0 for torus)
    float    SignedEnclosedVolumeCubicMeters = 0.0f;  // [m^3]   - Divergence-theorem enclosed solid volume
    float    SurfaceAreaSquareMeters         = 0.0f;  // [m^2]   - Total 3D polygon surface area
    bool     ManifoldWatertight              = false; // [-]     - True iff Open == 0, NonManifold == 0, Conflicts == 0, Volume > 0
};

struct WatertightRepairSummary
{
    uint32_t               WeldedVertexCount    = 0u; // [count] - Coincident vertices merged within WeldToleranceMeters
    uint32_t               RemovedDegenerateFaces = 0u; // [count] - Collapsed faces pruned after vertex welding
    uint32_t               FlippedFaceCount     = 0u; // [count] - Faces reversed to achieve consistent outward normals
    uint32_t               ClosedBoundaryLoops  = 0u; // [count] - Open hole loops sealed via advancing-front triangulation
    uint32_t               SynthesizedCapFaces  = 0u; // [count] - New cap faces appended during hole closure
    WatertightAuditSummary PostRepairAudit      {};   // [-]     - Verified solid audit after repair sequence
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] WatertightAuditSummary EvaluateWatertightAudit(const PolygonNetwork& Network) noexcept;

uint32_t WeldCoincidentVertices(
    PolygonNetwork& TargetNetwork,
    float           WeldToleranceMeters = 1.0e-4f,
    uint32_t*       OutRemovedDegenerateFaces = nullptr);

uint32_t UnifyConsistentFaceWinding(
    PolygonNetwork& TargetNetwork,
    bool            OrientOutwardVolume = true);

uint32_t CloseOpenBoundaryLoops(
    PolygonNetwork& TargetNetwork,
    uint32_t        MaxHolePerimeterEdges = 64u,
    uint32_t*       OutSynthesizedFaces   = nullptr);

[[nodiscard]] WatertightRepairSummary RepairWatertightSolid(
    PolygonNetwork& TargetNetwork,
    float           WeldToleranceMeters   = 1.0e-4f,
    uint32_t        MaxHolePerimeterEdges = 64u);

} // namespace Frontier::GeometrySpace
