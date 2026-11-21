/*============================================================================================================================================
                                                            WORLDSPACECODEC.H
============================================================================================================================================*/
// 🧩 .WorldSpace Two-Tier Hybrid encoder & decoder for complete World Scene Hierarchies, Placements, Luminaires & Environment Links

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"
#include "Engine/Mathematics/LinearColors.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorldGeometryPlacementEntry
{
    std::string           PlacementTitle        {};                     // [-]   - Outliner & world hierarchy title (e.g. "ControlVehicle")
    std::string           StableIdentifier      {};                     // [-]   - Stable scene row identifier
    std::string           Subtitle              {};                     // [-]   - Outliner caption
    std::string           AuthorNotes           {};                     // [-]   - Inspector authored notes
    std::string           GeometrySpacePath     {};                     // [-]   - Relative .GeometrySpace asset path
    std::string           ComponentSpacePath    {};                     // [-]   - Optional .ComponentSpace rig path
    std::string           PrimaryMaterialPath   {};                     // [-]   - Relative .MaterialSpace asset path
    Mathematics::Axis3D   CenterWorld           { 0.0f, 0.0f, 0.0f };   // [m]   - World-space center coordinate
    Mathematics::Scalar3D DimensionsMeters      { 1.0f, 1.0f, 1.0f };   // [m]   - Full X/Y/Z extent in meters
    float                 RotationZDegrees      = 0.0f;                 // [deg] - Rigid yaw rotation around +Z
    uint32_t              PrimitiveShapeOrdinal = 0u;                   // [idx] - 0=Box, 1=Sphere, 2=Cylinder, 3=Cone, 4=Torus
    bool                  SphericalPrimitive    = false;
    bool                  GlassShell            = false;
    bool                  VisibilityEnabled     = true;
    bool                  CastShadows           = true;
};

struct WorldMeasuredLuminairePlacement
{
    std::string            PlacementTitle      {};
    std::string            PhotometrySpacePath {};
    Mathematics::Axis3D    PositionWorld       { 0.0f, 0.0f, 3.0f };
    Mathematics::Axis3D    BeamAxis            { 0.0f, 0.0f, -1.0f };
    Mathematics::Axis3D    ZeroAzimuthAxis     { 1.0f, 0.0f, 0.0f };
    Mathematics::LinearRGB PeakIntensityRgb    { 1.0f, 1.0f, 1.0f };
    float                  RangeMeters         = 24.0f;
    float                  RadiusMeters        = 0.06f;
};

struct WorldAreaLuminairePlacement
{
    std::string         PlacementTitle      {};
    std::string         PhotometrySpacePath {};
    Mathematics::Axis3D CenterWorld         { 0.0f, 0.0f, 2.5f };
    bool                VisibilityEnabled   = true;
};

struct WorldSpaceDocumentModel
{
    std::string                                  DocumentTitle               { "VehicleClearSkyDefault" };
    std::string                                  SolarPhotometryPath         {};
    std::string                                  EnvironmentVolumetricPath   {};
    std::string                                  RenderSettingsPath          {};
    GeometricRaster::PerspectiveCameraBasis      EditorCamera                {};
    std::vector<WorldGeometryPlacementEntry>     GeometryPlacements          {};
    std::vector<WorldAreaLuminairePlacement>     AreaLuminairePlacements     {};
    std::vector<WorldMeasuredLuminairePlacement> MeasuredLuminairePlacements {};
};

class WorldSpaceEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const WorldSpaceDocumentModel&     Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const WorldSpaceDocumentModel&     Model,
        std::vector<std::byte>&            OutContainerBytes,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const WorldSpaceDocumentModel&     Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class WorldSpaceDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        WorldSpaceDocumentModel&     OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>   ContainerBytes,
        WorldSpaceDocumentModel&     OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        WorldSpaceDocumentModel&     OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct WorldSpaceCodec
{
    using Encoder = WorldSpaceEncoder;
    using Decoder = WorldSpaceDecoder;
};

} // namespace Frontier::Codecs
