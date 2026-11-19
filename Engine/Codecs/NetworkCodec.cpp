/*============================================================================================================================================
                                                               NETWORKCODEC.CPP
============================================================================================================================================*/
// 🧩 .NetworkSpace Two-Tier Hybrid container encoder/decoder (TOML .SessionRouting tier + 64B-aligned .ReplicationPolicy binary tier)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/NetworkCodec.h"
#include "Engine/NetworkingSpace/PacketSerializer.h"

#include <bit>
#include <cstring>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                NETWORK CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload NetworkEncoder::BuildPayload(
    const NetworkSpaceConfiguration&   Configuration,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory  = SpaceSuffixCategory::NetworkSpace;
    Payload.DocumentTitle      = Configuration.SessionProfileName;
    Payload.AuthoringWorkspace = "Frontier.NetworkingSpace";

    toml::table RoutingSection;
    RoutingSection.insert_or_assign("SessionProfileName",    Configuration.SessionProfileName);
    RoutingSection.insert_or_assign("PreferredTransport",    static_cast<int64_t>(Configuration.PreferredTransport));
    RoutingSection.insert_or_assign("AccessTier",            static_cast<int64_t>(Configuration.AccessTier));
    RoutingSection.insert_or_assign("PreferredRelayCluster", static_cast<int64_t>(Configuration.PreferredRelayCluster));
    RoutingSection.insert_or_assign("SimulationTickRateHz",  static_cast<int64_t>(Configuration.SimulationTickRateHz));
    RoutingSection.insert_or_assign("MaximumRealPeers",      static_cast<int64_t>(Configuration.MaximumRealPeers));
    RoutingSection.insert_or_assign("StunServerHostname",    Configuration.StunServerHostname);
    RoutingSection.insert_or_assign("StunServerPort",        static_cast<int64_t>(Configuration.StunServerPort));
    RoutingSection.insert_or_assign("AllowEncryptedRelay",   Configuration.AllowEncryptedRelay);
    Payload.DomainManifest.insert_or_assign(".SessionRouting", std::move(RoutingSection));

    // Pack binary ReplicationScheduleSpec records into a deterministic little-endian Tier 2 section
    std::vector<uint8_t> RawBytes;
    RawBytes.reserve(4u + Configuration.ReplicationContracts.size() * 20u);
    NetworkingSpace::AppendLittleEndian32(RawBytes, static_cast<uint32_t>(Configuration.ReplicationContracts.size()));
    for (const NetworkingSpace::ReplicationScheduleSpec& Contract : Configuration.ReplicationContracts)
    {
        NetworkingSpace::AppendLittleEndian32(RawBytes, Contract.ContractIdentifier);
        RawBytes.push_back(static_cast<uint8_t>(Contract.Authority));
        RawBytes.push_back(static_cast<uint8_t>(Contract.Channel));
        NetworkingSpace::AppendLittleEndian16(RawBytes, 0u);
        NetworkingSpace::AppendLittleEndian32(RawBytes, Contract.ComponentMask);
        NetworkingSpace::AppendLittleEndian32(RawBytes, std::bit_cast<uint32_t>(Contract.PublishFrequencyHz));
        NetworkingSpace::AppendLittleEndian32(RawBytes, std::bit_cast<uint32_t>(Contract.SpatialDeadbandMillimeters));
    }

    BinarySectionBlock ContractsSection{};
    ContractsSection.SectionIdentifier = "ReplicationContracts";
    ContractsSection.SubFormatTag      = ".ReplicationPolicy";
    ContractsSection.PayloadBytes.resize(RawBytes.size());
    if (!RawBytes.empty())
    {
        std::memcpy(ContractsSection.PayloadBytes.data(), RawBytes.data(), RawBytes.size());
    }
    Payload.BinarySections.push_back(std::move(ContractsSection));

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool NetworkEncoder::EncodeToBytes(
    const NetworkSpaceConfiguration&   Configuration,
    std::vector<std::byte>&            OutContainerBytes,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToBytes(BuildPayload(Configuration, OptionalRevision), OutContainerBytes, OutDiagnostic);
}

bool NetworkEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const NetworkSpaceConfiguration&   Configuration,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Configuration, OptionalRevision), OutDiagnostic);
}

