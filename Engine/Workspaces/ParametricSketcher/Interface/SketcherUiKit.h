/*============================================================================================================================================
                                                              SKETCHERUIKIT.H
============================================================================================================================================*/
// 🧩 Self-contained UI primitives & compatibility surface for Parametric Sketcher (ImDrawList, ControlKit, VectorAuthoring, PanelChrome)

#pragma once

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/PixelSpace.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

namespace Frontier::Workspaces::Sketcher
{

struct PlaneExtent
{
    float MinimumX = 0.0f;
    float MinimumY = 0.0f;
    float MaximumX = 0.0f;
    float MaximumY = 0.0f;

    [[nodiscard]] constexpr float Width()  const noexcept { return MaximumX - MinimumX; }
    [[nodiscard]] constexpr float Height() const noexcept { return MaximumY - MinimumY; }
    [[nodiscard]] constexpr bool  Encloses(float X, float Y) const noexcept
    {
        return X >= MinimumX && X < MaximumX && Y >= MinimumY && Y < MaximumY;
    }
};

[[nodiscard]] constexpr PlaneExtent Spanning(float X, float Y, float Width, float Height) noexcept
{
    return PlaneExtent{ X, Y, X + Width, Y + Height };
}

struct PlanePoint
{
    float X = 0.0f;
    float Y = 0.0f;
};

[[nodiscard]] inline ImU32 ToImU32(ColorQuad Ink) noexcept
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(Ink.Red, Ink.Green, Ink.Blue, Ink.Alpha));
}

// Thin canvas adapter over ImGui::GetWindowDrawList() matching SolidArcSheet::Surface()
class SketcherSurfaceCanvas
{
public:
    void FillRectangle(const PlaneExtent& Extent, ColorQuad Fill, float CornerRadius = 0.0f) const noexcept
    {
        if (Fill.Alpha <= 0.0f || Extent.Width() <= 0.0f || Extent.Height() <= 0.0f)
        {
            return;
        }
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        if (Draw != nullptr)
        {
            Draw->AddRectFilled(ImVec2(Extent.MinimumX, Extent.MinimumY), ImVec2(Extent.MaximumX, Extent.MaximumY), ToImU32(Fill), CornerRadius);
        }
    }

    void StrokeRectangle(const PlaneExtent& Extent, ColorQuad Border, float CornerRadius = 0.0f, float Thickness = 1.0f) const noexcept
    {
        if (Border.Alpha <= 0.0f || Extent.Width() <= 0.0f || Extent.Height() <= 0.0f)
        {
            return;
        }
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        if (Draw != nullptr)
        {
            Draw->AddRect(ImVec2(Extent.MinimumX, Extent.MinimumY), ImVec2(Extent.MaximumX, Extent.MaximumY), ToImU32(Border), CornerRadius, 0, Thickness);
        }
    }

    void StrokePolyline(const PlanePoint* Points, int Count, ColorQuad Ink, float Thickness = 1.0f, bool Closed = false) const noexcept
    {
        if (Points == nullptr || Count < 2 || Ink.Alpha <= 0.0f)
        {
            return;
        }
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        if (Draw == nullptr)
        {
            return;
        }
        if (Count == 2 && !Closed)
        {
            Draw->AddLine(ImVec2(Points[0].X, Points[0].Y), ImVec2(Points[1].X, Points[1].Y), ToImU32(Ink), Thickness);
            return;
        }
        for (int I = 0; I + 1 < Count; ++I)
        {
            Draw->AddLine(ImVec2(Points[I].X, Points[I].Y), ImVec2(Points[I + 1].X, Points[I + 1].Y), ToImU32(Ink), Thickness);
        }
        if (Closed)
        {
            Draw->AddLine(ImVec2(Points[Count - 1].X, Points[Count - 1].Y), ImVec2(Points[0].X, Points[0].Y), ToImU32(Ink), Thickness);
        }
    }

