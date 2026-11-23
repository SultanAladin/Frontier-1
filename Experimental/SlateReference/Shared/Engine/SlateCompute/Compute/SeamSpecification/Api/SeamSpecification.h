//============================================================================================================================================
//                                                          SEAMSPECIFICATION.H
//============================================================================================================================================
// 🧩 Where the topology is cut — authored seams that survive every re-partition, derived seams that do not.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE SEAM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One cut edge, named by the two imported vertex ordinals it runs between.
/// note  🔴 `68` §2: a seam is an **edge identity**, never a position. `38`'s conditioning may weld a vertex to a
///        neighbour; a seam recorded as a position would then be a seam somewhere else, and the surface would cut
///        through the middle of a chart.
/// note  🔴 Imported vertex ordinals rather than welded ones, because `38` §7 guarantees an index into the
///        imported arrays means the same thing after conditioning as before. A welded ordinal is derived and
///        moves when the welding tolerance moves — which is exactly what an authored seam must survive.
/// tag   nonallocating, nonthrowing
struct SeamEdge
{
    std::uint32_t  MinimumVertex    = 0u;   // [-] - the lesser imported vertex ordinal
    std::uint32_t  MaximumVertex = 0u;   // [-] - the greater; normalised so one edge has one spelling
};

/// 🧩 Normalises two imported vertex ordinals into one seam edge.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr SeamEdge DeclareEdge(std::uint32_t FirstVertex, std::uint32_t SecondVertex)
{
    SeamEdge Declaring;
    Declaring.MinimumVertex    = FirstVertex < SecondVertex ? FirstVertex  : SecondVertex;
    Declaring.MaximumVertex = FirstVertex < SecondVertex ? SecondVertex : FirstVertex;

    return Declaring;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SEAMS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One surface's seams, both sources held apart because they have different lifetimes.
/// note  🔴 `68` §2: an authored seam is a document amendment in `10`'s `RevisionSequence` and is **never**
///        discarded by a re-unwrap. An artist who spent an hour marking seams and watched an automatic partition
///        erase them has lost work the program told them it was holding.
/// note  🔴 Derived seams are **added** to the authored set, never substituted for it, and survive only until the
///        next partition. Where a surface will not flatten within the declared distortion using the authored set
///        alone, the shortfall is cut and the addition is reported through `86` — a silent extra cut is a seam
///        the artist finds later in the textured result.
/// tag   owning
class SeamSpecification
{
public:

    /// 🧩 Declares one authored seam.
    /// in    FirstVertex   [-]  one imported vertex ordinal
    /// in    SecondVertex  [-]  the other
    /// out   Result       [-]  refuses with ContentUnsupported when the two ordinals are the same vertex
    /// post  the seam survives every re-partition until it is withdrawn
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareAuthored(std::uint32_t FirstVertex, std::uint32_t SecondVertex);

    /// 🧩 Withdraws one authored seam.
    /// out   Result  [-]  refuses with ContentUnsupported when no authored seam runs between the two
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> RemoveAuthored(std::uint32_t FirstVertex, std::uint32_t SecondVertex);

    /// 🧩 Records one seam the partitioner derived, for reporting and for the standing partition.
    /// note  Held apart from the authored set so that `ReclaimDerived` cannot reach an authored seam. The two
    ///        sets are separately stored for the same reason `12` separately stores its two relations.
    /// cost  🚩
    /// tag   api, nonthrowing
    void DeclareDerived(std::uint32_t FirstVertex, std::uint32_t SecondVertex);

    /// 🧩 Discards every derived seam, ahead of a re-partition.
    /// post  🔴 the authored set is untouched
    /// cost  ✔️
    /// tag   api, nonthrowing
    void ReclaimDerived();

    /// 🧩 Whether either set cuts the edge between two imported vertices.
    /// cost  🚩
    /// tag   api, nonthrowing
    bool SeamDeclared(std::uint32_t FirstVertex, std::uint32_t SecondVertex) const;

    /// 🧩 The authored set, in declaration order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<SeamEdge>& Authored() const;

    /// 🧩 The derived set, in derivation order — `86`'s `68` §2 row.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<SeamEdge>& Derived() const;

    /// 🧩 The revision the last authored amendment advanced to.
    /// note  Only the authored set advances it. A derived seam is a consequence of the partition rather than an
    ///        input to it, so counting one would make every partition invalidate the partition that produced it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t Revision() const;

    std::uint32_t AuthoredCount() const;
    std::uint32_t DerivedCount() const;

private:

    std::vector<SeamEdge>  AuthoredEdges;         // [-] - persisted by `10`; survives a re-partition
    std::vector<SeamEdge>  DerivedEdges;          // [-] - discarded at the next partition
    std::uint64_t          AuthoredRevision = 1u; // [-] - advanced by an authored amendment only
};

}   // namespace Slate
