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

namespace InputModifierBits
{

inline constexpr uint16_t Shift   = 1u << 0; // [mask] - Left or Right Shift held
inline constexpr uint16_t Control = 1u << 1; // [mask] - Left or Right Control held
inline constexpr uint16_t Alt     = 1u << 2; // [mask] - Left or Right Alt/Option held
inline constexpr uint16_t Super   = 1u << 3; // [mask] - Left or Right OS Super/Command held

} // namespace InputModifierBits

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InputSignalCategory : uint8_t
{
    KeyboardKey      = 0, // [-] - Physical scancode press, repeat, or release
    PointerMotion    = 1, // [-] - Desktop cursor or relative motion delta
    PointerButton    = 2, // [-] - Mouse or stylus button transition
    ScrollWheel      = 3, // [-] - Precision two-axis wheel or trackpad scroll
    TouchContact     = 4, // [-] - Android / touchscreen multi-touch contact sample
    GamepadAxis      = 5, // [-] - Normalized analog stick or trigger deflection
    GamepadButton    = 6, // [-] - Digital gamepad button transition
    TextCodepoint    = 7, // [-] - Translated Unicode UTF-32 character codepoint
    ExternalPathDrop = 8, // [-] - OS external file path drop notification
};

enum class InputLogicalKey : uint8_t
{
    Unassigned   = 0, // [-] - Unmapped or hardware-scancode-only key
    Escape       = 1,
    Enter        = 2,
    KeypadEnter  = 3,
    Tab          = 4,
    Backspace    = 5,
    Delete       = 6,
    Space        = 7,
    LeftArrow    = 8,
    RightArrow   = 9,
    UpArrow      = 10,
    DownArrow    = 11,
    Home         = 12,
    End          = 13,
    PageUp       = 14,
    PageDown     = 15,
    Comma        = 16,
    Period       = 17,
    Slash        = 18,
    Semicolon    = 19,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4,
    Digit5, Digit6, Digit7, Digit8, Digit9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift, RightShift,
    LeftControl, RightControl,
    LeftAlt, RightAlt,
    LeftSuper, RightSuper,
    Count,
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
    uint32_t                 ControlCode          = 0;                                  // [idx] - Scancode, button index, codepoint, or finger ID
    uint16_t                 ModifierBitmask      = 0;                                  // [mask]- Shift/Ctrl/Alt/Super bitfield
    InputSignalCategory      Category             = InputSignalCategory::KeyboardKey;   // [-]   - Discriminator for packet payload
    ButtonTransitionCategory Transition           = ButtonTransitionCategory::Released; // [-]   - Digital switch transition
    TouchContactCategory     TouchPhase           = TouchContactCategory::Began;        // [-]   - Multi-touch lifecycle phase
    InputLogicalKey          LogicalKey           = InputLogicalKey::Unassigned;        // [-]   - Normalized cross-platform logical key
    uint8_t                  ReservedPadding[2]   = { 0, 0 };                           // [B]   - Explicit alignment padding to 48 bytes
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
