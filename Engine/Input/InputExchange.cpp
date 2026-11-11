/*============================================================================================================================================
                                                      INPUTEXCHANGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of bi-directional OS-to-Engine signal queueing and Engine-to-OS cursor/haptic directive exchange

#include "Engine/Input/InputExchange.h"

namespace Frontier::Input
{

static_assert((DefaultSignalQueueCapacity & (DefaultSignalQueueCapacity - 1)) == 0,
              "DefaultSignalQueueCapacity must be a power of two for bitmask wraparound.");
static_assert((DefaultFeedbackQueueCapacity & (DefaultFeedbackQueueCapacity - 1)) == 0,
              "DefaultFeedbackQueueCapacity must be a power of two for bitmask wraparound.");

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void InputExchange::Initialize() noexcept
{
    SignalReadCursor    = 0;
    SignalWriteCursor   = 0;
    QueuedSignals       = 0;
    HapticReadCursor    = 0;
    HapticWriteCursor   = 0;
    QueuedHaptics       = 0;
    DroppedSignals      = 0;
    TotalDrainedSignals = 0;
    DesiredConfinement  = CursorConfinementMode::FreeVisible;
    ConfinementDirty    = false;
    InitializedActive   = true;
}

void InputExchange::Reset() noexcept
{
    SignalReadCursor  = 0;
    SignalWriteCursor = 0;
    QueuedSignals     = 0;
    HapticReadCursor  = 0;
    HapticWriteCursor = 0;
    QueuedHaptics     = 0;
    ConfinementDirty  = false;
}

void InputExchange::Finalize() noexcept
{
    Reset();
    DesiredConfinement = CursorConfinementMode::FreeVisible;
    InitializedActive  = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              INBOUND SIGNAL INTAKE (OS -> ENGINE)
//------------------------------------------------------------------------------------------------------------------------

bool InputExchange::IntakeSignal(const InputSignalPacket& Packet) noexcept
{
    if (!InitializedActive)
    {
        return false;
    }

    if (QueuedSignals >= DefaultSignalQueueCapacity)
    {
        ++DroppedSignals;
        return false;
    }

    SignalRing[SignalWriteCursor] = Packet;
    SignalWriteCursor             = (SignalWriteCursor + 1) & (DefaultSignalQueueCapacity - 1);
    ++QueuedSignals;
    return true;
}

std::size_t InputExchange::IntakeSignalBatch(std::span<const InputSignalPacket> Packets) noexcept
{
    std::size_t AcceptedCount = 0;
    for (const InputSignalPacket& Packet : Packets)
    {
        if (!IntakeSignal(Packet))
        {
            break;
        }
        ++AcceptedCount;
    }
    return AcceptedCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           OUTBOUND DIRECTIVES (ENGINE -> OS)
//------------------------------------------------------------------------------------------------------------------------

void InputExchange::RequestCursorConfinement(CursorConfinementMode RequestedMode) noexcept
{
    if (!InitializedActive)
    {
        return;
    }

    if (DesiredConfinement != RequestedMode)
    {
        DesiredConfinement = RequestedMode;
        ConfinementDirty   = true;
    }
}

bool InputExchange::QueueHapticFeedback(const HapticFeedbackDirective& Directive) noexcept
{
    if (!InitializedActive || QueuedHaptics >= DefaultFeedbackQueueCapacity)
    {
        return false;
    }

    HapticRing[HapticWriteCursor] = Directive;
    HapticWriteCursor             = (HapticWriteCursor + 1) & (DefaultFeedbackQueueCapacity - 1);
    ++QueuedHaptics;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INSPECTION QUERIES
//------------------------------------------------------------------------------------------------------------------------

std::size_t InputExchange::PendingSignalCount() const noexcept
{
    return QueuedSignals;
}

std::size_t InputExchange::PendingHapticCount() const noexcept
{
    return QueuedHaptics;
}

CursorConfinementMode InputExchange::ActiveCursorConfinement() const noexcept
{
    return DesiredConfinement;
}

uint64_t InputExchange::DroppedSignalCount() const noexcept
{
    return DroppedSignals;
}

uint64_t InputExchange::ExchangedSignalTotal() const noexcept
{
    return TotalDrainedSignals;
}

} // namespace Frontier::Input
