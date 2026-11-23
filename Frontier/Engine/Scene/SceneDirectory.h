/*==========================================================================================================================================
                                                             SCENE DIRECTORY
                                     ------------------------------------------------------------------
          Editor-machinery Outliner model: names and nesting for every registered record, Editor/Development only.
==========================================================================================================================================*/

// 🧩 Editor-machinery Outliner model: names and nesting for every registered record, Editor/Development only.

#pragma once

#include "RecordClassification.h"
#include "RecordToken.h"

#if defined(FRONTIER_EDITOR_PROFILE)
    #include "RecordEditorDescription.h"
#endif

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{

class RecordEntry;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE DIRECTORY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The Outliner ("SceneDirectory" in code per plan D2): directory-side metadata for records — display name and
//    nesting — while identity itself lives in the RecordStore. RecordEntry's constructor/destructor drive
//    Register/Unregister under Editor/Development profiles only; Shipping compiles those calls out and its builds
//    do not link SceneDirectory.cpp, so Shipping binaries contain no directory at all (plan §5/§6).
//    Nesting is a directory-side link set after construction by the creating code; destroying a parent re-roots
//    its children rather than leaving dangling up-links.
class SceneDirectory
{
  public:
    [[nodiscard]] static SceneDirectory& Instance();

    SceneDirectory()                                   = default;
    SceneDirectory(const SceneDirectory&)            = delete;
    SceneDirectory& operator=(const SceneDirectory&) = delete;

    // Called only by RecordEntry's constructor (Editor/Development). Re-registering a token is refused (logged).
    void Register(const RecordEntry& Entry);

    // Called only by RecordEntry's destructor. Children of the removed entry become roots.
    void Unregister(RecordToken Token);

    // Directory-side metadata. Both return false (logged) for unknown tokens; ConfigureParent also refuses
    // self-parenting and cycles. Passing an invalid Parent token makes Child a root.
    bool ConfigureName(RecordToken Token, std::string Name);
    bool ConfigureParent(RecordToken Child, RecordToken Parent);

    // Outliner visibility (the demo rows' eye toggle): records start visible; hidden records stay registered.
    bool ConfigureVisible(RecordToken Token, bool Visible);
    [[nodiscard]] bool QueryVisible(RecordToken Token) const;   // [-] - False for unknown tokens

    [[nodiscard]] bool                 QueryRegistered(RecordToken Token) const;
    [[nodiscard]] std::string          QueryName(RecordToken Token) const;        // [text] - Configured name, else "<Classification> <token>"
    [[nodiscard]] RecordToken          QueryParent(RecordToken Token) const;      // [-] - Invalid token for roots/unknown
    [[nodiscard]] RecordClassification QueryClassification(RecordToken Token) const;
    [[nodiscard]] uint32_t             QueryChildCount(RecordToken Token) const;  // [records]
    [[nodiscard]] size_t               QueryCount() const;                        // [records]

    // Registration order snapshot (oldest first) for panel projections; copied under lock.
    void CopyRegistrationOrder(std::vector<RecordToken>& Out) const;

#if defined(FRONTIER_EDITOR_PROFILE)
    // 📝 Editor descriptions are built by each record's own editor-conditional constructor and
    //    registered through RecordEntry. The returned pointer is invalidated by Unregister.
    void ConfigureEditorDescription(RecordToken Token, RecordEditorDescription Description);
    [[nodiscard]] const RecordEditorDescription* QueryEditorDescription(RecordToken Token) const;
#endif

  private:
    struct DirectoryEntry
    {
        std::string          Name;                          // [text] - Empty means "use the default name"
        RecordToken          Parent;                        // [-] - Invalid token = root
        RecordClassification Classification = RecordClassification::Unclassified;
        bool                 Visible        = true;         // [-] - Outliner eye toggle
#if defined(FRONTIER_EDITOR_PROFILE)
        RecordEditorDescription EditorDescription;          // [-] - Built by the record itself; empty until it registers one
        bool                    HasEditorDescription = false;
#endif
    };

    mutable std::mutex                              Mutex;
    std::vector<RecordToken>                        RegistrationOrder;   // [-] - Oldest registration first
    std::unordered_map<uint64_t, DirectoryEntry>    Entries;             // [-] - Token value → directory metadata
};

} // namespace Frontier
