/*============================================================================================================================================
                                                           ATOMICFILEWRITER.H
============================================================================================================================================*/
// 🧩 Cross-platform crash-safe file writer using same-volume temporary staging, OS flush, and atomic rename

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool WriteBytesAtomically(
    const std::filesystem::path& TargetPath,
    std::span<const std::byte>   PayloadBytes,
    std::string&                 OutDiagnostic);

[[nodiscard]] bool ReadFileBytes(
    const std::filesystem::path& SourcePath,
    std::vector<std::byte>&      OutBytes,
    std::string&                 OutDiagnostic);

[[nodiscard]] bool VerifyPathContainedWithinRoot(
    const std::filesystem::path& RootDirectory,
    const std::filesystem::path& CandidateRelativePath,
    std::filesystem::path&       OutResolvedPath,
    std::string&                 OutDiagnostic);

} // namespace Frontier::Serialization
