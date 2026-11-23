/*==========================================================================================================================================
                                                               RECORD STORE
                                     ------------------------------------------------------------------
              Runtime token-to-record registry, present in every profile: construction inserts, destruction removes.
==========================================================================================================================================*/

// 🧩 Runtime token-to-record registry, present in every profile: construction inserts, destruction removes.

#pragma once

#include "RecordToken.h"

#include <cstddef>
#include <mutex>
#include <unordered_map>

namespace Frontier
{

class RecordEntry;

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORD STORE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The runtime half of record registration (plan §5/§6): compiled into Shipping too, because game code resolves
//    records by token at runtime. Only RecordEntry's constructor/destructor insert and remove — nothing registers
//    by hand anywhere else. Records are constructed on the main thread; lookups are mutex-guarded so worker and
//    render divisions may resolve tokens safely. Records must not have static storage duration: the store is a
//    function-local static and unregistration during its own destruction is not supported.
class RecordStore
{
  public:
    [[nodiscard]] static RecordStore& Instance();

    RecordStore()                                = default;
    RecordStore(const RecordStore&)            = delete;
    RecordStore& operator=(const RecordStore&) = delete;

    // Called only by RecordEntry's constructor. Refuses an invalid or duplicate token (logged); returns success.
    bool Insert(RecordEntry& Entry);

    // Called only by RecordEntry's destructor. Removing an unknown token is a logged no-op.
    void Remove(RecordToken Token);

    // Resolves a token to its live record, or nullptr when the record is gone (stale tokens simply miss).
    [[nodiscard]] RecordEntry*       Find(RecordToken Token) const;
    [[nodiscard]] const RecordEntry* FindConst(RecordToken Token) const { return Find(Token); }

    // Authentication: true only when Token currently names exactly this entry (pointer and token agree).
    [[nodiscard]] bool Authenticate(RecordToken Token, const RecordEntry& Entry) const;

    [[nodiscard]] size_t QueryCount() const;   // [records] - Live records right now

  private:
    mutable std::mutex                          Mutex;
    std::unordered_map<uint64_t, RecordEntry*>  Entries;   // [-] - Token value → live record
};

} // namespace Frontier
