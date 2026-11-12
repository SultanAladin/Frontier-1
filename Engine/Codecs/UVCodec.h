/*============================================================================================================================================
                                                                 UVCODEC.H
============================================================================================================================================*/
// 🧩 .UVSpace encoder & decoder persisting .UDIM layout specifications and 64B-aligned .UVTile parameterization streams

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct UVTileDescriptor
{
    int32_t  TileU                  = 0;     // [tile] - Integer UDIM horizontal UVTile index
    int32_t  TileV                  = 0;     // [tile] - Integer UDIM vertical UVTile index
    uint32_t UDIMNumber             = 1001u; // [code] - Canonical UDIM code (1001 + TileU + 10 * TileV)
    double   TexelDensityPxPerMeter = 512.0; // [px/m] - Target physical texel density for this UVTile
    double   GutterPaddingPixels    = 8.0;   // [px]   - Inter-island bleed margin inside this UVTile
};

struct UVSpaceDocumentModel
{
    std::string                   DocumentTitle        { "AssetParameterization" }; // [-]  - .UVSpace document title
    uint32_t                      GridColumns          = 10u;                       // [col]- UDIM horizontal row width (standard 10)
    double                        ReferenceScaleMeters = 1.0;                       // [m]  - Real-world reference metric scale
    std::vector<UVTileDescriptor> Tiles                {};                          // [-]  - .UVTile layout descriptors
    std::vector<float>            ParameterizedUVPairs {};                          // [uv] - 64B-aligned [u0, v0, u1, v1...] stream
};

class UVEncoder
{
public:
    [[nodiscard]] static constexpr uint32_t ComputeUDIMNumber(int32_t TileU, int32_t TileV) noexcept
    {
        const int32_t ClampedU = TileU < 0 ? 0 : (TileU > 9 ? 9 : TileU);
        const int32_t ClampedV = TileV < 0 ? 0 : TileV;
        return static_cast<uint32_t>(1001 + ClampedU + 10 * ClampedV);
    }

    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const UVSpaceDocumentModel&        Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const UVSpaceDocumentModel&        Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class UVDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        UVSpaceDocumentModel&        OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        UVSpaceDocumentModel&        OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct UVCodec
{
    using Encoder = UVEncoder;
    using Decoder = UVDecoder;
};

} // namespace Frontier::Codecs
