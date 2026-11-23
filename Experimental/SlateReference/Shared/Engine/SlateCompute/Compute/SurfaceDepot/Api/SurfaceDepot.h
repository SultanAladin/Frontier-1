//============================================================================================================================================
//                                                             SURFACEDEPOT.H
//============================================================================================================================================
// 🧩 Derived, evictable, reconstructible artefacts keyed by content — and nothing authored, ever.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CONTENT KEY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What an artefact was derived from — every revision that, having moved, invalidates it.
/// note  🔴 `24` §3 fixes these fields: both topology revisions, the chart partition revision, the specification,
///        and the domain extent. The partition revision is the one most easily left out, and leaving it out is
///        the defect that matters — re-unwrapping moves every domain position, so an artefact keyed without it
///        survives a re-unwrap and is then read at positions that mean something else. That presents as
///        attributes subtly wrong everywhere rather than as an obvious failure.
/// tag   nonallocating, nonthrowing
struct ContentKey
{
    std::uint64_t  SourceRevision       = 0u;   // [-] - the dense topology the transfer read
    std::uint64_t  WorkingRevision      = 0u;   // [-] - the sparse topology it wrote onto
    std::uint64_t  PartitionRevision    = 0u;   // [-] - `68`'s; every domain position moves with it
    std::uint64_t  SpecificationIndex = 0u;   // [-] - which transfer or resolution produced it
    std::uint32_t  ExtentTexels         = 0u;   // [px] - the extent it was written at
    std::uint32_t  ChannelMask          = 0u;   // [-]  - which of `42`'s channels it carries
};

/// 🧩 Whether two keys describe the same derivation.
/// note  Exact throughout — every field is an integer, and a key compared approximately is an artefact read
///        at a revision it does not describe.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool KeysAgree(const ContentKey& Left, const ContentKey& Right)
{
    return Left.SourceRevision       == Right.SourceRevision
        && Left.WorkingRevision      == Right.WorkingRevision
        && Left.PartitionRevision    == Right.PartitionRevision
        && Left.SpecificationIndex == Right.SpecificationIndex
        && Left.ExtentTexels         == Right.ExtentTexels
        && Left.ChannelMask          == Right.ChannelMask;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE ARTEFACT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One derived artefact the depot holds.
/// tag   nonallocating, nonthrowing
struct DepotArtefact
{
    ContentKey          Keyed      = {};                                    // [-]  - what it was derived from
    LayerContentSource  Source     = LayerContentSource::AnalyticResolution; // [-]  - must be reconstructible
    std::uint64_t       ByteExtent = 0u;                                    // [B]  - what it occupies
    std::uint64_t       ResolvedAt = 0u;                                    // [-]  - the rotation it was last read
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE DEPOT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The evictable half of a surface's residency — reduction levels and transferred results, keyed by content.
/// note  🔴 `20` §4: eviction requires reconstructibility. An artefact that cannot be rebuilt is not evictable
///        and does not belong here, and `Declare` refuses one outright rather than accepting it and hoping
///        nothing evicts it.
/// note  🔴 The decision is read from `56` §3 and never made here — `SourceReconstructible` is that document's
///        function and this calls it. `20` never decides what may be discarded; it reads the decision the
///        document already made, because a residency system classifying content itself would be classifying
///        content it can only see as texels.
/// note  ⚠️ `Cache` is banned and the substitution is not a euphemism. A cache is a copy of something that
///        exists elsewhere; a depot is a store of artefacts that exist nowhere else and are cheaper to rebuild
///        than to keep. `24` §3's transferred result is the second kind, which is why it may be evicted at all.
/// tag   owning
class SurfaceDepot
{
public:

    /// 🧩 Sizes the depot to a byte ceiling.
    /// out   Result  [-]  refuses with ContentUnsupported for a ceiling of zero
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ReserveSurfaceStorage(std::uint64_t ByteLimit);

    /// 🧩 Accepts one derived artefact, evicting to make room for it.
    /// in    Keyed       [-]  what it was derived from
    /// in    Source      [-]  which of `56` §3's four sources produced it
    /// in    ByteExtent  [B]  what it occupies
    /// in    RecordingIndex [-]  the rotation it was derived on
    /// out   Result     [-]  refuses with ContentUnsupported for an unreconstructible source, and with
    ///                        ExtentExhausted when the artefact alone exceeds the whole ceiling
    /// note  🔴 A textured source is rejected. `20` §4: textured texels are authored content and live in `56`'s
    ///        layer sequence; accepting them here would make the artist's work evictable, and it would be
    ///        evicted under exactly the memory pressure a long texturing session produces.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Declare(const ContentKey&  Keyed,
                          LayerContentSource Source,
                          std::uint64_t      ByteExtent,
                          std::uint64_t      RecordingIndex);

    /// 🧩 Resolves one artefact by its content key, marking it recently read.
    /// out   Result  [-]  refuses with ExtentExhausted when nothing matching is held
    /// note  🔴 A key that differs in any field resolves to nothing rather than to the nearest artefact. That
    ///        refusal is `20` §2.1's second reconstruction source declining, and the promotion then falls
    ///        through to the third — which is correct, and slower, and visible only as one deferred tile.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<DepotArtefact> Resolve(const ContentKey& Keyed, std::uint64_t RecordingIndex);

    /// 🧩 Evicts least-recently-resolved artefacts until the declared extent is free.
    /// out   Evicted  [-]  how many artefacts left
    /// cost  🚩
    /// tag   api, nonthrowing
    std::uint32_t Evict(std::uint64_t ByteExtent);

    /// 🧩 Discards every artefact whose partition revision has been superseded.
    /// in    PartitionRevision  [-]  the revision `68` has advanced to
    /// out   Discarded          [-]  how many artefacts the re-partition invalidated
    /// note  🔴 `68` §6: a re-partition moves domain positions, so every artefact addressed in the old domain is
    ///        invalid. Discarding them here rather than letting them fail their key comparison one at a time
    ///        returns their extent immediately, which is exactly when the re-resolution needs it.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::uint32_t Supersede(std::uint64_t PartitionRevision);

    std::uint64_t OccupiedBytes() const;
    std::uint64_t ByteLimit() const;
    std::uint32_t HeldCount() const;
    std::uint64_t ResolvedCount() const;
    std::uint64_t EvictedCount() const;

    /// 🧩 🔍 Whether every held artefact is reconstructible and the occupancy agrees with what is held.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    bool DepotConsistent() const;

private:

    std::vector<DepotArtefact>  Held;                     // [-] - in admission order
    std::uint64_t               Limit        = 0u;      // [B]
    std::uint64_t               Occupied       = 0u;      // [B]
    std::uint64_t               ResolvedTotal  = 0u;      // [-]
    std::uint64_t               EvictedTotal   = 0u;      // [-]
};

}   // namespace Slate
