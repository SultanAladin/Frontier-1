/*============================================================================================================================================
                                             CAMERAEFFECTSANDATMOSPHEREVALIDATION.CPP
============================================================================================================================================*/
// 🧪 End-to-end Vulkan GPU + Editor UI + Camera Component Layerstack validation for:
//    Moon, Fog (Height / Aerial / Local), Lens Flare, Motion Blur (CameraVelocity / Directional), Exposure, and Editor Camera Layerstack

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Editor/Inspectors/CameraInspector.h"
#include "Engine/Editor/Inspectors/InspectorRegistry.h"
#include "Engine/Editor/WorldEnvironmentRegistry.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;

struct ValidationScorecard
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++Passed;
            std::printf("[CameraEffectsAndAtmosphereValidation][PASS] %s\n", Label);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[CameraEffectsAndAtmosphereValidation][FAIL] %s\n", Label);
        }
    }
};

[[nodiscard]] std::filesystem::path ResolveRepositoryRootPath()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#else
    return std::filesystem::current_path();
#endif
}

[[nodiscard]] std::filesystem::path ResolveShaderDir()
{
#ifdef FRONTIER_COMPILED_SHADER_DIR
    return std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
#else
    return ResolveRepositoryRootPath() / "build" / "CompiledShaders";
#endif
}

[[nodiscard]] bool WriteRgba8Png(
    const std::filesystem::path& Path,
    uint32_t                     Width,
    uint32_t                     Height,
    const std::vector<uint8_t>&  Rgba)
{
    if (Rgba.size() < static_cast<std::size_t>(Width) * Height * 4u) return false;
    Frontier::Exhibits::SoftwareRasterCanvas Canvas(Width, Height, Frontier::Components::RgbaColor8{ 0, 0, 0, 255 });
    Canvas.BlitRgbaRegion(0u, 0u, Width, Height, Rgba.data());
    return Canvas.WritePngFile(Path);
}

[[nodiscard]] double ComputeMeanLuminance(const std::vector<uint8_t>& Rgba) noexcept
{
    if (Rgba.size() < 4u) return 0.0;
    const std::size_t Pixels = Rgba.size() / 4u;
    double Sum = 0.0;
    for (std::size_t I = 0u; I < Pixels; ++I)
    {
        const double R = Rgba[I * 4u + 0u];
        const double G = Rgba[I * 4u + 1u];
        const double B = Rgba[I * 4u + 2u];
        Sum += 0.2126 * R + 0.7152 * G + 0.0722 * B;
    }
    return Sum / static_cast<double>(Pixels);
}

[[nodiscard]] double ComputeMeanAbsDiff(
    const std::vector<uint8_t>& A,
    const std::vector<uint8_t>& B) noexcept
{
    const std::size_t N = std::min(A.size(), B.size());
    if (N < 4u) return 0.0;
    const std::size_t Pixels = N / 4u;
    double Sum = 0.0;
    for (std::size_t I = 0u; I < Pixels; ++I)
    {
        Sum += std::abs(static_cast<int>(A[I * 4u + 0u]) - static_cast<int>(B[I * 4u + 0u]));
        Sum += std::abs(static_cast<int>(A[I * 4u + 1u]) - static_cast<int>(B[I * 4u + 1u]));
        Sum += std::abs(static_cast<int>(A[I * 4u + 2u]) - static_cast<int>(B[I * 4u + 2u]));
    }
    return Sum / static_cast<double>(Pixels * 3u);
}

[[nodiscard]] double ComputePeakRegionDiff(
    const std::vector<uint8_t>& A,
    const std::vector<uint8_t>& B) noexcept
{
    const std::size_t N = std::min(A.size(), B.size());
    double Peak = 0.0;
    for (std::size_t I = 0u; I + 3u < N; I += 4u)
    {
        const double DR = std::abs(static_cast<double>(A[I + 0u]) - static_cast<double>(B[I + 0u]));
        const double DG = std::abs(static_cast<double>(A[I + 1u]) - static_cast<double>(B[I + 1u]));
        const double DB = std::abs(static_cast<double>(A[I + 2u]) - static_cast<double>(B[I + 2u]));
        Peak = std::max(Peak, std::max(DR, std::max(DG, DB)));
    }
    return Peak;
}

