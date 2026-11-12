/*============================================================================================================================================
                                                               INPUTEXCHANGE.H
============================================================================================================================================*/
// 🧩 Bi-directional OS-to-Engine hardware signal ring intake and Engine-to-OS cursor confinement & haptic feedback exchange

#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Frontier::Input
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::size_t DefaultSignalQueueCapacity   = 1024; // [count] - Power-of-two inbound signal ring capacity
inline constexpr std::size_t DefaultFeedbackQueueCapacity = 64;   // [count] - Power-of-two outbound haptic ring capacity
inline constexpr uint32_t    MaximumScanCodeCount         = 512;  // [count] - Bounded hardware keyboard scancode ceiling
inline constexpr uint32_t    MaximumTouchContactSlots     = 16;   // [count] - Simultaneous multi-touch contact ceiling

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InputSignalCategory : uint8_t
{
    KeyboardKey   = 0, // [-] - Physical scancode press, repeat, or release
    PointerMotion = 1, // [-] - Desktop cursor or relative motion delta
    PointerButton = 2, // [-] - Mouse or stylus button transition
    ScrollWheel   = 3, // [-] - Precision two-axis wheel or trackpad scroll
    TouchContact  = 4, // [-] - Android / touchscreen multi-touch contact sample
    GamepadAxis   = 5, // [-] - Normalized analog stick or trigger deflection
    GamepadButton = 6, // [-] - Digital gamepad button transition
};

enum class ButtonTransitionCategory : uint8_t
{
    Released = 0, // [-] - Contact or switch opened
    Pressed  = 1, // [-] - Initial actuation edge
    Repeated = 2, // [-] - OS auto-repeat pulse while held
};

enum class TouchContactCategory : uint8_t
{
    Began      = 0, // [-] - Finger or stylus touched surface
    Moved      = 1, // [-] - Active contact translated on surface
    Stationary = 2, // [-] - Contact held without translation
    Ended      = 3, // [-] - Finger or stylus lifted cleanly
    Cancelled  = 4, // [-] - Gesture interrupted by OS surface loss
};

