/*============================================================================================================================================
                                                        COMPONENTSPECIMENBAKER.H
============================================================================================================================================*/
// 🧩 Offscreen 128x128 transparent OpenPbrDirect studio thumbnail baker for ComponentClass & compiled geometry assets

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"
#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.h"

#include <cstdint>
#include <span>

namespace Frontier::GeometricRaster::ComponentEditor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ComponentSpecimenBaker
{
public:
    [[nodiscard]] static bool BakeClassThumbnail(
        ComponentViewportRenderer&                                    Renderer,
        ComponentClass::ComponentClassDefinition&                     InOutClassDef,
        std::span<const ComponentClass::CompiledGeometryCatalogEntry> Catalog,
        uint32_t                                                      ResolutionPixels = 128u);

    [[nodiscard]] static bool BakeGeometryAssetThumbnail(
        ComponentViewportRenderer&                          Renderer,
        const ComponentClass::CompiledGeometryCatalogEntry& Entry,
        std::vector<uint8_t>&                               OutRgba8,
        uint32_t                                            ResolutionPixels = 128u);
};

} // namespace Frontier::GeometricRaster::ComponentEditor
