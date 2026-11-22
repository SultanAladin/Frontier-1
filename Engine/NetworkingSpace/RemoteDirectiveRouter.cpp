/*============================================================================================================================================
                                                          REMOTEDIRECTIVEROUTER.CPP
============================================================================================================================================*/
// 🧩 5-byte input quantization, 3-tick packet-loss healing redundancy ring, and reliable in-order command directive delivery

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/RemoteDirectiveRouter.h"
#include "Engine/NetworkingSpace/PacketSerializer.h"

#include <algorithm>
#include <cmath>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 5-BYTE DIRECTIVE PACKING
//------------------------------------------------------------------------------------------------------------------------

std::array<uint8_t, QuantizedDirectiveWireBytes> PackInputDirective5B(const QuantizedInputDirective& Directive) noexcept
{
    const float ClampedSteer    = std::clamp(Directive.SteeringNormalized, -1.0f, 1.0f);
    const float ClampedThrottle = std::clamp(Directive.ThrottleNormalized, 0.0f, 1.0f);
    const float ClampedBrake    = std::clamp(Directive.BrakeNormalized, 0.0f, 1.0f);

    const int16_t  SignedSteer = static_cast<int16_t>(std::lround(ClampedSteer * 32767.0f));
    const uint16_t RawSteer    = static_cast<uint16_t>(SignedSteer);
    const uint8_t  RawThrottle = static_cast<uint8_t>(std::lround(ClampedThrottle * 255.0f));
    const uint8_t  RawBrake    = static_cast<uint8_t>(std::lround(ClampedBrake * 255.0f));

    return {{
        static_cast<uint8_t>(RawSteer & 0xFFu),
        static_cast<uint8_t>((RawSteer >> 8u) & 0xFFu),
        RawThrottle,
        RawBrake,
        Directive.TriggerMask
    }};
}

