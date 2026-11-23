/*============================================================================================================================================
                                                        DEPENDENCYORDERLEDGER.H
============================================================================================================================================*/
// 🧩 Acyclic incremental build order ledger computing transitive xxHash64/SHA-256 InputSignatures and detecting dependency cycles

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/SuffixRegistry.h"
#include "Engine/Serialization/DependencyOrder/ContentHashCalculator.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BuildInputSignature
{
    uint64_t    FastXxHash64       = 0u;  // [hash] - Fast dirty-scan hash over source bytes + upstream hashes
    Sha256Hash  CanonicalSha256    {};    // [hash] - Collision-resistant repository filename key
    std::string CanonicalSha256Hex {};    // [hash] - Lowercase 64-character hex representation
    uint32_t    SchemaVersion      = 1u;  // [ver]  - Format schema version
    uint32_t    CompilerVersion    = 1u;  // [ver]  - Compiler/baker algorithm revision
    std::string TargetAbi          {};    // [-]    - Platform ABI key (e.g. Linux-x86_64)
    std::string HardwareDriverUuid {};    // [-]    - Optional GPU/driver UUID for hardware-keyed outputs
};

struct DependencyBuildEntry
{
    std::string                        EntryIdentifier        {};                               // [-]   - Unique build entry key
    FormatSuffixCategory               Category               = FormatSuffixCategory::WorldSpace;// [-]  - Target format category
    uint32_t                           SchemaVersion          = 1u;                             // [ver] - Target schema version
    uint32_t                           CompilerVersion        = 1u;                             // [ver] - Baker algorithm version
    std::string                        TargetAbi              { "Portable" };                   // [-]   - Platform ABI identifier
    std::string                        HardwareDriverUuid     {};                               // [-]   - Empty for portable outputs
    std::string                        BuildSpecificationText {};                               // [-]   - Canonicalized build parameters
    std::vector<std::filesystem::path> SourceFilePaths        {};                               // [-]   - Authored source files read by baker
    std::vector<std::string>           UpstreamIdentifiers    {};                               // [-]   - Prerequisite build entries
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class DependencyOrderLedger
{
public:
    [[nodiscard]] bool RegisterEntry(DependencyBuildEntry Entry, std::string& OutDiagnostic);

    [[nodiscard]] bool UpdateSpecification(
        std::string_view EntryIdentifier,
        std::string      UpdatedSpecificationText,
        std::string&     OutDiagnostic);

    [[nodiscard]] const DependencyBuildEntry* FindEntry(std::string_view EntryIdentifier) const noexcept;

    [[nodiscard]] bool ResolveTopologicalOrder(
        std::vector<std::string>& OutOrderedIdentifiers,
        std::vector<std::string>& OutCycleChain,
        std::string&              OutDiagnostic) const;

    [[nodiscard]] bool ComputeAllInputSignatures(
        std::unordered_map<std::string, BuildInputSignature>& OutSignatures,
        std::string&                                          OutDiagnostic) const;

    [[nodiscard]] std::size_t QueryEntryCount() const noexcept;

    void Clear() noexcept;

private:
    std::vector<DependencyBuildEntry>          Entries       {}; // [-]   - Contiguous registered build entries
    std::unordered_map<std::string, std::size_t> IndexByIdentifier {}; // [idx] - Fast lookup by EntryIdentifier
};

} // namespace Frontier::Serialization
