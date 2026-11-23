/*==========================================================================================================================================
                                                NATIVE PRESENTATION OF THE OUTLINER PANEL
                                     ------------------------------------------------------------------
                 SceneDirectoryPanel drawn with the kit (StackRail + TreeRow + IdentityHeader) in the Frontier
                      visual language; menus remain the reusable upstream DropdownControlSurface, the frame
                                                                   remains PanelDockspace's.
==========================================================================================================================================*/

// Native presentation of the Outliner panel: kit-built chrome over the portable SceneDirectory view-model.

#include "SceneDirectoryPanel.h"

// This translation unit compiles only with the interface SDK present (define FRONTIER_WITH_INTERFACE_SDK).
// Without the SDK it compiles to the empty presentation below so portable builds keep linking.
#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/StackRailControls.h"
#include "Engine/Scene/SceneDirectory.h"
#include "SceneDirectoryPanelRegistration.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

using Theme = KitTheme;

ImU32 ClassificationColour(RecordClassification Classification) noexcept
{
    switch (Classification)
    {
        case RecordClassification::Vehicle:      return KitInkHex(0x2fe6a7);
        case RecordClassification::Tyre:         return KitInkHex(0x63a8ff);
        case RecordClassification::Environment:  return KitInkHex(0x8fd14f);
        case RecordClassification::Geometry:     return KitInkHex(0xb18cff);
        case RecordClassification::Illumination: return KitInkHex(0xffb454);
        case RecordClassification::Camera:       return KitInkHex(0xff7ad9);
        case RecordClassification::Physics:      return KitInkHex(0xff6d6d);
        case RecordClassification::Interface:    return KitInkHex(0x9fd8e8);
        case RecordClassification::Content:      return KitInkHex(0xd8cf9f);
        case RecordClassification::Unclassified: return KitInkHex(0x5f6b64);
        case RecordClassification::Count:        break;
    }
    return KitInkHex(0x5f6b64);
}

void MenuCaption(const KitFaces& Faces, const char* Text)
{
    ImDrawList*  Ink = ImGui::GetWindowDrawList();
    const ImVec2 At  = ImGui::GetCursorScreenPos();
    KitDrawTracked(Ink, Faces.Medium, 10.5f, ImVec2(At.x + 10.0f, At.y + 8.0f), Theme::Faint, Text, 10.5f * 0.13f);
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 27.0f));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

