/*============================================================================================================================================
                                                       COMPILEDDISKREPOSITORY.CPP
============================================================================================================================================*/
// 🧩 Implementation of persistent compiled binary lookup, FNV-1a verification, atomic publication, and quota-bounded cleanup

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/DependencyOrder/CompiledDiskRepository.h"

#include "Engine/Serialization/Container/HybridContainerCodec.h"

#include <algorithm>
#include <chrono>

namespace Frontier::Serialization
{

namespace
{

constexpr std::string_view CompiledPayloadSectionName = "CompiledBinaryPayload";

[[nodiscard]] std::string StripLeadingDot(std::string_view Suffix)
{
    if (!Suffix.empty() && Suffix.front() == '.')
    {
        Suffix.remove_prefix(1u);
    }
    return std::string(Suffix);
}

struct StoredRepositoryFileEntry
{
    std::filesystem::path           FilePath         {};  // [-]   - Full path to .fbin file
    std::string                     Sha256Stem       {};  // [hash]- Filename stem (64-char SHA-256 hex)
    std::size_t                     ByteSize         = 0; // [B]   - File byte length on disk
    std::filesystem::file_time_type LastWriteTime    {};  // [s]   - OS modification timestamp for LRU pruning
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              REPOSITORY IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

CompiledDiskRepository::CompiledDiskRepository(std::filesystem::path RepositoryRootDirectory)
    : RepositoryRoot(std::move(RepositoryRootDirectory))
{
}

std::filesystem::path CompiledDiskRepository::ResolveCompiledBinaryPath(
    FormatSuffixCategory Category,
    std::string_view     CanonicalSha256Hex) const
{
    const std::string CategoryFolder = StripLeadingDot(QueryCanonicalSuffix(Category));
    return RepositoryRoot / CategoryFolder / (std::string(CanonicalSha256Hex) + ".fbin");
}

bool CompiledDiskRepository::ResolveOrBakeEntry(
    const DependencyBuildEntry&  Entry,
    const BuildInputSignature&   Signature,
    const BakeProcedureCallback& BakeProcedure,
    std::vector<std::byte>&      OutCompiledBytes,
    BuildSourceTrace&            OutTrace,
    std::string&                 OutDiagnostic) const
{
    const auto StartClock = std::chrono::steady_clock::now();
    OutCompiledBytes.clear();

    const std::filesystem::path BinaryPath = ResolveCompiledBinaryPath(Entry.Category, Signature.CanonicalSha256Hex);
    OutTrace                     = {};
    OutTrace.EntryIdentifier     = Entry.EntryIdentifier;
    OutTrace.CanonicalSha256Hex  = Signature.CanonicalSha256Hex;
    OutTrace.FastXxHash64        = Signature.FastXxHash64;
    OutTrace.CompiledBinaryPath  = BinaryPath;

    bool HadCorruptOnDisk = false;
    std::error_code FsError{};
    if (std::filesystem::exists(BinaryPath, FsError))
    {
        HybridContainerDocument LoadedDoc{};
        std::string             DecodeDiag{};
        if (HybridContainerDecoder::DecodeFromFile(BinaryPath, LoadedDoc, DecodeDiag))
        {
            const toml::table* SigSection = LoadedDoc.TomlHeader[".InputSignature"].as_table();
            const BinarySectionBlock* PayloadBlock =
                HybridContainerDecoder::FindBinarySection(LoadedDoc, CompiledPayloadSectionName);

            const bool SignatureMatched =
                (SigSection != nullptr) &&
                ((*SigSection)["CanonicalSha256"].value_or<std::string>("") == Signature.CanonicalSha256Hex) &&
                ((*SigSection)["FastXxHash64"].value_or<std::string>("") == FormatHex64(Signature.FastXxHash64)) &&
                ((*SigSection)["TargetAbi"].value_or<std::string>("") == Signature.TargetAbi) &&
                ((*SigSection)["HardwareDriverUuid"].value_or<std::string>("") == Signature.HardwareDriverUuid);

            if (SignatureMatched && PayloadBlock != nullptr)
            {
                OutCompiledBytes = PayloadBlock->PayloadBytes;
                const auto EndClock = std::chrono::steady_clock::now();
                OutTrace.ResolutionReason    = RepositoryResolutionReason::LoadedFromDisk;
                OutTrace.CompiledByteLength  = OutCompiledBytes.size();
                OutTrace.ElapsedMilliseconds = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();
                OutTrace.DiagnosticSummary   = "Disk hit: InputSignature & FNV-1a 64 verified (zero recomputation).";
                OutDiagnostic.clear();
                return true;
            }
        }
        HadCorruptOnDisk = true;
    }

    // ① Execute deterministic bake procedure when missing or corrupted
    if (!BakeProcedure(Entry, Signature, OutCompiledBytes, OutDiagnostic))
    {
        return false;
    }

    // ② Wrap compiled bytes in a verified Two-Tier Hybrid .fbin container with InputSignature stamp
    HybridContainerDocument CompiledDoc{};
    CompiledDoc.SuffixCategory     = Entry.Category;
    CompiledDoc.SchemaVersion      = Entry.SchemaVersion;
    CompiledDoc.StableIdentifier   = Entry.EntryIdentifier;
    CompiledDoc.DocumentTitle      = Entry.EntryIdentifier;
    CompiledDoc.AuthoringWorkspace = "Frontier.DependencyOrder";

    toml::table SigToml{};
    SigToml.insert("CanonicalSha256",    Signature.CanonicalSha256Hex);
    SigToml.insert("CompilerVersion",    static_cast<int64_t>(Signature.CompilerVersion));
    SigToml.insert("EntryIdentifier",    Entry.EntryIdentifier);
    SigToml.insert("FastXxHash64",       FormatHex64(Signature.FastXxHash64));
    SigToml.insert("HardwareDriverUuid", Signature.HardwareDriverUuid);
    SigToml.insert("SchemaVersion",      static_cast<int64_t>(Signature.SchemaVersion));
    SigToml.insert("TargetAbi",          Signature.TargetAbi);
    CompiledDoc.TomlHeader.insert(".InputSignature", std::move(SigToml));

    BinarySectionBlock PayloadSection{};
    PayloadSection.SectionIdentifier = std::string(CompiledPayloadSectionName);
    PayloadSection.SubFormatTag      = std::string(QueryCanonicalSuffix(Entry.Category));
    PayloadSection.PayloadBytes      = OutCompiledBytes;
    CompiledDoc.BinarySections.push_back(std::move(PayloadSection));

    if (!HybridContainerEncoder::EncodeToFile(BinaryPath, CompiledDoc, OutDiagnostic))
    {
        return false;
    }

    const auto EndClock = std::chrono::steady_clock::now();
    OutTrace.ResolutionReason    = HadCorruptOnDisk
                                       ? RepositoryResolutionReason::RebakedAfterCorrupt
                                       : RepositoryResolutionReason::BakedMissingBinary;
    OutTrace.CompiledByteLength  = OutCompiledBytes.size();
    OutTrace.ElapsedMilliseconds = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();
    OutTrace.DiagnosticSummary   = HadCorruptOnDisk
                                       ? "Integrity repair: corrupted binary failed FNV-1a/signature check and was rebuilt atomically."
                                       : "Fresh bake: no compiled binary matched InputSignature; published atomically to disk.";
    OutDiagnostic.clear();
    return true;
}

bool CompiledDiskRepository::ExecuteLedger(
    const DependencyOrderLedger& Ledger,
    const BakeProcedureCallback& BakeProcedure,
    RepositoryExecutionSummary&  OutSummary,
    std::string&                 OutDiagnostic) const
{
    OutSummary = {};

    std::vector<std::string> OrderedIds{};
    std::vector<std::string> CycleChain{};
    if (!Ledger.ResolveTopologicalOrder(OrderedIds, CycleChain, OutDiagnostic))
    {
        return false;
    }

    std::unordered_map<std::string, BuildInputSignature> Signatures{};
    if (!Ledger.ComputeAllInputSignatures(Signatures, OutDiagnostic))
    {
        return false;
    }

    for (const std::string& EntryId : OrderedIds)
    {
        const DependencyBuildEntry* EntryPtr = Ledger.FindEntry(EntryId);
        if (EntryPtr == nullptr)
        {
            OutDiagnostic = "Internal ledger lookup miss for: " + EntryId;
            return false;
        }

        std::vector<std::byte> CompiledBytes{};
        BuildSourceTrace       Trace{};
        if (!ResolveOrBakeEntry(*EntryPtr, Signatures.at(EntryId), BakeProcedure, CompiledBytes, Trace, OutDiagnostic))
        {
            return false;
        }

        switch (Trace.ResolutionReason)
        {
            case RepositoryResolutionReason::LoadedFromDisk:      ++OutSummary.DiskHitCount;       break;
            case RepositoryResolutionReason::BakedMissingBinary:  ++OutSummary.FreshBakeCount;     break;
            case RepositoryResolutionReason::RebakedAfterCorrupt: ++OutSummary.CorruptRepairCount; break;
        }
        OutSummary.Traces.push_back(std::move(Trace));
    }

    OutDiagnostic.clear();
    return true;
}

bool CompiledDiskRepository::PruneUnreachableAboveQuota(
    const std::unordered_set<std::string>& ActiveSha256HexKeys,
    std::size_t                            MaximumRepositoryBytes,
    uint32_t&                              OutRemovedFileCount,
    std::string&                           OutDiagnostic) const
{
    OutRemovedFileCount = 0u;
    std::error_code FsError{};
    if (!std::filesystem::exists(RepositoryRoot, FsError))
    {
        OutDiagnostic.clear();
        return true;
    }

    std::size_t                            TotalBytes = 0u;
    std::vector<StoredRepositoryFileEntry> UnreachableFiles{};

    for (const auto& DirEntry : std::filesystem::recursive_directory_iterator(RepositoryRoot, FsError))
    {
        if (FsError)
        {
            OutDiagnostic = "Directory iteration error during repository quota prune.";
            return false;
        }
        if (!DirEntry.is_regular_file(FsError) || DirEntry.path().extension() != ".fbin")
        {
            continue;
        }

        const std::size_t FileBytes = static_cast<std::size_t>(DirEntry.file_size(FsError));
        TotalBytes += FileBytes;

        const std::string Stem = DirEntry.path().stem().string();
        if (!ActiveSha256HexKeys.contains(Stem))
        {
            UnreachableFiles.push_back({
                DirEntry.path(),
                Stem,
                FileBytes,
                DirEntry.last_write_time(FsError)
            });
        }
    }

    if (TotalBytes <= MaximumRepositoryBytes)
    {
        OutDiagnostic.clear();
        return true;
    }

    // Evict oldest unreachable compiled binaries first — never delete active keys
    std::sort(
        UnreachableFiles.begin(),
        UnreachableFiles.end(),
        [](const StoredRepositoryFileEntry& Left, const StoredRepositoryFileEntry& Right)
        {
            return Left.LastWriteTime < Right.LastWriteTime;
        });

    for (const StoredRepositoryFileEntry& Candidate : UnreachableFiles)
    {
        if (TotalBytes <= MaximumRepositoryBytes)
        {
            break;
        }
        if (std::filesystem::remove(Candidate.FilePath, FsError))
        {
            TotalBytes -= Candidate.ByteSize;
            ++OutRemovedFileCount;
        }
    }

    OutDiagnostic.clear();
    return true;
}

const std::filesystem::path& CompiledDiskRepository::QueryRepositoryRoot() const noexcept
{
    return RepositoryRoot;
}

} // namespace Frontier::Serialization
