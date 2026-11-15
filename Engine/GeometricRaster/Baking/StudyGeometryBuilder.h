/*============================================================================================================================================
                                                            STUDYGEOMETRYBUILDER.H
============================================================================================================================================*/
// 🧩 High/Low baking study synthesis, .OBJ/.STL file import, UV-island flood extraction, and GPU triangle/vertex upload preparation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::GeometricRaster::Baking
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class StudyGeometryCategory : uint8_t
{
    UtahTeapot = 0u,
    ShaderBall = 1u,
    ChamferBox = 2u,
    UvSphere   = 3u,
    TorusKnot  = 4u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   GPU ALIGNED STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) GpuBakeTriangle
{
    float    CornerA[3]      {}; // [m]
    uint32_t PartOrdinal     = 0u;
    float    CornerB[3]      {}; // [m]
    uint32_t IslandOrdinal   = 0u;
    float    CornerC[3]      {}; // [m]
    uint32_t MaterialOrdinal = 0u;
    float    NormalA[3]      {}; // [-]
    float    AreaWeight      = 0.0f;
    float    NormalB[3]      {}; // [-]
    float    PadB            = 0.0f;
    float    NormalC[3]      {}; // [-]
    float    PadC            = 0.0f;
    float    GeometricNormal[3] {}; // [-]
    float    PadGeom         = 0.0f;
};
static_assert(sizeof(GpuBakeTriangle) == 112u, "GpuBakeTriangle must match 112-byte HLSL/Slang layout.");

struct alignas(16) GpuLowSurfaceVertex
{
    float Position[3]     {}; // [m]
    float PartOrdinal     = 0.0f;
    float SmoothNormal[3] {}; // [-]
    float IslandOrdinal   = 0.0f;
    float Tangent[3]      {}; // [-]
    float Handedness      = 1.0f;
    float Bitangent[3]    {}; // [-]
    float UvCoordU        = 0.0f;
    float UvCoordV        = 0.0f;
    float BarycentricX    = 0.0f;
    float BarycentricY    = 0.0f;
    float WireMask        = 1.0f;
};
static_assert(sizeof(GpuLowSurfaceVertex) == 80u, "GpuLowSurfaceVertex must match 80-byte HLSL/Slang layout.");

struct alignas(16) GpuSurfaceBakeConfig
{
    uint32_t ResolutionPx           = 512u;
    uint32_t HighTriangleCount      = 0u;
    uint32_t WideEntryCount         = 0u;
    uint32_t AoSampleCount          = 12u;
    float    FrontCageMeters        = 0.14f;
    float    RearCageMeters         = 0.14f;
    float    AoMaxRayDistanceMeters = 0.45f;
    float    TangentSignY           = 1.0f;
    float    BoundsMin[3]           {};
    float    BoundsDiagMeters       = 1.0f;
    float    BoundsMax[3]           {};
    float    BevelSigmaMeters       = 0.015f;
    uint32_t ActiveTargetBitmask    = 0xFFFFFFFFu;
    uint32_t DilationRadiusPx       = 6u;
    uint32_t DilationStepIndex      = 0u;
    uint32_t Pad0                   = 0u;
};
static_assert(sizeof(GpuSurfaceBakeConfig) == 80u, "GpuSurfaceBakeConfig must match 80-byte HLSL/Slang constant buffer.");

struct alignas(16) GpuVolumeBakeConfig
{
    uint32_t GridDim                = 36u;
    uint32_t HighTriangleCount      = 0u;
    uint32_t WideEntryCount         = 0u;
    uint32_t SignedMode             = 1u;
    float    BoundsMin[3]           {};
    float    VoxelStepMeters        = 0.05f;
    float    BoundsMax[3]           {};
    float    BoundsDiagMeters       = 1.0f;
    uint32_t SectionAxis            = 0u;
    float    SectionDepthNormalized = 0.5f;
    uint32_t SectionSizePx          = 512u;
    uint32_t Pad0                   = 0u;
};
static_assert(sizeof(GpuVolumeBakeConfig) == 64u, "GpuVolumeBakeConfig must match 64-byte HLSL/Slang constant buffer.");

struct alignas(16) GpuPreviewConfig
{
    float ViewProjTransform[16]     {};
    float CameraEyeMode[4]       {};
    float OverlayFlagsPacked[4]        {};
    float ViewportSizeTime[4]    {};
    float OverlayColor[4]           {};
    float BoundsMinPacked[4]        {};
    float BoundsMaxPacked[4]        {};
};
static_assert(sizeof(GpuPreviewConfig) == 160u, "GpuPreviewConfig must match 160-byte HLSL/Slang constant buffer.");

//------------------------------------------------------------------------------------------------------------------------
//                                                  PREPARED STUDY PAIR
//------------------------------------------------------------------------------------------------------------------------

struct PreparedStudyPair
{
    GeometrySpace::PolygonNetwork                  LowNetwork         {};
    GeometrySpace::PolygonNetwork                  HighNetwork        {};
    std::vector<GpuLowSurfaceVertex>               LowSurfaceVertices {};
    std::vector<GpuLowSurfaceVertex>               HighWireVertices   {};
    std::vector<GpuBakeTriangle>                   HighTriangles      {};
    std::vector<SpatialAcceleration::RayTrianglePrimitive> HighBvhPrimitives {};
    std::vector<Mathematics::Axis3D>               HighPositionsFlat  {};
    Mathematics::Axis3D                            BoundsMin          {};
    Mathematics::Axis3D                            BoundsMax          {};
    float                                          BoundsDiagMeters   = 1.0f;
    uint32_t                                       LowTriangleCount   = 0u;
    uint32_t                                       HighTriangleCount  = 0u;
    uint32_t                                       UvIslandCount      = 1u;
    uint32_t                                       PartGroupCount     = 1u;
    std::string                                    StudyDisplayLabel  {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  BUILDER ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

class StudyGeometryBuilder
{
public:
    [[nodiscard]] static PreparedStudyPair ConstructStudioStudy(StudyGeometryCategory Kind);

    [[nodiscard]] static bool ImportPolygonNetworkFromFile(
        const std::filesystem::path&     FilePath,
        GeometrySpace::PolygonNetwork&   OutNetwork,
        bool                             NormalizeUnitPedestal,
        std::string*                     OutDiagnostic = nullptr);

    [[nodiscard]] static PreparedStudyPair PrepareFromPolygonNetworks(
        GeometrySpace::PolygonNetwork LowNetwork,
        GeometrySpace::PolygonNetwork HighNetwork,
        std::string                   Label);
};

} // namespace Frontier::GeometricRaster::Baking
