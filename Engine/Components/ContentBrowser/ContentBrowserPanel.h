/*============================================================================================================================================
                                                           CONTENTBROWSERPANEL.H
============================================================================================================================================*/
// 🧩 Bottom-edge ContentBrowser drawer with pre-baked ThumbnailSheet, dual-path SSD/HDD streaming, strict workspace masks & ColourCodex

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/ContentBrowser/ColourCodex.h"
#include "Engine/Components/ContentBrowser/ThumbnailSheetRepository.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Motion/SpringIntegrator.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Threading/Threads/Streaming/AssetStreamThread.h"

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
    std::string                                             SlotIdentifier       {};        // [-]  - Stable colour-slot key
    std::string                                             DisplayTitle         {};        // [-]  - Human-readable slot label
    Rectangle2D                                             ScreenBounds         {};        // [px] - Logical drop-target rectangle
    ColorRgba*                                              TargetColour         = nullptr; // [-]  - Optional direct ColorRgba destination
    std::function<void(ColorRgba, const ColourCodexEntry&)> NotifyColourAssigned {};        // [-]  - Optional assignment callback
};

struct MatCapSlotRegistration
{
    std::string                                                 SlotIdentifier       {}; // [-]  - Stable MatCap drop-target key
    std::string                                                 DisplayTitle         {}; // [-]  - Human-readable target label
    Rectangle2D                                                 ScreenBounds         {}; // [px] - Logical drop-target rectangle
    std::function<void(uint8_t, const ContentRepositoryEntry&)> NotifyMatCapAssigned {}; // [-]  - Callback receiving MatCap slot (0..15)
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
    StandardRepositoryLayout       ActiveStandardLayout   = StandardRepositoryLayout::Grid;                 // [-]     - Non-Colours layout mode
    ColourFilterGroup              ActiveColourFilter     = ColourFilterGroup::All;                         // [-]     - Dedicated Colour Filters selection
    ColourSortOrder                ActiveColourSort       = ColourSortOrder::Spectrum;                      // [-]     - Colour sort ordering
    Threading::StorageHardwareTier ActiveStorageTier      = Threading::StorageHardwareTier::FlashStorageRandomIo; // [-] - SSD vs HDD streaming path
    ImportDestinationMode          ActiveImportMode       = ImportDestinationMode::ProjectImportedFolder;   // [-]     - Persistent vs Scratch/Temp vs Custom
    uint32_t                       PermittedCategoryCount = 0u;                                             // [count] - Categories visible in active workspace
    uint32_t                       VisibleEntryCount      = 0u;                                             // [count] - Filtered visible entries
    bool                           ColourFiltersVisible   = false;                                          // [-]     - True only when Colours is active
    bool                           DraggingColourActive   = false;                                          // [-]     - True while dragging a ColourCodex swatch
    bool                           DraggingMatCapActive   = false;                                          // [-]     - True while dragging a Studio MatCap sphere
    std::string                    SelectedColourAlias    {};                                               // [-]     - Selected ColourCodex alias name
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
    void ConfigureColourAndMatCapLock(bool Locked) noexcept;
    [[nodiscard]] bool QueryColourAndMatCapLocked() const noexcept { return ColourAndMatCapLocked; }
    bool ActivateCategory(ContentCategory NextCategory) noexcept;
    void ConfigureOriginFilter(ContentOriginFilter Origin) noexcept;
    void ConfigureStandardLayout(StandardRepositoryLayout Layout) noexcept;
    void ConfigureColourLayout(ColourRepositoryLayout Layout) noexcept;
    void ConfigureColourFilter(ColourFilterGroup Filter) noexcept;
    void ConfigureColourSort(ColourSortOrder Order) noexcept;
    void ConfigureSearchQuery(std::string_view SearchText);
    void ConfigureStorageHardwareTier(Threading::StorageHardwareTier RequestedTier, const std::filesystem::path& MountTarget = {}) noexcept;
    void ConfigureImportDestination(const ImportIntakeConfiguration& Policy);
    bool SelectColourByName(std::string_view AliasName) noexcept;

