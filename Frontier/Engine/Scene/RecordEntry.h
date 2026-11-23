/*==========================================================================================================================================
                                                               RECORD ENTRY
                                     ------------------------------------------------------------------
          Base of every scene object: the constructor registers it, the destructor unregisters it — nothing else can.
==========================================================================================================================================*/

// 🧩 Base of every scene object: the constructor registers it, the destructor unregisters it — nothing else can.

#pragma once

#include "RecordClassification.h"
#include "RecordToken.h"

#if defined(FRONTIER_EDITOR_PROFILE)
    #include "RecordEditorDescription.h"
#endif

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORD ENTRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Plan §6: construction issues a token, inserts into the RecordStore (every profile) and — Editor/Development
//    only — registers with the SceneDirectory so the object appears in the Outliner. Destruction reverses both.
//    Forgetting to register is impossible; there is no other registration API. Display name and Outliner nesting
//    are directory-side metadata set after construction — the base class never guesses a parent.
//    ⚠️ Records are constructed on the main thread in editor builds; job threads never construct a RecordEntry.
//    ⚠️ Records must not have static storage duration (the store/directory are function-local statics).
class RecordEntry
{
  public:
    explicit RecordEntry(RecordClassification Classification);
    virtual ~RecordEntry();

    RecordEntry(const RecordEntry&)            = delete;
    RecordEntry& operator=(const RecordEntry&) = delete;
    RecordEntry(RecordEntry&&)                 = delete;
    RecordEntry& operator=(RecordEntry&&)      = delete;

    [[nodiscard]] RecordToken          Token() const noexcept { return IssuedToken; }                  // [-] - Issued at construction
    [[nodiscard]] RecordClassification Classification() const noexcept { return EntryClassification; }   // [-] - Outliner category + page

#if defined(FRONTIER_EDITOR_PROFILE)
  protected:
    // 📝 Editor-conditional construction: a derived constructor builds its own outliner/inspector
    //    description and registers it here. The directory copy points into the record's live
    //    settings; unregistering in the destructor removes it before the token can be reused.
    void RegisterEditorDescription(RecordEditorDescription Description);
  private:
#endif

  private:
    RecordToken          IssuedToken;           // [-] - From TokenIssuer; authenticated through RecordStore
    RecordClassification EntryClassification;   // [-] - Fixed for the entry's whole lifetime
};

} // namespace Frontier
