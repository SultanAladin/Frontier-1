/*============================================================================================================================================
                                                              PROJECTGENERATOR.H
============================================================================================================================================*/
// 🧩 Scaffolder for standalone C++20 Frontier projects (<ProjectName>, <ProjectName>Editor, <ProjectName>Shipping) with .projectspace

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Configuration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ProjectScaffoldSpecification
{
    std::string                  DisplayName          = "NewFrontierProject"; // [-] - Human-authored title ("Aero Tunnel" -> "AeroTunnel")
    std::filesystem::path        DestinationDirectory {};                     // [-] - Folder where <PascalName>/ will be created
    std::filesystem::path        FrontierRootPath     {};                     // [-] - Optional Frontier repository / install root path
    std::filesystem::path        FrontierBinariesPath {};                     // [-] - Optional Binaries/Win64 folder for DLL deployment
    ThemePreset                  InitialTheme         = ThemePreset::Dark;    // [-] - Default .visualappearance theme
    bool                         EnableFrontierWorld  = true;                 // [-] - Include "Frontier" capability
    bool                         EnableUvWorkspace    = true;                 // [-] - Include "UVWorkspace" capability
};

struct ProjectScaffoldOutcome
{
    bool                         Succeeded            = false;
    std::string                  PascalIdentifier     {};                     // [-] - Sanitized C++20 namespace & target identifier
    std::filesystem::path        ProjectRootDirectory {};                     // [-] - <DestinationDirectory>/<PascalIdentifier>
    std::filesystem::path        ProjectSpaceFile     {};                     // [-] - <ProjectRootDirectory>/<PascalIdentifier>.projectspace
    std::filesystem::path        CMakeListsFile       {};                     // [-] - <ProjectRootDirectory>/CMakeLists.txt
    std::filesystem::path        MainWorldFile        {};                     // [-] - Content/Worlds/MainWorld.WorldSpace
    std::filesystem::path        MainUvFile           {};                     // [-] - Content/UV/MainSurface.UVSpace
    std::vector<std::filesystem::path> GeneratedFiles {};                     // [-] - Complete list of scaffolded artifacts
    std::string                  Diagnostic           {};                     // [-] - Error or completion summary
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ProjectGenerator
{
public:
    [[nodiscard]] static std::string SanitizePascalIdentifier(std::string_view RawTitle);
    [[nodiscard]] static std::string FormatUppercaseMacroPrefix(std::string_view PascalIdentifier);

    [[nodiscard]] static ProjectScaffoldOutcome GenerateProject(const ProjectScaffoldSpecification& Specification);
};

} // namespace Frontier::Configuration
