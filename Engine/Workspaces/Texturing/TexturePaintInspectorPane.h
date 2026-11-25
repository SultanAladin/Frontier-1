/*==========================================================================================================================================
                                                    TEXTUREPAINTINSPECTORPANE.H
==========================================================================================================================================*/
// 🎛️ Right-docked Layer Inspector: identity block, 3 metric tiles, Layer properties, Gradient points, 5-group ChannelStack & Mask controls.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Texturing/TexturePaintSession.h"

#include <cstdint>
#include <functional>

namespace Frontier::Workspaces
{

class TexturePaintInspectorPane
{
public:
    TexturePaintInspectorPane() noexcept = default;
    ~TexturePaintInspectorPane() noexcept;

    void AttachSession(TexturePaintSession* TargetSession) noexcept;
    void AssignOpenBrushStudioCallback(std::function<void()> Callback) noexcept { OpenBrushStudioCallback = std::move(Callback); }
    void SetOpenBrushStudioCallback(std::function<void()> Callback) noexcept { OpenBrushStudioCallback = std::move(Callback); }
    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor);

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void AssignScrollOffsetY(float OffsetY) noexcept { ScrollOffsetY = OffsetY; }
    [[nodiscard]] float QueryScrollOffsetY() const noexcept { return ScrollOffsetY; }

private:
    TexturePaintSession*  Session                 = nullptr;
    std::function<void()> OpenBrushStudioCallback {};
    float                 ScrollOffsetY           = 0.0f;
    float                 MaxScrollY              = 0.0f;
    bool                  KindSectionOpen         = false;
    bool                  LayerSwitcherOpen       = false;
    bool                  BlendDropdownOpen       = false;
    bool                  PartDropdownOpen        = false;
    bool                  MaskGenDropdownOpen     = false;
    TextEditBuffer        LayerNameBuffer         {};
    TextEditBuffer        ChannelSearchBuffer     {};
    TextEditBuffer        StampTextBuffer         {};
};

} // namespace Frontier::Workspaces
