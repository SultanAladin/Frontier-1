/*============================================================================================================================================
                                                               CONTENTBROWSER.H
============================================================================================================================================*/
// 🧩 Editor-tier ContentBrowser coordinator connecting ColourCodex (.ColourSwatch), ThumbnailSheet streaming, and workspace domain masks

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"

#if FRONTIER_EDITOR_ENABLED

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"

#include <filesystem>
#include <string>

namespace Frontier::Editor::Explorer
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ContentBrowser
{
public:
    ContentBrowser() = default;

    void SynchronizeWorkspaceTargets(
        OutlinerPanel*  ActiveOutliner,
        InspectorPanel* ActiveInspector) noexcept;
    void AttachThemeRepository(const ThemeRepository* SourceTheme) noexcept;

    void ConfigureWorkspaceDomain(WorkspaceCategory ActiveDomain) noexcept;
    void ConfigureImportPolicy(const ImportIntakeConfiguration& Policy);

    [[nodiscard]] ImportIntakeResult IntakeExternalAssetDrop(const std::filesystem::path& DroppedSourceFile);

    bool ExportColourCodexToml(const std::filesystem::path& DestinationFile, std::string* FailureReason = nullptr) const;
    bool ImportColourCodexToml(const std::filesystem::path& SourceFile, std::string* FailureReason = nullptr);
    bool ExportSingleColourSwatch(std::size_t EntryIndex, const std::filesystem::path& DestinationFile, std::string* FailureReason = nullptr) const;
    bool ImportSingleColourSwatch(const std::filesystem::path& SourceFile, std::string* FailureReason = nullptr);

    void Render(Rectangle2D HostBoundsPhysical, double DeltaSeconds, float ScaleFactor = 1.0f);

    [[nodiscard]] ContentBrowserPanel&       AccessPanel() noexcept;
    [[nodiscard]] const ContentBrowserPanel& QueryPanel() const noexcept;

private:
    ContentBrowserPanel Panel          {};       // [-] - Underlying PixelSpace ContentBrowser drawer
    OutlinerPanel*      BoundOutliner  = nullptr;// [-] - Active workspace Outliner colour target
    InspectorPanel*     BoundInspector = nullptr;// [-] - Active workspace Inspector colour target
};

} // namespace Frontier::Editor::Explorer

#endif // FRONTIER_EDITOR_ENABLED
