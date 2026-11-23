//============================================================================================================================================
//                                                            DOCUMENTSESSION.H
//============================================================================================================================================
// 🧩 `48` §1 — one open document and everything true of it only while it is open, plus every open session at once.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/OutlinerSequence/Api/OutlinerSequence.h"
#include "SlateDocument/Document/PersistenceSequence/Api/PersistenceSequence.h"
#include "SlateDocument/Document/RecoverySequence/Api/RecoverySequence.h"
#include "SlateDocument/Document/ReferenceIndex/Api/ReferenceIndex.h"
#include "SlateDocument/Format/FormatCodec/Api/FormatCodec.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHERE IT CAME FROM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether an open session has a file behind it yet.
/// note  📝 A session with no location is a document the artist began rather than opened. It is not an error and
///        it is not a lesser document — it simply has no target for a save until one is declared.
/// tag   guarantee
enum class StorageCurrent : std::uint32_t
{
    Undeclared    = 0u,   // [-] - begun here; no file behind it yet
    Declared      = 1u,   // [-] - opened from, or last saved to, a location
    CurrentCount = 2u    // [-] - the closed count, never a standing
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE SESSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One open document — the population it holds, and everything true of it only while it is open.
/// note  🔴 `48` §2's three columns. The population, revisions, working space, cameras and enrollment are held in
///        the **document**; the storage location, unsaved standing, selection, presented camera and outliner
///        expansion are held **here only**; the display space, device residency and `20`'s resident tiles are held
///        in neither and are re-derived. A saved derivation is a saved opportunity for the file to disagree with
///        itself, so nothing derived is written and nothing derived is stored here either.
/// note  🔴 `SelectionSequence` sits inside `OutlinerSequence` and is session state — `48` §2 and `12` §11. It is
///        not written on save. A document that reopened with someone else's selection restored has restored a
///        decision the artist had already finished making, and the first stroke lands on the wrong owner.
/// note  ⚠️ Non-copyable. Two copies of one open document are two populations issuing identities from two indexs,
///        and the generation that makes a reference safe is only unique within one of them.
/// tag   owning
class DocumentSession
{
public:

    DocumentSession()                                  = default;
    DocumentSession(const DocumentSession&)            = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    /// 🧩 The document itself — population, relations, revisions, rows and subsets.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    OutlinerSequence&       Document();
    const OutlinerSequence& Document() const;

    /// 🧩 What this document depends on outside itself — `48` §5.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ReferenceIndex&       References();
    const ReferenceIndex& References() const;

    /// 🧩 This document's own journal — `48` §4.1, one per document and never per application.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    RecoverySequence&       Journal();
    const RecoverySequence& Journal() const;

    /// 🧩 Declares where this session's document lives, and where its journal is written beside it.
    /// in    DeclaredPath  [-]  UTF-8; the document's location
    /// in    JournalPath   [-]  UTF-8; the journal's own location
    /// out   Result       [-]  refuses with ContentUnsupported when either path is empty
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareStorage(const std::string& DeclaredPath, const std::string& JournalPath);

    /// 🧩 Captures everything a save reads, from sealed state only — `48` §3.
    /// in    Encoded   [-]  the document as `FormatCodec` wrote it; sealed transactions only
    /// in    SealedAt  [ns] the tick's reading at capture
    /// out   Result   [-]  refuses with ContentUnsupported when no storage location is declared, and with
    ///                      ExtentExhausted when a transaction is open
    /// note  🔴 An open transaction refuses the capture rather than being sealed by it. Sealing someone's
    ///        half-finished drag on their behalf commits an edit they had not decided to keep, and it would
    ///        land in `RevisionSequence` where they can only undo it after the fact.
    /// note  📝 The encoding is handed in rather than performed here. `10`'s codecs know the stream layout and
    ///        this knows the session; a session that encoded would be a second place the layout is written.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<SealedContent> Seal(const std::vector<std::uint8_t>& Encoded, std::uint64_t SealedAt) const;

    /// 🧩 Records that a save landed, so the session stops standing amended.
    /// in    Completed  [-]  what `PersistenceSequence::Persist` delivered
    /// post  the journal entries the save subsumes are retired — `48` §3 ④
    /// note  📝 Called on the tick, after the save's conclusion is drained from `34`. Retiring the journal from
    ///        the worker would retire it before the requester knew the replacement had landed.
    /// cost  🚩
    /// tag   api, nonthrowing
    void DeclareSaved(const PersistenceConclusion& Completed);

    /// 🧩 Records that the document was amended, so a save is owed and the journal has a tail.
    /// note  📝 Declared by whoever sealed the transaction rather than derived from a revision count. A count
    ///        comparison cannot tell an amendment from a scrub back to the saved position and forward again.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareAmended();

    /// 🧩 Whether this session holds amendments the file does not.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool AmendmentsCurrent() const;

    /// 🧩 Which camera owner this session presents. Session state — `48` §2.
    /// note  🔴 The cameras are in the document and which one is presented is not. Two artists opening one file
    ///        should each look at it from where they left off, not from where the last person to save was.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void             DeclareCurrentCamera(OwnerIdentity CameraOwner);
    OwnerIdentity CurrentCamera() const;

