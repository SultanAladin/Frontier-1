/*============================================================================================================================================
                                                            MATERIALSURFACELAYER.H
============================================================================================================================================*/
// 🧩 Ordered 8-layer physical surface stack (substrate, metallic flakes, pigment coat, dust/sheen, moisture film)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Frontier::MaterialSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::size_t MaxMaterialSurfaceLayers = 8u; // [count] - Ordered physical surface layer ceiling

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class MaterialLayerBlendCategory : uint8_t
{
    CoverageOver          = 0, // [-] - Standard linear coverage mask alpha-over blend
    HeightThresholdBlend  = 1, // [-] - Micro-height relief cavity/crest blend (e.g. dust or moss in crevices)
    OpticalClearcoatOver  = 2, // [-] - Dielectric varnish/lacquer opticalmedium layered over substrate
    MoisturePorosityFilm  = 3, // [-] - Adsorbed water film darkening porous substrate + smooth F0=0.02 film
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct MaterialSurfaceLayer
{
    SurfaceMaterial        LayerMaterial          {};                                      // [-] - Optical properties of this layer
    MaterialLayerBlendCategory BlendKind              = MaterialLayerBlendCategory::CoverageOver;  // [-] - Physical vertical composition operator
    float                  CoverageOpacity        = 1.0f;                                  // [-] - Layer presence [0, 1]
    float                  HeightContrastSharpness= 0.50f;                                 // [-] - Relief transition sharpness [0.01, 1]
    float                  SampledMicroHeight     = 0.50f;                                 // [-] - Local surface height sample [0, 1]
    bool                   LayerEnabled           = true;                                  // [-] - Active toggle in authoring stack
};

struct MaterialSurfaceStack
{
    std::array<MaterialSurfaceLayer, MaxMaterialSurfaceLayers> Layers {};                  // [-]     - Bottom-to-top physical layers
    uint32_t                                                   ActiveLayerCount = 0u;      // [count] - Populated layer count [0..8]

    bool AppendLayer(const MaterialSurfaceLayer& NewLayer) noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Flatten an ordered physical surface layer stack (up to 8 layers) into a single energy-conserving OpenPBR SurfaceMaterial
[[nodiscard]] SurfaceMaterial FlattenMaterialSurfaceStack(std::span<const MaterialSurfaceLayer> OrderedLayers);

[[nodiscard]] SurfaceMaterial FlattenMaterialSurfaceStack(const MaterialSurfaceStack& Stack);

} // namespace Frontier::MaterialSpace
