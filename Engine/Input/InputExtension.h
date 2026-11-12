/*============================================================================================================================================
                                                               INPUTEXTENSION.H
============================================================================================================================================*/
// 🧩 Input lifecycle coordinator: hardware signal intake, per-tick key/pointer/touch snapshots, and outbound OS feedback

#pragma once

#include "Engine/Input/InputExchange.h"

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Frontier::Input
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct TouchContactSample
{
    uint32_t CoordinateSlot   = 0;     // [idx] - Finger or stylus identifier
    float    SurfacePositionX = 0.0f;  // [px]  - Horizontal contact position in window client coordinates
    float    SurfacePositionY = 0.0f;  // [px]  - Vertical contact position in window client coordinates
    float    ContactPressure  = 0.0f;  // [0-1] - Normalized pressure reading
    bool     ContactHeld      = false; // [-]   - True while digitizer contact remains active
};

struct PointerSnapshot
{
    float    PositionX       = 0.0f; // [px]   - Latest pointer horizontal position
    float    PositionY       = 0.0f; // [px]   - Latest pointer vertical position
    float    TickDeltaX      = 0.0f; // [px]   - Accumulated horizontal motion during current tick
    float    TickDeltaY      = 0.0f; // [px]   - Accumulated vertical motion during current tick
    float    ScrollDeltaX    = 0.0f; // [step] - Accumulated horizontal scroll during current tick
    float    ScrollDeltaY    = 0.0f; // [step] - Accumulated vertical scroll during current tick
    uint32_t HeldButtonsMask = 0;    // [mask] - Bitmask of held pointer buttons (bits 0..31)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 LIFECYCLE COORDINATOR
//------------------------------------------------------------------------------------------------------------------------

class InputExtension
{
public:
    InputExtension() noexcept = default;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                FUNCTION PROTOTYPES
    //--------------------------------------------------------------------------------------------------------------------

    void Initialize() noexcept;
    void BeginTick() noexcept;
    void Finalize() noexcept;

    [[nodiscard]] bool        IntakeHardwareSignal(const InputSignalPacket& Packet) noexcept;
    [[nodiscard]] std::size_t IntakeHardwareBatch(std::span<const InputSignalPacket> Packets) noexcept;

    void               RequestCursorConfinement(CursorConfinementMode Mode) noexcept;
    [[nodiscard]] bool RequestHapticFeedback(const HapticFeedbackDirective& Directive) noexcept;

    std::size_t TickExchange() noexcept;

    template <InputSignalSink SinkType>
    std::size_t TickExchangeWithSink(SinkType&& ExternalSink) noexcept;

    template <InputFeedbackTarget TargetType>
    std::size_t FlushOutboundDirectives(TargetType&& OperatingTarget) noexcept;

    [[nodiscard]] bool                   QueryScanCodeHeld(uint32_t ScanCode) const noexcept;
    [[nodiscard]] const PointerSnapshot& InspectPointerSnapshot()             const noexcept;
    [[nodiscard]] uint32_t               ActiveTouchContactCount()            const noexcept;
    [[nodiscard]] const InputExchange&   InspectExchange()                    const noexcept;
    [[nodiscard]] bool                   CoordinatorActive()                  const noexcept;

private:
    void ApplyPacketToSnapshot(const InputSignalPacket& Packet) noexcept;

    InputExchange                                            Exchange{};              // [-]   - Bi-directional OS<->Engine signal & feedback exchange
    std::array<bool, MaximumScanCodeCount>                   HeldScanCodes{};         // [-]   - Direct scancode residency array
    std::array<TouchContactSample, MaximumTouchContactSlots> TouchContacts{};         // [-]   - Active multi-touch contact slots
    PointerSnapshot                                          CurrentPointer{};        // [-]   - Aggregated pointer reading for active tick
    bool                                                     LifecycleActive = false; // [-]   - True between Initialize and Finalize
};

//------------------------------------------------------------------------------------------------------------------------
//                                                TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

template <InputSignalSink SinkType>
std::size_t InputExtension::TickExchangeWithSink(SinkType&& ExternalSink) noexcept
{
    if (!LifecycleActive)
    {
        return 0;
    }

    struct CombinedSink
    {
        InputExtension*                    Self;
        std::remove_reference_t<SinkType>* Downstream;

        void IntakeSignalPacket(const InputSignalPacket& Packet) noexcept
        {
            Self->ApplyPacketToSnapshot(Packet);
            Downstream->IntakeSignalPacket(Packet);
        }
    };

    CombinedSink Relay{ this, &ExternalSink };
    return Exchange.DrainSignals(Relay);
}

template <InputFeedbackTarget TargetType>
std::size_t InputExtension::FlushOutboundDirectives(TargetType&& OperatingTarget) noexcept
{
    if (!LifecycleActive)
    {
        return 0;
    }
    return Exchange.DrainOutboundDirectives(OperatingTarget);
}

} // namespace Frontier::Input
