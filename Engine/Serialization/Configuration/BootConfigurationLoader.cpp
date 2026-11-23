/*============================================================================================================================================
                                                      BOOTCONFIGURATIONLOADER.CPP
============================================================================================================================================*/
// 🧩 Implementation of TOML-driven startup configuration loading without silent hardcoded fallbacks

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Configuration/BootConfigurationLoader.h"

#include "Engine/Serialization/Codecs/FormatCodecSuite.h"
#include "Engine/Serialization/Container/AtomicFileWriter.h"
#include "Engine/Serialization/Container/DocumentContainerCodec.h"

namespace Frontier::Serialization
{

namespace
{

[[nodiscard]] bool WriteIfAbsent(
    const std::filesystem::path&   TargetPath,
    const DocumentContainer& DefaultDoc,
    std::string&                   OutDiagnostic)
{
    std::error_code FsError{};
    if (std::filesystem::exists(TargetPath, FsError))
    {
        return true;
    }
    return DocumentContainerEncoder::EncodeToFile(TargetPath, DefaultDoc, OutDiagnostic);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               BOOT LOADER IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

bool BootConfigurationLoader::EnsureDefaultConfigurationFiles(
    const std::filesystem::path& RepositoryRoot,
    std::string&                 OutDiagnostic)
{
    const std::filesystem::path EngineSettingsPath       = RepositoryRoot / "Configuration" / "EngineDefault.EngineSettings";
    const std::filesystem::path RuntimeConfigurationPath = RepositoryRoot / "Configuration" / "RuntimeDefault.RuntimeConfiguration";
    const std::filesystem::path PlatformPath             = RepositoryRoot / "Configuration" / "PlatformDefault.PlatformSettings";
    const std::filesystem::path ProjectIndexPath         = RepositoryRoot / "Content" / "Project" / "Frontier.ProjectIndex";

    DocumentContainer EngineDoc{};
    DocumentContainer RuntimeDoc{};
    DocumentContainer PlatformDoc{};
    DocumentContainer ProjectDoc{};

    (void)FormatCodecSuite::EncodeEngineSettings(EngineSettingsDocument{}, EngineDoc);
    (void)FormatCodecSuite::EncodeRuntimeConfiguration(RuntimeConfigurationDocument{}, RuntimeDoc);
    (void)FormatCodecSuite::EncodePlatformSettings(PlatformSettingsDocument{}, PlatformDoc);
    (void)FormatCodecSuite::EncodeProjectIndex(ProjectIndexDocument{}, ProjectDoc);

    if (!WriteIfAbsent(EngineSettingsPath,       EngineDoc,   OutDiagnostic) ||
        !WriteIfAbsent(RuntimeConfigurationPath, RuntimeDoc,  OutDiagnostic) ||
        !WriteIfAbsent(PlatformPath,             PlatformDoc, OutDiagnostic) ||
        !WriteIfAbsent(ProjectIndexPath,         ProjectDoc,  OutDiagnostic))
    {
        return false;
    }

    OutDiagnostic.clear();
    return true;
}

bool BootConfigurationLoader::LoadFromRepositoryRoot(
    const std::filesystem::path& RepositoryRoot,
    ResolvedBootConfiguration&   OutResolved,
    std::string&                 OutDiagnostic)
{
    OutResolved = {};

    const std::filesystem::path EngineSettingsPath = RepositoryRoot / "Configuration" / "EngineDefault.EngineSettings";
    const std::filesystem::path RuntimePath        = RepositoryRoot / "Configuration" / "RuntimeDefault.RuntimeConfiguration";
    const std::filesystem::path PlatformPath       = RepositoryRoot / "Configuration" / "PlatformDefault.PlatformSettings";

    // ① Decode EngineSettings
    DocumentContainer EngineContainer{};
    if (!DocumentContainerDecoder::DecodeFromFile(EngineSettingsPath, EngineContainer, OutDiagnostic) ||
        !FormatCodecSuite::DecodeEngineSettings(EngineContainer, OutResolved.EngineSettings, OutDiagnostic))
    {
        OutDiagnostic = "Failed to load .EngineSettings (" + EngineSettingsPath.string() + "): " + OutDiagnostic;
        return false;
    }

    // ② Decode RuntimeConfiguration
    DocumentContainer RuntimeContainer{};
    if (!DocumentContainerDecoder::DecodeFromFile(RuntimePath, RuntimeContainer, OutDiagnostic) ||
        !FormatCodecSuite::DecodeRuntimeConfiguration(RuntimeContainer, OutResolved.RuntimeConfiguration, OutDiagnostic))
    {
        OutDiagnostic = "Failed to load .RuntimeConfiguration (" + RuntimePath.string() + "): " + OutDiagnostic;
        return false;
    }

    // ③ Decode PlatformSettings
    DocumentContainer PlatformContainer{};
    if (!DocumentContainerDecoder::DecodeFromFile(PlatformPath, PlatformContainer, OutDiagnostic) ||
        !FormatCodecSuite::DecodePlatformSettings(PlatformContainer, OutResolved.PlatformSettings, OutDiagnostic))
    {
        OutDiagnostic = "Failed to load .PlatformSettings (" + PlatformPath.string() + "): " + OutDiagnostic;
        return false;
    }

    // ④ Decode ProjectIndex (anchored inside repository root)
    std::filesystem::path ProjectIndexPath{};
    if (!VerifyPathContainedWithinRoot(
            RepositoryRoot,
            OutResolved.EngineSettings.ActiveProjectIndexPath,
            ProjectIndexPath,
            OutDiagnostic))
    {
        return false;
    }

    DocumentContainer ProjectContainer{};
    if (!DocumentContainerDecoder::DecodeFromFile(ProjectIndexPath, ProjectContainer, OutDiagnostic) ||
        !FormatCodecSuite::DecodeProjectIndex(ProjectContainer, OutResolved.ProjectIndex, OutDiagnostic))
    {
        OutDiagnostic = "Failed to load .ProjectIndex (" + ProjectIndexPath.string() + "): " + OutDiagnostic;
        return false;
    }

    // ⑤ Resolve final StartupWorldPath (RuntimeConfiguration -> ProjectIndex -> EngineSettings)
    std::string SelectedWorldRelative = OutResolved.RuntimeConfiguration.StartupWorldPath;
    if (SelectedWorldRelative.empty())
    {
        SelectedWorldRelative = OutResolved.ProjectIndex.StartupWorldPath;
    }
    if (SelectedWorldRelative.empty())
    {
        SelectedWorldRelative = OutResolved.EngineSettings.DefaultStartupWorldPath;
    }
    if (SelectedWorldRelative.empty())
    {
        OutDiagnostic = "No StartupWorldPath declared across RuntimeConfiguration, ProjectIndex, or EngineSettings.";
        return false;
    }

    std::filesystem::path WorldPath{};
    if (!VerifyPathContainedWithinRoot(RepositoryRoot, SelectedWorldRelative, WorldPath, OutDiagnostic))
    {
        return false;
    }

    OutResolved.ResolvedWorldPath = WorldPath;
    OutResolved.LoadedFromDisk    = true;
    OutDiagnostic.clear();
    return true;
}

} // namespace Frontier::Serialization
