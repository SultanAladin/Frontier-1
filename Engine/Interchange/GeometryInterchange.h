/*============================================================================================================================================
                                                            GEOMETRYINTERCHANGE.H
============================================================================================================================================*/
// 🧩 External Wavefront .obj and glTF 2.0 .gltf geometry interchange importing/exporting multi-UDIM PolygonNetworks & FacePartitionRegisters

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/FacePartitionRegister.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::Interchange
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ExternalGeometryFormat : uint8_t
{
    Unknown            = 0u, // [-] - Unrecognized file extension
    WavefrontObj       = 1u, // [-] - Wavefront .obj polygonal text stream
    Gltf2Json          = 2u, // [-] - Khronos glTF 2.0 .gltf JSON + embedded/external buffer
    Fct1TriangleStream = 3u, // [-] - Frontier FCT1 binary triangle + CourseMaterial stream (.facet)
    Gltf2Binary        = 4u, // [-] - Khronos glTF 2.0 binary .glb container
    Stereolithography  = 5u, // [-] - Binary or ASCII .stl triangle soup
    StanfordPolygon    = 6u, // [-] - ASCII or binary_little_endian .ply polygon stream
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class GeometryInterchange
{
public:
    [[nodiscard]] static ExternalGeometryFormat DetectFormatFromPath(const std::filesystem::path& FilePath) noexcept;

    [[nodiscard]] static bool ImportAssetFromFile(
        const std::filesystem::path&           SourcePath,
        GeometrySpace::ConstructedSampleAsset& OutAsset,
        std::string&                           OutDiagnostic);

    [[nodiscard]] static bool ExportAssetToFile(
        const std::filesystem::path&                 TargetPath,
        const GeometrySpace::PolygonNetwork&         Network,
        const GeometrySpace::FacePartitionRegister*  OptionalPartitions,
        std::string&                                 OutDiagnostic);

    [[nodiscard]] static bool ImportParametricComplex(
        const GeometrySpace::Parametric::PolygonNetworkStructure& Complex,
        std::string_view                                 DefaultTitle,
        GeometrySpace::ConstructedSampleAsset&           OutAsset,
        std::string&                                     OutDiagnostic);

    [[nodiscard]] static bool ImportParametricBody(
        const GeometrySpace::Parametric::ModelPartition&       Partition,
        GeometrySpace::Parametric::BodyToken                   Body,
        const GeometrySpace::Parametric::TessellationSettings& Settings,
        std::string_view                                       DefaultTitle,
        GeometrySpace::ConstructedSampleAsset&                 OutAsset,
        std::string&                                           OutDiagnostic);

    [[nodiscard]] static bool ExportParametricBodyToFile(
        const std::filesystem::path&                           TargetPath,
        const GeometrySpace::Parametric::ModelPartition&       Partition,
        GeometrySpace::Parametric::BodyToken                   Body,
        const GeometrySpace::Parametric::TessellationSettings& Settings,
        std::string_view                                       DefaultTitle,
        std::string&                                           OutDiagnostic);

    [[nodiscard]] static bool ImportWavefrontObjText(
        std::string_view                       ObjText,
        std::string_view                       DefaultTitle,
        GeometrySpace::ConstructedSampleAsset& OutAsset,
        std::string&                           OutDiagnostic);

    [[nodiscard]] static std::string ExportWavefrontObjText(
        const GeometrySpace::PolygonNetwork&        Network,
        const GeometrySpace::FacePartitionRegister* OptionalPartitions = nullptr);

    [[nodiscard]] static bool ImportGltf2JsonText(
        std::string_view                       GltfJsonText,
        const std::filesystem::path&           RootDirectory,
        std::string_view                       DefaultTitle,
        GeometrySpace::ConstructedSampleAsset& OutAsset,
        std::string&                           OutDiagnostic);

    [[nodiscard]] static bool ImportGltf2BinaryBytes(
        std::span<const uint8_t>               GlbBytes,
        std::string_view                       DefaultTitle,
        GeometrySpace::ConstructedSampleAsset& OutAsset,
        std::string&                           OutDiagnostic);

    [[nodiscard]] static bool ImportStlBytes(
        std::span<const uint8_t>               StlBytes,
        std::string_view                       DefaultTitle,
        GeometrySpace::ConstructedSampleAsset& OutAsset,
        std::string&                           OutDiagnostic);

    [[nodiscard]] static bool ImportPlyBytes(
        std::span<const uint8_t>               PlyBytes,
        std::string_view                       DefaultTitle,
        GeometrySpace::ConstructedSampleAsset& OutAsset,
        std::string&                           OutDiagnostic);

    [[nodiscard]] static std::string ExportGltf2JsonText(
        const GeometrySpace::PolygonNetwork& Network);
};

} // namespace Frontier::Interchange
