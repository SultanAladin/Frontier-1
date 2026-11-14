/*============================================================================================================================================
                                                               INPUTEXTENSION.H
============================================================================================================================================*/
// 🧩 Input lifecycle coordinator: hardware signal intake, split RuntimeInputExchange & EditorInputExchange routing, and OS feedback

#pragma once

#include "Engine/Input/InputExchange.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Frontier::Input
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::size_t LogicalKeySlotCount        = static_cast<std::size_t>(InputLogicalKey::Count); // [count] - Total logical key slots
inline constexpr std::size_t TypedUtf8BufferCapacity    = 128;                                              // [B]     - Per-tick UTF-8 text capacity
inline constexpr std::size_t TypedCodepointSlotCapacity = 32;                                               // [count] - Per-tick UTF-32 codepoints

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InputRoutingTarget : uint8_t
{
    BothExchanges       = 0, // [-] - Deliver hardware signals to both RuntimeInputExchange and EditorInputExchange
    RuntimeExchangeOnly = 1, // [-] - Exclusive viewport/simulation capture (RuntimeInputExchange only)
    EditorExchangeOnly  = 2, // [-] - Modal dialogue or text field capture (EditorInputExchange only)
};

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
    float    PositionX       = -1.0e9f; // [px]   - Latest pointer horizontal position (-1e9 until seeded)
    float    PositionY       = -1.0e9f; // [px]   - Latest pointer vertical position (-1e9 until seeded)
    float    TickDeltaX      = 0.0f;    // [px]   - Accumulated horizontal motion during current tick
    float    TickDeltaY      = 0.0f;    // [px]   - Accumulated vertical motion during current tick
    float    ScrollDeltaX    = 0.0f;    // [step] - Accumulated horizontal scroll during current tick
    float    ScrollDeltaY    = 0.0f;    // [step] - Accumulated vertical scroll during current tick
    uint32_t HeldButtonsMask = 0;       // [mask] - Bitmask of held pointer buttons (bits 0..31)
};

struct EditorInputSnapshot
{
    float                                            PositionX                = -1.0e9f; // [px]   - Editor cursor X (-1e9 when unseeded)
    float                                            PositionY                = -1.0e9f; // [px]   - Editor cursor Y (-1e9 when unseeded)
    float                                            TickDeltaX               = 0.0f;    // [px]   - Per-tick cursor delta X
    float                                            TickDeltaY               = 0.0f;    // [px]   - Per-tick cursor delta Y
    float                                            ScrollDeltaX             = 0.0f;    // [step] - Per-tick horizontal wheel steps
    float                                            ScrollDeltaY             = 0.0f;    // [step] - Per-tick vertical wheel steps
    uint32_t                                         HeldButtonsMask          = 0u;      // [mask] - Currently held mouse buttons
    uint32_t                                         PressedButtonsMask       = 0u;      // [mask] - Leading-edge pressed mouse buttons
    uint32_t                                         ReleasedButtonsMask      = 0u;      // [mask] - Trailing-edge released mouse buttons
    uint32_t                                         DoubleClickedButtonsMask = 0u;      // [mask] - Double-click edges this tick
    bool                                             PointerSeeded            = false;   // [-]    - True once cursor coordinates arrive
    bool                                             ShiftHeld                = false;   // [-]    - Shift modifier active
    bool                                             ControlHeld              = false;   // [-]    - Control modifier active
    bool                                             AltHeld                  = false;   // [-]    - Alt modifier active
    bool                                             SuperHeld                = false;   // [-]    - Super/Command modifier active
    std::array<bool, LogicalKeySlotCount>            HeldLogicalKeys{};                  // [-]    - Logical key held residency
    std::array<bool, LogicalKeySlotCount>            PressedLogicalKeys{};               // [-]    - Logical key initial press edge
    std::array<bool, LogicalKeySlotCount>            RepeatedLogicalKeys{};              // [-]    - Logical key OS repeat pulse
    std::array<bool, LogicalKeySlotCount>            ReleasedLogicalKeys{};              // [-]    - Logical key release edge
    std::array<char, TypedUtf8BufferCapacity>        TypedUtf8Buffer{};                  // [B]    - Null-terminated UTF-8 text typed this tick
    uint32_t                                         TypedUtf8Length          = 0u;      // [B]    - Valid UTF-8 byte count
    std::array<uint32_t, TypedCodepointSlotCapacity> TypedCodepoints{};                  // [UTF32]- Codepoints typed this tick
    uint32_t                                         TypedCodepointCount      = 0u;      // [count]- Valid UTF-32 codepoint count