[[nodiscard]] double ComputeHorizontalGradientEnergy(
    const std::vector<uint8_t>& Rgba,
    uint32_t                    Width,
    uint32_t                    Height) noexcept
{
    if (Width < 3u || Height < 3u) return 0.0;
    double Sum = 0.0;
    uint64_t Count = 0u;
    for (uint32_t Y = Height / 6u; Y < (Height * 5u) / 6u; ++Y)
    {
        for (uint32_t X = 1u; X + 1u < Width; ++X)
        {
            const std::size_t L = (static_cast<std::size_t>(Y) * Width + (X - 1u)) * 4u;
            const std::size_t R = (static_cast<std::size_t>(Y) * Width + (X + 1u)) * 4u;
            const double LumL = 0.2126 * Rgba[L] + 0.7152 * Rgba[L + 1u] + 0.0722 * Rgba[L + 2u];
            const double LumR = 0.2126 * Rgba[R] + 0.7152 * Rgba[R + 1u] + 0.0722 * Rgba[R + 2u];
            Sum += std::abs(LumR - LumL);
            ++Count;
        }
    }
    return (Count > 0u) ? (Sum / static_cast<double>(Count)) : 0.0;
}

void ComposeSideBySideStrip(
    const std::vector<std::vector<uint8_t>>& Panels,
    uint32_t                                 PanelW,
    uint32_t                                 PanelH,
    std::vector<uint8_t>&                    OutRgba,
    uint32_t&                                OutW,
    uint32_t&                                OutH)
{
    const uint32_t Count = static_cast<uint32_t>(Panels.size());
    OutW = PanelW * Count;
    OutH = PanelH;
    OutRgba.assign(static_cast<std::size_t>(OutW) * OutH * 4u, 255u);
    for (uint32_t P = 0u; P < Count; ++P)
    {
        for (uint32_t Y = 0u; Y < PanelH; ++Y)
        {
            const uint8_t* SrcRow = Panels[P].data() + static_cast<std::size_t>(Y) * PanelW * 4u;
            uint8_t*       DstRow = OutRgba.data() + (static_cast<std::size_t>(Y) * OutW + P * PanelW) * 4u;
            std::copy_n(SrcRow, static_cast<std::size_t>(PanelW) * 4u, DstRow);
        }
    }
}

void ApplyDisplaySettingsToRenderConfig(
    const ViewportDisplaySettings& Display,
    WorldRenderConfiguration&      Config) noexcept
{
    Config.ExposureGain                  = Display.ExposureGain;
    Config.PostProcessSaturation         = Display.PostProcessSaturation;
    Config.PostProcessContrast           = Display.PostProcessContrast;
    Config.PostProcessVignette           = Display.PostProcessVignette;
    Config.MotionBlurEnabled             = Display.MotionBlurEnabled;
    Config.MotionBlurStyleIndex          = Display.MotionBlurStyleIndex;
    Config.MotionBlurShutterAngleDeg     = Display.MotionBlurShutterAngleDeg;
    Config.MotionBlurIntensity           = Display.MotionBlurIntensity;
    Config.MotionBlurMaxRadiusPx         = Display.MotionBlurMaxRadiusPx;
    Config.MotionBlurSampleCount         = Display.MotionBlurSampleCount;
    Config.MotionBlurDirectionalAngleRad = Display.MotionBlurDirectionalAngleRad;
}

bool RenderInspectorPng(
    InspectorRegistry&       Inspectors,
    OutlinerPanel&           Outliner,
    WorldSceneSpecification& Scene,
    ViewportDisplaySettings& DisplaySettings,
    OutlinerSlotToken        TargetToken,
    const std::filesystem::path& OutPath,
    const PaneInputReading&  Input = {})
{
    constexpr int W = 360;
    constexpr int H = 860;
    Outliner.SelectSingle(TargetToken);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.DisplaySize = ImVec2(static_cast<float>(W), static_cast<float>(H));
    Io.DeltaTime   = 1.0f / 60.0f;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(W), static_cast<float>(H)), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##CameraEffectsUiStage", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

    PixelSpace Surface(ImGui::GetWindowDrawList(), Point2D{ 0.0f, 0.0f }, 1.0f, ImGui::GetFont());
    bool Mutated = false;
    const bool Handled = Inspectors.ConstructActiveInspector(
        Surface,
        Rectangle2D{ 0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H) },
        Outliner,
        nullptr,
        Scene,
        &DisplaySettings,
        Input,
        Mutated);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::Render();

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(W, H, Frontier::Components::RgbaColor8{ 18, 18, 18, 255 });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    return Handled && Canvas.WritePngFile(OutPath);
}

} // namespace

