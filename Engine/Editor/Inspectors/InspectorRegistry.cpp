/*============================================================================================================================================
                                                          INSPECTORREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Implementation of generic InspectorRegistry dispatching any Outliner entry to its standalone Inspector with dynamic breadcrumbs

#include "Engine/Editor/Inspectors/InspectorRegistry.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Lifecycle/ThoughtLog.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace Frontier
{

namespace
{

enum class ResolvedInspectorTarget : uint8_t
{
    Folder           = 0u,
    Atmosphere       = 1u,
    Solar            = 2u,
    Cloud            = 3u,
    LocalCloud       = 4u,
    Lunar            = 5u,
    Starfield        = 6u,
    Wind             = 7u,
    HeightFog        = 8u,
    AtmosphericFog   = 9u,
    LocalFog         = 10u,
    GodRays          = 11u,
    Precipitation    = 12u,
    Rainbow          = 13u,
    Camera           = 14u,
    CameraSettings   = 15u,
    LensFlare        = 16u,
    MotionBlur       = 17u,
    PointLight         = 18u,
    SpotLight          = 19u,
    IesLight           = 20u,
    AreaLight          = 21u,
    TubeLight          = 22u,
    LedLight           = 23u,
    LedStrip           = 24u,
    EnvironmentLight   = 25u,
    PostProcess        = 26u,
    Exposure           = 27u,
    AnalyticPrimitive  = 28u,
    NativeGeometry     = 29u,
    ImportedGeometry   = 30u,
    ProceduralGeometry = 31u,
};

[[nodiscard]] bool ContainsCaseFold(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty() || Haystack.size() < Needle.size()) return false;
    for (std::size_t I = 0u; I + Needle.size() <= Haystack.size(); ++I)
    {
        bool Match = true;
        for (std::size_t J = 0u; J < Needle.size(); ++J)
        {
            const unsigned char A = static_cast<unsigned char>(Haystack[I + J]);
            const unsigned char B = static_cast<unsigned char>(Needle[J]);
            if (std::tolower(A) != std::tolower(B)) { Match = false; break; }
        }
        if (Match) return true;
    }
    return false;
}

[[nodiscard]] ResolvedInspectorTarget ClassifyLuminaireEntry(const OutlinerRecordEntry& Entry) noexcept
{
    const std::string_view Tag = Entry.DomainTag;
    if (Tag == "light:spot"      || Tag == "spotlight") return ResolvedInspectorTarget::SpotLight;
    if (Tag == "light:ies"       || Tag == "ieslight")  return ResolvedInspectorTarget::IesLight;
    if (Tag == "light:area"      || Tag == "arealight") return ResolvedInspectorTarget::AreaLight;
    if (Tag == "light:tube"      || Tag == "tubelight") return ResolvedInspectorTarget::TubeLight;
    if (Tag == "light:led-strip" || Tag == "ledstrip")  return ResolvedInspectorTarget::LedStrip;
    if (Tag == "light:led"       || Tag == "ledlight")  return ResolvedInspectorTarget::LedLight;
    if (Tag == "light:point"     || Tag == "pointlight")return ResolvedInspectorTarget::PointLight;

    // Smart automatic classification for any arbitrary Outliner luminaire entry
    const std::string Combined = Entry.Title + " " + Entry.Subtitle + " " + Entry.DomainTag;
    if (ContainsCaseFold(Combined, "strip") || ContainsCaseFold(Combined, "ribbon") || ContainsCaseFold(Combined, "tape"))
        return ResolvedInspectorTarget::LedStrip;
    if (ContainsCaseFold(Combined, "led") || ContainsCaseFold(Combined, "diode") || ContainsCaseFold(Combined, "cob"))
        return ResolvedInspectorTarget::LedLight;
    if (ContainsCaseFold(Combined, "ies") || ContainsCaseFold(Combined, "photometric") ||
        ContainsCaseFold(Combined, "downlight") || ContainsCaseFold(Combined, "low beam") ||
        ContainsCaseFold(Combined, "high beam") || ContainsCaseFold(Combined, "headlamp"))
        return ResolvedInspectorTarget::IesLight;
    if (ContainsCaseFold(Combined, "area") || ContainsCaseFold(Combined, "softbox") ||
        ContainsCaseFold(Combined, "quad") || ContainsCaseFold(Combined, "rect"))
        return ResolvedInspectorTarget::AreaLight;
    if (ContainsCaseFold(Combined, "tube") || ContainsCaseFold(Combined, "linear") || ContainsCaseFold(Combined, "fluorescent"))
        return ResolvedInspectorTarget::TubeLight;
    if (ContainsCaseFold(Combined, "spot"))
        return ResolvedInspectorTarget::SpotLight;
    return ResolvedInspectorTarget::PointLight;
}

[[nodiscard]] ResolvedInspectorTarget ClassifyGeometryEntry(const OutlinerRecordEntry& Entry) noexcept
{
    const std::string_view Tag = Entry.DomainTag;
    if (Tag.rfind("geom:primitive", 0u) == 0u || Tag.rfind("geometry:primitive", 0u) == 0u || Tag.rfind("prim:", 0u) == 0u)
        return ResolvedInspectorTarget::AnalyticPrimitive;
    if (Tag.rfind("geom:imported", 0u) == 0u || Tag.rfind("import:", 0u) == 0u)
        return ResolvedInspectorTarget::ImportedGeometry;
    if (Tag.rfind("geom:procedural", 0u) == 0u || Tag.rfind("proc:", 0u) == 0u || Tag.rfind("parametric:", 0u) == 0u)
        return ResolvedInspectorTarget::ProceduralGeometry;
    if (Tag.rfind("geom:native", 0u) == 0u || Tag.rfind("native:", 0u) == 0u)
        return ResolvedInspectorTarget::NativeGeometry;

    // Smart automatic classification for any arbitrary Outliner geometry entry
    const std::string Combined = Entry.Title + " " + Entry.Subtitle + " " + Entry.DomainTag;
    if (ContainsCaseFold(Combined, ".gltf") || ContainsCaseFold(Combined, ".glb") ||
        ContainsCaseFold(Combined, ".fbx")  || ContainsCaseFold(Combined, ".obj") ||
        ContainsCaseFold(Combined, ".usd")  || ContainsCaseFold(Combined, ".step") ||
        ContainsCaseFold(Combined, ".stp")  || ContainsCaseFold(Combined, ".stl") ||
        ContainsCaseFold(Combined, "imported"))
    {
        return ResolvedInspectorTarget::ImportedGeometry;
    }
    if (ContainsCaseFold(Combined, "procedural") || ContainsCaseFold(Combined, "parametric") ||
        ContainsCaseFold(Combined, "terrain")    || ContainsCaseFold(Combined, "voronoi") ||
        ContainsCaseFold(Combined, "sweep")      || ContainsCaseFold(Combined, "fracture"))
    {
        return ResolvedInspectorTarget::ProceduralGeometry;
    }
    if (ContainsCaseFold(Combined, "sphere") || ContainsCaseFold(Combined, "box") ||
        ContainsCaseFold(Combined, "cube")   || ContainsCaseFold(Combined, "cylinder") ||
        ContainsCaseFold(Combined, "cone")   || ContainsCaseFold(Combined, "torus") ||
        ContainsCaseFold(Combined, "capsule")|| ContainsCaseFold(Combined, "plane") ||
        ContainsCaseFold(Combined, "primitive"))
    {
        return ResolvedInspectorTarget::AnalyticPrimitive;
    }
    // Universal fallback for all native engine geometry / assemblies / vehicles
    return ResolvedInspectorTarget::NativeGeometry;
}

[[nodiscard]] ResolvedInspectorTarget ResolveTarget(const OutlinerRecordEntry& Entry) noexcept
{
    if (Entry.Category == ConstructSlotCategory::Folder) return ResolvedInspectorTarget::Folder;

    const std::string_view Tag = Entry.DomainTag;
    if (Tag == "env:atmosphere")        return ResolvedInspectorTarget::Atmosphere;
    if (Tag == "env:sun")               return ResolvedInspectorTarget::Solar;
    if (Tag == "env:clouds")            return ResolvedInspectorTarget::Cloud;
    if (Tag == "env:local-cloud")       return ResolvedInspectorTarget::LocalCloud;
    if (Tag == "env:moon")              return ResolvedInspectorTarget::Lunar;
    if (Tag == "env:stars")             return ResolvedInspectorTarget::Starfield;
    if (Tag == "env:wind")              return ResolvedInspectorTarget::Wind;
    if (Tag == "env:height-fog")        return ResolvedInspectorTarget::HeightFog;
    if (Tag == "env:aerial-fog")        return ResolvedInspectorTarget::AtmosphericFog;
    if (Tag == "env:local-fog")         return ResolvedInspectorTarget::LocalFog;
    if (Tag == "env:god-rays")          return ResolvedInspectorTarget::GodRays;
    if (Tag == "env:precipitation")     return ResolvedInspectorTarget::Precipitation;
    if (Tag == "env:rainbow")           return ResolvedInspectorTarget::Rainbow;
    if (Tag == "env:lens-flare")        return ResolvedInspectorTarget::LensFlare;
    if (Tag == "env:motion-blur")       return ResolvedInspectorTarget::MotionBlur;
    if (Tag == "env:camera-settings" || Tag == "env:render-engine" || Tag == "camera:settings" || Tag == "render:settings")
        return ResolvedInspectorTarget::CameraSettings;
    if (Tag == "env:environment-light") return ResolvedInspectorTarget::EnvironmentLight;
    if (Tag == "env:post-process")      return ResolvedInspectorTarget::PostProcess;
    if (Tag == "env:exposure")          return ResolvedInspectorTarget::Exposure;
    if (Tag == "camera:perspective")    return ResolvedInspectorTarget::Camera;
    if (Tag.rfind("light:", 0u) == 0u)  return ClassifyLuminaireEntry(Entry);
    if (Tag.rfind("geom:", 0u) == 0u)   return ClassifyGeometryEntry(Entry);

    // Fallback title / category classifier so even legacy untagged Outliner items route to their exact Inspector card
    const auto MatchAny = [&](std::string_view Needle) noexcept -> bool
    {
        return ContainsCaseFold(Entry.Title, Needle) || ContainsCaseFold(Entry.CategoryLabel, Needle);
    };
    if (MatchAny("camera settings") || MatchAny("render engine")) return ResolvedInspectorTarget::CameraSettings;
    if (MatchAny("atmosphere"))                                   return ResolvedInspectorTarget::Atmosphere;
    if (MatchAny("lens flare"))                                   return ResolvedInspectorTarget::LensFlare;
    if (MatchAny("motion blur"))                                  return ResolvedInspectorTarget::MotionBlur;
    if (MatchAny("exposure"))                                     return ResolvedInspectorTarget::Exposure;
    if (MatchAny("post process"))                                 return ResolvedInspectorTarget::PostProcess;
    if (MatchAny("environment light") || MatchAny("sky dome"))    return ResolvedInspectorTarget::EnvironmentLight;
    if (MatchAny("local cloud"))                                  return ResolvedInspectorTarget::LocalCloud;
    if (MatchAny("cloud"))                                        return ResolvedInspectorTarget::Cloud;
    if (MatchAny("local fog"))                                    return ResolvedInspectorTarget::LocalFog;
    if (MatchAny("atmospheric fog") || MatchAny("aerial fog"))    return ResolvedInspectorTarget::AtmosphericFog;
    if (MatchAny("height fog") || MatchAny("fog"))                return ResolvedInspectorTarget::HeightFog;
    if (MatchAny("god ray"))                                      return ResolvedInspectorTarget::GodRays;
    if (MatchAny("precipitation") || MatchAny("rain") || MatchAny("snow")) return ResolvedInspectorTarget::Precipitation;
    if (MatchAny("rainbow"))                                      return ResolvedInspectorTarget::Rainbow;
    if (MatchAny("wind"))                                         return ResolvedInspectorTarget::Wind;
    if (MatchAny("star"))                                         return ResolvedInspectorTarget::Starfield;
    if (MatchAny("moon") || MatchAny("luna"))                     return ResolvedInspectorTarget::Lunar;
    if (MatchAny("sun") || MatchAny("solar"))                     return ResolvedInspectorTarget::Solar;

    switch (Entry.Category)
    {
        case ConstructSlotCategory::OpticsCamera:   return ResolvedInspectorTarget::Camera;
        case ConstructSlotCategory::PointLuminaire: return ClassifyLuminaireEntry(Entry);
        default:                                    return ClassifyGeometryEntry(Entry);
    }
}

[[nodiscard]] const char* ResolveSubtitle(ResolvedInspectorTarget Target, const OutlinerRecordEntry& /*Entry*/) noexcept
{
    switch (Target)
    {
        case ResolvedInspectorTarget::Folder:             return FolderInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Atmosphere:         return AtmosphereInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Solar:              return SolarInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Cloud:              return CloudInspector::QuerySubtitle();
        case ResolvedInspectorTarget::LocalCloud:         return LocalCloudInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Lunar:              return LunarInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Starfield:          return StarfieldInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Wind:               return WindInspector::QuerySubtitle();
        case ResolvedInspectorTarget::HeightFog:          return HeightFogInspector::QuerySubtitle();
        case ResolvedInspectorTarget::AtmosphericFog:     return AtmosphericFogInspector::QuerySubtitle();
        case ResolvedInspectorTarget::LocalFog:           return LocalFogInspector::QuerySubtitle();
        case ResolvedInspectorTarget::GodRays:            return GodRaysInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Precipitation:      return PrecipitationInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Rainbow:            return RainbowInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Camera:             return CameraInspector::QuerySubtitle();
        case ResolvedInspectorTarget::CameraSettings:     return CameraSettingsInspector::QuerySubtitle();
        case ResolvedInspectorTarget::LensFlare:          return LensFlareInspector::QuerySubtitle();
        case ResolvedInspectorTarget::MotionBlur:         return MotionBlurInspector::QuerySubtitle();
        case ResolvedInspectorTarget::PointLight:         return PointLightInspector::QuerySubtitle();
        case ResolvedInspectorTarget::SpotLight:          return SpotLightInspector::QuerySubtitle();
        case ResolvedInspectorTarget::IesLight:           return IesLightInspector::QuerySubtitle();
        case ResolvedInspectorTarget::AreaLight:          return AreaLightInspector::QuerySubtitle();
        case ResolvedInspectorTarget::TubeLight:          return TubeLightInspector::QuerySubtitle();
        case ResolvedInspectorTarget::LedLight:         return LedLightInspector::QuerySubtitle();
        case ResolvedInspectorTarget::LedStrip:           return LedStripInspector::QuerySubtitle();
        case ResolvedInspectorTarget::EnvironmentLight:   return EnvironmentLightInspector::QuerySubtitle();
        case ResolvedInspectorTarget::PostProcess:        return PostProcessInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Exposure:           return ExposureInspector::QuerySubtitle();
        case ResolvedInspectorTarget::AnalyticPrimitive:  return AnalyticPrimitiveInspector::QuerySubtitle();
        case ResolvedInspectorTarget::ImportedGeometry:   return ImportedGeometryInspector::QuerySubtitle();
        case ResolvedInspectorTarget::ProceduralGeometry: return ProceduralGeometryInspector::QuerySubtitle();
        default:                                          return NativeGeometryInspector::QuerySubtitle();
    }
}

