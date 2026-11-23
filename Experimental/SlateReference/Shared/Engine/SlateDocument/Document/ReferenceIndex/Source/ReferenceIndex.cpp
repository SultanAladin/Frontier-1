//============================================================================================================================================
//                                                            REFERENCEINDEX.CPP
//============================================================================================================================================
// 🧩 `48` §5 — what one document depends on outside itself, each declared embedded or referenced, absence registered.

#include "SlateDocument/Document/ReferenceIndex/Api/ReferenceIndex.h"

namespace Slate
{

namespace
{

// 📝 Static text only. `ReportSpecification` holds every string as a literal, so nothing appended here can
//    outlive its storage or allocate on the thread that appends it.
constexpr const char* AbsenceOrigin  = "`48` §5";
constexpr const char* AbsenceDetail  = "a declared reference was not where the document named it; registered, never substituted";

/// 🧩 The subject spelling `86` presents beside an absence.
const char* SubjectSpelling(ReferenceSubject Subject)
{
    switch (Subject)
    {
        case ReferenceSubject::TexturedContent:   return "textured content";
        case ReferenceSubject::ImportedImagery:  return "imported imagery";
        case ReferenceSubject::ImportedTopology: return "imported topology";
        case ReferenceSubject::VectorContent:    return "vector content";
        case ReferenceSubject::TypefaceOutline:  return "typeface outline";
        default:                                 return "a declared reference";
    }
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ReferenceIndex::Declare(const DeclaredReference& Incoming)
{
    if (Incoming.Retention == ReferenceRetention::Referenced && Incoming.OriginPath.empty())
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "a referenced dependency naming no path can never be resolved — `48` §5" });
    }

    const std::uint32_t Registered = static_cast<std::uint32_t>(Declarations.size());

    Declarations.push_back(Incoming);
    AbsenceReported.push_back(false);

    if (Incoming.Condition == ReferenceCondition::Absent) { ++AbsentTotal; }

    return Deliver<std::uint32_t>::Result(Registered);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE STANDINGS
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ReferenceIndex::DeclareRetention(std::uint32_t ReferenceIndex, ReferenceRetention Declaring)
{
    if (ReferenceIndex >= Declarations.size())
    {
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no reference is declared at that ordinal" });
    }

    DeclaredReference& Amending = Declarations[ReferenceIndex];

    if (Declaring == ReferenceRetention::Referenced && Amending.OriginPath.empty())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a referenced dependency naming no path can never be resolved — `48` §5" });
    }

    Amending.Retention = Declaring;

    return Deliver<bool>::Result(true);
}

Deliver<bool> ReferenceIndex::DeclareResolved(std::uint32_t ReferenceIndex, std::uint64_t SpannedBytes)
{
    if (ReferenceIndex >= Declarations.size())
    {
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no reference is declared at that ordinal" });
    }

    DeclaredReference& Amending = Declarations[ReferenceIndex];

    // 📝 A reference that was absent and is now found leaves the absent count. The report already appended for
    //    it stays in the register, because it happened — `86` §2.2 appends at the occurrence and never retracts.
    if (Amending.Condition == ReferenceCondition::Absent && AbsentTotal > 0u) { --AbsentTotal; }

    Amending.Condition     = ReferenceCondition::Resolved;
    Amending.SpannedBytes = SpannedBytes;

    return Deliver<bool>::Result(true);
}

Deliver<bool> ReferenceIndex::DeclareAbsent(std::uint32_t ReferenceIndex)
{
    if (ReferenceIndex >= Declarations.size())
    {
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no reference is declared at that ordinal" });
    }

    DeclaredReference& Amending = Declarations[ReferenceIndex];

    if (Amending.Condition != ReferenceCondition::Absent) { ++AbsentTotal; }

    // 🔴 The origin path is untouched. It is the only thing that tells the artist which file to go and find,
    //    and the extent is cleared instead because the last reading described a file that is no longer there.
    Amending.Condition     = ReferenceCondition::Absent;
    Amending.SpannedBytes = 0u;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void ReferenceIndex::Report(ReportSequence& Reporting, TickPoint Sampled)
{
    for (std::size_t Index = 0u; Index < Declarations.size(); ++Index)
    {
        if (Declarations[Index].Condition != ReferenceCondition::Absent) { continue; }
        if (AbsenceReported[Index])                                    { continue; }

        ReportSpecification Appending;
        Appending.Origin         = AbsenceOrigin;
        Appending.Subject        = SubjectSpelling(Declarations[Index].Subject);
        Appending.Detail         = AbsenceDetail;
        Appending.SubjectIndex = static_cast<std::uint64_t>(Declarations[Index].Registered.SlotIndex);
        Appending.Verdict    = ReportVerdict::Rejected;
        Appending.Arrival        = Sampled;

        Reporting.Append(Appending);

        AbsenceReported[Index] = true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE READINGS
//------------------------------------------------------------------------------------------------------------------------

const std::vector<DeclaredReference>& ReferenceIndex::Declared() const
{
    return Declarations;
}

Deliver<DeclaredReference> ReferenceIndex::Resolve(const std::string& OriginPath) const
{
    // 📝 Walked backwards so the most recent declaration of one path is the one delivered. A path declared
    //    twice is two dependencies, and the later one is what the document last said about it.
    for (std::size_t Remaining = Declarations.size(); Remaining > 0u; --Remaining)
    {
        const DeclaredReference& Reading = Declarations[Remaining - 1u];

        if (Reading.OriginPath == OriginPath)
        {
            return Deliver<DeclaredReference>::Result(Reading);
        }
    }

    return Deliver<DeclaredReference>::Refuse({ RefusalReason::ExtentExhausted, "no reference names that path" });
}

void ReferenceIndex::DeclareTypefaceRetention(ReferenceRetention Declaring)
{
    TypefaceDeclared = Declaring;
}

ReferenceRetention ReferenceIndex::TypefaceRetention() const
{
    return TypefaceDeclared;
}

std::uint32_t ReferenceIndex::AbsentCount() const
{
    return AbsentTotal;
}

std::uint32_t ReferenceIndex::DeclaredCount() const
{
    return static_cast<std::uint32_t>(Declarations.size());
}

void ReferenceIndex::Reclaim()
{
    Declarations.clear();
    AbsenceReported.clear();
    AbsentTotal = 0u;

    // 📝 The typeface answer survives a reclaim deliberately. It is the document's own declaration, not a
    //    standing derived from the file system, and re-reading the document restores it rather than this.
}

}   // namespace Slate
