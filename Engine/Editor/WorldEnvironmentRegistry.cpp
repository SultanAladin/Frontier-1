/*============================================================================================================================================
                                                         WORLDENVIRONMENTREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Implementation of Editor-only WorldEnvironmentRegistry binding Outliner entries, Construct modal, and WorldAtmosphereConfiguration

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/WorldEnvironmentRegistry.h"

#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

constexpr std::array<GeometricRaster::LunarBodyConfiguration, WorldEnvironmentRegistry::MaximumMoonCount> CanonicalMoonPresets = {{
    { true, false, GeometricRaster::LunarSurfacePreset::Luna,    0.62f, 1.60f, 0.80f, 0.52f, 300.0f, 28.0f,   0.0f,  0.0f },
    { true, false, GeometricRaster::LunarSurfacePreset::Ember,   0.35f, 1.40f, 0.90f, 0.78f, 115.0f, 22.0f,  12.0f,  0.0f },
    { true, false, GeometricRaster::LunarSurfacePreset::Glacier, 0.80f, 1.25f, 0.65f, 0.44f, 210.0f, 41.0f,   0.0f, -8.0f },
    { true, false, GeometricRaster::LunarSurfacePreset::Sulfur,  0.50f, 1.80f, 1.00f, 0.60f,  45.0f, 19.0f,   0.0f,  0.0f },
    { true, false, GeometricRaster::LunarSurfacePreset::Shroud,  0.56f, 1.75f, 0.85f, 0.58f, 165.0f, 26.0f,   8.0f,  4.0f },
    { true, false, GeometricRaster::LunarSurfacePreset::Shard,   0.52f, 1.70f, 0.75f, 0.50f, 335.0f, 34.0f, -10.0f,  6.0f },
}};

[[nodiscard]] const char* QueryCategorySubtitle(WorldEnvironmentCategory Category) noexcept
{
    switch (Category)
    {
        case WorldEnvironmentCategory::Atmosphere: return "Atmosphere";
        case WorldEnvironmentCategory::Sun:        return "Solar Light";
        case WorldEnvironmentCategory::Clouds:     return "Volumetric";
        case WorldEnvironmentCategory::Moon:       return "Celestial";
        case WorldEnvironmentCategory::Stars:      return "Starfield";
        default:                                   return "Environment";
    }
}

[[nodiscard]] ConstructSlotCategory QueryFallbackSlotCategory(WorldEnvironmentCategory Category) noexcept
{
    switch (Category)
    {
        case WorldEnvironmentCategory::Sun:    return ConstructSlotCategory::PointLuminaire;
        case WorldEnvironmentCategory::Clouds: return ConstructSlotCategory::LayerGroup;
        default:                               return ConstructSlotCategory::LayerGroup;
    }
}

[[nodiscard]] bool LinearRgbDifferent(const Mathematics::LinearRGB& Left, const Mathematics::LinearRGB& Right) noexcept
{
    return std::fabs(Left.R - Right.R) > 1.0e-5f ||
           std::fabs(Left.G - Right.G) > 1.0e-5f ||
           std::fabs(Left.B - Right.B) > 1.0e-5f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    STATIC HELPERS
//------------------------------------------------------------------------------------------------------------------------

const char* WorldEnvironmentRegistry::QueryDomainTag(WorldEnvironmentCategory Category) noexcept
{
    switch (Category)
    {
        case WorldEnvironmentCategory::Atmosphere: return "env:atmosphere";
        case WorldEnvironmentCategory::Sun:        return "env:sun";
        case WorldEnvironmentCategory::Clouds:     return "env:clouds";
        case WorldEnvironmentCategory::Moon:       return "env:moon";
        case WorldEnvironmentCategory::Stars:      return "env:stars";
        default:                                   return "env:atmosphere";
    }
}

const char* WorldEnvironmentRegistry::QueryDefaultTitle(WorldEnvironmentCategory Category) noexcept
{
    switch (Category)
    {
        case WorldEnvironmentCategory::Atmosphere: return "Atmosphere";
        case WorldEnvironmentCategory::Sun:        return "Sun";
        case WorldEnvironmentCategory::Clouds:     return "Clouds";
        case WorldEnvironmentCategory::Moon:       return "Moon";
        case WorldEnvironmentCategory::Stars:      return "Stars";
        default:                                   return "Atmosphere";
    }
}

bool WorldEnvironmentRegistry::ResolveCategoryFromRegistryKey(
    std::string_view          RegistryKey,
    WorldEnvironmentCategory& OutCategory) noexcept
{
    if (RegistryKey == "env:atmosphere") { OutCategory = WorldEnvironmentCategory::Atmosphere; return true; }
    if (RegistryKey == "env:sun")        { OutCategory = WorldEnvironmentCategory::Sun;        return true; }
    if (RegistryKey == "env:clouds")     { OutCategory = WorldEnvironmentCategory::Clouds;     return true; }
    if (RegistryKey == "env:moon")       { OutCategory = WorldEnvironmentCategory::Moon;       return true; }
    if (RegistryKey == "env:stars")      { OutCategory = WorldEnvironmentCategory::Stars;      return true; }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE & ADOPTION
//------------------------------------------------------------------------------------------------------------------------

void WorldEnvironmentRegistry::Reset() noexcept
{
    EnvironmentFolderToken = {};
    RegisteredSlots.clear();
    UndoStack.clear();
    CachedSolarIrradiance  = { 5.40f, 5.12f, 4.72f };
    CachedFallbackSky      = { 0.0f, 0.0f, 0.0f };
    FallbackSkyCaptured    = false;
}

void WorldEnvironmentRegistry::AdoptInitialSceneState(
    OutlinerPanel&                            Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings)
{
    Reset();

    if (Scene.Sun.SolarIrradiance.R > 1.0e-4f ||
        Scene.Sun.SolarIrradiance.G > 1.0e-4f ||
        Scene.Sun.SolarIrradiance.B > 1.0e-4f)
    {
        CachedSolarIrradiance = Scene.Sun.SolarIrradiance;
    }
    CachedFallbackSky   = Scene.SkyRadiance;
    FallbackSkyCaptured = true;

    const auto& Atmo = Scene.Atmosphere;
    if (Atmo.Medium.AtmosphereEnabled)
    {
        RegisterCategory(WorldEnvironmentCategory::Atmosphere, "Atmosphere", Outliner, Scene, DisplaySettings);
    }
    if (Atmo.Solar.SolarEnabled)
    {
        RegisterCategory(WorldEnvironmentCategory::Sun, "Sun", Outliner, Scene, DisplaySettings);
    }
    if (Atmo.Clouds.CloudsEnabled)
    {
        RegisterCategory(WorldEnvironmentCategory::Clouds, "Clouds", Outliner, Scene, DisplaySettings);
    }
    for (uint32_t SlotIdx = 0u; SlotIdx < MaximumMoonCount; ++SlotIdx)
    {
        if (Atmo.Moons[SlotIdx].Visible)
        {
            const std::string MoonTitle = (SlotIdx == 0u) ? "Moon" : ("Moon " + std::to_string(SlotIdx + 1u));
            const OutlinerSlotToken FolderToken = EnsureEnvironmentFolder(Outliner);
            OutlinerRecordEntry Record{};
            Record.Title              = MoonTitle;
            Record.Subtitle           = QueryCategorySubtitle(WorldEnvironmentCategory::Moon);
            Record.Category           = QueryFallbackSlotCategory(WorldEnvironmentCategory::Moon);
            Record.EnclosingToken     = FolderToken;
            Record.VisibilityEnabled  = true;
            Record.Expanded           = true;
            Record.DomainTag          = QueryDomainTag(WorldEnvironmentCategory::Moon);
            const OutlinerSlotToken Issued = Outliner.ConstructDirectEntry(std::move(Record));
            RegisteredSlots.push_back({ Issued, WorldEnvironmentCategory::Moon, SlotIdx, MoonTitle, true });
        }
    }
    if (Atmo.Stars.StarfieldEnabled)
    {
        RegisterCategory(WorldEnvironmentCategory::Stars, "Stars", Outliner, Scene, DisplaySettings);
    }

    RefreshDuplicateProtection(Outliner);
    SyncToWorld(Outliner, Scene, DisplaySettings);
}

OutlinerSlotToken WorldEnvironmentRegistry::EnsureEnvironmentFolder(OutlinerPanel& Outliner)
{
    if (EnvironmentFolderToken.Valid() && Outliner.FindEntry(EnvironmentFolderToken) != nullptr)
    {
        return EnvironmentFolderToken;
    }

    // Reuse an existing Environment folder if present in Outliner
    for (const OutlinerRecordEntry& Entry : Outliner.QueryEntries())
    {
        if (Entry.Category == ConstructSlotCategory::Folder && Entry.DomainTag == "env:folder")
        {
            EnvironmentFolderToken = Entry.Token;
            return EnvironmentFolderToken;
        }
    }

    OutlinerRecordEntry FolderRecord{};
    FolderRecord.Title             = "Environment";
    FolderRecord.Subtitle          = "Folder";
    FolderRecord.Category          = ConstructSlotCategory::Folder;
    FolderRecord.FolderTint        = WorkspaceSurfaceColors::SageCollection;
    FolderRecord.VisibilityEnabled = true;
    FolderRecord.Expanded          = true;
    FolderRecord.DuplicateProtected = true;
    FolderRecord.DuplicateReason   = "Single default Environment folder";
    FolderRecord.DomainTag         = "env:folder";

    EnvironmentFolderToken = Outliner.ConstructDirectEntry(std::move(FolderRecord));
    return EnvironmentFolderToken;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           REGISTRATION & UNREGISTRATION
//------------------------------------------------------------------------------------------------------------------------

OutlinerSlotToken WorldEnvironmentRegistry::RegisterCategory(
    WorldEnvironmentCategory                  Category,
    std::string_view                          CustomTitle,
    OutlinerPanel&                            Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings)
{
    if (!FallbackSkyCaptured)
    {
        CachedFallbackSky   = Scene.SkyRadiance;
        FallbackSkyCaptured = true;
    }

    if (Category != WorldEnvironmentCategory::Moon && QueryCategoryRegistered(Category))
    {
        return {};
    }

    uint32_t AssignedMoonSlot = 0u;
    if (Category == WorldEnvironmentCategory::Moon)
    {
        if (QueryCategoryCount(WorldEnvironmentCategory::Moon) >= MaximumMoonCount)
        {
            return {};
        }
        AssignedMoonSlot = AllocateFreeMoonSlot();
        Scene.Atmosphere.Moons[AssignedMoonSlot] = CanonicalMoonPresets[AssignedMoonSlot];
        Scene.Atmosphere.Moons[AssignedMoonSlot].Visible = true;
    }
    else if (Category == WorldEnvironmentCategory::Atmosphere)
    {
        Scene.Atmosphere.Medium.AtmosphereEnabled = true;
    }
    else if (Category == WorldEnvironmentCategory::Sun)
    {
        Scene.Atmosphere.Solar.SolarEnabled = true;
        if (Scene.Sun.SolarIrradiance.R <= 1.0e-4f &&
            Scene.Sun.SolarIrradiance.G <= 1.0e-4f &&
            Scene.Sun.SolarIrradiance.B <= 1.0e-4f)
        {
            Scene.Sun.SolarIrradiance = CachedSolarIrradiance;
        }
        const auto SolarSolved = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Scene.Atmosphere.Solar);
        Scene.Sun.DirectionToSun = SolarSolved.WorldDirectionToSun;
    }
    else if (Category == WorldEnvironmentCategory::Clouds)
    {
        Scene.Atmosphere.Clouds.CloudsEnabled              = true;
        Scene.Atmosphere.Clouds.CloudShadow.ShadowEnabled  = true;
        if (Scene.Atmosphere.Clouds.CloudFraction < 0.05f)
        {
            Scene.Atmosphere.Clouds.CloudFraction = 0.34f;
        }
    }
    else if (Category == WorldEnvironmentCategory::Stars)
    {
        Scene.Atmosphere.Stars.StarfieldEnabled = true;
    }

    std::string FinalTitle(CustomTitle);
    if (FinalTitle.empty())
    {
        if (Category == WorldEnvironmentCategory::Moon)
        {
            const uint32_t NextMoonNum = QueryCategoryCount(WorldEnvironmentCategory::Moon) + 1u;
            FinalTitle = (NextMoonNum <= 1u) ? "Moon" : ("Moon " + std::to_string(NextMoonNum));
        }
        else
        {
            FinalTitle = QueryDefaultTitle(Category);
        }
    }

    const OutlinerSlotToken FolderToken = EnsureEnvironmentFolder(Outliner);

    OutlinerRecordEntry Record{};
    Record.Title             = FinalTitle;
    Record.Subtitle          = QueryCategorySubtitle(Category);
    Record.Category          = QueryFallbackSlotCategory(Category);
    Record.EnclosingToken    = FolderToken;
    Record.VisibilityEnabled = true;
    Record.Expanded          = true;
    Record.DomainTag         = QueryDomainTag(Category);
    if (Category != WorldEnvironmentCategory::Moon)
    {
        Record.DuplicateProtected = true;
        Record.DuplicateReason    = std::string("Only one ") + QueryDefaultTitle(Category) + " per scene";
    }

    const OutlinerSlotToken Issued = Outliner.ConstructDirectEntry(std::move(Record));
    Outliner.SelectSingle(Issued);

    RegisteredSlots.push_back({ Issued, Category, AssignedMoonSlot, FinalTitle, true });
    RefreshDuplicateProtection(Outliner);
    SyncToWorld(Outliner, Scene, DisplaySettings);
    return Issued;
}

OutlinerSlotToken WorldEnvironmentRegistry::RegisterFromRegistryKey(
    std::string_view                          RegistryKey,
    std::string_view                          CustomTitle,
    OutlinerPanel&                            Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings)
{
    WorldEnvironmentCategory Resolved{};
    if (!ResolveCategoryFromRegistryKey(RegistryKey, Resolved))
    {
        return {};
    }
    return RegisterCategory(Resolved, CustomTitle, Outliner, Scene, DisplaySettings);
}

OutlinerSlotToken WorldEnvironmentRegistry::DuplicateSlot(
    OutlinerSlotToken                         SourceToken,
    OutlinerPanel&                            Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings)
{
    const RegisteredEnvironmentEntry* SourceReg = FindRegistered(SourceToken);
    if (SourceReg == nullptr)
    {
        return Outliner.DuplicateSlot(SourceToken);
    }

    if (SourceReg->Category != WorldEnvironmentCategory::Moon)
    {
        return {};
    }

    const uint32_t NextMoonNum = QueryCategoryCount(WorldEnvironmentCategory::Moon) + 1u;
    const std::string NextTitle = "Moon " + std::to_string(NextMoonNum);
    return RegisterCategory(WorldEnvironmentCategory::Moon, NextTitle, Outliner, Scene, DisplaySettings);
}

uint32_t WorldEnvironmentRegistry::UnregisterSlots(
    std::span<const OutlinerSlotToken>        Tokens,
    OutlinerPanel&                            Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings)
{
    if (Tokens.empty())
    {
        return 0u;
    }

    // Expand condemned tokens to include nested descendants (e.g. deleting the Environment folder)
    std::vector<OutlinerSlotToken> Expanded{};
    Expanded.reserve(Tokens.size());
    for (const OutlinerSlotToken Tok : Tokens)
    {
        const OutlinerRecordEntry* Entry = Outliner.FindEntry(Tok);
        if (Entry != nullptr && !Entry->RemovalProtected &&
            std::find(Expanded.begin(), Expanded.end(), Tok) == Expanded.end())
        {
            Expanded.push_back(Tok);
        }
    }

    for (std::size_t Cursor = 0u; Cursor < Expanded.size(); ++Cursor)
    {
        const OutlinerSlotToken Current = Expanded[Cursor];
        for (const OutlinerRecordEntry& Entry : Outliner.QueryEntries())
        {
            if (Entry.EnclosingToken == Current && !Entry.RemovalProtected &&
                std::find(Expanded.begin(), Expanded.end(), Entry.Token) == Expanded.end())
            {
                Expanded.push_back(Entry.Token);
            }
        }
    }

    // Capture undo snapshot for every registered environment slot being removed
    EnvironmentUndoRecord UndoRec{};
    for (const OutlinerSlotToken Tok : Expanded)
    {
        const RegisteredEnvironmentEntry* Reg = FindRegistered(Tok);
        if (Reg == nullptr)
        {
            continue;
        }

        EnvironmentUndoEntry Snap{};
        Snap.Category      = Reg->Category;
        Snap.MoonSlotIndex = Reg->MoonSlotIndex;
        if (const OutlinerRecordEntry* Entry = Outliner.FindEntry(Tok))
        {
            Snap.Title      = Entry->Title;
            Snap.Visibility = Entry->VisibilityEnabled;
        }
        else
        {
            Snap.Title      = Reg->Title;
            Snap.Visibility = true;
        }

        if (Reg->Category == WorldEnvironmentCategory::Moon && Reg->MoonSlotIndex < MaximumMoonCount)
        {
            Snap.SavedMoon = Scene.Atmosphere.Moons[Reg->MoonSlotIndex];
        }
        else if (Reg->Category == WorldEnvironmentCategory::Atmosphere)
        {
            UndoRec.SavedMediumValid = true;
            UndoRec.SavedMedium      = Scene.Atmosphere.Medium;
        }
        else if (Reg->Category == WorldEnvironmentCategory::Sun)
        {
            UndoRec.SavedSolarValid    = true;
            UndoRec.SavedSolar         = Scene.Atmosphere.Solar;
            UndoRec.SavedSolarRadiance = CachedSolarIrradiance;
        }
        else if (Reg->Category == WorldEnvironmentCategory::Clouds)
        {
            UndoRec.SavedCloudsValid = true;
            UndoRec.SavedClouds      = Scene.Atmosphere.Clouds;
        }
        else if (Reg->Category == WorldEnvironmentCategory::Stars)
        {
            UndoRec.SavedStarsValid = true;
            UndoRec.SavedStars      = Scene.Atmosphere.Stars;
        }
        UndoRec.Entries.push_back(std::move(Snap));
    }

    if (!UndoRec.Entries.empty())
    {
        UndoStack.push_back(std::move(UndoRec));
    }

    // Remove registered bindings cleanly
    RegisteredSlots.erase(
        std::remove_if(
            RegisteredSlots.begin(),
            RegisteredSlots.end(),
            [&](const RegisteredEnvironmentEntry& Reg) noexcept
            {
                return std::find(Expanded.begin(), Expanded.end(), Reg.Token) != Expanded.end();
            }),
        RegisteredSlots.end());

    if (std::find(Expanded.begin(), Expanded.end(), EnvironmentFolderToken) != Expanded.end())
    {
        EnvironmentFolderToken = {};
    }

    // Delete all requested non-protected Outliner slots
    uint32_t RemovedCount = 0u;
    for (const OutlinerSlotToken Tok : Tokens)
    {
        if (Outliner.DeleteSlot(Tok))
        {
            ++RemovedCount;
        }
    }

    RefreshDuplicateProtection(Outliner);
    SyncToWorld(Outliner, Scene, DisplaySettings);
    return RemovedCount;
}

bool WorldEnvironmentRegistry::UndoLastRemoval(
    OutlinerPanel&                            Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings)
{
    if (UndoStack.empty())
    {
        return false;
    }

    EnvironmentUndoRecord Record = std::move(UndoStack.back());
    UndoStack.pop_back();

    if (Record.SavedMediumValid)
    {
        Scene.Atmosphere.Medium = Record.SavedMedium;
    }
    if (Record.SavedSolarValid)
    {
        Scene.Atmosphere.Solar = Record.SavedSolar;
        CachedSolarIrradiance  = Record.SavedSolarRadiance;
    }
    if (Record.SavedCloudsValid)
    {
        Scene.Atmosphere.Clouds = Record.SavedClouds;
    }
    if (Record.SavedStarsValid)
    {
        Scene.Atmosphere.Stars = Record.SavedStars;
    }

    const OutlinerSlotToken FolderToken = EnsureEnvironmentFolder(Outliner);
    OutlinerSlotToken LastRestored{};

    for (const EnvironmentUndoEntry& Snap : Record.Entries)
    {
        if (Snap.Category != WorldEnvironmentCategory::Moon && QueryCategoryRegistered(Snap.Category))
        {
            continue;
        }

        uint32_t MoonSlot = Snap.MoonSlotIndex;
        if (Snap.Category == WorldEnvironmentCategory::Moon)
        {
            if (QueryCategoryCount(WorldEnvironmentCategory::Moon) >= MaximumMoonCount)
            {
                continue;
            }
            bool SlotOccupied = false;
            for (const RegisteredEnvironmentEntry& Existing : RegisteredSlots)
            {
                if (Existing.Category == WorldEnvironmentCategory::Moon && Existing.MoonSlotIndex == MoonSlot)
                {
                    SlotOccupied = true;
                    break;
                }
            }
            if (SlotOccupied || MoonSlot >= MaximumMoonCount)
            {
                MoonSlot = AllocateFreeMoonSlot();
            }
            Scene.Atmosphere.Moons[MoonSlot] = Snap.SavedMoon;
            Scene.Atmosphere.Moons[MoonSlot].Visible = Snap.Visibility;
        }

        OutlinerRecordEntry OutlinerRec{};
        OutlinerRec.Title             = Snap.Title.empty() ? QueryDefaultTitle(Snap.Category) : Snap.Title;
        OutlinerRec.Subtitle          = QueryCategorySubtitle(Snap.Category);
        OutlinerRec.Category          = QueryFallbackSlotCategory(Snap.Category);
        OutlinerRec.EnclosingToken    = FolderToken;
        OutlinerRec.VisibilityEnabled = Snap.Visibility;
        OutlinerRec.Expanded          = true;
        OutlinerRec.DomainTag         = QueryDomainTag(Snap.Category);
        if (Snap.Category != WorldEnvironmentCategory::Moon)
        {
            OutlinerRec.DuplicateProtected = true;
            OutlinerRec.DuplicateReason    = std::string("Only one ") + QueryDefaultTitle(Snap.Category) + " per scene";
        }

        const OutlinerSlotToken Issued = Outliner.ConstructDirectEntry(std::move(OutlinerRec));
        RegisteredSlots.push_back({ Issued, Snap.Category, MoonSlot, Snap.Title, Snap.Visibility });
        LastRestored = Issued;
    }

    if (LastRestored.Valid())
    {
        Outliner.SelectSingle(LastRestored);
    }

    RefreshDuplicateProtection(Outliner);
    SyncToWorld(Outliner, Scene, DisplaySettings);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              AUTHORITATIVE WORLD SYNC
//------------------------------------------------------------------------------------------------------------------------

bool WorldEnvironmentRegistry::SyncToWorld(
    OutlinerPanel&                            Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings)
{
    // Prune any tokens removed outside the registry
    RegisteredSlots.erase(
        std::remove_if(
            RegisteredSlots.begin(),
            RegisteredSlots.end(),
            [&](const RegisteredEnvironmentEntry& Reg) noexcept
            {
                return Outliner.FindEntry(Reg.Token) == nullptr;
            }),
        RegisteredSlots.end());

    if (EnvironmentFolderToken.Valid() && Outliner.FindEntry(EnvironmentFolderToken) == nullptr)
    {
        EnvironmentFolderToken = {};
    }

    bool AtmoActive   = false;
    bool SunActive    = false;
    bool CloudsActive = false;
    bool StarsActive  = false;
    std::array<bool, MaximumMoonCount> MoonActive{};
    uint32_t ActiveMoonCount = 0u;

    for (RegisteredEnvironmentEntry& Reg : RegisteredSlots)
    {
        const bool EffVisible = Outliner.QueryEffectiveVisibility(Reg.Token);
        Reg.LastVisible       = EffVisible;
        if (!EffVisible)
        {
            continue;
        }
        switch (Reg.Category)
        {
            case WorldEnvironmentCategory::Atmosphere: AtmoActive   = true; break;
            case WorldEnvironmentCategory::Sun:        SunActive    = true; break;
            case WorldEnvironmentCategory::Clouds:     CloudsActive = true; break;
            case WorldEnvironmentCategory::Stars:      StarsActive  = true; break;
            case WorldEnvironmentCategory::Moon:
                if (Reg.MoonSlotIndex < MaximumMoonCount)
                {
                    MoonActive[Reg.MoonSlotIndex] = true;
                    ++ActiveMoonCount;
                }
                break;
        }
    }

    bool Changed = false;
    auto& Atmo   = Scene.Atmosphere;

    if (Atmo.Medium.AtmosphereEnabled != AtmoActive)
    {
        Atmo.Medium.AtmosphereEnabled = AtmoActive;
        Changed = true;
    }

    if (Atmo.Solar.SolarEnabled != SunActive)
    {
        Atmo.Solar.SolarEnabled = SunActive;
        Changed = true;
    }

    const Mathematics::LinearRGB PrevSunIrr = Scene.Sun.SolarIrradiance;
    ApplyBaselineSolarRadiance(Scene, SunActive);
    if (LinearRgbDifferent(PrevSunIrr, Scene.Sun.SolarIrradiance))
    {
        Changed = true;
    }

    if (Atmo.Clouds.CloudsEnabled != CloudsActive)
    {
        Atmo.Clouds.CloudsEnabled = CloudsActive;
        Changed = true;
    }

    const bool WantCloudShadows = CloudsActive;
    if (Atmo.Clouds.CloudShadow.ShadowEnabled != WantCloudShadows)
    {
        Atmo.Clouds.CloudShadow.ShadowEnabled = WantCloudShadows;
        Changed = true;
    }

    if (Atmo.Stars.StarfieldEnabled != StarsActive)
    {
        Atmo.Stars.StarfieldEnabled = StarsActive;
        Changed = true;
    }

    for (uint32_t Idx = 0u; Idx < MaximumMoonCount; ++Idx)
    {
        if (Atmo.Moons[Idx].Visible != MoonActive[Idx])
        {
            Atmo.Moons[Idx].Visible = MoonActive[Idx];
            Changed = true;
        }
    }

    // Keep background sky clean black vacuum whenever Atmosphere is not active so removing Atmosphere visibly clears the sky
    const Mathematics::LinearRGB TargetSkyRadiance = AtmoActive ? CachedFallbackSky : Mathematics::LinearRGB{ 0.0f, 0.0f, 0.0f };
    if (LinearRgbDifferent(Scene.SkyRadiance, TargetSkyRadiance))
    {
        Scene.SkyRadiance = TargetSkyRadiance;
        Changed = true;
    }

    if (DisplaySettings != nullptr)
    {
        DisplaySettings->AtmosphereSkyEnabled    = AtmoActive;
        DisplaySettings->VolumetricCloudsEnabled = CloudsActive;
        DisplaySettings->CloudShadowsEnabled     = WantCloudShadows;
        DisplaySettings->CelestialStarsEnabled   = StarsActive;
    }

    // Update Outliner footer telemetry slots for Sun and Moons
    OutlinerFooterReadings Footer = Outliner.QueryFooterReadings();
    if (SunActive)
    {
        const auto SolarSolved = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
        char SunBuf[32]{};
        std::snprintf(SunBuf, sizeof(SunBuf), "%.0f deg", SolarSolved.ElevationDegrees);
        Footer.PrimarySlot = SunBuf;
    }
    else
    {
        Footer.PrimarySlot = "Off";
    }
    Footer.SecondarySlot = (ActiveMoonCount > 0u) ? std::to_string(ActiveMoonCount) : "0";
    Outliner.ConfigureFooterReadings(Footer);

    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CONSTRUCT MODAL REGISTRY
//------------------------------------------------------------------------------------------------------------------------

std::vector<ConstructRegistryEntry> WorldEnvironmentRegistry::BuildConstructRegistry() const
{
    std::vector<ConstructRegistryEntry> Entries{};
    Entries.reserve(20u);

    const bool     HasAtmo   = QueryCategoryRegistered(WorldEnvironmentCategory::Atmosphere);
    const bool     HasSun    = QueryCategoryRegistered(WorldEnvironmentCategory::Sun);
    const bool     HasClouds = QueryCategoryRegistered(WorldEnvironmentCategory::Clouds);
    const bool     HasStars  = QueryCategoryRegistered(WorldEnvironmentCategory::Stars);
    const uint32_t MoonCount = QueryCategoryCount(WorldEnvironmentCategory::Moon);
    const bool     MoonsFull = (MoonCount >= MaximumMoonCount);

    // 1. Environment rail (HTML 03-ConstructModalEnvironment.png)
    Entries.push_back({
        "env:atmosphere",
        "Atmosphere",
        "Atmosphere",
        "Environment",
        "Physical Rayleigh, Mie, Ozone planetary sky medium and limb scattering.",
        "Environment",
        ConstructCategoryRail::Environment,
        EnvironmentSymbolCategory::Atmosphere,
        ConstructSlotCategory::LayerGroup,
        HasAtmo,
        HasAtmo ? "Added" : "",
        HasAtmo ? "Atmosphere is already in the scene" : "",
    });

    Entries.push_back({
        "env:sun",
        "Sun",
        "Sun",
        "Environment",
        "Physical solar ephemeris, photospheric disc, corona, and direct sunlight.",
        "Environment",
        ConstructCategoryRail::Environment,
        EnvironmentSymbolCategory::Sun,
        ConstructSlotCategory::PointLuminaire,
        HasSun,
        HasSun ? "Added" : "",
        HasSun ? "Sun is already in the scene" : "",
    });

    const std::string NextMoonTitle = (MoonCount == 0u) ? "Moon" : ("Moon " + std::to_string(MoonCount + 1u));
    const std::string MoonBadge     = (MoonCount > 0u) ? (std::to_string(MoonCount) + " / 6") : "";
    Entries.push_back({
        "env:moon",
        "Moon",
        NextMoonTitle,
        "Environment",
        "Celestial lunar body with phase illumination, regolith mare, and halo.",
        "Environment",
        ConstructCategoryRail::Environment,
        EnvironmentSymbolCategory::Moon,
        ConstructSlotCategory::LayerGroup,
        MoonsFull,
        MoonBadge,
        MoonsFull ? "Maximum of 6 Moons reached" : "",
    });

    Entries.push_back({
        "env:stars",
        "Stars",
        "Stars",
        "Environment",
        "Procedural starfield, J2000 bright navigation stars, and Milky Way band.",
        "Environment",
        ConstructCategoryRail::Environment,
        EnvironmentSymbolCategory::Stars,
        ConstructSlotCategory::LayerGroup,
        HasStars,
        HasStars ? "Added" : "",
        HasStars ? "Stars are already in the scene" : "",
    });

    // 2. Weather rail (HTML 04-ConstructModalWeather.png: Clouds active, others greyed out)
    Entries.push_back({
        "env:clouds",
        "Clouds",
        "Clouds",
        "Weather",
        "Raymarched volumetric cloud deck with multi-scattering and ground shadows.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::Clouds,
        ConstructSlotCategory::LayerGroup,
        HasClouds,
        HasClouds ? "Added" : "",
        HasClouds ? "Clouds are already in the scene" : "",
    });

    Entries.push_back({
        "weather:wind",
        "Wind",
        "Wind",
        "Weather",
        "Global advection wind vector field.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::Wind,
        ConstructSlotCategory::LayerGroup,
        true,
        "Soon",
        "Not yet implemented",
    });

    Entries.push_back({
        "weather:fog",
        "Fog",
        "Fog",
        "Weather",
        "Height-attenuated atmospheric aerial fog.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::Fog,
        ConstructSlotCategory::LayerGroup,
        true,
        "Soon",
        "Not yet implemented",
    });

    Entries.push_back({
        "weather:precipitation",
        "Precipitation",
        "Precipitation",
        "Weather",
        "Rain and snow weather emitter.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::Precipitation,
        ConstructSlotCategory::LayerGroup,
        true,
        "Soon",
        "Not yet implemented",
    });

    Entries.push_back({
        "weather:god-rays",
        "God Rays",
        "God Rays",
        "Weather",
        "Crepuscular volumetric light shafts.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::GodRays,
        ConstructSlotCategory::LayerGroup,
        true,
        "Soon",
        "Not yet implemented",
    });

    Entries.push_back({
        "weather:rainbow",
        "Rainbow",
        "Rainbow",
        "Weather",
        "Optical spectral atmospheric rainbow arc.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::Rainbow,
        ConstructSlotCategory::LayerGroup,
        true,
        "Soon",
        "Not yet implemented",
    });

    // 3. Cameras rail (greyed out pending Cameras batch)
    Entries.push_back({
        "camera:perspective",
        "Perspective Camera",
        "Camera",
        "Cameras",
        "Physical thin-lens perspective camera.",
        "Cameras",
        ConstructCategoryRail::Cameras,
        EnvironmentSymbolCategory::None,
        ConstructSlotCategory::OpticsCamera,
        true,
        "Soon",
        "Not yet implemented",
    });

    // 4. Geometry rail (greyed out pending Geometry batch)
    Entries.push_back({
        "geometry:cube",
        "Cube",
        "Cube",
        "Geometry",
        "Unit box primitive mesh.",
        "Geometry",
        ConstructCategoryRail::Geometry,
        EnvironmentSymbolCategory::None,
        ConstructSlotCategory::GeometryVolume,
        true,
        "Soon",
        "Not yet implemented",
    });

    // 5. Lighting rail (greyed out pending Lighting batch)
    Entries.push_back({
        "lighting:point",
        "Point Light",
        "Point Light",
        "Lighting",
        "Omnidirectional photometric point luminaire.",
        "Lighting",
        ConstructCategoryRail::Lighting,
        EnvironmentSymbolCategory::None,
        ConstructSlotCategory::PointLuminaire,
        true,
        "Soon",
        "Not yet implemented",
    });

    // 6. Vehicle rail (greyed out pending Vehicle batch)
    Entries.push_back({
        "vehicle:sedan",
        "Vehicle",
        "Vehicle",
        "Vehicle",
        "Drivable articulated rigid-body vehicle.",
        "Vehicles",
        ConstructCategoryRail::Vehicle,
        EnvironmentSymbolCategory::Vehicle,
        ConstructSlotCategory::GeometryVolume,
        true,
        "Soon",
        "Not yet implemented",
    });

    return Entries;
}

void WorldEnvironmentRegistry::RefreshViewportConstructRegistry(ViewportPanel& Viewport) const
{
    Viewport.ConfigureConstructRegistry(BuildConstructRegistry());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    QUERIES & HELPERS
//------------------------------------------------------------------------------------------------------------------------

bool WorldEnvironmentRegistry::QueryCategoryRegistered(WorldEnvironmentCategory Category) const noexcept
{
    return QueryCategoryCount(Category) > 0u;
}

uint32_t WorldEnvironmentRegistry::QueryCategoryCount(WorldEnvironmentCategory Category) const noexcept
{
    uint32_t Count = 0u;
    for (const RegisteredEnvironmentEntry& Reg : RegisteredSlots)
    {
        if (Reg.Category == Category)
        {
            ++Count;
        }
    }
    return Count;
}

const RegisteredEnvironmentEntry* WorldEnvironmentRegistry::FindRegistered(OutlinerSlotToken Token) const noexcept
{
    for (const RegisteredEnvironmentEntry& Reg : RegisteredSlots)
    {
        if (Reg.Token == Token)
        {
            return &Reg;
        }
    }
    return nullptr;
}

uint32_t WorldEnvironmentRegistry::AllocateFreeMoonSlot() const noexcept
{
    std::array<bool, MaximumMoonCount> Occupied{};
    for (const RegisteredEnvironmentEntry& Reg : RegisteredSlots)
    {
        if (Reg.Category == WorldEnvironmentCategory::Moon && Reg.MoonSlotIndex < MaximumMoonCount)
        {
            Occupied[Reg.MoonSlotIndex] = true;
        }
    }
    for (uint32_t Idx = 0u; Idx < MaximumMoonCount; ++Idx)
    {
        if (!Occupied[Idx])
        {
            return Idx;
        }
    }
    return 0u;
}

void WorldEnvironmentRegistry::RefreshDuplicateProtection(OutlinerPanel& Outliner) const noexcept
{
    const bool MoonsFull = (QueryCategoryCount(WorldEnvironmentCategory::Moon) >= MaximumMoonCount);
    for (const RegisteredEnvironmentEntry& Reg : RegisteredSlots)
    {
        if (OutlinerRecordEntry* Entry = Outliner.AccessEntry(Reg.Token))
        {
            if (Reg.Category == WorldEnvironmentCategory::Moon)
            {
                Entry->DuplicateProtected = MoonsFull;
                Entry->DuplicateReason    = MoonsFull ? "Maximum of 6 Moons reached" : "";
            }
            else
            {
                Entry->DuplicateProtected = true;
                Entry->DuplicateReason    = std::string("Only one ") + QueryDefaultTitle(Reg.Category) + " per scene";
            }
        }
    }
}

void WorldEnvironmentRegistry::ApplyBaselineSolarRadiance(
    GeometricRaster::WorldSceneSpecification& Scene,
    bool                                      SunActive) noexcept
{
    if (SunActive)
    {
        if (Scene.Sun.SolarIrradiance.R <= 1.0e-4f &&
            Scene.Sun.SolarIrradiance.G <= 1.0e-4f &&
            Scene.Sun.SolarIrradiance.B <= 1.0e-4f)
        {
            Scene.Sun.SolarIrradiance = CachedSolarIrradiance;
        }
        else
        {
            CachedSolarIrradiance = Scene.Sun.SolarIrradiance;
        }
        const auto SolarSolved = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Scene.Atmosphere.Solar);
        Scene.Sun.DirectionToSun = SolarSolved.WorldDirectionToSun;
    }
    else
    {
        if (Scene.Sun.SolarIrradiance.R > 1.0e-4f ||
            Scene.Sun.SolarIrradiance.G > 1.0e-4f ||
            Scene.Sun.SolarIrradiance.B > 1.0e-4f)
        {
            CachedSolarIrradiance = Scene.Sun.SolarIrradiance;
        }
        Scene.Sun.SolarIrradiance = { 0.0f, 0.0f, 0.0f };
    }
}

} // namespace Frontier
