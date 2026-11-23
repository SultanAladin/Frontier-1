/*==========================================================================================================================================
                                                         PROJECTSPACECODEC.CPP
==========================================================================================================================================*/
// 🧩 Bound project reads, share preference schemas, preserve unrelated sections, and create fresh-identity project snapshots.

#include "ProjectSpaceCodec.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/TextFileReplacement.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    SECTION VALIDATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition)
        throw std::runtime_error(Description);
}

bool RelativeLocator(const std::string &Locator)
{
    if (Locator.empty() || Locator.size() > 1024 || Locator.find_first_of("\\:\r\n") != std::string::npos ||
        Locator.find('\0') != std::string::npos)
        return false;
    const auto Path = DecodeContentPath(Locator);
    if (Path.is_absolute())
        return false;
    for (const auto &Part : Path)
        if (Part == "..")
            return false;
    return true;
}

std::string FreshIdentity()
{
    std::random_device Entropy;
    std::ostringstream Text;
    Text << "project-" << std::hex << std::setfill('0');
    for (int Ordinal = 0; Ordinal < 4; ++Ordinal)
        Text << std::setw(8) << static_cast<uint32_t>(Entropy());
    return Text.str();
}

void ValidateSections(const toml::table &Document)
{
    Require(Document["format"].value<std::string>() == "frontier.projectspace.editor",
            "Not an editable projectspace container");
    Require(Document["version"].value<int64_t>() == 1, "Unsupported projectspace version");
    const auto *Project = Document["project"].as_table();
    Require(Project != nullptr, "Missing project configuration");
    for (const auto *Key : {"identity", "name"})
    {
        const auto Text = (*Project)[Key].value<std::string>();
        Require(Text && !Text->empty() && Text->size() <= 256 && Text->find('\0') == std::string::npos,
                "Invalid project identity/name");
    }
    const auto *Roots = Document["content_roots"].as_array();
    Require(Roots && !Roots->empty() && Roots->size() <= 64, "Missing or excessive content roots");
    std::set<std::string> RootNames;
    for (const auto &Root : *Roots)
    {
        const auto Locator = Root.value<std::string>();
        Require(Locator && RelativeLocator(*Locator) && RootNames.insert(*Locator).second,
                "Invalid or duplicate content root");
    }
    for (const auto *Key : {"levels", "catalogue", "dependency_pins"})
    {
        const auto *Rows = Document[Key].as_array();
        Require(Rows && Rows->size() <= 4096, "Missing or excessive project catalogue/pins/levels");
        std::set<std::string> Identities;
        for (const auto &Entry : *Rows)
        {
            const auto *Row = Entry.as_table();
            Require(Row != nullptr, "Project entry must be a table");
            const auto Identity = (*Row)["identity"].value<std::string>();
            Require(Identity && !Identity->empty() && Identity->size() <= 256 && Identities.insert(*Identity).second,
                    "Missing or duplicate project entry identity");
            const auto Locator = (*Row)["path"].value<std::string>();
            Require(Locator && RelativeLocator(*Locator), "Project entry requires a confined relative locator");
            if (std::string_view(Key) == "dependency_pins")
            {
                const auto Revision = (*Row)["revision"].value<std::string>();
                const auto Type     = (*Row)["type"].value<std::string>();
                const auto Digest   = (*Row)["sha256"].value<std::string>();
                Require(Revision && !Revision->empty() && Type && !Type->empty() && Digest && Digest->size() == 64 &&
                            std::all_of(Digest->begin(), Digest->end(), [](char Digit)
                                        { return (Digit >= '0' && Digit <= '9') || (Digit >= 'a' && Digit <= 'f'); }),
                        "Invalid exact dependency pin");
            }
        }
    }
    Require(Document["engine"].is_table(), "Missing engine configuration section");
    const auto &Engine = *Document["engine"].as_table();
    if (Engine.contains("configuration_version"))
        Require(Engine["configuration_version"].value<int64_t>() == 1, "Unsupported engine configuration version");
    if (Engine.contains("personal_overrides"))
        Require(Engine["personal_overrides"].is_boolean(), "Mistyped personal override policy");
    if (Engine.contains("startup_level"))
    {
        const auto Startup = Engine["startup_level"].value<std::string>();
        Require(Startup.has_value(), "Mistyped startup level");
        bool Located = Startup->empty();
        for (const auto &Level : *Document["levels"].as_array())
            Located = Located || (*Level.as_table())["identity"].value<std::string>() == Startup;
        Require(Located, "Startup level is not in the project level directory");
    }
    const auto *Workspace = Document["workspace"].as_table();
    Require(Workspace != nullptr, "Missing workspace section");
    for (const auto *Key : {"appearance", "input", "notifications", "hosting"})
        Require(!Workspace->contains(Key) || (*Workspace)[Key].is_table(), "Mistyped workspace profile");
    if (const auto *Hosting = (*Workspace)["hosting"].as_table())
    {
        Require((*Hosting)["format"].value<int64_t>() == 1, "Unsupported workspace hosting format");
        Require((*Hosting)["allow_detached_windows"].is_boolean(), "Mistyped detached-window preference");
        Require(!Hosting->contains("dockable_content_browser") || (*Hosting)["dockable_content_browser"].is_boolean(),
                "Mistyped Content Browser hosting preference");
    }
    if (Document.contains("required_features"))
    {
        const auto *Features = Document["required_features"].as_array();
        Require(Features && Features->empty(), "Unsupported required project features");
    }
}

