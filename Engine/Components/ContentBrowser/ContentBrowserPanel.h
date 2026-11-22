/*============================================================================================================================================
                                                            CONTENTBROWSERPANEL.H
============================================================================================================================================*/
// 🧩 Bottom-edge ContentBrowser drawer with pre-baked ThumbnailSheet, dual-path SSD/HDD streaming, strict workspace masks & ColourSwatchBrowserView

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/ContentBrowser/ColourSwatchBrowserView.h"
#include "Engine/Components/ContentBrowser/ThumbnailSheetRepository.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Motion/DrawerDetentGesture.h"
#include "Engine/Components/Motion/SpringIntegrator.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Threading/Threads/Streaming/AssetStreamThread.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class StandardRepositoryLayout : uint8_t
{
    Grid = 0, // [-] - Multi-column pre-baked ThumbnailSheet grid (non-Colours categories)
    List = 1, // [-] - Compact single-column pre-baked ThumbnailSheet list (non-Colours categories)
};

enum class ContentShelfExtent : uint8_t
{
    Closed     = 0, // [-] - Collapsed to bottom-edge trapezoidal notch
    SplitHalf  = 1, // [-] - Docked to bottom 48% shelf so workspace colour slots stay live
    FullHeight = 2, // [-] - Expanded to full viewport height
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColourSlotRegistration
{
    std::string                                                       SlotIdentifier         {};        // [-]  - Stable colour-slot key
    std::string                                                       DisplayTitle           {};        // [-]  - Human-readable slot label
    Rectangle2D                                                       ScreenBounds           {};        // [px] - Logical drop-target rectangle
    ColorRgba*                                                        TargetColour           = nullptr; // [-]  - Optional direct ColorRgba destination
    std::function<void(ColorRgba, const ColourSwatchEntry&)>           NotifyColourAssigned   {};        // [-]  - Optional assignment callback
    std::function<void(ColorRgba, const ColourSwatchEntry&, Point2D)>  NotifyColourAssignedAt {};        // [-]  - Optional coordinate-aware drop callback
};

struct ColourRampSlotRegistration
{
    std::string                                                                               SlotIdentifier         {};        // [-]  - Stable colour-ramp drop-target key
    std::string                                                                               DisplayTitle           {};        // [-]  - Human-readable slot title
    Rectangle2D                                                                               ScreenBounds           {};        // [px] - Logical drop-target rectangle
    ColourSpace::ColourSourceProfile*                                                         TargetProfile          = nullptr; // [-]  - Optional direct ColourSourceProfile destination
    std::function<void(const ColourSpace::ColourSourceProfile&, std::string_view)>            NotifyRampAssigned     {};        // [-]  - Optional assignment callback
    std::function<void(const ColourSpace::ColourSourceProfile&, std::string_view, Point2D)>   NotifyRampAssignedAt   {};        // [-]  - Optional coordinate-aware drop callback
};

struct MatCapSlotRegistration
{
    std::string                                                 SlotIdentifier       {}; // [-]  - Stable MatCap drop-target key
    std::string                                                 DisplayTitle         {}; // [-]  - Human-readable target label
    Rectangle2D                                                 ScreenBounds         {}; // [px] - Logical drop-target rectangle
    std::function<void(uint8_t, const ContentRepositoryEntry&)> NotifyMatCapAssigned {}; // [-]  - Callback receiving MatCap slot (0..15)
};

struct ContentDropZoneRegistration
{
    std::string                                                        SlotIdentifier       {};                     // [-]  - Stable content drop-target key
    std::string                                                        DisplayTitle         {};                     // [-]  - Human-readable target label
    Rectangle2D                                                        ScreenBounds         {};                     // [px] - Logical drop-target rectangle
    ContentCategory                                                    AcceptedCategory     = ContentCategory::All; // [-]  - Permitted ContentCategory or All
    std::function<void(const ContentRepositoryEntry&, Point2D)>        NotifyContentDropped {};                     // [-]  - Drop callback receiving entry & cursor
};

struct ContentBrowserPointerInput
{
    float CursorX       = -10000.0f; // [px] - Logical pointer X coordinate
    float CursorY       = -10000.0f; // [px] - Logical pointer Y coordinate
    bool  Pressed       = false;     // [-]  - Primary button pressed this step
    bool  HeldDown      = false;     // [-]  - Primary button held down
    bool  Released      = false;     // [-]  - Primary button released this step
    bool  DoubleClicked = false;     // [-]  - Primary button double-clicked this step
    float WheelDeltaY   = 0.0f;      // [-]  - Vertical scroll wheel delta
};

struct ContentBrowserReading
{
    ContentShelfExtent             ShelfExtent            = ContentShelfExtent::Closed;                     // [-]     - Closed, SplitHalf, or FullHeight
    float                          ShelfProgress          = 0.0f;                                           // [0-1]   - Animated spring height ratio
    WorkspaceCategory            ActiveWorkspace        = WorkspaceCategory::SceneWorld;                // [-]     - Active workspace domain filter
    ContentCategory            ActiveCategory         = ContentCategory::Materials;                 // [-]     - Selected left-rail category
    ContentOriginFilter            ActiveOrigin           = ContentOriginFilter::All;                       // [-]     - All, Project, Imported, or Engine
    ColourRepositoryLayout         ActiveColourLayout     = ColourRepositoryLayout::GridCircles;            // [-]     - Colours-only layout mode
    ColourCollectionScope          ActiveColourScope      = ColourCollectionScope::Swatches;                // [-]     - Swatches vs Ramps sub-scope under Colours
    StandardRepositoryLayout       ActiveStandardLayout   = StandardRepositoryLayout::Grid;                 // [-]     - Non-Colours layout mode
    ColourFilterGroup              ActiveColourFilter     = ColourFilterGroup::All;                         // [-]     - Dedicated Colour Filters selection
    ColourSortOrder                ActiveColourSort       = ColourSortOrder::Spectrum;                      // [-]     - Colour sort ordering
    Threading::StorageHardwareTier ActiveStorageTier      = Threading::StorageHardwareTier::FlashStorageRandomIo; // [-] - SSD vs HDD streaming path
    ImportDestinationMode          ActiveImportMode       = ImportDestinationMode::ProjectImportedFolder;   // [-]     - Persistent vs Scratch/Temp vs Custom
    uint32_t                       PermittedCategoryCount = 0u;                                             // [count] - Categories visible in active workspace
    uint32_t                       VisibleEntryCount      = 0u;                                             // [count] - Filtered visible entries
    bool                           ColourFiltersVisible   = false;                                          // [-]     - True only when Colours is active
    bool                           DraggingColourActive   = false;                                          // [-]     - True while dragging a ColourSwatchBrowserView swatch
    bool                           DraggingColourRampActive = false;                                        // [-]     - True while dragging a ColourSpace ramp preset
    bool                           DraggingMatCapActive   = false;                                          // [-]     - True while dragging a Studio MatCap sphere
    std::string                    SelectedColourAlias    {};                                               // [-]     - Selected ColourSwatchBrowserView alias name
    std::string                    SelectedRampIdentifier {};                                               // [-]     - Selected ColourRamp preset identifier
    std::string                    LastStatusMessage      {};                                               // [-]     - Latest footer status feedback
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ContentBrowserPanel
{
public:
    ContentBrowserPanel();
    ~ContentBrowserPanel() noexcept;

    ContentBrowserPanel(const ContentBrowserPanel&)            = delete;
    ContentBrowserPanel& operator=(const ContentBrowserPanel&) = delete;

    void ConfigureShelfExtent(ContentShelfExtent RequestedExtent, bool Immediate = false) noexcept;
    void ConfigureWorkspaceDomain(WorkspaceCategory Workspace) noexcept;
    void ConfigureColourMatCapLock(bool Locked) noexcept;
    [[nodiscard]] bool QueryColourMatCapLocked() const noexcept { return ColourMatCapLocked; }
    bool ActivateCategory(ContentCategory NextCategory) noexcept;
    void ConfigureOriginFilter(ContentOriginFilter Origin) noexcept;
    void ConfigureStandardLayout(StandardRepositoryLayout Layout) noexcept;
    void ConfigureColourLayout(ColourRepositoryLayout Layout) noexcept;
    void ConfigureColourCollectionScope(ColourCollectionScope Scope) noexcept;
    void ConfigureColourFilter(ColourFilterGroup Filter) noexcept;
    void ConfigureColourSort(ColourSortOrder Order) noexcept;
    void ConfigureSearchQuery(std::string_view SearchText);
    void ConfigureStorageHardwareTier(Threading::StorageHardwareTier RequestedTier, const std::filesystem::path& MountTarget = {}) noexcept;
    void ConfigureImportDestination(const ImportIntakeConfiguration& Policy);
    bool SelectColourByName(std::string_view AliasName) noexcept;

    [[nodiscard]] ImportIntakeResult IntakeExternalAssetDrop(const std::filesystem::path& DroppedSourceFile);

    void ClearColourSlots() noexcept;
    void RegisterColourSlot(const ColourSlotRegistration& Registration);
    void UpdateColourSlotBounds(std::string_view SlotIdentifier, Rectangle2D UpdatedScreenBounds) noexcept;
    void FocusColourSlot(std::string_view SlotIdentifier) noexcept;
    void ClearColourRampSlots() noexcept;
    void RegisterColourRampSlot(const ColourRampSlotRegistration& Registration);
    void UpdateColourRampSlotBounds(std::string_view SlotIdentifier, Rectangle2D UpdatedScreenBounds) noexcept;
    void FocusColourRampSlot(std::string_view SlotIdentifier) noexcept;
    void ClearMatCapSlots() noexcept;
    void RegisterMatCapSlot(const MatCapSlotRegistration& Registration);
    void ClearContentDropZones() noexcept;
    void RegisterContentDropZone(const ContentDropZoneRegistration& Registration);
    void UpdateContentDropZoneBounds(std::string_view SlotIdentifier, Rectangle2D UpdatedScreenBounds) noexcept;
    void AttachOutlinerFallback(OutlinerPanel* TargetOutliner) noexcept;
    void AttachThemeRepository(const ThemeRepository* SourceTheme) noexcept;
    void AssignContentActivationCallback(std::function<void(const ContentRepositoryEntry&)> ActivationCallback);

    bool ApplySelectedColourToActiveSlot();
    bool ApplyColourIndexToSlot(std::size_t ColourIndex, std::string_view TargetSlotIdentifier = {}, Point2D DropLogicalPos = { -1.0f, -1.0f });
    bool ApplySelectedRampToActiveSlot();
    bool ApplyRampPresetToSlot(std::size_t RampIndex, std::string_view TargetSlotIdentifier = {}, Point2D DropLogicalPos = { -1.0f, -1.0f });
    bool ApplyMatCapEntryToSlot(std::size_t EntryIndex, std::string_view TargetSlotIdentifier = {});
    bool ApplyContentEntryToDropZone(std::size_t EntryIndex, std::string_view TargetSlotIdentifier, Point2D DropLogicalPos);
    bool ActivateContentEntryAt(std::size_t EntryIndex);

    void RenderWithInput(
        PixelSpace&                       Canvas,
        Rectangle2D                       ViewportLogicalBounds,
        const ContentBrowserPointerInput& PointerInput,
        double                            DeltaSeconds);

    void Render(Rectangle2D HostBoundsPhysical, double DeltaSeconds, float ScaleFactor = 1.0f);

    [[nodiscard]] ContentBrowserReading               InspectReading() const noexcept;
    [[nodiscard]] bool                                QueryOpen() const noexcept;
    [[nodiscard]] bool                                QueryShelfAnimating() const noexcept;
    [[nodiscard]] bool                                QueryShelfDragging() const noexcept { return ShelfDragContact; }
    [[nodiscard]] bool                                QueryBlocksUnderlyingWorkspace() const noexcept;
    void SynchronizeHostBounds(Rectangle2D HostBoundsPhysical, float ScaleFactor) noexcept;
    [[nodiscard]] bool                                QueryBlocksPointerAt(Point2D PhysicalCursor) const noexcept;
    [[nodiscard]] Rectangle2D                         QueryDrawerPhysicalBounds() const noexcept;
    [[nodiscard]] Rectangle2D                         QueryNotchPhysicalBounds() const noexcept;
    [[nodiscard]] float                               QueryVisibleDrawerHeightRatio() const noexcept;
    [[nodiscard]] float                               QueryDrawerTopPhysicalY() const noexcept;
    [[nodiscard]] Rectangle2D                         MapPhysicalToLogicalBounds(Rectangle2D PhysicalBounds) const noexcept;
    [[nodiscard]] Rectangle2D                         QueryNotchTabBounds(Rectangle2D ViewportLogicalBounds) const noexcept;
    [[nodiscard]] Rectangle2D                         QuerySearchFieldBounds() const noexcept;
    [[nodiscard]] Rectangle2D                         QueryRenderedColourCellBounds(std::size_t ColourIndex) const noexcept;
    [[nodiscard]] Rectangle2D                         QueryRenderedRampCellBounds(std::size_t RampIndex) const noexcept;
    [[nodiscard]] Rectangle2D                         QueryRenderedContentCellBounds(std::size_t EntryIndex) const noexcept;
    [[nodiscard]] ColourSwatchBrowserView&                        AccessColourSwatchBrowser() noexcept;
    [[nodiscard]] const ColourSwatchBrowserView&                  QueryColourSwatchBrowser() const noexcept;
    [[nodiscard]] ThumbnailSheetRepository&           AccessThumbnailRepository() noexcept;
    [[nodiscard]] const ThumbnailSheetRepository&     QueryThumbnailRepository() const noexcept;
    [[nodiscard]] Threading::AssetStreamThread&       AccessStreamThread() noexcept;
    [[nodiscard]] const Threading::AssetStreamThread& QueryStreamThread() const noexcept;

private:
    struct RecordedCellHitbox
    {
        std::size_t EntryIndex = 0u; // [idx] - Index inside ColourSwatchBrowserView
        Rectangle2D Bounds     {};   // [px]  - Last rendered logical cell or strip rectangle
    };

    [[nodiscard]] ControlColorSet ResolveActiveColorSet() const noexcept;
    [[nodiscard]] float           ResolveCornerRadius() const noexcept;
    [[nodiscard]] bool            QueryCategoryAllowedInMode(ContentCategory Candidate) const noexcept;

    void RenderColourSlotHighlights(PixelSpace& Canvas, Point2D Cursor, float VisibleBottomY, const ControlColorSet& AppearanceColors) const noexcept;
    void RenderColourRampSlotHighlights(PixelSpace& Canvas, Point2D Cursor, float VisibleBottomY, const ControlColorSet& AppearanceColors) const noexcept;
    void RenderMatCapSlotHighlights(PixelSpace& Canvas, Point2D Cursor, float VisibleBottomY, const ControlColorSet& AppearanceColors) const noexcept;
    void RenderContentDropZoneHighlights(PixelSpace& Canvas, Point2D Cursor, float VisibleBottomY, const ControlColorSet& AppearanceColors) const noexcept;
    void RenderBottomNotchHandle(PixelSpace& Canvas, Rectangle2D NotchBounds, bool Hovered, const ControlColorSet& AppearanceColors) const noexcept;
    void RenderColoursRepository(
        PixelSpace&                       Canvas,
        Rectangle2D                       RepositoryArea,
        const ContentBrowserPointerInput& PointerInput,
        const ControlColorSet&            AppearanceColors,
        float                             SectionRadius);
    void RenderColourRampsRepository(
        PixelSpace&                       Canvas,
        Rectangle2D                       RepositoryArea,
        const ContentBrowserPointerInput& PointerInput,
        const ControlColorSet&            AppearanceColors,
        float                             SectionRadius);
    void RenderNonColoursRepository(
        PixelSpace&                       Canvas,
        Rectangle2D                       RepositoryArea,
        const ContentBrowserPointerInput& PointerInput,
        const ControlColorSet&            AppearanceColors,
        float                             SectionRadius);
    void RenderContentDetailsSidebar(
        PixelSpace&                       Canvas,
        Rectangle2D                       SidebarArea,
        const ContentBrowserPointerInput& PointerInput,
        const ControlColorSet&            AppearanceColors,
        float                             SectionRadius);

    ColourSwatchBrowserView                         CodexLibrary          {};                                       // [-]   - Built-in + custom ColourSwatchBrowserView (.ColourSwatch)
    ThumbnailSheetRepository            ThumbnailRepository   {};                                       // [-]   - Pre-baked 1024x1024 ThumbnailSheet + 64B index
    Threading::AssetStreamThread        StreamChannelThread   {};                                       // [-]   - Dual-path SSD/HDD streaming thread
    SpringIntegrator                    ShelfSpring           {};                                       // [-]   - Smooth bottom-drawer spring
    uint32_t                            ShelfAxis             = 0u;                                     // [idx] - Vertical shelf spring axis
    ContentShelfExtent                  TargetExtent          = ContentShelfExtent::Closed;             // [-]   - Target stop (Closed/SplitHalf/Full)
    WorkspaceCategory                 ActiveWorkspace       = WorkspaceCategory::SceneWorld;        // [-]   - Active workspace domain mask
    ContentCategory                 Category              = ContentCategory::Materials;         // [-]   - Active left-rail category
    ContentOriginFilter                 OriginScope           = ContentOriginFilter::All;               // [-]   - Active origin scope
    StandardRepositoryLayout            StandardLayout        = StandardRepositoryLayout::Grid;         // [-]   - Non-Colours layout
    ColourRepositoryLayout              ColourLayout          = ColourRepositoryLayout::GridCircles;    // [-]   - Colours-only layout
    ColourCollectionScope               ColourScope           = ColourCollectionScope::Swatches;        // [-]   - Swatches vs Ramps sub-scope under Colours
    ColourFilterGroup                   ColourFilter          = ColourFilterGroup::All;                 // [-]   - Dedicated Colour Filters selection
    ColourSortOrder                     ColourSort            = ColourSortOrder::Spectrum;              // [-]   - Colour sort order
    std::string                         SearchBuffer          {};                                       // [-]   - Active search filter text
    TextEditBuffer                      SearchEditor          {};                                       // [-]   - Interactive UTF-8 search edit buffer
    Rectangle2D                         LastSearchBounds      {};                                       // [px]  - Last rendered search input bounds
    std::size_t                         SelectedColourIndex   = 0u;                                     // [idx] - Selected ColourSwatchBrowserView index
    std::size_t                         SelectedRampIndex     = 0u;                                     // [idx] - Selected ColourRamp preset index
    std::size_t                         SelectedContentIndex  = 0u;                                     // [idx] - Selected ThumbnailSheetRepository index
    float                               RepositoryScrollY     = 0.0f;                                   // [px]  - Vertical scroll offset
    uint32_t                            LastVisibleCount      = 0u;                                     // [cnt] - Cached visible entry count
    std::string                         StatusMessage         {};                                       // [-]   - Latest footer status string

    std::vector<ColourSlotRegistration>       RegisteredSlots       {};                                       // [-]   - Bound editor colour slots
    std::vector<ColourRampSlotRegistration>   RegisteredRampSlots   {};                                       // [-]   - Bound editor colour ramp slots
    std::vector<MatCapSlotRegistration>       RegisteredMatCapSlots {};                                       // [-]   - Bound MatCap drop targets
    std::vector<ContentDropZoneRegistration>  RegisteredDropZones   {};                                       // [-]   - Generalized content drop zones (Vectors, Fonts, Materials)
    std::string                         FocusedSlotId         {};                                       // [-]   - Active target colour slot key
    std::string                         FocusedRampSlotId     {};                                       // [-]   - Active target colour ramp slot key
    OutlinerPanel*                      FallbackOutliner      = nullptr;                                // [-]   - Optional Outliner fallback
    const ThemeRepository*              AttachedTheme         = nullptr;                                // [-]   - Optional live ThemeRepository source
    std::function<void(const ContentRepositoryEntry&)> ContentActivationCallback {};                    // [-]   - Optional asset open callback
    std::vector<RecordedCellHitbox>     LastColourHitboxes    {};                                       // [-]   - Hitboxes for testing & drag
    std::vector<RecordedCellHitbox>     LastRampHitboxes      {};                                       // [-]   - Ramp tile hitboxes for testing & drag
    std::vector<RecordedCellHitbox>     LastContentHitboxes   {};                                       // [-]   - Content tile hitboxes for testing & drag
    bool                                ColourMatCapLocked = false;                                  // [-]   - Restrict browser to Colours + MatCap Materials

    bool                                PotentialColourDrag   = false;                                  // [-]   - Pointer pressed on colour swatch
    bool                                DraggingColour        = false;                                  // [-]   - Active drag threshold crossed
    std::size_t                         DraggedColourIndex    = 0u;                                     // [idx] - ColourSwatchBrowserView entry being dragged
    bool                                PotentialRampDrag     = false;                                  // [-]   - Pointer pressed on colour ramp tile
    bool                                DraggingColourRamp    = false;                                  // [-]   - Active colour ramp drag threshold crossed
    std::size_t                         DraggedRampIndex      = 0u;                                     // [idx] - ColourRamp preset index being dragged
    bool                                PotentialMatCapDrag   = false;                                  // [-]   - Pointer pressed on MatCap sphere
    bool                                DraggingMatCap        = false;                                  // [-]   - Active MatCap drag threshold crossed
    std::size_t                         DraggedMatCapIndex    = 0u;                                     // [idx] - ThumbnailSheetRepository MatCap index
    bool                                PotentialContentDrag  = false;                                  // [-]   - Pointer pressed on general content tile
    bool                                DraggingContentEntry  = false;                                  // [-]   - Active non-MatCap content drag crossed threshold
    std::size_t                         DraggedContentIndex   = 0u;                                     // [idx] - ThumbnailSheetRepository entry index being dragged
    Point2D                             DragPressOrigin       {};                                       // [px]  - Press coordinate for deadzone
    bool                                ShelfDragContact      = false;                                  // [-]   - Active touch-like shelf drag session
    bool                                ShelfDragFromNotch    = false;                                  // [-]   - Drag initiated on bottom notch handle
    bool                                ShelfDragExceededDeadzone = false;                              // [-]   - 6px tap-vs-drag deadzone crossed
    float                               ShelfDragStartCursorY = 0.0f;                                   // [px]  - Initial logical pointer Y at drag start
    float                               ShelfDragPreviousCursorY = 0.0f;                                // [px]  - Previous tick logical pointer Y
    float                               ShelfDragStartRatio   = 0.0f;                                   // [0-1] - Normalized drawer height at drag start
    ContentShelfExtent                  ShelfDragStartExtent  = ContentShelfExtent::Closed;             // [-]   - Detent stop prior to drag start
    double                              ShelfDragDuration     = 0.0;                                    // [s]   - Elapsed drag duration
    double                              ShelfDragRatePxPerSec = 0.0;                                    // [px/s]- Smoothed opening velocity (+ = up)
    std::array<Rectangle2D, 8u>         LastHeaderInteractiveBounds {};                                 // [px]  - Header interactive control hitboxes
    uint32_t                            LastHeaderInteractiveCount  = 0u;                               // [cnt] - Recorded header hitbox count
    Rectangle2D                         LastHostBoundsPhysical{ 0.0f, 0.0f, 1840.0f, 980.0f };          // [px]  - Last host viewport physical bounds
    float                               LastSafeScale         = 1.0f;                                   // [-]   - Last rendered DPI safe scale
    RetainedCanvasCache                 RetainedCanvas        {};                                       // [-]   - Cached ImDrawList snapshot for settled shelf
};

} // namespace Frontier
