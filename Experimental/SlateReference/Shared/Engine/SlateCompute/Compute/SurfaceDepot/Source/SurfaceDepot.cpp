//============================================================================================================================================
//                                                            SURFACEDEPOT.CPP
//============================================================================================================================================
// 🧩 The reconstructibility refusal, key resolution, and least-recently-resolved eviction.

#include "SlateCompute/Compute/SurfaceDepot/Api/SurfaceDepot.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SurfaceDepot::ReserveSurfaceStorage(std::uint64_t ByteLimit_)
{
    if (ByteLimit_ == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a depot of no extent holds nothing" });

    Held.clear();
    Limit  = ByteLimit_;
    Occupied = 0u;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ADMISSION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SurfaceDepot::Declare(const ContentKey&  Keyed,
                                    LayerContentSource Source,
                                    std::uint64_t      ByteExtent,
                                    std::uint64_t      RecordingIndex)
{
    // 🔴 `20` §5's gate, enforced at the one door into the depot. `56` §3's own predicate decides it, so the
    //    classification lives with the document that owns the content rather than with the residency that
    //    can only see texels.
    if (!SourceReconstructible(Source))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "textured texels are authored content and are never evictable" });
    }

    if (ByteExtent == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "an artefact of no extent" });

    if (ByteExtent > Limit)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ExtentExhausted, "the artefact alone exceeds the whole depot" });
    }

    // 📝 A re-declaration of the same key replaces what stood. Two artefacts under one key would both be
    //    resolvable and the resolution would take whichever was accepted first, which is the older of the two.
    for (std::size_t Index = 0u; Index < Held.size(); ++Index)
    {
        if (!KeysAgree(Held[Index].Keyed, Keyed))
            continue;

        Occupied -= Held[Index].ByteExtent;
        Held.erase(Held.begin() + static_cast<std::ptrdiff_t>(Index));
        break;
    }

    if (Occupied + ByteExtent > Limit)
        Evict((Occupied + ByteExtent) - Limit);

    DepotArtefact Accepting;
    Accepting.Keyed      = Keyed;
    Accepting.Source     = Source;
    Accepting.ByteExtent = ByteExtent;
    Accepting.ResolvedAt = RecordingIndex;

    Held.push_back(Accepting);
    Occupied += ByteExtent;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<DepotArtefact> SurfaceDepot::Resolve(const ContentKey& Keyed, std::uint64_t RecordingIndex)
{
    for (DepotArtefact& Current : Held)
    {
        if (!KeysAgree(Current.Keyed, Keyed))
            continue;

        // 📝 Marked here rather than by a separate call, because an artefact resolved and not marked is one the
        //    eviction ordering believes is unused — and the tile being promoted from it right now is the one
        //    that gets evicted.
        Current.ResolvedAt = RecordingIndex;
        ++ResolvedTotal;

        return Deliver<DepotArtefact>::Result(Current);
    }

    return Deliver<DepotArtefact>::Refuse({ RefusalReason::ExtentExhausted, "nothing is held under that key" });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      EVICTION
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t SurfaceDepot::Evict(std::uint64_t ByteExtent)
{
    std::uint32_t Evicted = 0u;
    std::uint64_t Freed   = 0u;

    while (Freed < ByteExtent && !Held.empty())
    {
        // 📝 Minimum recently resolved. An artefact resolved this rotation is one a promotion is reading now, and
        //    evicting it to make room for the promotion that is reading it is the one ordering that cannot work.
        std::size_t Oldest = 0u;

        for (std::size_t Index = 1u; Index < Held.size(); ++Index)
        {
            if (Held[Index].ResolvedAt < Held[Oldest].ResolvedAt)
                Oldest = Index;
        }

        Freed    += Held[Oldest].ByteExtent;
        Occupied -= Held[Oldest].ByteExtent;

        Held.erase(Held.begin() + static_cast<std::ptrdiff_t>(Oldest));

        ++Evicted;
        ++EvictedTotal;
    }

    return Evicted;
}

std::uint32_t SurfaceDepot::Supersede(std::uint64_t PartitionRevision)
{
    std::uint32_t Discarded = 0u;

    for (std::size_t Index = Held.size(); Index-- > 0u;)
    {
        if (Held[Index].Keyed.PartitionRevision >= PartitionRevision)
            continue;

        Occupied -= Held[Index].ByteExtent;
        Held.erase(Held.begin() + static_cast<std::ptrdiff_t>(Index));

        ++Discarded;
        ++EvictedTotal;
    }

    return Discarded;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

std::uint64_t SurfaceDepot::OccupiedBytes() const { return Occupied;      }
std::uint64_t SurfaceDepot::ByteLimit() const   { return Limit;       }
std::uint64_t SurfaceDepot::ResolvedCount() const { return ResolvedTotal; }
std::uint64_t SurfaceDepot::EvictedCount() const  { return EvictedTotal;  }

std::uint32_t SurfaceDepot::HeldCount() const
{
    return static_cast<std::uint32_t>(Held.size());
}

bool SurfaceDepot::DepotConsistent() const
{
    std::uint64_t Accumulated = 0u;

    for (const DepotArtefact& Current : Held)
    {
        if (!SourceReconstructible(Current.Source))
            return false;

        Accumulated += Current.ByteExtent;
    }

    return Accumulated == Occupied && Occupied <= Limit;
}

}   // namespace Slate
