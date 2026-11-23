//============================================================================================================================================
//                                                            REVISIONSEQUENCE.H
//============================================================================================================================================
// 🧩 Ordered, scrubbable sequence of committed transactions, with the drag lifecycle every edit uses.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE DRAG LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where an interactive edit stands. Every edit in the engine is a drag and passes through these four.
/// note  🔴 An open transaction is absent from the sequence and is not scrubbable. A drag recording one
///       transaction per pointer sample fills the sequence with positions the artist never meant to stop at.
/// tag   guarantee
enum class TransactionPhase : std::uint32_t
{
    Open     = 0u,   // [-] - the edit began; the prior content of the extent it will touch is held
    Amend    = 1u,   // [-] - the edit's parameters changed; nothing is recorded
    Abandon  = 2u,   // [-] - the edit ended with no effect; the prior content is restored
    Seal     = 3u    // [-] - the edit ended; one transaction enters the sequence
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE TRANSACTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One committed transaction — the forward operation and the inverse that undoes it.
/// note  Scrubbing backwards replays inverses rather than restoring snapshots, which is what lets a texture
///       stroke's inverse be bounded by the extent the stroke touched rather than by the whole surface.
/// tag   owning
struct CommittedTransaction
{
    std::string    Description    = {};     // [-]  - supplied at Open; what `84` presents to the artist
    std::string    OperationName  = {};     // [-]  - the mechanism's own spelling, presented when no
                                            //        description was supplied
    std::uint64_t  ForwardIndex = 0u;     // [-]  - where the forward operation's record begins
    std::uint64_t  InverseIndex = 0u;     // [-]  - where its inverse begins
    std::uint64_t  SealedAt       = 0u;     // [ns] - arrival stamp at Seal, for the merge interval
    bool           MergeDeclared  = false;  // [-]  - this operation declares itself mergeable
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The document's committed revisions, scrubbable in both directions.
/// note  ⚠️ `HistoryStack` is the retired spelling: the sequence is scrubbed in both directions rather
///       than merely popped, and `History` is banned.
/// tag   owning
class RevisionSequence
{
public:

    static constexpr std::uint64_t MergeInterval = 500000000ull;   // [ns] - adjacency window for merging

    /// 🧩 Opens a transaction. Nothing enters the sequence until it is sealed.
    /// in    Description   [-]  what `84` presents; empty defers to OperationName
    /// in    OperationName [-]  the mechanism's spelling, always supplied
    /// out   Result       [-]  refuses when a transaction is already open
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Open(const std::string& Description, const std::string& OperationName);

    /// 🧩 Ends the open transaction with no effect. The prior content is restored by the caller.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Abandon();

    /// 🧩 Ends the open transaction and enters exactly one transaction into the sequence.
    /// in    SealedAt      [ns]  the arrival stamp at which the edit ended
    /// in    MergeDeclared [-]   whether this operation declares itself mergeable — never inferred
    /// out   Result       [-]   refuses when no transaction is open
    /// post  the scrub position is the end of the sequence
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Seal(std::uint64_t SealedAt, bool MergeDeclared);

    /// 🧩 Scrubs one transaction backwards, replaying its inverse.
    /// out   Result  [-]  refuses when the scrub position is already at the beginning
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Retreat();

    /// 🧩 Scrubs one transaction forwards, replaying its forward operation.
    /// out   Result  [-]  refuses when the scrub position is already at the end
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Advance();

    /// 🧩 The committed transactions, in order, for `84` to present.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<CommittedTransaction>& Committed() const;

    /// 🧩 Where the scrub position sits; equal to the committed count when nothing is undone.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t ScrubPosition() const;

    /// 🧩 Whether a transaction is currently open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool TransactionOpen() const;

private:

    std::vector<CommittedTransaction>  CommittedOrder;              // [-] - the sequence itself
    CommittedTransaction               OpenTransaction   = {};      // [-] - held outside the sequence
    std::uint64_t                      ScrubIndex      = 0u;      // [-] - transactions currently applied
    bool                               OpenDeclared      = false;   // [-] - a transaction is open
};

}   // namespace Slate
