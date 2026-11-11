/*============================================================================================================================================
                                                     CONFIRMATIONDIALOGUE.CPP
============================================================================================================================================*/
// 🧩 Foreground modal confirmation dialogue rendering Rounded (28px) and SemanticHeader (8px) layouts above all drawers

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ConfirmationDialogue.h"
#include "Engine/Components/Controls/CapsuleButton.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 SEVERITY TONE & WRAPPING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ColorQuad ResolveSeverityTone(DialogueSeverity Severity, const ControlColorSet& Colors) noexcept
{
    switch (Severity)
    {
        case DialogueSeverity::Notice:  return Colors.Notice;
        case DialogueSeverity::Success: return Colors.Success;
        case DialogueSeverity::Warning: return Colors.Warning;
        case DialogueSeverity::Error:   return Colors.Danger;
    }
    return Colors.Notice;
}

[[nodiscard]] ControlIcon ResolveSeverityIcon(DialogueSeverity Severity) noexcept
{
    switch (Severity)
    {
        case DialogueSeverity::Notice:  return ControlIcon::CircleNotice;
        case DialogueSeverity::Success: return ControlIcon::CircleCheck;
        case DialogueSeverity::Warning: return ControlIcon::TriangleAlert;
        case DialogueSeverity::Error:   return ControlIcon::OctagonAlert;
    }
    return ControlIcon::CircleNotice;
}

