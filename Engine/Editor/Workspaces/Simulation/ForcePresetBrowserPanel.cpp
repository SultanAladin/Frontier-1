/*============================================================================================================================================
                                                         FORCEPRESETBROWSERPANEL.CPP
============================================================================================================================================*/
// 🧩 Project-Zero pull-up Content Browser drawer & trapezoidal bottom handle hosting .ForceModel presets, 19 ForceModel laws & formulas

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Workspaces/Simulation/ForcePresetBrowserPanel.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 PROJECT ZERO COLOR TOKENS
//------------------------------------------------------------------------------------------------------------------------

namespace DrawerColors
{
inline constexpr ColorRgba DrawerBg        { 16,  16,  16,  253 };
inline constexpr ColorRgba PanelWellBg     { 24,  24,  26,  255 };
inline constexpr ColorRgba TileBg          { 26,  26,  28,  255 };
inline constexpr ColorRgba TileHoverBg     { 38,  38,  42,  255 };
inline constexpr ColorRgba TileThumbBg     { 18,  18,  20,  255 };
inline constexpr ColorRgba PreviewBg       { 20,  20,  22,  255 };
inline constexpr ColorRgba HandleFill      { 36,  36,  36,  255 };
inline constexpr ColorRgba HandleHoverFill { 50,  50,  52,  255 };
inline constexpr ColorRgba HairlineBorder  { 255, 255, 255, 22  };
inline constexpr ColorRgba SubtleBorder    { 255, 255, 255, 38  };
inline constexpr ColorRgba InkPrimary      { 238, 238, 240, 255 };
inline constexpr ColorRgba InkSecondary    { 168, 170, 176, 255 };
inline constexpr ColorRgba InkMuted        { 118, 120, 126, 255 };
inline constexpr ColorRgba AmberAccent     { 255, 180, 84,  255 };
inline constexpr ColorRgba ConstructCream  { 226, 224, 218, 255 };
inline constexpr ColorRgba ConstructInk    { 18,  18,  20,  255 };
inline constexpr ColorRgba TealFormula     { 169, 217, 200, 255 };
} // namespace DrawerColors

