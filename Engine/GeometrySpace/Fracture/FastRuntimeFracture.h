/*============================================================================================================================================
                                                          FASTRUNTIMEFRACTURE.H
============================================================================================================================================*/
// 🧩 Sub-millisecond preallocated flat-array convex fracture solver with Boussinesq-Hertz stress and CutToken bond builder

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Fracture/FracturePartitionTypes.h"

#include <vector>

namespace Frontier::GeometrySpace::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FlatClipScratch
{
    static constexpr uint32_t MaxVerts   = 1024u;
    static constexpr uint32_t MaxFaces   = 512u;
    static constexpr uint32_t MaxIndices = 8192u;
    static constexpr uint32_t MaxCapPts  = 128u;

    double  Dist[MaxVerts]             {};
    int8_t  Side[MaxVerts]             {};
    int32_t FrontRemap[MaxVerts]       {};
    int32_t BackRemap[MaxVerts]        {};

    double  FrontVerts[MaxVerts * 3u]  {};
    int32_t FrontFaceOff[MaxFaces + 1u]{};
    int32_t FrontFaceIdx[MaxIndices]   {};
    int32_t FrontTokens[MaxFaces]      {};

    double  BackVerts[MaxVerts * 3u]   {};
    int32_t BackFaceOff[MaxFaces + 1u] {};
    int32_t BackFaceIdx[MaxIndices]    {};
    int32_t BackTokens[MaxFaces]       {};

    double  CapPts[MaxCapPts * 3u]     {};
    double  CapAngles[MaxCapPts]       {};
    int32_t CapOrder[MaxCapPts]        {};
    int32_t FaceFront[MaxCapPts]       {};
    int32_t FaceBack[MaxCapPts]        {};

    FlatConvexCell FrontCell           {};
    FlatConvexCell BackCell            {};
    double         CutAreaM2           = 0.0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class FastRuntimeFracture
{
public:
    [[nodiscard]] static FlatConvexCell   PackFlatConvexCell(const ConvexPolyhedron& Polyhedron);
    [[nodiscard]] static ConvexPolyhedron UnpackFlatConvexCell(const FlatConvexCell& Cell);
    static void                           ComputeFlatCellMetrics(FlatConvexCell& Cell) noexcept;

    [[nodiscard]] static bool ClipFlatConvexCell(
        const FlatConvexCell& Cell,
        double                NormalX,
        double                NormalY,
        double                NormalZ,
        double                PlaneOffset,
        int32_t               CutToken,
        FlatClipScratch&      Scratch);

    [[nodiscard]] static std::vector<FractureBondEntry> BuildTokenBucketBonds(
        const std::vector<FlatConvexCell>&     Cells,
        const std::vector<FracturePieceEntry>& Fragments,
        const FractureMaterialSpec&            Material,
        Mathematics::Axis3D                    ImpactPoint,
        double                                 SolidRadiusM,
        double                                 BondStrengthScale,
        int32_t                                MaxToken);

    [[nodiscard]] static FracturePartitionResult Execute(
        const ConvexPolyhedron&      SourcePolyhedron,
        const FractureSpecification& Specification);
};

} // namespace Frontier::GeometrySpace::Fracture
