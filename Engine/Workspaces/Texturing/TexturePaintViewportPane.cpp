/*==========================================================================================================================================
                                                    TEXTUREPAINTVIEWPORTPANE.CPP
==========================================================================================================================================*/
// 🖼️ Implementation of TexturePaintViewportPane: 2-band CardDock header, real 3D/2D PBR viewport & floating Brush Studio modal.

#include "Engine/Workspaces/Texturing/TexturePaintViewportPane.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace Frontier::Workspaces
{

namespace
{

using namespace TexturingAppearance;

struct ToolButtonSpec
{
    PaintToolCategory Tool;
    ControlIcon       Icon;
    const char*       Title;
};

constexpr std::array<ToolButtonSpec, 9u> ViewportToolCatalog {{
    { PaintToolCategory::Brush,      ControlIcon::BrushTool,      "Paint Brush"    },
    { PaintToolCategory::Eraser,     ControlIcon::EraserTool,     "Eraser"         },
    { PaintToolCategory::Fill,       ControlIcon::FillBucket,     "Fill"           },
    { PaintToolCategory::Gradient3D, ControlIcon::PointGradient,  "3D Gradient"    },
    { PaintToolCategory::Curve3D,    ControlIcon::VectorCurve,    "3D Curve"       },
    { PaintToolCategory::Stamp,      ControlIcon::StampTool,      "Stamp"          },
    { PaintToolCategory::Text,       ControlIcon::TextType,       "Text"           },
    { PaintToolCategory::Sample,     ControlIcon::PipetteDropper, "Eyedropper"     },
    { PaintToolCategory::Move,       ControlIcon::MoveArrows,     "Transform"      },
}};

struct BrushStudioSectionSpec
{
    BrushDetailSubTab SubTab;
    ControlIcon       Icon;
    const char*       Label;
    const char*       CountBadge;
};

constexpr std::array<BrushStudioSectionSpec, 14u> BrushSectionsCatalog {{
    { BrushDetailSubTab::LibraryGrid,    ControlIcon::LayersSlabs,          "Library",         "12" },
    { BrushDetailSubTab::StrokePath,     ControlIcon::NoiseWave,            "Stroke path",     ""   },
    { BrushDetailSubTab::Stabilization,  ControlIcon::MotionActivity,       "Stabilization",   ""   },
    { BrushDetailSubTab::Taper,          ControlIcon::VectorPen,            "Taper",           ""   },
    { BrushDetailSubTab::Shape,          ControlIcon::ShapeCircle,          "Shape",           ""   },
    { BrushDetailSubTab::Grain,          ControlIcon::PatternTile,          "Grain",           ""   },
    { BrushDetailSubTab::Rendering,      ControlIcon::SparklesAntiAliasing, "Rendering",       ""   },
    { BrushDetailSubTab::WetMix,         ControlIcon::FillBucket,           "Wet mix",         ""   },
    { BrushDetailSubTab::ColourDynamics, ControlIcon::AppearanceSwatches,   "Colour dynamics", ""   },
    { BrushDetailSubTab::Curves,         ControlIcon::GaugeRate,            "Dynamics",        ""   },
    { BrushDetailSubTab::Stylus,         ControlIcon::VectorPointer,        "Stylus",          ""   },
    { BrushDetailSubTab::Properties,     ControlIcon::SlidersQuality,       "Properties",      ""   },
    { BrushDetailSubTab::Channels,       ControlIcon::MaterialSphere,       "Materials",       ""   },
    { BrushDetailSubTab::About,          ControlIcon::CircleNotice,         "About",           ""   },
}};

constexpr std::array<BrushStudioSectionSpec, 6u> TextSectionsCatalog {{
    { BrushDetailSubTab::LibraryGrid,    ControlIcon::LayersSlabs,          "Library",         "6"  },
    { BrushDetailSubTab::StrokePath,     ControlIcon::TextType,             "Text",            ""   },
    { BrushDetailSubTab::Properties,     ControlIcon::SlidersQuality,       "Type",            ""   },
    { BrushDetailSubTab::Shape,          ControlIcon::PatternTile,          "Layout",          ""   },
    { BrushDetailSubTab::Rendering,      ControlIcon::FillBucket,           "Look",            ""   },
    { BrushDetailSubTab::Channels,       ControlIcon::MaterialSphere,       "Material",        ""   },
}};

constexpr std::array<BrushStudioSectionSpec, 6u> StampSectionsCatalog {{
    { BrushDetailSubTab::LibraryGrid,    ControlIcon::LayersSlabs,          "Library",         "16" },
    { BrushDetailSubTab::Shape,          ControlIcon::StampTool,            "Decal",           ""   },
    { BrushDetailSubTab::Properties,     ControlIcon::MoveArrows,           "Transform",       ""   },
    { BrushDetailSubTab::Rendering,      ControlIcon::EyeVisible,           "Look",            ""   },
    { BrushDetailSubTab::Channels,       ControlIcon::MaterialSphere,       "Material",        ""   },
    { BrushDetailSubTab::About,          ControlIcon::CircleNotice,         "About",           ""   },
}};

constexpr std::array<BrushStudioSectionSpec, 5u> EraseSectionsCatalog {{
    { BrushDetailSubTab::LibraryGrid,    ControlIcon::EraserTool,           "Eraser",          ""   },
    { BrushDetailSubTab::Properties,     ControlIcon::ShapeCircle,          "Tip",             ""   },
    { BrushDetailSubTab::Curves,         ControlIcon::GaugeRate,            "Feel",            ""   },
    { BrushDetailSubTab::Channels,       ControlIcon::LayersSlabs,          "Channels",        ""   },
    { BrushDetailSubTab::About,          ControlIcon::CircleNotice,         "About erase",     ""   },
}};

constexpr std::array<BrushStudioSectionSpec, 4u> FillSectionsCatalog {{
    { BrushDetailSubTab::Shape,          ControlIcon::PatternTile,          "Scope",           ""   },
    { BrushDetailSubTab::Properties,     ControlIcon::FillBucket,           "Source",          ""   },
    { BrushDetailSubTab::Channels,       ControlIcon::MaterialSphere,       "Material",        ""   },
    { BrushDetailSubTab::About,          ControlIcon::CircleNotice,         "About fill",      ""   },
}};

constexpr std::array<BrushStudioSectionSpec, 6u> GradientSectionsCatalog {{
    { BrushDetailSubTab::Shape,          ControlIcon::ShapeCircle,          "Shape",           ""   },
    { BrushDetailSubTab::LibraryGrid,    ControlIcon::AppearanceSwatches,   "Colours",         "10" },
    { BrushDetailSubTab::Rendering,      ControlIcon::SlidersQuality,       "Blend",           ""   },
    { BrushDetailSubTab::StrokePath,     ControlIcon::VectorPen,            "Line",            ""   },
    { BrushDetailSubTab::Channels,       ControlIcon::LayersSlabs,          "Channels",        ""   },
    { BrushDetailSubTab::About,          ControlIcon::CircleNotice,         "About",           ""   },
}};

constexpr std::array<const char*, 16u> StampShapeCatalog {{
    "Round", "Hard round", "Square", "Flat nib",
    "Chalk", "Graphite", "Bristle", "Splatter",
    "Spray", "Leaf", "Ink blot", "Cloud",
    "Star", "Sparkle", "Hexagon", "Knife"
}};

struct GradientRampPresetSpec
{
    const char* Name;
    ColorRgba   StopA;
    ColorRgba   StopB;
    ColorRgba   StopC;
};

constexpr std::array<GradientRampPresetSpec, 10u> GradientRampPresets {{
    { "Ember Glow",       { 244u,  68u,  54u, 255u }, { 251u, 146u,  60u, 255u }, { 253u, 224u, 118u, 255u } },
    { "Oceanic Mint",     {  20u, 184u, 166u, 255u }, {  56u, 189u, 248u, 255u }, { 167u, 243u, 208u, 255u } },
    { "Iridescent Oil",   { 168u,  85u, 247u, 255u }, {  45u, 212u, 191u, 255u }, { 250u, 204u,  21u, 255u } },
    { "Sunset Neon",      { 236u,  72u, 153u, 255u }, { 249u, 115u,  22u, 255u }, { 250u, 204u,  21u, 255u } },
    { "Oxidized Bronze",  { 120u,  82u,  48u, 255u }, {  68u, 156u, 138u, 255u }, { 174u, 224u, 204u, 255u } },
    { "Earth & Indigo",   { 180u, 116u,  68u, 255u }, {  88u,  96u, 148u, 255u }, {  42u,  52u,  98u, 255u } },
    { "Arctic Aurora",    {  34u, 211u, 238u, 255u }, {  52u, 211u, 153u, 255u }, { 163u, 230u,  53u, 255u } },
    { "Volcanic Magma",   {  48u,  22u,  24u, 255u }, { 220u,  38u,  38u, 255u }, { 251u, 191u,  36u, 255u } },
    { "Gold Foil Patina", { 161u, 118u,  44u, 255u }, { 232u, 194u, 122u, 255u }, { 254u, 243u, 199u, 255u } },
    { "Cyber Berry",      {  59u, 130u, 246u, 255u }, { 168u,  85u, 247u, 255u }, { 244u,  63u,  94u, 255u } },
}};

void ConstructProceduralStampPreview(
    PixelSpace& Surface,
    Rectangle2D WellBounds,
    std::size_t ShapeIdx) noexcept
{
    const Point2D Center{ WellBounds.X + WellBounds.Width * 0.5f, WellBounds.Y + WellBounds.Height * 0.5f };
    const ColorRgba WhiteFull{ 246u, 247u, 249u, 245u };
    const ColorRgba WhiteMid { 246u, 247u, 249u, 135u };
    const ColorRgba WhiteSoft{ 246u, 247u, 249u,  55u };

    switch (ShapeIdx % 16u)
    {
        case 0u: // Round (soft)
            Surface.FillCircle(Center, 17.0f, WhiteSoft);
            Surface.FillCircle(Center, 12.5f, WhiteMid);
            Surface.FillCircle(Center, 7.5f,  WhiteFull);
            break;
        case 1u: // Hard round
            Surface.FillCircle(Center, 15.5f, WhiteFull);
            break;
        case 2u: // Square
            Surface.FillRectangle({ Center.X - 14.0f, Center.Y - 14.0f, 28.0f, 28.0f }, WhiteFull, 2.0f);
            break;
        case 3u: // Flat nib
            Surface.FillRectangle({ Center.X - 20.0f, Center.Y - 5.5f, 40.0f, 11.0f }, WhiteFull, 2.0f);
            break;
        case 4u: // Chalk
            for (int I = 0; I < 28; ++I)
            {
                const float Dx = std::sin(static_cast<float>(I) * 2.3f) * 14.0f;
                const float Dy = std::cos(static_cast<float>(I) * 3.7f) * 10.0f;
                Surface.FillCircle({ Center.X + Dx, Center.Y + Dy }, 3.2f, (I % 2 == 0) ? WhiteFull : WhiteMid);
            }
            break;
        case 5u: // Graphite
            Surface.FillCircle(Center, 15.5f, WhiteSoft);
            for (int I = 0; I < 36; ++I)
            {
                const float A  = static_cast<float>(I) * 0.85f;
                const float R  = static_cast<float>((I * 7) % 14);
                Surface.FillCircle({ Center.X + std::cos(A) * R, Center.Y + std::sin(A) * R }, 1.8f, WhiteFull);
            }
            break;
        case 6u: // Bristle
            for (int I = -2; I <= 2; ++I)
            {
                Surface.FillCircle({ Center.X + std::sin(static_cast<float>(I)) * 4.0f, Center.Y + static_cast<float>(I) * 5.0f }, 3.6f, WhiteMid);
                Surface.FillCircle({ Center.X + std::sin(static_cast<float>(I)) * 3.0f, Center.Y + static_cast<float>(I) * 5.0f }, 2.2f, WhiteFull);
            }
            break;
        case 7u: // Splatter
            Surface.FillCircle({ Center.X - 4.0f, Center.Y + 2.0f }, 5.5f, WhiteFull);
            Surface.FillCircle({ Center.X + 8.0f, Center.Y - 6.0f }, 3.5f, WhiteFull);
            Surface.FillCircle({ Center.X - 11.0f, Center.Y - 7.0f }, 2.4f, WhiteMid);
            Surface.FillCircle({ Center.X + 10.0f, Center.Y + 7.0f }, 2.8f, WhiteFull);
            Surface.FillCircle({ Center.X + 2.0f, Center.Y + 10.0f }, 1.9f, WhiteMid);
            break;
        case 8u: // Spray
            for (int I = 0; I < 40; ++I)
            {
                const float A = static_cast<float>(I) * 1.41f;
                const float R = static_cast<float>((I * 5) % 17);
                Surface.FillCircle({ Center.X + std::cos(A) * R, Center.Y + std::sin(A) * R * 0.8f }, 1.1f, WhiteMid);
            }
            break;
        case 9u: // Leaf
            Surface.FillRectangle({ Center.X - 16.0f, Center.Y - 6.0f, 32.0f, 12.0f }, WhiteFull, 6.0f);
            Surface.StrokeLine({ Center.X - 16.0f, Center.Y }, { Center.X + 16.0f, Center.Y }, ColorRgba{ 24u, 25u, 28u, 220u }, 1.2f);
            break;
        case 10u: // Ink blot
            Surface.FillCircle(Center, 10.5f, WhiteFull);
            Surface.FillCircle({ Center.X - 7.5f, Center.Y + 3.0f }, 7.0f, WhiteFull);
            Surface.FillCircle({ Center.X + 8.0f, Center.Y - 2.0f }, 6.5f, WhiteFull);
            Surface.FillCircle({ Center.X + 3.0f, Center.Y + 7.5f }, 5.5f, WhiteFull);
            break;
        case 11u: // Cloud
            Surface.FillCircle(Center, 11.0f, WhiteSoft);
            Surface.FillCircle({ Center.X - 7.0f, Center.Y + 2.0f }, 8.0f, WhiteMid);
            Surface.FillCircle({ Center.X + 6.0f, Center.Y + 1.0f }, 7.5f, WhiteMid);
            Surface.FillCircle(Center, 6.5f, WhiteFull);
            break;
        case 12u: // Star
            for (int I = 0; I < 5; ++I)
            {
                const float A = static_cast<float>(I) * 1.256637f - 1.570796f;
                Surface.StrokeLine(Center, { Center.X + std::cos(A) * 15.0f, Center.Y + std::sin(A) * 15.0f }, WhiteFull, 3.2f);
            }
            Surface.FillCircle(Center, 5.5f, WhiteFull);
            break;
        case 13u: // Sparkle
            Surface.StrokeLine({ Center.X - 14.0f, Center.Y }, { Center.X + 14.0f, Center.Y }, WhiteFull, 2.0f);
            Surface.StrokeLine({ Center.X, Center.Y - 14.0f }, { Center.X, Center.Y + 14.0f }, WhiteFull, 2.0f);
            Surface.FillCircle(Center, 3.8f, WhiteFull);
            break;
        case 14u: // Hexagon
            Surface.FillRectangle({ Center.X - 12.0f, Center.Y - 12.0f, 24.0f, 24.0f }, WhiteFull, 5.0f);
            break;
        case 15u: // Knife
        default:
            Surface.StrokeLine({ Center.X - 15.0f, Center.Y - 9.0f }, { Center.X + 15.0f, Center.Y + 9.0f }, WhiteFull, 3.6f);
            break;
    }
}

void UploadViewportTexturePixels(
    ImTextureData&              TargetTex,
    uint32_t&                   InOutW,
    uint32_t&                   InOutH,
    uint32_t                    TargetW,
    uint32_t                    TargetH,
    const std::vector<uint8_t>& RgbaPixels) noexcept
{
    if (RgbaPixels.size() < static_cast<std::size_t>(TargetW) * TargetH * 4u || TargetW == 0u || TargetH == 0u)
    {
        return;
    }
    const bool NeedsCreate =
        TargetTex.Width != static_cast<int>(TargetW) ||
        TargetTex.Height != static_cast<int>(TargetH) ||
        TargetTex.Pixels == nullptr;
    if (NeedsCreate)
    {
        TargetTex.DestroyPixels();
        TargetTex.SetStatus(ImTextureStatus_Destroyed);
        TargetTex.Create(ImTextureFormat_RGBA32, static_cast<int>(TargetW), static_cast<int>(TargetH));
        InOutW = TargetW;
        InOutH = TargetH;
    }
    std::memcpy(TargetTex.Pixels, RgbaPixels.data(), static_cast<std::size_t>(TargetW) * TargetH * 4u);
    if (!NeedsCreate && TargetTex.Status == ImTextureStatus_OK)
    {
        TargetTex.UpdateRect = { 0u, 0u, static_cast<unsigned short>(TargetW), static_cast<unsigned short>(TargetH) };
        TargetTex.SetStatus(ImTextureStatus_WantUpdates);
    }
}

void ConstructBrushStrokePreviewWave(
    PixelSpace&                          Surface,
    Rectangle2D                          TileBounds,
    const BrushLibraryPresetDescriptor&  Preset,
    bool                                 ActiveCard,
    const TexturingThemeColors&          Theme) noexcept
{
    const float StartX = TileBounds.X + 16.0f;
    const float EndX   = TileBounds.Right() - 16.0f;
    const float MidY   = TileBounds.Y + 18.0f;
    const float SpanX  = std::max(24.0f, EndX - StartX);
    const ColorRgba Ink = ActiveCard
        ? ColorRgba{ 245u, 245u, 242u, 245u }
        : ColorRgba{ 218u, 220u, 224u, 215u };

    const float BaseRadius = std::clamp(static_cast<float>(Preset.PixelRadius) * 0.24f, 1.8f, 6.8f);
    constexpr int Samples  = 34;
    for (int I = 0; I <= Samples; ++I)
    {
        const float T     = static_cast<float>(I) / static_cast<float>(Samples);
        const float Taper = std::sin(T * 3.14159f);
        const float X     = StartX + T * SpanX;
        const float Y     = MidY - std::sin(T * 6.28318f) * 7.2f;
        const float R     = std::max(1.2f, BaseRadius * (0.28f + 0.72f * Taper));

        if (Preset.TipShape == BrushTipShape::Splatter)
        {
            Surface.FillCircle({ X, Y }, R * 0.65f, Ink);
            if ((I % 3) == 0)
            {
                Surface.FillCircle({ X + 3.0f, Y - 4.5f }, 1.5f, Ink);
                Surface.FillCircle({ X - 2.5f, Y + 4.0f }, 1.2f, Ink);
            }
        }
        else if (Preset.TipShape == BrushTipShape::Chisel)
        {
            Surface.StrokeLine({ X - 2.0f, Y - R * 0.85f }, { X + 2.0f, Y + R * 0.85f }, Ink, std::max(2.0f, R * 0.9f));
        }
        else
        {
            Surface.FillCircle({ X, Y }, R, Ink);
        }
    }
    (void)Theme;
}

} // namespace

TexturePaintViewportPane::~TexturePaintViewportPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
    SpatialTexture.DestroyPixels();
    UvCanvasTexture.DestroyPixels();
}

