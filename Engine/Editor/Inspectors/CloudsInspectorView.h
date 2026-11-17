/*============================================================================================================================================
                                                           CLOUDSINSPECTORVIEW.H
============================================================================================================================================*/
// 🧩 Native C++20 Inspector view for 6-type Volumetric Clouds (vertical altitude deck section, optics, wind, shadows) and Local Cloud Volume

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"

#include <cstdint>

namespace Frontier::Editor::Inspectors
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct CloudsInspectorTargets
{
    GeometricRaster::CloudDeckConfiguration* Deck        = nullptr; // [-] - Mutable planetary volumetric cloud deck
    GeometricRaster::BoundedCloudVolume*     LocalVolume = nullptr; // [-] - Mutable bounded local cloud box
    bool                                     ShowLocalVolumeOnly = false; // [-] - Switches between Global Deck and Local Volume views
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLOUDSINSPECTORVIEW
//------------------------------------------------------------------------------------------------------------------------

class CloudsInspectorView
{
public:
    CloudsInspectorView() noexcept = default;

    void ConfigureScrollOffset(float OffsetY) noexcept;
    [[nodiscard]] float QueryScrollOffset() const noexcept;

    [[nodiscard]] bool Render(
        PixelSpace&                   Surface,
        Rectangle2D                   Bounds,
        const CloudsInspectorTargets& Targets,
        const PaneInputReading&       Input,
        double                        DeltaSeconds);

private:
    bool RenderGlobalDeck(
        PixelSpace&                   Surface,
        Rectangle2D                   Bounds,
        GeometricRaster::CloudDeckConfiguration& Deck,
        const PaneInputReading&       Input);

    bool RenderLocalVolume(
        PixelSpace&                   Surface,
        Rectangle2D                   Bounds,
        GeometricRaster::BoundedCloudVolume& LocalVol,
        const PaneInputReading&       Input);

    float       ScrollOffsetY      = 0.0f;  // [px] - Vertical scroll offset
    float       LastContentH       = 0.0f;  // [px] - Measured layout height
    int32_t     ActiveSliderId     = -1;    // [id] - Active dragging scalar slider tag
    int32_t     ActiveAxisDragId   = -1;    // [id] - Active XYZ axis box drag tag
    float       AxisDragStartX     = 0.0f;  // [px] - Cursor X at axis drag start
    float       AxisDragOrigin     = 0.0f;  // [m]  - Initial value at axis drag start
    bool        CloudTypeSheetOpen = false; // [-]  - Cloud category dropdown sheet open indicator
    bool        DraggingDeckDatum  = false; // [-]  - Vertical deck section drag indicator
    float       DeckDragStartY     = 0.0f;  // [px] - Cursor Y at deck drag start
    float       DeckOriginFloorM   = 1400.0f;//[m]  - Initial cloud floor at drag start
    Rectangle2D CloudTypeTrigger   {};      // [px] - Trigger bounds for cloud category sheet
};

inline void CloudsInspectorView::ConfigureScrollOffset(float OffsetY) noexcept
{
    ScrollOffsetY = OffsetY;
}

inline float CloudsInspectorView::QueryScrollOffset() const noexcept
{
    return ScrollOffsetY;
}

} // namespace Frontier::Editor::Inspectors
