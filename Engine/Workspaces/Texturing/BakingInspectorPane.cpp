/*============================================================================================================================================
                                                           BAKINGINSPECTORPANE.CPP
============================================================================================================================================*/
// 🧩 1:1 implementation of BakingInspectorPane matching Experimental/TextureStudio docked Bake Inspector (cards-bake.png):
//    Map Baking header, Study hero + dropdown, Low/High mesh cards, Bake 5 maps + Auto|GPU|CPU card, Open map (#222327) card,
//    Output & cage fold bar, 3D Volume SDF & Slice Inspector, Export & Texture Paint Bridge, and Saved locally footer.

#include "Engine/Workspaces/Texturing/BakingInspectorPane.h"

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

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

[[nodiscard]] bool PointInsideRect(Rectangle2D Rect, Point2D Cursor) noexcept
{
    return Cursor.X >= Rect.X && Cursor.X <= Rect.Right() && Cursor.Y >= Rect.Y && Cursor.Y <= Rect.Bottom();
}

void DrawBakeReadoutPill(PixelSpace& Surface, float RightEdgeX, float TopY, const char* Text) noexcept
{
    const float W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.2f, Text) + 14.0f;
    const Rectangle2D PillR{ RightEdgeX - W, TopY, W, 17.0f };
    Surface.FillRectangle(PillR, ColorRgba{ 9u, 10u, 12u, 255u }, 8.5f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { PillR.X + 7.0f, PillR.Y + 2.5f }, 8.2f, ColorRgba{ 235u, 235u, 235u, 255u }, Text);
}

bool DrawBakePillSlider(
    PixelSpace&           Surface,
    float                 X,
    float                 Y,
    float                 W,
    float&                InOutNorm,
    const ControlPointer& Pointer,
    bool&                 InOutDragging) noexcept
{
    const Rectangle2D TrackR{ X, Y + 3.0f, W, 8.0f };
    const Rectangle2D HitR  { X - 4.0f, Y - 2.0f, W + 8.0f, 18.0f };
    const Point2D     Cursor{ Pointer.X, Pointer.Y };

    bool Changed = false;
    if (Pointer.Pressed && HitR.Contains(Cursor))
    {
        InOutDragging = true;
    }
    if (InOutDragging && Pointer.Down)
    {
        const float NewNorm = std::clamp((Cursor.X - TrackR.X) / std::max(1.0f, TrackR.Width), 0.0f, 1.0f);
        if (std::abs(NewNorm - InOutNorm) > 1.0e-4f)
        {
            InOutNorm = NewNorm;
            Changed   = true;
        }
    }
    else if (!Pointer.Down)
    {
        InOutDragging = false;
    }

    Surface.FillRectangle(TrackR, ColorRgba{ 9u, 10u, 12u, 255u }, 4.0f);
    const float FillW = std::clamp(InOutNorm, 0.0f, 1.0f) * TrackR.Width;
    if (FillW > 1.0f)
    {
        Surface.FillRectangle({ TrackR.X, TrackR.Y, FillW, TrackR.Height }, ColorRgba{ 98u, 100u, 106u, 255u }, 4.0f);
    }
    const float ThumbX = std::clamp(TrackR.X + FillW - 8.0f, TrackR.X, TrackR.Right() - 16.0f);
    const Rectangle2D ThumbR{ ThumbX, Y + 2.0f, 16.0f, 10.0f };
    Surface.FillRectangle(ThumbR, ColorRgba{ 255u, 255u, 255u, 255u }, 5.0f);
    return Changed;
}

} // namespace

BakingInspectorPane::~BakingInspectorPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void BakingInspectorPane::AttachSession(BakingWorkspaceSession* TargetSession) noexcept
{
    Session        = TargetSession;
    ScrollOffsetY  = 0.0f;
    ActiveSliderId = -1;
}

void BakingInspectorPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImGuiStyle& Style = ImGui::GetStyle();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - Style.WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##BakingInspectorCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, ActiveSliderId >= 0);
}

void BakingInspectorPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Session == nullptr || Bounds.Width < 180.0f || Bounds.Height < 180.0f)
    {
        return;
    }

    const TexturingThemeColors Theme   = QueryTexturingThemeColors();
    const Point2D              Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed = Input.PrimaryPointer.Pressed;

    if (!Input.PrimaryPointer.Down)
    {
        ActiveSliderId = -1;
    }

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.LightSurface ? Theme.PaneBackground : ColorRgba{ 24u, 25u, 28u, 255u }, 0.0f);

    // 1. Top Header ("Map Baking  HIGH -> LOW CAGE" + Undo / Redo / Upload / Reset icons) — matches cards-bake.png 1:1
    constexpr float PadX       = 14.0f;
    constexpr float HeaderTopY = 12.0f;
    const float     InnerW     = std::max(80.0f, Bounds.Width - PadX * 2.0f);
    const float     Hx         = Bounds.X + PadX;
    const float     Hy         = Bounds.Y + HeaderTopY;

    Surface.FillRectangle({ Hx, Hy + 1.0f, 22.0f, 22.0f }, ColorRgba{ 20u, 42u, 26u, 255u }, 6.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::LayersSlabs, VectorIconPlacement{ Hx + 4.5f, Hy + 5.5f, 13.0f, ColorRgba{ 52u, 199u, 89u, 255u }, 1.7f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { Hx + 28.0f, Hy - 1.0f }, 10.6f, Theme.TextPrimary, "Map Baking");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { Hx + 28.0f, Hy + 12.0f }, 7.0f, Theme.TextFaint, "HIGH -> LOW CAGE");

    static constexpr std::array<ControlIcon, 4u> HdrIcons {{
        ControlIcon::UndoArrow,
        ControlIcon::RedoArrow,
        ControlIcon::FolderOpen,
        ControlIcon::RefreshCycle,
    }};
    for (std::size_t I = 0u; I < HdrIcons.size(); ++I)
    {
        const Rectangle2D BtnR{ Bounds.Right() - PadX - static_cast<float>(4u - I) * 26.0f + 2.0f, Hy, 24.0f, 24.0f };
        if (ConstructHeaderIconButton(Surface, BtnR, HdrIcons[I], false, true, Theme, Cursor, Pressed) && I == 3u)
        {
            Session->ApplyQuickPreset(BakerQuickPreset::PbrPlusVolume);
        }
    }

    // 2. Bottom Footer ("● Saved locally   Exports live in the header") — matches cards-bake.png 1:1
    constexpr float   FooterH = 26.0f;
    const Rectangle2D FooterBounds{ Bounds.X + PadX, Bounds.Bottom() - FooterH, InnerW, FooterH };
    Surface.FillCircle({ FooterBounds.X + 4.0f, FooterBounds.Y + 11.0f }, 3.2f, ColorRgba{ 52u, 199u, 89u, 255u });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { FooterBounds.X + 12.0f, FooterBounds.Y + 5.5f }, 8.2f, Theme.TextFaint, "Saved locally");
    const char* RightFoot = "Exports live in the header";
    const float RightFootW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, RightFoot);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { FooterBounds.Right() - RightFootW, FooterBounds.Y + 5.5f }, 8.2f, Theme.TextFaint, RightFoot);

    // 3. Scrollable region
    const float       ScrollTop = Bounds.Y + HeaderTopY + 30.0f;
    const Rectangle2D ScrollViewport{
        Bounds.X,
        ScrollTop,
        Bounds.Width,
        std::max(1.0f, FooterBounds.Y - ScrollTop),
    };

    if (PointInsideRect(ScrollViewport, Cursor) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetY = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScrollY);
    }

    Surface.PushClip(ScrollViewport);
    const float LeftX = ScrollViewport.X + PadX;
    float       CurY  = ScrollViewport.Y + 4.0f - ScrollOffsetY;

    const BakerStudyDescriptor& StudyDesc = Session->QueryActiveStudyDescriptor();
    const char* StudyTitle = !Session->CustomLowAssetLabel.empty() ? Session->CustomLowAssetLabel.c_str() : StudyDesc.Label;

    // 3A. Study Hero ("STANDARD STUDY / 01", "Utah Teapot", "6.4k tris · Sub-D Tri · Curvature & AO" + "Utah Teapot v" pill)
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { LeftX, CurY }, 7.6f, Theme.TextFaint, "STANDARD STUDY / 01");
    CurY += 13.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Light, { LeftX, CurY }, 17.5f, Theme.TextPrimary, StudyTitle);
    CurY += 22.0f;

    const Rectangle2D StudyDropR{ LeftX + InnerW - 102.0f, CurY - 3.0f, 102.0f, 22.0f };
    Surface.FillRectangle(StudyDropR, ColorRgba{ 9u, 10u, 12u, 255u }, 11.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { StudyDropR.X + 10.0f, StudyDropR.Y + 4.5f }, StudyDropR.Width - 24.0f, 8.4f, Theme.TextPrimary, StudyTitle);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ChevronDown, VectorIconPlacement{ StudyDropR.Right() - 14.0f, StudyDropR.Y + 7.0f, 8.5f, Theme.TextMuted, 1.6f });
    if (Pressed && StudyDropR.Contains(Cursor))
    {
        const uint32_t NextStudy = (static_cast<uint32_t>(Session->Study) + 1u) % 4u;
        Session->SelectStudy(static_cast<BakerStudyShape>(NextStudy));
    }

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, CurY + 1.0f }, InnerW - 110.0f, 8.6f, Theme.TextMuted,
        "6.4k tris · Sub-D Tri · Curvature & AO");
    CurY += 26.0f;

    // 3B. Two Side-by-Side Mesh Source Cards ("Low (Study mesh)" & "High (Study mesh)") — matches cards-bake.png 1:1
    {
        const float CardGap = 8.0f;
        const float CardW   = (InnerW - CardGap) * 0.5f;
        for (int M = 0; M < 2; ++M)
        {
            const Rectangle2D MeshCardR{ LeftX + static_cast<float>(M) * (CardW + CardGap), CurY, CardW, 48.0f };
            Surface.FillRectangle(MeshCardR, ColorRgba{ 28u, 29u, 33u, 255u }, 10.0f);
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::CubeShape, VectorIconPlacement{ MeshCardR.X + 10.0f, MeshCardR.Y + 11.0f, 12.0f, Theme.TextMuted, 1.55f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { MeshCardR.X + 26.0f, MeshCardR.Y + 7.0f }, 8.6f, Theme.TextPrimary,
                (M == 0) ? "Low (Study mesh)" : "High (Study mesh)");
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { MeshCardR.X + 26.0f, MeshCardR.Y + 21.0f }, CardW - 32.0f, 7.6f, Theme.TextFaint,
                "Utah Teapot · 6.4k tris · Sub-D");
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { MeshCardR.X + 26.0f, MeshCardR.Y + 32.0f }, CardW - 32.0f, 7.6f, Theme.TextFaint,
                "Tri · Drop .obj / .stl");
        }
        CurY += 56.0f;
    }

    // 3C. Bake Action & Progress Card ("|> Bake 5 maps" + "Auto | GPU | CPU" + telemetry + progress bar) — matches cards-bake.png 1:1
    {
        const Rectangle2D ProgBox{ LeftX, CurY, InnerW, 92.0f };
        Surface.FillRectangle(ProgBox, ColorRgba{ 28u, 29u, 33u, 255u }, 12.0f);

        char BakeBtnLabel[48];
        std::snprintf(BakeBtnLabel, sizeof(BakeBtnLabel), Session->BakingActive ? "Baking..." : "Bake %zu maps", Session->CountSelectedTargets());
        const float BakeBtnW = (InnerW - 24.0f) * 0.56f;
        const Rectangle2D BakeBtnRect{ LeftX + 10.0f, CurY + 10.0f, BakeBtnW, 28.0f };
        const bool BakeHov = BakeBtnRect.Contains(Cursor);
        Surface.FillRectangle(BakeBtnRect, BakeHov ? ColorRgba{ 245u, 245u, 245u, 255u } : ColorRgba{ 255u, 255u, 255u, 255u }, 14.0f);
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::BakeBox, VectorIconPlacement{ BakeBtnRect.X + 36.0f, BakeBtnRect.Y + 8.5f, 11.0f, ColorRgba{ 17u, 17u, 17u, 255u }, 1.8f });
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { BakeBtnRect.X + 52.0f, BakeBtnRect.Y + 7.0f }, 9.2f, ColorRgba{ 17u, 17u, 17u, 255u }, BakeBtnLabel);
        if (Pressed && BakeHov)
        {
            Session->ExecuteBakeSimulation();
        }

        // "Auto | GPU | CPU" segmented pill container
        const float SegX = BakeBtnRect.Right() + 8.0f;
        const float SegW = ProgBox.Right() - 10.0f - SegX;
        const Rectangle2D SegTrackR{ SegX, CurY + 10.0f, SegW, 28.0f };
        Surface.FillRectangle(SegTrackR, ColorRgba{ 9u, 10u, 12u, 255u }, 14.0f);
        static constexpr const char* ShortDev[3] { "Auto", "GPU", "CPU" };
        const float DevPillW = (SegW - 4.0f) / 3.0f;
        for (uint32_t Di = 0u; Di < 3u; ++Di)
        {
            const Rectangle2D DevR{ SegTrackR.X + 2.0f + static_cast<float>(Di) * DevPillW, SegTrackR.Y + 2.0f, DevPillW, 24.0f };
            const bool DevAct = (static_cast<uint32_t>(Session->ComputeDevice) == Di);
            if (DevAct)
            {
                Surface.FillRectangle(DevR, ColorRgba{ 255u, 255u, 255u, 255u }, 12.0f);
            }
            const float Tw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 8.0f, ShortDev[Di]);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, DevAct ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                { DevR.X + (DevR.Width - Tw) * 0.5f, DevR.Y + 5.5f }, 8.0f,
                DevAct ? ColorRgba{ 17u, 17u, 17u, 255u } : Theme.TextMuted, ShortDev[Di]);
            if (Pressed && DevR.Contains(Cursor))
            {
                Session->ComputeDevice = static_cast<BakerComputeDevice>(Di);
            }
        }

        // Middle stats row ("4.2 MB output  6.7 M rays  ~ 141 ms on GPU" + "UNBAKED" amber pill)
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + 10.0f, CurY + 45.0f }, InnerW - 88.0f, 8.2f, Theme.TextMuted,
            "4.2 MB output   6.7 M rays   ~ 141 ms on GPU");

        const bool BakedDone = Session->SurfaceBaked;
        const char* StateBadge = BakedDone ? "BAKED" : "UNBAKED";
        const float BadgeW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Bold, 7.4f, StateBadge) + 14.0f;
        const Rectangle2D BadgeR{ ProgBox.Right() - 10.0f - BadgeW, CurY + 43.0f, BadgeW, 17.0f };
        Surface.FillRectangle(BadgeR, BakedDone ? ColorRgba{ 20u, 46u, 28u, 255u } : ColorRgba{ 46u, 34u, 18u, 255u }, 8.5f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Bold, { BadgeR.X + 7.0f, BadgeR.Y + 3.0f }, 7.4f,
            BakedDone ? ColorRgba{ 52u, 199u, 89u, 255u } : ColorRgba{ 240u, 164u, 58u, 255u }, StateBadge);

        // Progress bar + bottom status row
        const Rectangle2D TrackRect{ LeftX + 10.0f, CurY + 65.0f, InnerW - 20.0f, 4.0f };
        Surface.FillRectangle(TrackRect, ColorRgba{ 15u, 16u, 19u, 255u }, 2.0f);
        const float ClampedProg = std::clamp(Session->BakeProgress, 0.0f, 1.0f);
        if (ClampedProg > 0.005f)
        {
            Surface.FillRectangle({ TrackRect.X, TrackRect.Y, std::max(6.0f, TrackRect.Width * ClampedProg), TrackRect.Height }, ColorRgba{ 52u, 199u, 89u, 255u }, 2.0f);
        }

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX + 10.0f, CurY + 73.0f }, 8.0f, Theme.TextFaint,
            Session->SurfaceBaked ? "Bake complete" : "Ready");
        char MapResNote[48];
        std::snprintf(MapResNote, sizeof(MapResNote), "%zu maps · %u px", Session->CountSelectedTargets(), Session->TargetResolutionPx);
        const float NoteW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.0f, MapResNote);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { ProgBox.Right() - 10.0f - NoteW, CurY + 73.0f }, 8.0f, Theme.TextFaint, MapResNote);

        CurY += ProgBox.Height + 12.0f;
    }

    // 3D. "Open map   PICK IN THE QUEUE" Section + #222327 Card — matches cards-bake.png 1:1
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX, CurY }, 9.2f, Theme.TextMuted, "Open map");
        const char* PickRight = "PICK IN THE QUEUE";
        const float PickW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Bold, 7.6f, PickRight);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Bold, { LeftX + InnerW - PickW, CurY + 1.5f }, 7.6f, Theme.TextFaint, PickRight);
        CurY += 18.0f;

        const auto& Desc = BakingWorkspaceSession::QueryTargetDescriptor(Session->InspectedTarget);
        const std::size_t TargetIdx = static_cast<std::size_t>(Session->InspectedTarget);
        const bool IsQueued = Session->SelectedTargets[TargetIdx];

        const Rectangle2D OpenMapR{ LeftX, CurY, InnerW, 166.0f };
        Surface.FillRectangle(OpenMapR, ColorRgba{ 34u, 35u, 39u, 255u }, 12.0f);

        // Card header: green dot + title + subtitle + "1.0 MB · 377 k" pill + white/dark switch
        Surface.FillCircle({ LeftX + 14.0f, CurY + 18.0f }, 3.5f, IsQueued ? ColorRgba{ 52u, 199u, 89u, 255u } : Theme.TextFaint);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { LeftX + 24.0f, CurY + 10.0f }, 9.8f, Theme.TextPrimary, Desc.Label);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX + 24.0f, CurY + 23.0f }, 8.0f, Theme.TextMuted, Desc.ShortSummary);

        // Toggle switch on right
        const Rectangle2D SwR{ OpenMapR.Right() - 40.0f, CurY + 13.0f, 28.0f, 16.0f };
        Surface.FillRectangle(SwR, IsQueued ? ColorRgba{ 255u, 255u, 255u, 255u } : ColorRgba{ 14u, 15u, 18u, 255u }, 8.0f);
        Surface.FillCircle(
            { IsQueued ? (SwR.Right() - 8.0f) : (SwR.X + 8.0f), SwR.Y + 8.0f },
            6.0f,
            IsQueued ? ColorRgba{ 17u, 17u, 17u, 255u } : ColorRgba{ 118u, 120u, 126u, 255u });
        if (Pressed && SwR.Contains(Cursor))
        {
            Session->ToggleTargetSelected(Session->InspectedTarget);
        }

        DrawBakeReadoutPill(Surface, SwR.X - 8.0f, CurY + 12.5f, "1.0 MB · 377 k");

        // Full-width "OpenGL +Y | DirectX -Y" segmented pill bar
        const Rectangle2D NormTrackR{ LeftX + 12.0f, CurY + 42.0f, InnerW - 24.0f, 26.0f };
        Surface.FillRectangle(NormTrackR, ColorRgba{ 18u, 18u, 20u, 255u }, 13.0f);
        static constexpr const char* NormShort[2] { "OpenGL +Y", "DirectX -Y" };
        const float HalfNormW = (NormTrackR.Width - 4.0f) * 0.5f;
        for (uint32_t Ni = 0u; Ni < 2u; ++Ni)
        {
            const Rectangle2D NormR{ NormTrackR.X + 2.0f + static_cast<float>(Ni) * HalfNormW, NormTrackR.Y + 2.0f, HalfNormW, 22.0f };
            const bool NAct = (static_cast<uint32_t>(Session->NormalConvention) == Ni);
            if (NAct)
            {
                Surface.FillRectangle(NormR, ColorRgba{ 255u, 255u, 255u, 255u }, 11.0f);
            }
            const float Tw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 8.2f, NormShort[Ni]);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, NAct ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                { NormR.X + (NormR.Width - Tw) * 0.5f, NormR.Y + 4.5f }, 8.2f,
                NAct ? ColorRgba{ 17u, 17u, 17u, 255u } : Theme.TextMuted, NormShort[Ni]);
            if (Pressed && NormR.Contains(Cursor))
            {
                Session->NormalConvention = static_cast<TangentNormalConvention>(Ni);
            }
        }

        // Two side-by-side slider columns: Cage front & Cage rear
        const float HalfCageW = (InnerW - 24.0f - 12.0f) * 0.5f;
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { LeftX + 12.0f, CurY + 76.0f }, 8.4f, Theme.TextMuted, "Cage front");
            char CfBuf[24];
            std::snprintf(CfBuf, sizeof(CfBuf), "%.3f", Session->FrontCageOffsetMeters);
            DrawBakeReadoutPill(Surface, LeftX + 12.0f + HalfCageW, CurY + 74.0f, CfBuf);
            bool DragCf = (ActiveSliderId == 191);
            float CfNorm = std::clamp((Session->FrontCageOffsetMeters - 0.005f) / 0.25f, 0.0f, 1.0f);
            if (DrawBakePillSlider(Surface, LeftX + 12.0f, CurY + 95.0f, HalfCageW, CfNorm, Input.PrimaryPointer, DragCf))
            {
                Session->FrontCageOffsetMeters = 0.005f + CfNorm * 0.25f;
            }
            if (DragCf) ActiveSliderId = 191;
        }
        {
            const float RightColX = LeftX + 12.0f + HalfCageW + 12.0f;
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { RightColX, CurY + 76.0f }, 8.4f, Theme.TextMuted, "Cage rear");
            char CrBuf[24];
            std::snprintf(CrBuf, sizeof(CrBuf), "%.3f", Session->RearCageOffsetMeters);
            DrawBakeReadoutPill(Surface, RightColX + HalfCageW, CurY + 74.0f, CrBuf);
            bool DragCr = (ActiveSliderId == 192);
            float CrNorm = std::clamp((Session->RearCageOffsetMeters - 0.005f) / 0.25f, 0.0f, 1.0f);
            if (DrawBakePillSlider(Surface, RightColX, CurY + 95.0f, HalfCageW, CrNorm, Input.PrimaryPointer, DragCr))
            {
                Session->RearCageOffsetMeters = 0.005f + CrNorm * 0.25f;
            }
            if (DragCr) ActiveSliderId = 192;
        }

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX + 12.0f, CurY + 120.0f }, 8.0f, Theme.TextFaint,
            "Also drives Height / Displacement.");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX + 12.0f, CurY + 142.0f }, 8.0f, Theme.TextFaint,
            "normalTangent · 2 rays/texel");
        const char* FootRight = "1.0 MB · 377 k rays";
        const float FootRightW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.0f, FootRight);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { OpenMapR.Right() - 12.0f - FootRightW, CurY + 142.0f }, 8.0f, Theme.TextFaint, FootRight);

        CurY += OpenMapR.Height + 10.0f;
    }

    // 4. "Output & cage   512 PX · PAD 6 · OPENGL   >" Fold Bar — matches cards-bake.png 1:1
    {
        const Rectangle2D FoldR{ LeftX, CurY, InnerW, 34.0f };
        Surface.FillRectangle(FoldR, ColorRgba{ 28u, 29u, 33u, 255u }, 10.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { FoldR.X + 12.0f, FoldR.Y + 9.5f }, 9.2f, Theme.TextPrimary, "Output & cage");

        char OutMeta[64];
        std::snprintf(
            OutMeta,
            sizeof(OutMeta),
            "%u PX · PAD %d · %s",
            Session->TargetResolutionPx,
            Session->UvEdgePaddingPx,
            (static_cast<uint32_t>(Session->NormalConvention) == 0u) ? "OPENGL" : "DIRECTX");
        const float MetaW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Bold, 7.6f, OutMeta);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Bold, { FoldR.Right() - 26.0f - MetaW, FoldR.Y + 11.0f }, 7.6f, Theme.TextFaint, OutMeta);
        VectorIconRepository::ConstructControl(
            Surface,
            SurfaceSectionOpen ? ControlIcon::ChevronDown : ControlIcon::ChevronForward,
            VectorIconPlacement{ FoldR.Right() - 18.0f, FoldR.Y + 12.5f, 9.0f, Theme.TextFaint, 1.6f });
        if (Pressed && FoldR.Contains(Cursor))
        {
            SurfaceSectionOpen = !SurfaceSectionOpen;
        }
        CurY += 42.0f;
    }

    // Interactive slider helper for lower sections (Volume SDF & Export Bridge, visible when scrolled down in Exhibit 10)
    auto RenderFloatSlider = [&](int32_t SliderKey, const char* Label, float MinVal, float MaxVal, const char* UnitText, const char* Fmt, float& InOutScalar)
    {
        float Norm = std::clamp((InOutScalar - MinVal) / std::max(0.0001f, MaxVal - MinVal), 0.0f, 1.0f);
        bool  Drag = (ActiveSliderId == SliderKey);
        char  NumBuf[32];
        std::snprintf(NumBuf, sizeof(NumBuf), Fmt, InOutScalar);
        if (ConstructSliderReadoutRow(Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, Label, NumBuf, UnitText, Norm, Theme, Input.PrimaryPointer, Drag))
        {
            InOutScalar = std::clamp(MinVal + Norm * (MaxVal - MinVal), MinVal, MaxVal);
        }
        if (Drag)
        {
            ActiveSliderId = SliderKey;
        }
        CurY += 52.0f;
    };

    // 5. Section 2: 3D Volume SDF & Slice Inspector
    {
        const float       SecStartY = CurY;
        const float       SecBodyH  = VolumeSectionOpen ? (40.0f + 56.0f * 2.0f + 52.0f * 2.0f + 36.0f) : 38.0f;
        const Rectangle2D SecRect{ LeftX, SecStartY, InnerW, SecBodyH };
        Surface.FillRectangle(SecRect, ColorRgba{ 28u, 29u, 33u, 255u }, 12.0f);

        if (ConstructSectionHeaderRow(
                Surface,
                { LeftX + 12.0f, CurY + 10.0f, InnerW - 24.0f, 22.0f },
                ControlIcon::VolumeBoxes,
                "3D Volume SDF & Slice Inspector",
                VolumeSectionOpen ? "-" : "+",
                VolumeSectionOpen,
                Theme,
                Cursor,
                Pressed))
        {
            VolumeSectionOpen = !VolumeSectionOpen;
        }
        CurY += 38.0f;

        if (VolumeSectionOpen)
        {
            static constexpr const char* DimOpts[4] { "24³", "36³", "48³", "64³" };
            static constexpr uint32_t    DimVals[4] { 24u, 36u, 48u, 64u };
            uint32_t ActiveDimIdx = 1u;
            for (uint32_t I = 0u; I < 4u; ++I)
            {
                if (DimVals[I] == Session->VolumeGridDim) ActiveDimIdx = I;
            }
            uint32_t ChosenDimIdx = ActiveDimIdx;
            if (ConstructSegmentedCapsuleRow(Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, "Voxel Grid Dim", DimOpts, 4u, ActiveDimIdx, ChosenDimIdx, Theme, Input.PrimaryPointer))
            {
                Session->AssignVolumeGridDim(DimVals[ChosenDimIdx]);
            }
            CurY += 56.0f;

            const bool PrevSigned = Session->SignedInteriorDistance;
            ConstructSwitchControlRow(
                Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, "Signed Interior Distance", Session->SignedInteriorDistance, Theme, Input.PrimaryPointer);
            if (Session->SignedInteriorDistance != PrevSigned)
            {
                Session->RefreshSdfSectionOnGpu();
            }
            CurY += 36.0f;

            RenderFloatSlider(201, "Bounds Padding", 0.05f, 0.45f, "", "%.2f", Session->VolumeBoundsPadding);

            static constexpr const char* AxisOpts[3] { "Z Slice", "Y Slice", "X Slice" };
            uint32_t ChosenAxis = static_cast<uint32_t>(Session->SectionAxis);
            if (ConstructSegmentedCapsuleRow(Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, "Inspect Slice Axis", AxisOpts, 3u, ChosenAxis, ChosenAxis, Theme, Input.PrimaryPointer))
            {
                Session->SectionAxis = static_cast<VolumeSectionAxis>(ChosenAxis);
                Session->InspectTarget(BakerTargetId::VolumeSdf);
                Session->RefreshSdfSectionOnGpu();
            }
            CurY += 56.0f;

            const float PrevDepth = Session->SectionDepthNormalized;
            RenderFloatSlider(202, "Slice Depth", 0.0f, 1.0f, "", "%.2f", Session->SectionDepthNormalized);
            if (std::abs(Session->SectionDepthNormalized - PrevDepth) > 0.0005f)
            {
                Session->RefreshSdfSectionOnGpu();
            }
        }
        CurY += 12.0f;
    }

    // 6. Section 3: Export & Texture Paint Bridge
    {
        const float       SecStartY = CurY;
        const float       SecBodyH  = ExportSectionOpen ? 120.0f : 38.0f;
        const Rectangle2D SecRect{ LeftX, SecStartY, InnerW, SecBodyH };
        Surface.FillRectangle(SecRect, ColorRgba{ 28u, 29u, 33u, 255u }, 12.0f);

        if (ConstructSectionHeaderRow(
                Surface,
                { LeftX + 12.0f, CurY + 10.0f, InnerW - 24.0f, 22.0f },
                ControlIcon::DownloadExport,
                "Export & Texture Paint Bridge",
                ExportSectionOpen ? "-" : "+",
                ExportSectionOpen,
                Theme,
                Cursor,
                Pressed))
        {
            ExportSectionOpen = !ExportSectionOpen;
        }
        CurY += 38.0f;

        if (ExportSectionOpen)
        {
            const float HalfBtnW = (InnerW - 24.0f - 8.0f) * 0.5f;
            if (ConstructCapsuleButtonControl(
                    Surface, { LeftX + 12.0f, CurY, HalfBtnW, 28.0f }, "Download Current PNG", Theme, Cursor, Pressed, false, false, ControlIcon::DownloadExport, 9.5f, 14.0f))
            {
                (void)Session->ExportInspectedTargetPng();
            }

            if (ConstructCapsuleButtonControl(
                    Surface, { LeftX + 12.0f + HalfBtnW + 8.0f, CurY, HalfBtnW, 28.0f }, "Download .fvol SDF", Theme, Cursor, Pressed && Session->VolumeSdfBaked, Session->VolumeSdfBaked, false, ControlIcon::VolumeBoxes, 9.5f, 14.0f))
            {
                (void)Session->ExportVolumeSdfFvol();
            }
            CurY += 36.0f;

            if (ConstructCapsuleButtonControl(
                    Surface, { LeftX + 12.0f, CurY, InnerW - 24.0f, 30.0f }, "Send Baked AO/Curv/Normals to Texture Paint", Theme, Cursor, Pressed && Session->SurfaceBaked, Session->BridgedToPaint, false, ControlIcon::BrushTool, 9.5f, 15.0f))
            {
                Session->SendBakedTargetsToPaint();
            }
            CurY += 42.0f;
        }
        CurY += 12.0f;
    }

    const float ContentBottom = CurY + ScrollOffsetY + 12.0f;
    MaxScrollY                = std::max(0.0f, ContentBottom - ScrollViewport.Bottom());
    ScrollOffsetY             = std::clamp(ScrollOffsetY, 0.0f, MaxScrollY);

    Surface.PopClip();
    Surface.PopClip();
}

} // namespace Frontier::Workspaces
