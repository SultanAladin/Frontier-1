/*============================================================================================================================================
                                                             PACKETSERIALIZER.CPP
============================================================================================================================================*/
// 🧩 Castagnoli CRC32C checksum, 'FP1' + 24B envelope wire framing, and bounded 64-slot 'FCNK' chunk fragmentation/reassembly

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/PacketSerializer.h"

#include <algorithm>
#include <cstring>

namespace Frontier::NetworkingSpace
{

namespace
{

constexpr std::array<uint32_t, 256u> BuildCastagnoliLookupArray() noexcept
{
    std::array<uint32_t, 256u> Lookup{};
    for (uint32_t ByteIndex = 0u; ByteIndex < 256u; ++ByteIndex)
    {
        uint32_t Accumulator = ByteIndex;
        for (uint32_t Bit = 0u; Bit < 8u; ++Bit)
        {
            Accumulator = (Accumulator & 1u) != 0u
                              ? ((Accumulator >> 1u) ^ 0x82F63B78u)
                              : (Accumulator >> 1u);
        }
        Lookup[ByteIndex] = Accumulator;
    }
    return Lookup;
}

constexpr std::array<uint32_t, 256u> CastagnoliLookupArray = BuildCastagnoliLookupArray();

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  LITTLE-ENDIAN HELPERS
//------------------------------------------------------------------------------------------------------------------------

void AppendLittleEndian16(std::vector<uint8_t>& Destination, uint16_t UnsignedWord)
{
    Destination.push_back(static_cast<uint8_t>(UnsignedWord & 0xFFu));
    Destination.push_back(static_cast<uint8_t>((UnsignedWord >> 8u) & 0xFFu));
}

void AppendLittleEndian32(std::vector<uint8_t>& Destination, uint32_t UnsignedDword)
{
    Destination.push_back(static_cast<uint8_t>(UnsignedDword & 0xFFu));
    Destination.push_back(static_cast<uint8_t>((UnsignedDword >> 8u) & 0xFFu));
    Destination.push_back(static_cast<uint8_t>((UnsignedDword >> 16u) & 0xFFu));
    Destination.push_back(static_cast<uint8_t>((UnsignedDword >> 24u) & 0xFFu));
}

void AppendLittleEndian64(std::vector<uint8_t>& Destination, uint64_t UnsignedQword)
{
    for (uint32_t Shift = 0u; Shift < 64u; Shift += 8u)
    {
        Destination.push_back(static_cast<uint8_t>((UnsignedQword >> Shift) & 0xFFu));
    }
}

bool ReadLittleEndian16(std::span<const uint8_t> Source, std::size_t& Cursor, uint16_t& OutWord) noexcept
{
    if (Cursor + 2u > Source.size())
    {
        return false;
    }
    OutWord = static_cast<uint16_t>(Source[Cursor]) |
              static_cast<uint16_t>(static_cast<uint16_t>(Source[Cursor + 1u]) << 8u);
    Cursor += 2u;
    return true;
}

bool ReadLittleEndian32(std::span<const uint8_t> Source, std::size_t& Cursor, uint32_t& OutDword) noexcept
{
    if (Cursor + 4u > Source.size())
    {
        return false;
    }
    OutDword = static_cast<uint32_t>(Source[Cursor]) |
               (static_cast<uint32_t>(Source[Cursor + 1u]) << 8u) |
               (static_cast<uint32_t>(Source[Cursor + 2u]) << 16u) |
               (static_cast<uint32_t>(Source[Cursor + 3u]) << 24u);
    Cursor += 4u;
    return true;
}

bool ReadLittleEndian64(std::span<const uint8_t> Source, std::size_t& Cursor, uint64_t& OutQword) noexcept
{
    if (Cursor + 8u > Source.size())
    {
        return false;
    }
    uint64_t Result = 0u;
    for (uint32_t ByteIndex = 0u; ByteIndex < 8u; ++ByteIndex)
    {
        Result |= (static_cast<uint64_t>(Source[Cursor + ByteIndex]) << (ByteIndex * 8u));
    }
    OutQword = Result;
    Cursor  += 8u;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CRC32C & WIRE FRAMING
//------------------------------------------------------------------------------------------------------------------------

uint32_t CalculateCrc32c(std::span<const uint8_t> Bytes) noexcept
{
    uint32_t Checksum = 0xFFFFFFFFu;
    for (uint8_t Byte : Bytes)
    {
        Checksum = (Checksum >> 8u) ^ CastagnoliLookupArray[(Checksum ^ Byte) & 0xFFu];
    }
    return ~Checksum;
}

bool SerializeWireDatagram(
    PacketMessageCategory         Category,
    DeliveryChannel               Channel,
    const NetworkEnvelopeHeader&  Envelope,
    std::span<const uint8_t>      InnerPayload,
    std::vector<uint8_t>&         OutDatagramBytes)
{
    const std::size_t BodyBytes = EnvelopeHeaderByteCount + InnerPayload.size();
    if (WireOuterHeaderByteCount + BodyBytes > MaximumWireDatagramBytes || BodyBytes > 65535u)
    {
        return false;
    }

    OutDatagramBytes.clear();
    OutDatagramBytes.reserve(WireOuterHeaderByteCount + BodyBytes);

    // ① 7-byte 'FP1' outer wire header
    OutDatagramBytes.push_back(static_cast<uint8_t>('F'));
    OutDatagramBytes.push_back(static_cast<uint8_t>('P'));
    OutDatagramBytes.push_back(static_cast<uint8_t>('1'));
    OutDatagramBytes.push_back(static_cast<uint8_t>(Category));
    OutDatagramBytes.push_back(static_cast<uint8_t>(Channel));
    AppendLittleEndian16(OutDatagramBytes, static_cast<uint16_t>(BodyBytes));

    // ② 24-byte inner network envelope header with computed CRC32C over payload
    const uint32_t ComputedCrc = CalculateCrc32c(InnerPayload);
    AppendLittleEndian32(OutDatagramBytes, Envelope.SequenceNumber);
    AppendLittleEndian32(OutDatagramBytes, Envelope.AckLatestSequence);
    AppendLittleEndian32(OutDatagramBytes, Envelope.AckHistoryBitfield);
    AppendLittleEndian32(OutDatagramBytes, Envelope.SimulationTick);
    AppendLittleEndian32(OutDatagramBytes, Envelope.SenderEndpointIdentifier);
    AppendLittleEndian32(OutDatagramBytes, ComputedCrc);

    // ③ Inner payload bytes
    OutDatagramBytes.insert(OutDatagramBytes.end(), InnerPayload.begin(), InnerPayload.end());
    return true;
}

bool DeserializeWireDatagram(
    std::span<const uint8_t> DatagramBytes,
    DecodedWireDatagram&     OutDecoded)
{
    if (DatagramBytes.size() < WireOuterHeaderByteCount + EnvelopeHeaderByteCount ||
        DatagramBytes.size() > MaximumWireDatagramBytes)
    {
        return false;
    }

    if (DatagramBytes[0] != static_cast<uint8_t>('F') ||
        DatagramBytes[1] != static_cast<uint8_t>('P') ||
        DatagramBytes[2] != static_cast<uint8_t>('1'))
    {
        return false;
    }

    const uint8_t RawCategory = DatagramBytes[3];
    const uint8_t RawChannel  = DatagramBytes[4];
    if (RawCategory < static_cast<uint8_t>(PacketMessageCategory::HeartbeatPing) ||
        RawCategory > static_cast<uint8_t>(PacketMessageCategory::LifecycleControlNotice) ||
        RawChannel > static_cast<uint8_t>(DeliveryChannel::ReliableOrdered))
    {
        return false;
    }

    std::size_t Cursor = 5u;
    uint16_t DeclaredBodyBytes = 0u;
    if (!ReadLittleEndian16(DatagramBytes, Cursor, DeclaredBodyBytes))
    {
        return false;
    }

    if (DeclaredBodyBytes < EnvelopeHeaderByteCount ||
        WireOuterHeaderByteCount + static_cast<std::size_t>(DeclaredBodyBytes) != DatagramBytes.size())
    {
        return false;
    }

    NetworkEnvelopeHeader Header{};
    if (!ReadLittleEndian32(DatagramBytes, Cursor, Header.SequenceNumber) ||
        !ReadLittleEndian32(DatagramBytes, Cursor, Header.AckLatestSequence) ||
        !ReadLittleEndian32(DatagramBytes, Cursor, Header.AckHistoryBitfield) ||
        !ReadLittleEndian32(DatagramBytes, Cursor, Header.SimulationTick) ||
        !ReadLittleEndian32(DatagramBytes, Cursor, Header.SenderEndpointIdentifier) ||
        !ReadLittleEndian32(DatagramBytes, Cursor, Header.PayloadCrc32c))
    {
        return false;
    }

    const std::span<const uint8_t> InnerSlice = DatagramBytes.subspan(Cursor);
    if (CalculateCrc32c(InnerSlice) != Header.PayloadCrc32c)
    {
        return false;
    }

    OutDecoded.Category = static_cast<PacketMessageCategory>(RawCategory);
    OutDecoded.Channel  = static_cast<DeliveryChannel>(RawChannel);
    OutDecoded.Envelope = Header;
    OutDecoded.Payload.assign(InnerSlice.begin(), InnerSlice.end());
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           FRAGMENTATION & BOUNDED REASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

bool FragmentPayloadIntoChunks(
    uint32_t                           TransferIdentifier,
    std::span<const uint8_t>           FullPayload,
    std::vector<std::vector<uint8_t>>& OutChunkPayloads,
    uint32_t                           MaxChunkSliceBytes)
{
    OutChunkPayloads.clear();
    if (TransferIdentifier == 0u ||
        FullPayload.empty() ||
        FullPayload.size() > MaximumReassemblyBytes ||
        MaxChunkSliceBytes < 64u ||
        ChunkHeaderByteCount + MaxChunkSliceBytes + EnvelopeHeaderByteCount + WireOuterHeaderByteCount > MaximumWireDatagramBytes)
    {
        return false;
    }

    const uint32_t TotalBytes      = static_cast<uint32_t>(FullPayload.size());
    const uint32_t TotalChunkCount = (TotalBytes + MaxChunkSliceBytes - 1u) / MaxChunkSliceBytes;
    if (TotalChunkCount == 0u || TotalChunkCount > MaximumReassemblyChunkCount)
    {
        return false;
    }

    const uint32_t FullCrc = CalculateCrc32c(FullPayload);
    OutChunkPayloads.reserve(TotalChunkCount);

    for (uint32_t ChunkIndex = 0u; ChunkIndex < TotalChunkCount; ++ChunkIndex)
    {
        const uint32_t ByteOffset = ChunkIndex * MaxChunkSliceBytes;
        const uint32_t SliceBytes = std::min<uint32_t>(MaxChunkSliceBytes, TotalBytes - ByteOffset);

        std::vector<uint8_t> Chunk;
        Chunk.reserve(ChunkHeaderByteCount + SliceBytes);
        Chunk.push_back(static_cast<uint8_t>('F'));
        Chunk.push_back(static_cast<uint8_t>('C'));
        Chunk.push_back(static_cast<uint8_t>('N'));
        Chunk.push_back(static_cast<uint8_t>('K'));
        AppendLittleEndian32(Chunk, TransferIdentifier);
        AppendLittleEndian16(Chunk, static_cast<uint16_t>(ChunkIndex));
        AppendLittleEndian16(Chunk, static_cast<uint16_t>(TotalChunkCount));
        AppendLittleEndian32(Chunk, TotalBytes);
        AppendLittleEndian32(Chunk, FullCrc);
        Chunk.insert(Chunk.end(), FullPayload.begin() + ByteOffset, FullPayload.begin() + ByteOffset + SliceBytes);

        OutChunkPayloads.push_back(std::move(Chunk));
    }

    return true;
}

bool ChunkReassemblyRegister::IngestChunkPayload(
    uint32_t                 SenderEndpointIdentifier,
    std::span<const uint8_t> ChunkPayload,
    double                   CurrentTimestampSeconds,
    CompletedChunkTransfer&  OutCompleted,
    bool&                    OutTransferFinished)
{
    OutTransferFinished = false;
    if (SenderEndpointIdentifier == 0u || ChunkPayload.size() <= ChunkHeaderByteCount)
    {
        ++RejectedChunkCount;
        return false;
    }

    if (ChunkPayload[0] != static_cast<uint8_t>('F') ||
        ChunkPayload[1] != static_cast<uint8_t>('C') ||
        ChunkPayload[2] != static_cast<uint8_t>('N') ||
        ChunkPayload[3] != static_cast<uint8_t>('K'))
    {
        ++RejectedChunkCount;
        return false;
    }

    std::size_t Cursor = 4u;
    ChunkTransferHeader Header{};
    if (!ReadLittleEndian32(ChunkPayload, Cursor, Header.TransferIdentifier) ||
        !ReadLittleEndian16(ChunkPayload, Cursor, Header.ChunkIndex) ||
        !ReadLittleEndian16(ChunkPayload, Cursor, Header.TotalChunkCount) ||
        !ReadLittleEndian32(ChunkPayload, Cursor, Header.FullTransferBytes) ||
        !ReadLittleEndian32(ChunkPayload, Cursor, Header.FullTransferCrc32c))
    {
        ++RejectedChunkCount;
        return false;
    }

    const std::span<const uint8_t> SliceBytes = ChunkPayload.subspan(Cursor);
    if (Header.TransferIdentifier == 0u ||
        Header.TotalChunkCount == 0u ||
        Header.TotalChunkCount > MaximumReassemblyChunkCount ||
        Header.ChunkIndex >= Header.TotalChunkCount ||
        Header.FullTransferBytes == 0u ||
        Header.FullTransferBytes > MaximumReassemblyBytes ||
        SliceBytes.empty() ||
        SliceBytes.size() > Header.FullTransferBytes)
    {
        ++RejectedChunkCount;
        return false;
    }

    // ① Locate existing slot or claim a free/oldest slot in the bounded 64-slot array
    ReassemblySlot* TargetSlot = nullptr;
    for (ReassemblySlot& Slot : Slots)
    {
        if (Slot.Occupied &&
            Slot.SenderEndpointIdentifier == SenderEndpointIdentifier &&
            Slot.Header.TransferIdentifier == Header.TransferIdentifier)
        {
            TargetSlot = &Slot;
            break;
        }
    }

    if (TargetSlot == nullptr)
    {
        for (ReassemblySlot& Slot : Slots)
        {
            if (!Slot.Occupied)
            {
                TargetSlot = &Slot;
                break;
            }
        }
        if (TargetSlot == nullptr)
        {
            // Evict stalest incomplete transfer when all 64 slots are saturated
            TargetSlot = &Slots[0];
            for (ReassemblySlot& Slot : Slots)
            {
                if (Slot.LastActivitySeconds < TargetSlot->LastActivitySeconds)
                {
                    TargetSlot = &Slot;
                }
            }
        }

        const uint32_t NominalStride = (Header.TotalChunkCount == 1u)
                                           ? Header.FullTransferBytes
                                           : (Header.ChunkIndex + 1u < Header.TotalChunkCount
                                                  ? static_cast<uint32_t>(SliceBytes.size())
                                                  : SafeMtuPayloadLimitBytes);

        TargetSlot->Occupied                 = true;
        TargetSlot->SenderEndpointIdentifier = SenderEndpointIdentifier;
        TargetSlot->Header                   = Header;
        TargetSlot->ReceivedChunkCount       = 0u;
        TargetSlot->ChunkStrideBytes         = NominalStride;
        TargetSlot->LastActivitySeconds      = CurrentTimestampSeconds;
        TargetSlot->ChunkPresenceBitmap.assign(Header.TotalChunkCount, 0u);
        TargetSlot->WorkingBuffer.assign(Header.FullTransferBytes, 0u);
    }
    else
    {
        // 📝 Reject inconsistent metadata spoofing mid-transfer
        if (TargetSlot->Header.TotalChunkCount != Header.TotalChunkCount ||
            TargetSlot->Header.FullTransferBytes != Header.FullTransferBytes ||
            TargetSlot->Header.FullTransferCrc32c != Header.FullTransferCrc32c)
        {
            ++RejectedChunkCount;
            return false;
        }
    }

    // Refine nominal stride if an interior chunk arrives after the tail chunk arrived first out-of-order
    if (Header.ChunkIndex + 1u < Header.TotalChunkCount &&
        TargetSlot->ChunkStrideBytes != static_cast<uint32_t>(SliceBytes.size()))
    {
        if (TargetSlot->ReceivedChunkCount <= 1u)
        {
            TargetSlot->ChunkStrideBytes = static_cast<uint32_t>(SliceBytes.size());
        }
        else
        {
            ++RejectedChunkCount;
            return false;
        }
    }

    // Duplicate chunk is idempotently ignored
    if (TargetSlot->ChunkPresenceBitmap[Header.ChunkIndex] != 0u)
    {
        TargetSlot->LastActivitySeconds = CurrentTimestampSeconds;
        return true;
    }

    const std::size_t WriteOffset = static_cast<std::size_t>(Header.ChunkIndex) * static_cast<std::size_t>(TargetSlot->ChunkStrideBytes);
    if (WriteOffset + SliceBytes.size() > TargetSlot->WorkingBuffer.size())
    {
        ++RejectedChunkCount;
        return false;
    }

    std::memcpy(TargetSlot->WorkingBuffer.data() + WriteOffset, SliceBytes.data(), SliceBytes.size());
    TargetSlot->ChunkPresenceBitmap[Header.ChunkIndex] = 1u;
    ++TargetSlot->ReceivedChunkCount;
    TargetSlot->LastActivitySeconds = CurrentTimestampSeconds;

    if (TargetSlot->ReceivedChunkCount == TargetSlot->Header.TotalChunkCount)
    {
        const uint32_t ReassembledCrc = CalculateCrc32c(TargetSlot->WorkingBuffer);
        const bool IntegrityMatches   = (ReassembledCrc == TargetSlot->Header.FullTransferCrc32c);
        if (IntegrityMatches)
        {
            OutCompleted.SenderEndpointIdentifier = SenderEndpointIdentifier;
            OutCompleted.TransferIdentifier       = Header.TransferIdentifier;
            OutCompleted.ReassembledBytes         = std::move(TargetSlot->WorkingBuffer);
            OutTransferFinished                   = true;
        }
        else
        {
            ++RejectedChunkCount;
        }

        *TargetSlot = ReassemblySlot{};
        return IntegrityMatches;
    }

    return true;
}

void ChunkReassemblyRegister::PurgeExpiredSlots(double CurrentTimestampSeconds, double TimeoutSeconds) noexcept
{
    for (ReassemblySlot& Slot : Slots)
    {
        if (Slot.Occupied && (CurrentTimestampSeconds - Slot.LastActivitySeconds) >= TimeoutSeconds)
        {
            Slot = ReassemblySlot{};
        }
    }
}

void ChunkReassemblyRegister::ResetAllSlots() noexcept
{
    for (ReassemblySlot& Slot : Slots)
    {
        Slot = ReassemblySlot{};
    }
    RejectedChunkCount = 0u;
}

uint32_t ChunkReassemblyRegister::QueryActiveSlotCount() const noexcept
{
    uint32_t ActiveCount = 0u;
    for (const ReassemblySlot& Slot : Slots)
    {
        if (Slot.Occupied)
        {
            ++ActiveCount;
        }
    }
    return ActiveCount;
}

} // namespace Frontier::NetworkingSpace