void TexturePaintViewportPane::AttachSession(TexturePaintSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void TexturePaintViewportPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##TexturePaintViewportCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);
}

void TexturePaintViewportPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  /*DeltaSeconds*/)
{
    if (Session == nullptr || Bounds.Width < 80.0f || Bounds.Height < 80.0f)
    {
        return;
    }

    const TexturingThemeColors Theme = QueryTexturingTheme(Surface);
    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground);

    const Point2D Cursor       { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed      = Input.PrimaryPointer.Pressed;
    const bool    RightPressed = Input.SecondaryPressed;
    const bool    PopupOpen    = BrushStudioModalOpen || ResolutionSheetOpen || ViewModeSheetOpen || MoreToolsSheetOpen || SaveSheetOpen;

    //======================================================================================================================
    // 1. TOP HEADER BAND 1 (Document Name "Teapot Paint" | RESOLUTION [512 PX v]) — matches cards-paint.png 1:1
    //======================================================================================================================
    const Rectangle2D Band1R{ Bounds.X, Bounds.Y, Bounds.Width, 36.0f };
    Surface.FillRectangle(Band1R, Theme.LightSurface ? Theme.SectionSurface : ColorRgba{ 24u, 25u, 28u, 255u });

    float LeftX = Band1R.X + 12.0f;
    const float Band1MidY = Band1R.Y + 5.0f;

    const Rectangle2D DocNameR{ LeftX, Band1MidY, 124.0f, 26.0f };
    if (!DocNameBuffer.Active && std::string_view(DocNameBuffer.Text, DocNameBuffer.Length) != Session->DocumentName)
    {
        DocNameBuffer.Begin(Session->DocumentName.c_str());
        DocNameBuffer.Active = false;
    }
    Surface.FillRectangle(DocNameR, ColorRgba{ 19u, 20u, 23u, 255u }, 13.0f);
    const ControlContact DocContact = TextInputField::Construct(
        Surface,
        { DocNameR.X + 8.0f, DocNameR.Y + 1.0f, DocNameR.Width - 16.0f, DocNameR.Height - 2.0f },
        DocNameBuffer,
        Input.PrimaryPointer,
        9.6f);
    if ((DocContact.Clicked || DocNameBuffer.Finalized) && DocNameBuffer.Length > 0u)
    {
        Session->DocumentName.assign(DocNameBuffer.Text, DocNameBuffer.Length);
    }
    LeftX = DocNameR.Right() + 12.0f;

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { LeftX, Band1MidY + 7.5f }, 7.6f, Theme.TextFaint, "RESOLUTION");
    LeftX += 60.0f;

    char ResBtnText[24];
    std::snprintf(ResBtnText, sizeof(ResBtnText), "%u PX", Session->ResolutionPx);
    const Rectangle2D ResBtnR{ LeftX, Band1MidY, 74.0f, 26.0f };
    if (ConstructHeaderActionCapsule(Surface, ResBtnR, ControlIcon::Count, ResBtnText, true, ResolutionSheetOpen, Theme, Cursor, Pressed))
    {
        ResolutionSheetOpen = !ResolutionSheetOpen;
        ViewModeSheetOpen   = false;
        MoreToolsSheetOpen  = false;
        SaveSheetOpen       = false;
    }

    //======================================================================================================================
    // 2. TOP HEADER BAND 2 (9 Circular Tool Buttons + More tools v | VIEW [3D Mesh v] | Undo/Redo | Save v | Export PNG | Status)
    //======================================================================================================================
    const Rectangle2D Band2R{ Bounds.X, Band1R.Bottom(), Bounds.Width, 38.0f };
    Surface.FillRectangle(Band2R, Theme.LightSurface ? Theme.SectionSurface : ColorRgba{ 24u, 25u, 28u, 255u });
    Surface.StrokeLine({ Band2R.X, Band2R.Bottom() - 0.5f }, { Band2R.Right(), Band2R.Bottom() - 0.5f }, Theme.BorderSubtle, 1.0f);

    float B2X = Band2R.X + 12.0f;
    const float B2Y = Band2R.Y + 5.0f;

    constexpr float ToolBtnSz   = 26.0f;
    constexpr float ToolBtnStep = 29.0f;
    for (std::size_t I = 0u; I < ViewportToolCatalog.size(); ++I)
    {
        const auto& Spec = ViewportToolCatalog[I];
        const Rectangle2D BtnR{ B2X + static_cast<float>(I) * ToolBtnStep, B2Y, ToolBtnSz, ToolBtnSz };
        const bool IsActiveTool = (Session->ActiveTool == Spec.Tool);
        const bool Hover        = BtnR.Contains(Cursor);

        const ColorRgba BtnFill = IsActiveTool
            ? (Theme.LightSurface ? Theme.AccentFill : ColorRgba{ 255u, 255u, 255u, 255u })
            : (Hover ? ColorRgba{ 38u, 40u, 46u, 255u } : ColorRgba{ 19u, 20u, 23u, 255u });
        const ColorRgba BtnInk  = IsActiveTool
            ? (Theme.LightSurface ? Theme.HighlightInk : ColorRgba{ 17u, 17u, 17u, 255u })
            : (Hover ? Theme.TextPrimary : Theme.TextMuted);

        Surface.FillRectangle(BtnR, BtnFill, 13.0f);
        VectorIconRepository::ConstructControl(
            Surface,
            Spec.Icon,
            VectorIconPlacement{ BtnR.X + 6.0f, BtnR.Y + 6.0f, 14.0f, BtnInk, 1.7f });

        if (Hover && Pressed)
        {
            Session->SelectTool(Spec.Tool);
        }
        else if (Hover && RightPressed)
        {
            Session->SelectTool(Spec.Tool);
            BrushStudioModalOpen = true;
            ResolutionSheetOpen  = false;
            ViewModeSheetOpen    = false;
            MoreToolsSheetOpen   = false;
        }
    }
    B2X += static_cast<float>(ViewportToolCatalog.size()) * ToolBtnStep + 4.0f;

    const Rectangle2D MoreToolsBtnR{ B2X, B2Y, 82.0f, 26.0f };
    if (ConstructHeaderActionCapsule(Surface, MoreToolsBtnR, ControlIcon::Count, "More tools", true, MoreToolsSheetOpen, Theme, Cursor, Pressed))
    {
        MoreToolsSheetOpen  = !MoreToolsSheetOpen;
        ResolutionSheetOpen = false;
        ViewModeSheetOpen   = false;
        SaveSheetOpen       = false;
    }
    B2X = MoreToolsBtnR.Right() + 10.0f;

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { B2X, B2Y + 7.5f }, 7.6f, Theme.TextFaint, "VIEW");
    B2X += 28.0f;

    const char* ViewBtnLabel = "3D Mesh";
    if (Session->ViewportLayout == PaintViewportLayout::SplitView)     ViewBtnLabel = "3D + 2D Split";
    else if (Session->ViewportLayout == PaintViewportLayout::TwoDOnly) ViewBtnLabel = "2D UV Only";
    else if (Session->ViewChannelIndex > 0u && Session->ViewChannelIndex < TexturePaintSession::QueryViewChannelLabels().size())
    {
        ViewBtnLabel = TexturePaintSession::QueryViewChannelLabels()[Session->ViewChannelIndex];
    }

    const Rectangle2D ViewBtnR{ B2X, B2Y, 88.0f, 26.0f };
    if (ConstructHeaderActionCapsule(Surface, ViewBtnR, ControlIcon::Count, ViewBtnLabel, true, ViewModeSheetOpen, Theme, Cursor, Pressed))
    {
        ViewModeSheetOpen   = !ViewModeSheetOpen;
        ResolutionSheetOpen = false;
        MoreToolsSheetOpen  = false;
        SaveSheetOpen       = false;
    }
    B2X = ViewBtnR.Right() + 6.0f;

    const Rectangle2D UndoBtnR{ B2X, B2Y, 26.0f, 26.0f };
    if (ConstructHeaderIconButton(Surface, UndoBtnR, ControlIcon::UndoArrow, false, Session->QueryUndoAvailable(), Theme, Cursor, Pressed))
    {
        Session->ApplyUndo();
    }
    const Rectangle2D RedoBtnR{ UndoBtnR.Right() + 3.0f, B2Y, 26.0f, 26.0f };
    if (ConstructHeaderIconButton(Surface, RedoBtnR, ControlIcon::RedoArrow, false, Session->QueryRedoAvailable(), Theme, Cursor, Pressed))
    {
        Session->ApplyRedo();
    }

    const float StatusW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.6f, Session->StatusBanner) + 26.0f;
    const Rectangle2D StatusPillR{ Band2R.Right() - 12.0f - StatusW, B2Y + 1.0f, StatusW, 24.0f };
    Surface.FillRectangle(StatusPillR, ColorRgba{ 19u, 20u, 23u, 255u }, 12.0f);
    Surface.FillCircle({ StatusPillR.X + 10.0f, StatusPillR.Y + 12.0f }, 3.2f, ColorRgba{ 52u, 199u, 89u, 255u });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { StatusPillR.X + 18.0f, StatusPillR.Y + 5.5f }, 8.6f, Theme.TextMuted, Session->StatusBanner);

    const Rectangle2D ExportBtnR{ StatusPillR.X - 6.0f - 84.0f, B2Y, 84.0f, 26.0f };
    (void)ConstructHeaderActionCapsule(Surface, ExportBtnR, ControlIcon::DownloadExport, "Export PNG", false, false, Theme, Cursor, Pressed);

    const Rectangle2D SaveBtnR{ ExportBtnR.X - 6.0f - 58.0f, B2Y, 58.0f, 26.0f };
    if (ConstructHeaderActionCapsule(Surface, SaveBtnR, ControlIcon::Count, "Save", true, SaveSheetOpen, Theme, Cursor, Pressed))
    {
        SaveSheetOpen         = !SaveSheetOpen;
        Session->StatusBanner = "Saved project";
    }

    //======================================================================================================================
    // 3. VIEWPORT STAGE (3D Mesh Only | 3D + 2D UV Split | 2D UV Only)
    //======================================================================================================================
    const Rectangle2D StageOuterR{
        Bounds.X + 10.0f,
        Band2R.Bottom() + 10.0f,
        std::max(64.0f, Bounds.Width - 20.0f),
        std::max(64.0f, Bounds.Bottom() - Band2R.Bottom() - 20.0f)
    };

    LastSpatialCanvasRect = {};
    LastUvCanvasRect      = {};

    if (Session->ViewportLayout == PaintViewportLayout::ThreeDOnly)
    {
        LastSpatialCanvasRect = StageOuterR;
    }
    else if (Session->ViewportLayout == PaintViewportLayout::TwoDOnly)
    {
        LastUvCanvasRect = StageOuterR;
    }
    else
    {
        const float HalfW     = (StageOuterR.Width - 10.0f) * 0.5f;
        LastSpatialCanvasRect = { StageOuterR.X, StageOuterR.Y, HalfW, StageOuterR.Height };
        LastUvCanvasRect      = { StageOuterR.X + HalfW + 10.0f, StageOuterR.Y, HalfW, StageOuterR.Height };
    }

    if (LastSpatialCanvasRect.Width > 24.0f && LastSpatialCanvasRect.Height > 24.0f)
    {
        const uint32_t TargetW = std::clamp(static_cast<uint32_t>(LastSpatialCanvasRect.Width * 0.72f), 192u, 520u);
        const uint32_t TargetH = std::clamp(static_cast<uint32_t>(LastSpatialCanvasRect.Height * 0.72f), 192u, 460u);
        const bool NeedsReRender =
            (SpatialTexture.Pixels == nullptr) ||
            (SpatialTexWidth != TargetW) ||
            (SpatialTexHeight != TargetH) ||
            (SpatialTexRevision != Session->ContentRevision) ||
            (std::abs(CachedYaw - Session->CameraYawRadians) > 1.0e-4f) ||
            (std::abs(CachedPitch - Session->CameraPitchRadians) > 1.0e-4f) ||
            (std::abs(CachedDistance - Session->CameraDistanceMeters) > 1.0e-4f) ||
            (CachedViewChannel != Session->ViewChannelIndex) ||
            (CachedWireframe != Session->WireframeOverlay);

        if (NeedsReRender)
        {
            if (Session->RenderSpatialViewportRgba8(TargetW, TargetH, SpatialRgbaBuffer))
            {
                UploadViewportTexturePixels(SpatialTexture, SpatialTexWidth, SpatialTexHeight, TargetW, TargetH, SpatialRgbaBuffer);
                SpatialTexRevision = Session->ContentRevision;
                CachedYaw          = Session->CameraYawRadians;
                CachedPitch        = Session->CameraPitchRadians;
                CachedDistance     = Session->CameraDistanceMeters;
                CachedViewChannel  = Session->ViewChannelIndex;
                CachedWireframe    = Session->WireframeOverlay;
            }
        }
        if (SpatialTexture.Pixels != nullptr)
        {
            Surface.FillTexturedRectangle(LastSpatialCanvasRect, &SpatialTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
        }
        Surface.StrokeRectangle(LastSpatialCanvasRect, Theme.BorderSubtle, 10.0f, 1.0f);

        // Top-left "3D SURFACE VIEWPORT" pill (cards-paint.png parity)
        const Rectangle2D ViewportTagPillR{ LastSpatialCanvasRect.X + 12.0f, LastSpatialCanvasRect.Y + 12.0f, 128.0f, 22.0f };
        Surface.FillRectangle(ViewportTagPillR, ColorRgba{ 16u, 17u, 20u, 210u }, 11.0f);
        Surface.StrokeRectangle(ViewportTagPillR, Theme.BorderSubtle, 11.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { ViewportTagPillR.X + 10.0f, ViewportTagPillR.Y + 4.5f }, 7.8f, Theme.TextMuted, "3D SURFACE VIEWPORT");

        // Bottom HUD hint + active tool size readout (cards-paint.png parity)
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { LastSpatialCanvasRect.X + 14.0f, LastSpatialCanvasRect.Bottom() - 20.0f },
            LastSpatialCanvasRect.Width - 150.0f,
            8.2f,
            Theme.TextFaint,
            "Drag paint · Hold Shift straight line · U 3D Curve · D 3D Gradient · T Text · Righ-click Brush Studio");

        const char* ActiveToolTag = "Brush";
        if (Session->ActiveTool == PaintToolCategory::Eraser)          ActiveToolTag = "Eraser";
        else if (Session->ActiveTool == PaintToolCategory::Fill)       ActiveToolTag = "Fill";
        else if (Session->ActiveTool == PaintToolCategory::Gradient3D) ActiveToolTag = "3D Gradient";
        else if (Session->ActiveTool == PaintToolCategory::Curve3D)    ActiveToolTag = "3D Curve";
        else if (Session->ActiveTool == PaintToolCategory::Stamp)      ActiveToolTag = "Stamp";
        else if (Session->ActiveTool == PaintToolCategory::Text)       ActiveToolTag = "Text";
        else if (Session->ActiveTool == PaintToolCategory::Move)       ActiveToolTag = "Transform";
        char ToolReadoutBuf[48];
        std::snprintf(ToolReadoutBuf, sizeof(ToolReadoutBuf), "%s · %dpx", ActiveToolTag, static_cast<int>(Session->Brush.PixelRadius));
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { LastSpatialCanvasRect.Right() - 96.0f, LastSpatialCanvasRect.Bottom() - 20.0f },
            8.4f,
            Theme.TextMuted,
            ToolReadoutBuf);

        // 3D Gradient Point Gizmos ("1", "2", ... + prominent ring on active point matching paint-gradient-points.png)
        const PaintLayerEntry* SelLayer = Session->InspectSelectedLayer();
        if (SelLayer != nullptr && (SelLayer->Category == PaintLayerCategory::Gradient || Session->ActiveTool == PaintToolCategory::Gradient3D))
        {
            for (std::size_t PtIdx = 0u; PtIdx < SelLayer->GradientPoints.size(); ++PtIdx)
            {
                const auto& Pt = SelLayer->GradientPoints[PtIdx];
                Point2D LocalPt {};
                if (Session->ProjectWorldPointToViewport(
                        Pt.PositionX,
                        Pt.PositionY,
                        Pt.PositionZ,
                        LastSpatialCanvasRect.Width,
                        LastSpatialCanvasRect.Height,
                        LocalPt))
                {
                    const Point2D ScreenPt{ LastSpatialCanvasRect.X + LocalPt.X, LastSpatialCanvasRect.Y + LocalPt.Y };
                    if (LastSpatialCanvasRect.Contains(ScreenPt))
                    {
                        const bool PtSelected = (SelLayer->SelectedGradientPointIndex == static_cast<int32_t>(PtIdx));
                        if (PtSelected)
                        {
                            Surface.FillCircle(ScreenPt, 24.0f, ColorRgba{ 255u, 255u, 255u, 220u });
                            Surface.FillCircle(ScreenPt, 19.0f, Pt.TintColor);
                            Surface.StrokeCircle(ScreenPt, 24.0f, ColorRgba{ 18u, 19u, 22u, 180u }, 1.5f);
                        }
                        else
                        {
                            Surface.FillCircle(ScreenPt, 7.5f, Pt.TintColor);
                            Surface.StrokeCircle(ScreenPt, 7.5f, ColorRgba{ 255u, 255u, 255u, 210u }, 1.5f);
                        }

                        const float HitDx = Cursor.X - ScreenPt.X;
                        const float HitDy = Cursor.Y - ScreenPt.Y;
                        if (!PopupOpen && Pressed && (HitDx * HitDx + HitDy * HitDy <= 576.0f))
                        {
                            if (auto* MutLayer = Session->AccessSelectedLayer())
                            {
                                MutLayer->SelectedGradientPointIndex = static_cast<int32_t>(PtIdx);
                            }
                        }
                    }
                }
            }
        }

        if (!PopupOpen && LastSpatialCanvasRect.Contains(Cursor))
        {
            if (std::abs(Input.WheelDeltaY) > 1.0e-4f)
            {
                Session->ZoomCameraByFactor(Input.WheelDeltaY > 0.0f ? 0.91f : 1.10f);
            }
            if (Input.SecondaryDown)
            {
                Session->OrbitCameraByDelta(-Input.PointerDeltaX * 0.011f, Input.PointerDeltaY * 0.011f);
            }
            else if (Input.PrimaryPointer.Pressed || Input.PrimaryPointer.Down)
            {
                const float NormX = (Cursor.X - LastSpatialCanvasRect.X) / std::max(1.0f, LastSpatialCanvasRect.Width);
                const float NormY = (Cursor.Y - LastSpatialCanvasRect.Y) / std::max(1.0f, LastSpatialCanvasRect.Height);
                if (!Session->ApplyViewportPointerStroke(NormX, NormY, Input.PrimaryPointer.Pressed, false) &&
                    Input.PrimaryPointer.Down &&
                    (std::abs(Input.PointerDeltaX) > 0.1f || std::abs(Input.PointerDeltaY) > 0.1f))
                {
                    Session->OrbitCameraByDelta(-Input.PointerDeltaX * 0.010f, Input.PointerDeltaY * 0.010f);
                }
            }
        }
    }

    if (LastUvCanvasRect.Width > 24.0f && LastUvCanvasRect.Height > 24.0f)
    {
        constexpr uint32_t UvDim = 256u;
        if (UvCanvasTexture.Pixels == nullptr || UvTexRevision != Session->ContentRevision)
        {
            if (Session->RenderUvCanvasRgba8(UvDim, UvDim, UvRgbaBuffer))
            {
                UploadViewportTexturePixels(UvCanvasTexture, UvTexWidth, UvTexHeight, UvDim, UvDim, UvRgbaBuffer);
                UvTexRevision = Session->ContentRevision;
            }
        }
        if (UvCanvasTexture.Pixels != nullptr)
        {
            Surface.FillTexturedRectangle(LastUvCanvasRect, &UvCanvasTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
        }
        Surface.StrokeRectangle(LastUvCanvasRect, Theme.BorderSubtle, 10.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::SemiBold,
            { LastUvCanvasRect.X + 10.0f, LastUvCanvasRect.Y + 8.0f },
            8.5f,
            Theme.TextMuted,
            "2D UV ATLAS");

        if (!PopupOpen && LastUvCanvasRect.Contains(Cursor) && (Input.PrimaryPointer.Pressed || Input.PrimaryPointer.Down))
        {
            const float NormX = (Cursor.X - LastUvCanvasRect.X) / std::max(1.0f, LastUvCanvasRect.Width);
            const float NormY = (Cursor.Y - LastUvCanvasRect.Y) / std::max(1.0f, LastUvCanvasRect.Height);
            (void)Session->ApplyViewportPointerStroke(NormX, NormY, Input.PrimaryPointer.Pressed, true);
        }
    }

    //======================================================================================================================
    // 4. HEADER DROPDOWN SHEETS (Resolution, View Mode, More Tools)
    //======================================================================================================================
    if (ResolutionSheetOpen)
    {
        constexpr std::array<uint32_t, 4u> ResOptions {{ 512u, 1024u, 2048u, 4096u }};
        const Rectangle2D SheetR{ ResBtnR.X, ResBtnR.Bottom() + 4.0f, 104.0f, 112.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);
        for (std::size_t I = 0u; I < ResOptions.size(); ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(I) * 25.0f, SheetR.Width - 10.0f, 23.0f };
            const bool Active = (Session->ResolutionPx == ResOptions[I]);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            char Label[24];
            std::snprintf(Label, sizeof(Label), "%u px", ResOptions[I]);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 4.0f }, 9.5f, Theme.TextPrimary, Label);
            if (Pressed && Hover)
            {
                Session->AssignResolution(ResOptions[I]);
                ResolutionSheetOpen = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !ResBtnR.Contains(Cursor))
        {
            ResolutionSheetOpen = false;
        }
    }

    if (ViewModeSheetOpen)
    {
        struct ViewModeOptionSpec
        {
            const char*         Label;
            PaintViewportLayout Layout;
            uint32_t            ChannelIdx;
        };
        constexpr std::array<ViewModeOptionSpec, 8u> ViewItems {{
            { "3D Mesh (Lit PBR)", PaintViewportLayout::ThreeDOnly, 0u },
            { "3D + 2D Split",     PaintViewportLayout::SplitView,  0u },
            { "2D UV Only",        PaintViewportLayout::TwoDOnly,   0u },
            { "Channel: Albedo",   PaintViewportLayout::ThreeDOnly, 1u },
            { "Channel: Roughness",PaintViewportLayout::ThreeDOnly, 2u },
            { "Channel: Metalness",PaintViewportLayout::ThreeDOnly, 3u },
            { "Channel: Height",   PaintViewportLayout::ThreeDOnly, 4u },
            { "Channel: Normal",   PaintViewportLayout::ThreeDOnly, 5u },
        }};
        const Rectangle2D SheetR{ ViewBtnR.X, ViewBtnR.Bottom() + 4.0f, 154.0f, static_cast<float>(ViewItems.size()) * 25.0f + 10.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);
        for (std::size_t I = 0u; I < ViewItems.size(); ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(I) * 25.0f, SheetR.Width - 10.0f, 23.0f };
            const bool Active = (Session->ViewportLayout == ViewItems[I].Layout && Session->ViewChannelIndex == ViewItems[I].ChannelIdx);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            WorkspaceAppearance::ConstructWeightedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 4.0f }, 9.4f, Theme.TextPrimary, ViewItems[I].Label);
            if (Pressed && Hover)
            {
                Session->ViewportLayout   = ViewItems[I].Layout;
                Session->ViewChannelIndex = ViewItems[I].ChannelIdx;
                ViewModeSheetOpen         = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !ViewBtnR.Contains(Cursor))
        {
            ViewModeSheetOpen = false;
        }
    }

    if (MoreToolsSheetOpen)
    {
        const auto& Studies = TexturePaintSession::QueryStudyRegistry();
        const Rectangle2D SheetR{ MoreToolsBtnR.Right() - 172.0f, MoreToolsBtnR.Bottom() + 4.0f, 172.0f, 210.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);

        float Sy = SheetR.Y + 6.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { SheetR.X + 10.0f, Sy }, 8.0f, Theme.TextFaint, "STUDY MESH");
        Sy += 16.0f;
        for (std::size_t I = 0u; I < Studies.size(); ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, Sy, SheetR.Width - 10.0f, 23.0f };
            const bool Active = (Session->Study == Studies[I].Asset);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            WorkspaceAppearance::ConstructWeightedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 4.0f }, 9.4f, Theme.TextPrimary, Studies[I].Label);
            if (Pressed && Hover)
            {
                Session->SelectStudy(Studies[I].Asset);
                MoreToolsSheetOpen = false;
                break;
            }
            Sy += 24.0f;
        }
        Sy += 4.0f;
        const Rectangle2D WireRowR{ SheetR.X + 5.0f, Sy, SheetR.Width - 10.0f, 24.0f };
        if (WireRowR.Contains(Cursor))
        {
            Surface.FillRectangle(WireRowR, Theme.HoverSurface, 6.0f);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { WireRowR.X + 8.0f, WireRowR.Y + 4.5f }, 9.4f, Theme.TextPrimary,
            Session->WireframeOverlay ? "Hide Wireframe" : "Show Wireframe");
        if (Pressed && WireRowR.Contains(Cursor))
        {
            Session->WireframeOverlay = !Session->WireframeOverlay;
            MoreToolsSheetOpen        = false;
        }
        Sy += 26.0f;
        const Rectangle2D StudioRowR{ SheetR.X + 5.0f, Sy, SheetR.Width - 10.0f, 24.0f };
        if (StudioRowR.Contains(Cursor))
        {
            Surface.FillRectangle(StudioRowR, Theme.HoverSurface, 6.0f);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { StudioRowR.X + 8.0f, StudioRowR.Y + 4.5f }, 9.4f, Theme.TextPrimary, "Open Brush Studio...");
        if (Pressed && StudioRowR.Contains(Cursor))
        {
            BrushStudioModalOpen = true;
            MoreToolsSheetOpen   = false;
        }

        if (Pressed && !SheetR.Contains(Cursor) && !MoreToolsBtnR.Contains(Cursor))
        {
            MoreToolsSheetOpen = false;
        }
    }

    //======================================================================================================================
    // 5. FLOATING BRUSH STUDIO MENU (.cd-brush-menu: Paint, Stamp, Erase, Fill, Gradient, Text, Curve, Move)
    //======================================================================================================================
    if (BrushStudioModalOpen)
    {
        const float ModalW = std::min(720.0f, Bounds.Width - 24.0f);
        const float ModalH = std::min(580.0f, Bounds.Height - 24.0f);
        const Rectangle2D ModalR{
            Bounds.X + (Bounds.Width - ModalW) * 0.5f,
            Bounds.Y + 84.0f,
            ModalW,
            ModalH
        };

        // Soft drop shadow (no full-screen canvas dim backdrop, matching cards-brush-library.png & cards-brush-stamp.png)
        Surface.FillRectangle({ ModalR.X - 4.0f, ModalR.Y + 6.0f, ModalR.Width + 8.0f, ModalR.Height + 8.0f }, ColorRgba{ 0u, 0u, 0u, 110u }, 18.0f);
        Surface.FillRectangle(ModalR, ColorRgba{ 24u, 25u, 28u, 255u }, 16.0f);
        Surface.StrokeRectangle(ModalR, ColorRgba{ 255u, 255u, 255u, 24u }, 16.0f, 1.0f);

        const bool IsTextStudio     = (Session->InspectorTab == BrushInspectorTab::Text     || Session->ActiveTool == PaintToolCategory::Text);
        const bool IsStampStudio    = (Session->InspectorTab == BrushInspectorTab::Stamp    || Session->ActiveTool == PaintToolCategory::Stamp);
        const bool IsEraseStudio    = (Session->InspectorTab == BrushInspectorTab::Erase    || Session->ActiveTool == PaintToolCategory::Eraser);
        const bool IsFillStudio     = (Session->InspectorTab == BrushInspectorTab::Fill     || Session->ActiveTool == PaintToolCategory::Fill);
        const bool IsGradientStudio = (Session->InspectorTab == BrushInspectorTab::Gradient || Session->ActiveTool == PaintToolCategory::Gradient3D);
        const bool IsCurveStudio    = (Session->InspectorTab == BrushInspectorTab::Curve    || Session->ActiveTool == PaintToolCategory::Curve3D);
        const bool IsMoveStudio     = (Session->InspectorTab == BrushInspectorTab::Move     || Session->ActiveTool == PaintToolCategory::Move);

        const char* ContextBadgeTag = "PAINT";
        ControlIcon StudioIcon      = ControlIcon::BrushTool;
        ColorRgba   StudioIconTint  = ColorRgba{ 240u, 102u, 144u, 255u };
        std::string StudioTitle     = "Brush Studio";
        std::string StudioSub       = "Hog Bristle Flat #12 (Oil) · 3 edited";

        const auto& Presets      = TexturePaintSession::QueryBrushLibraryRegistry();
        const auto& ActivePreset = Presets[std::min<std::size_t>(Session->Brush.ActivePresetIndex, Presets.size() - 1u)];

        if (IsStampStudio)
        {
            ContextBadgeTag = "STAMP";
            StudioIcon      = ControlIcon::StampTool;
            StudioIconTint  = ColorRgba{ 88u, 214u, 141u, 255u };
            StudioTitle     = "Stamp Studio";
            StudioSub       = "Decals · click, drag or grid";
        }
        else if (IsEraseStudio)
        {
            ContextBadgeTag = "ERASE";
            StudioIcon      = ControlIcon::EraserTool;
            StudioIconTint  = ColorRgba{ 240u, 120u, 100u, 255u };
            StudioTitle     = "Eraser Studio";
            StudioSub       = "Remove paint per channel";
        }
        else if (IsFillStudio)
        {
            ContextBadgeTag = "FILL";
            StudioIcon      = ControlIcon::FillBucket;
            StudioIconTint  = ColorRgba{ 96u, 165u, 250u, 255u };
            StudioTitle     = "Fill Studio";
            StudioSub       = "Flood regions and similar colour";
        }
        else if (IsGradientStudio)
        {
            ContextBadgeTag = "GRADIENT";
            StudioIcon      = ControlIcon::PointGradient;
            StudioIconTint  = ColorRgba{ 74u, 222u, 180u, 255u };
            StudioTitle     = "Gradient Studio";
            StudioSub       = "Linear, radial and 3D point ramps";
        }
        else if (IsTextStudio)
        {
            ContextBadgeTag = "TEXT";
            StudioIcon      = ControlIcon::TextType;
            StudioIconTint  = ColorRgba{ 192u, 132u, 252u, 255u };
            StudioTitle     = "Text Studio";
            StudioSub       = "Lettering painted into the texture";
        }
        else if (IsCurveStudio)
        {
            ContextBadgeTag = "CURVE";
            StudioIcon      = ControlIcon::VectorCurve;
            StudioIconTint  = ColorRgba{ 251u, 191u, 36u, 255u };
            StudioTitle     = "Curve Studio";
            StudioSub       = "3D spline stroke along surface";
        }
        else if (IsMoveStudio)
        {
            ContextBadgeTag = "MOVE";
            StudioIcon      = ControlIcon::MoveArrows;
            StudioIconTint  = ColorRgba{ 163u, 230u, 53u, 255u };
            StudioTitle     = "Transform Studio";
            StudioSub       = "Translate, scale and rotate layer UV";
        }
        else
        {
            ContextBadgeTag = "PAINT";
            StudioIcon      = ControlIcon::BrushTool;
            StudioIconTint  = ColorRgba{ 240u, 102u, 144u, 255u };
            StudioTitle     = "Brush Studio";
            StudioSub       = std::string(ActivePreset.Label) + " (Oil) · 3 edited";
        }

        // Outer Context Menu Bar ("Brush Studio  <TOOL>" + Close X, matching cards-brush-library.png & cards-brush-stamp.png)
        const Rectangle2D OuterTopBarR{ ModalR.X, ModalR.Y, ModalR.Width, 38.0f };
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { OuterTopBarR.X + 18.0f, OuterTopBarR.Y + 11.0f }, 11.0f, Theme.TextPrimary, "Brush Studio");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Bold, { OuterTopBarR.X + 96.0f, OuterTopBarR.Y + 12.5f }, 8.6f, ColorRgba{ 232u, 194u, 122u, 255u }, ContextBadgeTag);

        const Rectangle2D OuterCloseBtnR{ OuterTopBarR.Right() - 38.0f, OuterTopBarR.Y + 6.0f, 26.0f, 26.0f };
        if (ConstructHeaderIconButton(Surface, OuterCloseBtnR, ControlIcon::CloseCross, false, true, Theme, Cursor, Pressed))
        {
            BrushStudioModalOpen = false;
        }

        // Inner Card Container inside ModalR
        const Rectangle2D InnerCardR{ ModalR.X + 10.0f, OuterTopBarR.Bottom() + 2.0f, ModalR.Width - 20.0f, ModalR.Height - 50.0f };
        Surface.FillRectangle(InnerCardR, Theme.PaneBackground, 12.0f);
        Surface.StrokeRectangle(InnerCardR, Theme.BorderSubtle, 12.0f, 1.0f);

        const Rectangle2D HeadR{ InnerCardR.X, InnerCardR.Y, InnerCardR.Width, 48.0f };
        Surface.StrokeLine({ HeadR.X, HeadR.Bottom() }, { HeadR.Right(), HeadR.Bottom() }, Theme.BorderSubtle, 1.0f);

        VectorIconRepository::ConstructControl(
            Surface, StudioIcon, VectorIconPlacement{ HeadR.X + 16.0f, HeadR.Y + 15.0f, 16.0f, StudioIconTint, 1.75f });
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Bold, { HeadR.X + 42.0f, HeadR.Y + 9.0f }, HeadR.Width - 250.0f, 12.2f, Theme.TextPrimary, StudioTitle);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { HeadR.X + 42.0f, HeadR.Y + 26.0f }, HeadR.Width - 250.0f, 8.6f, Theme.TextFaint, StudioSub);

        float HBtnX = HeadR.Right() - 12.0f - 26.0f;
        const float HBtnY = HeadR.Y + 11.0f;
        (void)ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 26.0f, 26.0f }, ControlIcon::DownloadExport, false, true, Theme, Cursor, Pressed);
        HBtnX -= 29.0f;
        if (ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 26.0f, 26.0f }, ControlIcon::RefreshCycle, false, true, Theme, Cursor, Pressed))
        {
            Session->SelectBrushPreset(6u);
        }
        HBtnX -= 29.0f;
        if (ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 26.0f, 26.0f }, ControlIcon::RedoArrow, false, Session->QueryRedoAvailable(), Theme, Cursor, Pressed))
        {
            Session->ApplyRedo();
        }
        HBtnX -= 29.0f;
        if (ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 26.0f, 26.0f }, ControlIcon::UndoArrow, false, Session->QueryUndoAvailable(), Theme, Cursor, Pressed))
        {
            Session->ApplyUndo();
        }

        const float NavW = 176.0f;
        const Rectangle2D NavColR{ InnerCardR.X, HeadR.Bottom() + 1.0f, NavW, InnerCardR.Height - HeadR.Height - 1.0f };
        const Rectangle2D RightPaneR{ NavColR.Right() + 1.0f, NavColR.Y, InnerCardR.Width - NavW - 1.0f, NavColR.Height };
        Surface.StrokeLine({ NavColR.Right(), NavColR.Y }, { NavColR.Right(), NavColR.Bottom() }, Theme.BorderSubtle, 1.0f);

        std::span<const BrushStudioSectionSpec> NavItems(BrushSectionsCatalog);
        if (IsTextStudio)          NavItems = std::span<const BrushStudioSectionSpec>(TextSectionsCatalog);
        else if (IsStampStudio)    NavItems = std::span<const BrushStudioSectionSpec>(StampSectionsCatalog);
        else if (IsEraseStudio)    NavItems = std::span<const BrushStudioSectionSpec>(EraseSectionsCatalog);
        else if (IsFillStudio)     NavItems = std::span<const BrushStudioSectionSpec>(FillSectionsCatalog);
        else if (IsGradientStudio) NavItems = std::span<const BrushStudioSectionSpec>(GradientSectionsCatalog);

        float NavY = NavColR.Y + 8.0f;
        for (std::size_t SIdx = 0u; SIdx < NavItems.size(); ++SIdx)
        {
            const auto& Sec = NavItems[SIdx];
            if (SIdx == 1u)
            {
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Bold, { NavColR.X + 14.0f, NavY + 2.0f }, 7.6f, Theme.TextFaint, ContextBadgeTag);
                NavY += 16.0f;
            }
            const Rectangle2D SecRowR{ NavColR.X + 8.0f, NavY, NavColR.Width - 16.0f, 29.0f };
            const bool ActiveSec = (Session->InspectorSubTab == Sec.SubTab);
            const bool HoverSec  = SecRowR.Contains(Cursor);

            if (ActiveSec || HoverSec)
            {
                Surface.FillRectangle(SecRowR, ActiveSec ? StudioRowSelected : StudioRowHover, 7.0f);
            }
            if (ActiveSec)
            {
                Surface.FillRectangle({ SecRowR.X, SecRowR.Y + 6.0f, 3.0f, SecRowR.Height - 12.0f }, ColorRgba{ 240u, 164u, 58u, 255u }, 1.5f);
            }
            VectorIconRepository::ConstructControl(
                Surface,
                Sec.Icon,
                VectorIconPlacement{ SecRowR.X + 9.0f, SecRowR.Y + 7.5f, 14.0f, ActiveSec ? StudioIconTint : StudioIconTint, 1.7f });
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                ActiveSec ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                { SecRowR.X + 29.0f, SecRowR.Y + 6.5f },
                SecRowR.Width - 52.0f,
                9.5f,
                ActiveSec ? Theme.TextPrimary : Theme.TextMuted,
                Sec.Label);

            if (Sec.CountBadge != nullptr && Sec.CountBadge[0] != '\0')
            {
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { SecRowR.Right() - 22.0f, SecRowR.Y + 7.5f }, 8.4f, Theme.TextFaint, Sec.CountBadge);
            }
            if (Pressed && HoverSec)
            {
                Session->InspectorSubTab = Sec.SubTab;
                ModalScrollY             = 0.0f;
            }
            NavY += 31.0f;
        }

        Surface.PushClip(RightPaneR);
        float ContentY = RightPaneR.Y + 12.0f;
        const float CPadX = RightPaneR.X + 16.0f;
        const float CInnerW = RightPaneR.Width - 32.0f;

        if (Session->InspectorSubTab == BrushDetailSubTab::Channels)
        {
            // Brush Studio Channels view (channels-brush.png 1:1 parity: "PAINT CHANNELS  4 ON · CLICK TO EDIT" + Roughness open in-place)
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::LayersSlabs, VectorIconPlacement{ CPadX, ContentY + 3.0f, 15.0f, StudioIconTint, 1.7f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::SemiBold, { CPadX + 24.0f, ContentY - 2.0f }, 12.5f, Theme.TextPrimary, "Channels");
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX + 24.0f, ContentY + 15.0f }, 8.4f, Theme.TextFaint, "Which channels the brush writes into");
            ContentY += 36.0f;

            const Rectangle2D GroupBoxR{ CPadX, ContentY, CInnerW, RightPaneR.Bottom() - ContentY - 10.0f };
            Surface.FillRectangle(GroupBoxR, ColorRgba{ 28u, 29u, 33u, 255u }, 12.0f);

            float Gy = GroupBoxR.Y + 10.0f;
            const float Gx = GroupBoxR.X + 12.0f;
            const float Gw = GroupBoxR.Width - 24.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { Gx, Gy }, 7.8f, Theme.TextFaint, "PAINT CHANNELS");
            const char* RightHint = "4 ON · CLICK TO EDIT";
            const float HintW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Bold, 7.6f, RightHint);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { Gx + Gw - HintW, Gy }, 7.6f, Theme.TextFaint, RightHint);
            Gy += 20.0f;

            struct BrushChannelRowSpec
            {
                const char*          Name;
                ColorRgba            Swatch;
                const char*          PillText;
                bool                 DefaultOn;
                bool                 OpenEditor;
                PaintSurfaceChannel  EngineChannel;
            };
            const std::array<BrushChannelRowSpec, 12u> BrushChRows {{
                { "Base Color", Session->Brush.InkColor,              "#D96B2B", Session->Brush.PaintChannels[0], false, PaintSurfaceChannel::Albedo         },
                { "Roughness",  ColorRgba{  92u,  94u,  98u, 255u },  "35%",     Session->Brush.PaintChannels[1], true,  PaintSurfaceChannel::Roughness      },
                { "Metallic",   ColorRgba{  18u,  19u,  22u, 255u },  "0%",      Session->Brush.PaintChannels[2], false, PaintSurfaceChannel::Metalness      },
                { "Height",     ColorRgba{ 128u, 130u, 136u, 255u },  "50%",     Session->Brush.PaintChannels[3], false, PaintSurfaceChannel::Height         },
                { "Normal",     ColorRgba{ 128u, 128u, 255u, 255u },  "Off",     Session->Brush.PaintChannels[4], false, PaintSurfaceChannel::NormalStrength },
                { "Specular",   ColorRgba{ 128u, 130u, 136u, 255u },  "Off",     false,                           false, PaintSurfaceChannel::Specular       },
                { "Opacity",    ColorRgba{ 245u, 245u, 245u, 255u },  "Off",     false,                           false, PaintSurfaceChannel::Opacity        },
                { "Anisotropy", ColorRgba{  18u,  19u,  22u, 255u },  "Off",     false,                           false, PaintSurfaceChannel::Anisotropy     },
                { "Subsurface", ColorRgba{ 230u, 126u,  98u, 255u },  "Off",     false,                           false, PaintSurfaceChannel::Subsurface     },
                { "Coat",       ColorRgba{ 218u, 236u, 255u, 255u },  "Off",     false,                           false, PaintSurfaceChannel::Clearcoat      },
                { "Sheen",      ColorRgba{ 242u, 212u, 230u, 255u },  "Off",     false,                           false, PaintSurfaceChannel::Sheen          },
                { "Emissive",   ColorRgba{ 255u, 122u,  58u, 255u },  "Off",     false,                           false, PaintSurfaceChannel::Emissive       },
            }};

            PaintLayerEntry* SelLayer = Session->AccessSelectedLayer();
            for (std::size_t RIdx = 0u; RIdx < BrushChRows.size(); ++RIdx)
            {
                const auto& Row = BrushChRows[RIdx];
                const std::size_t ChIdx = static_cast<std::size_t>(Row.EngineChannel);
                const bool IsOn = Session->Brush.PaintChannels[ChIdx];
                const bool IsOpen = Row.OpenEditor && IsOn;
                const float RowCardH = IsOpen ? 68.0f : 31.0f;
                if (Gy + RowCardH > GroupBoxR.Bottom() - 6.0f) break;

                const Rectangle2D CardR{ Gx, Gy, Gw, RowCardH };
                Surface.FillRectangle(CardR, IsOpen ? ColorRgba{ 37u, 38u, 43u, 255u } : ColorRgba{ 22u, 23u, 26u, 255u }, 8.0f);

                // Left 20x20 rounded color swatch
                Surface.FillRectangle({ CardR.X + 8.0f, CardR.Y + 5.5f, 20.0f, 20.0f }, Row.Swatch, 5.0f);
                Surface.StrokeRectangle({ CardR.X + 8.0f, CardR.Y + 5.5f, 20.0f, 20.0f }, ColorRgba{ 255u, 255u, 255u, 28u }, 5.0f, 1.0f);

                WorkspaceAppearance::ConstructWeightedText(
                    Surface, IsOpen ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                    { CardR.X + 36.0f, CardR.Y + 8.0f }, 9.2f, IsOn ? Theme.TextPrimary : Theme.TextMuted, Row.Name);

                // Toggle switch on right
                const Rectangle2D SwR{ CardR.Right() - 36.0f, CardR.Y + 7.5f, 28.0f, 16.0f };
                Surface.FillRectangle(SwR, IsOn ? ColorRgba{ 255u, 255u, 255u, 255u } : ColorRgba{ 12u, 13u, 15u, 255u }, 8.0f);
                Surface.FillCircle(
                    { IsOn ? (SwR.Right() - 8.0f) : (SwR.X + 8.0f), SwR.Y + 8.0f },
                    6.0f,
                    IsOn ? ColorRgba{ 17u, 17u, 17u, 255u } : ColorRgba{ 118u, 120u, 126u, 255u });

                // Readout pill left of switch
                char PillBuf[24];
                if (!IsOn) std::snprintf(PillBuf, sizeof(PillBuf), "Off");
                else if (RIdx == 1u) std::snprintf(PillBuf, sizeof(PillBuf), "%d%%", static_cast<int>(std::lround(Session->Brush.Roughness * 100.0f)));
                else std::snprintf(PillBuf, sizeof(PillBuf), "%s", Row.PillText);
                const float Pw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.0f, PillBuf) + 14.0f;
                const Rectangle2D PillR{ SwR.X - 8.0f - Pw, CardR.Y + 7.0f, Pw, 17.0f };
                Surface.FillRectangle(PillR, ColorRgba{ 9u, 10u, 12u, 255u }, 8.5f);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { PillR.X + 7.0f, PillR.Y + 2.5f }, 8.0f, IsOn ? Theme.TextPrimary : Theme.TextFaint, PillBuf);

                if (Pressed && SwR.Contains(Cursor))
                {
                    Session->Brush.PaintChannels[ChIdx] = !Session->Brush.PaintChannels[ChIdx];
                    if (SelLayer != nullptr) SelLayer->ActiveChannels[ChIdx] = Session->Brush.PaintChannels[ChIdx];
                }

                if (IsOpen)
                {
                    (void)ConstructSliderReadoutRow(
                        Surface, { CardR.X + 10.0f, CardR.Y + 31.0f, CardR.Width - 20.0f, 34.0f },
                        "Roughness", Session->Brush.Roughness, 0.0f, 1.0f, "%.0f %%", 100.0f, Input, Theme);
                }
                Gy += RowCardH + 3.0f;
            }
        }
        else if (IsStampStudio && (Session->InspectorSubTab == BrushDetailSubTab::LibraryGrid || Session->InspectorSubTab == BrushDetailSubTab::Shape))
        {
            // Stamp Studio 16-shape 4x4 grid (cards-brush-stamp.png & studio-stamp-decals.png parity)
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::LayersSlabs, VectorIconPlacement{ CPadX, ContentY + 3.0f, 15.0f, Theme.TextMuted, 1.7f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX + 24.0f, ContentY - 2.0f }, 13.0f, Theme.TextPrimary, "Stamps");
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX + 24.0f, ContentY + 15.0f }, 8.6f, Theme.TextFaint, "16 presets · Stamp Studio");
            ContentY += 34.0f;

            const Rectangle2D SearchR{ CPadX, ContentY, CInnerW, 28.0f };
            Surface.FillRectangle(SearchR, Theme.WellSurface, 8.0f);
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchR.X + 9.0f, SearchR.Y + 7.5f, 13.0f, Theme.TextFaint, 1.65f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { SearchR.X + 28.0f, SearchR.Y + 6.5f }, 9.4f, Theme.TextFaint, "Search stamps");
            ContentY += 36.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::SemiBold, { CPadX, ContentY }, 9.4f, Theme.TextPrimary, "Shapes");
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { RightPaneR.Right() - 28.0f, ContentY }, 8.8f, Theme.TextFaint, "16");
            ContentY += 20.0f;

            const float CardGap = 8.0f;
            const float CardW   = (CInnerW - CardGap * 3.0f) / 4.0f;
            const float CardH   = 74.0f;

            for (std::size_t S = 0u; S < StampShapeCatalog.size(); ++S)
            {
                const std::size_t Col = S % 4u;
                const std::size_t Row = S / 4u;
                const Rectangle2D CardR{
                    CPadX + static_cast<float>(Col) * (CardW + CardGap),
                    ContentY + static_cast<float>(Row) * (CardH + CardGap),
                    CardW,
                    CardH
                };
                if (CardR.Bottom() > RightPaneR.Bottom() + 30.0f) continue;

                const bool ActiveStamp = (Session->Brush.ActiveStampName == StampShapeCatalog[S]);
                const bool HoverStamp  = CardR.Contains(Cursor);
                Surface.FillRectangle(CardR, ActiveStamp ? StudioRowSelected : Theme.SectionSurface, 9.0f);
                Surface.StrokeRectangle(
                    CardR,
                    ActiveStamp ? ColorRgba{ 240u, 240u, 238u, 240u } : (HoverStamp ? Theme.BorderStrong : Theme.BorderSubtle),
                    9.0f,
                    ActiveStamp ? 1.5f : 1.0f);

                const Rectangle2D WellR{ CardR.X + 5.0f, CardR.Y + 5.0f, CardR.Width - 10.0f, 44.0f };
                Surface.FillRectangle(WellR, ColorRgba{ 12u, 13u, 15u, 255u }, 6.0f);
                ConstructProceduralStampPreview(Surface, WellR, S);

                WorkspaceAppearance::ConstructTruncatedText(
                    Surface,
                    ActiveStamp ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                    { CardR.X + 7.0f, CardR.Bottom() - 18.0f },
                    CardR.Width - 14.0f,
                    8.8f,
                    ActiveStamp ? Theme.TextPrimary : Theme.TextMuted,
                    StampShapeCatalog[S]);

                if (Pressed && HoverStamp)
                {
                    Session->Brush.ActiveStampName = StampShapeCatalog[S];
                }
            }
        }
        else if (IsEraseStudio)
        {
            // Eraser Studio Tip & Controls (studio-eraser.png parity)
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::ShapeCircle, VectorIconPlacement{ CPadX, ContentY + 3.0f, 15.0f, StudioIconTint, 1.7f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX + 24.0f, ContentY - 2.0f }, 13.0f, Theme.TextPrimary, "Tip");
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX + 24.0f, ContentY + 15.0f }, 8.6f, Theme.TextFaint, "Size, strength, flow, edge");
            ContentY += 38.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX, ContentY }, 8.2f, Theme.TextFaint, "TIP");
            ContentY += 16.0f;

            float EraseRad = static_cast<float>(Session->Brush.EraserRadius);
            if (ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Size", EraseRad, 1.0f, 100.0f, "%.0f %%", 1.0f, Input, Theme))
            {
                Session->Brush.EraserRadius = static_cast<int32_t>(std::lround(EraseRad));
            }
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Strength", Session->Brush.EraserStrength, 0.0f, 1.0f, "%.0f %%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Flow", Session->Brush.DeliveryRate, 0.02f, 1.0f, "%.0f %%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Hardness", Session->Brush.EraserHardness, 0.0f, 1.0f, "%.0f %%", 100.0f, Input, Theme);
            ContentY += 52.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { CPadX, ContentY }, 9.2f, Theme.TextPrimary, "Edge");
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX + 34.0f, ContentY + 1.0f }, 8.2f, Theme.TextFaint, "Grain erases through the paper texture");
            ContentY += 18.0f;

            constexpr std::array<const char*, 3u> EdgeSegs {{ "Soft", "Hard", "Grain" }};
            uint32_t EdgeIdx = Session->Brush.EraseWithBrush ? 2u : (Session->Brush.EraserHardness > 0.8f ? 1u : 0u);
            if (ConstructSegmentedCapsuleRow(Surface, { CPadX, ContentY, CInnerW, 28.0f }, EdgeSegs.data(), 3u, EdgeIdx, Theme, Cursor, Pressed))
            {
                Session->Brush.EraseWithBrush = (EdgeIdx == 2u);
                Session->Brush.EraserHardness = (EdgeIdx == 1u) ? 0.95f : 0.70f;
            }
        }
        else if (IsFillStudio)
        {
            // Fill Studio Source & Scope (studio-fill.png parity)
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::FillBucket, VectorIconPlacement{ CPadX, ContentY + 3.0f, 15.0f, StudioIconTint, 1.7f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX + 24.0f, ContentY - 2.0f }, 13.0f, Theme.TextPrimary, "Source");
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX + 24.0f, ContentY + 15.0f }, 8.6f, Theme.TextFaint, "Colour or grain, opacity, blend");
            ContentY += 38.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX, ContentY }, 8.2f, Theme.TextFaint, "SOURCE");
            ContentY += 16.0f;
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX, ContentY }, 8.8f, Theme.TextMuted, "Paint with");
            ContentY += 16.0f;

            constexpr std::array<const char*, 2u> FillWithSegs {{ "Colour", "Grain" }};
            uint32_t FillWithIdx = 0u;
            (void)ConstructSegmentedCapsuleRow(Surface, { CPadX, ContentY, CInnerW, 28.0f }, FillWithSegs.data(), 2u, FillWithIdx, Theme, Cursor, Pressed);
            ContentY += 36.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX, ContentY }, 8.8f, Theme.TextMuted, "Colour");
            ContentY += 16.0f;

            const Rectangle2D ColBarR{ CPadX, ContentY, CInnerW, 34.0f };
            Surface.FillRectangle(ColBarR, Theme.WellSurface, 8.0f);
            Surface.StrokeRectangle(ColBarR, Theme.BorderSubtle, 8.0f, 1.0f);
            Surface.FillRectangle({ ColBarR.X + 5.0f, ColBarR.Y + 5.0f, 32.0f, 24.0f }, Session->Brush.InkColor, 6.0f);
            char HexBuf[24];
            std::snprintf(HexBuf, sizeof(HexBuf), "#%02X%02X%02X", Session->Brush.InkColor.Red, Session->Brush.InkColor.Green, Session->Brush.InkColor.Blue);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { ColBarR.X + 46.0f, ColBarR.Y + 9.0f }, 10.0f, Theme.TextPrimary, HexBuf);
            ContentY += 42.0f;

            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Opacity", Session->Brush.Opacity, 0.0f, 1.0f, "%.0f %%", 100.0f, Input, Theme);
            ContentY += 48.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX, ContentY }, 8.8f, Theme.TextMuted, "Scope");
            ContentY += 16.0f;
            constexpr std::array<const char*, 3u> ScopeSegs {{ "Asset part", "UV island", "Whole mesh" }};
            uint32_t ScopeIdx = static_cast<uint32_t>(Session->Brush.FillScope);
            if (ConstructSegmentedCapsuleRow(Surface, { CPadX, ContentY, CInnerW, 28.0f }, ScopeSegs.data(), 3u, ScopeIdx, Theme, Cursor, Pressed))
            {
                Session->Brush.FillScope = static_cast<PaintFillScope>(ScopeIdx);
            }
        }
        else if (IsGradientStudio)
        {
            // Gradient Studio 10-preset ramp library + Stops editor (studio-gradient.png parity)
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::PointGradient, VectorIconPlacement{ CPadX, ContentY + 3.0f, 15.0f, StudioIconTint, 1.7f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX + 24.0f, ContentY - 2.0f }, 13.0f, Theme.TextPrimary, "Colours");
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX + 24.0f, ContentY + 15.0f }, 8.6f, Theme.TextFaint, "Preset and stops");
            ContentY += 36.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX, ContentY }, 8.2f, Theme.TextFaint, "COLOURS");
            ContentY += 15.0f;
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX, ContentY }, 8.6f, Theme.TextMuted, "Preset");
            ContentY += 16.0f;

            const float CardGap = 8.0f;
            const float CardW   = (CInnerW - CardGap) * 0.5f;
            const float CardH   = 30.0f;
            for (std::size_t RIdx = 0u; RIdx < GradientRampPresets.size(); ++RIdx)
            {
                const std::size_t Col = RIdx % 2u;
                const std::size_t Row = RIdx / 2u;
                const Rectangle2D PillR{
                    CPadX + static_cast<float>(Col) * (CardW + CardGap),
                    ContentY + static_cast<float>(Row) * (CardH + 6.0f),
                    CardW,
                    CardH
                };
                const bool ActiveRamp = (Session->Brush.RampPresetIndex == static_cast<uint32_t>(RIdx));
                const bool HoverRamp  = PillR.Contains(Cursor);
                Surface.FillRectangle(PillR, ActiveRamp ? ColorRgba{ 238u, 238u, 236u, 255u } : (HoverRamp ? StudioRowHover : Theme.WellSurface), 8.0f);

                // 3-segment mini gradient chip
                const Rectangle2D SwR{ PillR.X + 6.0f, PillR.Y + 6.0f, 28.0f, 18.0f };
                Surface.FillRectangle({ SwR.X, SwR.Y, 10.0f, 18.0f }, GradientRampPresets[RIdx].StopA, 4.0f);
                Surface.FillRectangle({ SwR.X + 9.0f, SwR.Y, 10.0f, 18.0f }, GradientRampPresets[RIdx].StopB, 0.0f);
                Surface.FillRectangle({ SwR.X + 18.0f, SwR.Y, 10.0f, 18.0f }, GradientRampPresets[RIdx].StopC, 4.0f);

                WorkspaceAppearance::ConstructTruncatedText(
                    Surface,
                    ActiveRamp ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                    { SwR.Right() + 8.0f, PillR.Y + 7.5f },
                    PillR.Width - 46.0f,
                    9.0f,
                    ActiveRamp ? ColorRgba{ 16u, 17u, 20u, 255u } : Theme.TextPrimary,
                    GradientRampPresets[RIdx].Name);

                if (Pressed && HoverRamp)
                {
                    Session->Brush.RampPresetIndex = static_cast<uint32_t>(RIdx);
                    Session->Brush.InkColor        = GradientRampPresets[RIdx].StopA;
                    Session->Brush.SecondaryColor  = GradientRampPresets[RIdx].StopC;
                }
            }
            ContentY += 5.0f * (CardH + 6.0f) + 10.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX, ContentY }, 8.6f, Theme.TextMuted, "Stops");
            ContentY += 16.0f;

            const auto& ActiveRampSpec = GradientRampPresets[std::min<std::size_t>(Session->Brush.RampPresetIndex, GradientRampPresets.size() - 1u)];
            const Rectangle2D RampBarR{ CPadX, ContentY, CInnerW, 14.0f };
            constexpr int Segs = 24;
            const float SegW = RampBarR.Width / static_cast<float>(Segs);
            for (int S = 0; S < Segs; ++S)
            {
                const float T = static_cast<float>(S) / static_cast<float>(Segs - 1);
                const ColorRgba C0 = (T < 0.5f) ? ActiveRampSpec.StopA : ActiveRampSpec.StopB;
                const ColorRgba C1 = (T < 0.5f) ? ActiveRampSpec.StopB : ActiveRampSpec.StopC;
                const float LocalT = (T < 0.5f) ? (T * 2.0f) : ((T - 0.5f) * 2.0f);
                const ColorRgba Mix{
                    static_cast<uint8_t>(C0.Red   + (C1.Red   - C0.Red)   * LocalT),
                    static_cast<uint8_t>(C0.Green + (C1.Green - C0.Green) * LocalT),
                    static_cast<uint8_t>(C0.Blue  + (C1.Blue  - C0.Blue)  * LocalT),
                    255u
                };
                Surface.FillRectangle({ RampBarR.X + static_cast<float>(S) * SegW, RampBarR.Y, SegW + 1.0f, RampBarR.Height }, Mix, (S == 0 || S + 1 == Segs) ? 6.0f : 0.0f);
            }
            ContentY += 22.0f;

            Surface.FillRectangle({ CPadX, ContentY, 24.0f, 24.0f }, ActiveRampSpec.StopA, 6.0f);
            Surface.FillRectangle({ CPadX + 30.0f, ContentY, 24.0f, 24.0f }, ActiveRampSpec.StopB, 6.0f);
            Surface.FillRectangle({ CPadX + 60.0f, ContentY, 24.0f, 24.0f }, ActiveRampSpec.StopC, 6.0f);
        }
        else if (Session->InspectorSubTab == BrushDetailSubTab::LibraryGrid)
        {
            if (IsTextStudio)
            {
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Bold, { CPadX, ContentY }, 9.2f, Theme.TextFaint, "FONTS");
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { RightPaneR.Right() - 26.0f, ContentY }, 9.2f, Theme.TextFaint, "6");
                ContentY += 22.0f;

                const auto& Fonts = TexturePaintSession::QueryFontLabels();
                const float CardGap = 10.0f;
                const float CardW   = (CInnerW - CardGap) * 0.5f;
                const float CardH   = 82.0f;

                for (std::size_t F = 0u; F < Fonts.size(); ++F)
                {
                    const std::size_t Col = F % 2u;
                    const std::size_t Row = F / 2u;
                    const Rectangle2D CardR{
                        CPadX + static_cast<float>(Col) * (CardW + CardGap),
                        ContentY + static_cast<float>(Row) * (CardH + CardGap),
                        CardW,
                        CardH
                    };
                    const bool ActiveFont = (Session->Brush.TextFontIndex == static_cast<uint32_t>(F));
                    const bool HoverFont  = CardR.Contains(Cursor);

                    Surface.FillRectangle(CardR, ActiveFont ? StudioRowSelected : Theme.SectionSurface, 10.0f);
                    Surface.StrokeRectangle(
                        CardR,
                        ActiveFont ? ColorRgba{ 238u, 238u, 236u, 230u } : (HoverFont ? Theme.BorderStrong : Theme.BorderSubtle),
                        10.0f,
                        ActiveFont ? 1.5f : 1.0f);

                    const Rectangle2D WellR{ CardR.X + 6.0f, CardR.Y + 6.0f, CardR.Width - 12.0f, 48.0f };
                    Surface.FillRectangle(WellR, Theme.WellSurface, 7.0f);
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Bold, { WellR.X + 12.0f, WellR.Y + 12.0f }, 19.0f, Theme.TextPrimary, "Aa");
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Medium, { WellR.X + 46.0f, WellR.Y + 17.0f }, 12.5f, Theme.TextMuted, "Frontier");

                    WorkspaceAppearance::ConstructWeightedText(
                        Surface,
                        ActiveFont ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                        { CardR.X + 10.0f, CardR.Bottom() - 21.0f },
                        9.8f,
                        Theme.TextPrimary,
                        Fonts[F]);

                    if (Pressed && HoverFont)
                    {
                        Session->SelectTextFont(static_cast<uint32_t>(F));
                    }
                }
            }
            else
            {
                VectorIconRepository::ConstructControl(
                    Surface, ControlIcon::LayersSlabs, VectorIconPlacement{ CPadX, ContentY + 3.0f, 15.0f, StudioIconTint, 1.7f });
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Bold, { CPadX + 24.0f, ContentY - 2.0f }, 13.0f, Theme.TextPrimary, "Brushes");
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Regular, { CPadX + 24.0f, ContentY + 15.0f }, 8.6f, Theme.TextFaint, "27 presets · Brush Studio");
                ContentY += 32.0f;

                const Rectangle2D SearchR{ CPadX, ContentY, CInnerW, 26.0f };
                Surface.FillRectangle(SearchR, ColorRgba{ 19u, 20u, 23u, 255u }, 13.0f);
                VectorIconRepository::ConstructControl(
                    Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchR.X + 9.0f, SearchR.Y + 7.0f, 12.0f, Theme.TextFaint, 1.6f });
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Regular, { SearchR.X + 27.0f, SearchR.Y + 5.5f }, 9.0f, Theme.TextFaint, "Search brushes");
                ContentY += 32.0f;

                // Category filter pills ("All 27 | Oil 4 | Pencil 3 | Ink 3 | Paint 4 | Water 2 | FX 6 | Tech 5", matching cards-brush-library.png)
                static constexpr std::array<const char*, 8u> CatPills {{
                    "All 27", "Oil 4", "Pencil 3", "Ink 3", "Paint 4", "Water 2", "FX 6", "Tech 5"
                }};
                float PillX = CPadX;
                for (std::size_t C = 0u; C < CatPills.size(); ++C)
                {
                    const float Pw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 7.8f, CatPills[C]) + 14.0f;
                    if (PillX + Pw > CPadX + CInnerW) break;
                    const Rectangle2D PillR{ PillX, ContentY, Pw, 19.0f };
                    const bool ActC = (C == 0u);
                    Surface.FillRectangle(PillR, ActC ? ColorRgba{ 46u, 36u, 42u, 255u } : ColorRgba{ 30u, 31u, 36u, 255u }, 9.5f);
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, ActC ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                        { PillR.X + 7.0f, PillR.Y + 3.0f }, 7.8f, ActC ? Theme.TextPrimary : Theme.TextMuted, CatPills[C]);
                    PillX += Pw + 4.0f;
                }
                ContentY += 25.0f;

                Surface.FillCircle({ CPadX + 4.0f, ContentY + 7.0f }, 3.5f, ColorRgba{ 96u, 165u, 250u, 255u });
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::SemiBold, { CPadX + 14.0f, ContentY }, 9.2f, Theme.TextPrimary, "Brushes & Inks");
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { RightPaneR.Right() - 28.0f, ContentY }, 8.6f, Theme.TextFaint, "12");
                ContentY += 18.0f;

                // 4-column S-curve brush library grid (cards-brush-library.png 1:1 parity)
                const float CardGap = 6.0f;
                const float CardW   = (CInnerW - CardGap * 3.0f) / 4.0f;
                const float CardH   = 84.0f;

                for (std::size_t P = 0u; P < Presets.size(); ++P)
                {
                    const std::size_t Col = P % 4u;
                    const std::size_t Row = P / 4u;
                    const Rectangle2D CardR{
                        CPadX + static_cast<float>(Col) * (CardW + CardGap),
                        ContentY + static_cast<float>(Row) * (CardH + CardGap),
                        CardW,
                        CardH
                    };
                    if (CardR.Bottom() > RightPaneR.Bottom() + 20.0f)
                    {
                        continue;
                    }
                    const bool ActiveCard = (Session->Brush.ActivePresetIndex == static_cast<uint32_t>(P));
                    const bool HoverCard  = CardR.Contains(Cursor);

                    Surface.FillRectangle(CardR, ActiveCard ? ColorRgba{ 37u, 38u, 43u, 255u } : ColorRgba{ 28u, 29u, 33u, 255u }, 9.0f);
                    if (ActiveCard || HoverCard)
                    {
                        Surface.StrokeRectangle(
                            CardR,
                            ActiveCard ? ColorRgba{ 255u, 255u, 255u, 255u } : ColorRgba{ 255u, 255u, 255u, 42u },
                            9.0f,
                            ActiveCard ? 1.8f : 1.0f);
                    }

                    const Rectangle2D PreviewWellR{ CardR.X + 5.0f, CardR.Y + 5.0f, CardR.Width - 10.0f, 52.0f };
                    Surface.FillRectangle(PreviewWellR, ColorRgba{ 12u, 13u, 15u, 255u }, 6.0f);
                    ConstructBrushStrokePreviewWave(Surface, PreviewWellR, Presets[P], ActiveCard, Theme);

                    WorkspaceAppearance::ConstructTruncatedText(
                        Surface,
                        ActiveCard ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                        { CardR.X + 7.0f, CardR.Bottom() - 20.0f },
                        CardR.Width - 14.0f,
                        8.2f,
                        ActiveCard ? Theme.TextPrimary : Theme.TextMuted,
                        Presets[P].Label);

                    if (Pressed && HoverCard)
                    {
                        Session->SelectBrushPreset(static_cast<uint32_t>(P));
                    }
                }
            }
        }
        else if (Session->InspectorSubTab == BrushDetailSubTab::Curves)
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX, ContentY }, 10.5f, Theme.TextPrimary, "Stroke Dynamics & Response Curves");
            ContentY += 24.0f;

            constexpr std::array<const char*, 4u> CurveTabs {{ "Tip Falloff", "Size", "Opacity", "Hue Shift" }};
            uint32_t CurIdx = static_cast<uint32_t>(Session->Brush.ActiveCurve);
            if (ConstructSegmentedCapsuleRow(Surface, { CPadX, ContentY, CInnerW, 28.0f }, CurveTabs.data(), 4u, CurIdx, Theme, Cursor, Pressed))
            {
                Session->Brush.ActiveCurve = static_cast<BrushResponseCurve>(CurIdx);
            }
            ContentY += 38.0f;

            const Rectangle2D GraphR{ CPadX, ContentY, CInnerW, 148.0f };
            Surface.FillRectangle(GraphR, Theme.WellSurface, 10.0f);
            Surface.StrokeRectangle(GraphR, Theme.BorderSubtle, 10.0f, 1.0f);

            const auto& Pts = Session->Brush.Curves[static_cast<std::size_t>(Session->Brush.ActiveCurve)];
            for (std::size_t K = 0u; K + 1u < Pts.size(); ++K)
            {
                const Point2D P0{ GraphR.X + 14.0f + Pts[K].X * (GraphR.Width - 28.0f), GraphR.Bottom() - 14.0f - Pts[K].Y * (GraphR.Height - 28.0f) };
                const Point2D P1{ GraphR.X + 14.0f + Pts[K + 1u].X * (GraphR.Width - 28.0f), GraphR.Bottom() - 14.0f - Pts[K + 1u].Y * (GraphR.Height - 28.0f) };
                Surface.StrokeLine(P0, P1, ColorRgba{ 238u, 238u, 236u, 255u }, 2.0f);
                Surface.FillCircle(P0, 4.5f, ColorRgba{ 255u, 255u, 255u, 255u });
                if (K + 2u == Pts.size())
                {
                    Surface.FillCircle(P1, 4.5f, ColorRgba{ 255u, 255u, 255u, 255u });
                }
            }
            ContentY += 162.0f;

            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Size Jitter", Session->Brush.SizeJitter, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 50.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Opacity Jitter", Session->Brush.OpacityJitter, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
        }
        else if (IsTextStudio)
        {
            // Text Studio Type & Layout (studio-text.png parity)
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::TextType, VectorIconPlacement{ CPadX, ContentY + 3.0f, 15.0f, StudioIconTint, 1.7f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX + 24.0f, ContentY - 2.0f }, 13.0f, Theme.TextPrimary, "Type");
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX + 24.0f, ContentY + 15.0f }, 8.6f, Theme.TextFaint, "Font, weight, italic, align");
            ContentY += 38.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX, ContentY }, 8.2f, Theme.TextFaint, "TYPE");
            ContentY += 16.0f;
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX, ContentY }, 8.8f, Theme.TextMuted, "Font");
            ContentY += 16.0f;

            const auto& Fonts = TexturePaintSession::QueryFontLabels();
            const char* ActiveFontName = Fonts[std::min<std::size_t>(Session->Brush.TextFontIndex, Fonts.size() - 1u)];
            (void)ConstructHeaderActionCapsule(Surface, { CPadX, ContentY, CInnerW, 30.0f }, ControlIcon::Count, ActiveFontName, true, false, Theme, Cursor, Pressed);
            ContentY += 38.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX, ContentY }, 8.8f, Theme.TextMuted, "Weight");
            ContentY += 16.0f;
            constexpr std::array<const char*, 4u> WeightSegs {{ "300", "400", "600", "800" }};
            uint32_t WeightIdx = 2u;
            (void)ConstructSegmentedCapsuleRow(Surface, { CPadX, ContentY, CInnerW, 28.0f }, WeightSegs.data(), 4u, WeightIdx, Theme, Cursor, Pressed);
            ContentY += 36.0f;

            bool ItalicFlag = false;
            (void)ConstructSwitchControlRow(Surface, { CPadX, ContentY, CInnerW, 30.0f }, "Italic", ItalicFlag, Theme, Cursor, Pressed);
            ContentY += 36.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { CPadX, ContentY }, 8.8f, Theme.TextMuted, "Align");
            ContentY += 16.0f;
            constexpr std::array<const char*, 3u> AlignSegs {{ "Left", "Centre", "Right" }};
            uint32_t AlignIdx = 1u;
            (void)ConstructSegmentedCapsuleRow(Surface, { CPadX, ContentY, CInnerW, 28.0f }, AlignSegs.data(), 3u, AlignIdx, Theme, Cursor, Pressed);
            ContentY += 38.0f;

            float TxtSz = static_cast<float>(Session->Brush.TextSizePx);
            if (ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Font size", TxtSz, 12.0f, 160.0f, "%.0f px", 1.0f, Input, Theme))
            {
                Session->Brush.TextSizePx = static_cast<int32_t>(std::lround(TxtSz));
            }
        }
        else
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX, ContentY }, 10.5f, Theme.TextPrimary, "Brush Parameters");
            ContentY += 24.0f;

            float RadiusFloat = static_cast<float>(Session->Brush.PixelRadius);
            if (ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Radius", RadiusFloat, 2.0f, 128.0f, "%.0f px", 1.0f, Input, Theme))
            {
                Session->Brush.PixelRadius = static_cast<int32_t>(std::lround(RadiusFloat));
            }
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Opacity", Session->Brush.Opacity, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Flow", Session->Brush.DeliveryRate, 0.02f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Hardness", Session->Brush.Hardness, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Spacing", Session->Brush.Spacing, 0.02f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Stabilization", Session->Brush.Stabilizer, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Roughness", Session->Brush.Roughness, 0.02f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Metalness", Session->Brush.Metalness, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
        }

        Surface.PopClip();

        if (Pressed && !ModalR.Contains(Cursor))
        {
            BrushStudioModalOpen = false;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
