/*============================================================================================================================================
                                                  INDEXEDTRIANGLESTREAMINTERCHANGE.H
============================================================================================================================================*/
// 🧩 Binary FCT1 stream importer converting Z-up vehicle triangle streams & CourseMaterial slots into Y-up PolygonNetworks

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::Interchange
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct CourseMaterialDescriptor
{
    uint32_t              SlotIdentifier   = 0u;    // [id]  - Authored CourseMaterial slot (6..15)
    std::string_view      Title            {};      // [-]   - Canonical slot title
    Mathematics::Scalar3D AlbedoLinearRGB  {};      // [0-1] - Linear RGB base albedo
    float                 Roughness        = 0.5f;  // [0-1] - Microfacet roughness
    float                 Metallic         = 0.0f;  // [0-1] - Metallic response
    Mathematics::Scalar3D EmissiveLinear   {};      // [0-1] - Linear RGB emissive radiance
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class IndexedTriangleStreamInterchange
{
public:
    [[nodiscard]] static const CourseMaterialDescriptor* InspectCourseMaterial(uint32_t SlotIdentifier) noexcept;

    [[nodiscard]] static bool ImportFct1Bytes(
        std::span<const std::byte>             RawBytes,
        std::string_view                       DefaultTitle,
        bool                                   ConvertZUpToYUp,
        GeometrySpace::ConstructedSampleAsset& OutAsset,
        std::string&                           OutDiagnostic);

    [[nodiscard]] static bool ImportFct1File(
        const std::filesystem::path&           SourcePath,
        bool                                   ConvertZUpToYUp,
        GeometrySpace::ConstructedSampleAsset& OutAsset,
        std::string&                           OutDiagnostic);
};

} // namespace Frontier::Interchange