//------------------------------------------------------------------------------------------------------------------------
//                                                    HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool TextContainsCaseInsensitive(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty())
    {
        return true;
    }
    if (Needle.size() > Haystack.size())
    {
        return false;
    }
    for (std::size_t I = 0u; I + Needle.size() <= Haystack.size(); ++I)
    {
        bool Matched = true;
        for (std::size_t J = 0u; J < Needle.size(); ++J)
        {
            const unsigned char A = static_cast<unsigned char>(Haystack[I + J]);
            const unsigned char B = static_cast<unsigned char>(Needle[J]);
            if (std::tolower(A) != std::tolower(B))
            {
                Matched = false;
                break;
            }
        }
        if (Matched)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const char* QueryAttenuationCaption(SimulationSpace::ForceFields::ForceAttenuationMode Mode) noexcept
{
    using namespace SimulationSpace::ForceFields;
    switch (Mode)
    {
    case ForceAttenuationMode::SmoothStep:    return "SmoothStep";
    case ForceAttenuationMode::Linear:        return "Linear";
    case ForceAttenuationMode::InverseSquare: return "InverseSq";
    case ForceAttenuationMode::None:          return "Constant";
    default:                                  return "SmoothStep";
    }
}

[[nodiscard]] const char* QueryForceDomainCaption(SimulationSpace::ForceFields::ForceDomainCategory Domain) noexcept
{
    using namespace SimulationSpace::ForceFields;
    switch (Domain)
    {
    case ForceDomainCategory::VectorField:     return "VectorField";
    case ForceDomainCategory::StreamTransport:       return "StreamTransport";
    case ForceDomainCategory::TurbulenceField: return "Turbulence";
    default:                                   return "ForceField";
    }
}

[[nodiscard]] const char* QueryDurationCaption(SimulationSpace::ForceFields::ForceDurationMode Mode) noexcept
{
    using namespace SimulationSpace::ForceFields;
    switch (Mode)
    {
    case ForceDurationMode::Continuous: return "Continuous";
    case ForceDurationMode::Modulated:  return "Modulated";
    case ForceDurationMode::Impulse:    return "Impulse";
    default:                            return "Continuous";
    }
}

[[nodiscard]] const char* QueryOriginCaption(ContentAssetOrigin Origin) noexcept
{
    switch (Origin)
    {
    case ContentAssetOrigin::Engine:   return "Engine";
    case ContentAssetOrigin::Project:  return "Project";
    case ContentAssetOrigin::Imported: return "Imported";
    default:                           return "All";
    }
}

[[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry ConstructDefaultLawSource(
    SimulationSpace::ForceFields::ForceModel Model)
{
    using namespace SimulationSpace::ForceFields;
    ForceSourceRecordEntry Src{};
    Src.Model           = Model;
    Src.Domain          = QueryDomainForModel(Model);
    Src.Label           = std::string(QueryForceModelDisplayName(Model));
    Src.CentreMeters    = { 0.0f, 0.0f, 6.0f };
    Src.EvaluatedCentreMeters = Src.CentreMeters;
    Src.RadiusMeters    = 20.0f;
    Src.Strength        = 18.0f;
    Src.FalloffExponent = 1.4f;
    Src.Attenuation     = ForceAttenuationMode::SmoothStep;
    Src.Envelope        = ReachEnvelopeCategory::Sphere;

    switch (Model)
    {
    case ForceModel::LinearWind:
        Src.Strength       = 14.5f;
        Src.RadiusMeters   = 26.0f;
        Src.EulerDegrees.Z = 228.0f;
        break;
    case ForceModel::Buoyancy:
        Src.Envelope     = ReachEnvelopeCategory::Column;
        Src.Strength     = 14.0f;
        Src.RadiusMeters = 18.0f;
        break;
    case ForceModel::RadialBurst:
        Src.Strength              = 28.0f;
        Src.RadiusMeters          = 18.0f;
        Src.DurationMode          = ForceDurationMode::Impulse;
        Src.ImpulseDurationSec    = 1.8f;
        Src.FalloffExponent       = 1.2f;
        break;
    case ForceModel::Attractor:
        Src.Strength         = 22.0f;
        Src.CoreRadiusMeters = 3.0f;
        Src.Attenuation      = ForceAttenuationMode::InverseSquare;
        break;
    case ForceModel::Repulsor:
        Src.Strength         = 24.0f;
        Src.CoreRadiusMeters = 3.5f;
        break;
    case ForceModel::AxisAttractor:
        Src.Envelope     = ReachEnvelopeCategory::Column;
        Src.Strength     = 18.0f;
        Src.SwirlRatio   = 0.55f;
        break;
    case ForceModel::Containment:
        Src.Strength         = 24.0f;
        Src.CoreRadiusMeters = 8.0f;
        Src.RadiusMeters     = 22.0f;
        break;
    case ForceModel::RotationalColumn:
        Src.Strength     = 22.0f;
        Src.SwirlRatio   = 1.15f;
        Src.RadiusMeters = 20.0f;
        break;
    case ForceModel::Torque:
        Src.Strength     = 16.0f;
        Src.SwirlRatio   = 1.4f;
        break;
    case ForceModel::Drag:
        Src.Strength        = 12.0f;
        Src.DragCoefficient = 1.15f;
        break;
    case ForceModel::CurlNoise:
        Src.Strength              = 16.0f;
        Src.TurbulenceScaleMeters = 12.0f;
        Src.OctaveCount           = 3u;
        break;
    case ForceModel::AtmosphericGust:
        Src.Strength       = 21.0f;
        Src.DurationMode   = ForceDurationMode::Modulated;
        Src.FrequencyHertz = 0.25f;
        break;
    case ForceModel::VorticityConfinement:
        Src.Strength              = 18.0f;
        Src.TurbulenceScaleMeters = 10.0f;
        break;
    case ForceModel::BrownianNoise:
        Src.Strength              = 14.0f;
        Src.TurbulenceScaleMeters = 8.0f;
        break;
    case ForceModel::LorentzField:
        Src.Strength     = 20.0f;
        Src.SwirlRatio   = 1.1f;
        break;
    case ForceModel::MagneticDipole:
        Src.Strength         = 19.0f;
        Src.CoreRadiusMeters = 3.5f;
        break;
    case ForceModel::FlowStream:
        Src.Strength      = 24.0f;
        Src.RadiusMeters  = 18.0f;
        Src.StreamProfile = StreamTransportProfile::SlipstreamJet;
        break;
    case ForceModel::SplineForce:
        Src.Strength     = 20.0f;
        Src.RadiusMeters = 18.0f;
        break;
    case ForceModel::BoundaryAdhesion:
        Src.Strength         = 15.0f;
        Src.CoreRadiusMeters = 4.0f;
        break;
    default:
        break;
    }
    return Src;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              LIFECYCLE & PRESET LIBRARY
//------------------------------------------------------------------------------------------------------------------------

ForcePresetBrowserPanel::ForcePresetBrowserPanel() noexcept
    : VerticalAxis(Motion.RegisterAxis(0.0, 225.0, 24.0))
{
}

void ForcePresetBrowserPanel::ClearAssets() noexcept
{
    Assets.clear();
    SelectedIdentifier.clear();
}

void ForcePresetBrowserPanel::PopulateDefaultForcePresets()
{
    using namespace SimulationSpace::ForceFields;
    Assets.clear();

    auto AppendPreset = [&](
        const char*            Id,
        const char*            Title,
        const char*            Note,
        ForceSourceRecordEntry Src,
        ContentAssetCategory   Category = ContentAssetCategory::ForcePresets)
    {
        Src.Label  = Title;
        Src.Domain = QueryDomainForModel(Src.Model);

        ContentBrowserAssetEntry Entry{};
        Entry.Identifier                = Id;
        Entry.Title                     = Title;
        Entry.CategoryCaption           = std::string(QueryForceModelDisplayName(Src.Model)) + " · " + QueryForceDomainCaption(Src.Domain);
        char SumBuf[96]{};
        std::snprintf(SumBuf, sizeof(SumBuf), "%.1f m/s · R %.0f m", Src.Strength, Src.RadiusMeters);
        Entry.MetricSummary             = SumBuf;
        Entry.Origin                    = ContentAssetOrigin::Engine;
        Entry.Category                  = Category;
        Entry.FormulaOverride           = Src.FormulaEnabled;
        Entry.PresetDocument.PresetTitle = Title;
        Entry.PresetDocument.AuthorNote  = Note;
        Entry.PresetDocument.ForceSource = std::move(Src);
        Assets.push_back(std::move(Entry));
    };

    // 1. Curated Simulation & Weather .ForceModel Presets
    {
        auto S = ConstructDefaultLawSource(ForceModel::LinearWind);
        S.Strength = 14.5f; S.RadiusMeters = 32.0f; S.EulerDegrees.Z = 232.0f;
        AppendPreset("preset-coastal-crosswind", "Coastal Crosswind", "Steady SW prevailing airflow across test deck.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::RotationalColumn);
        S.Strength = 28.0f; S.RadiusMeters = 24.0f; S.SwirlRatio = 1.15f; S.CoreRadiusMeters = 4.0f;
        AppendPreset("preset-supercell-cyclone", "Supercell Mesocyclone", "High-swirl helical column with inward base inflow.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::RadialBurst);
        S.Strength = 42.0f; S.RadiusMeters = 20.0f; S.DurationMode = ForceDurationMode::Impulse; S.ImpulseDurationSec = 2.0f;
        AppendPreset("preset-shockwave-detonation", "Shockwave Detonation", "Transient outward radial impulse wavefront.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::FlowStream);
        S.Strength = 26.0f; S.RadiusMeters = 17.0f; S.StreamProfile = StreamTransportProfile::SlipstreamJet; S.CentreMeters = { -16.0f, 0.0f, 6.0f };
        AppendPreset("preset-cantilever-jet", "Cantilever Jet Plume", "Focused directional jet corridor with Gaussian profile.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::Buoyancy);
        S.Strength = 16.0f; S.RadiusMeters = 18.0f; S.Envelope = ReachEnvelopeCategory::Column;
        AppendPreset("preset-thermal-updraft", "Thermal Updraft Column", "Rising convective chimney plume (+Z up).", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::CurlNoise);
        S.Strength = 17.5f; S.RadiusMeters = 28.0f; S.TurbulenceScaleMeters = 11.0f; S.OctaveCount = 3u;
        AppendPreset("preset-curl-eddy", "Divergence-Free Curl Eddy", "3-octave incompressible vortex-curl turbulence field.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::Attractor);
        S.Strength = 24.0f; S.RadiusMeters = 22.0f; S.CoreRadiusMeters = 3.5f; S.Attenuation = ForceAttenuationMode::InverseSquare;
        AppendPreset("preset-gravity-sink", "Gravitational Singularity", "Smooth-clamped 1/r^2 inward sink well.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::AtmosphericGust);
        S.Strength = 22.0f; S.RadiusMeters = 30.0f; S.DurationMode = ForceDurationMode::Modulated; S.FrequencyHertz = 0.22f;
        AppendPreset("preset-squall-gust", "Squall Line Wind Gusts", "Periodic atmospheric wind burst across boundary layer.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::Drag);
        S.Strength = 12.0f; S.RadiusMeters = 16.0f; S.DragCoefficient = 1.45f;
        AppendPreset("preset-drag-baffle", "Aerodynamic Drag Baffle", "Local viscous damping zone stabilizing fast particles.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::LorentzField);
        S.Strength = 20.0f; S.RadiusMeters = 20.0f; S.SwirlRatio = 1.25f;
        AppendPreset("preset-lorentz-ring", "Lorentz Cyclotron Ring", "Velocity-cross-B perpendicular helical deflection.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::MagneticDipole);
        S.Strength = 19.0f; S.RadiusMeters = 18.0f; S.CoreRadiusMeters = 3.5f;
        AppendPreset("preset-magnetic-trap", "Magnetic Quadrupole Trap", "Symmetric axial dipole lobes capturing charged trajectories.", std::move(S));
    }
    {
        auto S = ConstructDefaultLawSource(ForceModel::Containment);
        S.Strength = 26.0f; S.RadiusMeters = 24.0f; S.CoreRadiusMeters = 9.0f;
        AppendPreset("preset-containment-shell", "Elastic Containment Shell", "Restoring inward spring shell beyond inner radius.", std::move(S));
    }

    // 2. All 5 Built-in VectorFieldLang Symbolic Formula Presets
    const auto FormulaList = QueryBuiltInFormulaPresets();
    for (std::size_t I = 0u; I < FormulaList.size(); ++I)
    {
        auto S              = ConstructDefaultLawSource(ForceModel::FlowStream);
        S.FormulaEnabled    = true;
        S.FormulaSourceText = FormulaList[I].FormulaSource;
        S.Strength          = 20.0f;
        S.RadiusMeters      = 22.0f;
        char IdBuf[48]{};
        std::snprintf(IdBuf, sizeof(IdBuf), "formula-preset-%zu", I);
        AppendPreset(
            IdBuf,
            FormulaList[I].Title,
            "Compiled VectorFieldLang expression targeting CPU bytecode & SPIR-V GPU evaluation.",
            std::move(S),
            ContentAssetCategory::FormulaPresets);
    }

    // 3. All 19 Fundamental ForceModel Analytical Laws (0..18)
    for (uint32_t Ord = 0u; Ord < static_cast<uint32_t>(ForceModel::Count); ++Ord)
    {
        const ForceModel M = static_cast<ForceModel>(Ord);
        auto S             = ConstructDefaultLawSource(M);
        char IdBuf[48]{};
        std::snprintf(IdBuf, sizeof(IdBuf), "forcelaw-%02u", Ord);
        AppendPreset(
            IdBuf,
            QueryForceModelDisplayName(M),
            "Analytical C++20 / .slang ForceModel template ready for immediate scene construction.",
            std::move(S),
            ContentAssetCategory::ForceModels);
    }

    if (!Assets.empty())
    {
        SelectedIdentifier = Assets.front().Identifier;
    }
    StatusBannerText = "Loaded ForceModel presets, 19 analytical laws, and VectorFieldLang expressions.";
}

void ForcePresetBrowserPanel::AddProjectPreset(
    const Codecs::ForceModelPresetDocument& Document,
    ContentAssetOrigin                      Origin)
{
    using namespace SimulationSpace::ForceFields;
    ContentBrowserAssetEntry Entry{};
    char IdBuf[64]{};
    std::snprintf(IdBuf, sizeof(IdBuf), "custom-preset-%zu", Assets.size());
    Entry.Identifier                  = IdBuf;
    Entry.Title                       = Document.PresetTitle;
    Entry.CategoryCaption             = std::string(QueryForceModelDisplayName(Document.ForceSource.Model)) + " · " +
                                        QueryForceDomainCaption(Document.ForceSource.Domain);
    char SumBuf[96]{};
    std::snprintf(SumBuf, sizeof(SumBuf), "%.1f m/s · R %.0f m", Document.ForceSource.Strength, Document.ForceSource.RadiusMeters);
    Entry.MetricSummary               = SumBuf;
    Entry.Origin                      = Origin;
    Entry.Category                    = Document.ForceSource.FormulaEnabled
                                            ? ContentAssetCategory::FormulaPresets
                                            : ContentAssetCategory::ForcePresets;
    Entry.FormulaOverride             = Document.ForceSource.FormulaEnabled;
    Entry.PresetDocument              = Document;
    SelectedIdentifier                = Entry.Identifier;
    Assets.insert(Assets.begin(), std::move(Entry));
    StatusBannerText                  = "Saved preset '" + Document.PresetTitle + "' to Content Browser.";
}

uint32_t ForcePresetBrowserPanel::LoadForcePresetsFromDirectory(const std::filesystem::path& DirectoryPath)
{
    std::error_code Ec;
    if (!std::filesystem::exists(DirectoryPath, Ec) || !std::filesystem::is_directory(DirectoryPath, Ec))
    {
        return 0u;
    }

    uint32_t LoadedCount = 0u;
    for (const auto& DirEntry : std::filesystem::directory_iterator(DirectoryPath, Ec))
    {
        if (!DirEntry.is_regular_file())
        {
            continue;
        }
        const auto Ext = DirEntry.path().extension().string();
        if (Ext != ".ForceModel" && Ext != ".forcemodel")
        {
            continue;
        }
        Codecs::ForceModelPresetDocument Decoded{};
        std::string Diag;
        if (Codecs::ForceModelPresetDecoder::DecodeFromFile(DirEntry.path(), Decoded, Diag))
        {
            AddProjectPreset(Decoded, ContentAssetOrigin::Imported);
            ++LoadedCount;
        }
    }
    return LoadedCount;
}

void ForcePresetBrowserPanel::ConfigureOpen(bool OpenRequested, bool Immediate) noexcept
{
    DrawerOpen = OpenRequested;
    const double Target = DrawerOpen ? 1.0 : 0.0;
    if (Immediate)
    {
        Motion.Axis(VerticalAxis).ConfigurePosition(Target);
    }
    else
    {
        Motion.Axis(VerticalAxis).Depart(Target);
    }
}

void ForcePresetBrowserPanel::SnapOpen(bool OpenRequested) noexcept
{
    ConfigureOpen(OpenRequested, true);
}

void ForcePresetBrowserPanel::ConfigureOriginFilter(ContentAssetOrigin Origin) noexcept
{
    OriginFilter      = Origin;
    GridScrollOffsetY = 0.0f;
}

void ForcePresetBrowserPanel::ConfigureCategoryFilter(ContentAssetCategory Category) noexcept
{
    CategoryFilter    = Category;
    GridScrollOffsetY = 0.0f;
}

void ForcePresetBrowserPanel::ConfigureViewLayout(ContentBrowserViewLayout Layout) noexcept
{
    ViewLayout = Layout;
}

void ForcePresetBrowserPanel::ConfigureSelectedAssetId(std::string_view Identifier)
{
    SelectedIdentifier.assign(Identifier.begin(), Identifier.end());
}

void ForcePresetBrowserPanel::ConfigureConstructPresetCallback(std::function<void(const Codecs::ForceModelPresetDocument&)> Callback)
{
    ConstructPresetCallback = std::move(Callback);
}

void ForcePresetBrowserPanel::ConfigureSaveSelectedCallback(std::function<bool(Codecs::ForceModelPresetDocument&)> Callback)
{
    SaveSelectionCallback = std::move(Callback);
}

float ForcePresetBrowserPanel::QueryDrawerProgress() const noexcept
{
    return std::clamp(static_cast<float>(Motion.Axis(VerticalAxis).Current), 0.0f, 1.0f);
}

float ForcePresetBrowserPanel::QueryDrawerMaximumHeight(Rectangle2D HostBounds) const noexcept
{
    return std::clamp(HostBounds.Height * 0.56f, 340.0f, 540.0f);
}

Rectangle2D ForcePresetBrowserPanel::QueryBottomHandleBounds(Rectangle2D HostBounds, float DrawerHeightPx) const noexcept
{
    const float TabW = std::min(220.0f, HostBounds.Width * 0.45f);
    const float TabH = 22.0f;
    return { HostBounds.Center().X - TabW * 0.5f, HostBounds.Bottom() - DrawerHeightPx - TabH, TabW, TabH };
}

const ContentBrowserAssetEntry* ForcePresetBrowserPanel::QuerySelectedAsset() const noexcept
{
    for (const auto& Entry : Assets)
    {
        if (Entry.Identifier == SelectedIdentifier)
        {
            return &Entry;
        }
    }
    return Assets.empty() ? nullptr : &Assets.front();
}

std::vector<uint32_t> ForcePresetBrowserPanel::QueryVisibleAssetIndices() const
{
    std::vector<uint32_t> Out;
    Out.reserve(Assets.size());
    const std::string_view Query(SearchEditor.Text, SearchEditor.Length);
    for (uint32_t I = 0u; I < static_cast<uint32_t>(Assets.size()); ++I)
    {
        const auto& Entry = Assets[I];
        if (OriginFilter != ContentAssetOrigin::All && Entry.Origin != OriginFilter)
        {
            continue;
        }
        if (CategoryFilter != ContentAssetCategory::All && Entry.Category != CategoryFilter)
        {
            continue;
        }
        if (!TextContainsCaseInsensitive(Entry.Title, Query) &&
            !TextContainsCaseInsensitive(Entry.CategoryCaption, Query) &&
            !TextContainsCaseInsensitive(Entry.MetricSummary, Query))
        {
            continue;
        }
        Out.push_back(I);
    }
    return Out;
}

bool ForcePresetBrowserPanel::ConstructSelectedPreset()
{
    const ContentBrowserAssetEntry* Sel = QuerySelectedAsset();
    if (Sel == nullptr || !ConstructPresetCallback)
    {
        return false;
    }
    ConstructPresetCallback(Sel->PresetDocument);
    StatusBannerText = "Constructed '" + Sel->Title + "' in active scene.";
    return true;
}

bool ForcePresetBrowserPanel::ConstructPresetByIndex(uint32_t VisibleIndex)
{
    const auto Visible = QueryVisibleAssetIndices();
    if (VisibleIndex >= Visible.size() || !ConstructPresetCallback)
    {
        return false;
    }
    const auto& Entry = Assets[Visible[VisibleIndex]];
    SelectedIdentifier = Entry.Identifier;
    ConstructPresetCallback(Entry.PresetDocument);
    StatusBannerText = "Constructed '" + Entry.Title + "' in active scene.";
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           RENDERING & INTERACTION
//------------------------------------------------------------------------------------------------------------------------

void ForcePresetBrowserPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             HostBoundsLogical,
    const PaneInputReading& Input,
    double                  Δt)
{
    if (HostBoundsLogical.Width <= 40.0f || HostBoundsLogical.Height <= 40.0f)
    {
        return;
    }

    (void)Motion.Integrate(std::clamp(Δt, 0.0, 0.05));
    LastDrawerMaxH = QueryDrawerMaximumHeight(HostBoundsLogical);

    const float Progress       = QueryDrawerProgress();
    const float DrawerHeightPx = LastDrawerMaxH * Progress;
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    // Escape closes drawer smoothly when open
    if (DrawerOpen && Input.EscapePressed)
    {
        ConfigureOpen(false, false);
    }

    // 1. Bottom Trapezoidal Pull-Up Handle Tab ("Content Browser") matching ProjectZeroEditorOverview
    const Rectangle2D HandleBounds = QueryBottomHandleBounds(HostBoundsLogical, DrawerHeightPx);
    {
        const float Inset = HandleBounds.Width * 0.08f;
        const std::array<Point2D, 4> Trap = {{
            { HandleBounds.X + Inset,        HandleBounds.Y },
            { HandleBounds.Right() - Inset,  HandleBounds.Y },
            { HandleBounds.Right(),          HandleBounds.Bottom() },
            { HandleBounds.X,                HandleBounds.Bottom() },
        }};
        const bool HandleHov = HandleBounds.Contains(Cursor);
        Surface.FillPolygon(Trap, (HandleHov || DrawerOpen) ? DrawerColors::HandleHoverFill : DrawerColors::HandleFill);
        Surface.StrokePolyline(Trap, ColorRgba{ 255, 255, 255, 28 }, 1.0f, true);

        const float CapW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 10.5f, "Content Browser");
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { HandleBounds.Center().X - CapW * 0.5f, HandleBounds.Y + 5.0f },
            10.5f,
            (HandleHov || DrawerOpen) ? DrawerColors::InkPrimary : ColorRgba{ 176, 176, 178, 255 },
            "Content Browser");

        if (HandleHov && Input.PrimaryPointer.Pressed)
        {
            ConfigureOpen(!DrawerOpen, false);
        }
    }

    if (DrawerHeightPx <= 2.0f)
    {
        return;
    }

    // Forward typing ONLY while drawer is active and search is focused
    WorkspaceAppearance::ForwardInputToTextBuffer(SearchEditor, Input, Δt);

    const Rectangle2D Drawer{
        HostBoundsLogical.X,
        HostBoundsLogical.Bottom() - DrawerHeightPx,
        HostBoundsLogical.Width,
        DrawerHeightPx
    };

    Surface.PushClip(Drawer);
    Surface.FillRectangle(Drawer, DrawerColors::DrawerBg);
    Surface.StrokeLine({ Drawer.X, Drawer.Y }, { Drawer.Right(), Drawer.Y }, ColorRgba{ 255, 255, 255, 36 }, 1.0f);

    // Top centered pull-grip bar
    const Rectangle2D GripBar{ Drawer.Center().X - 24.0f, Drawer.Y + 6.0f, 48.0f, 4.0f };
    Surface.FillRectangle(GripBar, ColorRgba{ 118, 118, 122, 220 }, 2.0f);
    if (Rectangle2D{ Drawer.Center().X - 70.0f, Drawer.Y, 140.0f, 18.0f }.Contains(Cursor) && Input.PrimaryPointer.Pressed)
    {
        ConfigureOpen(false, false);
    }

    // Header Row (46px)
    const float HeadY = Drawer.Y + 14.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Drawer.X + 22.0f, HeadY + 4.0f }, 14.5f, DrawerColors::InkPrimary, "Content Browser");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Drawer.X + 146.0f, HeadY + 7.0f }, 10.5f, DrawerColors::InkMuted, "ForceModel Presets & Libraries");

    const Rectangle2D CloseBtn{ Drawer.Right() - 42.0f, HeadY, 28.0f, 28.0f };
    const bool CloseHov = CloseBtn.Contains(Cursor);
    Surface.FillRectangle(CloseBtn, CloseHov ? ColorRgba{ 48, 48, 52, 255 } : DrawerColors::PanelWellBg, 14.0f);
    Surface.StrokeRectangle(CloseBtn, DrawerColors::HairlineBorder, 14.0f, 1.0f);
    VectorIconRepository::ConstructControl(Surface, ControlIcon::CloseCross, VectorIconPlacement{ CloseBtn.X + 7.5f, CloseBtn.Y + 7.5f, 13.0f, DrawerColors::InkSecondary, 1.75f });
    if (CloseHov && Input.PrimaryPointer.Pressed)
    {
        ConfigureOpen(false, false);
    }

    if (SaveSelectionCallback)
    {
        const Rectangle2D SaveBtn{ CloseBtn.X - 156.0f, HeadY + 1.0f, 146.0f, 26.0f };
        const bool SaveHov = SaveBtn.Contains(Cursor);
        Surface.FillRectangle(SaveBtn, SaveHov ? ColorRgba{ 44, 42, 34, 255 } : DrawerColors::PanelWellBg, 13.0f);
        Surface.StrokeRectangle(SaveBtn, SaveHov ? DrawerColors::AmberAccent : DrawerColors::SubtleBorder, 13.0f, 1.0f);
        const float SaveW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, "+ Save Selected Force");
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { SaveBtn.Center().X - SaveW * 0.5f, SaveBtn.Y + 7.0f },
            10.0f,
            SaveHov ? DrawerColors::AmberAccent : DrawerColors::InkPrimary,
            "+ Save Selected Force");
        if (SaveHov && Input.PrimaryPointer.Pressed)
        {
            Codecs::ForceModelPresetDocument Captured{};
            if (SaveSelectionCallback(Captured))
            {
                AddProjectPreset(Captured, ContentAssetOrigin::Project);
            }
            else
            {
                StatusBannerText = "Select a force entry first to save a .ForceModel preset.";
            }
        }
    }

    // Tool Strip (Origin Tabs + Search + Grid/List switch)
    const float ToolsY = HeadY + 36.0f;
    const std::array<std::pair<const char*, ContentAssetOrigin>, 4> Origins = {{
        { "All",      ContentAssetOrigin::All      },
        { "Engine",   ContentAssetOrigin::Engine   },
        { "Project",  ContentAssetOrigin::Project  },
        { "Imported", ContentAssetOrigin::Imported },
    }};
    float OrigX = Drawer.X + 22.0f;
    for (const auto& Orig : Origins)
    {
        const float W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.0f, Orig.first) + 22.0f;
        const Rectangle2D ChipBtn{ OrigX, ToolsY, W, 26.0f };
        const bool Active = (OriginFilter == Orig.second);
        const bool Hov    = ChipBtn.Contains(Cursor);
        Surface.FillRectangle(ChipBtn, Active ? ColorRgba{ 230, 228, 222, 255 } : (Hov ? ColorRgba{ 36, 36, 40, 255 } : DrawerColors::PanelWellBg), 13.0f);
        Surface.StrokeRectangle(ChipBtn, Active ? ColorRgba{ 230, 228, 222, 255 } : DrawerColors::HairlineBorder, 13.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { ChipBtn.X + 11.0f, ChipBtn.Y + 7.0f },
            10.0f,
            Active ? DrawerColors::ConstructInk : DrawerColors::InkSecondary,
            Orig.first);
        if (Hov && Input.PrimaryPointer.Pressed)
        {
            ConfigureOriginFilter(Orig.second);
        }
        OrigX += W + 6.0f;
    }

    const Rectangle2D ListBtn{ Drawer.Right() - 48.0f, ToolsY, 26.0f, 26.0f };
    const Rectangle2D GridBtn{ ListBtn.X - 30.0f, ToolsY, 26.0f, 26.0f };
    auto RenderLayoutSwitch = [&](Rectangle2D Btn, ContentBrowserViewLayout Mode, const char* SymbolText)
    {
        const bool Active = (ViewLayout == Mode);
        const bool Hov    = Btn.Contains(Cursor);
        Surface.FillRectangle(Btn, Active ? ColorRgba{ 52, 52, 56, 255 } : (Hov ? ColorRgba{ 36, 36, 40, 255 } : DrawerColors::PanelWellBg), 8.0f);
        Surface.StrokeRectangle(Btn, Active ? DrawerColors::SubtleBorder : DrawerColors::HairlineBorder, 8.0f, 1.0f);
        const float GW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.0f, SymbolText);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Btn.Center().X - GW * 0.5f, Btn.Y + 6.5f }, 11.0f, Active ? DrawerColors::InkPrimary : DrawerColors::InkSecondary, SymbolText);
        if (Hov && Input.PrimaryPointer.Pressed)
        {
            ConfigureViewLayout(Mode);
        }
    };
    RenderLayoutSwitch(GridBtn, ContentBrowserViewLayout::Grid, "::");
    RenderLayoutSwitch(ListBtn, ContentBrowserViewLayout::List, "==");

    const float SearchLeft = OrigX + 8.0f;
    const float SearchW    = std::max(120.0f, GridBtn.X - 10.0f - SearchLeft);
    const Rectangle2D SearchBox{ SearchLeft, ToolsY, SearchW, 26.0f };
    Surface.FillRectangle(SearchBox, ColorRgba{ 12, 12, 14, 255 }, 13.0f);
    Surface.StrokeRectangle(SearchBox, SearchEditor.Active ? DrawerColors::AmberAccent : DrawerColors::HairlineBorder, 13.0f, 1.0f);
    VectorIconRepository::ConstructControl(Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchBox.X + 10.0f, SearchBox.Y + 6.5f, 13.0f, DrawerColors::InkMuted, 1.6f });
    const std::string_view SearchText(SearchEditor.Text, SearchEditor.Length);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { SearchBox.X + 30.0f, SearchBox.Y + 6.5f },
        SearchBox.Width - 40.0f,
        10.0f,
        SearchText.empty() ? DrawerColors::InkMuted : DrawerColors::InkPrimary,
        SearchText.empty() ? "Search force presets, 19 ForceModel laws & formulas..." : SearchText);
    if (SearchBox.Contains(Cursor) && Input.PrimaryPointer.Pressed)
    {
        SearchEditor.Active = true;
    }

    // 3-Column Body Area
    const float BodyTopY  = ToolsY + 34.0f;
    const float FooterH   = 24.0f;
    const float BodyH     = std::max(40.0f, Drawer.Bottom() - FooterH - BodyTopY);
    const float LeftRailW = 176.0f;
    const float PreviewW  = 292.0f;
    const float GridAreaW = std::max(140.0f, Drawer.Width - LeftRailW - PreviewW);

    Surface.StrokeLine({ Drawer.X, BodyTopY }, { Drawer.Right(), BodyTopY }, DrawerColors::HairlineBorder, 1.0f);
    Surface.StrokeLine({ Drawer.X + LeftRailW, BodyTopY }, { Drawer.X + LeftRailW, Drawer.Bottom() - FooterH }, DrawerColors::HairlineBorder, 1.0f);
    Surface.StrokeLine({ Drawer.Right() - PreviewW, BodyTopY }, { Drawer.Right() - PreviewW, Drawer.Bottom() - FooterH }, DrawerColors::HairlineBorder, 1.0f);

    // 3a. Left Category Rail
    const std::array<std::pair<const char*, ContentAssetCategory>, 4> Categories = {{
        { "All Presets & Laws", ContentAssetCategory::All            },
        { "Force Presets",      ContentAssetCategory::ForcePresets   },
        { "Force Models (19)",    ContentAssetCategory::ForceModels      },
        { "Formulas",           ContentAssetCategory::FormulaPresets },
    }};
    for (std::size_t CIdx = 0u; CIdx < Categories.size(); ++CIdx)
    {
        const Rectangle2D RowRect{ Drawer.X + 12.0f, BodyTopY + 12.0f + static_cast<float>(CIdx) * 32.0f, LeftRailW - 24.0f, 28.0f };
        const bool Active = (CategoryFilter == Categories[CIdx].second);
        const bool Hov    = RowRect.Contains(Cursor);
        if (Active || Hov)
        {
            Surface.FillRectangle(RowRect, Active ? ColorRgba{ 42, 42, 46, 255 } : ColorRgba{ 28, 28, 30, 255 }, 8.0f);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            Active ? FontWeightClass::Medium : FontWeightClass::Regular,
            { RowRect.X + 10.0f, RowRect.Y + 7.5f },
            10.5f,
            Active ? DrawerColors::InkPrimary : DrawerColors::InkSecondary,
            Categories[CIdx].first);

        uint32_t Cnt = 0u;
        for (const auto& A : Assets)
        {
            if ((OriginFilter == ContentAssetOrigin::All || A.Origin == OriginFilter) &&
                (Categories[CIdx].second == ContentAssetCategory::All || A.Category == Categories[CIdx].second))
            {
                ++Cnt;
            }
        }
        char CntBuf[16]{};
        std::snprintf(CntBuf, sizeof(CntBuf), "%u", Cnt);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { RowRect.Right() - 8.0f, RowRect.Y + 8.0f },
            9.5f,
            Active ? DrawerColors::AmberAccent : DrawerColors::InkMuted,
            CntBuf);

        if (Hov && Input.PrimaryPointer.Pressed)
        {
            ConfigureCategoryFilter(Categories[CIdx].second);
        }
    }

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Drawer.X + 18.0f, Drawer.Bottom() - FooterH - 42.0f },
        LeftRailW - 36.0f,
        9.0f,
        DrawerColors::InkMuted,
        "Single click inspects.\nDouble-click spawns in scene.");

    // 3b. Center Results Grid / List
    const auto VisibleIndices = QueryVisibleAssetIndices();
    const Rectangle2D CenterHeader{ Drawer.X + LeftRailW, BodyTopY, GridAreaW, 30.0f };
    const char* CategoryHeading = "All Force Assets";
    switch (CategoryFilter)
    {
    case ContentAssetCategory::ForcePresets:   CategoryHeading = "Force Presets (.ForceModel)"; break;
    case ContentAssetCategory::ForceModels:      CategoryHeading = "All 19 Force Models"; break;
    case ContentAssetCategory::FormulaPresets: CategoryHeading = "VectorFieldLang Formulas"; break;
    default: break;
    }
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { CenterHeader.X + 14.0f, CenterHeader.Y + 8.0f }, 11.0f, DrawerColors::InkPrimary, CategoryHeading);
    char ResBuf[32]{};
    std::snprintf(ResBuf, sizeof(ResBuf), "%zu results", VisibleIndices.size());
    WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { CenterHeader.Right() - 14.0f, CenterHeader.Y + 9.0f }, 9.5f, DrawerColors::InkMuted, ResBuf);

    const Rectangle2D ScrollRect{ Drawer.X + LeftRailW + 10.0f, CenterHeader.Bottom(), GridAreaW - 26.0f, std::max(20.0f, BodyH - 34.0f) };
    const float       Gap       = 10.0f;
    const uint32_t    Cols      = (ViewLayout == ContentBrowserViewLayout::List)
                                    ? 1u
                                    : std::max(1u, static_cast<uint32_t>((ScrollRect.Width + Gap) / (152.0f + Gap)));
    const float       TileW     = (ScrollRect.Width - Gap * static_cast<float>(Cols - 1u)) / static_cast<float>(Cols);
    const float       TileH     = (ViewLayout == ContentBrowserViewLayout::List) ? 46.0f : 138.0f;
    const uint32_t    Rows      = (static_cast<uint32_t>(VisibleIndices.size()) + Cols - 1u) / Cols;
    const float       ContentH  = static_cast<float>(Rows) * (TileH + Gap);
    const float       MaxScroll = std::max(0.0f, ContentH - ScrollRect.Height + 6.0f);

    if (ScrollRect.Contains(Cursor) && std::fabs(Input.WheelDeltaY) > 0.001f)
    {
        GridScrollOffsetY -= Input.WheelDeltaY * 42.0f;
    }
    GridScrollOffsetY = std::clamp(GridScrollOffsetY, 0.0f, MaxScroll);

    Surface.PushClip(ScrollRect);
    for (std::size_t VIdx = 0u; VIdx < VisibleIndices.size(); ++VIdx)
    {
        const auto& Asset = Assets[VisibleIndices[VIdx]];
        const uint32_t Col = static_cast<uint32_t>(VIdx) % Cols;
        const uint32_t Row = static_cast<uint32_t>(VIdx) / Cols;
        const Rectangle2D Tile{
            ScrollRect.X + static_cast<float>(Col) * (TileW + Gap),
            ScrollRect.Y + 4.0f + static_cast<float>(Row) * (TileH + Gap) - GridScrollOffsetY,
            TileW,
            TileH
        };
        if (Tile.Bottom() < ScrollRect.Y || Tile.Y > ScrollRect.Bottom())
        {
            continue;
        }

        const bool Selected = (Asset.Identifier == SelectedIdentifier) || (SelectedIdentifier.empty() && VIdx == 0u);
        const bool Hovered  = ScrollRect.Contains(Cursor) && Tile.Contains(Cursor);

        Surface.FillRectangle(Tile, Hovered ? DrawerColors::TileHoverBg : DrawerColors::TileBg, 10.0f);
        Surface.StrokeRectangle(Tile, Selected ? DrawerColors::AmberAccent : DrawerColors::HairlineBorder, 10.0f, Selected ? 1.5f : 1.0f);

        const uint32_t LawOrd = static_cast<uint32_t>(Asset.PresetDocument.ForceSource.Model);
        const ColorRgba IconTint = Asset.FormulaOverride
            ? DrawerColors::AmberAccent
            : WorkspaceAppearance::QueryForceModelTint(LawOrd);

        if (ViewLayout == ContentBrowserViewLayout::Grid)
        {
            const Rectangle2D ThumbWell{ Tile.X + 7.0f, Tile.Y + 7.0f, Tile.Width - 14.0f, 76.0f };
            Surface.FillRectangle(ThumbWell, DrawerColors::TileThumbBg, 8.0f);
            WorkspaceAppearance::ConstructForceModelIcon(
                Surface,
                LawOrd,
                { ThumbWell.Center().X - 20.0f, ThumbWell.Center().Y - 20.0f },
                40.0f,
                IconTint,
                1.0f);

            const char* TagStr = Asset.FormulaOverride ? "fx" : QueryOriginCaption(Asset.Origin);
            WorkspaceAppearance::ConstructRightAlignedText(
                Surface,
                FontWeightClass::Medium,
                { ThumbWell.Right() - 6.0f, ThumbWell.Y + 5.0f },
                8.5f,
                Asset.FormulaOverride ? DrawerColors::AmberAccent : ColorRgba{ 148, 150, 158, 255 },
                TagStr);

            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Medium,
                { Tile.X + 9.0f, ThumbWell.Bottom() + 8.0f },
                Tile.Width - 18.0f,
                10.5f,
                DrawerColors::InkPrimary,
                Asset.Title);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { Tile.X + 9.0f, ThumbWell.Bottom() + 24.0f },
                Tile.Width - 18.0f,
                9.0f,
                ColorRgba{ 152, 154, 162, 255 },
                Asset.MetricSummary);
        }
        else
        {
            WorkspaceAppearance::ConstructForceModelIcon(Surface, LawOrd, { Tile.X + 12.0f, Tile.Center().Y - 11.0f }, 22.0f, IconTint, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { Tile.X + 44.0f, Tile.Y + 8.0f }, Tile.Width * 0.48f, 10.5f, DrawerColors::InkPrimary, Asset.Title);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Tile.X + 44.0f, Tile.Y + 24.0f }, Tile.Width * 0.48f, 9.0f, ColorRgba{ 152, 154, 162, 255 }, Asset.CategoryCaption);
            WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { Tile.Right() - 12.0f, Tile.Y + 15.0f }, 9.5f, DrawerColors::InkSecondary, Asset.MetricSummary);
        }

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            SelectedIdentifier = Asset.Identifier;
            if (Input.DoubleClicked && ConstructPresetCallback)
            {
                ConstructPresetCallback(Asset.PresetDocument);
                StatusBannerText = "Constructed '" + Asset.Title + "' in active scene.";
            }
        }
    }
    Surface.PopClip();

    if (MaxScroll > 0.5f)
    {
        const Rectangle2D TrackRect{ ScrollRect.Right() + 4.0f, ScrollRect.Y + 4.0f, 4.0f, ScrollRect.Height - 8.0f };
        Surface.FillRectangle(TrackRect, ColorRgba{ 32, 32, 36, 220 }, 2.0f);
        const float Ratio   = std::clamp(ScrollRect.Height / std::max(ScrollRect.Height, ContentH), 0.14f, 0.9f);
        const float ThumbH  = TrackRect.Height * Ratio;
        const float ScrollT = std::clamp(GridScrollOffsetY / MaxScroll, 0.0f, 1.0f);
        const float ThumbY  = TrackRect.Y + (TrackRect.Height - ThumbH) * ScrollT;
        Surface.FillRectangle({ TrackRect.X, ThumbY, TrackRect.Width, ThumbH }, ColorRgba{ 126, 128, 136, 255 }, 2.0f);
    }

    // 3c. Right Detail Preview Pane ("ASSET DETAILS" + Big Icon + Metrics + "+ Construct in Scene")
    const Rectangle2D DetailPane{ Drawer.Right() - PreviewW, BodyTopY, PreviewW, BodyH };
    Surface.FillRectangle(DetailPane, DrawerColors::PreviewBg);
    Surface.PushClip(DetailPane);

    const float Pad = 16.0f;
    float       DY  = DetailPane.Y + 14.0f;
    const float DW  = DetailPane.Width - Pad * 2.0f;

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { DetailPane.X + Pad, DY }, 8.5f, DrawerColors::InkMuted, "ASSET DETAILS");

    if (const ContentBrowserAssetEntry* Sel = QuerySelectedAsset())
    {
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Medium, { DetailPane.Right() - Pad, DY }, 8.5f, DrawerColors::AmberAccent, QueryOriginCaption(Sel->Origin));
        DY += 14.0f;

        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { DetailPane.X + Pad, DY }, DW, 14.0f, DrawerColors::InkPrimary, Sel->Title);
        DY += 22.0f;

        const auto& Src      = Sel->PresetDocument.ForceSource;
        const uint32_t Ord   = static_cast<uint32_t>(Src.Model);
        const ColorRgba Tint = Sel->FormulaOverride ? DrawerColors::AmberAccent : WorkspaceAppearance::QueryForceModelTint(Ord);

        const Rectangle2D HeroWell{ DetailPane.X + Pad, DY, DW, 86.0f };
        Surface.FillRectangle(HeroWell, DrawerColors::TileThumbBg, 10.0f);
        Surface.StrokeRectangle(HeroWell, DrawerColors::HairlineBorder, 10.0f, 1.0f);
        WorkspaceAppearance::ConstructForceModelIcon(Surface, Ord, { HeroWell.Center().X - 24.0f, HeroWell.Y + 11.0f }, 48.0f, Tint, 1.0f);
        const float CapW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, Sel->CategoryCaption);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { HeroWell.Center().X - std::min(DW - 12.0f, CapW) * 0.5f, HeroWell.Bottom() - 20.0f },
            DW - 12.0f,
            9.0f,
            Tint,
            Sel->CategoryCaption);
        DY += 94.0f;

        // 2x2 Metrics Grid
        const float CellGap = 6.0f;
        const float CellW   = (DW - CellGap) * 0.5f;
        char StrBuf[24]{}, RadBuf[24]{};
        std::snprintf(StrBuf, sizeof(StrBuf), "%.1f m/s", Src.Strength);
        std::snprintf(RadBuf, sizeof(RadBuf), "%.0f m", Src.RadiusMeters);
        const std::array<std::pair<const char*, const char*>, 4> Metrics = {{
            { "STRENGTH",    StrBuf },
            { "RADIUS",      RadBuf },
            { "ATTENUATION", QueryAttenuationCaption(Src.Attenuation) },
            { "DURATION",    QueryDurationCaption(Src.DurationMode) },
        }};
        for (std::size_t M = 0u; M < Metrics.size(); ++M)
        {
            const Rectangle2D MBox{
                DetailPane.X + Pad + static_cast<float>(M % 2u) * (CellW + CellGap),
                DY + static_cast<float>(M / 2u) * 40.0f,
                CellW,
                35.0f
            };
            Surface.FillRectangle(MBox, DrawerColors::TileBg, 8.0f);
            Surface.StrokeRectangle(MBox, DrawerColors::HairlineBorder, 8.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { MBox.X + 8.0f, MBox.Y + 5.0f }, 7.5f, DrawerColors::InkMuted, Metrics[M].first);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { MBox.X + 8.0f, MBox.Y + 16.5f }, MBox.Width - 12.0f, 9.5f, DrawerColors::InkPrimary, Metrics[M].second);
        }
        DY += 86.0f;

        const std::string_view NoteStr = Src.FormulaEnabled && !Src.FormulaSourceText.empty()
            ? std::string_view(Src.FormulaSourceText)
            : std::string_view(Sel->PresetDocument.AuthorNote);
        const Rectangle2D NoteBox{ DetailPane.X + Pad, DY, DW, std::max(34.0f, DetailPane.Bottom() - 50.0f - DY) };
        if (NoteBox.Height >= 34.0f)
        {
            Surface.FillRectangle(NoteBox, ColorRgba{ 14, 14, 16, 255 }, 8.0f);
            Surface.StrokeRectangle(NoteBox, DrawerColors::HairlineBorder, 8.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::SemiBold,
                { NoteBox.X + 9.0f, NoteBox.Y + 7.0f },
                7.5f,
                Src.FormulaEnabled ? DrawerColors::AmberAccent : DrawerColors::InkMuted,
                Src.FormulaEnabled ? "VECTORFIELDLANG EXPRESSION" : "AUTHOR NOTES");
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { NoteBox.X + 9.0f, NoteBox.Y + 21.0f },
                NoteBox.Width - 18.0f,
                9.5f,
                Src.FormulaEnabled ? DrawerColors::TealFormula : ColorRgba{ 204, 206, 212, 255 },
                NoteStr.empty() ? "Ready to instantiate into active scene." : NoteStr,
                Src.FormulaEnabled);
        }

        const Rectangle2D ConstructBtn{ DetailPane.X + Pad, DetailPane.Bottom() - 42.0f, DW, 30.0f };
        const bool BtnHov = ConstructBtn.Contains(Cursor);
        Surface.FillRectangle(ConstructBtn, BtnHov ? ColorRgba{ 248, 246, 240, 255 } : DrawerColors::ConstructCream, 15.0f);
        Surface.StrokeRectangle(ConstructBtn, ColorRgba{ 245, 243, 236, 255 }, 15.0f, 1.0f);
        const char* BtnText = "+ Construct in Scene";
        const float BtnTextW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, BtnText);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { ConstructBtn.Center().X - BtnTextW * 0.5f, ConstructBtn.Y + 8.0f },
            10.5f,
            DrawerColors::ConstructInk,
            BtnText);
        if (BtnHov && Input.PrimaryPointer.Pressed)
        {
            (void)ConstructSelectedPreset();
        }
    }
    Surface.PopClip();

    // Footer status strip
    const Rectangle2D FooterRect{ Drawer.X, Drawer.Bottom() - FooterH, Drawer.Width, FooterH };
    Surface.FillRectangle(FooterRect, ColorRgba{ 13, 13, 15, 255 });
    Surface.StrokeLine({ FooterRect.X, FooterRect.Y }, { FooterRect.Right(), FooterRect.Y }, DrawerColors::HairlineBorder, 1.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { FooterRect.X + 18.0f, FooterRect.Y + 6.0f },
        FooterRect.Width - 36.0f,
        9.0f,
        DrawerColors::InkMuted,
        StatusBannerText);

    Surface.PopClip();
}

void ForcePresetBrowserPanel::PresentInImGuiHost(double Δt, float ScaleFactor)
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const float SafeScale = std::max(0.25f, ScaleFactor);
    const ImGuiIO& Io     = ImGui::GetIO();
    const Rectangle2D ScreenBounds{ 0.0f, 0.0f, std::max(1.0f, Io.DisplaySize.x), std::max(1.0f, Io.DisplaySize.y) };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, ScreenBounds.Width / SafeScale, ScreenBounds.Height / SafeScale };

    PixelSpace ForegroundSurface(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(ForegroundSurface, LogicalBounds, Input, Δt);
}

} // namespace Frontier
