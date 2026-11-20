/*============================================================================================================================================
                                                           CONVEXPARTITIONING.H
============================================================================================================================================*/
// 🧩 Exact 3D convex polyhedron half-space clipping, PolygonNetwork bridges, 6-solver fracture dispatch, shared-cut bonds, and SDF section query

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Fracture/FracturePartitionTypes.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>
#include <vector>

namespace Frontier::GeometrySpace::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PolyhedronClipOutput
{
    ConvexPolyhedron Front     {};
    ConvexPolyhedron Back      {};
    bool             HasFront  = false;
    bool             HasBack   = false;
    double           CutAreaM2 = 0.0;
};

struct PieceSdfSectionOutput
{
    std::vector<float> Field          {};
    uint32_t           Resolution     = 64u;
    double             OccupancyRatio = 0.0;
};

struct SdfVolumeFootprint
{
    uint64_t Bytes          = 0ull;
    double   Megabytes      = 0.0;
    uint64_t VoxelCount     = 0ull;
    uint32_t OccupiedBricks = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ConvexPartitioning
{
public:
    [[nodiscard]] static Mathematics::Axis3D ComputeFaceNormal(
        const ConvexPolyhedron& Polyhedron,
        const ConvexFace&       Face) noexcept;

    [[nodiscard]] static double ComputeFaceAreaM2(
        const ConvexPolyhedron& Polyhedron,
        const ConvexFace&       Face) noexcept;

    [[nodiscard]] static PolyhedronMetrics ComputePolyhedronMetrics(const ConvexPolyhedron& Polyhedron) noexcept;

    static void OrientFacesOutward(ConvexPolyhedron& Polyhedron) noexcept;

    [[nodiscard]] static ConvexPolyhedron ConstructBoxPolyhedron(float ExtentX, float ExtentY, float ExtentZ);
    [[nodiscard]] static ConvexPolyhedron ConstructPrismPolyhedron(float RadiusBottom, float RadiusTop, float HeightZ, uint32_t Sides = 20u);
    [[nodiscard]] static ConvexPolyhedron ConstructSolidPolyhedron(
        FractureSolidCategory Category,
        Mathematics::Scalar3D Scale = { 1.0f, 1.0f, 1.0f },
        uint32_t              Seed  = 17u);

    [[nodiscard]] static ConvexPolyhedron ConvertPolygonNetworkToPolyhedron(const PolygonNetwork& Network);
    [[nodiscard]] static PolygonNetwork   ConvertPolyhedronToPolygonNetwork(
        const ConvexPolyhedron&      Polyhedron,
        const FaceShadingAssignment& ExteriorShading = {},
        const FaceShadingAssignment& InteriorShading = {});

    [[nodiscard]] static PolyhedronClipOutput ClipConvexPolyhedron(
        const ConvexPolyhedron& Polyhedron,
        Mathematics::Axis3D     PlaneNormal,
        double                  PlaneOffset,
        uint32_t                CutToken     = 1u,
        bool                    MarkInterior = true);

    [[nodiscard]] static double ComputeDirectionalToughness(
        const FractureMaterialSpec& Material,
        Mathematics::Axis3D         PlaneNormal,
        double                      GrainAngleRad,
        DeterministicRandomStream*  Rand = nullptr) noexcept;

    [[nodiscard]] static std::vector<FractureBondEntry> ConstructSharedCutBonds(
        const std::vector<FracturePieceEntry>& Fragments,
        const FractureMaterialSpec&            Material,
        Mathematics::Axis3D                    ImpactPoint,
        double                                 SolidRadiusM,
        double                                 BondStrengthScale = 1.0);

    [[nodiscard]] static FracturePartitionResult ExecuteFracturePartition(
        const ConvexPolyhedron&      SourcePolyhedron,
        const FractureSpecification& Specification);

    [[nodiscard]] static PieceSdfSectionOutput ComputePieceSdfSection(
        const std::vector<FracturePieceEntry>& Fragments,
        float                                  SectionZ       = 0.0f,
        uint32_t                               GridResolution = 64u,
        float                                  BoundsRadius   = 1.15f);

    [[nodiscard]] static SdfVolumeFootprint ComputeSdfVolumeFootprint(
        uint32_t FragmentCount,
        uint32_t SdfResolution,
        bool     Enabled) noexcept;
};

} // namespace Frontier::GeometrySpace::Fracture
