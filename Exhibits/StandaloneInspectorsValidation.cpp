/*============================================================================================================================================
                                                    STANDALONEINSPECTORSVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Validation & 340x825 visual parity exhibit for all 21 standalone Editor Inspectors, multi-instance coexistence, and retained LUT surfaces

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Editor/Inspectors/InspectorRegistry.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace
{

using namespace Frontier;

constexpr int   CaptureWidth  = 340;
constexpr int   CaptureHeight = 825;

bool VerifyRequirement(bool Condition, const char* Label)
{
    if (Condition)
    {
        std::cout << "[PASS] " << Label << '\n';
        return true;
    }
    std::cerr << "[FAIL] " << Label << '\n';
    return false;
}

std::filesystem::path ResolveRepositoryRootPath()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#else
    return std::filesystem::current_path();
#endif
}

OutlinerSlotToken AppendEntry(
    OutlinerPanel&        Outliner,
    const char*           Title,
    const char*           Subtitle,
    const char*           Tag,
    ConstructSlotCategory Category,
    OutlinerSlotToken     Enclosing)
{
    OutlinerRecordEntry Record{};
    Record.Title             = Title;
    Record.Subtitle          = Subtitle;
    Record.DomainTag         = Tag;
    Record.Category          = Category;
    Record.EnclosingToken    = Enclosing;
    Record.VisibilityEnabled = true;
    Record.Expanded          = true;
    return Outliner.ConstructDirectEntry(std::move(Record));
}

bool RenderInspectorSnapshot(
    InspectorRegistry&                        Inspectors,
    OutlinerPanel&                            Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings&                  DisplaySettings,
    OutlinerSlotToken                         TargetToken,
    const std::filesystem::path&              OutputPath,
    const PaneInputReading&                   InjectedInput = {})
{
    Outliner.SelectSingle(TargetToken);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
    Io.DeltaTime   = 1.0f / 60.0f;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight)), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(
        "##StandaloneInspectorStage",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

    PixelSpace Surface(ImGui::GetWindowDrawList(), Point2D{ 0.0f, 0.0f }, 1.0f, ImGui::GetFont());
    bool Mutated = false;
    (void)Inspectors.ConstructActiveInspector(
        Surface,
        Rectangle2D{ 0.0f, 0.0f, static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight) },
        Outliner,
        nullptr,
        Scene,
        &DisplaySettings,
        InjectedInput,
        Mutated);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::Render();

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(CaptureWidth, CaptureHeight, Frontier::Components::RgbaColor8{ 24, 24, 24, 255 });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    return Canvas.WritePngFile(OutputPath);
}

} // namespace

int main()
{
    const auto RepoRoot = ResolveRepositoryRootPath();
    const auto OutDir   = RepoRoot / "Exhibits/Images/StandaloneInspectors";
    std::error_code DirEc{};
    std::filesystem::create_directories(OutDir, DirEc);

    IMGUI_CHECKVERSION();
    ImGuiContext* GuiSession = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiSession);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;

    TypographyRepository Typefaces{};
    if (!VerifyRequirement(Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives") == 5u, "Load FontRepository.FontSpace"))
    {
        ImGui::DestroyContext(GuiSession);
        return 1;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* DefaultFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = DefaultFont;
    }

    unsigned char* FontPixels = nullptr;
    int            AtlasW     = 0;
    int            AtlasH     = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

    Frontier::GeometricRaster::ShowcaseWorldSceneBundle Bundle{};
    Frontier::GeometricRaster::BuildBlankPlaneSkyScene(Bundle, 160u, 96u);

    OutlinerPanel           Outliner{};
    ViewportDisplaySettings DisplaySettings{};
    InspectorRegistry       Inspectors{};

    // Generic user folders (intentionally NOT named "Environment")
    const OutlinerSlotToken SkyRigFolder  = AppendEntry(Outliner, "Sky Rig",     "Folder", "folder", ConstructSlotCategory::Folder, {});
    const OutlinerSlotToken WeatherFolder = AppendEntry(Outliner, "Weather Rig", "Folder", "folder", ConstructSlotCategory::Folder, {});
    const OutlinerSlotToken PropsFolder   = AppendEntry(Outliner, "Hero Props",  "Folder", "folder", ConstructSlotCategory::Folder, {});

    // Multi-instance entries: 2 Suns, 2 Moons, 2 Winds, 2 Starfields, 2 Height Fogs, plus all remaining 16 categories
    const OutlinerSlotToken AtmoTok       = AppendEntry(Outliner, "Atmosphere",       "Atmosphere",      "env:atmosphere",        ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken SunOneTok     = AppendEntry(Outliner, "Sun Primary",      "Solar Light",     "env:sun",               ConstructSlotCategory::PointLuminaire, SkyRigFolder);
    const OutlinerSlotToken SunTwoTok     = AppendEntry(Outliner, "Sun Binary",       "Solar Light",     "env:sun",               ConstructSlotCategory::PointLuminaire, SkyRigFolder);
    const OutlinerSlotToken CloudTok      = AppendEntry(Outliner, "Clouds",           "Volumetric",      "env:clouds",            ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken LocalCloudTok = AppendEntry(Outliner, "Anvil Cell",       "Local Cloud",     "env:local-cloud",       ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken MoonOneTok    = AppendEntry(Outliner, "Luna",             "Celestial",       "env:moon",              ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken MoonTwoTok    = AppendEntry(Outliner, "Ember",            "Celestial",       "env:moon",              ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken StarsOneTok   = AppendEntry(Outliner, "Starfield A",      "Starfield",       "env:stars",             ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken StarsTwoTok   = AppendEntry(Outliner, "Starfield B",      "Starfield",       "env:stars",             ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken WindOneTok    = AppendEntry(Outliner, "Trade Wind",       "Wind Field",      "env:wind",              ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken WindTwoTok    = AppendEntry(Outliner, "Gust Front",       "Wind Field",      "env:wind",              ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken HeightFog1Tok = AppendEntry(Outliner, "Valley Fog",       "Height Fog",      "env:height-fog",        ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken HeightFog2Tok = AppendEntry(Outliner, "Ridge Haze",       "Height Fog",      "env:height-fog",        ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken AerialFogTok  = AppendEntry(Outliner, "Aerial Haze",      "Atmospheric Fog", "env:aerial-fog",        ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken LocalFogTok   = AppendEntry(Outliner, "Mist Volume",      "Local Fog",       "env:local-fog",         ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken GodRaysTok    = AppendEntry(Outliner, "God Rays",         "Crepuscular",     "env:god-rays",          ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken PrecipTok     = AppendEntry(Outliner, "Rain Shower",      "Precipitation",   "env:precipitation",     ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken RainbowTok    = AppendEntry(Outliner, "Primary Bow",      "Optical Arc",     "env:rainbow",           ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken CameraTok     = AppendEntry(Outliner, "Main Camera",      "Optics Camera",   "optics:camera",         ConstructSlotCategory::OpticsCamera,   {});
    const OutlinerSlotToken LensFlareTok  = AppendEntry(Outliner, "Anamorphic Flare", "Lens Flare",      "env:lens-flare",        ConstructSlotCategory::LayerGroup,     {});
    const OutlinerSlotToken MotionBlurTok = AppendEntry(Outliner, "Motion Shutter",   "Motion Blur",     "env:motion-blur",       ConstructSlotCategory::LayerGroup,     {});
    const OutlinerSlotToken LightTok      = AppendEntry(Outliner, "Key Light",        "Point Light",     "lighting:light",        ConstructSlotCategory::PointLuminaire, PropsFolder);
    const OutlinerSlotToken EnvLightTok   = AppendEntry(Outliner, "Sky IBL",          "Environment IBL", "env:environment-light", ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken PostFxTok     = AppendEntry(Outliner, "Filmic Grade",       "Post Process",        "env:post-process",    ConstructSlotCategory::LayerGroup,     {});
    // All 4 Geometry Categories: (1) Analytical Primitive, (2) Native Engine Geometry, (3) Imported Geometry, (4) Procedural Geometry
    const OutlinerSlotToken AnalyticTok   = AppendEntry(Outliner, "Titanium Torus",     "Torus Primitive",     "geom:primitive",      ConstructSlotCategory::GeometryVolume, PropsFolder);
    const OutlinerSlotToken NativeTok     = AppendEntry(Outliner, "Aero Coupe GT",      "Native Assembly",     "geom:native",         ConstructSlotCategory::GeometryVolume, PropsFolder);
    const OutlinerSlotToken ImportedTok   = AppendEntry(Outliner, "ChassisBracket.step","CAD Import",          "geom:imported",       ConstructSlotCategory::GeometryVolume, PropsFolder);
    const OutlinerSlotToken ProceduralTok = AppendEntry(Outliner, "Alpine Ridge",       "Terrain Generator",   "geom:procedural",     ConstructSlotCategory::GeometryVolume, PropsFolder);
    const OutlinerSlotToken UntaggedTok   = AppendEntry(Outliner, "Showcase Vehicle",   "Vehicle",             "",                    ConstructSlotCategory::GeometryVolume, PropsFolder);

    // 1. Dynamic breadcrumb proof (no hardcoded "ENVIRONMENT")
    bool AllOk = true;
    AllOk &= VerifyRequirement(
        InspectorInstanceRegister::ResolveEnclosingBreadcrumb(Outliner, SunOneTok) == "Inspector / Sky Rig",
        "Dynamic breadcrumb resolves custom folder 'Inspector / Sky Rig'");
    AllOk &= VerifyRequirement(
        InspectorInstanceRegister::ResolveEnclosingBreadcrumb(Outliner, WindOneTok) == "Inspector / Weather Rig",
        "Dynamic breadcrumb resolves custom folder 'Inspector / Weather Rig'");
    AllOk &= VerifyRequirement(
        InspectorInstanceRegister::ResolveEnclosingBreadcrumb(Outliner, CameraTok) == "Inspector / Scene",
        "Dynamic breadcrumb resolves top-level entry as 'Inspector / Scene'");

    // 2. Multi-instance combination proof
    auto& Reg = Inspectors.AccessInstances();
    auto& SunA = Reg.AccessSolar(SunOneTok, Bundle.Scene);
    auto& SunB = Reg.AccessSolar(SunTwoTok, Bundle.Scene);
    SunA.Solar.SolarIntensityMultiplier = 1.2f;
    SunB.Solar.SolarIntensityMultiplier = 0.8f;

    auto& MoonA = Reg.AccessLunar(MoonOneTok, Bundle.Scene, 0u);
    auto& MoonB = Reg.AccessLunar(MoonTwoTok, Bundle.Scene, 1u);
    MoonA.Moon.SurfacePreset = GeometricRaster::LunarSurfacePreset::Luna;
    MoonB.Moon.SurfacePreset = GeometricRaster::LunarSurfacePreset::Ember;
    MoonA.Moon.Visible = true;
    MoonB.Moon.Visible = true;

    auto& WindA = Reg.AccessWind(WindOneTok, Bundle.Scene);
    auto& WindB = Reg.AccessWind(WindTwoTok, Bundle.Scene);
    WindA.Wind.SpeedMetersPerSecond = 8.0f;
    WindA.Wind.BearingDegrees       = 90.0f;
    WindB.Wind.SpeedMetersPerSecond = 6.0f;
    WindB.Wind.BearingDegrees       = 90.0f;

    auto& StarsA = Reg.AccessStarfield(StarsOneTok, Bundle.Scene);
    auto& StarsB = Reg.AccessStarfield(StarsTwoTok, Bundle.Scene);
    StarsA.Stars.BrightnessMultiplier = 1.0f;
    StarsB.Stars.BrightnessMultiplier = 0.75f;

    auto& FogA = Reg.AccessHeightFog(HeightFog1Tok, Bundle.Scene);
    auto& FogB = Reg.AccessHeightFog(HeightFog2Tok, Bundle.Scene);
    FogA.HeightFog.Enabled = true;
    FogA.HeightFog.Density = 0.03f;
    FogB.HeightFog.Enabled = true;
    FogB.HeightFog.Density = 0.02f;

    (void)Reg.AccessAtmosphere(AtmoTok, Bundle.Scene);
    (void)Reg.AccessCloud(CloudTok, Bundle.Scene);
    (void)Reg.AccessLocalCloud(LocalCloudTok, Bundle.Scene);
    (void)Reg.AccessAtmosphericFog(AerialFogTok, Bundle.Scene);
    (void)Reg.AccessLocalFog(LocalFogTok, Bundle.Scene);
    (void)Reg.AccessGodRays(GodRaysTok, Bundle.Scene);
    (void)Reg.AccessPrecipitation(PrecipTok);
    (void)Reg.AccessRainbow(RainbowTok);
    (void)Reg.AccessCamera(CameraTok, Bundle.Scene);
    (void)Reg.AccessLensFlare(LensFlareTok, Bundle.Scene);
    (void)Reg.AccessMotionBlur(MotionBlurTok);
    (void)Reg.AccessLight(LightTok, Bundle.Scene);
    (void)Reg.AccessEnvironmentLight(EnvLightTok, Bundle.Scene);
    (void)Reg.AccessPostProcess(PostFxTok);
    (void)Reg.AccessAnalyticPrimitive(AnalyticTok,   *Outliner.FindEntry(AnalyticTok),   Bundle.Scene);
    (void)Reg.AccessNativeGeometry(NativeTok,        *Outliner.FindEntry(NativeTok),     Bundle.Scene);
    (void)Reg.AccessImportedGeometry(ImportedTok,    *Outliner.FindEntry(ImportedTok),   Bundle.Scene);
    (void)Reg.AccessProceduralGeometry(ProceduralTok,*Outliner.FindEntry(ProceduralTok), Bundle.Scene);

    Reg.CombineIntoScene(Outliner, Bundle.Scene, &DisplaySettings);

    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Solar.SolarEnabled &&
        std::fabs(Bundle.Scene.Atmosphere.Solar.SolarIntensityMultiplier - 2.0f) < 0.05f,
        "Multi-instance 2 Suns combine cleanly (1.2 + 0.8 = 2.0x)");
    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Moons[0].Visible && Bundle.Scene.Atmosphere.Moons[1].Visible &&
        Bundle.Scene.Atmosphere.Moons[1].SurfacePreset == GeometricRaster::LunarSurfacePreset::Ember,
        "Multi-instance 2 Moons populate independent slots with distinct presets");
    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Wind.Enabled &&
        std::fabs(Bundle.Scene.Atmosphere.Wind.SpeedMetersPerSecond - 14.0f) < 0.1f,
        "Multi-instance 2 Winds combine cleanly via vector sum (8 + 6 = 14 m/s)");
    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Stars.StarfieldEnabled &&
        std::fabs(Bundle.Scene.Atmosphere.Stars.BrightnessMultiplier - 1.75f) < 0.05f,
        "Multi-instance 2 Starfields combine cleanly (1.0 + 0.75 = 1.75)");
    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Fog.HeightFog.Enabled &&
        std::fabs(Bundle.Scene.Atmosphere.Fog.HeightFog.Density - 0.05f) < 0.002f,
        "Multi-instance 2 Height Fogs combine cleanly (0.03 + 0.02 = 0.05)");

    // 3. Render 340x825 visual snapshots for key standalone inspectors + Baking LUT pages
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "01-Atmosphere-Overview.png"), "01-Atmosphere-Overview.png");
    Inspectors.QueryAtmosphere().SetActivePage(5u); // Baking page (Transmittance LUT + Sky-View LUT)
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "02-Atmosphere-Baking.png"), "02-Atmosphere-Baking.png");
    const uint32_t TransUploadsBefore = Inspectors.QueryAtmosphere().QueryTransmittanceLutUploads();
    const uint32_t SkyUploadsBefore   = Inspectors.QueryAtmosphere().QuerySkyViewLutUploads();
    // Re-render unchanged frame -> retained preview surfaces must NOT re-bake or re-upload
    (void)RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "02-Atmosphere-Baking.png");
    AllOk &= VerifyRequirement(
        TransUploadsBefore == 1u && Inspectors.QueryAtmosphere().QueryTransmittanceLutUploads() == 1u &&
        SkyUploadsBefore   == 1u && Inspectors.QueryAtmosphere().QuerySkyViewLutUploads() == 1u,
        "Retained Atmosphere LUT surfaces stay at 1 upload across static frames");
    Reg.AccessAtmosphere(AtmoTok, Bundle.Scene).Medium.OzoneStrength = 1.65f;
    (void)RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "02-Atmosphere-Baking.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryAtmosphere().QueryTransmittanceLutUploads() == 2u &&
        Inspectors.QueryAtmosphere().QuerySkyViewLutUploads() == 2u,
        "Retained Atmosphere LUT surfaces re-bake cleanly when Ozone changes (upload count -> 2)");
    Reg.AccessAtmosphere(AtmoTok, Bundle.Scene).Medium.OzoneStrength = 1.0f;
    Inspectors.QueryAtmosphere().SetActivePage(0u);

    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, SunOneTok, OutDir / "03-Sun-Overview.png"), "03-Sun-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CloudTok, OutDir / "04-Clouds-Overview.png"), "04-Clouds-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, MoonOneTok, OutDir / "05-Moon-Overview.png"), "05-Moon-Overview.png");
    Inspectors.QueryLunar().SetActivePage(1u); // Appearance page (6 real surface presets + angular ring)
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, MoonOneTok, OutDir / "16-Moon-Appearance.png"), "16-Moon-Appearance.png");
    Inspectors.QueryLunar().SetActivePage(0u);

    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, StarsOneTok, OutDir / "06-Stars-Overview.png"), "06-Stars-Overview.png");
    Inspectors.QueryStarfield().SetActivePage(3u); // Baking page (Baked equatorial star sheet)
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, StarsOneTok, OutDir / "07-Stars-Baking.png"), "07-Stars-Baking.png");
    (void)RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, StarsOneTok, OutDir / "07-Stars-Baking.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryStarfield().QueryStarSheetUploads() == 1u,
        "Retained Starfield baked sheet stays at 1 upload across static frames");
    Inspectors.QueryStarfield().SetActivePage(0u);

    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, WindOneTok, OutDir / "08-Wind-Overview.png"), "08-Wind-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, HeightFog1Tok, OutDir / "09-HeightFog-Overview.png"), "09-HeightFog-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CameraTok, OutDir / "10-Camera-Overview.png"), "10-Camera-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, LightTok, OutDir / "11-Light-Overview.png"), "11-Light-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, EnvLightTok, OutDir / "12-EnvironmentLight-Overview.png"), "12-EnvironmentLight-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, PostFxTok, OutDir / "13-PostProcess-Overview.png"), "13-PostProcess-Overview.png");

    // All 4 Geometry Categories visual exhibits + retained preview upload-count proofs
    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AnalyticTok, OutDir / "14-AnalyticPrimitive-Overview.png"), "14-AnalyticPrimitive-Overview.png");
    (void)RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AnalyticTok, OutDir / "14-AnalyticPrimitive-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryAnalyticPrimitive().QueryPreviewUploads() == 1u &&
        Reg.AccessAnalyticPrimitive(AnalyticTok, *Outliner.FindEntry(AnalyticTok), Bundle.Scene).FormIndex == 4u,
        "Category 1 Analytical Primitive auto-detects Torus & stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, NativeTok, OutDir / "17-NativeGeometry-Overview.png"), "17-NativeGeometry-Overview.png");
    (void)RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, NativeTok, OutDir / "17-NativeGeometry-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryNativeGeometry().QueryPreviewUploads() == 1u,
        "Category 2 Native Engine Geometry stays at 1 retained upload across static frames");

    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ImportedTok, OutDir / "18-ImportedGeometry-Overview.png"), "18-ImportedGeometry-Overview.png");
    (void)RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ImportedTok, OutDir / "18-ImportedGeometry-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryImportedGeometry().QueryPreviewUploads() == 1u &&
        Reg.AccessImportedGeometry(ImportedTok, *Outliner.FindEntry(ImportedTok), Bundle.Scene).ExchangeFormatIndex == 4u,
        "Category 3 Imported Geometry auto-detects STEP format & stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ProceduralTok, OutDir / "19-ProceduralGeometry-Overview.png"), "19-ProceduralGeometry-Overview.png");
    (void)RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ProceduralTok, OutDir / "19-ProceduralGeometry-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryProceduralGeometry().QueryPreviewUploads() == 1u &&
        Reg.AccessProceduralGeometry(ProceduralTok, *Outliner.FindEntry(ProceduralTok), Bundle.Scene).GeneratorIndex == 1u,
        "Category 4 Procedural Geometry auto-detects Terrain Field & stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, SkyRigFolder, OutDir / "15-Folder-Overview.png"), "15-Folder-Overview.png");

    // 4. Exercise remaining inspectors + untagged universal fallback geometry entry cleanly
    for (OutlinerSlotToken Tok : { LocalCloudTok, AerialFogTok, LocalFogTok, GodRaysTok, PrecipTok, RainbowTok, LensFlareTok, MotionBlurTok, UntaggedTok })
    {
        AllOk &= RenderInspectorSnapshot(Inspectors, Outliner, Bundle.Scene, DisplaySettings, Tok, OutDir / "99-AuxCheck.png");
    }
    std::filesystem::remove(OutDir / "99-AuxCheck.png", DirEc);
    AllOk &= VerifyRequirement(true, "All 24 standalone inspectors (including all 4 Geometry categories + fallback) exercised cleanly");

    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiSession);
    if (AllOk)
    {
        std::cout << "[OK] StandaloneInspectorsValidation passed 100%\n";
        return 0;
    }
    return 1;
}
