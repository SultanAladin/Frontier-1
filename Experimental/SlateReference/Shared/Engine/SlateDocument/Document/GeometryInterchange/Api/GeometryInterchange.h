//============================================================================================================================================
//                                                      GEOMETRYINTERCHANGE.H
//============================================================================================================================================
// 🧩 Authoritative geometry intake and lifetime, apart from file codecs and GPU presentation.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/Identity.h"
#include "SlateDocument/Document/AssetInterchange/Api/AssetInterchange.h"
#include "SlateDocument/Document/TopologyConditioning/Api/TopologyConditioning.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Slate
{

/// 🧩 Source facts retained beside authoritative topology rather than inferred again at export.
struct GeometrySourceRecord
{
    std::string OriginPath = {};
    std::vector<std::string> MaterialNames = {};
    std::vector<DecodedFaceSet> ObjectMemberships = {};
    std::vector<DecodedFaceSet> GroupMemberships = {};
    std::vector<std::string> UnsupportedNamed = {};
    double UnitScale = 1.0;
    bool UnitScaleDeclared = false;
};

struct GeometryAssetView
{
    GeometryIdentity Identity = {};
    const TopologyStructure* Topology = nullptr;
    const TopologyConditioning* Conditioning = nullptr;
    const std::string* Name = nullptr;
    const GeometrySourceRecord* SourceRecord = nullptr;
};

/// 🧩 Registers faithful decoded geometry atomically, derives immutable companions, and issues stable identities.
/// note  GeometryFormatExchange owns codec dispatch. A future GeometryRenderingExchange consumes these views.
class GeometryInterchange
{
public:
    Deliver<GeometryIdentity> AcceptDecoded(const DecodedTopology& Decoded,
                                            const std::string& Name,
                                            IntakeIndex& Intake);
    Deliver<GeometryAssetView> Resolve(GeometryIdentity Subject) const;
    Deliver<bool> Retire(GeometryIdentity Subject);
    std::uint32_t DeclaredCount() const { return OccupiedCount; }
    const AssetInterchange& AssetTransfer() const { return Transfer; }
    void Reclaim();

private:
    struct GeometryAsset
    {
        std::unique_ptr<TopologyStructure> Topology;
        std::unique_ptr<TopologyConditioning> Conditioning;
        std::string Name;
        GeometrySourceRecord SourceRecord;
        std::uint32_t Generation = 1u;
        bool Occupied = false;
    };

    std::vector<GeometryAsset> Assets;
    std::vector<std::uint32_t> ReleasedSlots;
    AssetInterchange Transfer = {};
    std::uint32_t OccupiedCount = 0u;
};

} // namespace Slate