    void PushClip(const PlaneExtent& Extent) const noexcept
    {
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        if (Draw != nullptr)
        {
            Draw->PushClipRect(ImVec2(Extent.MinimumX, Extent.MinimumY), ImVec2(Extent.MaximumX, Extent.MaximumY), true);
        }
    }

    void PopClip() const noexcept
    {
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        if (Draw != nullptr)
        {
            Draw->PopClipRect();
        }
    }
};

struct ControlKitTheme
{
    ImVec4 Background       { 0.09f, 0.09f, 0.10f, 1.0f };
    ImVec4 Surface          { 0.12f, 0.12f, 0.13f, 1.0f };
    ImVec4 ControlFill      { 0.17f, 0.17f, 0.19f, 1.0f };
    ImVec4 Accent           { 0.25f, 0.60f, 1.00f, 1.0f };
    ImVec4 AccentSecondary  { 0.85f, 0.70f, 0.51f, 1.0f };
};

namespace ControlKit
{

inline ControlKitTheme& MutablePalette() noexcept
{
    static ControlKitTheme Instance {};
    return Instance;
}

[[nodiscard]] inline ControlKitTheme Palette() noexcept { return MutablePalette(); }
inline void ConfigurePalette(const ControlKitTheme& Next) noexcept { MutablePalette() = Next; }

[[nodiscard]] inline ColorQuad Faded(ColorQuad Ink, float Opacity) noexcept
{
    return ColorQuad{ Ink.Red, Ink.Green, Ink.Blue, Ink.Alpha * std::clamp(Opacity, 0.0f, 1.0f) };
}

inline void OutlineRounded(const SketcherSurfaceCanvas& Canvas, const PlaneExtent& Extent, ColorQuad Border, float Radius, float Thickness) noexcept
{
    Canvas.StrokeRectangle(Extent, Border, Radius, Thickness);
}

} // namespace ControlKit

namespace PanelChrome
{

struct DockChromeToken
{
    int ColorCount = 0;
    int VarCount   = 0;
};

[[nodiscard]] inline ControlKitTheme StudioPalette(ControlKitTheme Base, ImVec4 SurfaceCol, ImVec4 AccentCol) noexcept
{
    Base.Surface         = SurfaceCol;
    Base.AccentSecondary = AccentCol;
    return Base;
}

[[nodiscard]] inline DockChromeToken PushDockChrome(ImVec4 TabBg, ImVec4 Bg, bool /*Rounded*/, bool /*Compact*/, float /*Scale*/) noexcept
{
    ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, Bg);
    ImGui::PushStyleColor(ImGuiCol_Tab, TabBg);
    return DockChromeToken{ 2, 0 };
}

inline void PopDockChrome(DockChromeToken Token) noexcept
{
    if (Token.VarCount > 0) ImGui::PopStyleVar(Token.VarCount);
    if (Token.ColorCount > 0) ImGui::PopStyleColor(Token.ColorCount);
}

} // namespace PanelChrome

class VectorAuthoringControlSurface
{
public:
    float Scale = 1.0f;
    float ControlHeight = 30.0f;
    std::function<void(std::string_view, PlaneExtent)> RegisterExtent;

