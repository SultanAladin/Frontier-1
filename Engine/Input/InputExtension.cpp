/*============================================================================================================================================
                                                              INPUTEXTENSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of InputExtension hardware signal aggregation and outbound cursor/haptic directive coordination

#include "Engine/Input/InputExtension.h"

namespace Frontier::Input
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void InputExtension::Initialize() noexcept
{
    Exchange.Initialize();
    HeldScanCodes.fill(false);
    TouchContacts   = {};
    CurrentPointer  = {};
    LifecycleActive = true;
}

void InputExtension::BeginTick() noexcept
{
    CurrentPointer.TickDeltaX   = 0.0f;
    CurrentPointer.TickDeltaY   = 0.0f;
    CurrentPointer.ScrollDeltaX = 0.0f;
    CurrentPointer.ScrollDeltaY = 0.0f;
}

void InputExtension::Finalize() noexcept
{
    Exchange.Finalize();
    HeldScanCodes.fill(false);
    TouchContacts   = {};
    CurrentPointer  = {};
    LifecycleActive = false;
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
    return Exchange.IntakeSignal(Packet);
}

std::size_t InputExtension::IntakeHardwareBatch(std::span<const InputSignalPacket> Packets) noexcept
{
    if (!LifecycleActive)
    {
        return 0;
    }
    return Exchange.IntakeSignalBatch(Packets);
}

void InputExtension::RequestCursorConfinement(CursorConfinementMode Mode) noexcept
{
    if (!LifecycleActive)
    {
        return;
    }
    Exchange.RequestCursorConfinement(Mode);
}

bool InputExtension::RequestHapticFeedback(const HapticFeedbackDirective& Directive) noexcept
{
    if (!LifecycleActive)
    {
        return false;
    }
    return Exchange.QueueHapticFeedback(Directive);
}

std::size_t InputExtension::TickExchange() noexcept
{
    if (!LifecycleActive)
    {
        return 0;
    }

    struct InternalSnapshotSink
    {
        InputExtension* Self;

        void IntakeSignalPacket(const InputSignalPacket& Packet) noexcept
        {
            Self->ApplyPacketToSnapshot(Packet);
        }
    };

    InternalSnapshotSink Sink{ this };
    return Exchange.DrainSignals(Sink);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SNAPSHOT CONSOLIDATION
//------------------------------------------------------------------------------------------------------------------------

void InputExtension::ApplyPacketToSnapshot(const InputSignalPacket& Packet) noexcept
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
            CurrentPointer.PositionX  = Packet.PrimaryAxis;
            CurrentPointer.PositionY  = Packet.SecondaryAxis;
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
            // 📝 Allocate or update matching finger identifier inside the bounded 16-contact slot array.
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
    return Exchange;
}

bool InputExtension::CoordinatorActive() const noexcept
{
    return LifecycleActive;
}

} // namespace Frontier::Input