bool NetworkDecoder::ExtractConfiguration(
    const SpaceContainerPayload& Payload,
    NetworkSpaceConfiguration&   OutConfiguration,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::NetworkSpace)
    {
        OutDiagnostic = "Expected .NetworkSpace container category.";
        return false;
    }

    OutConfiguration                    = NetworkSpaceConfiguration{};
    OutConfiguration.SessionProfileName = Payload.DocumentTitle;

    if (const toml::table* Routing = Payload.DomainManifest[".SessionRouting"].as_table())
    {
        OutConfiguration.SessionProfileName = (*Routing)["SessionProfileName"].value_or(Payload.DocumentTitle);
        OutConfiguration.StunServerHostname = (*Routing)["StunServerHostname"].value_or(std::string{ "stun.frontier.invalid" });

        const int64_t RawTransport = (*Routing)["PreferredTransport"].value_or(int64_t{ 0 });
        if (RawTransport >= 0 && RawTransport <= static_cast<int64_t>(NetworkingSpace::TransportChannelCategory::PhotonRealtime))
        {
            OutConfiguration.PreferredTransport = static_cast<NetworkingSpace::TransportChannelCategory>(RawTransport);
        }

        const int64_t RawTier = (*Routing)["AccessTier"].value_or(int64_t{ 0 });
        if (RawTier >= 0 && RawTier <= static_cast<int64_t>(NetworkingSpace::EntitlementAccessTier::PremiumAccess))
        {
            OutConfiguration.AccessTier = static_cast<NetworkingSpace::EntitlementAccessTier>(RawTier);
        }

        const int64_t RawCluster = (*Routing)["PreferredRelayCluster"].value_or(int64_t{ 1 });
        if (RawCluster >= 0 && RawCluster <= static_cast<int64_t>(NetworkingSpace::RelayRegionCluster::AsiaTokyo))
        {
            OutConfiguration.PreferredRelayCluster = static_cast<NetworkingSpace::RelayRegionCluster>(RawCluster);
        }

        const int64_t TickRate = (*Routing)["SimulationTickRateHz"].value_or(int64_t{ 60 });
        if (TickRate >= 1 && TickRate <= 1000)
        {
            OutConfiguration.SimulationTickRateHz = static_cast<uint32_t>(TickRate);
        }

        const int64_t MaxPeers = (*Routing)["MaximumRealPeers"].value_or(int64_t{ 8 });
        if (MaxPeers >= 1 && MaxPeers <= 256)
        {
            OutConfiguration.MaximumRealPeers = static_cast<uint32_t>(MaxPeers);
        }

        const int64_t StunPort = (*Routing)["StunServerPort"].value_or(int64_t{ 3478 });
        if (StunPort >= 1 && StunPort <= 65535)
        {
            OutConfiguration.StunServerPort = static_cast<uint16_t>(StunPort);
        }

        OutConfiguration.AllowEncryptedRelay = (*Routing)["AllowEncryptedRelay"].value_or(true);
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".ReplicationPolicy")
        {
            std::span<const uint8_t> Bytes(
                reinterpret_cast<const uint8_t*>(Section.PayloadBytes.data()),
                Section.PayloadBytes.size());

            std::size_t Cursor     = 0u;
            uint32_t ContractCount = 0u;
            if (!NetworkingSpace::ReadLittleEndian32(Bytes, Cursor, ContractCount) || ContractCount > 4096u)
            {
                OutDiagnostic = "Malformed .ReplicationPolicy contract count.";
                return false;
            }

            OutConfiguration.ReplicationContracts.reserve(ContractCount);
            for (uint32_t Index = 0u; Index < ContractCount; ++Index)
            {
                if (Cursor + 20u > Bytes.size())
                {
                    OutDiagnostic = "Truncated .ReplicationPolicy binary contract.";
                    return false;
                }

                NetworkingSpace::ReplicationScheduleSpec Contract{};
                if (!NetworkingSpace::ReadLittleEndian32(Bytes, Cursor, Contract.ContractIdentifier))
                {
                    return false;
                }
                const uint8_t RawAuthority = Bytes[Cursor++];
                const uint8_t RawChannel   = Bytes[Cursor++];
                uint16_t ReservedWord      = 0u;
                uint32_t FreqBits          = 0u;
                uint32_t DeadbandBits      = 0u;
                if (!NetworkingSpace::ReadLittleEndian16(Bytes, Cursor, ReservedWord) ||
                    !NetworkingSpace::ReadLittleEndian32(Bytes, Cursor, Contract.ComponentMask) ||
                    !NetworkingSpace::ReadLittleEndian32(Bytes, Cursor, FreqBits) ||
                    !NetworkingSpace::ReadLittleEndian32(Bytes, Cursor, DeadbandBits))
                {
                    return false;
                }

                if (RawAuthority <= static_cast<uint8_t>(NetworkingSpace::AuthorityMode::PeerAuthoritative))
                {
                    Contract.Authority = static_cast<NetworkingSpace::AuthorityMode>(RawAuthority);
                }
                if (RawChannel <= static_cast<uint8_t>(NetworkingSpace::DeliveryChannel::ReliableOrdered))
                {
                    Contract.Channel = static_cast<NetworkingSpace::DeliveryChannel>(RawChannel);
                }
                Contract.PublishFrequencyHz         = std::bit_cast<float>(FreqBits);
                Contract.SpatialDeadbandMillimeters = std::bit_cast<float>(DeadbandBits);
                OutConfiguration.ReplicationContracts.push_back(Contract);
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool NetworkDecoder::DecodeFromBytes(
    std::span<const std::byte>   ContainerBytes,
    NetworkSpaceConfiguration&   OutConfiguration,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromBytes(ContainerBytes, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractConfiguration(Payload, OutConfiguration, OutDiagnostic, OptionalOutRevision);
}

bool NetworkDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    NetworkSpaceConfiguration&   OutConfiguration,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractConfiguration(Payload, OutConfiguration, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