enum class CursorConfinementMode : uint8_t
{
    FreeVisible          = 0, // [-] - Standard OS cursor visible and unrestricted
    ConfinedVisible      = 1, // [-] - Visible cursor clamped to client window bounds
    LockedRelativeHidden = 2, // [-] - Hidden cursor with raw relative delta capture (3D viewport fly/orbit)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) InputSignalPacket
{
    uint64_t                 TimestampNanoseconds = 0;                                  // [ns]  - Monotonic hardware sample timestamp
    float                    PrimaryAxis          = 0.0f;                               // [px]  - Cursor X, scroll X, touch X, or axis deflection
    float                    SecondaryAxis        = 0.0f;                               // [px]  - Cursor Y, scroll Y, touch Y, or secondary deflection
    float                    DeltaPrimary         = 0.0f;                               // [px]  - Relative X motion or contact pressure [0-1]
    float                    DeltaSecondary       = 0.0f;                               // [px]  - Relative Y motion or contact radius [px]
    uint32_t                 DeviceIdentifier     = 0;                                  // [idx] - Originating peripheral or touch digitizer ID
    uint32_t                 ControlCode          = 0;                                  // [idx] - Scancode, button index, axis index, or finger ID
    uint16_t                 ModifierBitmask      = 0;                                  // [mask]- Shift/Ctrl/Alt/Super bitfield
    InputSignalCategory      Category             = InputSignalCategory::KeyboardKey;   // [-]   - Discriminator for packet payload
    ButtonTransitionCategory Transition           = ButtonTransitionCategory::Released; // [-]   - Digital switch transition
    TouchContactCategory     TouchPhase           = TouchContactCategory::Began;        // [-]   - Multi-touch lifecycle phase
    uint8_t                  ReservedPadding[3]   = { 0, 0, 0 };                        // [B]   - Explicit alignment padding to 48 bytes
};

static_assert(sizeof(InputSignalPacket) == 48, "InputSignalPacket size must remain 48 bytes across all target architectures.");
static_assert(alignof(InputSignalPacket) == 16, "InputSignalPacket alignment must remain 16 bytes.");

struct alignas(16) HapticFeedbackDirective
{
    uint32_t DeviceIdentifier       = 0;    // [idx] - Target gamepad or mobile haptic actuator ID
    float    LowFrequencyAmplitude  = 0.0f; // [0-1] - Heavy rotor / low-band vibration intensity
    float    HighFrequencyAmplitude = 0.0f; // [0-1] - Crisp actuator / high-band intensity
    uint32_t DurationMilliseconds   = 0;    // [ms]  - Actuation duration before automatic cutoff
};

static_assert(sizeof(HapticFeedbackDirective) == 16, "HapticFeedbackDirective size must remain 16 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename ReceiverType>
concept InputSignalSink = requires(ReceiverType& Receiver, const InputSignalPacket& Packet)
{
    { Receiver.IntakeSignalPacket(Packet) } noexcept -> std::same_as<void>;
};

template <typename FeedbackReceiverType>
concept InputFeedbackTarget = requires(
    FeedbackReceiverType&          Target,
    CursorConfinementMode          Confinement,
    const HapticFeedbackDirective& Haptic)
{
    { Target.ApplyCursorConfinement(Confinement) } noexcept -> std::same_as<void>;
    { Target.ApplyHapticFeedback(Haptic) }         noexcept -> std::same_as<void>;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   EXCHANGE STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

class InputExchange
{
public:
    InputExchange() noexcept = default;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                FUNCTION PROTOTYPES
    //--------------------------------------------------------------------------------------------------------------------

    void Initialize() noexcept;
    void Reset() noexcept;
    void Finalize() noexcept;

    // OS -> Engine inbound signal intake
    [[nodiscard]] bool        IntakeSignal(const InputSignalPacket& Packet) noexcept;
    [[nodiscard]] std::size_t IntakeSignalBatch(std::span<const InputSignalPacket> Packets) noexcept;

    // Engine -> OS outbound cursor & haptic directives
    void               RequestCursorConfinement(CursorConfinementMode RequestedMode) noexcept;
    [[nodiscard]] bool QueueHapticFeedback(const HapticFeedbackDirective& Directive) noexcept;

    [[nodiscard]] std::size_t           PendingSignalCount()      const noexcept;
    [[nodiscard]] std::size_t           PendingHapticCount()      const noexcept;
    [[nodiscard]] CursorConfinementMode ActiveCursorConfinement() const noexcept;
    [[nodiscard]] uint64_t              DroppedSignalCount()      const noexcept;
    [[nodiscard]] uint64_t              ExchangedSignalTotal()    const noexcept;

    template <InputSignalSink SinkType>
    std::size_t DrainSignals(SinkType&& DestinationSink) noexcept;

    template <InputFeedbackTarget TargetType>
    std::size_t DrainOutboundDirectives(TargetType&& OperatingTarget) noexcept;

private:
    std::array<InputSignalPacket, DefaultSignalQueueCapacity>         SignalRing{};                                       // [-]     - Inbound OS->Engine signal ring
    std::array<HapticFeedbackDirective, DefaultFeedbackQueueCapacity> HapticRing{};                                       // [-]     - Outbound Engine->OS haptic ring
    std::size_t                                                       SignalReadCursor    = 0;                            // [idx]   - Next inbound slot to drain
    std::size_t                                                       SignalWriteCursor   = 0;                            // [idx]   - Next inbound slot to write
    std::size_t                                                       QueuedSignals       = 0;                            // [count] - Resident inbound signals
    std::size_t                                                       HapticReadCursor    = 0;                            // [idx]   - Next outbound haptic slot to drain
    std::size_t                                                       HapticWriteCursor   = 0;                            // [idx]   - Next outbound haptic slot to write
    std::size_t                                                       QueuedHaptics       = 0;                            // [count] - Resident outbound haptic directives
    uint64_t                                                          DroppedSignals      = 0;                            // [count] - Inbound packets rejected when full
    uint64_t                                                          TotalDrainedSignals = 0;                            // [count] - Cumulative inbound signals drained
    CursorConfinementMode                                             DesiredConfinement  = CursorConfinementMode::FreeVisible; // [-] - Desired cursor lock mode
    bool                                                              ConfinementDirty    = false;                        // [-]     - True when OS cursor mode needs sync
    bool                                                              InitializedActive   = false;                        // [-]     - Lifecycle activation guard
};

//------------------------------------------------------------------------------------------------------------------------
//                                                TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

template <InputSignalSink SinkType>
std::size_t InputExchange::DrainSignals(SinkType&& DestinationSink) noexcept
{
    if (!InitializedActive || QueuedSignals == 0)
    {
        return 0;
    }

    const std::size_t ToDrainCount = QueuedSignals;
    for (std::size_t StepIndex = 0; StepIndex < ToDrainCount; ++StepIndex)
    {
        DestinationSink.IntakeSignalPacket(SignalRing[SignalReadCursor]);
        SignalReadCursor = (SignalReadCursor + 1) & (DefaultSignalQueueCapacity - 1);
    }

    QueuedSignals        = 0;
    TotalDrainedSignals += ToDrainCount;
    return ToDrainCount;
}

template <InputFeedbackTarget TargetType>
std::size_t InputExchange::DrainOutboundDirectives(TargetType&& OperatingTarget) noexcept
{
    if (!InitializedActive)
    {
        return 0;
    }

    std::size_t DeliveredDirectives = 0;
    if (ConfinementDirty)
    {
        OperatingTarget.ApplyCursorConfinement(DesiredConfinement);
        ConfinementDirty = false;
        ++DeliveredDirectives;
    }

    const std::size_t ToDrainHaptics = QueuedHaptics;
    for (std::size_t StepIndex = 0; StepIndex < ToDrainHaptics; ++StepIndex)
    {
        OperatingTarget.ApplyHapticFeedback(HapticRing[HapticReadCursor]);
        HapticReadCursor = (HapticReadCursor + 1) & (DefaultFeedbackQueueCapacity - 1);
    }

    QueuedHaptics        = 0;
    DeliveredDirectives += ToDrainHaptics;
    return DeliveredDirectives;
}

} // namespace Frontier::Input
