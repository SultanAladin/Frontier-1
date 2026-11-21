/*============================================================================================================================================
                                                      CONTROLVEHICLESURFACEDECODER.H
============================================================================================================================================*/
// 🧩 FCT1 binary face binary decoder assembling the 25,068-triangle Controlled Integrator vehicle (Body + 4 Rims + 4 Tyres)

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

enum class VehicleSurfaceSlot : uint8_t
{
    BodyCoat       = 0u, // Clear-coated metallic car body paint
    CanopyGloss    = 1u, // Clear-coated dark roof/pillar canopy
    VehicleGlass   = 2u, // Dielectric windshield & side glass
    VehiclePlastic = 3u, // Matte dark lower trim & intake grille
    TyreRubber     = 4u, // Vulcanized tyre tread & sidewall rubber
    RimAlloy       = 5u, // Satin forged wheel spoke alloy
    WheelCapAccent      = 6u, // Center wheel wheel cap accent
    BrakeDisc      = 7u, // Brushed steel brake rotor
    VehicleTrim    = 8u, // Satin dark exterior trim
    HeadlightLens  = 9u, // Front headlamp lens luminaire
    TaillightLens  = 10u,// Rear tail-lamp lens luminaire
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

struct DecodedFaceStream
{
    uint32_t              VertexCount   = 0u;
    uint32_t              FaceCount    = 0u;
    std::vector<float>    Positions     {}; // [m] - 3 floats per vertex
    std::vector<uint32_t> Indices       {}; // [-] - 3 vertex indices per face
    std::vector<uint32_t> PartGroupCodes   {}; // [-] - 1 part group code per face
    std::vector<float>    CornerNormals {}; // [-] - 9 floats per face (38-deg creased if absent in binary)
};

struct DecodedVehicleTriangle
{
    Mathematics::Axis3D        CornerA      {};
    Mathematics::Axis3D        CornerB      {};
    Mathematics::Axis3D        CornerC      {};
    Mathematics::Axis3D        NormalA      { 0.0f, 0.0f, 1.0f };
    Mathematics::Axis3D        NormalB      { 0.0f, 0.0f, 1.0f };
    Mathematics::Axis3D        NormalC      { 0.0f, 0.0f, 1.0f };
    float                      AreaMeters2  = 0.0f;
    uint32_t                   PartGroupCode   = 6u;
    VehicleSubassemblyPart     Part         = VehicleSubassemblyPart::BodyChassis;
    VehicleSurfaceSlot SurfaceSlot = VehicleSurfaceSlot::BodyCoat;
};

struct AssembledControlVehicle
{
    std::vector<DecodedVehicleTriangle> Triangles          {};
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
    // Decodes a single FCT1 binary buffer (.face or .face) and derives 38-degree creased normals when omitted
    [[nodiscard]] static bool DecodeFaceBytes(
        std::span<const uint8_t> PayloadBytes,
        DecodedFaceStream&      OutStream,
        std::string&             OutDiagnostic,
        float                    CreaseAngleDegrees = 38.0f);

    // Decodes a single FCT1 binary file from disk
    [[nodiscard]] static bool DecodeFaceFile(
        const std::filesystem::path& FilePath,
        DecodedFaceStream&          OutStream,
        std::string&                 OutDiagnostic,
        float                        CreaseAngleDegrees = 38.0f);

    // Assembles the complete 25,068-triangle Controlled Integrator vehicle (Body + 4 Rims + 4 Tyres)
    [[nodiscard]] static bool DecodeAssembledVehicle(
        const std::filesystem::path& RepositoryRoot,
        VehicleCoordinateConvention  Convention,
        AssembledControlVehicle&     OutVehicle,
        std::string&                 OutDiagnostic);

    // Converts a subset (or all) of assembled vehicle triangles into a PolygonNetwork for ClusterPyramid / world instancing
    [[nodiscard]] static GeometrySpace::PolygonNetwork ConstructPolygonNetworkFromSlot(
        const AssembledControlVehicle& Vehicle,
        VehicleSurfaceSlot     TargetSlot);
};

} // namespace Frontier::Codecs