    /// 🧩 Where the outliner is scrolled to. Session state — `48` §2.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void          DeclareScrollPosition(std::uint32_t VisiblePosition);
    std::uint32_t ScrollPosition() const;

    /// 🧩 Where this session's document lives, empty while the standing is Undeclared.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::string& StorageOrigin() const;

    /// 🧩 Whether a file stands behind this session yet.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    StorageCurrent Current() const;

    /// 🧩 The revision ordinal the file on disc carries; zero when nothing has been saved.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t SavedThrough() const;

    /// 🧩 When the last save landed; zero when nothing has been saved.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t SavedAt() const;

    /// 🧩 The stream version this document was read at, migrated to the current one — `48` §7.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void          DeclareReadVersion(std::uint32_t ReadFrom);
    std::uint32_t ReadVersion() const;

private:

    OutlinerSequence   Population;                                       // [-]  - the document; `12` owns its order
    ReferenceIndex     External;                                         // [-]  - `48` §5's dependencies
    RecoverySequence   Recovery;                                         // [-]  - `48` §4.1's per-document journal

    std::string        StoragePath        = {};                          // [-]  - UTF-8; empty while Undeclared
    OwnerIdentity   CameraIdentity   = {};                          // [-]  - session state, never written
    std::uint64_t      SavedRevision      = 0u;                          // [-]  - what the file carries
    std::uint64_t      SavedStamp         = 0u;                          // [ns] - when the replacement landed
    std::uint32_t      ScrollVisible      = 0u;                          // [-]  - session state, never written
    std::uint32_t      VersionRead        = CurrentStreamVersion;        // [-]  - `48` §7's migrated-from version
    StorageCurrent    StorageDeclared    = StorageCurrent::Undeclared; // [-]  - whether a file stands behind it
    bool               AmendmentsDeclared = false;                       // [-]  - session state; a save is owed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   EVERY OPEN SESSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every open document, and which one the interface presents — `48` §6.
/// note  🔴 `48` §6: tool state is **per application** and is not held here. `ToolSequence` is the declared home
///        for it, one instance beside this index rather than one per session. An artist who sets a brush size and
///        switches document is expressing a preference about how they are working, not about that file.
/// note  🔴 Sessions are held behind pointers so that opening or closing one does not move the others. Every
///        reference an interface panel holds across a tick would otherwise dangle the moment a second document
///        opened, and the defect appears only for artists who work with more than one file at a time.
/// tag   owning
class SessionIndex
{
public:

    // 📝 A bound rather than an absence of one. The ceiling is what makes an unbounded open loop report instead
    //    of exhausting the host, and every open document costs its whole population.
    static constexpr std::uint32_t SessionLimit = 64u;   // [-] - documents open at once

    /// 🧩 Opens one session and issues the ordinal that addresses it.
    /// out   Result  [-]  refuses with ExtentExhausted at the ceiling
    /// post  the new session is presented when it is the first one, and is otherwise not
    /// note  📝 Opening a second document does not steal the display from the first. An artist importing a
    ///        reference file mid-stroke would otherwise lose the workspace they were texturing in.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Open();

    /// 🧩 Closes one session, discarding everything held only while it was open.
    /// out   Result  [-]  refuses with ExtentExhausted outside the open count
    /// post  the presented ordinal moves to another open session, or to none when this was the last
    /// note  ⚠️ Nothing here asks whether amendments stand. `48` §4 makes that the caller's question, because
    ///        the answer is a conversation with the artist and this component cannot have one.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Close(std::uint32_t SessionIndex);

    /// 🧩 One open session.
    /// out   Result  [-]  refuses with ExtentExhausted outside the open count, and for a closed ordinal
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<DocumentSession*>       Resolve(std::uint32_t SessionIndex);
    Deliver<const DocumentSession*> Resolve(std::uint32_t SessionIndex) const;

    /// 🧩 Declares which session the interface presents — `14` presents one at a time.
    /// out   Result  [-]  refuses with ExtentExhausted outside the open count, and for a closed ordinal
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareCurrent(std::uint32_t SessionIndex);

    /// 🧩 The session the interface presents.
    /// out   Result  [-]  refuses with ExtentExhausted when no session is open
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<DocumentSession*>       Current();
    Deliver<const DocumentSession*> Current() const;

    /// 🧩 Which ordinal is presented; the ceiling when nothing is open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t CurrentIndex() const;

    /// 🧩 The most recently opened session naming one storage location.
    /// out   Result  [-]  refuses with ExtentExhausted when no open session holds that path
    /// note  📝 What an open-file action asks before opening a second copy. Two sessions over one file are two
    ///        journals against it, and §4.1's pairing then cannot say which one recovers it.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Located(const std::string& StoragePath) const;

    /// 🧩 How many sessions are open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t OpenCount() const;

    /// 🧩 How many slots the index spans, open or not.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t SpannedCount() const;

    /// 🧩 Closes every session. Called at process teardown and by nothing else.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

private:

    std::vector<std::unique_ptr<DocumentSession>>  Sessions;                            // [-] - by session ordinal; empty where closed
    std::uint32_t                                  CurrentSession = SessionLimit;   // [-] - the ceiling declares none
    std::uint32_t                                  OpenTotal        = 0u;               // [-] - sessions currently open
};

}   // namespace Slate
