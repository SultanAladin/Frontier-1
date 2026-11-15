/*============================================================================================================================================
                                                         PATTERNSTAMPLAYERSTACKPANE.H
============================================================================================================================================*/
// 🧩 Left-docked Layer Stack pane shared by Pattern Studio and Stamp Studio (filters, search, nested tree, mask sub-rows, footer metrics).

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

class PatternStampLayerStackPane
{
public:
    ~PatternStampLayerStackPane() noexcept;

    void AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

public:
    bool  AddLayerSheetOpen = false;
    float ScrollOffsetY     = 0.0f;

private:
    PatternStampWorkspaceSession* Session             = nullptr;
    TextEditBuffer           SearchBuffer        {};
    TextEditBuffer           InlineRenameBuffer  {};
    uint32_t                 RenamingLayerId     = 0u;
    uint32_t                 LastClickLayerId    = 0u;
    double                   TimeSinceLastClick  = 10.0;
    float                    CachedContentHeight = 0.0f;

    // Inline drag reorder & opacity scrub tracking
    uint32_t DraggingLayerId    = 0u;
    bool     DraggingReorder    = false;
    float    DragStartCursorY   = 0.0f;
    uint32_t ScrubbingOpacityId = 0u;
    bool     ScrubbingOpacity   = false;
    float    ScrubStartCursorX  = 0.0f;
    float    ScrubStartOpacity  = 1.0f;
};

} // namespace Frontier::Workspaces
