/*============================================================================================================================================
                                                          WORLDENVIRONMENTREGISTRY.H
============================================================================================================================================*/
// 🧩 Editor-only authoritative registry binding Outliner environment entries to WorldAtmosphereConfiguration, WorldScene, and Construct modal

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <array>
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

enum class WorldEnvironmentCategory : uint8_t
{
    Atmosphere = 0, // [-] - Planetary Rayleigh/Mie/Ozone sky medium (singleton)
    Sun        = 1, // [-] - Solar ephemeris disc, corona, direct sunlight (singleton)
    Clouds     = 2, // [-] - Raymarched volumetric cloud deck & shadows (singleton)
    Moon       = 3, // [-] - Celestial lunar body slot [0..5] (max 6)
    Stars      = 4, // [-] - Procedural nocturnal starfield & Milky Way (singleton)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RegisteredEnvironmentEntry
{
    OutlinerSlotToken        Token          {};                                       // [id]  - Bound Outliner row token
    WorldEnvironmentCategory Category       = WorldEnvironmentCategory::Atmosphere;   // [-]   - Authoritative environment category
    uint32_t                 MoonSlotIndex  = 0u;                                     // [idx] - Dedicated index in Moons[0..5] when Category == Moon
    std::string              Title          {};                                       // [-]   - Current display title
    bool                     LastVisible    = true;                                   // [-]   - Cached effective visibility from last sync
};

struct EnvironmentUndoEntry
{
    WorldEnvironmentCategory                   Category       = WorldEnvironmentCategory::Atmosphere; // [-]   - Restored category
    uint32_t                                   MoonSlotIndex  = 0u;                                   // [idx] - Restored moon slot
    std::string                                Title          {};                                     // [-]   - Restored display title
    bool                                       Visibility     = true;                                 // [-]   - Restored visibility toggle
    GeometricRaster::LunarBodyConfiguration    SavedMoon      {};                                     // [-]   - Restored lunar body parameters
};

struct EnvironmentUndoRecord
{
    std::vector<EnvironmentUndoEntry>              Entries             {};    // [-] - Removed environment entries to restore
    bool                                           SavedSolarValid     = false;
    GeometricRaster::SolarCelestialConfiguration   SavedSolar          {};    // [-] - Restored solar config when Sun removed
    Mathematics::LinearRGB                         SavedSolarRadiance  {};    // [-] - Restored Scene.Sun.SolarIrradiance
    bool                                           SavedMediumValid    = false;
    GeometricRaster::AtmosphericMediumConfiguration SavedMedium        {};    // [-] - Restored medium config when Atmosphere removed
    bool                                           SavedCloudsValid    = false;
    GeometricRaster::CloudDeckConfiguration        SavedClouds         {};    // [-] - Restored cloud deck config when Clouds removed
    bool                                           SavedStarsValid     = false;
    GeometricRaster::StarfieldConfiguration        SavedStars          {};    // [-] - Restored starfield config when Stars removed
};

//------------------------------------------------------------------------------------------------------------------------
//                                              WORLD ENVIRONMENT REGISTRY
//------------------------------------------------------------------------------------------------------------------------

class WorldEnvironmentRegistry
{
public:
    static constexpr uint32_t MaximumMoonCount = 6u; // [count] - Matches WorldAtmosphereConfiguration::Moons size

    WorldEnvironmentRegistry() noexcept = default;

    // Resets internal bindings without mutating external scene data
    void Reset() noexcept;

    // Adopts active atmosphere/sun/clouds/moon/stars flags already present in a loaded WorldScene into 1 default Environment folder
    void AdoptInitialSceneState(
        OutlinerPanel&                                 Outliner,
        GeometricRaster::WorldSceneSpecification&      Scene,
        ViewportDisplaySettings*                       DisplaySettings = nullptr);

    // Ensures the single default "Environment" folder exists in Outliner and returns its token
    OutlinerSlotToken EnsureEnvironmentFolder(OutlinerPanel& Outliner);

    // Registers an environment object by category into Outliner + WorldScene (returns invalid token if singleton/cap reached)
    OutlinerSlotToken RegisterCategory(
        WorldEnvironmentCategory                       Category,
        std::string_view                               CustomTitle,
        OutlinerPanel&                                 Outliner,
        GeometricRaster::WorldSceneSpecification&      Scene,
        ViewportDisplaySettings*                       DisplaySettings = nullptr);

    // Registers an environment object by Construct registry key ("env:atmosphere", "env:sun", "env:clouds", "env:moon", "env:stars")
    OutlinerSlotToken RegisterFromRegistryKey(
        std::string_view                               RegistryKey,
        std::string_view                               CustomTitle,
        OutlinerPanel&                                 Outliner,
        GeometricRaster::WorldSceneSpecification&      Scene,
        ViewportDisplaySettings*                       DisplaySettings = nullptr);

