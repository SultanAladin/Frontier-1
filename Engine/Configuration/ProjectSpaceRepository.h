/*============================================================================================================================================
                                                           PROJECTSPACEREPOSITORY.H
============================================================================================================================================*/
// 🧩 Single-container .projectspace persistence for .visualappearance, .FontSpace, .workspacelayout, .inputsettings, and .notificationsettings

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Controls/NotificationSequence.h"
#include "Engine/Configuration/InputSettings.h"

#include <filesystem>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorkspaceLayoutSettings
{
    bool        DetachedWindowsEnabled = false; // [-] - Permit tearing docked panes into separate OS windows
    std::string DockLayout             = "";    // [-] - Serialized docking split topology

    [[nodiscard]] constexpr bool operator==(const WorkspaceLayoutSettings&) const noexcept = default;
};

struct ProjectSpaceDocument
{
    std::string             ProjectTitle    = "Untitled";   // [-] - Logical project title
    AppearanceSettings      Appearance      {};             // [-] - Interface scale, theme, corner radius, swatches, type scale
    std::string             TypefaceName    = "Inter";      // [-] - Active typeface group name stored in .FontSpace
    WorkspaceLayoutSettings WorkspaceLayout {};             // [-] - Docked window hosting preferences
    InputPreferences        Input           {};             // [-] - Shortcut profile and pointer response preferences
    NotificationPreferences Notifications   {};             // [-] - Rate indicator and alert preferences
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ProjectSpaceRepository
{
public:
    static constexpr const char* ContainerExtension      = ".projectspace";        // [-] - Sole permitted project container extension
    static constexpr const char* VisualAppearanceSection = ".visualappearance";    // [-] - Visual appearance section key
    static constexpr const char* FontSpaceSection        = ".FontSpace";           // [-] - Typography section key
    static constexpr const char* WorkspaceLayoutSection  = ".workspacelayout";     // [-] - Workspace docking section key
    static constexpr const char* InputSettingsSection    = ".inputsettings";       // [-] - Input settings section key
    static constexpr const char* NotificationSection     = ".notificationsettings";// [-] - Notification settings section key

    [[nodiscard]] static bool ValidateContainerPath(const std::filesystem::path& RepositoryPath, std::string& Diagnostic) noexcept;
    [[nodiscard]] static bool ValidateAppearance(const AppearanceSettings& Appearance, std::string& Diagnostic) noexcept;

    [[nodiscard]] static bool Save(
        const std::filesystem::path& RepositoryPath,
        const ProjectSpaceDocument&  Document,
        std::string&                 Diagnostic) noexcept;

    [[nodiscard]] static bool Load(
        const std::filesystem::path& RepositoryPath,
        ProjectSpaceDocument&        Document,
        std::string&                 Diagnostic) noexcept;
};

} // namespace Frontier