    [[nodiscard]] ImportIntakeResult IntakeExternalAssetDrop(const std::filesystem::path& DroppedSourceFile);

    void ClearColourSlots() noexcept;
    void RegisterColourSlot(const ColourSlotRegistration& Registration);
    void FocusColourSlot(std::string_view SlotIdentifier) noexcept;
    void ClearMatCapSlots() noexcept;
    void RegisterMatCapSlot(const MatCapSlotRegistration& Registration);
    void AttachOutlinerFallback(OutlinerPanel* TargetOutliner) noexcept;
    void AttachThemeRepository(const ThemeRepository* SourceTheme) noexcept;
    void AssignContentActivationCallback(std::function<void(const ContentRepositoryEntry&)> ActivationCallback);

    bool ApplySelectedColourToActiveSlot();
    bool ApplyColourIndexToSlot(std::size_t ColourIndex, std::string_view TargetSlotIdentifier = {});
    bool ApplyMatCapEntryToSlot(std::size_t EntryIndex, std::string_view TargetSlotIdentifier = {});
    bool ActivateContentEntryAt(std::size_t EntryIndex);

    void RenderWithInput(
        PixelSpace&                       Canvas,
        Rectangle2D                       ViewportLogicalBounds,
        const ContentBrowserPointerInput& PointerInput,
        double                            DeltaSeconds);

    void Render(Rectangle2D HostBoundsPhysical, double DeltaSeconds, float ScaleFactor = 1.0f);

    [[nodiscard]] ContentBrowserReading               InspectReading() const noexcept;
    [[nodiscard]] bool                                QueryOpen() const noexcept;
    [[nodiscard]] bool                                QueryBlocksUnderlyingWorkspace() const noexcept;
    [[nodiscard]] bool                                QueryBlocksPointerAt(Point2D PhysicalCursor) const noexcept;
    [[nodiscard]] float                               QueryVisibleDrawerHeightRatio() const noexcept;
    [[nodiscard]] float                               QueryDrawerTopPhysicalY() const noexcept;
    [[nodiscard]] Rectangle2D                         MapPhysicalToLogicalBounds(Rectangle2D PhysicalBounds) const noexcept;
    [[nodiscard]] Rectangle2D                         QueryNotchTabBounds(Rectangle2D ViewportLogicalBounds) const noexcept;
    [[nodiscard]] Rectangle2D                         QuerySearchFieldBounds() const noexcept;
    [[nodiscard]] Rectangle2D                         QueryRenderedColourCellBounds(std::size_t ColourIndex) const noexcept;
    [[nodiscard]] Rectangle2D                         QueryRenderedContentCellBounds(std::size_t EntryIndex) const noexcept;
    [[nodiscard]] ColourCodex&                        AccessColourCodex() noexcept;
    [[nodiscard]] const ColourCodex&                  QueryColourCodex() const noexcept;
    [[nodiscard]] ThumbnailSheetRepository&           AccessThumbnailRepository() noexcept;
    [[nodiscard]] const ThumbnailSheetRepository&     QueryThumbnailRepository() const noexcept;
    [[nodiscard]] Threading::AssetStreamThread&       AccessStreamThread() noexcept;
    [[nodiscard]] const Threading::AssetStreamThread& QueryStreamThread() const noexcept;

private:
    struct RecordedCellHitbox
    {
        std::size_t EntryIndex = 0u; // [idx] - Index inside ColourCodex
        Rectangle2D Bounds     {};   // [px]  - Last rendered logical cell or strip rectangle
    };

    [[nodiscard]] ControlColorSet ResolveActiveColorSet() const noexcept;
    [[nodiscard]] float           ResolveCornerRadius() const noexcept;
    [[nodiscard]] bool            QueryCategoryAllowedInMode(ContentCategory Candidate) const noexcept;

