/*============================================================================================================================================
                                                      COLOURCOMPONENTSVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end UI validation & visual proof for ContentBrowser Colour Ramps drag-drop + reusable ColourRampEditor & ColourSwatchPicker

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ColourRampEditor.h"
#include "Engine/Components/Controls/ColourSwatchPicker.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{

using namespace Frontier;
using namespace Frontier::ColourSpace;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ColourComponentsValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    constexpr uint32_t CanvasW = 1440u;
    constexpr uint32_t CanvasH = 900u;

    ImGuiContext* Gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(Gui);
    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;

    const std::filesystem::path RepositoryRoot = ResolveRepositoryRootPath();

    TypographyRepository Typefaces {};
    if (!VerifyCheck(Typefaces.LoadRepository(RepositoryRoot / "EngineContent/FontsArchives") == 5u, "Load FontRepository.FontSpace"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* DefaultFace = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular))
    {
        Io.FontDefault = DefaultFace;
    }
    unsigned char* FontTexPixels = nullptr;
    int            FontTexW      = 0;
    int            FontTexH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontTexPixels, &FontTexW, &FontTexH);

    ThemeRepository Theme {};
    Theme.ConfigureTheme(Frontier::ThemePreset::Dark);
    Theme.ConfigureAccent(Frontier::AccentPreset::Blue);
    ControlAppearance::ConfigureTheme(Theme);

    ContentBrowserPanel Browser {};
    Browser.AttachThemeRepository(&Theme);
    Browser.ConfigureWorkspaceDomain(WorkspaceCategory::Particle);
    Browser.ConfigureShelfExtent(ContentShelfExtent::SplitHalf, true);
    (void)Browser.ActivateCategory(ContentCategory::Colours);
    Browser.ConfigureColourCollectionScope(ColourCollectionScope::Ramps);

    // Live workspace target: Particle Colour Ramp + Swatch Picker above the SplitHalf ContentBrowser drawer
    ColourSourceProfile ActiveRampProfile = ConstructProfileFromRampPreset(*FindBuiltInColourRampPreset("thermal"));
    ColourRampEditorSession RampSession {};

    LinearRgba ActiveSwatchTint { 0.14f, 0.36f, 0.96f, 1.0f };
    ColourSwatchPickerSession SwatchSession {};

    std::string LastAssignedRampId {};
    bool SwatchCallbackFired = false;

    const Rectangle2D ViewportBounds { 0.0f, 0.0f, static_cast<float>(CanvasW), static_cast<float>(CanvasH) };
    const Rectangle2D RampEditorBounds  { 44.0f,  52.0f, 520.0f, 174.0f };
    const Rectangle2D SwatchEditorBounds{ 604.0f, 52.0f, 320.0f, 260.0f };

    auto RenderStep = [&](const ContentBrowserPointerInput& Input) {
        Io.DisplaySize = ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();

        PixelSpace Canvas(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
        const ColorRgba Bg = Theme.QueryDarkestSurface().ToRgba8();
        Canvas.FillRectangle(ViewportBounds, Bg, 0.0f);
        Canvas.Text({ 44.0f, 20.0f }, 14.0f, ControlAppearance::Colors().Text.ToRgba8(),
            "FRONTIER COLOURSPACE  ·  Reusable ColourRampEditor, ColourSwatchPicker & ContentBrowser Drag-Drop");

        ControlPointer Ptr {};
        Ptr.X        = Input.CursorX;
        Ptr.Y        = Input.CursorY;
        Ptr.Down     = Input.HeldDown;
        Ptr.Pressed  = Input.Pressed;
        Ptr.Released = Input.Released;
        Ptr.Enabled  = !Browser.InspectReading().DraggingColourRampActive && !Browser.InspectReading().DraggingColourActive;

        const bool HighlightRampDrop = Browser.InspectReading().DraggingColourRampActive;
        (void)ColourRampEditor::Construct(
            Canvas,
            RampEditorBounds,
            "Particle Colour Ramp Slot (Drop From ContentBrowser)",
            ActiveRampProfile,
            RampSession,
            Ptr,
            HighlightRampDrop);

        (void)ColourSwatchPicker::Construct(
            Canvas,
            SwatchEditorBounds,
            "Active Stop Swatch (Drop .ColourSwatch)",
            ActiveSwatchTint,
            SwatchSession,
            Ptr,
            true);

        ColourRampEditor::RegisterDropSlot(
            Browser,
            "Particle.ColourRamp",
            "Particle Colour Ramp",
            RampSession,
            &ActiveRampProfile,
            [&](const ColourSourceProfile&, std::string_view AssignedId) {
                LastAssignedRampId = std::string(AssignedId);
            });

        ColourSwatchPicker::RegisterDropSlot(
            Browser,
            "Particle.StopSwatch",
            "Stop Colour Swatch",
            SwatchSession,
            &ActiveSwatchTint,
            [&](LinearRgba) { SwatchCallbackFired = true; });

        Browser.RenderWithInput(Canvas, ViewportBounds, Input, 1.0 / 60.0);
        ImGui::Render();
    };

    const std::filesystem::path OutputDir = ResolveRepositoryRootPath() / "Exhibits/Images/ColourSpace";
    std::filesystem::create_directories(OutputDir);

    auto SavePng = [&](const char* FileName) -> bool {
        const ColorRgba Bg = Theme.QueryDarkestSurface().ToRgba8();
        SoftwareRasterCanvas Raster(CanvasW, CanvasH, RgbaColor8 { Bg.Red, Bg.Green, Bg.Blue, 255u });
        Raster.RasterizeImGuiBatches(ImGui::GetDrawData());
        return VerifyCheck(Raster.WritePngFile(OutputDir / FileName), FileName);
    };

    // Warmup & verify Ramps list populates cleanly
    ContentBrowserPointerInput Idle {};
    for (int W = 0; W < 3; ++W) RenderStep(Idle);

    const std::size_t AuroraIdx = Browser.QueryColourSwatchBrowser().FindRampPresetIndex("aurora");
    const Rectangle2D AuroraCell = Browser.QueryRenderedRampCellBounds(AuroraIdx);
    if (!VerifyCheck(AuroraCell.Width > 50.0f && AuroraCell.Height > 30.0f, "Aurora ramp tile rendered with valid hitbox"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // Press on Aurora ramp tile -> drag up onto ColourRampEditor strip (capture mid-drag screenshot!) -> release drop!
    ContentBrowserPointerInput Press {};
    Press.CursorX  = AuroraCell.Center().X;
    Press.CursorY  = AuroraCell.Center().Y;
    Press.Pressed  = true;
    Press.HeldDown = true;
    RenderStep(Press);

    ContentBrowserPointerInput DragHover {};
    DragHover.CursorX  = RampSession.LastStripBounds.Center().X;
    DragHover.CursorY  = RampSession.LastStripBounds.Center().Y;
    DragHover.HeldDown = true;
    RenderStep(DragHover);

    if (!VerifyCheck(Browser.InspectReading().DraggingColourRampActive, "ColourRamp drag activates across deadzone")
        || !SavePng("01-ColourRampDragGhostHighlight.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    ContentBrowserPointerInput Release {};
    Release.CursorX  = RampSession.LastStripBounds.Center().X;
    Release.CursorY  = RampSession.LastStripBounds.Center().Y;
    Release.Released = true;
    RenderStep(Release);

    if (!VerifyCheck(LastAssignedRampId == "aurora", "Dropping Aurora ramp onto ColourRampEditor slot assigns 'aurora'"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // Render post-drop state & save final studio screenshot
    RenderStep(Idle);
    if (!SavePng("02-ColourComponentsStudioPostDrop.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // Verify Swatch drop-slot on ColourSwatchPicker as well
    Browser.ConfigureColourCollectionScope(ColourCollectionScope::Swatches);
    RenderStep(Idle);
    const std::size_t FigIdx = Browser.QueryColourSwatchBrowser().FindEntryByName("Fig Purple");
    if (!VerifyCheck(Browser.ApplyColourIndexToSlot(FigIdx, "Particle.StopSwatch") && SwatchCallbackFired,
                     "Assigning Fig Purple swatch updates ColourSwatchPicker target"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    ImGui::DestroyContext(Gui);
    std::printf("[ColourComponentsValidation] ALL CHECKS PASSED (ContentBrowser Ramps drag-drop + ColourRampEditor + ColourSwatchPicker + PNG proofs).\n");
    return 0;
}