[[nodiscard]] std::vector<std::string> WrapDialogueText(
    PixelSpace&        Surface,
    float              MaxWidth,
    const std::string& Text)
{
    std::vector<std::string> Lines{};
    if (Text.empty() || MaxWidth <= 16.0f)
    {
        return Lines;
    }

    std::string CurrentLine{};
    std::size_t Cursor = 0u;
    while (Cursor < Text.size() && Lines.size() < 4u)
    {
        while (Cursor < Text.size() && Text[Cursor] == ' ')
        {
            ++Cursor;
        }
        if (Cursor >= Text.size())
        {
            break;
        }
        const std::size_t NextSpace  = Text.find(' ', Cursor);
        const std::size_t WordEnd    = (NextSpace == std::string::npos) ? Text.size() : NextSpace;
        const std::string SingleWord = Text.substr(Cursor, WordEnd - Cursor);
        const std::string Candidate  = CurrentLine.empty() ? SingleWord : (CurrentLine + " " + SingleWord);

        if (Surface.MeasureTextWidth(14.5f, Candidate.c_str()) <= MaxWidth)
        {
            CurrentLine = Candidate;
            Cursor      = WordEnd;
        }
        else if (!CurrentLine.empty())
        {
            Lines.push_back(CurrentLine);
            CurrentLine.clear();
        }
        else
        {
            Lines.push_back(SingleWord);
            Cursor = WordEnd;
        }
    }

    if (!CurrentLine.empty() && Lines.size() < 4u)
    {
        Lines.push_back(CurrentLine);
    }
    return Lines;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              CONFIRMATION DIALOGUE METHODS
//------------------------------------------------------------------------------------------------------------------------

bool ConfirmationDialogue::Activate(const ConfirmationSpecification& Specification)
{
    if (Active || Specification.Title.empty() || Specification.PrimaryLabel.empty())
    {
        return false;
    }
    ActiveSpec    = Specification;
    Active        = true;
    ModalBounds   = {};
    PrimaryBounds = {};
    CancelBounds  = {};
    return true;
}

void ConfirmationDialogue::Reset() noexcept
{
    Active        = false;
    ModalBounds   = {};
    PrimaryBounds = {};
    CancelBounds  = {};
}

DialogueResponse ConfirmationDialogue::DrawModal(
    PixelSpace&           Surface,
    Rectangle2D           ViewportLogicalBounds,
    const ControlPointer& Pointer)
{
    if (!Active)
    {
        return DialogueResponse::None;
    }

    const ControlColorSet& Colors   = ControlStyle::Colors();
    const ColorQuad        Tone     = ResolveSeverityTone(ActiveSpec.Severity, Colors);
    const ControlIcon      Icon     = ResolveSeverityIcon(ActiveSpec.Severity);
    DialogueResponse       Response = DialogueResponse::None;

    // Full-viewport dim scrim above the Control Centre drawer
    Surface.FillRectangle(ViewportLogicalBounds, ColorRgba{ 0u, 0u, 0u, 140u }, 0.0f);

    const float ModalWidth  = std::clamp(ViewportLogicalBounds.Width - 32.0f, 300.0f, 440.0f);
    const float ModalHeight = 284.0f;
    ModalBounds             = {
        ViewportLogicalBounds.X + (ViewportLogicalBounds.Width - ModalWidth) * 0.5f,
        ViewportLogicalBounds.Y + (ViewportLogicalBounds.Height - ModalHeight) * 0.5f,
        ModalWidth,
        ModalHeight,
    };

    // Subtle elevation shadow beneath modal surface
    Surface.FillRectangle(
        { ModalBounds.X, ModalBounds.Y + 8.0f, ModalBounds.Width, ModalBounds.Height },
        ColorRgba{ 0u, 0u, 0u, 120u },
        ActiveSpec.Layout == DialogueLayoutStyle::Rounded ? 28.0f : 8.0f);

    if (ActiveSpec.Layout == DialogueLayoutStyle::Rounded)
    {
        Surface.FillRectangle(ModalBounds, Colors.Panel.ToRgba8(), 28.0f);
        ControlStyle::DrawRoundedOutline(Surface, ModalBounds, Colors.StrokeStrong, 28.0f);

        ColorQuad DiscWash = Tone;
        DiscWash.Alpha     = 0.14f;
        const Rectangle2D EmblemRect{ ModalBounds.X + 32.0f, ModalBounds.Y + 28.0f, 48.0f, 48.0f };
        Surface.FillRectangle(EmblemRect, DiscWash.ToRgba8(), 24.0f);
        ControlStyle::DrawIconCentered(Surface, EmblemRect, 24.0f, Tone, Icon, 2.0f);

        Surface.Text({ ModalBounds.X + 32.0f, ModalBounds.Y + 94.0f }, 22.0f, Colors.Text.ToRgba8(), ActiveSpec.Title.c_str());

        const auto Lines = WrapDialogueText(Surface, ModalBounds.Width - 64.0f, ActiveSpec.Description);
        for (std::size_t Index = 0u; Index < Lines.size(); ++Index)
        {
            Surface.Text(
                { ModalBounds.X + 32.0f, ModalBounds.Y + 132.0f + static_cast<float>(Index) * 22.0f },
                14.5f,
                Colors.TextDim.ToRgba8(),
                Lines[Index].c_str());
        }
    }
    else
    {
        Surface.FillRectangle(ModalBounds, Colors.Panel.ToRgba8(), 8.0f);

        const Rectangle2D HeaderBand{ ModalBounds.X, ModalBounds.Y, ModalBounds.Width, 96.0f };
        Surface.PushClip(ModalBounds);
        Surface.FillRectangle({ HeaderBand.X, HeaderBand.Y, HeaderBand.Width, HeaderBand.Height + 8.0f }, Tone.ToRgba8(), 8.0f);
        Surface.FillRectangle({ HeaderBand.X, HeaderBand.Bottom(), HeaderBand.Width, 12.0f }, Colors.Panel.ToRgba8(), 0.0f);

        Surface.PushClip(HeaderBand);
        ControlStyle::DrawIcon(
            Surface,
            HeaderBand.Right() - 96.0f,
            HeaderBand.Y - 12.0f,
            120.0f,
            ColorQuad{ 1.0f, 1.0f, 1.0f, 0.12f },
            Icon,
            2.4f);
        Surface.PopClip();

        Surface.Text({ HeaderBand.X + 28.0f, HeaderBand.Y + 34.0f }, 22.0f, ColorRgba{ 255u, 255u, 255u, 255u }, ActiveSpec.Title.c_str());

        const Rectangle2D CloseHit{ HeaderBand.Right() - 48.0f, HeaderBand.Y + 18.0f, 32.0f, 32.0f };
        if (ControlStyle::QueryPointerOver(CloseHit, Pointer))
        {
            Surface.FillRectangle(CloseHit, ColorRgba{ 255u, 255u, 255u, 38u }, 16.0f);
            if (Pointer.Released)
            {
                Response = DialogueResponse::Cancelled;
            }
        }
        ControlStyle::DrawIconCentered(
            Surface,
            CloseHit,
            16.0f,
            ColorQuad{ 1.0f, 1.0f, 1.0f, 0.90f },
            ControlIcon::CloseCross,
            2.0f);
        Surface.PopClip();

        ControlStyle::DrawRoundedOutline(Surface, ModalBounds, Colors.StrokeStrong, 8.0f);

        const auto Lines = WrapDialogueText(Surface, ModalBounds.Width - 56.0f, ActiveSpec.Description);
        for (std::size_t Index = 0u; Index < Lines.size(); ++Index)
        {
            Surface.Text(
                { ModalBounds.X + 28.0f, ModalBounds.Y + 120.0f + static_cast<float>(Index) * 22.0f },
                14.5f,
                Colors.TextDim.ToRgba8(),
                Lines[Index].c_str());
        }
    }

    const float ButtonPadX  = (ActiveSpec.Layout == DialogueLayoutStyle::Rounded) ? 32.0f : 28.0f;
    const float ButtonWidth = (ModalBounds.Width - ButtonPadX * 2.0f - 12.0f) * 0.5f;
    const float ButtonTopY  = ModalBounds.Bottom() - 28.0f - 40.0f;

    CancelBounds  = { ModalBounds.X + ButtonPadX, ButtonTopY, ButtonWidth, 40.0f };
    PrimaryBounds = { CancelBounds.Right() + 12.0f, ButtonTopY, ButtonWidth, 40.0f };

    const ButtonDescriptor CancelButton{ "Cancel", ButtonTone::Secondary, 14.0f, 20.0f, false, {} };
    const ButtonDescriptor PrimaryButton{ ActiveSpec.PrimaryLabel.c_str(), ButtonTone::Tinted, 14.0f, 20.0f, false, Tone };

    if (CapsuleButton::Draw(Surface, CancelBounds, CancelButton, Pointer).Clicked)
    {
        Response = DialogueResponse::Cancelled;
    }
    if (CapsuleButton::Draw(Surface, PrimaryBounds, PrimaryButton, Pointer).Clicked)
    {
        Response = DialogueResponse::Confirmed;
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            Response = DialogueResponse::Cancelled;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        {
            Response = DialogueResponse::Confirmed;
        }
    }

    if (Response != DialogueResponse::None)
    {
        Active = false;
    }
    return Response;
}

} // namespace Frontier
