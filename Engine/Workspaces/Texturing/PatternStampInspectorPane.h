/*============================================================================================================================================
                                                         PATTERNSTAMPINSPECTORPANE.H
============================================================================================================================================*/
// 🧩 Right-docked Inspector pane shared by Pattern Studio and Stamp Studio: KPI trio, Layer Properties, 22 PBR Surface Channels,
//    Layer Mask, 8 Non-Destructive Modifiers, Document Ground & Tile, fitted dropdown sheets, and 16-swatch colour picker.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Workspaces/Texturing/PatternStampStackSession.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <cstdint>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLASS DECLARATION
//------------------------------------------------------------------------------------------------------------------------

class PatternStampInspectorPane
{
public:
    ~PatternStampInspectorPane() noexcept;

    void AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

public:
    float ScrollOffsetY = 0.0f;

private:
    enum class InspectorSheetVariant : uint8_t
    {
        None = 0,
        MotifShape,
        BlendMode,
        SurfaceFinish,
        AddChannel,
        ModifierNoiseAlgo,
        ModifierModTarget,
    };

    enum class SwatchTargetChannel : uint8_t
    {
        None = 0,
        LayerFill,
        LayerStroke,
        GroundTint,
        PbrChannel,
        ModifierTintB,
    };

    void CloseAllSheets() noexcept;

    PatternStampWorkspaceSession* Session                  = nullptr;

    // Collapsible sections
    bool LayerPropsExpanded     = true;
    bool PbrChannelsExpanded    = true;
    bool LayerMaskExpanded      = false;
    bool ModifiersExpanded      = true;
    bool GroundSectionExpanded  = false;

    // Active sheets / pickers / buffers
    InspectorSheetVariant       ActiveSheet              = InspectorSheetVariant::None;
    std::size_t              ActiveSheetModIdx        = 0u;
    Rectangle2D              ActiveSheetAnchor        {};
    float                    DropdownScrollY          = 0.0f;

    SwatchTargetChannel      ActiveSwatchTarget       = SwatchTargetChannel::None;
    PatternStampSurfaceChannelKey ActiveSwatchChannelKey   = PatternStampSurfaceChannelKey::Albedo;
    std::size_t              ActiveSwatchModIdx       = 0u;
    Rectangle2D              ActiveSwatchAnchor       {};

    TextEditBuffer           LayerNameBuffer          {};
    TextEditBuffer           TypographicBuffer        {};
    uint32_t                 SyncedNameLayerId        = 0u;
    uint32_t                 SyncedTextLayerId        = 0u;

    int32_t                  ActiveSliderId           = -1;
    float                    CachedContentHeight      = 0.0f;
};

} // namespace Frontier::Workspaces
