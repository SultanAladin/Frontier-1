/*============================================================================================================================================
                                                      VECTORMASKCONTROLS.CPP
============================================================================================================================================*/
// 🧩 Native retained icon geometry, shared themed switches and range controls; mask/Boolean evaluation remains in the GPU shader.

#include "VectorMaskControls.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/DropdownControls.h"
#include <imgui.h>
#include <algorithm>

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED CONTROL GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

bool ConstructVectorAuthoringToggle(const char *Caption, bool &Enabled)
{
    ImGui::PushID(Caption);
    const auto Origin = ImGui::GetCursorScreenPos();
    const bool Changed = ImGui::InvisibleButton("Toggle", ImVec2(46, 28));
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
    if (Changed) Enabled = !Enabled;
    (void)ControlKit::Switch(Surface, Origin.x, Origin.y, Enabled, ControlPointer{}, ImGui::GetStyle().Alpha);
    ImGui::SameLine(); ImGui::TextUnformatted(Caption); ImGui::PopID(); return Changed;
}
bool ConstructVectorAuthoringRange(const char *Caption, float &Position, float Minimum, float Maximum)
{
    ImGui::PushID(Caption); ImGui::TextUnformatted(Caption);
    ImGui::SetNextItemWidth(-1);
    bool Changed = ImGui::InputFloat("##Numeric", &Position, 0, 0, "%.2f");
    if (Changed) Position = std::isfinite(Position) ? std::clamp(Position, Minimum, Maximum) : Minimum;
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = std::max(48.f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("Range", ImVec2(Width, 28));
    const auto &Input = ImGui::GetIO(); const bool Active = ImGui::IsItemActive();
    ControlPointer Pointer{Input.MousePos.x, Input.MousePos.y, Input.MouseDown[0], ImGui::IsItemActivated(), ImGui::IsMouseReleased(0), Active || ImGui::IsItemHovered(), Input.MouseWheel};
    PixelSpace Surface; (void)Surface.Begin(SurfaceLayer::Window, Input.DisplaySize.x, Input.DisplaySize.y, 1);
    float Replacement = Position;
    (void)ControlKit::Slider(Surface, {Origin.x, Origin.y, Origin.x + Width, Origin.y + 28}, Minimum, Maximum, Position, Active, Pointer, Replacement, ImGui::GetStyle().Alpha, 28, 26);
    if (Active && Replacement != Position) { Position = Replacement; Changed = true; }
    ImGui::PopID(); return Changed;
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ICON-ONLY BOOLEAN ACTIONS
//------------------------------------------------------------------------------------------------------------------------

std::optional<uint32_t> ConstructVectorBooleanControls(bool Enabled)
{
    const char *Captions[]{"Union", "Difference", "Intersection", "Exclude"};
    std::optional<uint32_t> Operation;
    ImGui::PushID("VectorBooleans"); ImGui::BeginDisabled(!Enabled);
    for (uint32_t Index = 0; Index < 4; ++Index)
    {
        if (Index) ImGui::SameLine();
        ImGui::PushID(Captions[Index]);
        // 📝 Retain the semantic command label in ImGui metadata without painting words over the icon.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(Captions[Index], ImVec2(36, 32))) Operation = 16 + Index;
        ImGui::PopStyleColor();
        const auto Origin = ImGui::GetItemRectMin(); auto *Ink = ImGui::GetWindowDrawList();
        const auto Colour = ImGui::GetColorU32(ImGuiCol_Text);
        const auto Tint = ImGui::GetColorU32(ImVec4(1, 1, 1, .45f * ImGui::GetStyle().Alpha));
        const auto Point = [&](float X, float Y) { return ImVec2(Origin.x + 6 + X, Origin.y + 4 + Y); };
        Ink->AddRect(Point(3, 3), Point(15, 15), Colour, 0, 0, 1.5f);
        Ink->AddRect(Point(9, 9), Point(21, 21), Colour, 0, 0, 1.5f);
        // 📝 Disjoint retained rectangles describe the same filled regions as the approved HTML SVG glyphs.
        if (Index != 2)
        {
            Ink->AddRectFilled(Point(3, 3), Point(15, 9), Tint);
            Ink->AddRectFilled(Point(3, 9), Point(9, 15), Tint);
        }
        if (Index != 1 && Index != 2)
        {
            Ink->AddRectFilled(Point(15, 9), Point(21, 21), Tint);
            Ink->AddRectFilled(Point(9, 15), Point(15, 21), Tint);
        }
        if (Index == 0 || Index == 2) Ink->AddRectFilled(Point(9, 9), Point(15, 15), Index == 2 ? Colour : Tint);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s\nSelect 2–16 visible, unlocked, filled vectors. The lower appearance is retained.", Captions[Index]);
        ImGui::PopID();
    }
    ImGui::EndDisabled(); ImGui::PopID(); return Operation;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MASK AUTHORING CONTROLS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructVectorAuthoringMaskControls(VectorAuthoringLayer &Layer, const std::array<float, 4> &SourceBounds)
{
    ImGui::PushID("VectorAuthoringMask"); bool Changed = false;
    ImGui::BeginDisabled(Layer.Locked);
    ImGui::TextUnformatted("Mask");
    if (!Layer.Mask)
    {
        if (ImGui::Button("Add mask"))
        {
            VectorCoverageInstruction Mask; Mask.Topology[0] = 1; Mask.Bounds = SourceBounds; Layer.Mask = Mask; Changed = true;
        }
    }
    else
    {
        Changed |= ConstructVectorAuthoringToggle("Enable vector mask", Layer.MaskEnabled);
        Changed |= ConstructVectorAuthoringToggle("Invert vector mask", Layer.MaskInverted);
        Changed |= ConstructVectorAuthoringToggle("Luminance mask", Layer.MaskLuminance);
        auto &Mask = *Layer.Mask;
        const char *Categories[]{"Rectangle", "Ellipse"}; int Category = int(Mask.Topology[0]);
        if (DropdownControlSurface::BeginSelection("Shape", Categories[std::clamp(Category, 0, 1)]))
        {
            for (int Index = 0; Index < 2; ++Index)
                if (DropdownControlSurface::Option(Categories[Index], Category == Index)) { Mask.Topology[0] = uint32_t(Index); Changed = true; }
            DropdownControlSurface::EndSelection();
        }
        Changed |= ConstructVectorAuthoringRange("Mask opacity", Mask.Transform[2], 0, 1);
        Changed |= ConstructVectorAuthoringRange("Mask X", Mask.Bounds[0], -512, 1024);
        Changed |= ConstructVectorAuthoringRange("Mask Y", Mask.Bounds[1], -512, 1024);
        Changed |= ConstructVectorAuthoringRange("Mask width", Mask.Bounds[2], 1, 1024);
        Changed |= ConstructVectorAuthoringRange("Mask height", Mask.Bounds[3], 1, 1024);
        if (ImGui::Button("Remove mask")) { Layer.Mask.reset(); Changed = true; }
        ImGui::TextDisabled("Image masks have not been ported to native GPU residency yet.");
    }
    ImGui::EndDisabled();
    ImGui::PopID(); return Changed;
}
}
