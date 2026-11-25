/*============================================================================================================================================
                                                                NETWORKCODEC.H
============================================================================================================================================*/
// 🧩 .NetworkSpace Two-Tier Hybrid container encoder/decoder (TOML .SessionRouting tier + 64B-aligned .ReplicationPolicy binary tier)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/NetworkingSpace/Policies/ReplicationPolicy.h"
#include "Engine/NetworkingSpace/Policies/TransportSelectionPolicy.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct NetworkSpaceConfiguration
{
    std::string                                               SessionProfileName    = "DefaultNetworkSpace";                           // [-] - Logical profile name
    NetworkingSpace::TransportChannelCategory                 PreferredTransport    = NetworkingSpace::TransportChannelCategory::None; // [-] - Optional transport override
    NetworkingSpace::EntitlementAccessTier                    AccessTier            = NetworkingSpace::EntitlementAccessTier::StandardAccess; // [-] - Entitlement routing tier
    NetworkingSpace::RelayRegionCluster                       PreferredRelayCluster = NetworkingSpace::RelayRegionCluster::EuCentralFrankfurt; // [-] - Fallback relay cluster
    uint32_t                                                  SimulationTickRateHz  = 60u;                                             // [Hz] - Authoritative tick rate
    uint32_t                                                  MaximumRealPeers      = 8u;                                              // [count] - Real network peer cap
    std::string                                               StunServerHostname    = "stun.frontier.invalid";                         // [-] - Public STUN endpoint host
    uint16_t                                                  StunServerPort        = 3478u;                                           // [-] - Public STUN UDP port
    bool                                                      AllowEncryptedRelay   = true;                                            // [-] - Permit ChaCha20-Poly1305 relay
    std::vector<NetworkingSpace::ReplicationScheduleSpec> ReplicationContracts  {};                                                // [-] - Binary replication contracts
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class NetworkEncoder final
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const NetworkSpaceConfiguration&   Configuration,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const NetworkSpaceConfiguration&   Configuration,
        std::vector<std::byte>&            OutContainerBytes,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const NetworkSpaceConfiguration&   Configuration,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class NetworkDecoder final
{
public:
    [[nodiscard]] static bool ExtractConfiguration(
        const SpaceContainerPayload& Payload,
        NetworkSpaceConfiguration&   OutConfiguration,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>   ContainerBytes,
        NetworkSpaceConfiguration&   OutConfiguration,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        NetworkSpaceConfiguration&   OutConfiguration,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct NetworkCodec final
{
    using Encoder = NetworkEncoder;
    using Decoder = NetworkDecoder;
};

} // namespace Frontier::Codecs
