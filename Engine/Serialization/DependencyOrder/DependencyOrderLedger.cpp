/*============================================================================================================================================
                                                       DEPENDENCYORDERLEDGER.CPP
============================================================================================================================================*/
// 🧩 Implementation of acyclic topological sorting, exact cycle-chain reporting, and transitive xxHash64/SHA-256 signature calculation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/DependencyOrder/DependencyOrderLedger.h"

#include "Engine/Serialization/Container/AtomicFileWriter.h"

#include <algorithm>
#include <queue>
#include <sstream>

namespace Frontier::Serialization
{

namespace
{

enum class CycleVisitMark : uint8_t
{
    Unvisited = 0u, // [-] - Entry has not been visited during cycle trace
    Active    = 1u, // [-] - Entry sits on the active recursion stack
    Finished  = 2u  // [-] - Entry and all outgoing paths verified acyclic
};

[[nodiscard]] bool TraceCyclePath(
    std::size_t                                     CurrentIdx,
    const std::vector<std::vector<std::size_t>>&    OutgoingLists,
    const std::vector<DependencyBuildEntry>&        Entries,
    std::vector<CycleVisitMark>&                    VisitMarks,
    std::vector<std::size_t>&                       ActiveStack,
    std::vector<std::string>&                       OutCycleChain)
{
    VisitMarks[CurrentIdx] = CycleVisitMark::Active;
    ActiveStack.push_back(CurrentIdx);

    for (const std::size_t NextIdx : OutgoingLists[CurrentIdx])
    {
        if (VisitMarks[NextIdx] == CycleVisitMark::Active)
        {
            const auto CycleStart = std::find(ActiveStack.begin(), ActiveStack.end(), NextIdx);
            OutCycleChain.clear();
            for (auto Iter = CycleStart; Iter != ActiveStack.end(); ++Iter)
            {
                OutCycleChain.push_back(Entries[*Iter].EntryIdentifier);
            }
            OutCycleChain.push_back(Entries[NextIdx].EntryIdentifier);
            return true;
        }
        if (VisitMarks[NextIdx] == CycleVisitMark::Unvisited)
        {
            if (TraceCyclePath(NextIdx, OutgoingLists, Entries, VisitMarks, ActiveStack, OutCycleChain))
            {
                return true;
            }
        }
    }

    ActiveStack.pop_back();
    VisitMarks[CurrentIdx] = CycleVisitMark::Finished;
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               LEDGER REGISTRATION & SORT
//------------------------------------------------------------------------------------------------------------------------

bool DependencyOrderLedger::RegisterEntry(DependencyBuildEntry Entry, std::string& OutDiagnostic)
{
    if (Entry.EntryIdentifier.empty())
    {
        OutDiagnostic = "DependencyBuildEntry requires a non-empty EntryIdentifier.";
        return false;
    }
    if (IndexByIdentifier.contains(Entry.EntryIdentifier))
    {
        OutDiagnostic = "Duplicate DependencyBuildEntry EntryIdentifier: " + Entry.EntryIdentifier;
        return false;
    }

    const std::size_t SlotIndex = Entries.size();
    IndexByIdentifier.emplace(Entry.EntryIdentifier, SlotIndex);
    Entries.push_back(std::move(Entry));
    OutDiagnostic.clear();
    return true;
}

bool DependencyOrderLedger::UpdateSpecification(
    std::string_view EntryIdentifier,
    std::string      UpdatedSpecificationText,
    std::string&     OutDiagnostic)
{
    const auto Found = IndexByIdentifier.find(std::string(EntryIdentifier));
    if (Found == IndexByIdentifier.end())
    {
        OutDiagnostic = "Unknown DependencyBuildEntry EntryIdentifier: " + std::string(EntryIdentifier);
        return false;
    }
    Entries[Found->second].BuildSpecificationText = std::move(UpdatedSpecificationText);
    OutDiagnostic.clear();
    return true;
}

const DependencyBuildEntry* DependencyOrderLedger::FindEntry(std::string_view EntryIdentifier) const noexcept
{
    const auto Found = IndexByIdentifier.find(std::string(EntryIdentifier));
    if (Found == IndexByIdentifier.end())
    {
        return nullptr;
    }
    return &Entries[Found->second];
}

bool DependencyOrderLedger::ResolveTopologicalOrder(
    std::vector<std::string>& OutOrderedIdentifiers,
    std::vector<std::string>& OutCycleChain,
    std::string&              OutDiagnostic) const
{
    OutOrderedIdentifiers.clear();
    OutCycleChain.clear();

    const std::size_t EntryCount = Entries.size();
    std::vector<std::vector<std::size_t>> DownstreamLists(EntryCount);
    std::vector<std::size_t>              InDegree(EntryCount, 0u);

    // ① Validate all upstream references and build directed dependency edges (Upstream -> Downstream)
    for (std::size_t EntryIdx = 0u; EntryIdx < EntryCount; ++EntryIdx)
    {
        for (const std::string& UpstreamId : Entries[EntryIdx].UpstreamIdentifiers)
        {
            const auto Found = IndexByIdentifier.find(UpstreamId);
            if (Found == IndexByIdentifier.end())
            {
                OutDiagnostic = "Entry '" + Entries[EntryIdx].EntryIdentifier +
                                "' references missing upstream entry '" + UpstreamId + "'.";
                return false;
            }
            DownstreamLists[Found->second].push_back(EntryIdx);
            ++InDegree[EntryIdx];
        }
    }

    // ② Deterministic Kahn topological sort (lexicographical min-heap by EntryIdentifier)
    auto CompareEntryIds = [&](std::size_t LeftIdx, std::size_t RightIdx) -> bool
    {
        return Entries[LeftIdx].EntryIdentifier > Entries[RightIdx].EntryIdentifier;
    };
    std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(CompareEntryIds)> ReadyQueue(CompareEntryIds);

    for (std::size_t EntryIdx = 0u; EntryIdx < EntryCount; ++EntryIdx)
    {
        if (InDegree[EntryIdx] == 0u)
        {
            ReadyQueue.push(EntryIdx);
        }
    }

    OutOrderedIdentifiers.reserve(EntryCount);
    while (!ReadyQueue.empty())
    {
        const std::size_t CurrentIdx = ReadyQueue.top();
        ReadyQueue.pop();
        OutOrderedIdentifiers.push_back(Entries[CurrentIdx].EntryIdentifier);

        for (const std::size_t DownstreamIdx : DownstreamLists[CurrentIdx])
        {
            if (--InDegree[DownstreamIdx] == 0u)
            {
                ReadyQueue.push(DownstreamIdx);
            }
        }
    }

    // ③ If cycle exists, extract the exact cycle chain for actionable diagnostics
    if (OutOrderedIdentifiers.size() != EntryCount)
    {
        OutOrderedIdentifiers.clear();
        std::vector<CycleVisitMark> VisitMarks(EntryCount, CycleVisitMark::Unvisited);
        std::vector<std::size_t>    ActiveStack{};
        for (std::size_t EntryIdx = 0u; EntryIdx < EntryCount; ++EntryIdx)
        {
            if (VisitMarks[EntryIdx] == CycleVisitMark::Unvisited &&
                TraceCyclePath(EntryIdx, DownstreamLists, Entries, VisitMarks, ActiveStack, OutCycleChain))
            {
                break;
            }
        }

        std::ostringstream DiagStream{};
        DiagStream << "Cyclic build dependency detected: ";
        for (std::size_t Index = 0u; Index < OutCycleChain.size(); ++Index)
        {
            if (Index > 0u)
            {
                DiagStream << " -> ";
            }
            DiagStream << OutCycleChain[Index];
        }
        OutDiagnostic = DiagStream.str();
        return false;
    }

    OutDiagnostic.clear();
    return true;
}

bool DependencyOrderLedger::ResolveTopologicalWaves(
    std::vector<std::vector<std::string>>& OutParallelWaves,
    std::vector<std::string>&              OutCycleChain,
    std::string&                           OutDiagnostic) const
{
    OutParallelWaves.clear();
    OutCycleChain.clear();

    std::vector<std::string> OrderedIds{};
    if (!ResolveTopologicalOrder(OrderedIds, OutCycleChain, OutDiagnostic))
    {
        return false;
    }

    // Assign each entry its topological depth (0 for roots, 1 + max(upstream depth)) so same-depth entries run concurrently
    std::unordered_map<std::string, std::size_t> DepthById{};
    std::size_t MaxDepth = 0u;
    for (const std::string& EntryId : OrderedIds)
    {
        const DependencyBuildEntry& Entry = Entries[IndexByIdentifier.at(EntryId)];
        std::size_t Depth = 0u;
        for (const std::string& UpstreamId : Entry.UpstreamIdentifiers)
        {
            Depth = std::max(Depth, DepthById.at(UpstreamId) + 1u);
        }
        DepthById.emplace(EntryId, Depth);
        MaxDepth = std::max(MaxDepth, Depth);
    }

    if (!OrderedIds.empty())
    {
        OutParallelWaves.resize(MaxDepth + 1u);
        for (const std::string& EntryId : OrderedIds)
        {
            OutParallelWaves[DepthById.at(EntryId)].push_back(EntryId);
        }
    }

    OutDiagnostic.clear();
    return true;
}

bool DependencyOrderLedger::ComputeAllInputSignatures(
    std::unordered_map<std::string, BuildInputSignature>& OutSignatures,
    std::string&                                          OutDiagnostic) const
{
    OutSignatures.clear();

    std::vector<std::string> OrderedIds{};
    std::vector<std::string> CycleChain{};
    if (!ResolveTopologicalOrder(OrderedIds, CycleChain, OutDiagnostic))
    {
        return false;
    }

    for (const std::string& EntryId : OrderedIds)
    {
        const DependencyBuildEntry& Entry = Entries[IndexByIdentifier.at(EntryId)];

        std::ostringstream CanonicalPayload{};
        CanonicalPayload << "Entry=" << Entry.EntryIdentifier << '\n';
        CanonicalPayload << "Suffix=" << QueryCanonicalSuffix(Entry.Category) << '\n';
        CanonicalPayload << "SchemaVersion=" << Entry.SchemaVersion << '\n';
        CanonicalPayload << "CompilerVersion=" << Entry.CompilerVersion << '\n';
        CanonicalPayload << "TargetAbi=" << Entry.TargetAbi << '\n';
        CanonicalPayload << "HardwareDriverUuid=" << Entry.HardwareDriverUuid << '\n';
        CanonicalPayload << "Specification=" << Entry.BuildSpecificationText << '\n';

        // ① Include sorted source file hashes so file edits immediately alter signature
        std::vector<std::filesystem::path> SortedSources = Entry.SourceFilePaths;
        std::sort(SortedSources.begin(), SortedSources.end());
        for (const std::filesystem::path& SourcePath : SortedSources)
        {
            std::vector<std::byte> SourceBytes{};
            if (!ReadFileBytes(SourcePath, SourceBytes, OutDiagnostic))
            {
                return false;
            }
            const uint64_t   SourceXx  = ComputeXxHash64(SourceBytes);
            const Sha256Hash SourceSha = ComputeSha256(SourceBytes);
            CanonicalPayload << "Source=" << SourcePath.generic_string()
                             << "|XX=" << FormatHex64(SourceXx)
                             << "|SHA=" << FormatSha256Hex(SourceSha) << '\n';
        }

        // ② Include sorted upstream transitive signatures (guarantees minimal downstream invalidation)
        std::vector<std::string> SortedUpstreams = Entry.UpstreamIdentifiers;
        std::sort(SortedUpstreams.begin(), SortedUpstreams.end());
        for (const std::string& UpstreamId : SortedUpstreams)
        {
            const BuildInputSignature& UpSig = OutSignatures.at(UpstreamId);
            CanonicalPayload << "Upstream=" << UpstreamId
                             << "|XX=" << FormatHex64(UpSig.FastXxHash64)
                             << "|SHA=" << UpSig.CanonicalSha256Hex << '\n';
        }

        const std::string SerializedCanonical = CanonicalPayload.str();

        BuildInputSignature Signature{};
        Signature.FastXxHash64       = ComputeXxHash64String(SerializedCanonical);
        Signature.CanonicalSha256    = ComputeSha256String(SerializedCanonical);
        Signature.CanonicalSha256Hex = FormatSha256Hex(Signature.CanonicalSha256);
        Signature.SchemaVersion      = Entry.SchemaVersion;
        Signature.CompilerVersion    = Entry.CompilerVersion;
        Signature.TargetAbi          = Entry.TargetAbi;
        Signature.HardwareDriverUuid = Entry.HardwareDriverUuid;

        OutSignatures.emplace(EntryId, std::move(Signature));
    }

    OutDiagnostic.clear();
    return true;
}

std::size_t DependencyOrderLedger::QueryEntryCount() const noexcept
{
    return Entries.size();
}

void DependencyOrderLedger::Clear() noexcept
{
    Entries.clear();
    IndexByIdentifier.clear();
}

} // namespace Frontier::Serialization
