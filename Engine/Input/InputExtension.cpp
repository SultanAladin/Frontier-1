/*============================================================================================================================================
                                                              INPUTEXTENSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of InputExtension dual RuntimeInputExchange & EditorInputExchange routing and snapshot consolidation

#include "Engine/Input/InputExtension.h"

namespace Frontier::Input
{

namespace
{

InputExtension* ActiveCoordinatorInstance = nullptr;

void AppendCodepointAsUtf8(uint32_t Codepoint, EditorInputSnapshot& Snapshot) noexcept
{
    if (Codepoint < 0x20u || Codepoint == 0x7Fu || Codepoint > 0x10FFFFu)
    {
        return;
    }

    char     Encoded[4]{};
    uint32_t ByteCount = 0u;
    if (Codepoint < 0x80u)
    {
        Encoded[0] = static_cast<char>(Codepoint);
        ByteCount  = 1u;
    }
    else if (Codepoint < 0x800u)
    {
        Encoded[0] = static_cast<char>(0xC0u | (Codepoint >> 6u));
        Encoded[1] = static_cast<char>(0x80u | (Codepoint & 0x3Fu));
        ByteCount  = 2u;
    }
    else if (Codepoint < 0x10000u)
    {
        Encoded[0] = static_cast<char>(0xE0u | (Codepoint >> 12u));
        Encoded[1] = static_cast<char>(0x80u | ((Codepoint >> 6u) & 0x3Fu));
        Encoded[2] = static_cast<char>(0x80u | (Codepoint & 0x3Fu));
        ByteCount  = 3u;
    }
    else
    {
        Encoded[0] = static_cast<char>(0xF0u | (Codepoint >> 18u));
        Encoded[1] = static_cast<char>(0x80u | ((Codepoint >> 12u) & 0x3Fu));
        Encoded[2] = static_cast<char>(0x80u | ((Codepoint >> 6u) & 0x3Fu));
        Encoded[3] = static_cast<char>(0x80u | (Codepoint & 0x3Fu));
        ByteCount  = 4u;
    }

    if (Snapshot.TypedUtf8Length + ByteCount + 1u <= TypedUtf8BufferCapacity)
    {
        for (uint32_t Offset = 0u; Offset < ByteCount; ++Offset)
        {
            Snapshot.TypedUtf8Buffer[Snapshot.TypedUtf8Length + Offset] = Encoded[Offset];
        }
        Snapshot.TypedUtf8Length += ByteCount;
        Snapshot.TypedUtf8Buffer[Snapshot.TypedUtf8Length] = '\0';
    }

    if (Snapshot.TypedCodepointCount < TypedCodepointSlotCapacity)
    {
        Snapshot.TypedCodepoints[Snapshot.TypedCodepointCount] = Codepoint;
        ++Snapshot.TypedCodepointCount;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

InputExtension::~InputExtension() noexcept
{
    if (LifecycleActive)
    {
        Finalize();
    }
    else if (ActiveCoordinatorInstance == this)
    {
        ActiveCoordinatorInstance = nullptr;
    }
}

void InputExtension::Initialize() noexcept
{
    HardwareIntakeExchange.Initialize();
    RuntimeExchange.Initialize();
    EditorExchange.Initialize();
    HeldScanCodes.fill(false);
    TouchContacts       = {};
    EditorClickHistory  = {};
    CurrentPointer      = {};
    EditorSnapshot      = {};
    PendingDroppedPaths.clear();
    RoutingTarget   = InputRoutingTarget::BothExchanges;
    EditorEnabled   = (FRONTIER_PROFILE_SHIPPING == 0);
    LifecycleActive = true;

    ActiveCoordinatorInstance = this;
}

void InputExtension::BeginTick() noexcept
{
    ClearPerTickEdges();

    // Discard any unconsumed downstream EditorExchange signals from the prior tick so headless loops never saturate.
    if (EditorExchange.PendingSignalCount() > 0u)
    {
        struct DiscardSink
        {
            void IntakeSignalPacket(const InputSignalPacket&) noexcept {}
        };
        DiscardSink Discard{};
        (void)EditorExchange.DrainSignals(Discard);
    }
}

void InputExtension::Finalize() noexcept
{
    HardwareIntakeExchange.Finalize();
    RuntimeExchange.Finalize();
    EditorExchange.Finalize();
    HeldScanCodes.fill(false);
    TouchContacts       = {};
    EditorClickHistory  = {};
    CurrentPointer      = {};
    EditorSnapshot      = {};
    PendingDroppedPaths.clear();
    LifecycleActive = false;

    if (ActiveCoordinatorInstance == this)
    {
        ActiveCoordinatorInstance = nullptr;
    }
}

void InputExtension::ClearPerTickEdges() noexcept
{
    CurrentPointer.TickDeltaX   = 0.0f;
    CurrentPointer.TickDeltaY   = 0.0f;
    CurrentPointer.ScrollDeltaX = 0.0f;
    CurrentPointer.ScrollDeltaY = 0.0f;

    EditorSnapshot.TickDeltaX               = 0.0f;
    EditorSnapshot.TickDeltaY               = 0.0f;
    EditorSnapshot.ScrollDeltaX             = 0.0f;
    EditorSnapshot.ScrollDeltaY             = 0.0f;
    EditorSnapshot.PressedButtonsMask       = 0u;
    EditorSnapshot.ReleasedButtonsMask      = 0u;
    EditorSnapshot.DoubleClickedButtonsMask = 0u;
    EditorSnapshot.PressedLogicalKeys.fill(false);
    EditorSnapshot.RepeatedLogicalKeys.fill(false);
    EditorSnapshot.ReleasedLogicalKeys.fill(false);
    EditorSnapshot.TypedUtf8Buffer[0]  = '\0';
    EditorSnapshot.TypedUtf8Length     = 0u;
    EditorSnapshot.TypedCodepointCount = 0u;
}

void InputExtension::ConfigureRoutingTarget(InputRoutingTarget Target) noexcept
{
    RoutingTarget = Target;
}

InputRoutingTarget InputExtension::QueryRoutingTarget() const noexcept
{
    return RoutingTarget;
}

void InputExtension::ConfigureEditorChannelEnabled(bool Enabled) noexcept
{
    EditorEnabled = Enabled;
}

bool InputExtension::QueryEditorChannelEnabled() const noexcept
{
    return EditorEnabled;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          HARDWARE INTAKE & OUTBOUND REQUESTS
//------------------------------------------------------------------------------------------------------------------------

bool InputExtension::IntakeHardwareSignal(const InputSignalPacket& Packet) noexcept
{
    if (!LifecycleActive)
    {
        return false;
    }
    return HardwareIntakeExchange.IntakeSignal(Packet);
}

std::size_t InputExtension::IntakeHardwareBatch(std::span<const InputSignalPacket> Packets) noexcept
{
    if (!LifecycleActive)
    {
        return 0;
    }
    return HardwareIntakeExchange.IntakeSignalBatch(Packets);
}

void InputExtension::IntakeExternalDroppedPath(std::string_view DroppedPath)
{
    if (!LifecycleActive || DroppedPath.empty())
    {
        return;
    }
    PendingDroppedPaths.emplace_back(DroppedPath);

    InputSignalPacket DropPacket{};
    DropPacket.Category    = InputSignalCategory::ExternalPathDrop;
    DropPacket.ControlCode = static_cast<uint32_t>(PendingDroppedPaths.size() - 1u);
    (void)HardwareIntakeExchange.IntakeSignal(DropPacket);
}

std::vector<std::string> InputExtension::ConsumeDroppedFilePaths()
{
    std::vector<std::string> Harvested = std::move(PendingDroppedPaths);
    PendingDroppedPaths.clear();
    return Harvested;
}

void InputExtension::RequestCursorConfinement(CursorConfinementMode Mode) noexcept
{
    if (!LifecycleActive)
    {
        return;
    }
    RuntimeExchange.RequestCursorConfinement(Mode);
}

bool InputExtension::RequestHapticFeedback(const HapticFeedbackDirective& Directive) noexcept
{
    if (!LifecycleActive)
    {
        return false;
    }
    return RuntimeExchange.QueueHapticFeedback(Directive);
}

void InputExtension::RouteHardwarePacket(const InputSignalPacket& Packet) noexcept
{
    const bool DeliverRuntime = (RoutingTarget != InputRoutingTarget::EditorExchangeOnly);
    const bool DeliverEditor  = EditorEnabled && (RoutingTarget != InputRoutingTarget::RuntimeExchangeOnly);

    if (DeliverRuntime)
    {
        (void)RuntimeExchange.IntakeSignal(Packet);
    }
    if (DeliverEditor)
    {
        (void)EditorExchange.IntakeSignal(Packet);
        ApplyPacketToEditorSnapshot(Packet);
    }
}

std::size_t InputExtension::TickExchange() noexcept
{
    if (!LifecycleActive)
    {
        return 0;
    }

    struct HardwareRouterSink
    {
        InputExtension* Self;

        void IntakeSignalPacket(const InputSignalPacket& Packet) noexcept
        {
            Self->RouteHardwarePacket(Packet);
        }
    };

    HardwareRouterSink Router{ this };
    const std::size_t  RoutedHardwareCount = HardwareIntakeExchange.DrainSignals(Router);

    struct InternalRuntimeSink
    {
        InputExtension* Self;

        void IntakeSignalPacket(const InputSignalPacket& Packet) noexcept
        {
            Self->ApplyPacketToRuntimeSnapshot(Packet);
        }
    };

    InternalRuntimeSink RuntimeSink{ this };
    (void)RuntimeExchange.DrainSignals(RuntimeSink);
    return RoutedHardwareCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SNAPSHOT CONSOLIDATION
//------------------------------------------------------------------------------------------------------------------------

void InputExtension::ApplyPacketToRuntimeSnapshot(const InputSignalPacket& Packet) noexcept
{
    switch (Packet.Category)
    {
        case InputSignalCategory::KeyboardKey:
        {
            if (Packet.ControlCode < MaximumScanCodeCount)
            {
                HeldScanCodes[Packet.ControlCode] = (Packet.Transition != ButtonTransitionCategory::Released);
            }
            break;
        }
        case InputSignalCategory::PointerMotion:
        {
            CurrentPointer.PositionX   = Packet.PrimaryAxis;
            CurrentPointer.PositionY   = Packet.SecondaryAxis;
            CurrentPointer.TickDeltaX += Packet.DeltaPrimary;
            CurrentPointer.TickDeltaY += Packet.DeltaSecondary;
            break;
        }
        case InputSignalCategory::PointerButton:
        {
            if (Packet.ControlCode < 32u)
            {
                const uint32_t ButtonBit = (1u << Packet.ControlCode);
                if (Packet.Transition == ButtonTransitionCategory::Released)
                {
                    CurrentPointer.HeldButtonsMask &= ~ButtonBit;
                }
                else
                {
                    CurrentPointer.HeldButtonsMask |= ButtonBit;
                }
            }
            break;
        }
        case InputSignalCategory::ScrollWheel:
        {
            CurrentPointer.ScrollDeltaX += Packet.PrimaryAxis;
            CurrentPointer.ScrollDeltaY += Packet.SecondaryAxis;
            break;
        }
        case InputSignalCategory::TouchContact:
        {
            TouchContactSample* TargetSlot = nullptr;
            for (TouchContactSample& Candidate : TouchContacts)
            {
                if (Candidate.ContactHeld && Candidate.CoordinateSlot == Packet.ControlCode)
                {
                    TargetSlot = &Candidate;
                    break;
                }
            }

            if (TargetSlot == nullptr &&
                (Packet.TouchPhase == TouchContactCategory::Began || Packet.TouchPhase == TouchContactCategory::Moved))
            {
                for (TouchContactSample& Candidate : TouchContacts)
                {
                    if (!Candidate.ContactHeld)
                    {
                        TargetSlot = &Candidate;
                        break;
                    }
                }
            }

            if (TargetSlot != nullptr)
            {
                TargetSlot->CoordinateSlot   = Packet.ControlCode;
                TargetSlot->SurfacePositionX = Packet.PrimaryAxis;
                TargetSlot->SurfacePositionY = Packet.SecondaryAxis;
                TargetSlot->ContactPressure  = Packet.DeltaPrimary;
                TargetSlot->ContactHeld      = (Packet.TouchPhase != TouchContactCategory::Ended &&
                                                Packet.TouchPhase != TouchContactCategory::Cancelled);
            }
            break;
        }
        case InputSignalCategory::GamepadAxis:
        case InputSignalCategory::GamepadButton:
        case InputSignalCategory::TextCodepoint:
        case InputSignalCategory::ExternalPathDrop:
        {
            break;
        }
    }
}

void InputExtension::ApplyPacketToEditorSnapshot(const InputSignalPacket& Packet) noexcept
{
    switch (Packet.Category)
    {
        case InputSignalCategory::KeyboardKey:
        {
            const auto Slot = static_cast<std::size_t>(Packet.LogicalKey);
            if (Slot > 0u && Slot < LogicalKeySlotCount)
            {
                if (Packet.Transition == ButtonTransitionCategory::Pressed)
                {
                    EditorSnapshot.HeldLogicalKeys[Slot]    = true;
                    EditorSnapshot.PressedLogicalKeys[Slot] = true;
                }
                else if (Packet.Transition == ButtonTransitionCategory::Repeated)
                {
                    EditorSnapshot.HeldLogicalKeys[Slot]     = true;
                    EditorSnapshot.RepeatedLogicalKeys[Slot] = true;
                }
                else
                {
                    EditorSnapshot.HeldLogicalKeys[Slot]     = false;
                    EditorSnapshot.ReleasedLogicalKeys[Slot] = true;
                }
            }

            const bool ExplicitModifiers = (Packet.ModifierBitmask != 0u) ||
                                           (Packet.Transition == ButtonTransitionCategory::Released &&
                                            (Packet.LogicalKey == InputLogicalKey::LeftShift ||
                                             Packet.LogicalKey == InputLogicalKey::RightShift ||
                                             Packet.LogicalKey == InputLogicalKey::LeftControl ||
                                             Packet.LogicalKey == InputLogicalKey::RightControl ||
                                             Packet.LogicalKey == InputLogicalKey::LeftAlt ||
                                             Packet.LogicalKey == InputLogicalKey::RightAlt ||
                                             Packet.LogicalKey == InputLogicalKey::LeftSuper ||
                                             Packet.LogicalKey == InputLogicalKey::RightSuper));
            if (ExplicitModifiers || Packet.LogicalKey != InputLogicalKey::Unassigned)
            {
                EditorSnapshot.ShiftHeld   = (Packet.ModifierBitmask & InputModifierBits::Shift) != 0u ||
                                             EditorSnapshot.QueryKeyHeld(InputLogicalKey::LeftShift) ||
                                             EditorSnapshot.QueryKeyHeld(InputLogicalKey::RightShift);
                EditorSnapshot.ControlHeld = (Packet.ModifierBitmask & InputModifierBits::Control) != 0u ||
                                             EditorSnapshot.QueryKeyHeld(InputLogicalKey::LeftControl) ||
                                             EditorSnapshot.QueryKeyHeld(InputLogicalKey::RightControl);
                EditorSnapshot.AltHeld     = (Packet.ModifierBitmask & InputModifierBits::Alt) != 0u ||
                                             EditorSnapshot.QueryKeyHeld(InputLogicalKey::LeftAlt) ||
                                             EditorSnapshot.QueryKeyHeld(InputLogicalKey::RightAlt);
                EditorSnapshot.SuperHeld   = (Packet.ModifierBitmask & InputModifierBits::Super) != 0u ||
                                             EditorSnapshot.QueryKeyHeld(InputLogicalKey::LeftSuper) ||
                                             EditorSnapshot.QueryKeyHeld(InputLogicalKey::RightSuper);
            }
            break;
        }
        case InputSignalCategory::PointerMotion:
        {
            const float PrevX = EditorSnapshot.PointerSeeded ? EditorSnapshot.PositionX : Packet.PrimaryAxis;
            const float PrevY = EditorSnapshot.PointerSeeded ? EditorSnapshot.PositionY : Packet.SecondaryAxis;
            const float Dx    = (Packet.DeltaPrimary != 0.0f || Packet.DeltaSecondary != 0.0f)
                                    ? Packet.DeltaPrimary
                                    : (Packet.PrimaryAxis - PrevX);
            const float Dy    = (Packet.DeltaPrimary != 0.0f || Packet.DeltaSecondary != 0.0f)
                                    ? Packet.DeltaSecondary
                                    : (Packet.SecondaryAxis - PrevY);

            EditorSnapshot.PositionX     = Packet.PrimaryAxis;
            EditorSnapshot.PositionY     = Packet.SecondaryAxis;
            EditorSnapshot.TickDeltaX   += Dx;
            EditorSnapshot.TickDeltaY   += Dy;
            EditorSnapshot.PointerSeeded = true;
            break;
        }
        case InputSignalCategory::PointerButton:
        {
            if (Packet.ControlCode < 32u)
            {
                const uint32_t ButtonBit = (1u << Packet.ControlCode);
                if (Packet.Transition == ButtonTransitionCategory::Released)
                {
                    EditorSnapshot.HeldButtonsMask     &= ~ButtonBit;
                    EditorSnapshot.ReleasedButtonsMask |= ButtonBit;
                }
                else if (Packet.Transition == ButtonTransitionCategory::Pressed)
                {
                    EditorSnapshot.HeldButtonsMask    |= ButtonBit;
                    EditorSnapshot.PressedButtonsMask |= ButtonBit;

                    if (Packet.ControlCode < EditorClickHistory.size())
                    {
                        ButtonClickHistory& History = EditorClickHistory[Packet.ControlCode];
                        if (History.Seeded && Packet.TimestampNanoseconds > History.LastPressNanoseconds)
                        {
                            const uint64_t ElapsedNs = Packet.TimestampNanoseconds - History.LastPressNanoseconds;
                            const float    DiffX     = EditorSnapshot.PositionX - History.LastPressX;
                            const float    DiffY     = EditorSnapshot.PositionY - History.LastPressY;
                            if (ElapsedNs <= 350'000'000ull && (DiffX * DiffX + DiffY * DiffY) <= 36.0f)
                            {
                                EditorSnapshot.DoubleClickedButtonsMask |= ButtonBit;
                            }
                        }
                        History.LastPressNanoseconds = Packet.TimestampNanoseconds;
                        History.LastPressX           = EditorSnapshot.PositionX;
                        History.LastPressY           = EditorSnapshot.PositionY;
                        History.Seeded               = true;
                    }
                }
            }
            if (Packet.ModifierBitmask != 0u)
            {
                EditorSnapshot.ShiftHeld   = (Packet.ModifierBitmask & InputModifierBits::Shift) != 0u;
                EditorSnapshot.ControlHeld = (Packet.ModifierBitmask & InputModifierBits::Control) != 0u;
                EditorSnapshot.AltHeld     = (Packet.ModifierBitmask & InputModifierBits::Alt) != 0u;
                EditorSnapshot.SuperHeld   = (Packet.ModifierBitmask & InputModifierBits::Super) != 0u;
            }
            break;
        }
        case InputSignalCategory::ScrollWheel:
        {
            EditorSnapshot.ScrollDeltaX += Packet.PrimaryAxis;
            EditorSnapshot.ScrollDeltaY += Packet.SecondaryAxis;
            break;
        }
        case InputSignalCategory::TextCodepoint:
        {
            AppendCodepointAsUtf8(Packet.ControlCode, EditorSnapshot);
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

//------------------------------------------------------------------------------------------------------------------------
//                                                   INSPECTION QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool InputExtension::QueryScanCodeHeld(uint32_t ScanCode) const noexcept
{
    if (ScanCode >= MaximumScanCodeCount)
    {
        return false;
    }
    return HeldScanCodes[ScanCode];
}

const PointerSnapshot& InputExtension::InspectPointerSnapshot() const noexcept
{
    return CurrentPointer;
}

const EditorInputSnapshot& InputExtension::InspectEditorSnapshot() const noexcept
{
    return EditorSnapshot;
}

uint32_t InputExtension::ActiveTouchContactCount() const noexcept
{
    uint32_t ActiveContacts = 0;
    for (const TouchContactSample& Sample : TouchContacts)
    {
        if (Sample.ContactHeld)
        {
            ++ActiveContacts;
        }
    }
    return ActiveContacts;
}

const InputExchange& InputExtension::InspectExchange() const noexcept
{
    return RuntimeExchange;
}

const InputExchange& InputExtension::InspectRuntimeExchange() const noexcept
{
    return RuntimeExchange;
}

const InputExchange& InputExtension::InspectEditorExchange() const noexcept
{
    return EditorExchange;
}

InputExchange& InputExtension::AccessRuntimeExchange() noexcept
{
    return RuntimeExchange;
}

InputExchange& InputExtension::AccessEditorExchange() noexcept
{
    return EditorExchange;
}

bool InputExtension::CoordinatorActive() const noexcept
{
    return LifecycleActive;
}

void InputExtension::AssignActiveCoordinator(InputExtension* Coordinator) noexcept
{
    ActiveCoordinatorInstance = Coordinator;
}

InputExtension* InputExtension::QueryActiveCoordinator() noexcept
{
    return ActiveCoordinatorInstance;
}

const EditorInputSnapshot& InputExtension::InspectActiveEditorSnapshot() noexcept
{
    static const EditorInputSnapshot InertSnapshot{};
    if (ActiveCoordinatorInstance != nullptr && ActiveCoordinatorInstance->LifecycleActive)
    {
        return ActiveCoordinatorInstance->EditorSnapshot;
    }
    return InertSnapshot;
}

} // namespace Frontier::Input
