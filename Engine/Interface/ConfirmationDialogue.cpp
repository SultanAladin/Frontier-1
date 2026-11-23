/*==========================================================================================================================================
                                                        CONFIRMATIONDIALOGUE.CPP
==========================================================================================================================================*/
// 🧩 Supplied rounded and semantic-header dialogue variants with native modal input confinement and safe cancellation.

#include "ConfirmationDialogue.h"
#include <algorithm>
#include <imgui.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONFIRMATION LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

bool ConfirmationDialogue::Activate(const ConfirmationSpecification &Specification)
{
    if (Open || Specification.Title.empty() || Specification.PrimaryLabel.empty() ||
        Specification.Category > NotificationCategory::Error || Specification.Title.size() > 160 ||
        Specification.Description.size() > 2048 || Specification.PrimaryLabel.size() > 40 ||
        Specification.Presentation > ConfirmationPresentationCategory::SemanticHeader)
        return false;
    Profile = Specification;
    Open = Opening  = true;
    KeyboardPrimary = false;
    return true;
}

void ConfirmationDialogue::Reset() noexcept
{
    Open = Opening = false;
    Extent = PrimaryExtent = CancelExtent = {};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODAL PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

ConfirmationResponseCategory ConfirmationDialogue::Construct(float InterfaceScale)
{
    if (!Open)
        return ConfirmationResponseCategory::None;
    const float Scale    = std::clamp(InterfaceScale, 0.75f, 1.5f);
    const auto &IO       = ImGui::GetIO();
    const auto &Palette  = ControlKit::Palette();
    const auto *Viewport = ImGui::GetMainViewport();
    const float Width    = std::max(160.0f, std::min(440.0f, IO.DisplaySize.x / Scale - 32));
    const float Height   = std::max(160.0f, std::min(284.0f, IO.DisplaySize.y / Scale - 32));
    if (Opening)
    {
        ImGui::OpenPopup("Confirm changes##Frontier");
        Opening = false;
    }
    ImGui::SetNextWindowPos(ImVec2(Viewport->Pos.x + (IO.DisplaySize.x - Width * Scale) / 2,
                                   Viewport->Pos.y + (IO.DisplaySize.y - Height * Scale) / 2),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(Width * Scale, Height * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,
                        (Profile.Presentation == ConfirmationPresentationCategory::Rounded ? 28 : 8) * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(Palette.Panel.Red, Palette.Panel.Green, Palette.Panel.Blue, 1));
    ConfirmationResponseCategory Response = ConfirmationResponseCategory::None;
    if (ImGui::BeginPopupModal("Confirm changes##Frontier", nullptr,
                               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings))
    {
        const auto Position = ImGui::GetWindowPos();
        const auto Bounds   = Spanning(Position.x / Scale, Position.y / Scale, Width, Height);
        Extent              = Spanning(Position.x, Position.y, Width * Scale, Height * Scale);
        PixelSpace Surface;
        (void)Surface.Begin(SurfaceLayer::Window, IO.DisplaySize.x, IO.DisplaySize.y, Scale);
        const ColorQuad Colours[] = {Palette.Info, Palette.Ok, Palette.Warning, Palette.Danger};
        const auto      Tone      = Colours[static_cast<size_t>(Profile.Category)];
        const bool      Header    = Profile.Presentation == ConfirmationPresentationCategory::SemanticHeader;
        const ColorQuad White{1, 1, 1, 1};
        if (Header)
        {
            Surface.PushClip(Spanning(Bounds.MinimumX, Bounds.MinimumY, Width, Height - 88));
            Surface.FillRectangle(Bounds, Tone, 8);
            Surface.PopClip();
            auto Wash  = White;
            Wash.Alpha = .10f;
            ControlKit::GlyphCentred(
                Surface, Spanning(Bounds.MaximumX - 150, Bounds.MinimumY + 10, 130, 130), 120, Wash,
                Profile.Category == NotificationCategory::Error ? ControlCentreIconCategory::CloseCross
                                                                : ControlCentreIconCategory::CircleInfo);
            Surface.PushClip(Spanning(Bounds.MinimumX + 24, Bounds.MinimumY + 22, Width - 76, 32));
            ControlKit::TextLeading(Surface, Spanning(Bounds.MinimumX + 24, Bounds.MinimumY + 22, Width - 76, 32), 0,
                                    White, Profile.Title.c_str(), 22);
            Surface.PopClip();
            Surface.FillRectangle(Spanning(Bounds.MinimumX, Bounds.MinimumY + 68, Width, 1), Wash);
        }
        else
        {
            const auto Disc = Spanning(Bounds.MinimumX + Width / 2 - 24, Bounds.MinimumY + 28, 48, 48);
            auto       Wash = Tone;
            Wash.Alpha      = .14f;
            Surface.FillRectangle(Disc, Wash, 24);
            ControlKit::GlyphCentred(Surface, Disc, 26, Tone,
                                     Profile.Category == NotificationCategory::Warning
                                         ? ControlCentreIconCategory::TriangleAlert
                                         : ControlCentreIconCategory::CircleInfo);
            Surface.PushClip(Spanning(Bounds.MinimumX + 20, Bounds.MinimumY + 92, Width - 40, 28));
            ControlKit::TextCentred(Surface, Spanning(Bounds.MinimumX + 20, Bounds.MinimumY + 92, Width - 40, 28),
                                    Palette.Text, Profile.Title.c_str(), Width < 320 ? 18 : 22);
            Surface.PopClip();
        }
        const float DescriptionY = Header ? 88.0f : 130.0f;
        ImGui::SetCursorPos(ImVec2(28 * Scale, DescriptionY * Scale));
        ImGui::BeginChild(
            "Confirmation details",
            ImVec2((Width - 56) * Scale, std::max(16.0f, Height - (Header ? 100 : 80) - DescriptionY) * Scale),
            ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
        ImGui::PushTextWrapPos(0);
        const auto TextColour = Header ? White : Palette.TextDim;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(TextColour.Red, TextColour.Green, TextColour.Blue, 1));
        ImGui::TextUnformatted(Profile.Description.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        if (Header)
        {
            const auto Close = Spanning(Bounds.MaximumX - 48, Bounds.MinimumY + 18, 32, 32);
            ImGui::SetCursorScreenPos(ImVec2(Close.MinimumX * Scale, Close.MinimumY * Scale));
            if (ImGui::InvisibleButton("Close", ImVec2(32 * Scale, 32 * Scale), ImGuiButtonFlags_EnableNav))
                Response = ConfirmationResponseCategory::Cancelled;
            ControlKit::GlyphCentred(Surface, Close, 18, White, ControlCentreIconCategory::CloseCross);
        }
        const float ButtonWidth = (Width - 68) / 2;
        const auto  Cancel      = Spanning(Bounds.MinimumX + 28, Bounds.MaximumY - 68, ButtonWidth, 40);
        const auto  Primary     = Spanning(Cancel.MaximumX + 12, Cancel.MinimumY, ButtonWidth, 40);
        CancelExtent = Spanning(Cancel.MinimumX * Scale, Cancel.MinimumY * Scale, Cancel.Width() * Scale, 40 * Scale);
        PrimaryExtent =
            Spanning(Primary.MinimumX * Scale, Primary.MinimumY * Scale, Primary.Width() * Scale, 40 * Scale);
        const bool Navigate = ImGui::IsKeyPressed(ImGuiKey_Tab, false);
        if (Navigate)
            KeyboardPrimary = !KeyboardPrimary;
        const auto ConstructButton = [&](const char *Label, const PlaneExtent &Button, bool PrimaryEnabled)
        {
            ImGui::SetCursorScreenPos(ImVec2(Button.MinimumX * Scale, Button.MinimumY * Scale));
            if (Navigate && PrimaryEnabled == KeyboardPrimary)
                ImGui::SetKeyboardFocusHere();
            const bool Activated = ImGui::InvisibleButton(
                Label, ImVec2(Button.Width() * Scale, Button.Height() * Scale), ImGuiButtonFlags_EnableNav);
            ControlPointer Pointer;
            Pointer.X       = IO.MousePos.x / Scale;
            Pointer.Y       = IO.MousePos.y / Scale;
            Pointer.Enabled = true;
            ButtonStructure Appearance{Label,
                                       PrimaryEnabled ? ButtonToneCategory::Tinted : ButtonToneCategory::Secondary};
            Appearance.Tint = Tone;
            (void)ControlKit::PillButton(Surface, Button, Appearance, Pointer);
            if (ImGui::IsItemFocused())
                ControlKit::OutlineRounded(Surface, Button, Palette.Accent, 20, 2);
            return Activated;
        };
        if (ConstructButton("Cancel", Cancel, false))
            Response = ConfirmationResponseCategory::Cancelled;
        // 📝 Default keyboard focus is the non-mutating action; Enter never silently approves a fresh popup.
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere(-1);
        if (ConstructButton(Profile.PrimaryLabel.c_str(), Primary, true))
            Response = ConfirmationResponseCategory::Confirmed;
        if (Response == ConfirmationResponseCategory::None &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
            Response =
                KeyboardPrimary ? ConfirmationResponseCategory::Confirmed : ConfirmationResponseCategory::Cancelled;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            Response = ConfirmationResponseCategory::Cancelled;
        if (Response != ConfirmationResponseCategory::None)
        {
            ImGui::CloseCurrentPopup();
            Open = false;
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return Response;
}

} // namespace Frontier
