/*============================================================================================================================================
                                                    VERTEXMOTIONATLASEXPORTER.H
============================================================================================================================================*/
// 🧩 Phase D05 Zero-dependency Unreal/Unity VAT interchange exporter (quantized RGBA8 PNG + displacement bounds JSON)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.4

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                VERTEX MOTION ATLAS EXPORTER
//------------------------------------------------------------------------------------------------------------------------

class VertexMotionAtlasExporter
{
public:
    /// Quantizes displacement (worldPos - restPos) into RGBA8 pixels (RGB = normalized [min, max], A = strain/4).
    [[nodiscard]] static std::vector<uint8_t> QuantizeToRgba8(
        const VertexMotionAtlas& atlas,
        const HighPolySurfaceRest& restSurface);

    /// Encodes a valid standard PNG byte stream from raw RGBA8 pixels (pure C++20 stored-DEFLATE zlib + CRC32).
    [[nodiscard]] static std::vector<std::byte> EncodeRgba8Png(
        uint32_t width,
        uint32_t height,
        const std::vector<uint8_t>& rgbaPixels);

    /// Formats the companion Unreal/Unity-compatible JSON metadata describing packing & bounds.
    [[nodiscard]] static std::string FormatBoundsJson(const VertexMotionAtlas& atlas);

    /// Writes both `<stem>.png` and `<stem>.json` to disk.
    [[nodiscard]] static bool ExportToFiles(
        const std::filesystem::path& pngPath,
        const std::filesystem::path& jsonPath,
        const VertexMotionAtlas& atlas,
        const HighPolySurfaceRest& restSurface);
};

} // namespace Frontier::GeometrySpace::Deformation
