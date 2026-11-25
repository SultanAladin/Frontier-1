/*==========================================================================================================================================
                                                     TEXTUREPAINTLAYERSTACKPANE.H
==========================================================================================================================================*/
// 🗂️ Left-docked Texture Paint Layer Outliner: Add Layer sheet, stat tiles, search/filter, mesh parts, layer thumbnails, gradient points & footer bar.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Texturing/TexturePaintSession.h"

#include <cstdint>

namespace Frontier::Workspaces
{

class TexturePaintLayerStackPane
{
public:
    TexturePaintLayerStackPane() noexcept = default;
    ~TexturePaintLayerStackPane() noexcept;

    void AttachSession(TexturePaintSession* TargetSession) noexcept;
    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor);

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void ConfigureAddLayerSheetOpen(bool Open) noexcept { AddLayerSheetOpen = Open; }
    [[nodiscard]] bool QueryAddLayerSheetOpen() const noexcept { return AddLayerSheetOpen; }

    void ConfigureFooterAddSheetOpen(bool Open) noexcept { FooterAddSheetOpen = Open; }
    [[nodiscard]] bool QueryFooterAddSheetOpen() const noexcept { return FooterAddSheetOpen; }

    void AssignScrollOffsetY(float OffsetY) noexcept { ScrollOffsetY = OffsetY; }

private:
    TexturePaintSession* Session            = nullptr;
    bool                 AddLayerSheetOpen  = false;
    bool                 FooterAddSheetOpen = false;
    bool                 FilterSheetOpen    = false;
    float                ScrollOffsetY      = 0.0f;
    float                MaxScrollY         = 0.0f;
    uint32_t             RenamingLayerId    = 0u;
    TextEditBuffer       SearchBuffer       {};
    TextEditBuffer       RenameBuffer       {};
};

} // namespace Frontier::Workspaces