    void RenderColourSlotHighlights(PixelSpace& Canvas, Point2D Cursor, float VisibleBottomY, const ControlColorSet& AppearanceColors) const noexcept;
    void RenderMatCapSlotHighlights(PixelSpace& Canvas, Point2D Cursor, float VisibleBottomY, const ControlColorSet& AppearanceColors) const noexcept;
    void RenderBottomNotchHandle(PixelSpace& Canvas, Rectangle2D NotchBounds, bool Hovered, const ControlColorSet& AppearanceColors) const noexcept;
    void RenderColoursRepository(
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

    ColourCodex                         CodexLibrary          {};                                       // [-]   - Built-in + custom ColourCodex (.ColourSwatch)
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
    ColourFilterGroup                   ColourFilter          = ColourFilterGroup::All;                 // [-]   - Dedicated Colour Filters selection
    ColourSortOrder                     ColourSort            = ColourSortOrder::Spectrum;              // [-]   - Colour sort order
    std::string                         SearchBuffer          {};                                       // [-]   - Active search filter text
    TextEditBuffer                      SearchEditor          {};                                       // [-]   - Interactive UTF-8 search edit buffer
    Rectangle2D                         LastSearchBounds      {};                                       // [px]  - Last rendered search input bounds
    std::size_t                         SelectedColourIndex   = 0u;                                     // [idx] - Selected ColourCodex index
    std::size_t                         SelectedContentIndex  = 0u;                                     // [idx] - Selected ThumbnailSheetRepository index
    float                               RepositoryScrollY     = 0.0f;                                   // [px]  - Vertical scroll offset
    uint32_t                            LastVisibleCount      = 0u;                                     // [cnt] - Cached visible entry count
    std::string                         StatusMessage         {};                                       // [-]   - Latest footer status string

    std::vector<ColourSlotRegistration> RegisteredSlots       {};                                       // [-]   - Bound editor colour slots
    std::vector<MatCapSlotRegistration> RegisteredMatCapSlots {};                                       // [-]   - Bound MatCap drop targets
    std::string                         FocusedSlotId         {};                                       // [-]   - Active target slot key
    OutlinerPanel*                      FallbackOutliner      = nullptr;                                // [-]   - Optional Outliner fallback
    const ThemeRepository*              AttachedTheme         = nullptr;                                // [-]   - Optional live ThemeRepository source
    std::function<void(const ContentRepositoryEntry&)> ContentActivationCallback {};                    // [-]   - Optional asset open callback
    std::vector<RecordedCellHitbox>     LastColourHitboxes    {};                                       // [-]   - Hitboxes for testing & drag
    std::vector<RecordedCellHitbox>     LastContentHitboxes   {};                                       // [-]   - Content tile hitboxes for testing & drag
    bool                                ColourAndMatCapLocked = false;                                  // [-]   - Restrict browser to Colours + MatCap Materials

    bool                                PotentialColourDrag   = false;                                  // [-]   - Pointer pressed on colour swatch
    bool                                DraggingColour        = false;                                  // [-]   - Active drag threshold crossed
    std::size_t                         DraggedColourIndex    = 0u;                                     // [idx] - ColourCodex entry being dragged
    bool                                PotentialMatCapDrag   = false;                                  // [-]   - Pointer pressed on MatCap sphere
    bool                                DraggingMatCap        = false;                                  // [-]   - Active MatCap drag threshold crossed
    std::size_t                         DraggedMatCapIndex    = 0u;                                     // [idx] - ThumbnailSheetRepository MatCap index
    Point2D                             DragPressOrigin       {};                                       // [px]  - Press coordinate for deadzone
    Rectangle2D                         LastHostBoundsPhysical{ 0.0f, 0.0f, 1840.0f, 980.0f };          // [px]  - Last host viewport physical bounds
    float                               LastSafeScale         = 1.0f;                                   // [-]   - Last rendered DPI safe scale
};

} // namespace Frontier