    // Duplicates an environment slot if allowed (e.g. Moon up to 6)
    OutlinerSlotToken DuplicateSlot(
        OutlinerSlotToken                              SourceToken,
        OutlinerPanel&                                 Outliner,
        GeometricRaster::WorldSceneSpecification&      Scene,
        ViewportDisplaySettings*                       DisplaySettings = nullptr);

    // Cleanly unregisters selected slots (including any nested environment entries if the Environment folder is removed)
    uint32_t UnregisterSlots(
        std::span<const OutlinerSlotToken>             Tokens,
        OutlinerPanel&                                 Outliner,
        GeometricRaster::WorldSceneSpecification&      Scene,
        ViewportDisplaySettings*                       DisplaySettings = nullptr);

    // Restores the most recent removed environment entry batch (Ctrl+Z / Outliner Undo delete)
    bool UndoLastRemoval(
        OutlinerPanel&                                 Outliner,
        GeometricRaster::WorldSceneSpecification&      Scene,
        ViewportDisplaySettings*                       DisplaySettings = nullptr);

    // Authoritative sync: propagates Outliner existence & effective visibility into WorldScene + ViewportDisplaySettings + Footer
    // Returns true whenever WorldScene atmosphere/sun state changed (caller should mark GPU/CPU scene dirty)
    bool SyncToWorld(
        OutlinerPanel&                                 Outliner,
        GeometricRaster::WorldSceneSpecification&      Scene,
        ViewportDisplaySettings*                       DisplaySettings = nullptr);

    // Builds the full 7-rail Construct modal registry with live availability badges & disabled reasons
    [[nodiscard]] std::vector<ConstructRegistryEntry> BuildConstructRegistry() const;

    // Convenience helper to push updated Construct registry to a ViewportPanel
    void RefreshViewportConstructRegistry(ViewportPanel& Viewport) const;

    [[nodiscard]] bool QueryCategoryRegistered(WorldEnvironmentCategory Category) const noexcept;
    [[nodiscard]] uint32_t QueryCategoryCount(WorldEnvironmentCategory Category) const noexcept;
    [[nodiscard]] uint32_t QueryTotalRegisteredCount() const noexcept { return static_cast<uint32_t>(RegisteredSlots.size()); }
    [[nodiscard]] bool QueryUndoAvailable() const noexcept { return !UndoStack.empty(); }
    [[nodiscard]] OutlinerSlotToken QueryEnvironmentFolderToken() const noexcept { return EnvironmentFolderToken; }
    [[nodiscard]] std::span<const RegisteredEnvironmentEntry> QueryRegisteredEntries() const noexcept { return RegisteredSlots; }

    [[nodiscard]] static const char* QueryDomainTag(WorldEnvironmentCategory Category) noexcept;
    [[nodiscard]] static const char* QueryDefaultTitle(WorldEnvironmentCategory Category) noexcept;
    [[nodiscard]] static bool        ResolveCategoryFromRegistryKey(
        std::string_view          RegistryKey,
        WorldEnvironmentCategory& OutCategory) noexcept;

private:
    [[nodiscard]] const RegisteredEnvironmentEntry* FindRegistered(OutlinerSlotToken Token) const noexcept;
    [[nodiscard]] uint32_t                          AllocateFreeMoonSlot() const noexcept;
    void                                            RefreshDuplicateProtection(OutlinerPanel& Outliner) const noexcept;
    void                                            ApplyBaselineSolarRadiance(
        GeometricRaster::WorldSceneSpecification& Scene,
        bool                                      SunActive) noexcept;

    OutlinerSlotToken                       EnvironmentFolderToken   {};                                // [id]  - Single default "Environment" folder
    std::vector<RegisteredEnvironmentEntry> RegisteredSlots          {};                                // [-]   - Active environment registrations
    std::vector<EnvironmentUndoRecord>      UndoStack                {};                                // [-]   - Undo snapshots for clean Ctrl+Z restore
    Mathematics::LinearRGB                  CachedSolarIrradiance    { 5.40f, 5.12f, 4.72f };           // [-]   - Preserved direct sun irradiance across toggle/remove
    Mathematics::LinearRGB                  CachedFallbackSky        { 0.0f, 0.0f, 0.0f };              // [-]   - Baseline non-atmosphere sky radiance
    bool                                    FallbackSkyCaptured      = false;                           // [-]   - Tracks whether initial scene fallback sky was captured
};

} // namespace Frontier
