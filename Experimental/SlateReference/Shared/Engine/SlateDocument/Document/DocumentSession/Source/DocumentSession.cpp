//============================================================================================================================================
//                                                           DOCUMENTSESSION.CPP
//============================================================================================================================================
// 🧩 `48` §1 — one open document and everything true of it only while it is open, plus every open session at once.

#include "SlateDocument/Document/DocumentSession/Api/DocumentSession.h"

#include <cstddef>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IT HOLDS
//------------------------------------------------------------------------------------------------------------------------

OutlinerSequence& DocumentSession::Document()
{
    return Population;
}

const OutlinerSequence& DocumentSession::Document() const
{
    return Population;
}

ReferenceIndex& DocumentSession::References()
{
    return External;
}

const ReferenceIndex& DocumentSession::References() const
{
    return External;
}

RecoverySequence& DocumentSession::Journal()
{
    return Recovery;
}

const RecoverySequence& DocumentSession::Journal() const
{
    return Recovery;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE LOCATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DocumentSession::DeclareStorage(const std::string& DeclaredPath, const std::string& JournalPath)
{
    const Deliver<bool> Paired = Recovery.DeclareDocument(DeclaredPath, JournalPath);

    if (!Paired.Resolved)
    {
        return Paired;
    }

    StoragePath     = DeclaredPath;
    StorageDeclared = StorageCurrent::Declared;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SEALING
//------------------------------------------------------------------------------------------------------------------------

Deliver<SealedContent> DocumentSession::Seal(const std::vector<std::uint8_t>& Encoded, std::uint64_t SealedAt) const
{
    if (StorageDeclared != StorageCurrent::Declared)
    {
        return Deliver<SealedContent>::Refuse(
            { RefusalReason::ContentUnsupported, "this session has no storage location to save to — `48` §2" });
    }

    // 🔴 `48` §3: a save reads sealed state. An open transaction refuses the capture rather than being sealed
    //    by it — a half-finished drag is not a state the artist asked to keep, and sealing it on their behalf
    //    puts an edit they had not decided on into `RevisionSequence` where they meet it only afterwards.
    if (Population.Revisions().TransactionOpen())
    {
        return Deliver<SealedContent>::Refuse(
            { RefusalReason::ExtentExhausted, "a transaction is open; a save reads sealed state only — `48` §3" });
    }

    SealedContent Capturing;
    Capturing.Content       = Encoded;
    Capturing.TargetPath    = StoragePath;
    Capturing.SavedThrough  = Population.Revisions().ScrubPosition();
    Capturing.SealedAt      = SealedAt;
    Capturing.StreamVersion = CurrentStreamVersion;

    // 📝 The revision ordinal is the **scrub position** and not the committed count. An artist who undoes three
    //    transactions and saves has saved the document they are looking at, and a journal retired against the
    //    committed count would discard the three the file does not carry.
    return Deliver<SealedContent>::Result(Capturing);
}

void DocumentSession::DeclareSaved(const PersistenceConclusion& Completed)
{
    if (Completed.Reached != PersistenceStep::Replaced) { return; }

    SavedRevision      = Completed.SavedThrough;
    SavedStamp         = Completed.SavedAt;
    AmendmentsDeclared = false;

    // 📝 `48` §3 ④, run here because the journal belongs to the session and the save ran off the tick. What
    //    remains past the save is exactly what a crash after this point would have to replay.
    Recovery.Retire(Completed.SavedThrough);
}

void DocumentSession::DeclareAmended()
{
    AmendmentsDeclared = true;
}

bool DocumentSession::AmendmentsCurrent() const
{
    return AmendmentsDeclared;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SESSION STATE
//------------------------------------------------------------------------------------------------------------------------

void DocumentSession::DeclareCurrentCamera(OwnerIdentity CameraOwner)
{
    CameraIdentity = CameraOwner;
}

OwnerIdentity DocumentSession::CurrentCamera() const
{
    return CameraIdentity;
}

void DocumentSession::DeclareScrollPosition(std::uint32_t VisiblePosition)
{
    ScrollVisible = VisiblePosition;
}

std::uint32_t DocumentSession::ScrollPosition() const
{
    return ScrollVisible;
}

const std::string& DocumentSession::StorageOrigin() const
{
    return StoragePath;
}

StorageCurrent DocumentSession::Current() const
{
    return StorageDeclared;
}

std::uint64_t DocumentSession::SavedThrough() const
{
    return SavedRevision;
}

std::uint64_t DocumentSession::SavedAt() const
{
    return SavedStamp;
}

void DocumentSession::DeclareReadVersion(std::uint32_t ReadFrom)
{
    VersionRead = ReadFrom;
}

std::uint32_t DocumentSession::ReadVersion() const
{
    return VersionRead;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   EVERY OPEN SESSION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> SessionIndex::Open()
{
    // 📝 A closed slot is reused before the span grows, so a session opened and closed repeatedly does not walk
    //    the ordinal upward until it meets the ceiling.
    for (std::size_t Index = 0u; Index < Sessions.size(); ++Index)
    {
        if (Sessions[Index] != nullptr) { continue; }

        Sessions[Index] = std::make_unique<DocumentSession>();
        ++OpenTotal;

        if (CurrentSession == SessionLimit)
        {
            CurrentSession = static_cast<std::uint32_t>(Index);
        }

        return Deliver<std::uint32_t>::Result(static_cast<std::uint32_t>(Index));
    }

    if (Sessions.size() >= static_cast<std::size_t>(SessionLimit))
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "the declared session ceiling is reached — `48` §6" });
    }

    const std::uint32_t Registered = static_cast<std::uint32_t>(Sessions.size());

    Sessions.push_back(std::make_unique<DocumentSession>());
    ++OpenTotal;

    if (CurrentSession == SessionLimit)
    {
        CurrentSession = Registered;
    }

    return Deliver<std::uint32_t>::Result(Registered);
}

Deliver<bool> SessionIndex::Close(std::uint32_t SessionIndex)
{
    if (SessionIndex >= Sessions.size() || Sessions[SessionIndex] == nullptr)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no session is open at that ordinal" });
    }

    Sessions[SessionIndex].reset();
    --OpenTotal;

    if (CurrentSession != SessionIndex)
    {
        return Deliver<bool>::Result(true);
    }

    // 📝 The presentation moves to the first session still open rather than to none. Closing one of two open
    //    documents and being left presenting nothing reads as the application having closed both.
    CurrentSession = SessionLimit;

    for (std::size_t Index = 0u; Index < Sessions.size(); ++Index)
    {
        if (Sessions[Index] == nullptr) { continue; }

        CurrentSession = static_cast<std::uint32_t>(Index);
        break;
    }

    return Deliver<bool>::Result(true);
}

