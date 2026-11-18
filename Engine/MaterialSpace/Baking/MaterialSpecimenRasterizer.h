/*============================================================================================================================================
                                                         MATERIALSPECIMENRASTERIZER.H
============================================================================================================================================*/
// 🧩 Calibrated studio shader-ball specimen rasterizer lighting any 10-lobe SurfaceMaterial for ContentBrowser & Exhibits

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <cstdint>
#include <vector>

namespace Frontier::MaterialSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpecimenPreviewImage
{
    uint32_t             Width  = 0u; // [px]    - Thumbnail width
    uint32_t             Height = 0u; // [px]    - Thumbnail height
    std::vector<uint8_t> Rgba8  {};   // [RGBA8] - Tone-mapped sRGB RGBA8 pixel buffer (Width * Height * 4 bytes)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Render a calibrated studio shader-ball preview using the exact 10-lobe OpenPBR 1.1 + Glinty BSDF
[[nodiscard]] SpecimenPreviewImage RasterizeMaterialSpecimenPreview(
    const SurfaceMaterial& Material,
    uint32_t               ResolutionPixels      = 96u,
    bool                   TransparentBackground = false);

} // namespace Frontier::MaterialSpace
