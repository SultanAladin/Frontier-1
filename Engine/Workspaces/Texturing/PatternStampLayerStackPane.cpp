/*============================================================================================================================================
                                                        PATTERNSTAMPLAYERSTACKPANE.CPP
============================================================================================================================================*/
// 🧩 1:1 implementation of PatternStampLayerStackPane matching Experimental/TextureStudio docked Artwork Outliner (cards-pattern.png,
//    outliner-footer-pattern.png, menu-shapes.png): VISIBLE / MODIFIERS KPI tiles, Search + Filter pill row, + Motif / + Text / + Gradient
//    quick-add chips, hierarchical Document -> Layer -> Modifiers/Surface tree + Starters group, action bar, and 4-column telemetry footer.

#include "Engine/Workspaces/Texturing/PatternStampLayerStackPane.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

using namespace TexturingAppearance;

namespace
{

void ClearEditText(TextEditBuffer& Buf) noexcept
{
    Buf.Text[0]         = '\0';
    Buf.Length          = 0u;
    Buf.Caret           = 0u;
    Buf.SelectionAnchor = 0u;
}

ControlIcon QueryMotifPreviewIcon(const PatternStampLayerSpec& Layer) noexcept
{
    if (Layer.Category == PatternStampLayerCategory::Folder)       return ControlIcon::FolderOpen;
    if (Layer.Category == PatternStampLayerCategory::Typographic)  return ControlIcon::TextType;
    if (Layer.Category == PatternStampLayerCategory::GradientWash) return ControlIcon::PointGradient;
    switch (Layer.MotifShape)
    {
        case PatternStampMotifShape::Ring:
        case PatternStampMotifShape::Circle:    return ControlIcon::ShapeCircle;
        case PatternStampMotifShape::Square:
        case PatternStampMotifShape::RoundedBox:return ControlIcon::ShapeSquare;
        case PatternStampMotifShape::Diamond:   return ControlIcon::ShapeDiamond;
        case PatternStampMotifShape::Hexagon:   return ControlIcon::ShapeHexagon;
        case PatternStampMotifShape::Triangle:  return ControlIcon::ShapeTriangle;
        case PatternStampMotifShape::Star:      return ControlIcon::ShapeStar;
        case PatternStampMotifShape::StripeBar:
        case PatternStampMotifShape::LineStroke:return ControlIcon::ShapeStripe;
        default:                                return ControlIcon::VectorCurve;
    }
}

void DrawRootDocThumb(
    PixelSpace&                          Surface,
    Rectangle2D                          SlotR,
    const PatternStampWorkspaceSession&  Session) noexcept
{
    Surface.FillRectangle(SlotR, ColorRgba{ 23u, 26u, 31u, 255u }, 6.0f);
    Surface.StrokeRectangle(SlotR, ColorRgba{ 255u, 255u, 255u, 26u }, 6.0f, 1.0f);
    const float Cx = SlotR.X + SlotR.Width * 0.5f;
    const float Cy = SlotR.Y + SlotR.Height * 0.5f;
    if (Session.Mode == PatternStampWorkspaceMode::Pattern)
    {
        for (int R = -1; R <= 1; ++R)
        {
            for (int C = -1; C <= 1; ++C)
            {
                const float Dx = Cx + static_cast<float>(C) * 5.2f;
                const float Dy = Cy + static_cast<float>(R) * 5.2f;
                Surface.FillCircle({ Dx, Dy }, (R == 0 && C == 0) ? 2.1f : 1.5f,
                    ((R + C) & 1) ? ColorRgba{ 158u, 207u, 255u, 255u } : ColorRgba{ 239u, 244u, 207u, 255u });
            }
        }
    }
    else
    {
        Surface.FillCircle({ Cx, Cy }, 7.0f, ColorRgba{ 122u, 152u, 184u, 255u });
        Surface.FillCircle({ Cx, Cy }, 3.8f, ColorRgba{ 232u, 238u, 246u, 255u });
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

PatternStampLayerStackPane::~PatternStampLayerStackPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void PatternStampLayerStackPane::AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept
{
    Session            = TargetSession;
    AddLayerSheetOpen  = false;
    RenamingLayerId    = 0u;
    LastClickLayerId   = 0u;
    TimeSinceLastClick = 10.0;
    ClearEditText(SearchBuffer);
    SearchBuffer.Active = false;
    if (Session != nullptr)
    {
        Session->LayerSearch.clear();
    }
}

void PatternStampLayerStackPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##PatternStampLayerStackCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANE RENDERING
//------------------------------------------------------------------------------------------------------------------------

void PatternStampLayerStackPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Session == nullptr || Bounds.Width < 160.0f || Bounds.Height < 200.0f)
    {
        return;
    }

    const TexturingThemeColors Theme   = QueryTexturingThemeColors();
    const Point2D              Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed = Input.PrimaryPointer.Pressed;
    const bool                 IsPat   = (Session->Mode == PatternStampWorkspaceMode::Pattern);

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.LightSurface ? Theme.PaneBackground : ColorRgba{ 24u, 25u, 28u, 255u }, 0.0f);

