/*============================================================================================================================================
                                                          TRANSPORTSELECTIONPOLICY.H
============================================================================================================================================*/
// 🧩 Pure deterministic transport selection, entitlement routing, minimax global relay cluster choice, and failover policy

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <span>
#include <string_view>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class TransportChannelCategory : uint8_t
{
    None           = 0, // [-] - No transport active
    Loopback       = 1, // [-] - Intra-application deterministic packet switch
    SovereignSocket= 2, // [-] - Native OS UDP + RFC 5389 STUN punch + ChaCha20-Poly1305 relay
    EpicOnline     = 3, // [-] - Epic Online Services P2P + Lobby + Sessions
    PhotonRealtime = 4, // [-] - Photon Realtime Core cloud room with Epic JWT auth
};

enum class EntitlementAccessTier : uint8_t
{
    StandardAccess = 0, // [-] - Default free multiplayer routing
    PremiumAccess  = 1, // [-] - Managed low-jitter cloud relay entitlement
};

enum class TransportLinkAvailabilityMask : uint8_t
{
    None                  = 0u,      // [mask] - Zero transports compiled/linked
    EnableLoopback        = 1u << 0, // [mask] - In-memory LoopbackTransport available
    EnableSovereignSocket = 1u << 1, // [mask] - Native UDP SocketTransport available
    EnableEpicOnline      = 1u << 2, // [mask] - EOS SDK runtime linked and initialized
    EnablePhotonRealtime  = 1u << 3, // [mask] - Photon Realtime /MD libraries linked
};

enum class RelayRegionCluster : uint8_t
{
    UsEastVirginia     = 0, // [-] - North & South America anchor
    EuCentralFrankfurt = 1, // [-] - Europe, North Africa (Egypt), and Middle East anchor
    AsiaSingapore      = 2, // [-] - Southeast Asia, India, and Southern China anchor
    AsiaTokyo          = 3, // [-] - East Asia, Northern China, Japan, and Korea anchor
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PeerRegionLatencyReading
{
    float UsEastRttMs         = 180.0f; // [ms] - Measured RTT to US-East relay
    float EuCentralRttMs      = 180.0f; // [ms] - Measured RTT to Frankfurt relay
    float AsiaSingaporeRttMs  = 180.0f; // [ms] - Measured RTT to Singapore relay
    float AsiaTokyoRttMs      = 180.0f; // [ms] - Measured RTT to Tokyo relay
};

struct TransportSelectionInput
{
    TransportChannelCategory ExplicitOverride   = TransportChannelCategory::None;               // [-] - Explicit user/config override
    EntitlementAccessTier    AccessTier         = EntitlementAccessTier::StandardAccess;        // [-] - Standard vs. Premium tier
    uint8_t                  AvailabilityMask   = 0u;                                           // [mask] - Linked/ready transports
    uint8_t                  FailedTransportMask= 0u;                                           // [mask] - Transports that failed/dropped
};

struct RelayClusterSelection
{
    RelayRegionCluster OptimalCluster       = RelayRegionCluster::EuCentralFrankfurt; // [-]  - Chosen relay cluster
    float              WorstPeerRttMs       = 0.0f;                                   // [ms] - Minimax worst-peer RTT
    float              MeanPeerRttMs        = 0.0f;                                   // [ms] - Arithmetic mean peer RTT
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr uint8_t                  DeriveTransportBit(TransportChannelCategory Category) noexcept;
[[nodiscard]] constexpr TransportChannelCategory ResolveActiveTransport(const TransportSelectionInput& Input) noexcept;
[[nodiscard]] RelayClusterSelection              SelectMinimaxRelayCluster(std::span<const PeerRegionLatencyReading> Readings) noexcept;
[[nodiscard]] std::string_view                   InspectTransportCategoryTitle(TransportChannelCategory Category) noexcept;
[[nodiscard]] std::string_view                   InspectRelayClusterCode(RelayRegionCluster Cluster) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint8_t DeriveTransportBit(TransportChannelCategory Category) noexcept
{
    switch (Category)
    {
    case TransportChannelCategory::Loopback:        return static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableLoopback);
    case TransportChannelCategory::SovereignSocket: return static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableSovereignSocket);
    case TransportChannelCategory::EpicOnline:      return static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableEpicOnline);
    case TransportChannelCategory::PhotonRealtime:  return static_cast<uint8_t>(TransportLinkAvailabilityMask::EnablePhotonRealtime);
    case TransportChannelCategory::None:            return 0u;
    }
    return 0u;
}

constexpr TransportChannelCategory ResolveActiveTransport(const TransportSelectionInput& Input) noexcept
{
    const uint8_t UsableMask = static_cast<uint8_t>(Input.AvailabilityMask & static_cast<uint8_t>(~Input.FailedTransportMask));

    // ① Honor explicit user/document transport override if usable; otherwise fall through to resilient failover
    if (Input.ExplicitOverride != TransportChannelCategory::None)
    {
        const uint8_t OverrideBit = DeriveTransportBit(Input.ExplicitOverride);
        if ((UsableMask & OverrideBit) != 0u)
        {
            return Input.ExplicitOverride;
        }
    }

    // ② Premium entitlement routes first to Photon Realtime, failing over cleanly to EOS -> Sovereign UDP -> Loopback
    if (Input.AccessTier == EntitlementAccessTier::PremiumAccess &&
        (UsableMask & static_cast<uint8_t>(TransportLinkAvailabilityMask::EnablePhotonRealtime)) != 0u)
    {
        return TransportChannelCategory::PhotonRealtime;
    }

    if ((UsableMask & static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableEpicOnline)) != 0u)
    {
        return TransportChannelCategory::EpicOnline;
    }

    if ((UsableMask & static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableSovereignSocket)) != 0u)
    {
        return TransportChannelCategory::SovereignSocket;
    }

    if ((UsableMask & static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableLoopback)) != 0u)
    {
        return TransportChannelCategory::Loopback;
    }

    return TransportChannelCategory::None;
}

} // namespace Frontier::NetworkingSpace
