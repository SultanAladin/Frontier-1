/*============================================================================================================================================
                                                           IMGUIINPUTADAPTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of one-way EditorInputExchange -> ImGuiIO downstream packet translation

#include "Engine/Input/ImGuiInputAdapter.h"

#include <imgui.h>

namespace Frontier::Input
{

namespace
{

[[nodiscard]] ImGuiKey TranslateLogicalKeyToImGui(InputLogicalKey Key) noexcept
{
    switch (Key)
    {
        case InputLogicalKey::Escape:       return ImGuiKey_Escape;
        case InputLogicalKey::Enter:        return ImGuiKey_Enter;
        case InputLogicalKey::KeypadEnter:  return ImGuiKey_KeypadEnter;
        case InputLogicalKey::Tab:          return ImGuiKey_Tab;
        case InputLogicalKey::Backspace:    return ImGuiKey_Backspace;
        case InputLogicalKey::Delete:       return ImGuiKey_Delete;
        case InputLogicalKey::Space:        return ImGuiKey_Space;
        case InputLogicalKey::LeftArrow:    return ImGuiKey_LeftArrow;
        case InputLogicalKey::RightArrow:   return ImGuiKey_RightArrow;
        case InputLogicalKey::UpArrow:      return ImGuiKey_UpArrow;
        case InputLogicalKey::DownArrow:    return ImGuiKey_DownArrow;
        case InputLogicalKey::Home:         return ImGuiKey_Home;
        case InputLogicalKey::End:          return ImGuiKey_End;
        case InputLogicalKey::PageUp:       return ImGuiKey_PageUp;
        case InputLogicalKey::PageDown:     return ImGuiKey_PageDown;
        case InputLogicalKey::Comma:        return ImGuiKey_Comma;
        case InputLogicalKey::Period:       return ImGuiKey_Period;
        case InputLogicalKey::Slash:        return ImGuiKey_Slash;
        case InputLogicalKey::Semicolon:    return ImGuiKey_Semicolon;
        case InputLogicalKey::A:            return ImGuiKey_A;
        case InputLogicalKey::B:            return ImGuiKey_B;
        case InputLogicalKey::C:            return ImGuiKey_C;
        case InputLogicalKey::D:            return ImGuiKey_D;
        case InputLogicalKey::E:            return ImGuiKey_E;
        case InputLogicalKey::F:            return ImGuiKey_F;
        case InputLogicalKey::G:            return ImGuiKey_G;
        case InputLogicalKey::H:            return ImGuiKey_H;
        case InputLogicalKey::I:            return ImGuiKey_I;
        case InputLogicalKey::J:            return ImGuiKey_J;
        case InputLogicalKey::K:            return ImGuiKey_K;
        case InputLogicalKey::L:            return ImGuiKey_L;
        case InputLogicalKey::M:            return ImGuiKey_M;
        case InputLogicalKey::N:            return ImGuiKey_N;
        case InputLogicalKey::O:            return ImGuiKey_O;
        case InputLogicalKey::P:            return ImGuiKey_P;
        case InputLogicalKey::Q:            return ImGuiKey_Q;
        case InputLogicalKey::R:            return ImGuiKey_R;
        case InputLogicalKey::S:            return ImGuiKey_S;
        case InputLogicalKey::T:            return ImGuiKey_T;
        case InputLogicalKey::U:            return ImGuiKey_U;
        case InputLogicalKey::V:            return ImGuiKey_V;
        case InputLogicalKey::W:            return ImGuiKey_W;
        case InputLogicalKey::X:            return ImGuiKey_X;
        case InputLogicalKey::Y:            return ImGuiKey_Y;
        case InputLogicalKey::Z:            return ImGuiKey_Z;
        case InputLogicalKey::Digit0:       return ImGuiKey_0;
        case InputLogicalKey::Digit1:       return ImGuiKey_1;
        case InputLogicalKey::Digit2:       return ImGuiKey_2;
        case InputLogicalKey::Digit3:       return ImGuiKey_3;
        case InputLogicalKey::Digit4:       return ImGuiKey_4;
        case InputLogicalKey::Digit5:       return ImGuiKey_5;
        case InputLogicalKey::Digit6:       return ImGuiKey_6;
        case InputLogicalKey::Digit7:       return ImGuiKey_7;
        case InputLogicalKey::Digit8:       return ImGuiKey_8;
        case InputLogicalKey::Digit9:       return ImGuiKey_9;
        case InputLogicalKey::F1:           return ImGuiKey_F1;
        case InputLogicalKey::F2:           return ImGuiKey_F2;
        case InputLogicalKey::F3:           return ImGuiKey_F3;
        case InputLogicalKey::F4:           return ImGuiKey_F4;
        case InputLogicalKey::F5:           return ImGuiKey_F5;
        case InputLogicalKey::F6:           return ImGuiKey_F6;
        case InputLogicalKey::F7:           return ImGuiKey_F7;
        case InputLogicalKey::F8:           return ImGuiKey_F8;
        case InputLogicalKey::F9:           return ImGuiKey_F9;
        case InputLogicalKey::F10:          return ImGuiKey_F10;
        case InputLogicalKey::F11:          return ImGuiKey_F11;
        case InputLogicalKey::F12:          return ImGuiKey_F12;
        case InputLogicalKey::LeftShift:    return ImGuiKey_LeftShift;
        case InputLogicalKey::RightShift:   return ImGuiKey_RightShift;
        case InputLogicalKey::LeftControl:  return ImGuiKey_LeftCtrl;
        case InputLogicalKey::RightControl: return ImGuiKey_RightCtrl;
        case InputLogicalKey::LeftAlt:      return ImGuiKey_LeftAlt;
        case InputLogicalKey::RightAlt:     return ImGuiKey_RightAlt;
        case InputLogicalKey::LeftSuper:    return ImGuiKey_LeftSuper;
        case InputLogicalKey::RightSuper:   return ImGuiKey_RightSuper;
        case InputLogicalKey::Unassigned:
        case InputLogicalKey::Count:        break;
    }
    return ImGuiKey_None;
}

} // namespace

ImGuiInputAdapter::ImGuiInputAdapter(ImGuiIO& TargetIo) noexcept
    : DestinationIo(&TargetIo)
{
}

void ImGuiInputAdapter::IntakeSignalPacket(const InputSignalPacket& Packet) noexcept
{
    if (DestinationIo == nullptr)
    {
        return;
    }

    switch (Packet.Category)
    {
        case InputSignalCategory::PointerMotion:
        {
            DestinationIo->AddMousePosEvent(Packet.PrimaryAxis, Packet.SecondaryAxis);
            break;
        }
        case InputSignalCategory::PointerButton:
        {
            if (Packet.ControlCode < 5u)
            {
                DestinationIo->AddMouseButtonEvent(
                    static_cast<int>(Packet.ControlCode),
                    Packet.Transition != ButtonTransitionCategory::Released);
            }
            break;
        }
        case InputSignalCategory::ScrollWheel:
        {
            DestinationIo->AddMouseWheelEvent(Packet.PrimaryAxis, Packet.SecondaryAxis);
            break;
        }
        case InputSignalCategory::KeyboardKey:
        {
            const ImGuiKey Mapped = TranslateLogicalKeyToImGui(Packet.LogicalKey);
            if (Mapped != ImGuiKey_None)
            {
                DestinationIo->AddKeyEvent(Mapped, Packet.Transition != ButtonTransitionCategory::Released);
            }
            DestinationIo->AddKeyEvent(ImGuiMod_Shift, (Packet.ModifierBitmask & InputModifierBits::Shift) != 0u);
            DestinationIo->AddKeyEvent(ImGuiMod_Ctrl, (Packet.ModifierBitmask & InputModifierBits::Control) != 0u);
            DestinationIo->AddKeyEvent(ImGuiMod_Alt, (Packet.ModifierBitmask & InputModifierBits::Alt) != 0u);
            DestinationIo->AddKeyEvent(ImGuiMod_Super, (Packet.ModifierBitmask & InputModifierBits::Super) != 0u);
            break;
        }
        case InputSignalCategory::TextCodepoint:
        {
            if (Packet.ControlCode >= 0x20u && Packet.ControlCode != 0x7Fu)
            {
                DestinationIo->AddInputCharacter(Packet.ControlCode);
            }
            break;
        }
        case InputSignalCategory::TouchContact:
        case InputSignalCategory::GamepadAxis:
        case InputSignalCategory::GamepadButton:
        case InputSignalCategory::ExternalPathDrop:
        {
            break;
        }
    }
}

std::size_t ImGuiInputAdapter::ForwardEditorExchangeToImGui(InputExtension& Coordinator, ImGuiIO& TargetIo) noexcept
{
    ImGuiInputAdapter Adapter(TargetIo);
    const std::size_t Drained = Coordinator.DrainEditorExchange(Adapter);

    // Keep modifier readings synchronized with consolidated EditorSnapshot even on quiet steps.
    const EditorInputSnapshot& Snapshot = Coordinator.InspectEditorSnapshot();
    TargetIo.AddKeyEvent(ImGuiMod_Shift, Snapshot.ShiftHeld);
    TargetIo.AddKeyEvent(ImGuiMod_Ctrl, Snapshot.ControlHeld);
    TargetIo.AddKeyEvent(ImGuiMod_Alt, Snapshot.AltHeld);
    TargetIo.AddKeyEvent(ImGuiMod_Super, Snapshot.SuperHeld);
    return Drained;
}

} // namespace Frontier::Input
