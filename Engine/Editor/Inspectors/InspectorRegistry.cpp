/*============================================================================================================================================
                                                          INSPECTORREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Implementation of generic InspectorRegistry dispatching any Outliner entry to its standalone Inspector with dynamic breadcrumbs

#include "Engine/Editor/Inspectors/InspectorRegistry.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

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
    LensFlare        = 15u,
    MotionBlur       = 16u,
    Light            = 17u,
    EnvironmentLight = 18u,
    PostProcess      = 19u,
    Geometry         = 20u,
};

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
    if (Tag == "env:environment-light") return ResolvedInspectorTarget::EnvironmentLight;
    if (Tag == "env:post-process")      return ResolvedInspectorTarget::PostProcess;

    switch (Entry.Category)
    {
        case ConstructSlotCategory::OpticsCamera:   return ResolvedInspectorTarget::Camera;
        case ConstructSlotCategory::PointLuminaire: return ResolvedInspectorTarget::Light;
        default:                                    return ResolvedInspectorTarget::Geometry;
    }
}

[[nodiscard]] const char* ResolveSubtitle(ResolvedInspectorTarget Target, const OutlinerRecordEntry& Entry) noexcept
{
    switch (Target)
    {
        case ResolvedInspectorTarget::Folder:           return FolderInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Atmosphere:       return AtmosphereInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Solar:            return SolarInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Cloud:            return CloudInspector::QuerySubtitle();
        case ResolvedInspectorTarget::LocalCloud:       return LocalCloudInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Lunar:            return LunarInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Starfield:        return StarfieldInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Wind:             return WindInspector::QuerySubtitle();
        case ResolvedInspectorTarget::HeightFog:        return HeightFogInspector::QuerySubtitle();
        case ResolvedInspectorTarget::AtmosphericFog:   return AtmosphericFogInspector::QuerySubtitle();
        case ResolvedInspectorTarget::LocalFog:         return LocalFogInspector::QuerySubtitle();
        case ResolvedInspectorTarget::GodRays:          return GodRaysInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Precipitation:    return PrecipitationInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Rainbow:          return RainbowInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Camera:           return CameraInspector::QuerySubtitle();
        case ResolvedInspectorTarget::LensFlare:        return LensFlareInspector::QuerySubtitle();
        case ResolvedInspectorTarget::MotionBlur:       return MotionBlurInspector::QuerySubtitle();
        case ResolvedInspectorTarget::Light:            return LightInspector::QuerySubtitle();
        case ResolvedInspectorTarget::EnvironmentLight: return EnvironmentLightInspector::QuerySubtitle();
        case ResolvedInspectorTarget::PostProcess:      return PostProcessInspector::QuerySubtitle();
        default:                                        return GeometryInspector::QuerySubtitle(Entry);
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
    OutlinerRecordEntry* Entry = Outliner.AccessEntry(Outliner.QueryPrimarySelection());
    if (Entry == nullptr)
    {
        return false;
    }

    if (Entry->Token.Identifier != LastEntryId)
    {
        LastEntryId   = Entry->Token.Identifier;
        ScrollOffsetY = 0.0f;
    }

    const ResolvedInspectorTarget Target = ResolveTarget(*Entry);

    Surface.FillRectangle(PaneBounds, ColorRgba{ 24u, 24u, 24u, 255u }, 0.0f);
    Surface.StrokeLine({ PaneBounds.X + 0.5f, PaneBounds.Y }, { PaneBounds.X + 0.5f, PaneBounds.Bottom() }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);

    const float FooterH    = 28.0f;
    const Rectangle2D ScrollClip { PaneBounds.X, PaneBounds.Y, PaneBounds.Width, std::max(40.0f, PaneBounds.Height - FooterH) };
    const Rectangle2D FooterRect { PaneBounds.X, ScrollClip.Bottom(), PaneBounds.Width, FooterH };

    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    if (ScrollClip.Contains(Cursor) && std::fabs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetY = std::max(0.0f, ScrollOffsetY - Input.WheelDeltaY * 36.0f);
    }

    const float PadX       = std::min(InspectorMetrics::PaneInsetX, std::max(12.0f, (PaneBounds.Width - 272.0f) * 0.5f));
    const float InnerLeft  = PaneBounds.X + PadX;
    const float InnerWidth = std::max(180.0f, PaneBounds.Width - PadX * 2.0f);

    Surface.PushClip(ScrollClip);
    float CursorY = PaneBounds.Y + 18.0f - ScrollOffsetY;

    // Dynamic breadcrumb from actual enclosing Outliner hierarchy (zero hardcoded folder names!)
    const std::string Breadcrumb = InspectorInstanceRegister::ResolveEnclosingBreadcrumb(Outliner, Entry->Token);
    CursorY = InspectorPrimitives::ConstructInspectorHeader(
        Surface, InnerLeft, CursorY, InnerWidth,
        Breadcrumb.c_str(), Entry->Title.c_str(), ResolveSubtitle(Target, *Entry));

    bool LocalMutated = false;
    bool PageSwitched = false;

    switch (Target)
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
            CursorY = Camera.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessCamera(Entry->Token, Scene), DisplaySettings, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::LensFlare:
            CursorY = LensFlare.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessLensFlare(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::MotionBlur:
            CursorY = MotionBlur.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessMotionBlur(Entry->Token), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Light:
            CursorY = Light.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessLight(Entry->Token, Scene), Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::EnvironmentLight:
            CursorY = EnvironmentLight.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessEnvironmentLight(Entry->Token, Scene), DisplaySettings, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::PostProcess:
            CursorY = PostProcess.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessPostProcess(Entry->Token), DisplaySettings, Input, LocalMutated, PageSwitched);
            break;
        case ResolvedInspectorTarget::Geometry:
            CursorY = Geometry.Construct(Surface, InnerLeft, CursorY, InnerWidth, *Entry, Instances.AccessGeometrySpecimen(Entry->Token, Scene), Scene, Input, LocalMutated, PageSwitched);
            break;
    }

    // Action row (Reset + Delete)
    bool ResetRequested  = false;
    bool DeleteRequested = false;
    {
        const float Gap       = 8.0f;
        const float BtnH      = 30.0f;
        const bool  CanDelete = !Entry->RemovalProtected;
        const float BtnW      = CanDelete ? (InnerWidth - Gap) * 0.5f : InnerWidth;

        const Rectangle2D ResetRect { InnerLeft, CursorY, BtnW, BtnH };
        const bool ResetHov = ResetRect.Contains(Cursor);
        Surface.FillRectangle(ResetRect, ResetHov ? ColorRgba{ 46u, 46u, 52u, 255u } : ColorRgba{ 32u, 32u, 36u, 255u }, 12.0f);
        Surface.StrokeRectangle(ResetRect, ColorRgba{ 255u, 255u, 255u, 18u }, 12.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ResetRect.X + BtnW * 0.5f - 16.0f, ResetRect.Y + 8.0f }, 11.5f, InspectorMetrics::HeadingInk, "Reset");
        if (ResetHov && Input.PrimaryPointer.Pressed) ResetRequested = true;

        if (CanDelete)
        {
            const Rectangle2D DelRect { ResetRect.Right() + Gap, CursorY, BtnW, BtnH };
            const bool DelHov = DelRect.Contains(Cursor);
            Surface.FillRectangle(DelRect, DelHov ? ColorRgba{ 68u, 28u, 30u, 255u } : ColorRgba{ 44u, 22u, 24u, 255u }, 12.0f);
            Surface.StrokeRectangle(DelRect, ColorRgba{ 232u, 108u, 108u, 52u }, 12.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { DelRect.X + BtnW * 0.5f - 18.0f, DelRect.Y + 8.0f }, 11.5f, ColorRgba{ 238u, 146u, 146u, 255u }, "Delete");
            if (DelHov && Input.PrimaryPointer.Pressed) DeleteRequested = true;
        }
        CursorY += BtnH + 24.0f;
    }

    if (ResetRequested)
    {
        Instances.EraseEntry(Entry->Token);
        if (Target == ResolvedInspectorTarget::Geometry)
        {
            Entry->PositionX = Entry->PositionY = Entry->PositionZ = 0.0f;
            Entry->RotationX = Entry->RotationY = Entry->RotationZ = 0.0f;
            Entry->ScaleX    = Entry->ScaleY    = Entry->ScaleZ    = 1.0f;
        }
        LocalMutated = true;
    }

    if (DeleteRequested && !Entry->RemovalProtected)
    {
        const OutlinerSlotToken TargetToken = Entry->Token;
        Instances.EraseEntry(TargetToken);
        bool RemovedViaRegistry = false;
        if (EnvironmentRegistry != nullptr)
        {
            for (const auto& Reg : EnvironmentRegistry->QueryRegisteredEntries())
            {
                if (Reg.Token == TargetToken)
                {
                    std::array<OutlinerSlotToken, 1u> Tokens {{ TargetToken }};
                    EnvironmentRegistry->UnregisterSlots(Tokens, Outliner, Scene, DisplaySettings);
                    RemovedViaRegistry = true;
                    break;
                }
            }
        }
        if (!RemovedViaRegistry)
        {
            Outliner.DeleteSlot(TargetToken);
        }
        LocalMutated = true;
    }

    if (PageSwitched)
    {
        ScrollOffsetY = 0.0f;
    }

    if (LocalMutated)
    {
        Instances.CombineIntoScene(Outliner, Scene, DisplaySettings);
        OutMutated = true;
    }

    const float ContentHeight = (CursorY + ScrollOffsetY) - PaneBounds.Y + 24.0f;
    const float MaxScroll     = std::max(0.0f, ContentHeight - ScrollClip.Height);
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, MaxScroll);

    Surface.PopClip();
    InspectorPrimitives::ConstructInspectorFooter(Surface, FooterRect, ResolveSubtitle(Target, *Entry), "F11");
    return true;
}

} // namespace Frontier
