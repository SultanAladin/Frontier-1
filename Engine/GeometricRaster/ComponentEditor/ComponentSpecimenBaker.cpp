/*============================================================================================================================================
                                                       COMPONENTSPECIMENBAKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of offscreen transparent OpenPbrDirect studio thumbnail baker for ComponentClass & geometry assets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/ComponentEditor/ComponentSpecimenBaker.h"

#include <array>

namespace Frontier::GeometricRaster::ComponentEditor
{

bool ComponentSpecimenBaker::BakeClassThumbnail(
    ComponentViewportRenderer&                                    Renderer,
    ComponentClass::ComponentClassDefinition&                     InOutClassDef,
    std::span<const ComponentClass::CompiledGeometryCatalogEntry> Catalog,
    uint32_t                                                      ResolutionPixels)
{
    if (!Renderer.QueryVulkanReady() || ResolutionPixels == 0u)
    {
        return false;
    }

    ComponentViewportCamera Cam = Renderer.ComputeFramedCameraForClass(InOutClassDef, Catalog, 0.62f, 0.28f);
    Cam.DistanceMeters *= 0.92f;

    ComponentRenderOptions Opts {};
    Opts.Channel             = ComponentInspectionChannel::Lit;
    Opts.ShowDotFloor        = false;
    Opts.ShowDebugHelpers    = false;
    Opts.TransparentBackdrop = true;

    const ComponentViewportFrameOutput Out = Renderer.RenderClassFrame(
        InOutClassDef,
        Catalog,
        Cam,
        Opts,
        ResolutionPixels,
        ResolutionPixels,
        true);

    if (Out.DisplayRgba8.size() != static_cast<std::size_t>(ResolutionPixels) * ResolutionPixels * 4u)
    {
        return false;
    }

    InOutClassDef.BakedIconWidth  = ResolutionPixels;
    InOutClassDef.BakedIconHeight = ResolutionPixels;
    InOutClassDef.BakedIconRgba8  = Out.DisplayRgba8;
    return true;
}

bool ComponentSpecimenBaker::BakeGeometryAssetThumbnail(
    ComponentViewportRenderer&                          Renderer,
    const ComponentClass::CompiledGeometryCatalogEntry& Entry,
    std::vector<uint8_t>&                               OutRgba8,
    uint32_t                                            ResolutionPixels)
{
    if (!Renderer.QueryVulkanReady() || ResolutionPixels == 0u)
    {
        return false;
    }

    ComponentClass::ComponentClassDefinition TempClass {};
    TempClass.Title = Entry.DisplayTitle;
    TempClass.Storage.ResetWithSpatialOrigin("Origin");
    const auto Root = TempClass.Storage.QueryOriginToken();
    const auto Tok  = TempClass.Storage.AppendInstance(
        ComponentClass::ComponentCategory::GeometryInstance,
        Entry.DisplayTitle,
        Root);
    if (!Tok.Valid())
    {
        return false;
    }
    if (auto* Geom = TempClass.Storage.ResolveGeometry(Tok))
    {
        Geom->AssetIdentifier = Entry.AssetIdentifier;
    }

    const std::array<ComponentClass::CompiledGeometryCatalogEntry, 1> SingleCatalog {{ Entry }};
    if (!BakeClassThumbnail(Renderer, TempClass, SingleCatalog, ResolutionPixels))
    {
        return false;
    }
    OutRgba8 = std::move(TempClass.BakedIconRgba8);
    return true;
}

} // namespace Frontier::GeometricRaster::ComponentEditor
