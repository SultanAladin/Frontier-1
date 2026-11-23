/*============================================================================================================================================
                                                       SKETCHEROPERATIONINPUT.H
============================================================================================================================================*/
// Dedicated modal-operation input view. SolidArc commands consume Frontier's InputExtension snapshot through this class
// instead of binding operation confirmation/cancellation directly to ImGui. The ImGui fallback only serves isolated
// validation contexts where no InputExtension lifecycle participant exists.

#pragma once

#include "Engine/Input/InputExtension.h"

namespace Frontier::Workspaces::Sketcher
{

class SketcherOperationInput
{
public:
    [[nodiscard]] static SketcherOperationInput Capture() noexcept;

    [[nodiscard]] bool PrimaryPressed() const noexcept;
    [[nodiscard]] bool PrimaryReleased() const noexcept;
    [[nodiscard]] bool SecondaryPressed() const noexcept;
    [[nodiscard]] bool SecondaryReleased() const noexcept;
    [[nodiscard]] bool ConfirmPressed() const noexcept;
    [[nodiscard]] bool CancelPressed() const noexcept;

private:
    const Input::EditorInputSnapshot* Snapshot = nullptr;
};

} // namespace Frontier::Workspaces::Sketcher
