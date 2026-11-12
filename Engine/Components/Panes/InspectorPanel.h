/*============================================================================================================================================
                                                               INSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Workspace Inspector pane featuring Showcase Folder Inspector, Add Notes editor, header typography, and capability tiles

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"

#include <cstdint>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class FolderBrowseScope : uint8_t
{
    NestedAll  = 0, // [-] - Include all nested levels inside the folder
    DirectOnly = 1, // [-] - Include only direct nested entries
};

enum class FolderBrowseSort : uint8_t
{
    OutlinerOrder = 0, // [-] - Preserve Outliner order
    Alphabetical  = 1, // [-] - Sort entries alphabetically by title
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class InspectorPanel
{
public:
    InspectorPanel() noexcept = default;

    void AttachOutliner(OutlinerPanel* SourceOutliner) noexcept;
    void ConfigureNotesBoxOpen(bool Open) noexcept;
    void AssignActiveNotes(std::string_view NotesText);
    void ConfigureScrollOffset(float OffsetY) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

    [[nodiscard]] OutlinerPanel* QueryAttachedOutliner() const noexcept;
    [[nodiscard]] bool           QueryNotesBoxOpen() const noexcept;
    [[nodiscard]] float          QueryScrollOffset() const noexcept;

private:
    void SyncNotesBufferWithSelection(OutlinerSlotToken ActiveToken, const OutlinerRecordEntry* ActiveEntry) noexcept;
    float RenderNotesControl(
        PixelSpace&             Surface,
        float                   LeftX,
        float                   TopY,
        float                   AvailableWidth,
        bool                    RightAnchoredButton,
        float                   ButtonRightX,
        float                   ButtonTopY,
        OutlinerRecordEntry&    Entry,
        const PaneInputReading& Input);

    float RenderFolderInspector(
        PixelSpace&             Surface,
        Rectangle2D             ScrollViewport,
        float                   OriginY,
        OutlinerRecordEntry&    FolderEntry,
        const PaneInputReading& Input);

    float RenderStandardEntryInspector(
        PixelSpace&             Surface,
        Rectangle2D             ScrollViewport,
        float                   OriginY,
        OutlinerRecordEntry&    Entry,
        const PaneInputReading& Input);

    OutlinerPanel*           AttachedOutliner       = nullptr;                               // [-]  - Bound Outliner / LayerStack
    OutlinerSlotToken        SyncedNotesToken       {};                                      // [id] - Slot currently loaded in NotesEditor
    TextEditBuffer           NotesEditor            {};                                      // [-]  - Interactive Add Notes text buffer
    bool                     NotesBoxExpanded       = false;                                 // [-]  - Notes editor open indicator
    TextEditBuffer           FolderSearchEditor     {};                                      // [-]  - Showcase folder search buffer
    FolderBrowseScope        BrowseScope            = FolderBrowseScope::NestedAll;          // [-]  - Folder browser scope mode
    FolderBrowseSort         BrowseSort             = FolderBrowseSort::OutlinerOrder;       // [-]  - Folder browser sort mode
    bool                     ScopeDropdownOpen      = false;                                 // [-]  - Scope DropdownSelector open flag
    bool                     SortDropdownOpen       = false;                                 // [-]  - Sort DropdownSelector open flag
    Rectangle2D              ScopeButtonBounds      {};                                      // [px] - Last Scope button bounds
    Rectangle2D              SortButtonBounds       {};                                      // [px] - Last Sort button bounds
    OutlinerVisibilityFilter FolderVisibilityFilter = OutlinerVisibilityFilter::All;         // [-]  - Folder browser visibility filter
    int32_t                  ActiveTransformCell    = -1;                                    // [idx]- Focused Transform cell (0..8 or -1)
    int32_t                  DraggingTransformCell  = -1;                                    // [idx]- Dragged Transform cell (0..8 or -1)
    float                    TransformDragOriginX   = 0.0f;                                  // [px] - Pointer X at drag start
    float                    TransformDragStartVal  = 0.0f;                                  // [-]  - Value snapshot at drag start
    bool                     TransformDragMoved     = false;                                 // [-]  - Drag distance exceeded 4px threshold
    TextEditBuffer           TransformCellEditor    {};                                      // [-]  - Numeric text input for focused cell
    float                    ScrollOffsetY          = 0.0f;                                  // [px] - Vertical inspector scroll offset
    float                    LastContentHeight      = 0.0f;                                  // [px] - Measured scroll document height
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void InspectorPanel::AttachOutliner(OutlinerPanel* SourceOutliner) noexcept
{
    AttachedOutliner = SourceOutliner;
}

inline OutlinerPanel* InspectorPanel::QueryAttachedOutliner() const noexcept
{
    return AttachedOutliner;
}

inline bool InspectorPanel::QueryNotesBoxOpen() const noexcept
{
    return NotesBoxExpanded;
}

inline float InspectorPanel::QueryScrollOffset() const noexcept
{
    return ScrollOffsetY;
}

} // namespace Frontier
