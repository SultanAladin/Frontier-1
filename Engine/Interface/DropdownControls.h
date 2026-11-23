/*============================================================================================================================================
                                                             DROPDOWNCONTROLS.H
============================================================================================================================================*/
// 🧩 One themed rounded and padded native selection/action dropdown surface with retained ImGui input and focus semantics.

#pragma once
#include "ControlKit.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace Frontier
{
class DropdownControlSurface
{
public:
    static constexpr float CornerRadius = ControlKitTokens::DropdownCornerRadius;
    static constexpr float RowRadius    = ControlKitTokens::DropdownRowRadius;
    static constexpr float PaddingX     = ControlKitTokens::DropdownPaddingX;
    static constexpr float PaddingY     = ControlKitTokens::DropdownPaddingY;
    static constexpr float RowHeight    = ControlKitTokens::DropdownRowHeight;

    static bool BeginSelection(const char *Caption, const char *Preview, float Scale = 1)
    {
        PushSurface(Scale);
        if (ImGui::BeginCombo(Caption, Preview)) return true;
        PopSurface();
        return false;
    }

    static void EndSelection()
    {
        ImGui::EndCombo();
        PopSurface();
    }

    static bool BeginMenu(const char *Caption, float Scale = 1)
    {
        PushSurface(Scale);
        if (ImGui::BeginPopup(Caption)) return true;
        PopSurface();
        return false;
    }

    static bool BeginContextMenu(const char *Caption, float Scale = 1)
    {
        PushSurface(Scale);
        if (ImGui::BeginPopupContextWindow(Caption)) return true;
        PopSurface();
        return false;
    }

    static void EndMenu()
    {
        ImGui::EndPopup();
        PopSurface();
    }

    static bool Option(
        const char *Caption,
        bool        Selected = false,
        float       Scale = 1,
        bool        Enabled = true)
    {
        ImGui::BeginDisabled(!Enabled);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        const float Width = std::max(ImGui::GetContentRegionAvail().x, ImGui::CalcTextSize(Caption, nullptr, true).x + 2 * PaddingX * Scale);
        const bool Pressed = ImGui::Selectable(Caption, Selected, ImGuiSelectableFlags_NoPadWithHalfSpacing, ImVec2(Width, RowHeight * Scale));
        ImGui::PopStyleColor(4);
        const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
        const auto &Palette = ControlKit::Palette();
        auto *Ink = ImGui::GetWindowDrawList();
        if (Selected || ImGui::IsItemHovered() || ImGui::IsItemFocused())
            Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(ConvertSpectrum(ImGui::IsItemHovered() ? Palette.Raised : Palette.Selected)), RowRadius * Scale);
        const char *CaptionEnd = ImGui::FindRenderedTextEnd(Caption);
        Ink->AddText(ImVec2(Minimum.x + PaddingX * Scale, Minimum.y + (Maximum.y - Minimum.y - ImGui::GetFontSize()) * .5f),
            ImGui::GetColorU32(ConvertSpectrum(Palette.Text)), Caption, CaptionEnd);
        if (Selected) ImGui::SetItemDefaultFocus();
        ImGui::EndDisabled();
        return Pressed;
    }

private:
    static ImVec4 ConvertSpectrum(ColorQuad Spectrum)
    {
        return {Spectrum.Red, Spectrum.Green, Spectrum.Blue, Spectrum.Alpha};
    }

    static void PushSurface(float Scale)
    {
        const auto &Palette = ControlKit::Palette();
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, CornerRadius * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PaddingX * Scale, PaddingY * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * Scale, ControlKitTokens::DropdownRowGap * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(PaddingX * Scale, 8 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, RowRadius * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1 * Scale);
        auto Background = ConvertSpectrum(Palette.Panel);
        Background.w = 1;
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Background);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ConvertSpectrum(Palette.Inset));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ConvertSpectrum(Palette.Raised));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ConvertSpectrum(Palette.Selected));
        ImGui::PushStyleColor(ImGuiCol_Button, ConvertSpectrum(Palette.Inset));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ConvertSpectrum(Palette.Raised));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ConvertSpectrum(Palette.Selected));
        ImGui::PushStyleColor(ImGuiCol_Text, ConvertSpectrum(Palette.Text));
    }

    static void PopSurface()
    {
        ImGui::PopStyleColor(8);
        ImGui::PopStyleVar(6);
    }
};
}