    const float PadX   = 12.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float       CurY   = Bounds.Y + 12.0f;

    // 1. Outliner Header ("Layers  Pattern Studio · seamless tile" or "Stamp Studio · pbr decal" + Folder icon button)
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, CurY - 2.0f }, 15.0f, Theme.TextPrimary, "Layers");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX + 52.0f, CurY + 3.0f }, 8.8f, Theme.TextFaint,
        IsPat ? "Pattern Studio · seamless tile" : "Stamp Studio · pbr decal");

    const Rectangle2D TopFolderR{ Bounds.Right() - PadX - 24.0f, CurY - 1.0f, 24.0f, 24.0f };
    Surface.FillRectangle(TopFolderR, TopFolderR.Contains(Cursor) ? ColorRgba{ 44u, 45u, 49u, 255u } : ColorRgba{ 34u, 35u, 39u, 255u }, 12.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::FolderOpen, VectorIconPlacement{ TopFolderR.X + 6.0f, TopFolderR.Y + 6.0f, 12.0f, Theme.TextMuted, 1.65f });
    if (Pressed && TopFolderR.Contains(Cursor))
    {
        Session->AddLayer(PatternStampLayerCategory::Folder);
    }
    CurY += 30.0f;

    // 2. Two KPI Stat Tiles: VISIBLE (2/2 or 1/1) & MODIFIERS (4/4 or 0/0) — matches cards-pattern.png & menu-shapes.png 1:1
    const std::size_t TotalLayers = Session->Document.Layers.size();
    const std::size_t VisLayers   = Session->CountVisibleLayers();
    const std::size_t ActMods     = Session->CountEnabledModifiers();
    std::size_t TotalMods         = 0u;
    for (const auto& L : Session->Document.Layers)
    {
        TotalMods += L.Modifiers.size();
    }

    char VisBuf[24], ModBuf[24];
    std::snprintf(VisBuf, sizeof(VisBuf), "%zu/%zu", VisLayers, TotalLayers);
    std::snprintf(ModBuf, sizeof(ModBuf), "%zu/%zu", ActMods, TotalMods);

    const float StatGap = 8.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    ConstructOutlinerStatTile(
        Surface, { LeftX, CurY, StatW, 42.0f }, ControlIcon::EyeVisible, "VISIBLE", VisBuf, VisLayers > 0u, false, Theme, Cursor, Pressed);
    ConstructOutlinerStatTile(
        Surface, { LeftX + StatW + StatGap, CurY, StatW, 42.0f }, ControlIcon::SlidersQuality, "MODIFIERS", ModBuf, ActMods > 0u, false, Theme, Cursor, Pressed);
    CurY += 50.0f;

    // 3. Search & Filter Row ("Search layers, modifiers" + "Filter v")
    const float FilterW = 68.0f;
    const Rectangle2D SearchR{ LeftX, CurY, InnerW - FilterW - 6.0f, 28.0f };
    Surface.FillRectangle(SearchR, Theme.LightSurface ? Theme.WellSurface : ColorRgba{ 19u, 20u, 23u, 255u }, 14.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchR.X + 10.0f, SearchR.Y + 8.0f, 12.0f, Theme.TextFaint, 1.6f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { SearchR.X + 27.0f, SearchR.Y + 6.5f }, 9.0f, Theme.TextFaint, "Search layers, modifiers");

    const Rectangle2D FilterR{ SearchR.Right() + 6.0f, CurY, FilterW, 28.0f };
    Surface.FillRectangle(FilterR, Theme.LightSurface ? Theme.WellSurface : ColorRgba{ 19u, 20u, 23u, 255u }, 14.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { FilterR.X + 12.0f, FilterR.Y + 6.5f }, 8.8f, Theme.TextMuted, "Filter");
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ChevronDown, VectorIconPlacement{ FilterR.Right() - 16.0f, FilterR.Y + 9.5f, 9.0f, Theme.TextFaint, 1.6f });
    CurY += 35.0f;

    // 4. Quick-Add Chip Row ("+ Motif", "+ Text", "+ Gradient") — matches cards-pattern.png 1:1
    struct QuickAddSpec { const char* Label; PatternStampLayerCategory Cat; };
    static constexpr std::array<QuickAddSpec, 3u> QuickAdds {{
        { "+ Motif",    PatternStampLayerCategory::Motif        },
        { "+ Text",     PatternStampLayerCategory::Typographic  },
        { "+ Gradient", PatternStampLayerCategory::GradientWash },
    }};
    float ChipX = LeftX;
    for (const auto& Qa : QuickAdds)
    {
        const float ChipW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, Qa.Label) + 20.0f;
        const Rectangle2D ChipR{ ChipX, CurY, ChipW, 22.0f };
        Surface.FillRectangle(
            ChipR,
            ChipR.Contains(Cursor)
                ? (Theme.LightSurface ? Theme.HoverSurface : ColorRgba{ 40u, 42u, 48u, 255u })
                : (Theme.LightSurface ? Theme.SectionSurface : ColorRgba{ 30u, 31u, 36u, 255u }),
            11.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { ChipR.X + 10.0f, ChipR.Y + 4.0f }, 8.4f, Theme.TextMuted, Qa.Label);
        if (Pressed && ChipR.Contains(Cursor))
        {
            Session->AddLayer(Qa.Cat);
        }
        ChipX += ChipW + 6.0f;
    }
    CurY += 29.0f;

    // Reserve space at bottom for the Action Bar (30px) + 4-Column Telemetry Footer (32px) matching outliner-footer-pattern.png
    constexpr float ActionBarH = 30.0f;
    constexpr float StatusBarH = 32.0f;
    const Rectangle2D ScrollRect{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - ActionBarH - StatusBarH - CurY)
    };
    if (ScrollRect.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        const float MaxScroll = std::max(0.0f, CachedContentHeight - ScrollRect.Height);
        ScrollOffsetY         = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScroll);
    }

    Surface.PushClip(ScrollRect);
    float Sy = ScrollRect.Y + 2.0f - ScrollOffsetY;
    const float ScrollStartY = Sy;

    // 5a. Root Document Row (Depth 0): "Noise-Scaled Hex Matrix" / "Canvas tile · 1024 px · ground on" + SEAMLESS or CUTOUT badge
    {
        char DocMeta[64];
        std::snprintf(DocMeta, sizeof(DocMeta), "%s · %u px · %s",
            IsPat ? "Canvas tile" : "Decal",
            Session->ResolutionPx,
            Session->Document.GroundEnabled ? "ground on" : "no ground");

        const Rectangle2D DocRowR{ LeftX, Sy, InnerW, 36.0f };
        Rectangle2D DocIconSlot {};
        (void)ConstructOutlinerTreeRow(
            Surface, DocRowR, 0u, true, true, false, false,
            ControlIcon::Count, Theme.TextPrimary,
            Session->Document.DocumentName, DocMeta,
            {}, "", -1, -1, -1, Theme, Cursor, Pressed, false, &DocIconSlot);
        DrawRootDocThumb(Surface, DocIconSlot, *Session);

        // Badge: SEAMLESS (green tint) or CUTOUT (blue tint)
        const char* BadgeText = IsPat ? "SEAMLESS" : "CUTOUT";
        const float BadgeW    = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 7.2f, BadgeText) + 12.0f;
        const Rectangle2D BadgeR{ DocRowR.Right() - BadgeW - 6.0f, DocRowR.Y + 10.0f, BadgeW, 16.0f };
        Surface.FillRectangle(BadgeR, IsPat ? ColorRgba{ 22u, 52u, 36u, 255u } : ColorRgba{ 22u, 42u, 62u, 255u }, 5.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { BadgeR.X + 6.0f, BadgeR.Y + 2.5f }, 7.2f,
            IsPat ? ColorRgba{ 74u, 222u, 128u, 255u } : ColorRgba{ 96u, 165u, 250u, 255u }, BadgeText);
        Sy += 38.0f;
    }

    // 5b. Layer Rows (Depth 1) + Indented Modifier & Surface Child Rows (Depth 2)
    static constexpr std::array<const char*, 6u> BlendNames {{ "Normal", "Multiply", "Screen", "Overlay", "Add", "Darken" }};
    const auto& MotifLabels  = PatternStampWorkspaceSession::InspectMotifShapeLabels();
    const auto& ModLabels    = PatternStampWorkspaceSession::InspectModifierLabels();
    const auto& FinishLabels = PatternStampWorkspaceSession::InspectFinishLabels();

    for (std::size_t LIdx = 0u; LIdx < Session->Document.Layers.size(); ++LIdx)
    {
        auto& Layer          = Session->Document.Layers[LIdx];
        const bool IsSel     = (Layer.Identity == Session->Document.SelectedLayerId);
        const bool LayerOpen = IsSel;

        char LayerMeta[64];
        const int OpPct = static_cast<int>(std::lround(Layer.Opacity * 100.0f));
        const char* MotifLbl = MotifLabels[std::min<std::size_t>(static_cast<std::size_t>(Layer.MotifShape), MotifLabels.size() - 1u)];
        const char* BlendLbl = BlendNames[std::min<std::size_t>(Layer.BlendMode, BlendNames.size() - 1u)];
        if (IsSel)
        {
            std::snprintf(LayerMeta, sizeof(LayerMeta), "%s · %s %d%%", MotifLbl, BlendLbl, OpPct);
        }
        else
        {
            std::snprintf(LayerMeta, sizeof(LayerMeta), "%s · %s · %zu/%zu mods",
                MotifLbl, BlendLbl, Layer.Modifiers.size(), Layer.Modifiers.size());
        }

        const Rectangle2D LRowR{ LeftX, Sy, InnerW, 36.0f };
        const auto LAct = ConstructOutlinerTreeRow(
            Surface, LRowR, 1u, true, LayerOpen, IsSel, !Layer.Visible,
            QueryMotifPreviewIcon(Layer), Theme.TextPrimary,
            Layer.Name, LayerMeta,
            {}, "", Layer.Locked ? 1 : 0, -1, Layer.Visible ? 1 : 0,
            Theme, Cursor, Pressed, false);

        if (LAct.EyeToggled)
        {
            Layer.Visible = !Layer.Visible;
        }
        else if (LAct.LockToggled)
        {
            Layer.Locked = !Layer.Locked;
        }
        else if (LAct.RowSelected || LAct.CaretToggled)
        {
            Session->SelectLayer(Layer.Identity);
        }
        Sy += 37.0f;

        if (LayerOpen)
        {
            for (std::size_t MIdx = 0u; MIdx < Layer.Modifiers.size(); ++MIdx)
            {
                auto& Mod = Layer.Modifiers[MIdx];
                const bool IsNoise = (Mod.Category == PatternStampModifierCategory::NoiseScale ||
                                      Mod.Category == PatternStampModifierCategory::ScatterDropout);
                char ModMeta[64];
                if (IsNoise)
                {
                    std::snprintf(ModMeta, sizeof(ModMeta), "NOISE · #%zu · WORLEY · %.2f-%.2fx",
                        MIdx + 1u, Mod.MinScale, Mod.MaxScale);
                }
                else
                {
                    std::snprintf(ModMeta, sizeof(ModMeta), "ARRAY · #%zu · Grid %dx%d",
                        MIdx + 1u, Mod.Columns, Mod.Rows);
                }

                const char* ModName = ModLabels[std::min<std::size_t>(static_cast<std::size_t>(Mod.Category), ModLabels.size() - 1u)];
                const Rectangle2D MRowR{ LeftX, Sy, InnerW, 32.0f };
                const auto MAct = ConstructOutlinerTreeRow(
                    Surface, MRowR, 2u, false, false, false, !Mod.Enabled,
                    IsNoise ? ControlIcon::NoiseWave : ControlIcon::PatternTile,
                    Theme.TextMuted,
                    ModName,
                    ModMeta,
                    {}, "", -1, Mod.Enabled ? 1 : 0, -1,
                    Theme, Cursor, Pressed, false);
                if (MAct.StatusToggled)
                {
                    Mod.Enabled = !Mod.Enabled;
                }
                Sy += 33.0f;
            }

            // Indented Surface row under active layer ("Surface / Metal Inlay · 4 ch")
            {
                char SurfMeta[64];
                const char* FinLbl = FinishLabels[std::min<std::size_t>(static_cast<std::size_t>(Layer.FinishPreset), FinishLabels.size() - 1u)];
                std::snprintf(SurfMeta, sizeof(SurfMeta), "%s · %zu ch", FinLbl, Layer.Channels.size());
                const Rectangle2D SRowR{ LeftX, Sy, InnerW, 32.0f };
                (void)ConstructOutlinerTreeRow(
                    Surface, SRowR, 2u, false, false, false, false,
                    ControlIcon::LayersSlabs, Theme.TextMuted,
                    "Surface", SurfMeta,
                    {}, "", -1, -1, -1,
                    Theme, Cursor, Pressed, false);
                Sy += 33.0f;
            }
        }
    }

    // 5c. Starters Folder Row (Depth 0): "Starters" / "21 patterns · 5 groups" or "30 stamps · 8 groups"
    {
        const Rectangle2D StartRowR{ LeftX, Sy, InnerW, 34.0f };
        (void)ConstructOutlinerTreeRow(
            Surface, StartRowR, 0u, true, false, false, false,
            ControlIcon::FolderOpen, Theme.TextMuted,
            "Starters", IsPat ? "21 patterns · 5 groups" : "30 stamps · 8 groups",
            {}, "", -1, -1, -1,
            Theme, Cursor, Pressed, false);
        Sy += 36.0f;
    }

    CachedContentHeight = (Sy - ScrollStartY) + 8.0f;
    Surface.PopClip();

    // 6. Action Bar above Footer (+ | Folder | ^ | v | Copy | Eye | Lock ... Trash, matching outliner-footer-pattern.png)
    const float ActY = Bounds.Bottom() - ActionBarH - StatusBarH;
    static constexpr std::array<ControlIcon, 7u> LeftIcons {{
        ControlIcon::PlusAdd,
        ControlIcon::FolderOpen,
        ControlIcon::ChevronUp,
        ControlIcon::ChevronDown,
        ControlIcon::CopyDuplicate,
        ControlIcon::EyeVisible,
        ControlIcon::LockOpen,
    }};
    for (std::size_t I = 0u; I < LeftIcons.size(); ++I)
    {
        const Rectangle2D BtnR{ LeftX + static_cast<float>(I) * 25.0f, ActY + 3.0f, 23.0f, 23.0f };
        if (ConstructHeaderIconButton(Surface, BtnR, LeftIcons[I], I == 0u && AddLayerSheetOpen, true, Theme, Cursor, Pressed))
        {
            if (I == 0u)      AddLayerSheetOpen = !AddLayerSheetOpen;
            else if (I == 1u) Session->AddLayer(PatternStampLayerCategory::Folder);
            else if (I == 2u) (void)Session->MoveLayerStep(Session->Document.SelectedLayerId, 1);
            else if (I == 3u) (void)Session->MoveLayerStep(Session->Document.SelectedLayerId, -1);
            else if (I == 4u) (void)Session->DuplicateSelectedLayer();
        }
    }
    const Rectangle2D TrashR{ Bounds.Right() - PadX - 24.0f, ActY + 3.0f, 23.0f, 23.0f };
    if (ConstructHeaderIconButton(Surface, TrashR, ControlIcon::TrashDelete, false, TotalLayers > 1u, Theme, Cursor, Pressed))
    {
        (void)Session->DeleteSelectedLayer();
    }

    // 7. Bottom 4-Column Telemetry Footer (LAYERS | INSTANCES | COVERAGE | SEAM / MARGIN, matching outliner-footer-pattern.png)
    const Rectangle2D FootR{ Bounds.X + 8.0f, Bounds.Bottom() - StatusBarH - 4.0f, Bounds.Width - 16.0f, StatusBarH };
    char LayCol[24], InstCol[24], CovCol[24], SeamCol[24];
    std::snprintf(LayCol, sizeof(LayCol), "%zu / 32", TotalLayers);
    std::snprintf(InstCol, sizeof(InstCol), "%d", std::max(1, Session->InstancesEvaluated));
    std::snprintf(CovCol, sizeof(CovCol), "%d %%", IsPat ? 46 : Session->SurfaceFractionPct);
    std::snprintf(SeamCol, sizeof(SeamCol), "%s", IsPat ? "8 wrap" : "106 px");

    const std::array<std::pair<std::string_view, std::string_view>, 4u> FootCols {{
        { "LAYERS",    LayCol  },
        { "INSTANCES", InstCol },
        { "COVERAGE",  CovCol  },
        { IsPat ? "SEAM" : "MARGIN", SeamCol },
    }};
    ConstructOutlinerTelemetryStrip(Surface, FootR, FootCols, Theme);

    // 8. Floating Add Layer Menu when '+' is toggled open (Exhibit 06)
    if (AddLayerSheetOpen)
    {
        struct AddItem { ControlIcon Icon; const char* Label; PatternStampLayerCategory Cat; };
        static constexpr std::array<AddItem, 4u> Items {{
            { ControlIcon::ShapeCircle,   "Motif layer",    PatternStampLayerCategory::Motif        },
            { ControlIcon::TextType,      "Text layer",     PatternStampLayerCategory::Typographic  },
            { ControlIcon::PointGradient, "Gradient layer", PatternStampLayerCategory::GradientWash },
            { ControlIcon::FolderOpen,    "Folder group",   PatternStampLayerCategory::Folder       },
        }};
        const float MenuH = 12.0f + static_cast<float>(Items.size()) * 28.0f;
        const Rectangle2D MenuR{ LeftX, ActY - MenuH - 6.0f, 172.0f, MenuH };
        Surface.FillRectangle(MenuR, ColorRgba{ 32u, 33u, 36u, 255u }, 12.0f);
        Surface.StrokeRectangle(MenuR, ColorRgba{ 255u, 255u, 255u, 28u }, 12.0f, 1.0f);
        for (std::size_t I = 0u; I < Items.size(); ++I)
        {
            const Rectangle2D ItemR{ MenuR.X + 6.0f, MenuR.Y + 6.0f + static_cast<float>(I) * 28.0f, MenuR.Width - 12.0f, 26.0f };
            if (ItemR.Contains(Cursor))
            {
                Surface.FillRectangle(ItemR, ColorRgba{ 44u, 46u, 52u, 255u }, 7.0f);
            }
            VectorIconRepository::ConstructControl(
                Surface, Items[I].Icon, VectorIconPlacement{ ItemR.X + 8.0f, ItemR.Y + 6.5f, 13.0f, Theme.TextMuted, 1.65f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { ItemR.X + 28.0f, ItemR.Y + 5.5f }, 9.0f, Theme.TextPrimary, Items[I].Label);
            if (Pressed && ItemR.Contains(Cursor))
            {
                Session->AddLayer(Items[I].Cat);
                AddLayerSheetOpen = false;
            }
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
