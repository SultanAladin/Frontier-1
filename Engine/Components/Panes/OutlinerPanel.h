/*============================================================================================================================================
                                                               OUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Generic Workspace Outliner and LayerStack tree pane with Showcase folders, search, filters, drag-reorder, and slot directives

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class OutlinerTitleVariant : uint8_t
{
    Outliner   = 0, // [-] - Spatial scene and entry Outliner heading
    LayerStack = 1, // [-] - Ordered compositing LayerStack heading
};

enum class OutlinerVisibilityFilter : uint8_t
{
    All         = 0, // [-] - Show both visible and hidden entries
    VisibleOnly = 1, // [-] - Show only visible entries
    HiddenOnly  = 2, // [-] - Show only hidden entries
};

enum class OutlinerCategoryFilter : uint8_t
{
    All         = 0, // [-] - Show every slot category
    FoldersOnly = 1, // [-] - Show only folder containers
    FirstType   = 2, // [-] - Show ConstructableFirst entries
    SecondType  = 3, // [-] - Show ConstructableSecond entries
    ThirdType   = 4, // [-] - Show ConstructableThird entries
    FourthType  = 5, // [-] - Show ConstructableFourth entries
    WithNotes   = 6, // [-] - Show entries carrying authored notes
};

enum class DropRelativePlacement : uint8_t
{
    BeforeTarget = 0, // [-] - Reorder immediately before target under target's enclosure
    InsideTarget = 1, // [-] - Attach nested inside target container
    AfterTarget  = 2, // [-] - Reorder immediately after target under target's enclosure
};

enum class OutlinerSettingsDirective : uint8_t
{
    None            = 0, // [-] - No settings directive invoked yet
    ImportEntries   = 1, // [-] - Generic Import placeholder invoked
    ExportSelection = 2, // [-] - Generic Export Selection placeholder invoked
    ExpandAll       = 3, // [-] - Expand every folder and nested branch
    CollapseAll     = 4, // [-] - Collapse every folder and nested branch
    ShowAllHidden   = 5, // [-] - Restore visibility across all entries
    ClearWorkspace  = 6, // [-] - Remove all constructed entries
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OutlinerSlotToken
{
    uint32_t Identifier = 0u; // [id] - Non-zero monotonic slot identifier (0 = root/none)

    [[nodiscard]] constexpr bool Valid() const noexcept { return Identifier != 0u; }
    [[nodiscard]] constexpr bool operator==(const OutlinerSlotToken&) const noexcept = default;
};

struct OutlinerConstructTemplate
{
    std::string          DropdownLabel {};                                           // [-] - Construct sheet label (e.g. "Add Item 1")
    std::string          DefaultTitle  {};                                           // [-] - Initial name assigned to created entry
    std::string          CategoryTitle {};                                           // [-] - Inspector breadcrumb category label
    std::string          DefaultDetail {};                                           // [-] - Secondary row summary caption
    ConstructSlotCategory Category      = ConstructSlotCategory::CategoryOne;   // [-] - Visual icon and filter category
};

struct OutlinerRecordEntry
{
    OutlinerSlotToken    Token             {};                                         // [id] - Unique slot token
    OutlinerSlotToken    EnclosingToken    {};                                         // [id] - Enclosing slot token (0 = root)
    ConstructSlotCategory Category          = ConstructSlotCategory::CategoryOne; // [-]  - Slot visual category
    std::string          Title             {};                                         // [-]  - Display name in tree and inspector
    std::string          CategoryLabel     {};                                         // [-]  - Inspector domain category label
    std::string          Subtitle          {};                                         // [-]  - Secondary caption below title
    std::string          AuthorNotes       {};                                         // [-]  - Optional notes edited in Inspector
    ColorRgba            FolderTint        = WorkspaceSurfaceColors::ShowcaseSage;   // [-]  - Showcase folder accent color
    float                PositionX         = 0.0f;                                     // [m]   - Local X translation
    float                PositionY         = 0.0f;                                     // [m]   - Local Y translation
    float                PositionZ         = 0.0f;                                     // [m]   - Local Z translation
    float                RotationX         = 0.0f;                                     // [deg] - Local X Euler rotation
    float                RotationY         = 0.0f;                                     // [deg] - Local Y Euler rotation
    float                RotationZ         = 0.0f;                                     // [deg] - Local Z Euler rotation
    float                ScaleX            = 1.0f;                                     // [-]   - Local X scale factor
    float                ScaleY            = 1.0f;                                     // [-]   - Local Y scale factor
    float                ScaleZ            = 1.0f;                                     // [-]   - Local Z scale factor
    bool                 VisibilityEnabled = true;                                     // [-]  - Direct visibility switch
    bool                 Expanded          = true;                                     // [-]  - Disclosure branch open indicator
    bool                 Locked            = false;                                    // [-]  - Transform edit lock switch
    bool                 DynamicMotion     = false;                                    // [-]  - Dynamic simulation switch
    bool                 CastShadows       = true;                                     // [-]  - Shadow caster capability switch
};

struct OutlinerVisibleRow
{
    OutlinerSlotToken Token            {};    // [id]    - Displayed slot token
    uint32_t          DepthLevel       = 0u;  // [count] - Nesting depth from root
    uint32_t          NestedEntryCount = 0u;  // [count] - Total nested entries under this slot
    bool              NestedBranchPresent = false; // [-]   - Whether disclosure chevron is shown
    bool              EffectiveVisible = true;  // [-]   - Combined self & enclosing visibility
};

struct OutlinerFooterReadings
{
    std::string RealtimeText = "-- fps";   // [-] - First footer slot (rendered in StatusGreen)
    std::string QualityText  = "Standard"; // [-] - Second footer slot
    std::string PrimarySlot  = "--";       // [-] - Third footer slot (SUN)
    std::string SecondarySlot = "--";      // [-] - Fourth footer slot (MOONS)
    std::string CameraText   = "0, 2, 0";  // [-] - Fifth footer slot (CAM)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class OutlinerPanel
{
public:
    OutlinerPanel() noexcept;
    ~OutlinerPanel() noexcept;

    void ConfigureTitleVariant(OutlinerTitleVariant Variant) noexcept;
    void ConfigureConstructTemplates(std::span<const OutlinerConstructTemplate> Templates);
    void ConfigureFooterReadings(const OutlinerFooterReadings& Readings);

    OutlinerSlotToken ConstructFromTemplate(uint32_t TemplateIndex, OutlinerSlotToken ExplicitEnclosure = {});
    OutlinerSlotToken ConstructFolder(std::string_view FolderTitle = "Folder", OutlinerSlotToken ExplicitEnclosure = {});
    bool              DeleteSlot(OutlinerSlotToken Token) noexcept;
    uint32_t          DeleteSelection() noexcept;
    OutlinerSlotToken DuplicateSlot(OutlinerSlotToken Token);
    OutlinerSlotToken GroupSelectionIntoFolder(std::string_view FolderTitle = "Folder");
    void              ClearAll() noexcept;

    bool AttachToEnclosure(OutlinerSlotToken Token, OutlinerSlotToken NewEnclosure) noexcept;
    bool RepositionRelativeToTarget(
        OutlinerSlotToken     MovingToken,
        OutlinerSlotToken     TargetToken,
        DropRelativePlacement Placement) noexcept;

    void SelectSingle(OutlinerSlotToken Token) noexcept;
    void SelectWithModifiers(OutlinerSlotToken Token, bool ShiftHeld, bool ControlHeld) noexcept;
    void ClearSelection() noexcept;

    void ConfigureSearchQuery(std::string_view Query) noexcept;
    void ConfigureVisibilityFilter(OutlinerVisibilityFilter Filter) noexcept;
    void ConfigureCategoryFilter(OutlinerCategoryFilter Filter) noexcept;
    void ConfigureFilterStripOpen(bool Open) noexcept;
    void ConfigureScrollOffset(float OffsetY) noexcept;

    void OpenConstructDropdown() noexcept;
    void OpenHeaderSettingsDropdown() noexcept;
    void OpenDirectiveSheetForSlot(OutlinerSlotToken Token, Point2D AnchorPosition) noexcept;
    void CloseOverlays() noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt,
        PixelSpace*             OverlaySurface = nullptr);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

    [[nodiscard]] OutlinerTitleVariant                     QueryTitleVariant() const noexcept;
    [[nodiscard]] const char*                              QueryHeadingText() const noexcept;
    [[nodiscard]] std::span<const OutlinerRecordEntry>     QueryEntries() const noexcept;
    [[nodiscard]] std::span<const OutlinerConstructTemplate> QueryConstructTemplates() const noexcept;
    [[nodiscard]] const OutlinerRecordEntry*               FindEntry(OutlinerSlotToken Token) const noexcept;
    [[nodiscard]] OutlinerRecordEntry*                     AccessEntry(OutlinerSlotToken Token) noexcept;
    [[nodiscard]] std::span<const OutlinerSlotToken>       QuerySelectedTokens() const noexcept;
    [[nodiscard]] OutlinerSlotToken                        QueryPrimarySelection() const noexcept;
    [[nodiscard]] bool                                     QuerySlotSelected(OutlinerSlotToken Token) const noexcept;
    [[nodiscard]] bool                                     QueryEffectiveVisibility(OutlinerSlotToken Token) const noexcept;
    [[nodiscard]] uint32_t                                 QueryTotalCount() const noexcept;
    [[nodiscard]] uint32_t                                 QueryVisibleCount() const noexcept;
    [[nodiscard]] uint32_t                                 QueryHiddenCount() const noexcept;
    [[nodiscard]] uint32_t                                 CountDirectNested(OutlinerSlotToken Enclosure) const noexcept;
    [[nodiscard]] uint32_t                                 CountAllNested(OutlinerSlotToken Enclosure) const noexcept;
    [[nodiscard]] uint32_t                                 CountNestedFolders(OutlinerSlotToken Enclosure) const noexcept;
    [[nodiscard]] float                                    QueryScrollOffset() const noexcept;
    [[nodiscard]] OutlinerSettingsDirective                QueryLastSettingsDirective() const noexcept;
    [[nodiscard]] bool                                     QueryConstructDropdownOpen() const noexcept;
    [[nodiscard]] bool                                     QuerySettingsDropdownOpen() const noexcept;
    [[nodiscard]] bool                                     QueryDirectiveSheetOpen() const noexcept;

    void BuildVisibleRows(std::vector<OutlinerVisibleRow>& OutRows) const;

private:
    [[nodiscard]] bool              ContainsNestedSlot(OutlinerSlotToken PotentialEnclosure, OutlinerSlotToken Candidate) const noexcept;
    [[nodiscard]] bool              MatchesActiveFilters(const OutlinerRecordEntry& Entry) const noexcept;
    [[nodiscard]] bool              SubtreeMatchesFilters(OutlinerSlotToken Token) const noexcept;
    void                            AppendVisibleSubtree(
        OutlinerSlotToken                Enclosure,
        uint32_t                         Depth,
        bool                             EnclosingVisible,
        std::vector<OutlinerVisibleRow>& OutRows) const;
    [[nodiscard]] std::string       GenerateUniqueTitle(std::string_view StemTitle) const;
    void                            ExecuteSettingsDirective(OutlinerSettingsDirective Directive);

    OutlinerTitleVariant                   TitleVariant            = OutlinerTitleVariant::Outliner;      // [-]   - Heading label selector
    std::vector<OutlinerConstructTemplate> ConstructTemplates      {};                                    // [-]   - "+" dropdown construct presets
    std::vector<OutlinerRecordEntry>       Entries                 {};                                    // [-]   - Ordered slot records (empty initial)
    std::vector<OutlinerSlotToken>         SelectedTokens          {};                                    // [id]  - Active multi-selection set
    OutlinerSlotToken                      SelectionAnchor         {};                                    // [id]  - Shift-range anchor slot
    uint32_t                               NextTokenCounter        = 1u;                                  // [id]  - Monotonic token generator

    TextEditBuffer                         SearchEditor            {};                                    // [-]   - Live search text buffer
    OutlinerVisibilityFilter               VisibilityFilter        = OutlinerVisibilityFilter::All;       // [-]   - Active Visible/Hidden filter
    OutlinerCategoryFilter                 CategoryFilter          = OutlinerCategoryFilter::All;         // [-]   - Active category chip filter
    bool                                   FilterStripOpen         = false;                               // [-]   - Filter chip strip visibility
    float                                  ScrollOffsetY           = 0.0f;                                // [px]  - Vertical tree scroll offset

    bool                                   ConstructDropdownOpen   = false;                               // [-]   - "+" construct dropdown open
    Point2D                                ConstructDropdownAnchor {};                                    // [px]  - Construct dropdown top-right
    bool                                   SettingsDropdownOpen    = false;                               // [-]   - Header settings dropdown open
    Point2D                                SettingsDropdownAnchor  {};                                    // [px]  - Settings dropdown top-right
    bool                                   DirectiveSheetOpen        = false;                               // [-]   - Right-click directive sheet open
    OutlinerSlotToken                      DirectiveTargetToken      {};                                    // [id]  - Slot targeted by directive sheet
    Point2D                                DirectiveSheetAnchor      {};                                    // [px]  - Directive sheet origin

    OutlinerSlotToken                      PressedRowToken         {};                                    // [id]  - Row pressed on pointer down
    Point2D                                PressOrigin             {};                                    // [px]  - Pointer press coordinate
    bool                                   DragActive              = false;                               // [-]   - Tree drag-reposition active
    OutlinerSlotToken                      HoverDropTarget         {};                                    // [id]  - Hovered drop destination slot
    DropRelativePlacement                  HoverDropPlacement      = DropRelativePlacement::InsideTarget; // [-]   - Hovered drop placement zone

    OutlinerFooterReadings                 FooterReadings          {};                                    // [-]   - Bottom 5-slot telemetry strings
    OutlinerSettingsDirective              LastSettingsDirective   = OutlinerSettingsDirective::None;     // [-]   - Most recent settings selection
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline OutlinerTitleVariant OutlinerPanel::QueryTitleVariant() const noexcept
{
    return TitleVariant;
}

inline std::span<const OutlinerRecordEntry> OutlinerPanel::QueryEntries() const noexcept
{
    return Entries;
}

inline std::span<const OutlinerConstructTemplate> OutlinerPanel::QueryConstructTemplates() const noexcept
{
    return ConstructTemplates;
}

inline std::span<const OutlinerSlotToken> OutlinerPanel::QuerySelectedTokens() const noexcept
{
    return SelectedTokens;
}

inline uint32_t OutlinerPanel::QueryTotalCount() const noexcept
{
    return static_cast<uint32_t>(Entries.size());
}

inline float OutlinerPanel::QueryScrollOffset() const noexcept
{
    return ScrollOffsetY;
}

inline OutlinerSettingsDirective OutlinerPanel::QueryLastSettingsDirective() const noexcept
{
    return LastSettingsDirective;
}

inline bool OutlinerPanel::QueryConstructDropdownOpen() const noexcept
{
    return ConstructDropdownOpen;
}

inline bool OutlinerPanel::QuerySettingsDropdownOpen() const noexcept
{
    return SettingsDropdownOpen;
}

inline bool OutlinerPanel::QueryDirectiveSheetOpen() const noexcept
{
    return DirectiveSheetOpen;
}

} // namespace Frontier
