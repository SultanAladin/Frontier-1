/*============================================================================================================================================
                                                              SOCKETTRANSPORT.H
============================================================================================================================================*/
// 🧩 Sovereign non-blocking OS UDP transport with RFC 5389 STUN NAT discovery, RFC 8439 ChaCha20-Poly1305 AEAD, and stateless relay

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Transports/TransportInterface.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t StunMagicCookieWord             = 0x2112A442u; // [-] - RFC 5389 fixed magic cookie
inline constexpr std::size_t ChaCha20KeyByteCount         = 32u;         // [B] - 256-bit symmetric AEAD key
inline constexpr std::size_t ChaCha20NonceByteCount       = 12u;         // [B] - 96-bit IETF AEAD nonce
inline constexpr std::size_t Poly1305TagByteCount         = 16u;         // [B] - 128-bit authentication tag
inline constexpr std::size_t RelayRoutingPrefixByteCount  = 12u;         // [B] - 'F','R','L','Y' + Session32 + TargetEndpoint32

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct IpSocketAddress
{
    std::array<uint8_t, 4u> Ipv4Octets { 127u, 0u, 0u, 1u }; // [-] - Network-order IPv4 octets (e.g. 127.0.0.1)
    uint16_t                Port       = 0u;                 // [-] - Host-order UDP port

    [[nodiscard]] bool operator==(const IpSocketAddress&) const noexcept = default;
};

struct StatelessRelayRouteEntry
{
    uint32_t        SessionIdentifier = 0u; // [-] - Cleartext routing session id
    uint32_t        EndpointIdentifier= 0u; // [-] - Cleartext target peer slot id
    IpSocketAddress PeerAddress       {};   // [-] - Public UDP socket address for peer
};

//------------------------------------------------------------------------------------------------------------------------
//                                          RFC 5389 STUN & RFC 8439 AEAD PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ParseIpv4Literal(std::string_view DotDecimalText, std::array<uint8_t, 4u>& OutOctets) noexcept;
[[nodiscard]] std::string FormatIpv4Endpoint(const IpSocketAddress& Address);

void               BuildStunReflexiveRequest(
    std::span<const uint8_t, 12u> TransactionIdentifier,
    std::vector<uint8_t>&         OutStunPacket);

void               BuildStunReflexiveResponse(
    std::span<const uint8_t, 12u> TransactionIdentifier,
    const IpSocketAddress&        ObservedReflexiveAddress,
    std::vector<uint8_t>&         OutStunPacket);

[[nodiscard]] bool DecodeStunReflexiveResponse(
    std::span<const uint8_t>      StunPacket,
    std::span<const uint8_t, 12u> ExpectedTransactionIdentifier,
    IpSocketAddress&              OutReflexiveAddress) noexcept;

void               EncryptChaCha20Poly1305(
    std::span<const uint8_t, ChaCha20KeyByteCount>   SymmetricKey,
    std::span<const uint8_t, ChaCha20NonceByteCount> Nonce,
    std::span<const uint8_t>                         AssociatedHeader,
    std::span<const uint8_t>                         Plaintext,
    std::vector<uint8_t>&                            OutCiphertextWithTag);

[[nodiscard]] bool DecryptChaCha20Poly1305(
    std::span<const uint8_t, ChaCha20KeyByteCount>   SymmetricKey,
    std::span<const uint8_t, ChaCha20NonceByteCount> Nonce,
    std::span<const uint8_t>                         AssociatedHeader,
    std::span<const uint8_t>                         CiphertextWithTag,
    std::vector<uint8_t>&                            OutPlaintext);

void               WrapRelayEnvelope(
    uint32_t                 SessionIdentifier,
    uint32_t                 TargetEndpointIdentifier,
    std::span<const uint8_t> EncryptedPayload,
    std::vector<uint8_t>&    OutRelayDatagram);

[[nodiscard]] bool InspectRelayEnvelopeDestination(
    std::span<const uint8_t>                    InboundDatagram,
    std::span<const StatelessRelayRouteEntry>   RoutingEntries,
    IpSocketAddress&                            OutForwardAddress,
    std::span<const uint8_t>&                   OutOpaquePayload) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class SocketTransport final
{
public:
    SocketTransport() noexcept = default;
    ~SocketTransport() noexcept;

    SocketTransport(const SocketTransport&)            = delete;
    SocketTransport& operator=(const SocketTransport&) = delete;
    SocketTransport(SocketTransport&& Other) noexcept;
    SocketTransport& operator=(SocketTransport&& Other) noexcept;

    void               ConfigureBindPort(uint16_t RequestedPortHostOrder, uint32_t LocalEndpointIdentifier = 1u) noexcept;
    void               ConfigureSymmetricCipherKey(std::span<const uint8_t, ChaCha20KeyByteCount> KeyBytes, uint32_t SessionIdentifier) noexcept;
    void               DisableCipherProtection() noexcept;
    void               RegisterPeerSocketAddress(uint32_t EndpointIdentifier, const IpSocketAddress& Address);

    [[nodiscard]] bool OpenChannel() noexcept;
    void               CloseChannel() noexcept;
    void               AdvanceChannel(double Δt) noexcept;

    [[nodiscard]] bool TransmitDatagram(
        uint32_t                 TargetEndpointIdentifier,
        DeliveryChannel          Channel,
        std::span<const uint8_t> WireBytes);

    [[nodiscard]] bool ReceiveDatagram(PacketDatagram& OutDatagram);

    [[nodiscard]] bool SendRawDatagramToAddress(const IpSocketAddress& Destination, std::span<const uint8_t> RawBytes) noexcept;
    [[nodiscard]] bool PollRawDatagramFromSocket(IpSocketAddress& OutSender, std::vector<uint8_t>& OutRawBytes);

    [[nodiscard]] TransportChannelCategory QueryChannelCategory() const noexcept { return TransportChannelCategory::SovereignSocket; }
    [[nodiscard]] bool                     QueryChannelOperational() const noexcept { return SocketDescriptor >= 0; }
    [[nodiscard]] uint16_t                 QueryBoundPort() const noexcept { return BoundPortHostOrder; }

private:
    struct PeerRouteRecord
    {
        uint32_t        EndpointIdentifier = 0u;
        IpSocketAddress Address            {};
    };

    intptr_t                                        SocketDescriptor        = -1;
    uint16_t                                        RequestedPortHostOrder  = 0u;
    uint16_t                                        BoundPortHostOrder      = 0u;
    uint32_t                                        LocalEndpointIdentifier = 1u;
    uint32_t                                        ActiveSessionIdentifier = 1u;
    uint64_t                                        OutboundCipherCounter   = 1u;
    double                                          LocalClockSeconds       = 0.0;
    bool                                            CipherEnabled           = false;
    std::array<uint8_t, ChaCha20KeyByteCount>       CipherKey               {};
    std::vector<PeerRouteRecord>                    PeerRoutes              {};
};

static_assert(PacketTransportChannel<SocketTransport>, "SocketTransport must satisfy PacketTransportChannel");

} // namespace Frontier::NetworkingSpace
