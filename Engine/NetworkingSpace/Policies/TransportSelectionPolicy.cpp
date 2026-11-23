/*============================================================================================================================================
                                                         TRANSPORTSELECTIONPOLICY.CPP
============================================================================================================================================*/
// 🧩 Minimax global relay cluster selection (US <-> China <-> Egypt) and transport display metadata

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Policies/TransportSelectionPolicy.h"

#include <algorithm>
#include <array>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                             RELAY CLUSTER & METADATA QUERIES
//------------------------------------------------------------------------------------------------------------------------

RelayClusterSelection SelectMinimaxRelayCluster(std::span<const PeerRegionLatencyReading> Readings) noexcept
{
    RelayClusterSelection Best{};
    if (Readings.empty())
    {
        return Best;
    }

    static constexpr std::array<RelayRegionCluster, 4u> CandidateClusters = {{
        RelayRegionCluster::UsEastVirginia,
        RelayRegionCluster::EuCentralFrankfurt,
        RelayRegionCluster::AsiaSingapore,
        RelayRegionCluster::AsiaTokyo,
    }};

    float BestWorstRtt = 1.0e9f;
    float BestMeanRtt  = 1.0e9f;

    for (RelayRegionCluster Cluster : CandidateClusters)
    {
        float WorstRtt = 0.0f;
        float SumRtt   = 0.0f;

        for (const PeerRegionLatencyReading& PeerReading : Readings)
        {
            float CandidateRtt = PeerReading.EuCentralRttMs;
            switch (Cluster)
            {
            case RelayRegionCluster::UsEastVirginia:     CandidateRtt = PeerReading.UsEastRttMs;        break;
            case RelayRegionCluster::EuCentralFrankfurt: CandidateRtt = PeerReading.EuCentralRttMs;     break;
            case RelayRegionCluster::AsiaSingapore:      CandidateRtt = PeerReading.AsiaSingaporeRttMs; break;
            case RelayRegionCluster::AsiaTokyo:          CandidateRtt = PeerReading.AsiaTokyoRttMs;     break;
            }
            WorstRtt = std::max(WorstRtt, CandidateRtt);
            SumRtt  += CandidateRtt;
        }

        const float MeanRtt = SumRtt / static_cast<float>(Readings.size());

        // 💡 Minimax fairness: minimize the highest-latency peer's RTT first, breaking ties by lowest mean RTT
        if (WorstRtt < BestWorstRtt - 0.5f ||
            (WorstRtt <= BestWorstRtt + 0.5f && MeanRtt < BestMeanRtt))
        {
            BestWorstRtt        = WorstRtt;
            BestMeanRtt         = MeanRtt;
            Best.OptimalCluster = Cluster;
            Best.WorstPeerRttMs = WorstRtt;
            Best.MeanPeerRttMs  = MeanRtt;
        }
    }

    return Best;
}

std::string_view InspectTransportCategoryTitle(TransportChannelCategory Category) noexcept
{
    switch (Category)
    {
    case TransportChannelCategory::None:            return "None";
    case TransportChannelCategory::Loopback:        return "Loopback";
    case TransportChannelCategory::SovereignSocket: return "SovereignUDP";
    case TransportChannelCategory::EpicOnline:      return "EOS";
    case TransportChannelCategory::PhotonRealtime:  return "Photon";
    }
    return "None";
}

std::string_view InspectRelayClusterCode(RelayRegionCluster Cluster) noexcept
{
    switch (Cluster)
    {
    case RelayRegionCluster::UsEastVirginia:     return "us-east";
    case RelayRegionCluster::EuCentralFrankfurt: return "eu-central";
    case RelayRegionCluster::AsiaSingapore:      return "ap-southeast";
    case RelayRegionCluster::AsiaTokyo:          return "ap-northeast";
    }
    return "eu-central";
}

} // namespace Frontier::NetworkingSpace
