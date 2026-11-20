/*============================================================================================================================================
                                                      CONTROLVEHICLESURFACEDECODER.H
============================================================================================================================================*/
// 🧩 FCT1 binary facet archive decoder assembling the 25,068-triangle Controlled Integrator vehicle (Body + 4 Rims + 4 Tyres)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class VehicleSubassemblyPart : uint8_t
{
    BodyChassis    = 0u,
    RimFrontLeft   = 1u,
    RimFrontRight  = 2u,
    RimRearLeft    = 3u,
    RimRearRight   = 4u,
    TyreFrontLeft  = 5u,
    TyreFrontRight = 6u,
    TyreRearLeft   = 7u,
    TyreRearRight  = 8u,
};

enum class VehicleSurfaceMaterialRole : uint8_t
{
    BodyCoat       = 0u, // Clear-coated metallic car body paint
    CanopyGloss    = 1u, // Clear-coated dark roof/pillar canopy
    VehicleGlass   = 2u, // Dielectric windshield & side glass
    VehiclePlastic = 3u, // Matte dark lower trim & intake grille
    TyreRubber     = 4u, // Vulcanized tyre tread & sidewall rubber
    RimAlloy       = 5u, // Satin forged wheel spoke alloy
    HubAccent      = 6u, // Center wheel hub cap accent
    BrakeDisc      = 7u, // Brushed steel brake rotor
    VehicleTrim    = 8u, // Satin dark exterior trim
    HeadlightLens  = 9u, // Front headlamp lens emitter
    TaillightLens  = 10u,// Rear tail-lamp lens emitter
    Count          = 11u,
};

enum class VehicleCoordinateConvention : uint8_t
{
    FrontierWorldZUp = 0u, // Right-handed (+X forward, +Y left, +Z up) resting tyres on Z = 0
    StudioPathYUp    = 1u, // Right-handed (+X forward, +Y up, +Z right) resting tyres on Y = 0
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DecodedFacetStream
{
    uint32_t              VertexCount   = 0u;
    uint32_t              FacetCount    = 0u;
    std::vector<float>    Positions     {}; // [m] - 3 floats per vertex
    std::vector<uint32_t> Indices       {}; // [-] - 3 vertex indices per facet
    std::vector<uint32_t> FamilyCodes   {}; // [-] - 1 family code per facet
    std::vector<float>    CornerNormals {}; // [-] - 9 floats per facet (38-deg creased if absent in archive)
};

struct AssembledVehicleTriangle
{
    Mathematics::Axis3D        CornerA      {};
    Mathematics::Axis3D        CornerB      {};
    Mathematics::Axis3D        CornerC      {};
    Mathematics::Axis3D        NormalA      { 0.0f, 0.0f, 1.0f };
    Mathematics::Axis3D        NormalB      { 0.0f, 0.0f, 1.0f };
    Mathematics::Axis3D        NormalC      { 0.0f, 0.0f, 1.0f };
    float                      AreaMeters2  = 0.0f;
    uint32_t                   FamilyCode   = 6u;
    VehicleSubassemblyPart     Part         = VehicleSubassemblyPart::BodyChassis;
    VehicleSurfaceMaterialRole MaterialRole = VehicleSurfaceMaterialRole::BodyCoat;
};

struct AssembledControlVehicle
{
    std::vector<AssembledVehicleTriangle> Triangles          {};
    Mathematics::Axis3D                   BoundsMin          {};
    Mathematics::Axis3D                   BoundsMax          {};
    Mathematics::Axis3D                   BoundsCenter       {};
    uint32_t                              BodyTriangleCount  = 0u;
    uint32_t                              RimTriangleCount   = 0u;
    uint32_t                              TyreTriangleCount  = 0u;
    VehicleCoordinateConvention           Convention         = VehicleCoordinateConvention::FrontierWorldZUp;
};

//------------------------------------------------------------------------------------------------------------------------
//                                             CONTROL VEHICLE SURFACE DECODER
//------------------------------------------------------------------------------------------------------------------------

class ControlVehicleSurfaceDecoder
{
public:
    // Decodes a single FCT1 binary buffer (.face or .facet) and derives 38-degree creased normals when omitted
    [[nodiscard]] static bool DecodeFacetBytes(
        std::span<const uint8_t> ArchiveBytes,
        DecodedFacetStream&      OutStream,
        std::string&             OutDiagnostic,
        float                    CreaseAngleDegrees = 38.0f);

    // Decodes a single FCT1 binary file from disk
    [[nodiscard]] static bool DecodeFacetFile(
        const std::filesystem::path& FilePath,
        DecodedFacetStream&          OutStream,
        std::string&                 OutDiagnostic,
        float                        CreaseAngleDegrees = 38.0f);

    // Assembles the complete 25,068-triangle Controlled Integrator vehicle (Body + 4 Rims + 4 Tyres)
    [[nodiscard]] static bool DecodeAssembledVehicle(
        const std::filesystem::path& RepositoryRoot,
        VehicleCoordinateConvention  Convention,
        AssembledControlVehicle&     OutVehicle,
        std::string&                 OutDiagnostic);

    // Converts a subset (or all) of assembled vehicle triangles into a PolygonNetwork for ClusterPyramid / world instancing
    [[nodiscard]] static GeometrySpace::PolygonNetwork BuildPolygonNetworkFromRole(
        const AssembledControlVehicle& Vehicle,
        VehicleSurfaceMaterialRole     TargetRole);
};

} // namespace Frontier::Codecs
