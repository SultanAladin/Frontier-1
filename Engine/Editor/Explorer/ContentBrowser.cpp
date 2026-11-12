/*============================================================================================================================================
                                                              CONTENTBROWSER.CPP
============================================================================================================================================*/
// 🧩 Implementation of Editor-tier ContentBrowser coordinator

#include "Engine/Editor/Explorer/ContentBrowser.h"

#if FRONTIER_EDITOR_ENABLED

namespace Frontier::Editor::Explorer
{

//------------------------------------------------------------------------------------------------------------------------
//                                               CONTENTBROWSER IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowser::SynchronizeWorkspaceTargets(
    OutlinerPanel*  ActiveOutliner,
    InspectorPanel* ActiveInspector) noexcept
{
    BoundOutliner  = ActiveOutliner;
    BoundInspector = ActiveInspector;
    Panel.AttachOutlinerFallback(ActiveOutliner);
}

void ContentBrowser::AttachThemeRepository(const ThemeRepository* SourceTheme) noexcept
{
    Panel.AttachThemeRepository(SourceTheme);
}

void ContentBrowser::ConfigureWorkspaceDomain(WorkspaceCategory ActiveDomain) noexcept
{
    Panel.ConfigureWorkspaceDomain(ActiveDomain);
}

void ContentBrowser::ConfigureImportPolicy(const ImportIntakeConfiguration& Policy)
{
    Panel.ConfigureImportDestination(Policy);
}

ImportIntakeResult ContentBrowser::IntakeExternalAssetDrop(const std::filesystem::path& DroppedSourceFile)
{
    return Panel.IntakeExternalAssetDrop(DroppedSourceFile);
}

bool ContentBrowser::ExportColourCodexToml(const std::filesystem::path& DestinationFile, std::string* FailureReason) const
{
    std::string Diagnostic{};
    const bool  Succeeded = Panel.QueryColourCodex().SaveToTomlFile(DestinationFile, Diagnostic);
    if (!Succeeded && FailureReason != nullptr)
    {
        *FailureReason = Diagnostic;
    }
    return Succeeded;
}

bool ContentBrowser::ImportColourCodexToml(const std::filesystem::path& SourceFile, std::string* FailureReason)
{
    std::string Diagnostic{};
    const bool  Succeeded = Panel.AccessColourCodex().LoadFromTomlFile(SourceFile, Diagnostic);
    if (!Succeeded && FailureReason != nullptr)
    {
        *FailureReason = Diagnostic;
    }
    return Succeeded;
}

bool ContentBrowser::ExportSingleColourSwatch(
    std::size_t                  EntryIndex,
    const std::filesystem::path& DestinationFile,
    std::string*                 FailureReason) const
{
    std::string Diagnostic{};
    const bool  Succeeded = Panel.QueryColourCodex().SaveSingleSwatchFile(EntryIndex, DestinationFile, Diagnostic);
    if (!Succeeded && FailureReason != nullptr)
    {
        *FailureReason = Diagnostic;
    }
    return Succeeded;
}

bool ContentBrowser::ImportSingleColourSwatch(const std::filesystem::path& SourceFile, std::string* FailureReason)
{
    std::string Diagnostic{};
    const bool  Succeeded = Panel.AccessColourCodex().LoadSingleSwatchFile(SourceFile, Diagnostic);
    if (!Succeeded && FailureReason != nullptr)
    {
        *FailureReason = Diagnostic;
    }
    return Succeeded;
}

void ContentBrowser::Render(Rectangle2D HostBoundsPhysical, double DeltaSeconds, float ScaleFactor)
{
    (void)BoundInspector;
    Panel.Render(HostBoundsPhysical, DeltaSeconds, ScaleFactor);
}

ContentBrowserPanel& ContentBrowser::AccessPanel() noexcept
{
    return Panel;
}

const ContentBrowserPanel& ContentBrowser::QueryPanel() const noexcept
{
    return Panel;
}

} // namespace Frontier::Editor::Explorer

#endif // FRONTIER_EDITOR_ENABLED
