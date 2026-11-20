/*============================================================================================================================================
                                                        FRACTUREPARTITIONTYPES.H
============================================================================================================================================*/
// 🧩 Shared polyhedron, flat-cell, engineering surface, bond, and partition result structures (RHR +Z Up)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Fracture/FractureMaterialRepository.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ConvexFace
{
    std::vector<uint32_t> Indices     {};
    bool                  Interior    = false;
    uint32_t              CutToken    = 0u;
    Mathematics::Axis3D   CrackNormal { 0.0f, 0.0f, 1.0f };
    bool                  Seam        = false;
    uint32_t              SeamId      = 0u;
};

struct ConvexPolyhedron
{
    std::vector<Mathematics::Axis3D> Vertices {};
    std::vector<ConvexFace>          Faces    {};
};

struct PolyhedronMetrics
{
    double              VolumeM3    = 0.0;
    Mathematics::Axis3D Centroid    { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D BoundsMin   { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D BoundsMax   { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D Span        { 0.0f, 0.0f, 0.0f };
    double              RadiusM     = 0.0;
    double              CrackAreaM2 = 0.0;
};

struct FlatConvexCell
{
    uint32_t             VertexCount = 0u;
    uint32_t             FaceCount   = 0u;
    std::vector<double>  Vertices    {}; // Packed [x, y, z, ...]
    std::vector<int32_t> FaceOffsets {}; // Size FaceCount + 1
    std::vector<int32_t> FaceIndices {};
    std::vector<int32_t> FaceTokens  {}; // 0 = exterior, >0 = CutToken
    double               CentroidX   = 0.0;
    double               CentroidY   = 0.0;
    double               CentroidZ   = 0.0;
    double               MinX        = 0.0;
    double               MinY        = 0.0;
    double               MinZ        = 0.0;
    double               MaxX        = 0.0;
    double               MaxY        = 0.0;
    double               MaxZ        = 0.0;
    double               VolumeM3    = 0.0;
    double               CrackAreaM2 = 0.0;
    double               RadiusM     = 0.0;
    double               MinSpanM    = 0.0;
};

struct EngineeringSurface
{
    std::vector<Mathematics::Axis3D> Positions           {};
    std::vector<Mathematics::Axis3D> Normals             {};
    std::vector<uint8_t>             InteriorMask        {};
    uint32_t                         TriangleCount       = 0u;
    double                           ExactNonPlanarVolM3 = 0.0;
};

struct EngineeringSolverMetrics
{
    uint32_t    GridDimension        = 0u;
    double      PeakSigma1MPa        = 0.0;
    double      PeakVonMisesMPa      = 0.0;
    double      PeakModeIKI          = 0.0;
    double      PeakMohrRatio        = 0.0;
    double      HertzContactRadiusM  = 0.0;
    double      PeakHertzPressureMPa = 0.0;
    uint32_t    ModeICount           = 0u;
    uint32_t    ModeIICount          = 0u;
    uint32_t    TJunctionCount       = 0u;
    double      RoughnessRmsMm       = 0.0;
    double      NonPlanarVolumeSumM3 = 0.0;
    double      NonPlanarVolumeDrift = 0.0;
    const char* FractographyLabel    = "";
};

struct FracturePieceEntry
{
    uint32_t                      Index             = 0u;
    std::string                   Token             {};
    ConvexPolyhedron              Polyhedron        {};
    std::vector<ConvexPolyhedron> SubConvexHulls    {};
    EngineeringSurface            DetailedSurface   {};
    bool                HasDetailedSurf   = false;
    double              VolumeM3          = 0.0;
    double              ConvexVolumeM3    = 0.0;
    double              MassKg            = 0.0;
    Mathematics::Axis3D Centroid          { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D BoundsMin         { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D BoundsMax         { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D Span              { 0.0f, 0.0f, 0.0f };
    double              RadiusM           = 0.0;
    double              CrackAreaM2       = 0.0;
    float               Proximity         = 0.0f;
    float               StressRatio       = 0.0f;
    Mathematics::Axis3D InitialVelocity   { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D InitialAngular    { 0.0f, 0.0f, 0.0f };
    bool                VisibilityEnabled = true;
};

struct FractureBondEntry
{
    uint32_t            IndexA         = 0u;
    uint32_t            IndexB         = 0u;
    Mathematics::Axis3D PointA         { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D PointB         { 0.0f, 0.0f, 0.0f };
    double              SharedAreaM2   = 0.0;
    double              CapacityKN     = 0.0;
    double              StrainRatio    = 0.0;
    bool                BrokenOnImpact = false;
};

struct FractureWavefrontPath
{
    std::vector<Mathematics::Axis3D> Points         {};
    bool                             IsPrimaryTrunk = false;
};

struct FracturePartitionReceipt
{
    uint32_t FragmentCount       = 0u;
    uint32_t BondCount           = 0u;
    uint32_t IntactBondCount     = 0u;
    double   SourceVolumeM3      = 0.0;
    double   FragmentVolumeSumM3 = 0.0;
    double   VolumeDriftRatio    = 0.0;
    double   TotalCrackAreaM2    = 0.0;
    double   TotalEnergyBudgetJ  = 0.0;
    double   SpentSurfaceEnergyJ = 0.0;
    double   RayleighSpeedMps    = 0.0;
    double   ComputeDurationMs   = 0.0;
    bool     WatertightManifold  = false;
};

struct FractureSpecification
{
    FractureSolverMethod Solver            = FractureSolverMethod::FastRuntimeBoussinesq;
    const char*          MaterialId        = "concrete";
    uint32_t             Seed              = 42u;
    uint32_t             Ceiling           = 32u;
    double               EnergyJoules      = 2800.0;
    double               MinimumSizeM      = 0.055;
    double               GrainAngleDeg     = 0.0;
    double               BondStrengthScale = 1.0;
    Mathematics::Axis3D  ImpactPoint       { 0.0f, 0.48f, 0.18f };
    Mathematics::Axis3D  ImpactNormal      { 0.0f, -1.0f, 0.0f };
};

struct FracturePartitionResult
{
    FractureSolverMethod               Solver             = FractureSolverMethod::FastRuntimeBoussinesq;
    FractureMaterialSpec               MaterialSpec       {};
    PolyhedronMetrics                  SourceMetrics      {};
    std::vector<FracturePieceEntry>    Fragments          {};
    std::vector<FractureBondEntry>     Bonds              {};
    std::vector<FractureWavefrontPath> WavefrontPaths     {};
    FracturePartitionReceipt           Receipt            {};
    EngineeringSolverMetrics           EngineeringMetrics {};
    bool                               HasEngineering     = false;
};

} // namespace Frontier::GeometrySpace::Fracture