int main()
{
    ValidationScorecard Score {};
    const auto RepoRoot  = ResolveRepositoryRootPath();
    const auto ShaderDir = ResolveShaderDir();

    std::filesystem::path OutDir = "/tmp/frontier-camera-effects-validation";
    if (const char* EnvOut = std::getenv("FRONTIER_VALIDATION_OUT_DIR"); EnvOut != nullptr && EnvOut[0] != '\0')
    {
        OutDir = EnvOut;
    }
    std::error_code Ec {};
    std::filesystem::create_directories(OutDir, Ec);

    //--------------------------------------------------------------------------------------------------------------------
    // 1. UI & CAMERA COMPONENT LAYERSTACK VERIFICATION (Outliner + Construct Registry + InspectorRegistry)
    //--------------------------------------------------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;

    TypographyRepository Typography {};
    (void)Typography.LoadRepository(RepoRoot / "EngineContent" / "FontsArchives");
    TypographyRepository::AssignCurrent(&Typography);
    if (ImFont* DefaultFont = Typography.QueryTabStripFont())
    {
        Io.FontDefault = DefaultFont;
    }
    unsigned char* FontPixels = nullptr;
    int            AtlasW     = 0;
    int            AtlasH     = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

    OutlinerPanel            Outliner {};
    WorldEnvironmentRegistry EnvReg {};
    InspectorRegistry        Inspectors {};
    ViewportDisplaySettings  Display {};

    constexpr uint32_t W = 480u;
    constexpr uint32_t H = 270u;

    ShowcaseWorldSceneBundle Bundle {};
    BuildVehicleClearSkyDefaultScene(Bundle, W, H);
    Score.Verify(!Bundle.Objects.empty(), "Built VehicleClearSkyDefaultScene for validation");

    WorldSceneSpecification& Scene = Bundle.Scene;
    Scene.Atmosphere.Medium.AtmosphereEnabled = true;
    Scene.Atmosphere.Solar.SolarEnabled       = true;
    Scene.Atmosphere.Solar.LocalHours         = 10.5f;
    Scene.Atmosphere.Clouds.CloudsEnabled     = false;

    EnvReg.AdoptInitialSceneState(Outliner, Scene, &Display);

    // Verify Construct Registry exposes Moon, Fog (Height/Aerial/Local), Lens Flare, Motion Blur, and Exposure
    const auto ConstructEntries = EnvReg.BuildConstructRegistry();
    auto HasConstructKey = [&](std::string_view Key) -> bool
    {
        for (const auto& E : ConstructEntries)
        {
            if (E.RegistryKey == Key) return true;
        }
        return false;
    };
    Score.Verify(HasConstructKey("env:moon"),        "Construct registry exposes Moon (env:moon)");
    Score.Verify(HasConstructKey("env:height-fog"),  "Construct registry exposes Height Fog (env:height-fog)");
    Score.Verify(HasConstructKey("env:aerial-fog"),  "Construct registry exposes Atmospheric Fog (env:aerial-fog)");
    Score.Verify(HasConstructKey("env:local-fog"),   "Construct registry exposes Local Fog (env:local-fog)");
    Score.Verify(HasConstructKey("env:lens-flare"),  "Construct registry exposes Lens Flare (env:lens-flare)");
    Score.Verify(HasConstructKey("env:motion-blur"), "Construct registry exposes Motion Blur (env:motion-blur)");
    Score.Verify(HasConstructKey("env:exposure"),    "Construct registry exposes Exposure (env:exposure)");

    // Locate or create Editor Camera in Outliner
    OutlinerSlotToken EditorCameraToken {};
    for (const auto& E : Outliner.QueryEntries())
    {
        if (E.DomainTag == "camera:perspective" || E.Title == "Editor Camera")
        {
            EditorCameraToken = E.Token;
            break;
        }
    }
    if (!EditorCameraToken.Valid())
    {
        const OutlinerSlotToken CamFolder = Outliner.ConstructFolder("Cameras");
        OutlinerRecordEntry CamRec {};
        CamRec.EnclosingToken    = CamFolder;
        CamRec.Category          = ConstructSlotCategory::OpticsCamera;
        CamRec.Title             = "Editor Camera";
        CamRec.DomainTag         = "camera:perspective";
        CamRec.CategoryLabel     = "Cameras / Optics";
        CamRec.VisibilityEnabled = true;
        CamRec.Expanded          = true;
        EditorCameraToken        = Outliner.ConstructDirectEntry(std::move(CamRec));
    }
    Score.Verify(EditorCameraToken.Valid(), "Located Editor Camera slot in Outliner");

    // Stack Lens Flare, Motion Blur, and Exposure directly on Editor Camera via CameraInspector layerstack API
    const OutlinerSlotToken FlareTok = CameraInspector::AttachCameraComponent(
        Outliner, Inspectors.AccessInstances(), Scene, EditorCameraToken, "env:lens-flare");
    const OutlinerSlotToken BlurTok = CameraInspector::AttachCameraComponent(
        Outliner, Inspectors.AccessInstances(), Scene, EditorCameraToken, "env:motion-blur");
    const OutlinerSlotToken ExpTok = CameraInspector::AttachCameraComponent(
        Outliner, Inspectors.AccessInstances(), Scene, EditorCameraToken, "env:exposure");

    Score.Verify(FlareTok.Valid() && Outliner.FindEntry(FlareTok)->EnclosingToken == EditorCameraToken,
                 "Lens Flare attached directly under Editor Camera layerstack");
    Score.Verify(BlurTok.Valid() && Outliner.FindEntry(BlurTok)->EnclosingToken == EditorCameraToken,
                 "Motion Blur attached directly under Editor Camera layerstack");
    Score.Verify(ExpTok.Valid() && Outliner.FindEntry(ExpTok)->EnclosingToken == EditorCameraToken,
                 "Exposure attached directly under Editor Camera layerstack");

    // Configure component parameters and verify CombineIntoScene syncs them live
    Inspectors.AccessInstances().AccessExposure(ExpTok).ExposureEv = 1.5f;
    Inspectors.AccessInstances().CombineIntoScene(Outliner, Scene, &Display);
    Score.Verify(Scene.Atmosphere.LensFlare.Enabled, "CombineIntoScene enables Lens Flare from camera stack");
    Score.Verify(Display.MotionBlurEnabled, "CombineIntoScene enables Motion Blur from camera stack");
    Score.Verify(std::abs(Display.ExposureGain - std::pow(2.0f, 1.5f)) < 0.02f,
                 "CombineIntoScene maps Exposure +1.5 EV -> 2.83x linear gain");

    // Render UI snapshots of Editor Camera layerstack + Exposure Inspector + Motion Blur Inspector
    Score.Verify(
        RenderInspectorPng(Inspectors, Outliner, Scene, Display, EditorCameraToken, OutDir / "07_camera_layerstack_ui.png"),
        "Rendered CameraInspector with stacked Lens Flare / Motion Blur / Exposure layers");
    Score.Verify(
        RenderInspectorPng(Inspectors, Outliner, Scene, Display, ExpTok, OutDir / "08_exposure_inspector_ui.png"),
        "Rendered ExposureInspector UI (EV meter & presets)");

    // Verify visibility toggle disables Motion Blur & deleting Lens Flare resets Scene.Atmosphere.LensFlare.Enabled
    Outliner.AccessEntry(BlurTok)->VisibilityEnabled = false;
    Inspectors.AccessInstances().CombineIntoScene(Outliner, Scene, &Display);
    Score.Verify(!Display.MotionBlurEnabled, "Hiding Motion Blur layer immediately disables GPU motion blur");

    Outliner.AccessEntry(FlareTok)->VisibilityEnabled = false;
    Outliner.AccessEntry(ExpTok)->VisibilityEnabled   = false;
    Inspectors.AccessInstances().CombineIntoScene(Outliner, Scene, &Display);
    Score.Verify(!Scene.Atmosphere.LensFlare.Enabled, "Hiding Lens Flare layer immediately disables GPU lens flare");
    Score.Verify(std::abs(Display.ExposureGain - 1.0f) < 0.01f, "Hiding Exposure layer cleanly restores neutral 1.0x gain");

    //--------------------------------------------------------------------------------------------------------------------
    // 2. VULKAN GPU RENDER VERIFICATION (Moon, Fog, Lens Flare, Exposure, Motion Blur)
    //--------------------------------------------------------------------------------------------------------------------
    VulkanInterchange VkDevice {};
    const bool VkOk = VkDevice.InitializeInterchange(RepoRoot);
    Score.Verify(VkOk, "VulkanInterchange initialized headless GPU device");
    if (!VkOk)
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    VulkanWorldRenderSequence Renderer {};
    const bool SeqOk = Renderer.InitializeSequence(VkDevice, ShaderDir);
    Score.Verify(SeqOk, "VulkanWorldRenderSequence initialized (including WorldViewportMotionBlurCompute.comp.spv)");
    if (!SeqOk)
    {
        VkDevice.TeardownInterchange();
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    Renderer.SynchronizeSceneResources(Scene, true);
    Score.Verify(Renderer.QueryReady(), "Synchronized scene resources to GPU");

    WorldRenderConfiguration BaseCfg {};
    BaseCfg.Engine             = WorldRenderEngine::OpenPbrDirect;
    BaseCfg.ShadowAlgorithm    = SolarShadowAlgorithm::ContactPcss;
    BaseCfg.QualityTier        = 1u;
    BaseCfg.DebugChannel       = 0u;
    BaseCfg.ReflectionsEnabled = true;

    PerspectiveCameraBasis MainCam = Bundle.EditorCamera;
    MainCam.ViewportWidthPx        = W;
    MainCam.ViewportHeightPx       = H;

    // 2A. Baseline Clear Sky Frame
    std::vector<uint8_t> BaselineRgba {};
    {
        WorldRenderConfiguration Cfg = BaseCfg;
        ApplyDisplaySettingsToRenderConfig(Display, Cfg);
        const bool Ok = Renderer.RenderWorldViewportWithEngine(Cfg, 2u, MainCam, Scene, BaselineRgba);
        Score.Verify(Ok && BaselineRgba.size() == static_cast<std::size_t>(W) * H * 4u,
                     "Rendered GPU baseline clear-sky frame");
        Score.Verify(WriteRgba8Png(OutDir / "01_baseline.png", W, H, BaselineRgba),
                     "Saved 01_baseline.png");
    }

    // 2B. MOON TEST (nocturnal sky + Luna Moon + aimed camera)
    {
        WorldSceneSpecification MoonScene = Scene;
        MoonScene.Atmosphere.Medium.AtmosphereEnabled = true;
        MoonScene.Atmosphere.Solar.LocalHours         = 22.5f;
        MoonScene.Atmosphere.Solar.SolarEnabled       = true;
        MoonScene.Atmosphere.Stars.StarfieldEnabled   = true;
        MoonScene.Atmosphere.Clouds.CloudsEnabled     = false;

        auto& M0 = MoonScene.Atmosphere.Moons[0];
        M0.Visible                = true;
        M0.SurfacePreset          = LunarSurfacePreset::Luna;
        M0.SynodicPhaseFraction   = 0.72f;
        M0.BrightnessMultiplier   = 2.60f;
        M0.HaloGlowMultiplier     = 1.15f;
        M0.AngularDiameterDegrees = 11.0f;
        M0.AzimuthDegrees         = 310.0f;
        M0.ElevationDegrees       = 30.0f;
        M0.HasAtmosphere          = true;
        M0.AtmosphereIntensity    = 0.45f;

        const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(MoonScene.Atmosphere.Solar);
        const auto MoonDir     = LunarStarfieldSequence::ResolveMoonWorldDirection(M0, SolarSolved.WorldDirectionToSun);

        PerspectiveCameraBasis MoonCam = MainCam;
        MoonCam.CameraOrigin       = { 0.0f, -6.0f, 1.8f };
        MoonCam.TargetPoint        = MoonCam.CameraOrigin + MoonDir * 20.0f;
        MoonCam.VerticalFovRadians = 42.0f * (3.14159265f / 180.0f);

        std::vector<uint8_t> MoonOnRgba {};
        std::vector<uint8_t> MoonOffRgba {};
        const bool OkOn = Renderer.RenderWorldViewportWithEngine(BaseCfg, 2u, MoonCam, MoonScene, MoonOnRgba);
        M0.Visible = false;
        const bool OkOff = Renderer.RenderWorldViewportWithEngine(BaseCfg, 2u, MoonCam, MoonScene, MoonOffRgba);

        const double MoonPeakDiff = ComputePeakRegionDiff(MoonOnRgba, MoonOffRgba);
        const double MoonMeanDiff = ComputeMeanAbsDiff(MoonOnRgba, MoonOffRgba);
        std::printf("[Metrics] Moon: PeakDiff=%.1f MeanDiff=%.2f\n", MoonPeakDiff, MoonMeanDiff);
        Score.Verify(OkOn && OkOff && MoonPeakDiff > 55.0 && MoonMeanDiff > 1.0,
                     "GPU Moon renders bright textured lunar disc over nocturnal sky");
        Score.Verify(WriteRgba8Png(OutDir / "02_moon.png", W, H, MoonOnRgba), "Saved 02_moon.png");
    }

    // 2C. FOG TEST (Height Fog + Atmospheric Aerial Fog + Local Fog via Outliner + CombineIntoScene)
    {
        const OutlinerSlotToken HfTok = EnvReg.RegisterFromRegistryKey("env:height-fog", "Height Fog", Outliner, Scene, &Display);
        const OutlinerSlotToken AfTok = EnvReg.RegisterFromRegistryKey("env:aerial-fog", "Atmospheric Fog", Outliner, Scene, &Display);
        const OutlinerSlotToken LfTok = EnvReg.RegisterFromRegistryKey("env:local-fog",  "Local Fog", Outliner, Scene, &Display);
        Score.Verify(HfTok.Valid() && AfTok.Valid() && LfTok.Valid(),
                     "Constructed Height Fog, Atmospheric Fog, and Local Fog via WorldEnvironmentRegistry");

        auto& HF = Inspectors.AccessInstances().AccessHeightFog(HfTok, Scene).HeightFog;
        HF.Enabled             = true;
        HF.Density             = 0.045f;
        HF.FalloffHeightMeters = 18.0f;
        HF.BaseAltitudeMeters  = 0.0f;
        HF.SunScatterIntensity = 1.40f;

        auto& AF = Inspectors.AccessInstances().AccessAtmosphericFog(AfTok, Scene).AerialFog;
        AF.Enabled             = true;
        AF.DensityMultiplier   = 1.85f;
        AF.StartDistanceMeters = 2.0f;

        auto& LF = Inspectors.AccessInstances().AccessLocalFog(LfTok, Scene).LocalFog;
        LF.Enabled             = true;
        LF.VolumeCenterMeters  = { 0.0f, 0.5f, 1.2f };
        LF.HalfExtentMeters    = { 6.5f, 6.5f, 2.8f };
        LF.Density             = 1.45f;
        LF.Coverage            = 0.72f;

        Inspectors.AccessInstances().CombineIntoScene(Outliner, Scene, &Display);
        Score.Verify(Scene.Atmosphere.Fog.HeightFog.Enabled &&
                     Scene.Atmosphere.Fog.AerialFog.Enabled &&
                     !Scene.Atmosphere.Fog.LocalFogs.empty(),
                     "CombineIntoScene populates HeightFog + AerialFog + LocalFog into WorldScene");

        std::vector<uint8_t> FogRgba {};
        WorldRenderConfiguration Cfg = BaseCfg;
        ApplyDisplaySettingsToRenderConfig(Display, Cfg);
        const bool Ok = Renderer.RenderWorldViewportWithEngine(Cfg, 2u, MainCam, Scene, FogRgba);
        const double FogDiff = ComputeMeanAbsDiff(FogRgba, BaselineRgba);
        std::printf("[Metrics] Fog (Height+Aerial+Local): MeanDiff vs baseline=%.2f\n", FogDiff);
        Score.Verify(Ok && FogDiff > 8.0, "GPU Volumetric Fog (Height + Aerial + Local) visibly scatters across scene");
        Score.Verify(WriteRgba8Png(OutDir / "03_fog.png", W, H, FogRgba), "Saved 03_fog.png");

        // Clean up fog entries for subsequent tests
        Outliner.DeleteSlot(HfTok);
        Outliner.DeleteSlot(AfTok);
        Outliner.DeleteSlot(LfTok);
        Inspectors.AccessInstances().EraseEntry(HfTok);
        Inspectors.AccessInstances().EraseEntry(AfTok);
        Inspectors.AccessInstances().EraseEntry(LfTok);
        Inspectors.AccessInstances().CombineIntoScene(Outliner, Scene, &Display);
        Score.Verify(!Scene.Atmosphere.Fog.HeightFog.Enabled && !Scene.Atmosphere.Fog.AerialFog.Enabled && Scene.Atmosphere.Fog.LocalFogs.empty(),
                     "Deleting Fog entries cleanly disables all GPU fog");
    }

    // 2D. LENS FLARE TEST (driven via Editor Camera component stack!)
    {
        WorldSceneSpecification FlareScene = Scene;
        FlareScene.Atmosphere.Medium.AtmosphereEnabled = true;
        FlareScene.Atmosphere.Solar.SolarEnabled       = true;
        FlareScene.Atmosphere.Solar.LocalHours         = 16.8f; // low golden afternoon sun in frame
        const auto SolvedSun = CelestialEphemerisSolver::SolveSolarEphemeris(FlareScene.Atmosphere.Solar);

        Outliner.AccessEntry(FlareTok)->VisibilityEnabled = true;
        auto& LF = Inspectors.AccessInstances().AccessLensFlare(FlareTok, FlareScene).Flare;
        LF.Enabled        = true;
        LF.Style          = LensFlareStyle::Anamorphic;
        LF.FlareIntensity = 1.65f;
        LF.FlareSpread    = 1.10f;
        LF.HaloIntensity  = 1.10f;
        LF.GhostIntensity = 0.90f;
        Inspectors.AccessInstances().CombineIntoScene(Outliner, FlareScene, &Display);

        PerspectiveCameraBasis FlareCam = MainCam;
        FlareCam.CameraOrigin = { 0.0f, -6.0f, 1.4f };
        // Aim slightly off-axis from the sun so streaks + ghosts sweep across the sky & showcase
        const Mathematics::Axis3D RawAim = SolvedSun.WorldDirectionToSun + Mathematics::Axis3D{ 0.14f, 0.0f, -0.05f };
        const float AimLen = std::max(1.0e-5f, std::sqrt(RawAim.X * RawAim.X + RawAim.Y * RawAim.Y + RawAim.Z * RawAim.Z));
        const Mathematics::Axis3D AimDir { RawAim.X / AimLen, RawAim.Y / AimLen, RawAim.Z / AimLen };
        FlareCam.TargetPoint = FlareCam.CameraOrigin + AimDir * 15.0f;

        std::vector<uint8_t> FlareOnRgba {};
        std::vector<uint8_t> FlareOffRgba {};
        const bool OkOn = Renderer.RenderWorldViewportWithEngine(BaseCfg, 2u, FlareCam, FlareScene, FlareOnRgba);

        Outliner.AccessEntry(FlareTok)->VisibilityEnabled = false;
        Inspectors.AccessInstances().CombineIntoScene(Outliner, FlareScene, &Display);
        const bool OkOff = Renderer.RenderWorldViewportWithEngine(BaseCfg, 2u, FlareCam, FlareScene, FlareOffRgba);

        const double FlarePeak = ComputePeakRegionDiff(FlareOnRgba, FlareOffRgba);
        const double FlareMean = ComputeMeanAbsDiff(FlareOnRgba, FlareOffRgba);
        std::printf("[Metrics] LensFlare: PeakDiff=%.1f MeanDiff=%.2f\n", FlarePeak, FlareMean);
        Score.Verify(OkOn && OkOff && FlarePeak > 30.0 && FlareMean > 1.5,
                     "GPU Baked Lens Flare attached to Editor Camera renders visible streaks/halos/ghosts");
        Score.Verify(WriteRgba8Png(OutDir / "04_lens_flare.png", W, H, FlareOnRgba), "Saved 04_lens_flare.png");
    }

    // 2E. EXPOSURE COMPONENT TEST (-2 EV vs 0 EV vs +2 EV driven by Editor Camera Exposure component)
    {
        Outliner.AccessEntry(ExpTok)->VisibilityEnabled = true;
        auto& ExpInst = Inspectors.AccessInstances().AccessExposure(ExpTok);
        ExpInst.Enabled = true;
        ExpInst.BiasMultiplier = 1.0f;

        std::array<float, 3u> Stops = {{ -2.0f, 0.0f, +2.0f }};
        std::vector<std::vector<uint8_t>> Panels(3u);
        std::array<double, 3u> Means {};

        for (std::size_t I = 0u; I < 3u; ++I)
        {
            ExpInst.ExposureEv = Stops[I];
            Inspectors.AccessInstances().CombineIntoScene(Outliner, Scene, &Display);
            WorldRenderConfiguration Cfg = BaseCfg;
            ApplyDisplaySettingsToRenderConfig(Display, Cfg);
            const bool Ok = Renderer.RenderWorldViewportWithEngine(Cfg, 2u, MainCam, Scene, Panels[I]);
            Score.Verify(Ok, "Rendered Exposure stop panel on GPU");
            Means[I] = ComputeMeanLuminance(Panels[I]);
        }
        std::printf("[Metrics] Exposure MeanLuma: -2EV=%.2f  0EV=%.2f  +2EV=%.2f\n", Means[0], Means[1], Means[2]);
        Score.Verify(Means[0] + 15.0 < Means[1] && Means[1] + 15.0 < Means[2],
                     "GPU Exposure strictly monotonic: Mean(-2 EV) < Mean(0 EV) < Mean(+2 EV)");

        std::vector<uint8_t> Strip {};
        uint32_t StripW = 0u, StripH = 0u;
        ComposeSideBySideStrip(Panels, W, H, Strip, StripW, StripH);
        Score.Verify(WriteRgba8Png(OutDir / "05_exposure_comparison.png", StripW, StripH, Strip),
                     "Saved 05_exposure_comparison.png (-2 EV | 0 EV | +2 EV)");

        Outliner.AccessEntry(ExpTok)->VisibilityEnabled = false;
        Inspectors.AccessInstances().CombineIntoScene(Outliner, Scene, &Display);
    }

    // 2F. MOTION BLUR COMPONENT TEST (Sharp vs 2-Frame CameraVelocity Pan vs DirectionalShutter)
    {
        Outliner.AccessEntry(BlurTok)->VisibilityEnabled = true;
        auto& MB = Inspectors.AccessInstances().AccessMotionBlur(BlurTok);
        MB.Enabled             = true;
        MB.ShutterAngleDegrees = 240.0f;
        MB.VelocityScale       = 1.60f;
        MB.MaxSampleRadiusPx   = 28.0f;
        MB.SampleCount         = 14u;

        // Prime previous camera basis with frame 0 at MainCam
        std::vector<uint8_t> SharpRgba {};
        {
            WorldRenderConfiguration Cfg = BaseCfg;
            Cfg.MotionBlurEnabled = false;
            (void)Renderer.RenderWorldViewportWithEngine(Cfg, 2u, MainCam, Scene, SharpRgba);
        }

        // Frame 1: pan camera horizontally by 0.65 m with Style=0 (CameraVelocity reprojection!)
        PerspectiveCameraBasis PanCam = MainCam;
        PanCam.CameraOrigin.X += 0.65f;
        PanCam.TargetPoint.X  += 0.20f;

        MB.StyleIndex = 0u; // CameraVelocity
        Inspectors.AccessInstances().CombineIntoScene(Outliner, Scene, &Display);
        std::vector<uint8_t> CamVelBlurRgba {};
        {
            WorldRenderConfiguration Cfg = BaseCfg;
            ApplyDisplaySettingsToRenderConfig(Display, Cfg);
            const bool Ok = Renderer.RenderWorldViewportWithEngine(Cfg, 2u, PanCam, Scene, CamVelBlurRgba);
            Score.Verify(Ok, "Rendered GPU CameraVelocity reprojection Motion Blur pass");
        }

        // Render sharp reference at the exact same PanCam pose to compare edge blur energy
        std::vector<uint8_t> PanSharpRgba {};
        {
            WorldRenderConfiguration Cfg = BaseCfg;
            Cfg.MotionBlurEnabled = false;
            (void)Renderer.RenderWorldViewportWithEngine(Cfg, 2u, PanCam, Scene, PanSharpRgba);
        }

        // DirectionalShutter mode (Style=1)
        MB.StyleIndex          = 1u;
        MB.DirectionalAngleDeg = 0.0f;
        Inspectors.AccessInstances().CombineIntoScene(Outliner, Scene, &Display);
        std::vector<uint8_t> DirBlurRgba {};
        {
            WorldRenderConfiguration Cfg = BaseCfg;
            ApplyDisplaySettingsToRenderConfig(Display, Cfg);
            const bool Ok = Renderer.RenderWorldViewportWithEngine(Cfg, 2u, PanCam, Scene, DirBlurRgba);
            Score.Verify(Ok, "Rendered GPU DirectionalShutter Motion Blur pass");
        }

        const double SharpGrad  = ComputeHorizontalGradientEnergy(PanSharpRgba, W, H);
        const double CamVelGrad = ComputeHorizontalGradientEnergy(CamVelBlurRgba, W, H);
        const double DirGrad    = ComputeHorizontalGradientEnergy(DirBlurRgba, W, H);
        const double CamVelDiff = ComputeMeanAbsDiff(CamVelBlurRgba, PanSharpRgba);
        const double DirDiff    = ComputeMeanAbsDiff(DirBlurRgba, PanSharpRgba);
        std::printf("[Metrics] MotionBlur: SharpGrad=%.2f CamVelGrad=%.2f (Diff=%.2f) DirGrad=%.2f (Diff=%.2f)\n",
                    SharpGrad, CamVelGrad, CamVelDiff, DirGrad, DirDiff);
        Score.Verify(CamVelDiff > 1.5 && CamVelGrad < SharpGrad,
                     "GPU CameraVelocity Motion Blur measurably blurs moving-camera edges");
        Score.Verify(DirDiff > 2.0 && DirGrad < SharpGrad,
                     "GPU DirectionalShutter Motion Blur measurably smears shutter streaks");

        std::vector<uint8_t> BlurStrip {};
        uint32_t BlurStripW = 0u, BlurStripH = 0u;
        ComposeSideBySideStrip({ PanSharpRgba, CamVelBlurRgba, DirBlurRgba }, W, H, BlurStrip, BlurStripW, BlurStripH);
        Score.Verify(WriteRgba8Png(OutDir / "06_motion_blur.png", BlurStripW, BlurStripH, BlurStrip),
                     "Saved 06_motion_blur.png (Sharp | CameraVelocity | DirectionalShutter)");
    }

    Renderer.TeardownSequence();
    VkDevice.TeardownInterchange();
    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiCtx);

    std::printf("\n[CameraEffectsAndAtmosphereValidation] Summary: %u passed, %u failed (outputs: %s)\n",
                Score.Passed, Score.Failed, OutDir.generic_string().c_str());
    return (Score.Failed == 0u) ? 0 : 1;
}