Deliver<DocumentSession*> SessionIndex::Resolve(std::uint32_t SessionIndex)
{
    if (SessionIndex >= Sessions.size() || Sessions[SessionIndex] == nullptr)
    {
        return Deliver<DocumentSession*>::Refuse({ RefusalReason::ExtentExhausted, "no session is open at that ordinal" });
    }

    return Deliver<DocumentSession*>::Result(Sessions[SessionIndex].get());
}

Deliver<const DocumentSession*> SessionIndex::Resolve(std::uint32_t SessionIndex) const
{
    if (SessionIndex >= Sessions.size() || Sessions[SessionIndex] == nullptr)
    {
        return Deliver<const DocumentSession*>::Refuse({ RefusalReason::ExtentExhausted, "no session is open at that ordinal" });
    }

    return Deliver<const DocumentSession*>::Result(Sessions[SessionIndex].get());
}

Deliver<bool> SessionIndex::DeclareCurrent(std::uint32_t SessionIndex)
{
    if (SessionIndex >= Sessions.size() || Sessions[SessionIndex] == nullptr)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no session is open at that ordinal" });
    }

    CurrentSession = SessionIndex;

    return Deliver<bool>::Result(true);
}

Deliver<DocumentSession*> SessionIndex::Current()
{
    return Resolve(CurrentSession);
}

Deliver<const DocumentSession*> SessionIndex::Current() const
{
    return Resolve(CurrentSession);
}

std::uint32_t SessionIndex::CurrentIndex() const
{
    return CurrentSession;
}

Deliver<std::uint32_t> SessionIndex::Located(const std::string& StoragePath) const
{
    for (std::size_t Remaining = Sessions.size(); Remaining > 0u; --Remaining)
    {
        const std::size_t Index = Remaining - 1u;

        if (Sessions[Index] == nullptr)                             { continue; }
        if (Sessions[Index]->StorageOrigin() != StoragePath)        { continue; }

        return Deliver<std::uint32_t>::Result(static_cast<std::uint32_t>(Index));
    }

    return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted, "no open session holds that location" });
}

std::uint32_t SessionIndex::OpenCount() const
{
    return OpenTotal;
}

std::uint32_t SessionIndex::SpannedCount() const
{
    return static_cast<std::uint32_t>(Sessions.size());
}

void SessionIndex::Reclaim()
{
    Sessions.clear();
    CurrentSession = SessionLimit;
    OpenTotal        = 0u;
}

}   // namespace Slate
