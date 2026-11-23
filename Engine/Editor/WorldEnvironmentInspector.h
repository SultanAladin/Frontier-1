/*============================================================================================================================================
                                                        WORLDENVIRONMENTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Editor-only dedicated Environment Inspectors (Atmosphere, Sun, Clouds, Moon, Stars) bound live to WorldAtmosphereConfiguration

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Editor/WorldEnvironmentRegistry.h"

#include <array>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                             WORLD ENVIRONMENT INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class WorldEnvironmentInspector
{
public:
    WorldEnvironmentInspector() noexcept = default;

    // Renders the dedicated Environment Inspector when PrimaryEntry is one of the 5 Environment categories.
    // Returns true if handled (and sets OutMutated = true whenever a live parameter changed).
    [[nodiscard]] bool RenderIfEnvironment(
        PixelSpace&                               Surface,
        Rectangle2D                               Bounds,
        OutlinerPanel*                            Outliner,
        OutlinerRecordEntry*                      PrimaryEntry,
        WorldEnvironmentRegistry&                 Registry,
        GeometricRaster::WorldSceneSpecification& Scene,
        ViewportDisplaySettings*                  DisplaySettings,
        const PaneInputReading&                   Input,
        double                                    Δt,
        bool&                                     OutMutated);

    void ConfigureActivePage(WorldEnvironmentCategory Category, uint32_t PageIndex) noexcept;
    [[nodiscard]] uint32_t QueryActivePage(WorldEnvironmentCategory Category) const noexcept;
    void ResetScroll() noexcept { ScrollOffsetY = 0.0f; }

private:
    [[nodiscard]] float RenderPageCarousel(
        PixelSpace&                   Surface,
        float                         LeftX,
        float                         TopY,
        float                         InnerWidth,
        std::span<const char* const>  PageTitles,
        uint32_t&                     InOutActivePage,
        const PaneInputReading&       Input) noexcept;

    [[nodiscard]] float RenderQuickControlGrid(
        PixelSpace&                                Surface,
        float                                      LeftX,
        float                                      TopY,
        float                                      InnerWidth,
        std::span<const struct QuickTileSpec>      Tiles,
        const PaneInputReading&                    Input,
        int32_t&                                   OutClickedIndex) noexcept;

    [[nodiscard]] float RenderLabeledScalarSlider(
        PixelSpace&             Surface,
        float                   LeftX,
        float                   TopY,
        float                   InnerWidth,
        uint32_t                ControlId,
        const char*             Label,
        const char*             UnitText,
        const char*             FormatSpec,
        float                   MinVal,
        float                   MaxVal,
        float&                  InOutValue,
        const PaneInputReading& Input,
        bool&                   OutChanged) noexcept;

    [[nodiscard]] float RenderPresetChipStrip(
        PixelSpace&                  Surface,
        float                        LeftX,
        float                        TopY,
        float                        InnerWidth,
        const char*                  SectionTitle,
        std::span<const char* const> Labels,
        uint32_t                     ActiveIndex,
        const PaneInputReading&      Input,
        int32_t&                     OutChosenIndex) noexcept;

    float RenderAtmospherePages(
        PixelSpace&                               Surface,
        float                                     LeftX,
        float                                     TopY,
        float                                     InnerWidth,
        OutlinerRecordEntry&                      Entry,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading&                   Input,
        bool&                                     OutMutated);

    float RenderSunPages(
        PixelSpace&                               Surface,
        float                                     LeftX,
        float                                     TopY,
        float                                     InnerWidth,
        OutlinerRecordEntry&                      Entry,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading&                   Input,
        bool&                                     OutMutated);

    float RenderCloudsPages(
        PixelSpace&                               Surface,
        float                                     LeftX,
        float                                     TopY,
        float                                     InnerWidth,
        OutlinerRecordEntry&                      Entry,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading&                   Input,
        bool&                                     OutMutated);

    float RenderMoonPages(
        PixelSpace&                               Surface,
        float                                     LeftX,
        float                                     TopY,
        float                                     InnerWidth,
        OutlinerRecordEntry&                      Entry,
        uint32_t                                  MoonSlotIndex,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading&                   Input,
        bool&                                     OutMutated);

    float RenderStarsPages(
        PixelSpace&                               Surface,
        float                                     LeftX,
        float                                     TopY,
        float                                     InnerWidth,
        OutlinerRecordEntry&                      Entry,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading&                   Input,
        bool&                                     OutMutated);

    std::array<uint32_t, 5u> ActivePageByCategory { 0u, 0u, 0u, 0u, 0u }; // [idx] - Current page per environment category
    OutlinerSlotToken        LastInspectedToken   {};                     // [id]  - Detects selection switches for scroll reset
    uint32_t                 ActiveDragControlId  = 0u;                   // [id]  - Currently dragged slider control identifier
    float                    ScrollOffsetY        = 0.0f;                 // [px]  - Vertical scroll position
    float                    LastContentHeight    = 0.0f;                 // [px]  - Measured scrollable height
};

} // namespace Frontier