    bool Toggle(const char* Caption, bool& Enabled)
    {
        const ImVec2 Pos = ImGui::GetCursorScreenPos();
        const char* Hash = std::strstr(Caption, "##");
        const char* LabelEnd = Hash ? Hash : (Caption + std::strlen(Caption));
        const bool HasLabel = LabelEnd > Caption;
        const float TrackW = 34.0f * Scale;
        const float TrackH = 18.0f * Scale;
        const float RowH = 26.0f * Scale;
        const float AvailX = std::max(TrackW, ImGui::GetContentRegionAvail().x);
        const float ButtonW = HasLabel ? AvailX : TrackW;

        const bool Changed = ImGui::InvisibleButton(Caption, ImVec2(ButtonW, RowH));
        if (Changed)
        {
            Enabled = !Enabled;
        }
        const ImVec2 RMin = ImGui::GetItemRectMin();
        const ImVec2 RMax = ImGui::GetItemRectMax();
        const bool Hovered = ImGui::IsItemHovered();
        if (RegisterExtent)
        {
            RegisterExtent(Caption, PlaneExtent{ RMin.x, RMin.y, RMax.x, RMax.y });
        }
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        if (Draw != nullptr)
        {
            const float TrackY = Pos.y + 0.5f * (RowH - TrackH);
            const ImU32 TrackCol = Enabled
                ? (Hovered ? IM_COL32(84, 168, 255, 255) : IM_COL32(64, 153, 255, 255))
                : (Hovered ? IM_COL32(72, 76, 86, 255) : IM_COL32(55, 58, 66, 255));
            Draw->AddRectFilled(ImVec2(Pos.x, TrackY), ImVec2(Pos.x + TrackW, TrackY + TrackH), TrackCol, TrackH * 0.5f);
            const float KnobX = Enabled ? (Pos.x + TrackW - TrackH * 0.5f) : (Pos.x + TrackH * 0.5f);
            Draw->AddCircleFilled(ImVec2(KnobX, TrackY + TrackH * 0.5f), TrackH * 0.38f, IM_COL32(245, 246, 250, 255));
            if (HasLabel)
            {
                const float TextX = Pos.x + TrackW + 10.0f * Scale;
                const float TextMaxX = Pos.x + ButtonW;
                if (TextMaxX > TextX + 4.0f)
                {
                    const float FontH = ImGui::GetFontSize();
                    const float TextY = Pos.y + 0.5f * (RowH - FontH);
                    Draw->PushClipRect(ImVec2(TextX, Pos.y), ImVec2(TextMaxX, Pos.y + RowH), true);
                    Draw->AddText(ImVec2(TextX, TextY), ImGui::GetColorU32(ImGuiCol_Text), Caption, LabelEnd);
                    Draw->PopClipRect();
                }
            }
        }
        return Changed;
    }

    bool Slider(const char* Caption, float& Quantity, float Minimum, float Maximum)
    {
        ImGui::PushID(Caption);
        const float AvailW = std::max(80.0f, ImGui::GetContentRegionAvail().x);
        const char* Hash = std::strstr(Caption, "##");
        const char* LabelEnd = Hash ? Hash : (Caption + std::strlen(Caption));
        const bool HasLabel = LabelEnd > Caption;

        char ValueBuf[32];
        std::snprintf(ValueBuf, sizeof(ValueBuf), "%.2f", Quantity);

        if (HasLabel)
        {
            const ImVec2 HeaderPos = ImGui::GetCursorScreenPos();
            const float FontH = ImGui::GetFontSize();
            const float ValueW = ImGui::CalcTextSize(ValueBuf).x;
            const float HeaderH = FontH + 2.0f * Scale;
            ImDrawList* Draw = ImGui::GetWindowDrawList();
            if (Draw != nullptr)
            {
                const float LabelMaxX = std::max(HeaderPos.x + 16.0f, HeaderPos.x + AvailW - ValueW - 8.0f);
                Draw->PushClipRect(ImVec2(HeaderPos.x, HeaderPos.y), ImVec2(LabelMaxX, HeaderPos.y + HeaderH), true);
                Draw->AddText(HeaderPos, ImGui::GetColorU32(ImGuiCol_Text), Caption, LabelEnd);
                Draw->PopClipRect();
                Draw->AddText(ImVec2(HeaderPos.x + AvailW - ValueW, HeaderPos.y), IM_COL32(180, 186, 198, 255), ValueBuf);
            }
            ImGui::Dummy(ImVec2(AvailW, HeaderH));
        }

        const ImVec2 TrackPos = ImGui::GetCursorScreenPos();
        const float SliderH = 22.0f * Scale;
        ImGui::SetNextItemWidth(AvailW);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, std::max(2.0f, 0.5f * (SliderH - ImGui::GetFontSize()))));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 12.0f * Scale);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(28, 30, 36, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(36, 39, 47, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(42, 46, 56, 255));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(82, 166, 255, 255));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(120, 190, 255, 255));
        const bool Changed = ImGui::SliderFloat("##Value", &Quantity, Minimum, Maximum, HasLabel ? "" : "%.2f");
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);

        const ImVec2 RMin = ImGui::GetItemRectMin();
        const ImVec2 RMax = ImGui::GetItemRectMax();
        if (ImDrawList* Draw = ImGui::GetWindowDrawList(); Draw != nullptr && Maximum > Minimum)
        {
            const float T = std::clamp((Quantity - Minimum) / (Maximum - Minimum), 0.0f, 1.0f);
            const float FillW = std::max(0.0f, (RMax.x - RMin.x - 4.0f) * T);
            if (FillW > 2.0f)
            {
                const float MidY = 0.5f * (RMin.y + RMax.y);
                Draw->AddRectFilled(ImVec2(RMin.x + 2.0f, MidY - 2.0f), ImVec2(RMin.x + 2.0f + FillW, MidY + 2.0f),
                                    IM_COL32(64, 153, 255, 190), 2.0f);
            }
        }
        (void)TrackPos;
        if (RegisterExtent)
        {
            RegisterExtent(Caption, PlaneExtent{ RMin.x, RMin.y, RMax.x, RMax.y });
        }
        ImGui::PopID();
        return Changed;
    }
};

