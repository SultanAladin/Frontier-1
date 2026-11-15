/*============================================================================================================================================
                                                           REMOTEDIRECTIVEROUTER.H
============================================================================================================================================*/
// 🧩 5-byte quantized input directives with 3-tick rolling loss redundancy plus reliable ordered workspace/session directive ring

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t QuantizedDirectiveWireBytes   = 5u;   // [B]     - Exact wire size per single-tick input directive
inline constexpr uint32_t RollingDirectiveRedundancy    = 3u;   // [count] - Consecutive ticks bundled per unreliable datagram
inline constexpr uint32_t DirectiveHistoryRingCapacity  = 128u; // [count] - Bounded tick ring for prediction/reconciliation

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DiscreteTriggerMask : uint8_t
{
    None                = 0u,      // [mask] - Zero digital triggers active
    EnablePrimaryBrake  = 1u << 0, // [mask] - Mechanical / emergency lock trigger
    EnableUpshift       = 1u << 1, // [mask] - Sequential transmission upshift pulse
    EnableDownshift     = 1u << 2, // [mask] - Sequential transmission downshift pulse
    EnableAuxiliaryOne  = 1u << 3, // [mask] - Domain auxiliary trigger 1
    EnableAuxiliaryTwo  = 1u << 4, // [mask] - Domain auxiliary trigger 2
    EnableResetPose     = 1u << 5, // [mask] - Deterministic recovery request
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct QuantizedInputDirective
{
    float   SteeringNormalized  = 0.0f; // [-1..+1] - Signed lateral steering command
    float   ThrottleNormalized  = 0.0f; // [0..1]   - Unsigned forward propulsion command
    float   BrakeNormalized     = 0.0f; // [0..1]   - Unsigned retardation command
    uint8_t TriggerMask         = 0u;   // [mask]   - DiscreteTriggerMask bits
};

struct ReliableDirectiveRecord
{
    uint32_t             DirectiveSequence = 0u; // [-] - Strictly monotonic sequence number
    uint16_t             DirectiveOpcode   = 0u; // [-] - Domain / workspace command opcode
    std::vector<uint8_t> ParameterBytes    {};   // [-] - Compact binary arguments
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::array<uint8_t, QuantizedDirectiveWireBytes> PackInputDirective5B(const QuantizedInputDirective& Directive) noexcept;
[[nodiscard]] QuantizedInputDirective                          UnpackInputDirective5B(std::span<const uint8_t, QuantizedDirectiveWireBytes> WireBytes) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class InputDirectiveStream final
{
public:
    InputDirectiveStream() = default;

    void               RecordTickDirective(uint32_t SimulationTick, const QuantizedInputDirective& Directive) noexcept;
    [[nodiscard]] bool BuildRedundantPacket(std::vector<uint8_t>& OutPayload) const;
    [[nodiscard]] bool IngestRedundantPacket(std::span<const uint8_t> Payload, uint32_t& OutLatestTick) noexcept;
    [[nodiscard]] bool QueryDirectiveAtTick(uint32_t SimulationTick, QuantizedInputDirective& OutDirective) const noexcept;
    void               ClearStream() noexcept;

private:
    struct SlotEntry
    {
        bool                    Present        = false;
        uint32_t                SimulationTick = 0u;
        QuantizedInputDirective Directive      {};
    };

    std::array<SlotEntry, DirectiveHistoryRingCapacity> Ring           {};
    uint32_t                                            HighestTick    = 0u;
    bool                                                StreamSeeded   = false;
};

class ReliableDirectiveRouter final
{
public:
    ReliableDirectiveRouter() = default;

    [[nodiscard]] uint32_t EnqueueDirective(uint16_t DirectiveOpcode, std::span<const uint8_t> ParameterBytes);
    [[nodiscard]] bool     BuildOutboundBatch(std::vector<uint8_t>& OutPayload, uint32_t MaxPayloadBytes = 960u) const;
    void                   AcknowledgeUpToSequence(uint32_t AckedDirectiveSequence) noexcept;

    [[nodiscard]] bool     IngestInboundBatch(
        std::span<const uint8_t>              Payload,
        std::vector<ReliableDirectiveRecord>& OutDeliveredInOrder);

    [[nodiscard]] uint32_t QueryHighestContiguousInbound() const noexcept { return ExpectedInboundSequence - 1u; }
    [[nodiscard]] uint32_t QueryPendingOutboundCount() const noexcept { return static_cast<uint32_t>(PendingOutbound.size()); }
    void                   ResetRouter() noexcept;

private:
    std::vector<ReliableDirectiveRecord> PendingOutbound         {};
    std::vector<ReliableDirectiveRecord> OutOfOrderHoldback      {};
    uint32_t                             NextOutboundSequence    = 1u;
    uint32_t                             ExpectedInboundSequence = 1u;
};

} // namespace Frontier::NetworkingSpace
