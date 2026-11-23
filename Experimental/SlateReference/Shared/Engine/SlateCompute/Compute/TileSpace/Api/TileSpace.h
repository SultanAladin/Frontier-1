//============================================================================================================================================
//                                                               TILESPACE.H
//============================================================================================================================================
// 🧩 Physical tile extents, sliced and reclaimed — slot index and byte offsets, and never a texel.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/NumericTolerance.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE TILE
//------------------------------------------------------------------------------------------------------------------------

// 📝 No slot; never a valid tile ordinal. Declared per unit, matching `12`'s `AbsentSlot` and `34`'s `AbsentWork`
//    — nothing reads two of them, and a shared spelling would be a dependency edge no traversal can see.
inline constexpr std::uint32_t AbsentTile = 0xFFFFFFFFu;   // [-] - no tile slot

/// 🧩 Texels per edge a resident tile actually stores — the tile plus its apron on each side.
/// note  🔴 `20` §1: the apron duplicates the neighbouring tiles' border texels so that filtered sampling and
///        brush impressions near a tile edge read valid neighbours without a residency test per texel. Four
///        texels covers trilinear filtering plus the widest impression footprint at the levels that are stored.
/// note  ⚠️ Without it every seam in the domain is visible in the textured result, and the artist reads that as
///        a defect in their brush rather than as a defect in residency.
inline constexpr std::uint32_t StoredTexelsPerEdge = PhysicalTileTexels + 2u * PhysicalTileApron;   // [-] - 136

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SLOT INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The physical tile slots one surface's residency draws from, and the byte offsets they occupy.
/// note  🔴 This holds **no texels**. `20` §5's gate is that no tile is the source of truth for any content —
///        `56` is, and a tile is a projection of it. What is held here is which slots are claimed and where each
///        one would sit inside a device extent `06` claims; the texels live on the device and nowhere else.
/// note  🔴 A released slot is **quarantined** for the recording slot count before it is reusable — `20` §5. A slot
///        reused in the same rotation is one the device may still be sampling from, and the artist sees another
///        cell's content appear inside theirs for exactly one rotation, which nobody attributes to reclamation.
/// tag   owning
class TileSpace
{
public:

    /// 🧩 Sizes the index to a slot ceiling and a declared texel width.
    /// in    SlotLimit   [-]  tiles the surface's backing extent holds
    /// in    BytesPerTexel [B]  the surface's channel set, as a width
    /// out   Result       [-]  refuses with ContentUnsupported for a ceiling or width of zero
    /// post  every slot is free; nothing is quarantined
    /// note  📝 The ceiling is the caller's, because it follows from how much extent `06` claimed and `06` §3
    ///        refuses a reserved claim rather than granting a smaller one. Deriving it here would mean deriving
    ///        it from a device this component is forbidden to name.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> ReserveTileSpace(std::uint32_t SlotLimit, std::uint32_t BytesPerTexel);

    /// 🧩 Reservations one free slot.
    /// out   Result  [-]  refuses with ExtentExhausted when every slot is claimed or quarantined
    /// note  🔴 A refusal is **not** a failure — `20` §2.2 and `86` §5. Every slot claimed means the promotion
    ///        must evict first, and exhaustion during ordinary texturing is residency policy operating as
    ///        designed. Reporting it would mean the register is never quiet.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Reserve();

    /// 🧩 Releases one claimed slot into quarantine.
    /// in    SlotIndex      [-]  the slot
    /// in    RecordingIndex  [-]  the rotation the release happened on
    /// out   Result          [-]  refuses with ContentUnsupported for an unclaimed or out-of-range slot
    /// post  the slot is unusable until `RecordingSlotCount` rotations have passed
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Release(std::uint32_t SlotIndex, std::uint64_t RecordingIndex);

    /// 🧩 Returns quarantined slots whose release is older than the recording slot count.
    /// in    RecordingIndex  [-]  the rotation now being recorded
    /// out   Reclaimed        [-]  how many slots became free
    /// note  🔴 This is the whole of `20` §5's deferred reclamation, and it is one comparison. Reclaiming
    ///        immediately is the defect that costs nothing to write and is invisible until a device is fast
    ///        enough to still be reading the slot.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::uint32_t Reclaim(std::uint64_t RecordingIndex);

    /// 🧩 Where one slot sits inside the surface's backing extent.
    /// out   Result  [-]  refuses with ContentUnsupported outside the ceiling
    /// note  A byte offset rather than an address, because the extent it indexes is `06`'s and this component
    ///        may not name it. `06` adds the base; nothing here knows one exists.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint64_t> ByteOffsetOf(std::uint32_t SlotIndex) const;

    /// 🧩 What one tile occupies, apron included.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t StoredBytesPerTile() const;

    /// 🧩 What the whole backing extent occupies — what `06` must claim for this surface.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t BackingBytes() const;

    std::uint32_t SlotLimit() const;
    std::uint32_t HeldCount() const;
    std::uint32_t QuarantinedCount() const;
    std::uint32_t FreeCount() const;

    /// 🧩 🔍 Whether every slot is in exactly one of free, claimed and quarantined.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    bool InteractionConsistent() const;

private:

    enum class SlotCondition : std::uint32_t
    {
        Free        = 0u,   // [-] - claimable now
        Held     = 1u,   // [-] - a resident cell holds it
        Quarantined = 2u    // [-] - released; the device may still be reading it
    };

    std::vector<SlotCondition>   Conditions;                 // [-] - one per slot
    std::vector<std::uint64_t>  ReleasedAt;               // [-] - rotation each quarantined slot was released
    std::vector<std::uint32_t>  FreeIndexs;             // [-] - claimable slots, most recently freed first
    std::uint64_t               TileBytes         = 0u;   // [B] - StoredTexelsPerEdge² × the declared width
    std::uint32_t               Limit           = 0u;   // [-] - slots the index spans
    std::uint32_t               HeldSlots      = 0u;   // [-]
    std::uint32_t               QuarantinedSlots  = 0u;   // [-]
};

}   // namespace Slate