[[nodiscard]] uint32_t ComputeMoonDisplayOrdinal(const OutlinerPanel& Outliner, OutlinerSlotToken TargetToken) noexcept
{
    uint32_t Ordinal = 0u;
    for (const auto& Entry : Outliner.QueryEntries())
    {
        if (Entry.DomainTag == "env:moon")
        {
            if (Entry.Token == TargetToken) return Ordinal;
            ++Ordinal;
        }
    }
    return 0u;
}

} // namespace

bool InspectorRegistry::ConstructActiveInspector(
    PixelSpace&                               Surface,
    const Rectangle2D&                        PaneBounds,
    OutlinerPanel&                            Outliner,
    WorldEnvironmentRegistry*                 EnvironmentRegistry,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings,
    const PaneInputReading&                   Input,
    bool&                                     OutMutated) noexcept
{
    FRONTIER_THOUGHT_SCOPE("Inspector.ConstructActive");
    OutlinerRecordEntry* Entry = Outliner.AccessEntry(Outliner.QueryPrimarySelection());
    if (Entry == nullptr)
    {
        return false;
    }

    if (Entry->Token.Identifier != LastEntryId)
    {
        LastEntryId = Entry->Token.Identifier;
        ScrollView.Reset();
        ++ActivePanelRevision;
    }

    const ResolvedInspectorTarget Target = ResolveTarget(*Entry);
    const ProjectInspectorBlueprint* ProjectBp = ProjectHost.ResolveBlueprint(*Entry);
    const bool ExplicitNativeTag =
        Entry->DomainTag.rfind("env:", 0u) == 0u ||
        Entry->DomainTag.rfind("light:", 0u) == 0u ||
        Entry->DomainTag.rfind("geom:", 0u) == 0u ||
        Entry->DomainTag == "camera:settings" ||
        Entry->DomainTag == "render:settings";
    const bool UseProjectBlueprint = (ProjectBp != nullptr) && (ProjectBp->OverrideNative || !ExplicitNativeTag);
    const char* ActiveSubtitle = UseProjectBlueprint ? ProjectBp->Subtitle.c_str() : ResolveSubtitle(Target, *Entry);

    InspectorPrimitives::ResetTooltipQueue();

    Surface.FillRectangle(PaneBounds, ColorRgba{ 18u, 18u, 18u, 255u }, 0.0f);
    Surface.StrokeLine({ PaneBounds.X + 0.5f, PaneBounds.Y }, { PaneBounds.X + 0.5f, PaneBounds.Bottom() }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);

    const float FooterH    = 45.0f;
    const Rectangle2D ScrollClip { PaneBounds.X, PaneBounds.Y, PaneBounds.Width, std::max(40.0f, PaneBounds.Height - FooterH) };
    const Rectangle2D FooterRect { PaneBounds.X, ScrollClip.Bottom(), PaneBounds.Width, FooterH };

    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (std::fabs(PaneBounds.Width - LastPaneWidthPx) > 0.5f)
    {
        LastPaneWidthPx = PaneBounds.Width;
        // Keep current scroll clamped safely without snapping on resize
    }

    const SmoothScrollUpdateResult ScrollRes =
        ScrollView.Advance(ScrollClip, -1.0f, Input.PrimaryPointer, Input.WheelDeltaY, 42.0f);
    const float ScrollOffsetY = ScrollRes.DisplayOffsetY;

    const float PadX       = std::clamp(PaneBounds.Width * 0.055f, 10.0f, InspectorMetrics::PaneInsetX);
    const float InnerLeft  = PaneBounds.X + PadX;
    const float InnerWidth = std::max(140.0f, PaneBounds.Width - PadX * 2.0f);

    Surface.PushClip(ScrollClip);
    float CursorY = PaneBounds.Y + 32.0f - ScrollOffsetY;

    // Dynamic breadcrumb from actual enclosing Outliner hierarchy (zero hardcoded folder names!)
    const std::string Breadcrumb = InspectorInstanceRegister::ResolveEnclosingBreadcrumb(Outliner, Entry->Token);
    CursorY = InspectorPrimitives::ConstructInspectorHeader(
        Surface, InnerLeft, CursorY, InnerWidth,
        Breadcrumb.c_str(), Entry->Title.c_str(), ActiveSubtitle);

    bool LocalMutated = false;
    bool PageSwitched = false;
    ++ConstructCallCount;
    LastEvaluatedCardCount = 1u;
    LastSkippedCardCount   = 30u;

    if (UseProjectBlueprint)
    {
        CursorY = ProjectHost.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, *ProjectBp, Input, LocalMutated, PageSwitched);
    }
    else switch (Target)
    {
        case ResolvedInspectorTarget::Folder:
            CursorY = Folder.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Outliner, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Atmosphere:
            CursorY = Atmosphere.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessAtmosphere(Entry->Token, Scene), Scene.Atmosphere.Solar, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Solar:
            CursorY = Solar.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessSolar(Entry->Token, Scene), Scene.Atmosphere.Medium, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Cloud:
            CursorY = Cloud.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessCloud(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::LocalCloud:
            CursorY = LocalCloud.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessLocalCloud(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Lunar:
        {
            const uint32_t Ord = ComputeMoonDisplayOrdinal(Outliner, Entry->Token);
            CursorY = Lunar.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessLunar(Entry->Token, Scene, Ord), Ord, Input, LocalMutated, PageSwitched);
            break;
        }
        case ResolvedInspectorTarget::Starfield:
            CursorY = Starfield.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessStarfield(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Wind:
            CursorY = Wind.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessWind(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::HeightFog:
            CursorY = HeightFog.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessHeightFog(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::AtmosphericFog:
            CursorY = AtmosphericFog.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessAtmosphericFog(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::LocalFog:
            CursorY = LocalFog.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessLocalFog(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::GodRays:
            CursorY = GodRays.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessGodRays(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Precipitation:
            CursorY = Precipitation.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessPrecipitation(Entry->Token), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Rainbow:
            CursorY = Rainbow.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessRainbow(Entry->Token), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Camera:
            CursorY = Camera.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessCamera(Entry->Token, Scene), Outliner, Instances, Scene, DisplaySettings, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::CameraSettings:
            CursorY = CameraSettings.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessCameraSettings(Entry->Token), DisplaySettings, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::LensFlare:
            CursorY = LensFlare.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessLensFlare(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::MotionBlur:
            CursorY = MotionBlur.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessMotionBlur(Entry->Token), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::PointLight:
            CursorY = PointLight.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessPointLight(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::SpotLight:
            CursorY = SpotLight.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessSpotLight(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::IesLight:
            CursorY = IesLight.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessIesLight(Entry->Token, *Entry), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::AreaLight:
            CursorY = AreaLight.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessAreaLight(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::TubeLight:
            CursorY = TubeLight.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessTubeLight(Entry->Token), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::LedLight:
            CursorY = LedLight.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessLedLight(Entry->Token), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::LedStrip:
            CursorY = LedStrip.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessLedStrip(Entry->Token), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::EnvironmentLight:
            CursorY = EnvironmentLight.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessEnvironmentLight(Entry->Token, Scene), DisplaySettings, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::PostProcess:
            CursorY = PostProcess.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessPostProcess(Entry->Token), DisplaySettings, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Exposure:
            CursorY = Exposure.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessExposure(Entry->Token), DisplaySettings, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::AnalyticPrimitive:
            CursorY = AnalyticPrimitive.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessAnalyticPrimitive(Entry->Token, *Entry, Scene), Scene, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::NativeGeometry:
            CursorY = NativeGeometry.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessNativeGeometry(Entry->Token, *Entry, Scene), Scene, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::ImportedGeometry:
            CursorY = ImportedGeometry.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessImportedGeometry(Entry->Token, *Entry, Scene), Scene, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::ProceduralGeometry:
            CursorY = ProceduralGeometry.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessProceduralGeometry(Entry->Token, *Entry, Scene), Scene, Input, LocalMutated, PageSwitched);
            break;
    }

    (void)EnvironmentRegistry;

    if (PageSwitched)
    {
        ScrollView.Reset();
        ++ActivePanelRevision;
    }

    if (LocalMutated)
    {
        ++ActivePanelRevision;
        Instances.CombineIntoScene(Outliner, Scene, DisplaySettings);
        OutMutated = true;
    }

    const float ContentHeight = (CursorY + ScrollOffsetY) - PaneBounds.Y + 24.0f;
    ScrollView.FinalizeContentHeight(ScrollClip.Height, ContentHeight);
    ScrollView.RenderIndicator(Surface, ScrollClip, ContentHeight, Cursor);

    Surface.PopClip();
    InspectorPrimitives::ConstructInspectorFooter(Surface, FooterRect, ActiveSubtitle, "F11");
    InspectorPrimitives::FlushTooltipOverlay(Surface, PaneBounds);
    return true;
}

uint64_t InspectorRegistry::QueryActiveSignature() const noexcept
{
    uint64_t Sig = RetainedCanvasCache::MixHash(0x51A7C0DE1337BEEFull, ActivePanelRevision);
    Sig = RetainedCanvasCache::MixHash(Sig, LastEntryId);
    Sig = RetainedCanvasCache::MixHash(Sig, static_cast<uint64_t>(std::llround(ScrollView.QueryDisplayOffset() * 4.0f)));
    Sig = RetainedCanvasCache::MixHash(Sig, static_cast<uint64_t>(std::llround(ScrollView.QueryTargetOffset() * 4.0f)));
    if (ScrollView.QueryAnimating())
    {
        const uint64_t Tick = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count() >> 14);
        Sig = RetainedCanvasCache::MixHash(Sig, Tick);
    }
    return Sig;
}

} // namespace Frontier
