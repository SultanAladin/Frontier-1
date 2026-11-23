/*==========================================================================================================================================
                                                          PROJECTSPACECODEC.H
==========================================================================================================================================*/
// 🧩 Persist one editable project container with embedded workspace profiles and preserved project sections.

#pragma once
#include "Engine/Interface/AppearanceCodec.h"
#include "Engine/Interface/InputPreferencesCodec.h"

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   PROJECT DOCUMENT
//------------------------------------------------------------------------------------------------------------------------

struct WorkspaceAppearanceProfile
{
    AppearanceSettings Preferences;
    std::string        RequestedFamily = "Inter";
};

struct ProjectSpaceDocument
{
    toml::table                Sections;
    WorkspaceAppearanceProfile Appearance;
    InputPreferences           Input;
    NotificationPreferences    Notifications;
    bool                       AppearancePresent    = false;
    bool                       InputPresent         = false;
    bool                       NotificationsPresent = false;
};

enum class PreferenceScopeCategory
{
    Personal,
    Project
};

class ProjectSpaceCodec
{
  public:
    static constexpr uintmax_t MaximumBytes = 4 * 1024 * 1024;
    static bool                Decode(const std::filesystem::path &Path,
                                      ProjectSpaceDocument        &Document,
                                      std::string                 &Diagnostic) noexcept;
    static bool                Encode(const std::filesystem::path &Path,
                                      const ProjectSpaceDocument  &Document,
                                      std::string                 &Diagnostic) noexcept;
    static bool                Validate(const ProjectSpaceDocument &Document, std::string &Diagnostic) noexcept;
    static bool                CreateBlank(const std::filesystem::path &Template,
                                           const std::filesystem::path &Destination,
                                           const std::string           &Name,
                                           std::string                 &Diagnostic) noexcept;
    static bool                DecodeNotifications(const toml::table       &Section,
                                                   NotificationPreferences &Preferences,
                                                   std::string             &Diagnostic) noexcept;
    static bool                EncodeNotifications(toml::table                   &Section,
                                                   const NotificationPreferences &Preferences,
                                                   std::string                   &Diagnostic) noexcept;
};
} // namespace Frontier
