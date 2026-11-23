// 🧩 Inspector host: draws the description the selected record built for itself.

#include "SceneInspectorPanel.h"

#include "Engine/Interface/SceneDirectoryPanel.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include <imgui.h>

#include <cstdio>

namespace Frontier
{

namespace
{
// 📝 Pinned HTML inspector metrics (Style/FrontierEditor.css at 6afb6f0), in pixels.
constexpr float CrumbBarHeight   = 44.0f;   // [px] - .inspector-top height
constexpr float ContentPaddingX  = 16.0f;   // [px] - .inspector-content horizontal padding
constexpr float ContentPaddingTop = 20.0f;  // [px] - .inspector-content top padding
constexpr float HeaderFontSize   = 19.0f;   // [px] - .object-header h1
constexpr float EyebrowFontSize  = 9.0f;    // [px] - .eyebrow
constexpr float CardRadius       = 18.0f;   // [px] - .card radius (ImGui child rounding)
} // namespace

void ConstructSceneInspectorInterface(SceneDirectoryPanel &Panel)
{
    const SceneInspectorModel &Model = Panel.QueryInspectorModel();

    // -- Crumb bar (44 px): Inspector > Group ......... Changes apply live --------------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ContentPaddingX, 0.0f));
    ImGui::BeginChild("##InspectorCrumbs", ImVec2(0, CrumbBarHeight), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Inspector");
    if (Model.HasSelection)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        ImGui::TextUnformatted(Model.Group.c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
        ImGui::TextDisabled("Changes apply live");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0, ContentPaddingTop - 8.0f));
    ImGui::Indent(ContentPaddingX * 0.25f);
    if (!Model.HasSelection)
    {
        ImGui::TextDisabled("No selection");
        ImGui::Unindent(ContentPaddingX * 0.25f);
        return;
    }

    // -- Object header: eyebrow + 19 px name + description, enabled pill on the right ----------------------
    ImGui::TextDisabled("%s", Model.Category.c_str());   // .eyebrow (9 px in the reference type scale)
    ImGui::PushFont(nullptr);                            // Host font at the header size is presentation-owned
    ImGui::TextUnformatted(Model.Name.c_str());
    ImGui::PopFont();
    if (!Model.Description.empty()) ImGui::TextWrapped("%s", Model.Description.c_str());

#if defined(FRONTIER_EDITOR_PROFILE)
    if (Model.HasEnabled)
    {
        // Find the live enabled target through the first switch-free path: the model mirrors the
        // record's current value; writes go through the description on the next Synchronize cycle.
        ImGui::TextDisabled(Model.Enabled ? "Enabled" : "Disabled");   // .enabled-pill
    }

    // -- Property switches ----------------------------------------------------------------------------------
    if (!Model.Switches.empty())
    {
        ImGui::Spacing();
        for (size_t Index = 0; Index < Model.Switches.size(); ++Index)
        {
            if (Index > 0) ImGui::SameLine();
            bool *Target = Model.Switches[Index].Target;   // Pointee stays mutable through the copied pointer
            if (Target != nullptr) ImGui::Checkbox(Model.Switches[Index].Label, Target);
        }
    }

    // -- Card of sliders: label + number field on one line, 3 px rail beneath (.control) -------------------
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, CardRadius);
    ImGui::BeginChild("##InspectorCard", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    for (size_t Index = 0; Index < Model.ScalarFields.size(); ++Index)
    {
        const RecordEditorScalarField &Field = Model.ScalarFields[Index];
        if (Field.Target == nullptr) continue;
        char NumberId[96] = {};
        char SliderId[96] = {};
        std::snprintf(NumberId, sizeof(NumberId), "##InspectorValue%zu", Index);
        std::snprintf(SliderId, sizeof(SliderId), "##InspectorSlider%zu", Index);
        ImGui::TextUnformatted(Field.Label);             // .control-line label (10 px reference)
        ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
        ImGui::SetNextItemWidth(76.0f);                  // .number-field minimum width
        double Value = *Field.Target;
        if (ImGui::InputDouble(NumberId, &Value, Field.Step, Field.Step * 10.0, "%.*f", ImGuiInputTextFlags_EnterReturnsTrue))
        {
            *Field.Target = Value < Field.Minimum ? Field.Minimum : (Value > Field.Maximum ? Field.Maximum : Value);
        }
        if (Field.Unit[0] != '\0')
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", Field.Unit);
        }
        ImGui::SetNextItemWidth(-1.0f);
        float SliderValue = static_cast<float>(*Field.Target);
        if (ImGui::SliderFloat(SliderId, &SliderValue, static_cast<float>(Field.Minimum),
                               static_cast<float>(Field.Maximum), ""))
        {
            *Field.Target = SliderValue;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
#else
    ImGui::TextDisabled("Editor profile required");
#endif
    ImGui::Unindent(ContentPaddingX * 0.25f);
}

} // namespace Frontier

#else // FRONTIER_WITH_INTERFACE_SDK

namespace Frontier
{
void ConstructSceneInspectorInterface(SceneDirectoryPanel &) {}
} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