DockPanelSpecification MakeSceneDirectoryPanelSpecification(SceneDirectoryPanel& Panel)
{
    DockPanelSpecification Specification;
    Specification.Key              = "Outliner";
    Specification.Caption          = "Outliner";
    Specification.ConstructContent = [&Panel]() { Panel.ConstructInterface(); };
    Specification.Open             = true;
    return Specification;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERFACE RECORDING
//------------------------------------------------------------------------------------------------------------------------

void SceneDirectoryPanel::ConstructInterface()
{
    SceneDirectory&     Directory = SceneDirectory::Instance();
    const KitFaces      Faces     = ResolveKitFaces();
    static std::array<char, 128> FilterBuffer{};
    static std::string           FilterSynchronised;

    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const ImVec2 WindowSize     = ImGui::GetWindowSize();
    const float  PL             = WindowPosition.x;
    const float  PT             = WindowPosition.y;
    const float  PR             = PL + WindowSize.x;
    const float  PB             = PT + WindowSize.y;
    const float  CL             = PL + 16.0f;   // [px] - Content inset (narrower column than the Log Stream)
    const float  CR             = PR - 16.0f;
    const float  ContentW       = CR - CL;
    ImDrawList*  Ink            = ImGui::GetWindowDrawList();

    Ink->AddRectFilled(WindowPosition, ImVec2(PR, PB), Theme::PanelBg);
    Ink->AddRect(WindowPosition, ImVec2(PR, PB), Theme::PanelEdge);

    // -- StackRail head (title · HeadCount · AddHost) -----------------------------------------------------------------
    char CountText[48] = {};
    std::snprintf(CountText, sizeof(CountText), "%u objects", QueryTotal());
    const bool AddClicked = DrawStackRailHead(Faces, ImVec2(CL, PT + 12.0f), ContentW, "Outliner", CountText);
    if (AddClicked)
    {
        ImGui::OpenPopup("##OutlinerSpawnPopup");
    }
    float CursorY = PT + 12.0f + 36.0f + 10.0f;

    // Spawn menu (reusable dropdown surface): one option per classification; the host creates the record.
    ImGui::SetNextWindowPos(ImVec2(CR - 220.0f, PT + 12.0f + 36.0f + 6.0f), ImGuiCond_Appearing);
    if (DropdownControlSurface::BeginMenu("##OutlinerSpawnPopup"))
    {
        MenuCaption(Faces, "SPAWN RECORD");
        for (uint32_t Index = 0; Index < static_cast<uint32_t>(RecordClassification::Count); ++Index)
        {
            const RecordClassification Classification = static_cast<RecordClassification>(Index);
            char Label[64] = {};
            std::snprintf(Label, sizeof(Label), "%s record", RecordClassificationName(Classification));
            if (DropdownControlSurface::Option(Label))
            {
                RequestSpawn(Classification);
            }
        }
        DropdownControlSurface::EndMenu();
    }

    // -- Category chips (real tabs with live counts) ------------------------------------------------------------------
    {
        float       X = CL;
        const auto& Tabs = QueryTabs();
        const auto  Chip = [&](const char* IdText, const char* Label, uint32_t Count, bool Active, ImU32 Dot,
                               const std::optional<RecordClassification>& Value) {
            char Text[64] = {};
            std::snprintf(Text, sizeof(Text), "%s %u", Label, Count);
            const float W = 12.0f + 7.0f + 7.0f + KitTextWidth(Faces.Regular, 12.0f, Text) + 12.0f;
            if (X + W > CR && X > CL)
            {
                X = CL;
                CursorY += 26.0f + 6.0f;
            }
            bool Hovered = false;
            char Id[80]  = {};
            std::snprintf(Id, sizeof(Id), "##OutlinerChip%s", IdText);
            if (KitTarget(Id, ImVec2(X, CursorY), ImVec2(W, 26.0f), Hovered))
            {
                ConfigureClassificationFilter(Value);
            }
            KitPillSurface(Ink, ImVec2(X, CursorY), ImVec2(X + W, CursorY + 26.0f),
                           Active ? Theme::ChipOnBg : (Hovered ? Theme::Surface3 : Theme::Surface2),
                           Active ? 0 : Theme::Line, 999.0f);
            Ink->AddCircleFilled(ImVec2(X + 15.5f, CursorY + 13.0f), 3.5f, Active ? Theme::ChipOnInk : Dot);
            KitDrawText(Ink, Faces.Regular, 12.0f, ImVec2(X + 26.0f, CursorY + 7.0f),
                        Active ? Theme::ChipOnInk : Theme::Muted, Text);
            X += W + 6.0f;
        };
        Chip("All", "All", QueryTotal(), !QueryClassificationFilter().has_value(), Theme::Mint, std::nullopt);
        for (const SceneClassificationTab& Tab : Tabs)
        {
            if (Tab.Count == 0 && QueryClassificationFilter() != Tab.Classification)
            {
                continue;
            }
            Chip(RecordClassificationName(Tab.Classification), RecordClassificationName(Tab.Classification),
                 Tab.Count, QueryClassificationFilter() == Tab.Classification,
                 ClassificationColour(Tab.Classification), Tab.Classification);
        }
        CursorY += 26.0f + 10.0f;
    }

    // -- Search field (StackRail Filter) ------------------------------------------------------------------------------
    {
        const float RowH  = 34.0f;
        const float RowCY = CursorY + RowH * 0.5f;
        KitPillSurface(Ink, ImVec2(CL, CursorY), ImVec2(CR, CursorY + RowH), Theme::Surface2, Theme::Line, 999.0f);
        // Magnifier glyph.
        Ink->AddCircle(ImVec2(CL + 19.0f, RowCY - 1.5f), 5.5f, Theme::Faint, 24, 1.4f);
        Ink->AddLine(ImVec2(CL + 23.0f, RowCY + 3.0f), ImVec2(CL + 27.0f, RowCY + 7.0f), Theme::Faint, 1.6f);

        if (QueryTextFilter() != FilterSynchronised)
        {
            std::snprintf(FilterBuffer.data(), FilterBuffer.size(), "%s", QueryTextFilter().c_str());
            FilterSynchronised = QueryTextFilter();
        }
        const bool  HasText  = !QueryTextFilter().empty();
        char        Shown[32] = {};
        float       TrailingW = 0.0f;
        if (HasText)
        {
            std::snprintf(Shown, sizeof(Shown), "%zu shown", QueryVisible().size());
            TrailingW = KitTextWidth(Faces.Regular, 11.5f, Shown) + 8.0f + 24.0f + 8.0f;
        }
        ImGui::SetCursorScreenPos(ImVec2(CL + 36.0f, CursorY + (RowH - 13.5f) * 0.5f));
        ImGui::PushFont(Faces.Regular, 13.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.933f, 0.953f, 0.941f, 1.0f));
        ImGui::SetNextItemWidth(std::max(30.0f, ContentW - 36.0f - TrailingW));
        if (ImGui::InputTextWithHint("##OutlinerFilter", "Filter objects\xE2\x80\xA6", FilterBuffer.data(),
                                     FilterBuffer.size()))
        {
            ConfigureTextFilter(FilterBuffer.data());
            FilterSynchronised = QueryTextFilter();
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();

        if (HasText)
        {
            const float ShownW = KitTextWidth(Faces.Regular, 11.5f, Shown);
            KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(CR - 8.0f - 24.0f - 8.0f - ShownW, RowCY - 5.75f),
                        Theme::Faint, Shown);
            const ImVec2 DiscCentre(CR - 8.0f - 12.0f, RowCY);
            bool ClearHover = false;
            if (KitTarget("##OutlinerFilterClear", ImVec2(DiscCentre.x - 12.0f, DiscCentre.y - 12.0f),
                          ImVec2(24.0f, 24.0f), ClearHover, "Clear filter"))
            {
                FilterBuffer[0]    = '\0';
                ConfigureTextFilter("");
                FilterSynchronised = QueryTextFilter();
            }
            Ink->AddCircleFilled(DiscCentre, 12.0f, Theme::Surface3);
            const ImU32 Cross = ClearHover ? Theme::Text : Theme::Muted;
            Ink->AddLine(ImVec2(DiscCentre.x - 4.0f, DiscCentre.y - 4.0f), ImVec2(DiscCentre.x + 4.0f, DiscCentre.y + 4.0f), Cross, 1.4f);
            Ink->AddLine(ImVec2(DiscCentre.x - 4.0f, DiscCentre.y + 4.0f), ImVec2(DiscCentre.x + 4.0f, DiscCentre.y - 4.0f), Cross, 1.4f);
        }
        CursorY += RowH + 10.0f;
    }

    Ink->AddRectFilled(ImVec2(CL, CursorY), ImVec2(CR, CursorY + 1.0f), Theme::LineSoft);
    CursorY += 6.0f;

    // -- Tree (stack-sled rows) ---------------------------------------------------------------------------------------
    const bool  HasInspector = QueryInspector().HasSelection;
    const float FooterH      = 26.0f;
    const float InspectorH   = HasInspector ? 56.0f + 10.0f : 0.0f;
    const float TreeBottom   = PB - 12.0f - FooterH - InspectorH;
    const float TreeH        = std::max(40.0f, TreeBottom - CursorY);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetCursorScreenPos(ImVec2(CL, CursorY));
    if (ImGui::BeginChild("##OutlinerRows", ImVec2(ContentW, TreeH), false))
    {
        const auto& Visible = QueryVisible();
        if (Visible.empty())
        {
            const char* Message = QueryTotal() == 0
                ? "No scene objects yet — records appear here as they are constructed."
                : "No objects match the current filter.";
            const float W = KitTextWidth(Faces.Regular, 12.5f, Message);
            KitDrawText(Ink, Faces.Regular, 12.5f,
                        ImVec2(CL + std::max(0.0f, (ContentW - W) * 0.5f), CursorY + 26.0f), Theme::Faint, Message);
        }
        bool RefreshNeeded = false;
        for (const SceneDirectoryRow& Row : Visible)
        {
            const ImVec2 At = ImGui::GetCursorScreenPos();
            char         Id[48] = {};
            std::snprintf(Id, sizeof(Id), "OutlinerRow%llu", static_cast<unsigned long long>(Row.Token.Value));
            const TreeRowInteraction Hit = DrawTreeRow(
                Faces, Id, ImVec2(CL, At.y), ContentW, static_cast<float>(Row.DepthTier) * 16.0f,
                Row.ChildCount > 0, Row.Collapsed, ClassificationColour(Row.Classification), Row.Name.c_str(),
                RecordClassificationName(Row.Classification), Row.Visible, QuerySelected() == Row.Token);
            if (Hit.ChevronClicked)
            {
                ConfigureCollapsed(Row.Token, !Row.Collapsed);
            }
            else if (Hit.EyeClicked)
            {
                (void)Directory.ConfigureVisible(Row.Token, !Row.Visible);
                RefreshNeeded = true;
            }
            else if (Hit.Clicked)
            {
                Select(Row.Token);
                RefreshNeeded = true;
            }
            // The row's hit targets are absolutely positioned; normalise the layout advance to one row.
            ImGui::SetCursorScreenPos(ImVec2(CL, At.y + 30.0f));
            ImGui::Dummy(ImVec2(ContentW, 0.0f));
        }
        if (RefreshNeeded)
        {
            Synchronize(Directory);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // -- Inspector identity (selection summary) -----------------------------------------------------------------------
    if (HasInspector)
    {
        const SceneInspectorSummary& Summary = QueryInspector();
        char Sub[160] = {};
        std::snprintf(Sub, sizeof(Sub), "%s \xC2\xB7 %u children \xC2\xB7 parent %s \xC2\xB7 #%llu",
                      RecordClassificationName(Summary.Classification), Summary.ChildCount,
                      Summary.ParentName.empty() ? "—" : Summary.ParentName.c_str(),
                      static_cast<unsigned long long>(Summary.Token.Value));
        DrawIdentityHeader(Faces, ImVec2(CL, TreeBottom + 8.0f), ContentW,
                           ClassificationColour(Summary.Classification), Summary.Name.c_str(), Sub);
    }

    // -- StackFoot readout --------------------------------------------------------------------------------------------
    uint32_t HiddenCount = 0;
    for (const SceneDirectoryRow& Row : QueryRows())
    {
        HiddenCount += Row.Visible ? 0 : 1;
    }
    char Left[64] = {};
    std::snprintf(Left, sizeof(Left), "%zu shown \xC2\xB7 %u total", QueryVisible().size(), QueryTotal());
    char Right[64] = {};
    if (QuerySelected().IsValid())
    {
        std::snprintf(Right, sizeof(Right), "Selected #%llu", static_cast<unsigned long long>(QuerySelected().Value));
    }
    else
    {
        std::snprintf(Right, sizeof(Right), "%u hidden", HiddenCount);
    }
    DrawStackRailFoot(Faces, ImVec2(CL, PB - 12.0f - FooterH), ContentW, Left, Right);
}

} // namespace Frontier

#else // FRONTIER_WITH_INTERFACE_SDK

namespace Frontier
{

void SceneDirectoryPanel::ConstructInterface()
{
    // No interface SDK in this build: the view-model runs (and is validated) headlessly; drawing arrives
    //    with the SDK-backed build. Nothing is faked here.
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
