//============================================================================================================================================
//                                                    GEOMETRYINTERCHANGE.CPP
//============================================================================================================================================

#include "SlateDocument/Document/GeometryInterchange/Api/GeometryInterchange.h"

namespace Slate
{

Deliver<GeometryIdentity> GeometryInterchange::AcceptDecoded(const DecodedTopology& Decoded,
                                                             const std::string& Name,
                                                             IntakeIndex& Intake)
{
    std::unique_ptr<TopologyStructure> Topology = std::make_unique<TopologyStructure>();
    const Deliver<bool> Intaken = Transfer.IntakeTopology(Decoded, *Topology, Intake);
    if (!Intaken.Resolved)
        return Deliver<GeometryIdentity>::Refuse(Intaken.Error);

    std::unique_ptr<TopologyConditioning> Conditioning = std::make_unique<TopologyConditioning>();
    const Deliver<bool> Conditioned = Conditioning->Condition(*Topology);
    if (!Conditioned.Resolved)
        return Deliver<GeometryIdentity>::Refuse(Conditioned.Error);

    std::uint32_t Slot = 0u;
    if (!ReleasedSlots.empty())
    {
        Slot = ReleasedSlots.back();
        ReleasedSlots.pop_back();
    }
    else
    {
        Slot = static_cast<std::uint32_t>(Assets.size());
        Assets.push_back(GeometryAsset{});
    }

    GeometryAsset& Registered = Assets[Slot];
    Registered.Topology = std::move(Topology);
    Registered.Conditioning = std::move(Conditioning);
    Registered.Name = Name;
    Registered.SourceRecord.OriginPath = Decoded.OriginPath;
    Registered.SourceRecord.MaterialNames = Decoded.MaterialNames;
    Registered.SourceRecord.ObjectMemberships = Decoded.ObjectMemberships;
    Registered.SourceRecord.GroupMemberships = Decoded.GroupMemberships;
    Registered.SourceRecord.UnsupportedNamed = Decoded.UnsupportedNamed;
    Registered.SourceRecord.UnitScale = Decoded.UnitScale;
    Registered.SourceRecord.UnitScaleDeclared = Decoded.UnitScaleDeclared;
    Registered.Occupied = true;
    if (Registered.Generation == 0u) Registered.Generation = 1u;
    ++OccupiedCount;

    GeometryIdentity Identity;
    Identity.SlotIndex = Slot;
    Identity.SlotGeneration = Registered.Generation;
    return Deliver<GeometryIdentity>::Result(Identity);
}

Deliver<GeometryAssetView> GeometryInterchange::Resolve(GeometryIdentity Subject) const
{
    if (!Subject.IdentityDeclared() || Subject.SlotIndex >= Assets.size())
        return Deliver<GeometryAssetView>::Refuse({ RefusalReason::IdentityStale, "the geometry identity is stale" });
    const GeometryAsset& Registered = Assets[Subject.SlotIndex];
    if (!Registered.Occupied || Registered.Generation != Subject.SlotGeneration)
        return Deliver<GeometryAssetView>::Refuse({ RefusalReason::IdentityStale, "the geometry identity is stale" });

    GeometryAssetView Delivered;
    Delivered.Identity = Subject;
    Delivered.Topology = Registered.Topology.get();
    Delivered.Conditioning = Registered.Conditioning.get();
    Delivered.Name = &Registered.Name;
    Delivered.SourceRecord = &Registered.SourceRecord;
    return Deliver<GeometryAssetView>::Result(Delivered);
}

Deliver<bool> GeometryInterchange::Retire(GeometryIdentity Subject)
{
    if (!Subject.IdentityDeclared() || Subject.SlotIndex >= Assets.size())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the geometry identity is stale" });
    GeometryAsset& Registered = Assets[Subject.SlotIndex];
    if (!Registered.Occupied || Registered.Generation != Subject.SlotGeneration)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the geometry identity is stale" });

    Registered.Topology.reset();
    Registered.Conditioning.reset();
    Registered.Name.clear();
    Registered.SourceRecord = GeometrySourceRecord{};
    Registered.Occupied = false;
    ++Registered.Generation;
    if (Registered.Generation == 0u) Registered.Generation = 1u;
    ReleasedSlots.push_back(Subject.SlotIndex);
    --OccupiedCount;
    return Deliver<bool>::Result(true);
}

void GeometryInterchange::Reclaim()
{
    // Keep the slots and advance every generation. Clearing the vector would let the next intake
    // recreate slot zero at generation one and make an identity issued before reclamation valid again.
    ReleasedSlots.clear();
    ReleasedSlots.reserve(Assets.size());
    for (std::uint32_t Slot = 0u; Slot < Assets.size(); ++Slot)
    {
        GeometryAsset& Registered = Assets[Slot];
        Registered.Topology.reset();
        Registered.Conditioning.reset();
        Registered.Name.clear();
        Registered.SourceRecord = GeometrySourceRecord{};
        Registered.Occupied = false;
        ++Registered.Generation;
        if (Registered.Generation == 0u) Registered.Generation = 1u;
        ReleasedSlots.push_back(Slot);
    }
    OccupiedCount = 0u;
}

} // namespace Slate
