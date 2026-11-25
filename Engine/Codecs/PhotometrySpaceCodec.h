/*============================================================================================================================================
                                                          PHOTOMETRYSPACECODEC.H
============================================================================================================================================*/
// 🧩 .PhotometrySpace Two-Tier Hybrid encoder & decoder for Solar Ephemeris, Area Luminaires, and 64B-aligned 128x32 Candela Grids

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/PhotometricProfile.h"
#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"

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

enum class PhotometryLuminaireCategory : uint8_t
{
    SolarEphemeris    = 0u, // [-] - Solar directional irradiance + astronomical ephemeris configuration
    AreaQuadLuminaire = 1u, // [-] - Polygonal rectangular Linearly Transformed Cosine area luminaire
    MeasuredCandela   = 2u, // [-] - Goniophotometric luminaire with 64B-aligned 128x32 float32 candela grid
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PhotometrySpaceDocumentModel
{
    std::string                                    DocumentTitle     { "UntitledLuminaire" };
    PhotometryLuminaireCategory                    LuminaireCategory = PhotometryLuminaireCategory::SolarEphemeris;
    GeometricRaster::SolarDirectionalSource        SolarSource       {};
    GeometricRaster::SolarCelestialConfiguration   SolarEphemeris    {};
    GeometricRaster::PolygonalAreaQuadLight        AreaQuad          {};
    GeometricRaster::PhotometricPointRadiator      RadiatorPlacement {};
    ResampledPhotometricProfile                    CandelaProfile    {};
    bool                                           CandelaIncluded   = false;
};

class PhotometrySpaceEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const PhotometrySpaceDocumentModel& Model,
        const Workspaces::RevisionTracker*  OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const PhotometrySpaceDocumentModel& Model,
        std::vector<std::byte>&             OutContainerBytes,
        std::string&                        OutDiagnostic,
        const Workspaces::RevisionTracker*  OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&        TargetPath,
        const PhotometrySpaceDocumentModel& Model,
        std::string&                        OutDiagnostic,
        const Workspaces::RevisionTracker*  OptionalRevision = nullptr);
};

class PhotometrySpaceDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload&  Payload,
        PhotometrySpaceDocumentModel& OutModel,
        std::string&                  OutDiagnostic,
        Workspaces::RevisionTracker*  OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>    ContainerBytes,
        PhotometrySpaceDocumentModel& OutModel,
        std::string&                  OutDiagnostic,
        Workspaces::RevisionTracker*  OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path&  SourcePath,
        PhotometrySpaceDocumentModel& OutModel,
        std::string&                  OutDiagnostic,
        Workspaces::RevisionTracker*  OptionalOutRevision = nullptr);
};

struct PhotometrySpaceCodec
{
    using Encoder = PhotometrySpaceEncoder;
    using Decoder = PhotometrySpaceDecoder;
};

} // namespace Frontier::Codecs
