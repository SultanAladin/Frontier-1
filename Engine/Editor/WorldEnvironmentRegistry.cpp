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

    const auto AdoptExistingByTag = [&](WorldEnvironmentCategory Category, uint32_t MoonSlot = 0u) -> bool
    {
        const char* ExpectedTag = QueryDomainTag(Category);
        for (const OutlinerRecordEntry& Entry : Outliner.QueryEntries())
        {
            if (Entry.DomainTag == ExpectedTag)
            {
                RegisteredSlots.push_back({ Entry.Token, Category, MoonSlot, Entry.Title, Entry.VisibilityEnabled });
                return true;
            }
        }
        return false;
    };

    const auto& Atmo = Scene.Atmosphere;
    if (!AdoptExistingByTag(WorldEnvironmentCategory::Atmosphere) && Atmo.Medium.AtmosphereEnabled)
    {
        RegisterCategory(WorldEnvironmentCategory::Atmosphere, "Atmosphere", Outliner, Scene, DisplaySettings);
    }
    if (!AdoptExistingByTag(WorldEnvironmentCategory::Sun) && Atmo.Solar.SolarEnabled)
    {
        RegisterCategory(WorldEnvironmentCategory::Sun, "Sun", Outliner, Scene, DisplaySettings);
    }
    if (!AdoptExistingByTag(WorldEnvironmentCategory::Clouds) && Atmo.Clouds.CloudsEnabled)
    {
        RegisterCategory(WorldEnvironmentCategory::Clouds, "Clouds", Outliner, Scene, DisplaySettings);
    }
    if (!AdoptExistingByTag(WorldEnvironmentCategory::Moon, 0u))
    {
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
    }
    if (!AdoptExistingByTag(WorldEnvironmentCategory::Stars) && Atmo.Stars.StarfieldEnabled)
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
    if (ResolveCategoryFromRegistryKey(RegistryKey, Resolved))
    {
        return RegisterCategory(Resolved, CustomTitle, Outliner, Scene, DisplaySettings);
    }

    struct DirectInspectorEntrySpec
    {
        std::string_view      Key;
        const char*           DomainTag;
        const char*           DefaultTitle;
        const char*           CategoryLabel;
        const char*           Subtitle;
        ConstructSlotCategory SlotCategory;
        const char*           FolderName;
    };

    static constexpr std::array<DirectInspectorEntrySpec, 25u> DirectSpecs = {{
        { "env:local-cloud",       "env:local-cloud",       "Local Cloud",        "World / Local Cloud",       "Bounded 3D cloud box",       ConstructSlotCategory::LayerGroup,     "World"    },
        { "env:wind",              "env:wind",              "Wind",               "World / Wind",              "Global advection field",     ConstructSlotCategory::LayerGroup,     "World"    },
        { "weather:wind",          "env:wind",              "Wind",               "World / Wind",              "Global advection field",     ConstructSlotCategory::LayerGroup,     "World"    },
        { "env:height-fog",        "env:height-fog",        "Height Fog",         "World / Height Fog",        "Exponential vertical fog",   ConstructSlotCategory::LayerGroup,     "World"    },
        { "weather:fog",           "env:height-fog",        "Height Fog",         "World / Height Fog",        "Exponential vertical fog",   ConstructSlotCategory::LayerGroup,     "World"    },
        { "env:aerial-fog",        "env:aerial-fog",        "Atmospheric Fog",    "World / Aerial Fog",        "Aerial perspective fog",     ConstructSlotCategory::LayerGroup,     "World"    },
        { "env:local-fog",         "env:local-fog",         "Local Fog",          "World / Local Fog",         "Bounded volumetric fog",     ConstructSlotCategory::LayerGroup,     "World"    },
        { "env:precipitation",     "env:precipitation",     "Precipitation",      "World / Precipitation",     "Rain & snow simulation",     ConstructSlotCategory::LayerGroup,     "World"    },
        { "weather:precipitation", "env:precipitation",     "Precipitation",      "World / Precipitation",     "Rain & snow simulation",     ConstructSlotCategory::LayerGroup,     "World"    },
        { "env:god-rays",          "env:god-rays",          "God Rays",           "World / God Rays",          "Crepuscular light shafts",   ConstructSlotCategory::LayerGroup,     "World"    },
        { "weather:god-rays",      "env:god-rays",          "God Rays",           "World / God Rays",          "Crepuscular light shafts",   ConstructSlotCategory::LayerGroup,     "World"    },
        { "env:rainbow",           "env:rainbow",           "Rainbow",            "World / Rainbow",           "Spectral atmospheric arc",   ConstructSlotCategory::LayerGroup,     "World"    },
        { "weather:rainbow",       "env:rainbow",           "Rainbow",            "World / Rainbow",           "Spectral atmospheric arc",   ConstructSlotCategory::LayerGroup,     "World"    },
        { "env:lens-flare",        "env:lens-flare",        "Lens Flare",         "World / Lens Flare",        "Optical streaks & ghosts",   ConstructSlotCategory::LayerGroup,     "World"    },
        { "env:environment-light", "env:environment-light", "Environment Light",  "Lighting / Sky Dome IBL",   "Hemisphere sky irradiance",  ConstructSlotCategory::PointLuminaire, "Lighting" },
        { "env:post-process",      "env:post-process",      "Post Process",       "Cameras / Post Process",    "Tone map & color grade",     ConstructSlotCategory::LayerGroup,     "Cameras"  },
        { "env:motion-blur",       "env:motion-blur",       "Motion Blur",        "Cameras / Motion Blur",     "Shutter velocity blur",      ConstructSlotCategory::LayerGroup,     "Cameras"  },
        { "env:camera-settings",   "env:camera-settings",   "Camera Settings",    "Cameras / Render Engine",   "Renderer GI, AO & shadows",  ConstructSlotCategory::OpticsCamera,   "Cameras"  },
        { "camera:perspective",    "camera:perspective",    "Camera",             "Cameras / Optics",          "Perspective optical camera", ConstructSlotCategory::OpticsCamera,   "Cameras"  },
        { "geom:primitive",        "geom:primitive",        "Primitive",          "Geometry / Analytic",       "Analytic surface primitive", ConstructSlotCategory::GeometryVolume, "Showcase" },
        { "geometry:cube",         "geom:primitive",        "Cube",               "Geometry / Analytic",       "Analytic box primitive",     ConstructSlotCategory::GeometryVolume, "Showcase" },
        { "geom:native",           "geom:native",           "Native Mesh",        "Geometry / Native",         "Native triangle mesh",       ConstructSlotCategory::GeometryVolume, "Showcase" },
        { "geom:procedural",       "geom:procedural",       "Procedural Mesh",    "Geometry / Procedural",     "Parametric procedural mesh", ConstructSlotCategory::GeometryVolume, "Showcase" },
        { "geom:imported",         "geom:imported",         "Imported Asset",     "Geometry / Imported",       "External USD / glTF mesh",   ConstructSlotCategory::GeometryVolume, "Showcase" },
        { "light:point",           "light:point",           "Point Light",        "Lighting / Point",          "Omnidirectional luminaire",  ConstructSlotCategory::PointLuminaire, "Lighting" },
    }};

    static constexpr std::array<DirectInspectorEntrySpec, 7u> ExtraLightSpecs = {{
        { "lighting:point",   "light:point",     "Point Light",  "Lighting / Point",     "Omnidirectional luminaire", ConstructSlotCategory::PointLuminaire, "Lighting" },
        { "light:spot",       "light:spot",      "Spot Light",   "Lighting / Spot",      "Focused cone spotlight",    ConstructSlotCategory::PointLuminaire, "Lighting" },
        { "light:area",       "light:area",      "Area Light",   "Lighting / Area",      "LTC polygonal quad light",  ConstructSlotCategory::PointLuminaire, "Lighting" },
        { "light:ies",        "light:ies",       "IES Light",    "Lighting / IES",       "Photometric IES profile",   ConstructSlotCategory::PointLuminaire, "Lighting" },
        { "light:tube",       "light:tube",      "Tube Light",   "Lighting / Tube",      "Linear glowing studio tube",ConstructSlotCategory::PointLuminaire, "Lighting" },
        { "light:led",        "light:led",       "LED Light",    "Lighting / LED Panel", "LED emitter fixture",       ConstructSlotCategory::PointLuminaire, "Lighting" },
        { "light:led-strip",  "light:led-strip", "LED Strip",    "Lighting / LED Strip", "Flexible LED light strip",  ConstructSlotCategory::PointLuminaire, "Lighting" },
    }};

    const DirectInspectorEntrySpec* Matched = nullptr;
    for (const auto& Spec : DirectSpecs)
    {
        if (Spec.Key == RegistryKey) { Matched = &Spec; break; }
    }
    if (Matched == nullptr)
    {
        for (const auto& Spec : ExtraLightSpecs)
        {
            if (Spec.Key == RegistryKey) { Matched = &Spec; break; }
        }
    }
    if (Matched == nullptr)
    {
        return {};
    }

    OutlinerSlotToken ParentFolder{};
    for (const OutlinerRecordEntry& Entry : Outliner.QueryEntries())
    {
        if (Entry.Category == ConstructSlotCategory::Folder &&
            (Entry.Title == Matched->FolderName ||
             (std::string_view(Matched->FolderName) == "World" && Entry.DomainTag == "env:folder")))
        {
            ParentFolder = Entry.Token;
            break;
        }
    }
    if (!ParentFolder.Valid())
    {
        ParentFolder = (std::string_view(Matched->FolderName) == "World")
            ? EnsureEnvironmentFolder(Outliner)
            : Outliner.ConstructFolder(Matched->FolderName);
    }

    OutlinerRecordEntry Record{};
    Record.Title             = CustomTitle.empty() ? std::string(Matched->DefaultTitle) : std::string(CustomTitle);
    Record.CategoryLabel     = Matched->CategoryLabel;
    Record.Subtitle          = Matched->Subtitle;
    Record.Category          = Matched->SlotCategory;
    Record.EnclosingToken    = ParentFolder;
    Record.DomainTag         = Matched->DomainTag;
    Record.VisibilityEnabled = true;
    Record.Expanded          = true;

    const OutlinerSlotToken Issued = Outliner.ConstructDirectEntry(std::move(Record));
    Outliner.SelectSingle(Issued);
    return Issued;
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

    // Keep background sky clean black vacuum whenever Atmosphere is not active so removing Atmosphere visibly clears the sky.
    // When Atmosphere IS active, adopt any live non-zero sky radiance computed by the atmosphere solver into CachedFallbackSky
    // instead of resetting it every frame (avoids an infinite Changed=true ping-pong with SynchronizeWorldAtmosphere).
    if (AtmoActive)
    {
        if (Scene.SkyRadiance.R <= 1.0e-4f &&
            Scene.SkyRadiance.G <= 1.0e-4f &&
            Scene.SkyRadiance.B <= 1.0e-4f)
        {
            Scene.SkyRadiance = CachedFallbackSky;
            Changed = true;
        }
        else
        {
            CachedFallbackSky = Scene.SkyRadiance;
        }
    }
    else
    {
        const Mathematics::LinearRGB VacuumSky { 0.0f, 0.0f, 0.0f };
        if (LinearRgbDifferent(Scene.SkyRadiance, VacuumSky))
        {
            Scene.SkyRadiance = VacuumSky;
            Changed = true;
        }
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

    Entries.push_back({
        "env:lens-flare",
        "Lens Flare",
        "Lens Flare",
        "Environment",
        "Optical anamorphic streaks, diffraction starburst, and chromatic ghosts.",
        "Environment",
        ConstructCategoryRail::Environment,
        EnvironmentSymbolCategory::LensFlare,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:post-process",
        "Post Process",
        "Post Process",
        "Environment",
        "Filmic tone mapping, exposure, bloom, and colour grading stack.",
        "Cameras",
        ConstructCategoryRail::Environment,
        EnvironmentSymbolCategory::PostProcess,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    // 2. Weather rail
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
        "env:local-cloud",
        "Local Cloud",
        "Local Cloud",
        "Weather",
        "Spatially bounded volumetric cloud box with local billow shaping.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::LocalCloud,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:wind",
        "Wind",
        "Wind",
        "Weather",
        "Global advection wind vector field and gust turbulence.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::Wind,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:height-fog",
        "Height Fog",
        "Height Fog",
        "Weather",
        "Exponential vertical height fog with horizon scattering.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::Fog,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:aerial-fog",
        "Atmospheric Fog",
        "Atmospheric Fog",
        "Weather",
        "Aerial perspective distance scattering and horizon spectrum.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::AtmosphericFog,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:local-fog",
        "Local Fog",
        "Local Fog",
        "Weather",
        "Bounded volumetric fog shape with edge feathering.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::LocalFog,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:precipitation",
        "Precipitation",
        "Precipitation",
        "Weather",
        "Rain, snow, and hail world-space weather simulation.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::Precipitation,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:god-rays",
        "God Rays",
        "God Rays",
        "Weather",
        "Crepuscular volumetric light shafts through atmospheric media.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::GodRays,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:rainbow",
        "Rainbow",
        "Rainbow",
        "Weather",
        "Optical spectral atmospheric rainbow arc and supernumerary bands.",
        "Environment",
        ConstructCategoryRail::Weather,
        EnvironmentSymbolCategory::Rainbow,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    // 3. Cameras rail
    Entries.push_back({
        "camera:perspective",
        "Perspective Camera",
        "Camera",
        "Cameras",
        "Physical thin-lens perspective camera with sensor and aperture.",
        "Cameras",
        ConstructCategoryRail::Cameras,
        EnvironmentSymbolCategory::Camera,
        ConstructSlotCategory::OpticsCamera,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:camera-settings",
        "Camera Settings",
        "Camera Settings",
        "Cameras",
        "Render engine GI, ambient occlusion, shadows, and output pipeline.",
        "Cameras",
        ConstructCategoryRail::Cameras,
        EnvironmentSymbolCategory::CameraSettings,
        ConstructSlotCategory::OpticsCamera,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:motion-blur",
        "Motion Blur",
        "Motion Blur",
        "Cameras",
        "Camera and per-object shutter angle velocity motion blur.",
        "Cameras",
        ConstructCategoryRail::Cameras,
        EnvironmentSymbolCategory::MotionBlur,
        ConstructSlotCategory::LayerGroup,
        false,
        "",
        "",
    });

    // 4. Geometry rail
    Entries.push_back({
        "geom:primitive",
        "Primitive",
        "Primitive",
        "Geometry",
        "Analytic geometric primitive mesh (box, sphere, cylinder, torus).",
        "Geometry",
        ConstructCategoryRail::Geometry,
        EnvironmentSymbolCategory::Primitive,
        ConstructSlotCategory::GeometryVolume,
        false,
        "",
        "",
    });

    Entries.push_back({
        "geom:procedural",
        "Procedural Mesh",
        "Procedural Mesh",
        "Geometry",
        "Parametric procedural geometry generator.",
        "Geometry",
        ConstructCategoryRail::Geometry,
        EnvironmentSymbolCategory::ProceduralMesh,
        ConstructSlotCategory::GeometryVolume,
        false,
        "",
        "",
    });

    Entries.push_back({
        "geom:native",
        "Native Mesh",
        "Native Mesh",
        "Geometry",
        "Native engine cluster-LOD mesh asset.",
        "Geometry",
        ConstructCategoryRail::Geometry,
        EnvironmentSymbolCategory::NativeMesh,
        ConstructSlotCategory::GeometryVolume,
        false,
        "",
        "",
    });

    Entries.push_back({
        "geom:imported",
        "Imported Asset",
        "Imported Asset",
        "Geometry",
        "Imported USD / glTF / FBX scene geometry asset.",
        "Geometry",
        ConstructCategoryRail::Geometry,
        EnvironmentSymbolCategory::ImportedAsset,
        ConstructSlotCategory::GeometryVolume,
        false,
        "",
        "",
    });

    // 5. Lighting rail
    Entries.push_back({
        "light:point",
        "Point Light",
        "Point Light",
        "Lighting",
        "Omnidirectional photometric point luminaire.",
        "Lighting",
        ConstructCategoryRail::Lighting,
        EnvironmentSymbolCategory::PointLight,
        ConstructSlotCategory::PointLuminaire,
        false,
        "",
        "",
    });

    Entries.push_back({
        "light:spot",
        "Spot Light",
        "Spot Light",
        "Lighting",
        "Focused cone spotlight with soft inner/outer beam penumbra.",
        "Lighting",
        ConstructCategoryRail::Lighting,
        EnvironmentSymbolCategory::SpotLight,
        ConstructSlotCategory::PointLuminaire,
        false,
        "",
        "",
    });

    Entries.push_back({
        "light:area",
        "Area Light",
        "Area Light",
        "Lighting",
        "Analytic LTC rectangular / disc studio softbox luminaire.",
        "Lighting",
        ConstructCategoryRail::Lighting,
        EnvironmentSymbolCategory::AreaLight,
        ConstructSlotCategory::PointLuminaire,
        false,
        "",
        "",
    });

    Entries.push_back({
        "light:ies",
        "IES Light",
        "IES Light",
        "Lighting",
        "Measured photometric IES intensity distribution luminaire.",
        "Lighting",
        ConstructCategoryRail::Lighting,
        EnvironmentSymbolCategory::IesLight,
        ConstructSlotCategory::PointLuminaire,
        false,
        "",
        "",
    });

    Entries.push_back({
        "light:tube",
        "Tube Light",
        "Tube Light",
        "Lighting",
        "Linear studio tube emitter with capsule falloff.",
        "Lighting",
        ConstructCategoryRail::Lighting,
        EnvironmentSymbolCategory::TubeLight,
        ConstructSlotCategory::PointLuminaire,
        false,
        "",
        "",
    });

    Entries.push_back({
        "light:led",
        "LED Light",
        "LED Light",
        "Lighting",
        "Directional LED diode matrix fixture.",
        "Lighting",
        ConstructCategoryRail::Lighting,
        EnvironmentSymbolCategory::LedLight,
        ConstructSlotCategory::PointLuminaire,
        false,
        "",
        "",
    });

    Entries.push_back({
        "light:led-strip",
        "LED Strip",
        "LED Strip",
        "Lighting",
        "Flexible architectural LED ribbon strip.",
        "Lighting",
        ConstructCategoryRail::Lighting,
        EnvironmentSymbolCategory::LedStrip,
        ConstructSlotCategory::PointLuminaire,
        false,
        "",
        "",
    });

    Entries.push_back({
        "env:environment-light",
        "Environment Light",
        "Environment Light",
        "Lighting",
        "Sky hemisphere IBL irradiance dome.",
        "Lighting",
        ConstructCategoryRail::Lighting,
        EnvironmentSymbolCategory::EnvironmentLight,
        ConstructSlotCategory::PointLuminaire,
        false,
        "",
        "",
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
