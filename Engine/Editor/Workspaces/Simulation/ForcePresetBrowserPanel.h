/*============================================================================================================================================
                                                          FORCEPRESETBROWSERPANEL.H
============================================================================================================================================*/
// 🧩 Project-Zero pull-up Content Browser drawer & trapezoidal bottom handle hosting .ForceModel presets, 19 ForceModel laws & formulas

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ForceModelPresetCodec.h"
#include "Engine/Components/Motion/SpringIntegrator.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

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

enum class ContentAssetOrigin : uint8_t
{
    All      = 0, // [-] - Display presets from every library origin
    Engine   = 1, // [-] - Built-in read-only engine preset library
    Project  = 2, // [-] - Project-authored .ForceModel presets
    Imported = 3, // [-] - Disk-imported .ForceModel containers
};

enum class ContentAssetCategory : uint8_t
{
    All            = 0, // [-] - All asset categories combined
    ForcePresets   = 1, // [-] - Curated ready-to-use physical Force presets
    ForceModels      = 2, // [-] - All 19 fundamental ForceModel analytical laws
    FormulaPresets = 3, // [-] - Symbolic VectorFieldLang GPU formula presets
};

enum class ContentBrowserViewLayout : uint8_t
{
    Grid = 0, // [-] - Thumbnail tile grid with SVG law icons
    List = 1, // [-] - Compact single-line inventory list
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ContentBrowserAssetEntry
{
    std::string                    Identifier      {};                                   // [-] - Unique stable asset key
    std::string                    Title           {};                                   // [-] - Displayed asset title
    std::string                    CategoryCaption {};                                   // [-] - Model + domain subtitle
    std::string                    MetricSummary   {};                                   // [-] - Concise physical parameter line
    ContentAssetOrigin             Origin          = ContentAssetOrigin::Engine;         // [-] - Library origin tier
    ContentAssetCategory           Category        = ContentAssetCategory::ForcePresets; // [-] - Asset rail classification
    Codecs::ForceModelPresetDocument PresetDocument{};                                   // [-] - Authoritative .ForceModel configuration
    bool                           FormulaOverride = false;                              // [-] - Symbolic GPU expression override active
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ForcePresetBrowserPanel
{
public:
    ForcePresetBrowserPanel() noexcept;

    void PopulateDefaultForcePresets();
    void ClearAssets() noexcept;
    void AddProjectPreset(
        const Codecs::ForceModelPresetDocument& Document,
        ContentAssetOrigin                      Origin = ContentAssetOrigin::Project);
    uint32_t LoadForcePresetsFromDirectory(const std::filesystem::path& DirectoryPath);

    void ConfigureOpen(bool OpenRequested, bool Immediate = false) noexcept;
    void SnapOpen(bool OpenRequested) noexcept;
    void ConfigureOriginFilter(ContentAssetOrigin Origin) noexcept;
    void ConfigureCategoryFilter(ContentAssetCategory Category) noexcept;
    void ConfigureViewLayout(ContentBrowserViewLayout Layout) noexcept;
    void ConfigureSelectedAssetId(std::string_view Identifier);
    void ConfigureConstructPresetCallback(std::function<void(const Codecs::ForceModelPresetDocument&)> Callback);
    void ConfigureSaveSelectedCallback(std::function<bool(Codecs::ForceModelPresetDocument&)> Callback);

    [[nodiscard]] bool ConstructSelectedPreset();
    [[nodiscard]] bool ConstructPresetByIndex(uint32_t VisibleIndex);

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             HostBoundsLogical,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiHost(double Δt, float ScaleFactor = 1.0f);

    [[nodiscard]] bool                                    QueryOpen() const noexcept;
    [[nodiscard]] float                                   QueryDrawerProgress() const noexcept;
    [[nodiscard]] ContentAssetOrigin                      QueryOriginFilter() const noexcept;
    [[nodiscard]] ContentAssetCategory                    QueryCategoryFilter() const noexcept;
    [[nodiscard]] std::span<const ContentBrowserAssetEntry> QueryAllAssets() const noexcept;
    [[nodiscard]] const ContentBrowserAssetEntry*         QuerySelectedAsset() const noexcept;
    [[nodiscard]] std::vector<uint32_t>                   QueryVisibleAssetIndices() const;

private:
    [[nodiscard]] Rectangle2D QueryBottomHandleBounds(Rectangle2D HostBounds, float DrawerHeightPx) const noexcept;
    [[nodiscard]] float       QueryDrawerMaximumHeight(Rectangle2D HostBounds) const noexcept;

    std::vector<ContentBrowserAssetEntry>                          Assets               {};                                 // [-]   - Registered presets, laws & formulas
    ContentAssetOrigin                                             OriginFilter         = ContentAssetOrigin::All;          // [-]   - Origin tab selector
    ContentAssetCategory                                           CategoryFilter       = ContentAssetCategory::ForcePresets;// [-]   - Left rail category filter
    ContentBrowserViewLayout                                       ViewLayout           = ContentBrowserViewLayout::Grid;   // [-]   - Grid vs compact list view
    TextEditBuffer                                                 SearchEditor         {};                                 // [-]   - Live search buffer
    std::string                                                    SelectedIdentifier   {};                                 // [-]   - Currently inspected asset identifier
    std::string                                                    StatusBannerText     {};                                 // [-]   - Concise footer status message
    float                                                          GridScrollOffsetY    = 0.0f;                             // [px]  - Vertical scroll offset inside center tile grid

    SpringIntegrator                                               Motion               {};                                 // [-]   - Bottom drawer spring animation solver
    uint32_t                                                       VerticalAxis         = 0u;                               // [idx] - Spring axis index [0..1]
    bool                                                           DrawerOpen           = false;                            // [-]   - Target open/closed toggle
    float                                                          LastDrawerMaxH       = 460.0f;                           // [px]  - Cached drawer height ceiling

    std::function<void(const Codecs::ForceModelPresetDocument&)>   ConstructPresetCallback {};                              // [-]   - Callback spawning preset into workspace
    std::function<bool(Codecs::ForceModelPresetDocument&)>         SaveSelectionCallback   {};                              // [-]   - Callback extracting selected scene force as preset
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline bool ForcePresetBrowserPanel::QueryOpen() const noexcept
{
    return DrawerOpen;
}

inline ContentAssetOrigin ForcePresetBrowserPanel::QueryOriginFilter() const noexcept
{
    return OriginFilter;
}

inline ContentAssetCategory ForcePresetBrowserPanel::QueryCategoryFilter() const noexcept
{
    return CategoryFilter;
}

inline std::span<const ContentBrowserAssetEntry> ForcePresetBrowserPanel::QueryAllAssets() const noexcept
{
    return Assets;
}

} // namespace Frontier