    [[nodiscard]] bool QueryButtonDown(uint32_t ButtonIndex) const noexcept
    {
        return ButtonIndex < 32u && (HeldButtonsMask & (1u << ButtonIndex)) != 0u;
    }

    [[nodiscard]] bool QueryButtonPressed(uint32_t ButtonIndex) const noexcept
    {
        return ButtonIndex < 32u && (PressedButtonsMask & (1u << ButtonIndex)) != 0u;
    }

    [[nodiscard]] bool QueryButtonReleased(uint32_t ButtonIndex) const noexcept
    {
        return ButtonIndex < 32u && (ReleasedButtonsMask & (1u << ButtonIndex)) != 0u;
    }

    [[nodiscard]] bool QueryButtonDoubleClicked(uint32_t ButtonIndex) const noexcept
    {
        return ButtonIndex < 32u && (DoubleClickedButtonsMask & (1u << ButtonIndex)) != 0u;
    }

    [[nodiscard]] bool QueryKeyHeld(InputLogicalKey Key) const noexcept
    {
        const auto Slot = static_cast<std::size_t>(Key);
        return Slot > 0u && Slot < LogicalKeySlotCount && HeldLogicalKeys[Slot];
    }

    [[nodiscard]] bool QueryKeyPressed(InputLogicalKey Key, bool EnableRepeat = false) const noexcept
    {
        const auto Slot = static_cast<std::size_t>(Key);
        if (Slot == 0u || Slot >= LogicalKeySlotCount)
        {
            return false;
        }
        return PressedLogicalKeys[Slot] || (EnableRepeat && RepeatedLogicalKeys[Slot]);
    }

    [[nodiscard]] bool QueryKeyReleased(InputLogicalKey Key) const noexcept
    {
        const auto Slot = static_cast<std::size_t>(Key);
        return Slot > 0u && Slot < LogicalKeySlotCount && ReleasedLogicalKeys[Slot];
    }

    [[nodiscard]] std::string_view QueryTypedUtf8() const noexcept
    {
        return std::string_view(TypedUtf8Buffer.data(), TypedUtf8Length);
    }

