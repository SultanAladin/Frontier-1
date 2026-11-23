/*============================================================================================================================================
                                                        COMPILEDDISKREPOSITORY.H
============================================================================================================================================*/
// 🧩 Persistent zero-recompute compiled binary disk repository keyed by transitive SHA-256 InputSignatures with atomic publication & quota GC

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/DependencyOrder/DependencyOrderLedger.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class RepositoryResolutionReason : uint8_t
{
    LoadedFromDisk      = 0u, // [-] - Exact InputSignature & FNV-1a 64 verified on disk (zero recomputation)
    BakedMissingBinary  = 1u, // [-] - No compiled binary existed for this InputSignature; baked fresh
    RebakedAfterCorrupt = 2u  // [-] - Existing binary failed integrity/signature check; rebuilt atomically
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BuildSourceTrace
{
    std::string                EntryIdentifier     {};                                            // [-]   - Resolved build entry key
    std::string                CanonicalSha256Hex  {};                                            // [hash]- 64-char SHA-256 repository key
    uint64_t                   FastXxHash64        = 0u;                                          // [hash]- Fast xxHash64 stamp
    RepositoryResolutionReason ResolutionReason    = RepositoryResolutionReason::LoadedFromDisk;  // [-]   - Hit vs. fresh bake reason
    std::filesystem::path      CompiledBinaryPath  {};                                            // [-]   - On-disk .fbin container path
    std::size_t                CompiledByteLength  = 0u;                                          // [B]   - Payload byte size
    double                     ElapsedMilliseconds = 0.0;                                         // [ms]  - Wall-clock resolve/bake duration
    std::string                DiagnosticSummary   {};                                            // [-]   - Human-readable source trace explanation
};

struct RepositoryExecutionSummary
{
    uint32_t                      DiskHitCount       = 0u; // [count] - Entries loaded directly from disk without recomputing
    uint32_t                      FreshBakeCount     = 0u; // [count] - Entries compiled because no matching binary existed
    uint32_t                      CorruptRepairCount = 0u; // [count] - Corrupted binaries automatically rebuilt atomically
    std::vector<BuildSourceTrace> Traces             {};   // [-]     - Ordered per-entry resolution traces
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class CompiledDiskRepository
{
public:
    using BakeProcedureCallback = std::function<bool(
        const DependencyBuildEntry& Entry,
        const BuildInputSignature&  Signature,
        std::vector<std::byte>&     OutCompiledBytes,
        std::string&                OutDiagnostic)>;

    explicit CompiledDiskRepository(std::filesystem::path RepositoryRootDirectory);

    [[nodiscard]] std::filesystem::path ResolveCompiledBinaryPath(
        FormatSuffixCategory Category,
        std::string_view     CanonicalSha256Hex) const;

    [[nodiscard]] bool ResolveOrBakeEntry(
        const DependencyBuildEntry&  Entry,
        const BuildInputSignature&   Signature,
        const BakeProcedureCallback& BakeProcedure,
        std::vector<std::byte>&      OutCompiledBytes,
        BuildSourceTrace&            OutTrace,
        std::string&                 OutDiagnostic) const;

    [[nodiscard]] bool ExecuteLedger(
        const DependencyOrderLedger&  Ledger,
        const BakeProcedureCallback&  BakeProcedure,
        RepositoryExecutionSummary&   OutSummary,
        std::string&                  OutDiagnostic,
        Threading::ExecutionChannels* OptionalChannels = nullptr) const;

    [[nodiscard]] bool PruneUnreachableAboveQuota(
        const std::unordered_set<std::string>& ActiveSha256HexKeys,
        std::size_t                            MaximumRepositoryBytes,
        uint32_t&                              OutRemovedFileCount,
        std::string&                           OutDiagnostic) const;

    [[nodiscard]] const std::filesystem::path& QueryRepositoryRoot() const noexcept;

private:
    std::filesystem::path RepositoryRoot {}; // [-] - Root directory (e.g. Build/CompiledRepository)
};

} // namespace Frontier::Serialization