QuantizedInputDirective UnpackInputDirective5B(std::span<const uint8_t, QuantizedDirectiveWireBytes> WireBytes) noexcept
{
    const uint16_t RawSteer    = static_cast<uint16_t>(WireBytes[0]) |
                                 static_cast<uint16_t>(static_cast<uint16_t>(WireBytes[1]) << 8u);
    const int16_t  SignedSteer = static_cast<int16_t>(RawSteer);

    QuantizedInputDirective Result{};
    Result.SteeringNormalized = std::clamp(static_cast<float>(SignedSteer) / 32767.0f, -1.0f, 1.0f);
    Result.ThrottleNormalized = static_cast<float>(WireBytes[2]) / 255.0f;
    Result.BrakeNormalized    = static_cast<float>(WireBytes[3]) / 255.0f;
    Result.TriggerMask        = WireBytes[4];
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              3-TICK REDUNDANT INPUT STREAM
//------------------------------------------------------------------------------------------------------------------------

void InputDirectiveStream::RecordTickDirective(uint32_t SimulationTick, const QuantizedInputDirective& Directive) noexcept
{
    const std::size_t SlotIndex   = SimulationTick % DirectiveHistoryRingCapacity;
    const auto CanonicalWire      = PackInputDirective5B(Directive);
    Ring[SlotIndex].Present       = true;
    Ring[SlotIndex].SimulationTick= SimulationTick;
    Ring[SlotIndex].Directive     = UnpackInputDirective5B(CanonicalWire);

    if (!StreamSeeded || SimulationTick >= HighestTick)
    {
        HighestTick  = SimulationTick;
        StreamSeeded = true;
    }
}

bool InputDirectiveStream::BuildRedundantPacket(std::vector<uint8_t>& OutPayload) const
{
    OutPayload.clear();
    if (!StreamSeeded)
    {
        return false;
    }

    uint8_t BundledCount = 0u;
    std::array<std::array<uint8_t, QuantizedDirectiveWireBytes>, RollingDirectiveRedundancy> PackedSlices{};

    for (uint32_t Offset = 0u; Offset < RollingDirectiveRedundancy; ++Offset)
    {
        if (HighestTick < Offset)
        {
            break;
        }
        const uint32_t CandidateTick = HighestTick - Offset;
        QuantizedInputDirective Found{};
        if (!QueryDirectiveAtTick(CandidateTick, Found))
        {
            break;
        }
        PackedSlices[BundledCount] = PackInputDirective5B(Found);
        ++BundledCount;
    }

    if (BundledCount == 0u)
    {
        return false;
    }

    // Wire layout: AnchorHighestTick32 (4B) + BundledCount (1B) + N * 5B = max 20 bytes total
    OutPayload.reserve(5u + static_cast<std::size_t>(BundledCount) * QuantizedDirectiveWireBytes);
    AppendLittleEndian32(OutPayload, HighestTick);
    OutPayload.push_back(BundledCount);
    for (uint8_t Index = 0u; Index < BundledCount; ++Index)
    {
        OutPayload.insert(OutPayload.end(), PackedSlices[Index].begin(), PackedSlices[Index].end());
    }
    return true;
}

bool InputDirectiveStream::IngestRedundantPacket(std::span<const uint8_t> Payload, uint32_t& OutLatestTick) noexcept
{
    if (Payload.size() < 5u + QuantizedDirectiveWireBytes)
    {
        return false;
    }

    std::size_t Cursor   = 0u;
    uint32_t AnchorTick  = 0u;
    if (!ReadLittleEndian32(Payload, Cursor, AnchorTick))
    {
        return false;
    }

    const uint8_t BundledCount = Payload[Cursor++];
    if (BundledCount == 0u ||
        BundledCount > RollingDirectiveRedundancy ||
        Cursor + static_cast<std::size_t>(BundledCount) * QuantizedDirectiveWireBytes != Payload.size())
    {
        return false;
    }

    // Insert oldest-to-newest so ring watermark advances monotonically
    for (uint32_t Reverse = BundledCount; Reverse > 0u; --Reverse)
    {
        const uint32_t Offset = Reverse - 1u;
        if (AnchorTick < Offset)
        {
            continue;
        }
        const uint32_t TickIndex      = AnchorTick - Offset;
        const std::size_t SliceOffset = Cursor + static_cast<std::size_t>(Offset) * QuantizedDirectiveWireBytes;

        QuantizedInputDirective Existing{};
        if (!QueryDirectiveAtTick(TickIndex, Existing))
        {
            std::span<const uint8_t, QuantizedDirectiveWireBytes> FixedSlice(
                Payload.data() + SliceOffset,
                QuantizedDirectiveWireBytes);
            RecordTickDirective(TickIndex, UnpackInputDirective5B(FixedSlice));
        }
    }

    OutLatestTick = AnchorTick;
    return true;
}

bool InputDirectiveStream::QueryDirectiveAtTick(uint32_t SimulationTick, QuantizedInputDirective& OutDirective) const noexcept
{
    const SlotEntry& Entry = Ring[SimulationTick % DirectiveHistoryRingCapacity];
    if (!Entry.Present || Entry.SimulationTick != SimulationTick)
    {
        return false;
    }
    OutDirective = Entry.Directive;
    return true;
}

void InputDirectiveStream::ClearStream() noexcept
{
    for (SlotEntry& Entry : Ring)
    {
        Entry = SlotEntry{};
    }
    HighestTick  = 0u;
    StreamSeeded = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            RELIABLE ORDERED DIRECTIVE ROUTER
//------------------------------------------------------------------------------------------------------------------------

uint32_t ReliableDirectiveRouter::EnqueueDirective(uint16_t DirectiveOpcode, std::span<const uint8_t> ParameterBytes)
{
    ReliableDirectiveEntry Record{};
    Record.DirectiveSequence = NextOutboundSequence++;
    Record.DirectiveOpcode   = DirectiveOpcode;
    Record.ParameterBytes.assign(ParameterBytes.begin(), ParameterBytes.end());
    PendingOutbound.push_back(std::move(Record));
    return PendingOutbound.back().DirectiveSequence;
}

bool ReliableDirectiveRouter::BuildOutboundBatch(std::vector<uint8_t>& OutPayload, uint32_t MaxPayloadBytes) const
{
    OutPayload.clear();
    AppendLittleEndian32(OutPayload, ExpectedInboundSequence - 1u);
    AppendLittleEndian16(OutPayload, 0u); // Placeholder for serialized directive count

    uint16_t WrittenCount = 0u;
    for (const ReliableDirectiveEntry& Record : PendingOutbound)
    {
        const std::size_t EntryBytes = 4u + 2u + 2u + Record.ParameterBytes.size();
        if (OutPayload.size() + EntryBytes > MaxPayloadBytes || Record.ParameterBytes.size() > 65535u)
        {
            break;
        }
        AppendLittleEndian32(OutPayload, Record.DirectiveSequence);
        AppendLittleEndian16(OutPayload, Record.DirectiveOpcode);
        AppendLittleEndian16(OutPayload, static_cast<uint16_t>(Record.ParameterBytes.size()));
        OutPayload.insert(OutPayload.end(), Record.ParameterBytes.begin(), Record.ParameterBytes.end());
        ++WrittenCount;
    }

    OutPayload[4] = static_cast<uint8_t>(WrittenCount & 0xFFu);
    OutPayload[5] = static_cast<uint8_t>((WrittenCount >> 8u) & 0xFFu);
    return WrittenCount > 0u;
}

void ReliableDirectiveRouter::AcknowledgeUpToSequence(uint32_t AckedDirectiveSequence) noexcept
{
    std::erase_if(PendingOutbound, [AckedDirectiveSequence](const ReliableDirectiveEntry& Record) noexcept
    {
        return Record.DirectiveSequence <= AckedDirectiveSequence;
    });
}

bool ReliableDirectiveRouter::IngestInboundBatch(
    std::span<const uint8_t>              Payload,
    std::vector<ReliableDirectiveEntry>& OutDeliveredInOrder)
{
    OutDeliveredInOrder.clear();
    if (Payload.size() < 6u)
    {
        return false;
    }

    std::size_t Cursor         = 0u;
    uint32_t RemoteContigAck   = 0u;
    uint16_t EntryCount        = 0u;
    if (!ReadLittleEndian32(Payload, Cursor, RemoteContigAck) ||
        !ReadLittleEndian16(Payload, Cursor, EntryCount))
    {
        return false;
    }

    AcknowledgeUpToSequence(RemoteContigAck);

    for (uint16_t Index = 0u; Index < EntryCount; ++Index)
    {
        ReliableDirectiveEntry Incoming{};
        uint16_t ParamBytes = 0u;
        if (!ReadLittleEndian32(Payload, Cursor, Incoming.DirectiveSequence) ||
            !ReadLittleEndian16(Payload, Cursor, Incoming.DirectiveOpcode) ||
            !ReadLittleEndian16(Payload, Cursor, ParamBytes) ||
            Cursor + ParamBytes > Payload.size())
        {
            return false;
        }
        Incoming.ParameterBytes.assign(Payload.begin() + static_cast<std::ptrdiff_t>(Cursor),
                                       Payload.begin() + static_cast<std::ptrdiff_t>(Cursor + ParamBytes));
        Cursor += ParamBytes;

        if (Incoming.DirectiveSequence < ExpectedInboundSequence)
        {
            continue; // Already delivered earlier
        }

        bool AlreadyHeld = false;
        for (const ReliableDirectiveEntry& Held : OutOfOrderHoldback)
        {
            if (Held.DirectiveSequence == Incoming.DirectiveSequence)
            {
                AlreadyHeld = true;
                break;
            }
        }
        if (!AlreadyHeld && OutOfOrderHoldback.size() < 256u)
        {
            OutOfOrderHoldback.push_back(std::move(Incoming));
        }
    }

    if (Cursor != Payload.size())
    {
        return false;
    }

    std::sort(OutOfOrderHoldback.begin(), OutOfOrderHoldback.end(),
        [](const ReliableDirectiveEntry& Left, const ReliableDirectiveEntry& Right) noexcept
        {
            return Left.DirectiveSequence < Right.DirectiveSequence;
        });

    while (!OutOfOrderHoldback.empty() &&
           OutOfOrderHoldback.front().DirectiveSequence == ExpectedInboundSequence)
    {
        OutDeliveredInOrder.push_back(std::move(OutOfOrderHoldback.front()));
        OutOfOrderHoldback.erase(OutOfOrderHoldback.begin());
        ++ExpectedInboundSequence;
    }

    return true;
}

void ReliableDirectiveRouter::ResetRouter() noexcept
{
    PendingOutbound.clear();
    OutOfOrderHoldback.clear();
    NextOutboundSequence    = 1u;
    ExpectedInboundSequence = 1u;
}

} // namespace Frontier::NetworkingSpace