void DecodeProfiles(ProjectSpaceDocument &Document)
{
    const auto &Workspace = *Document.Sections["workspace"].as_table();
    std::string Diagnostic;
    Document.AppearancePresent    = Workspace.contains("appearance");
    Document.InputPresent         = Workspace.contains("input");
    Document.NotificationsPresent = Workspace.contains("notifications");
    if (Document.AppearancePresent &&
        !AppearanceCodec::DecodeTable(*Workspace["appearance"].as_table(), Document.Appearance.Preferences,
                                      Document.Appearance.RequestedFamily, Diagnostic))
        throw std::runtime_error(Diagnostic);
    if (Document.InputPresent &&
        !InputPreferencesCodec::DecodeTable(*Workspace["input"].as_table(), Document.Input, Diagnostic))
        throw std::runtime_error(Diagnostic);
    if (Document.NotificationsPresent && !ProjectSpaceCodec::DecodeNotifications(*Workspace["notifications"].as_table(),
                                                                                 Document.Notifications, Diagnostic))
        throw std::runtime_error(Diagnostic);
}

toml::table EncodeSections(const ProjectSpaceDocument &Document)
{
    auto Sections = Document.Sections;
    ValidateSections(Sections);
    auto       &Workspace = *Sections["workspace"].as_table();
    std::string Diagnostic;
    const auto  PreserveExtensions = [](const toml::node_view<toml::node> Existing, toml::table &Known)
    {
        if (const auto *Previous = Existing.as_table())
            for (const auto &[Key, Entry] : *Previous)
                if (!Known.contains(Key))
                    Known.insert(Key, Entry);
    };
    toml::table Profile;
    if (Document.AppearancePresent)
    {
        if (!AppearanceCodec::EncodeTable(Profile, Document.Appearance.Preferences, Document.Appearance.RequestedFamily,
                                          Diagnostic))
            throw std::runtime_error(Diagnostic);
        PreserveExtensions(Workspace["appearance"], Profile);
        Workspace.insert_or_assign("appearance", std::move(Profile));
    }
    if (Document.InputPresent)
    {
        if (!InputPreferencesCodec::EncodeTable(Profile, Document.Input, Diagnostic))
            throw std::runtime_error(Diagnostic);
        PreserveExtensions(Workspace["input"], Profile);
        Workspace.insert_or_assign("input", std::move(Profile));
    }
    if (Document.NotificationsPresent)
    {
        if (!ProjectSpaceCodec::EncodeNotifications(Profile, Document.Notifications, Diagnostic))
            throw std::runtime_error(Diagnostic);
        PreserveExtensions(Workspace["notifications"], Profile);
        Workspace.insert_or_assign("notifications", std::move(Profile));
    }
    return Sections;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  NOTIFICATION PREFERENCES
//------------------------------------------------------------------------------------------------------------------------

bool ProjectSpaceCodec::DecodeNotifications(const toml::table &Section, NotificationPreferences &Preferences,
                                            std::string &Diagnostic) noexcept
{
    const auto Overlay = Section["frame_rate_overlay"].value<bool>();
    const auto Applied = Section["preferences_applied"].value<bool>();
    const auto Errors  = Section["preference_errors"].value<bool>();
    const auto Seconds = Section["hold_seconds"].value<float>();
    if (Section["format"].value<int64_t>() != 1 || !Overlay || !Applied || !Errors || !Seconds ||
        !std::isfinite(*Seconds) || *Seconds < 1 || *Seconds > 10)
    {
        Diagnostic = "Invalid notification preference profile";
        return false;
    }
    Preferences = {*Overlay, *Applied, *Errors, *Seconds};
    Diagnostic.clear();
    return true;
}

bool ProjectSpaceCodec::EncodeNotifications(toml::table &Section, const NotificationPreferences &Preferences,
                                            std::string &Diagnostic) noexcept
{
    try
    {
        toml::table             Candidate{{"format", 1},
                                          {"frame_rate_overlay", Preferences.ShowFrameRateOverlay},
                                          {"preferences_applied", Preferences.PreferencesApplied},
                                          {"preference_errors", Preferences.PreferenceErrors},
                                          {"hold_seconds", Preferences.HoldSeconds}};
        NotificationPreferences Validated;
        if (!DecodeNotifications(Candidate, Validated, Diagnostic))
            return false;
        Section = std::move(Candidate);
        return true;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTAINER PERSISTENCE
//------------------------------------------------------------------------------------------------------------------------

bool ProjectSpaceCodec::Validate(const ProjectSpaceDocument &Document, std::string &Diagnostic) noexcept
{
    try
    {
        auto                 Candidate = EncodeSections(Document);
        ProjectSpaceDocument Checked;
        Checked.Sections = std::move(Candidate);
        DecodeProfiles(Checked);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool ProjectSpaceCodec::Decode(const std::filesystem::path &Path, ProjectSpaceDocument &Document,
                               std::string &Diagnostic) noexcept
{
    try
    {
        Require(Path.extension() == ".projectspace", "Expected .projectspace extension");
        std::ifstream Input(Path, std::ios::binary);
        Require(Input.good(), "Cannot open projectspace container");
        std::string Text;
        char        Chunk[4096];
        while (Input.read(Chunk, sizeof(Chunk)) || Input.gcount())
        {
            Require(Text.size() + static_cast<size_t>(Input.gcount()) <= MaximumBytes,
                    "Projectspace exceeds 4 MiB limit");
            Text.append(Chunk, static_cast<size_t>(Input.gcount()));
        }
        Require(!Input.bad(), "Failed reading projectspace container");
        Require(Text.compare(0, 4, "FSPC") != 0,
                "Binary FSPC requires its runtime reader, not the editor profile reader");
        ProjectSpaceDocument Candidate;
        Candidate.Sections = toml::parse(Text);
        ValidateSections(Candidate.Sections);
        DecodeProfiles(Candidate);
        Document = std::move(Candidate);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool ProjectSpaceCodec::Encode(const std::filesystem::path &Path, const ProjectSpaceDocument &Document,
                               std::string &Diagnostic) noexcept
{
    try
    {
        Require(Path.extension() == ".projectspace", "Expected .projectspace extension");
        if (std::filesystem::exists(Path))
        {
            ProjectSpaceDocument Previous;
            if (!Decode(Path, Previous, Diagnostic))
                return false;
        }
        const auto           Sections = EncodeSections(Document);
        ProjectSpaceDocument Checked;
        Checked.Sections = Sections;
        DecodeProfiles(Checked);
        std::ostringstream Output;
        Output << Sections;
        const auto Text = Output.str();
        Require(Text.size() <= MaximumBytes, "Projectspace exceeds 4 MiB limit");
        return TextFileReplacement::Finalize(Path, Text, Diagnostic);
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool ProjectSpaceCodec::CreateBlank(const std::filesystem::path &Template, const std::filesystem::path &Destination,
                                    const std::string &Name, std::string &Diagnostic) noexcept
{
    std::filesystem::path Temporary;
    try
    {
        Require(Destination.extension() == ".projectspace", "Expected .projectspace destination");
        Require(!std::filesystem::exists(Destination), "Project already exists; refusing overwrite");
        Require(!Name.empty() && Name.size() <= 256, "Invalid project name");
        ProjectSpaceDocument Candidate;
        if (!Decode(Template, Candidate, Diagnostic))
            return false;
        auto      &Project  = *Candidate.Sections["project"].as_table();
        const auto Identity = FreshIdentity();
        Project.insert_or_assign("identity", Identity);
        Project.insert_or_assign("name", Name);
        if (!Validate(Candidate, Diagnostic))
            return false;
        Temporary = Destination;
        Temporary += "." + Identity + ".projectspace";
        if (!Encode(Temporary, Candidate, Diagnostic))
            return false;
        // Publishing a hard link refuses replacement, unlike rename on platforms that replace an existing file.
        for (const auto &Root : *Candidate.Sections["content_roots"].as_array())
            std::filesystem::create_directories(Destination.parent_path() /
                                                DecodeContentPath(*Root.value<std::string>()));
        std::filesystem::create_hard_link(Temporary, Destination);
        std::error_code CleanupFailure;
        std::filesystem::remove(Temporary, CleanupFailure);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure)
    {
        std::error_code Ignored;
        if (!Temporary.empty())
            std::filesystem::remove(Temporary, Ignored);
        Diagnostic = Failure.what();
        return false;
    }
}
} // namespace Frontier
