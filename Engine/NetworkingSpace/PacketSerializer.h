/*============================================================================================================================================
                                                              PACKETSERIALIZER.H
============================================================================================================================================*/
// 🧩 Deterministic 'FP1' wire framing, 24-byte sequence/ACK/CRC32C envelope, and bounded 'FCNK' 4 MiB chunk fragmentation/reassembly

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Policies/ReplicationPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t WireOuterHeaderByteCount     = 7u;                      // [B] - 'F','P','1', Category, Channel, BodyLen16
inline constexpr uint32_t EnvelopeHeaderByteCount      = 24u;                     // [B] - Fixed sequence/ACK/tick/sender/CRC32C header
inline constexpr uint32_t ChunkHeaderByteCount         = 20u;                     // [B] - 'F','C','N','K' + transfer metadata
inline constexpr uint32_t SafeMtuPayloadLimitBytes     = 1120u;                   // [B] - Max inner payload per single-MTU chunk
inline constexpr uint32_t MaximumWireDatagramBytes     = 1360u;                   // [B] - Hard UDP/EOS/Photon safe datagram ceiling
inline constexpr uint32_t MaximumReassemblyBytes       = 4u * 1024u * 1024u;      // [B] - 4 MiB reassembly ceiling per transfer
inline constexpr uint32_t MaximumReassemblyChunkCount  = 4096u;                   // [count] - Max chunks per fragmented transfer
inline constexpr std::size_t MaximumReassemblySlotCount= 64u;                     // [count] - Bounded concurrent reassembly slots

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class PacketMessageCategory : uint8_t
{
    HeartbeatPing          = 1, // [-] - RTT & clock measurement request
    HeartbeatPong          = 2, // [-] - RTT & clock measurement reply
    WorldSnapshotDelta     = 3, // [-] - Differential quantized spatial snapshot
    InputDirectiveBatch    = 4, // [-] - 3-tick redundant 5-byte quantized input directives
    ReliableDirectiveBatch = 5, // [-] - Reliable ordered command / workspace directives
    FragmentedChunkSlice   = 6, // [-] - 'FCNK' slice of a multi-kilobyte/megabyte payload
    LifecycleControlNotice = 7, // [-] - Session start/end/handshake notice
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct NetworkEnvelopeHeader
{
    uint32_t SequenceNumber           = 0u; // [-] - Monotonic outbound packet sequence
    uint32_t AckLatestSequence        = 0u; // [-] - Highest remote packet sequence received
    uint32_t AckHistoryBitfield       = 0u; // [mask] - 32-packet sliding ACK history bitfield
    uint32_t SimulationTick           = 0u; // [-] - Sender simulation tick index
    uint32_t SenderEndpointIdentifier = 0u; // [-] - Non-zero sender endpoint slot id
    uint32_t PayloadCrc32c            = 0u; // [-] - Castagnoli CRC32C over inner payload bytes
};
static_assert(sizeof(NetworkEnvelopeHeader) == EnvelopeHeaderByteCount, "NetworkEnvelopeHeader must occupy 24 bytes");

struct DecodedWireDatagram
{
    PacketMessageCategory  Category = PacketMessageCategory::HeartbeatPing; // [-] - Decoded wire message category
    DeliveryChannel        Channel  = DeliveryChannel::UnreliableSequenced; // [-] - Decoded reliability channel
    NetworkEnvelopeHeader  Envelope {};                                     // [-] - 24-byte sequence/ACK/CRC header
    std::vector<uint8_t>   Payload  {};                                     // [-] - Verified inner payload bytes
};

struct ChunkTransferHeader
{
    uint32_t TransferIdentifier  = 0u; // [-] - Unique per-sender transfer identifier
    uint16_t ChunkIndex          = 0u; // [-] - Zero-based chunk index
    uint16_t TotalChunkCount     = 0u; // [count] - Total chunks in transfer
    uint32_t FullTransferBytes   = 0u; // [B] - Exact reconstructed byte count
    uint32_t FullTransferCrc32c  = 0u; // [-] - Castagnoli CRC32C across complete payload
};

struct CompletedChunkTransfer
{
    uint32_t             SenderEndpointIdentifier = 0u; // [-] - Originating sender endpoint
    uint32_t             TransferIdentifier       = 0u; // [-] - Completed transfer id
    std::vector<uint8_t> ReassembledBytes         {};   // [-] - Integrity-verified payload
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LITTLE-ENDIAN HELPERS
//------------------------------------------------------------------------------------------------------------------------

void                   AppendLittleEndian16(std::vector<uint8_t>& Destination, uint16_t UnsignedWord);
void                   AppendLittleEndian32(std::vector<uint8_t>& Destination, uint32_t UnsignedDword);
void                   AppendLittleEndian64(std::vector<uint8_t>& Destination, uint64_t UnsignedQword);

[[nodiscard]] bool     ReadLittleEndian16(std::span<const uint8_t> Source, std::size_t& Cursor, uint16_t& OutWord) noexcept;
[[nodiscard]] bool     ReadLittleEndian32(std::span<const uint8_t> Source, std::size_t& Cursor, uint32_t& OutDword) noexcept;
[[nodiscard]] bool     ReadLittleEndian64(std::span<const uint8_t> Source, std::size_t& Cursor, uint64_t& OutQword) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                  CRC32C & WIRE FRAMING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t CalculateCrc32c(std::span<const uint8_t> Bytes) noexcept;

[[nodiscard]] bool     SerializeWireDatagram(
    PacketMessageCategory         Category,
    DeliveryChannel               Channel,
    const NetworkEnvelopeHeader&  Envelope,
    std::span<const uint8_t>      InnerPayload,
    std::vector<uint8_t>&         OutDatagramBytes);

[[nodiscard]] bool     DeserializeWireDatagram(
    std::span<const uint8_t>      DatagramBytes,
    DecodedWireDatagram&          OutDecoded);

[[nodiscard]] bool     FragmentPayloadIntoChunks(
    uint32_t                      TransferIdentifier,
    std::span<const uint8_t>      FullPayload,
    std::vector<std::vector<uint8_t>>& OutChunkPayloads,
    uint32_t                      MaxChunkSliceBytes = SafeMtuPayloadLimitBytes);

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ChunkReassemblyRegister final
{
public:
    ChunkReassemblyRegister() = default;

    [[nodiscard]] bool IngestChunkPayload(
        uint32_t                 SenderEndpointIdentifier,
        std::span<const uint8_t> ChunkPayload,
        double                   CurrentTimestampSeconds,
        CompletedChunkTransfer&  OutCompleted,
        bool&                    OutTransferFinished);

    void               PurgeExpiredSlots(double CurrentTimestampSeconds, double TimeoutSeconds = 15.0) noexcept;
    void               ResetAllSlots() noexcept;

    [[nodiscard]] uint32_t QueryActiveSlotCount() const noexcept;
    [[nodiscard]] uint32_t QueryRejectedChunkCount() const noexcept { return RejectedChunkCount; }

private:
    struct ReassemblySlot
    {
        bool                  Occupied                 = false; // [-] - Slot active marker
        uint32_t              SenderEndpointIdentifier = 0u;    // [-] - Sender endpoint id
        ChunkTransferHeader   Header                   {};      // [-] - Transfer metadata
        uint32_t              ReceivedChunkCount       = 0u;    // [count] - Distinct chunks stored
        uint32_t              ChunkStrideBytes         = 0u;    // [B] - Nominal slice size from chunk 0
        double                LastActivitySeconds      = 0.0;   // [s] - Timestamp of latest valid chunk
        std::vector<uint8_t>  ChunkPresenceBitmap      {};      // [-] - 1 byte per chunk received marker
        std::vector<uint8_t>  WorkingBuffer            {};      // [-] - Pre-sized reconstruction buffer
    };

    std::array<ReassemblySlot, MaximumReassemblySlotCount> Slots              {};
    uint32_t                                               RejectedChunkCount = 0u;
};

} // namespace Frontier::NetworkingSpace
