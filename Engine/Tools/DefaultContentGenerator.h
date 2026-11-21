/*============================================================================================================================================
                                                         DEFAULTCONTENTGENERATOR.H
============================================================================================================================================*/
// 🧩 Deterministic C++ generator & cryptographic verifier populating Frontier's zero-abbreviation Content/ .Space repository

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Tools
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct GeneratedContentReport
{
    uint32_t                        GeneratedAssetCount  = 0u;
    uint32_t                        VerifiedAssetCount   = 0u;
    std::size_t                     TotalPayloadBytes    = 0u;
    std::vector<std::filesystem::path> WrittenRelativePaths {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                              DEFAULT CONTENT GENERATOR
//------------------------------------------------------------------------------------------------------------------------

class DefaultContentGenerator
{
public:
    // Generates the complete zero-abbreviation Content/ hierarchy under TargetRepositoryRoot / "Content"
    [[nodiscard]] static bool GenerateRepositoryContent(
        const std::filesystem::path& RepositoryRoot,
        GeneratedContentReport&      OutReport,
        std::string&                 OutDiagnostic);

    // Verifies 64-byte binary alignment and 64-bit FNV-1a checksums across all generated Content/ containers
    [[nodiscard]] static bool VerifyRepositoryContent(
        const std::filesystem::path& RepositoryRoot,
        uint32_t&                    OutVerifiedCount,
        std::string&                 OutDiagnostic);

    // Cold-boot guard: generates Content/ automatically if Content/Worlds/VehicleClearSkyDefault.WorldSpace is missing
    [[nodiscard]] static bool EnsureDefaultContentAvailable(
        const std::filesystem::path& RepositoryRoot,
        std::string&                 OutDiagnostic);
};

} // namespace Frontier::Tools
