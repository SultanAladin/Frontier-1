/*============================================================================================================================================
                                                      SKETCHEROPERATIONINPUT.CPP
============================================================================================================================================*/

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherOperationInput.h"

#include <imgui.h>

namespace Frontier::Workspaces::Sketcher
{

SketcherOperationInput SketcherOperationInput::Capture() noexcept
{
    SketcherOperationInput Result;
    if (Input::InputExtension* Extension = Input::InputExtension::QueryActiveExtension();
        Extension != nullptr && Extension->ExtensionActive())
    {
        Result.Snapshot = &Extension->InspectEditorSnapshot();
    }
    return Result;
}

bool SketcherOperationInput::PrimaryPressed() const noexcept
{
    return Snapshot ? Snapshot->QueryButtonPressed(0u)
                    : ImGui::GetCurrentContext() != nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

bool SketcherOperationInput::PrimaryReleased() const noexcept
{
    return Snapshot ? Snapshot->QueryButtonReleased(0u)
                    : ImGui::GetCurrentContext() != nullptr && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
}

bool SketcherOperationInput::SecondaryPressed() const noexcept
{
    return Snapshot ? Snapshot->QueryButtonPressed(1u)
                    : ImGui::GetCurrentContext() != nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
}

bool SketcherOperationInput::SecondaryReleased() const noexcept
{
    return Snapshot ? Snapshot->QueryButtonReleased(1u)
                    : ImGui::GetCurrentContext() != nullptr && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
}

bool SketcherOperationInput::ConfirmPressed() const noexcept
{
    return Snapshot ? Snapshot->QueryKeyPressed(Input::InputLogicalKey::Enter)
                           || Snapshot->QueryKeyPressed(Input::InputLogicalKey::KeypadEnter)
                    : ImGui::GetCurrentContext() != nullptr
                           && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
}

bool SketcherOperationInput::CancelPressed() const noexcept
{
    return Snapshot ? Snapshot->QueryKeyPressed(Input::InputLogicalKey::Escape)
                    : ImGui::GetCurrentContext() != nullptr && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

} // namespace Frontier::Workspaces::Sketcher