    [[nodiscard]] std::span<const uint32_t> QueryTypedCodepoints() const noexcept
    {
        return std::span<const uint32_t>(TypedCodepoints.data(), TypedCodepointCount);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 LIFECYCLE COORDINATOR
//------------------------------------------------------------------------------------------------------------------------

class InputExtension
{
public:
    InputExtension() noexcept = default;
    ~InputExtension() noexcept;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                FUNCTION PROTOTYPES
    //--------------------------------------------------------------------------------------------------------------------

    void Initialize() noexcept;
    void BeginTick() noexcept;
    void Finalize() noexcept;

    void                              ConfigureRoutingTarget(InputRoutingTarget Target) noexcept;
    [[nodiscard]] InputRoutingTarget  QueryRoutingTarget() const noexcept;
    void                              ConfigureEditorChannelEnabled(bool Enabled) noexcept;
    [[nodiscard]] bool                QueryEditorChannelEnabled() const noexcept;

    [[nodiscard]] bool        IntakeHardwareSignal(const InputSignalPacket& Packet) noexcept;
    [[nodiscard]] std::size_t IntakeHardwareBatch(std::span<const InputSignalPacket> Packets) noexcept;
    void                      IntakeExternalDroppedPath(std::string_view DroppedPath);
    [[nodiscard]] std::vector<std::string> ConsumeDroppedFilePaths();

    void               RequestCursorConfinement(CursorConfinementMode Mode) noexcept;
    [[nodiscard]] bool RequestHapticFeedback(const HapticFeedbackDirective& Directive) noexcept;

    std::size_t TickExchange() noexcept;

    template <InputSignalSink SinkType>
    std::size_t TickExchangeWithSink(SinkType&& ExternalRuntimeSink) noexcept;

    template <InputSignalSink SinkType>
    std::size_t DrainEditorExchange(SinkType&& DownstreamEditorSink) noexcept;

    template <InputFeedbackTarget TargetType>
    std::size_t FlushOutboundDirectives(TargetType&& OperatingTarget) noexcept;

    [[nodiscard]] bool                       QueryScanCodeHeld(uint32_t ScanCode) const noexcept;
    [[nodiscard]] const PointerSnapshot&     InspectPointerSnapshot()             const noexcept;
    [[nodiscard]] const EditorInputSnapshot& InspectEditorSnapshot()              const noexcept;
    [[nodiscard]] uint32_t                   ActiveTouchContactCount()            const noexcept;
    [[nodiscard]] const InputExchange&       InspectExchange()                    const noexcept;
    [[nodiscard]] const InputExchange&       InspectRuntimeExchange()             const noexcept;
    [[nodiscard]] const InputExchange&       InspectEditorExchange()              const noexcept;
    [[nodiscard]] InputExchange&             AccessRuntimeExchange()              noexcept;
    [[nodiscard]] InputExchange&             AccessEditorExchange()               noexcept;
    [[nodiscard]] bool                       CoordinatorActive()                  const noexcept;

    static void                              AssignActiveCoordinator(InputExtension* Coordinator) noexcept;
    [[nodiscard]] static InputExtension*     QueryActiveCoordinator() noexcept;
    [[nodiscard]] static const EditorInputSnapshot& InspectActiveEditorSnapshot() noexcept;

private:
    void ClearPerTickEdges() noexcept;
    void RouteHardwarePacket(const InputSignalPacket& Packet) noexcept;
    void ApplyPacketToRuntimeSnapshot(const InputSignalPacket& Packet) noexcept;
    void ApplyPacketToEditorSnapshot(const InputSignalPacket& Packet) noexcept;

    struct ButtonClickHistory
    {
        uint64_t LastPressNanoseconds = 0u;
        float    LastPressX           = 0.0f;
        float    LastPressY           = 0.0f;
        bool     Seeded               = false;
    };

    InputExchange                                            HardwareIntakeExchange{}; // [-]   - Raw OS->Coordinator intake ring
    InputExchange                                            RuntimeExchange{};        // [-]   - Runtime game/simulation/camera stream
    InputExchange                                            EditorExchange{};         // [-]   - Editor UI/authoring/ImGui stream
    std::array<bool, MaximumScanCodeCount>                   HeldScanCodes{};          // [-]   - Runtime scancode residency array
    std::array<TouchContactSample, MaximumTouchContactSlots> TouchContacts{};          // [-]   - Active multi-touch contact slots
    std::array<ButtonClickHistory, 5>                        EditorClickHistory{};     // [-]   - Multi-button double-click tracker
    PointerSnapshot                                          CurrentPointer{};         // [-]   - Aggregated runtime pointer reading
    EditorInputSnapshot                                      EditorSnapshot{};         // [-]   - Consolidated per-tick Editor reading
    std::vector<std::string>                                 PendingDroppedPaths{};    // [-]   - External file drops awaiting intake
    InputRoutingTarget                                       RoutingTarget   = InputRoutingTarget::BothExchanges;
    bool                                                     EditorEnabled   = (FRONTIER_PROFILE_SHIPPING == 0);
    bool                                                     LifecycleActive = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

template <InputSignalSink SinkType>
std::size_t InputExtension::TickExchangeWithSink(SinkType&& ExternalRuntimeSink) noexcept
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
    (void)HardwareIntakeExchange.DrainSignals(Router);

    struct RuntimeConsolidatingSink
    {
        InputExtension*                    Self;
        std::remove_reference_t<SinkType>* Downstream;

        void IntakeSignalPacket(const InputSignalPacket& Packet) noexcept
        {
            Self->ApplyPacketToRuntimeSnapshot(Packet);
            Downstream->IntakeSignalPacket(Packet);
        }
    };

    RuntimeConsolidatingSink Relay{ this, &ExternalRuntimeSink };
    return RuntimeExchange.DrainSignals(Relay);
}

template <InputSignalSink SinkType>
std::size_t InputExtension::DrainEditorExchange(SinkType&& DownstreamEditorSink) noexcept
{
    if (!LifecycleActive || !EditorEnabled)
    {
        return 0;
    }
    return EditorExchange.DrainSignals(std::forward<SinkType>(DownstreamEditorSink));
}

template <InputFeedbackTarget TargetType>
std::size_t InputExtension::FlushOutboundDirectives(TargetType&& OperatingTarget) noexcept
{
    if (!LifecycleActive)
    {
        return 0;
    }
    std::size_t Delivered = RuntimeExchange.DrainOutboundDirectives(OperatingTarget);
    if (EditorEnabled)
    {
        Delivered += EditorExchange.DrainOutboundDirectives(OperatingTarget);
    }
    return Delivered;
}

} // namespace Frontier::Input
