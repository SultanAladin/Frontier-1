/*============================================================================================================================================
                                                       BOOTCONFIGURATIONLOADER.H
============================================================================================================================================*/
// 🧩 TOML-driven boot configuration loader resolving EngineSettings -> RuntimeConfiguration -> PlatformSettings -> ProjectIndex -> WorldSpace

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Codecs/FormatSpecifications.h"

#include <filesystem>
#include <string>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ResolvedBootConfiguration
{
    EngineSettingsDocument       EngineSettings       {}; // [-] - Core engine defaults & revision limits
    RuntimeConfigurationDocument RuntimeConfiguration {}; // [-] - Launch window, frame pacing & startup world
    PlatformSettingsDocument     PlatformSettings     {}; // [-] - Operating platform & Vulkan presentation preferences
    ProjectIndexDocument         ProjectIndex         {}; // [-] - Active project search roots & world index
    std::filesystem::path        ResolvedWorldPath    {}; // [-] - Final resolved .WorldSpace file path
    bool                         LoadedFromDisk       = false; // [-] - True when all TOML files resolved cleanly from disk
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class BootConfigurationLoader
{
public:
    [[nodiscard]] static bool EnsureDefaultConfigurationFiles(
        const std::filesystem::path& RepositoryRoot,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static bool LoadFromRepositoryRoot(
        const std::filesystem::path& RepositoryRoot,
        ResolvedBootConfiguration&   OutResolved,
        std::string&                 OutDiagnostic);
};

} // namespace Frontier::Serialization
