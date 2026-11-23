/*==========================================================================================================================================
                                                             PROJECT DESCRIPTOR
                                     ------------------------------------------------------------------
                      Decode a .frontier project descriptor (TOML) into the fields the host needs to open the project.
==========================================================================================================================================*/

// 🧩 Decode a .frontier project descriptor (TOML) into the fields the host needs to open the project.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ProjectLogCategory
{
    std::string Name;      // [-] - Category registered with the log registry when the project opens
    std::string Colour;    // [-] - Palette key text ("pink", "purple", "blue", ...); empty means neutral
};

// Field-for-field mirror of the established .frontier descriptor (Project-Networking precedent), plus the
// optional [[LogCategories]] declarations a project uses to publish its own log categories with colours.
struct ProjectDescriptor
{
    std::string Name;                                  // [-] - ProjectName (shown on the notch and in the browser)
    std::string ContentLocation = "Content";           // [-] - Project-relative user content folder
    std::string OpeningScene;                          // [-] - Scene opened on load; empty means a blank workspace
    std::string CodeImage;                             // [-] - Project-relative code image (DLL) the host loads
    uint32_t    ProjectFormatNumber   = 0;             // [-] - Descriptor format revision
    uint32_t    CodeInterchangeNumber = 0;             // [-] - Host C ABI revision the code image targets
    uint64_t    InterfaceFingerprint  = 0;             // [-] - Host interface fingerprint the code image was built against
    std::vector<ProjectLogCategory> LogCategories;     // [entries] - Declared user log categories

    std::filesystem::path DescriptorPath;              // [-] - Absolute path of the decoded .frontier file
    std::filesystem::path ProjectDirectory;            // [-] - Directory containing the descriptor
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Returns false with a human-readable Diagnostic when the file is missing, malformed, or has no project name.
// The fingerprint accepts either a TOML integer (hex literals included) or a quoted "0x..." string, matching
// how existing descriptors in this repository spell it.
bool DecodeProjectDescriptor(const std::filesystem::path& Path, ProjectDescriptor& Out, std::string& Diagnostic);

} // namespace Frontier