[[nodiscard]] inline std::filesystem::path ResolveContentDirectory(const std::filesystem::path& Requested)
{
    if (std::filesystem::exists(Requested))
    {
        return Requested;
    }
    const std::filesystem::path FromRepo = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / Requested;
    if (std::filesystem::exists(FromRepo))
    {
        return FromRepo;
    }
    return Requested;
}

struct DockPanelSpecification
{
    std::string           Key {};
    std::string           Caption {};
    std::function<void()> ConstructContent {};
    bool                  Open = true;
};

class PanelDockspace
{
public:
    void ConfigureDockspaceKey(std::string Key) { DockKey = std::move(Key); }
    void ConfigureWorkspaceInsets(float, float Bottom) noexcept { BottomInset = Bottom; }
    void Register(DockPanelSpecification Spec) { Panels.push_back(std::move(Spec)); }
    void ConfigureColumns(const std::vector<std::pair<std::string, float>>& Cols) { Columns = Cols; }
    void ConfigureOpen(const std::string& Key, bool Open) noexcept
    {
        for (auto& P : Panels) if (P.Key == Key) P.Open = Open;
    }
    void ResetLayout() noexcept {}
    void ConstructInterface(float)
    {
        const ImVec2 Display = ImGui::GetIO().DisplaySize;
        const float AvailH = std::max(100.0f, Display.y - BottomInset);
        float X = 0.0f;
        for (size_t I = 0; I < Panels.size(); ++I)
        {
            if (!Panels[I].Open) continue;
            float Frac = (I < Columns.size()) ? Columns[I].second : (1.0f / std::max<size_t>(1, Panels.size()));
            const float W = (I + 1 == Panels.size()) ? (Display.x - X) : std::floor(Display.x * Frac);
            ImGui::SetNextWindowPos(ImVec2(X, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(W, AvailH));
            const std::string WinName = Panels[I].Caption + "###FrontierPanel-" + Panels[I].Key;
            if (ImGui::Begin(WinName.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
            {
                if (Panels[I].ConstructContent) Panels[I].ConstructContent();
            }
            ImGui::End();
            X += W;
        }
    }
private:
    std::string DockKey {};
    float BottomInset = 0.0f;
    std::vector<DockPanelSpecification> Panels {};
    std::vector<std::pair<std::string, float>> Columns {};
};

} // namespace Frontier::Workspaces::Sketcher
