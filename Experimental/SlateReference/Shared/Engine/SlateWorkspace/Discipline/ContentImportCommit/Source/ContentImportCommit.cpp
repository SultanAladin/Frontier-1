//============================================================================================================================================
//                                                       CONTENTIMPORTCOMMIT.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/ContentImportCommit/Api/ContentImportCommit.h"

#include "SlateDocument/Format/MaterialImageImport/Api/MaterialImageImport.h"
#include "SlateDocument/Format/SceneMeshImport/Api/SceneMeshImport.h"
#include "SlateDocument/Format/WorkspaceSceneActivation/Api/WorkspaceSceneActivation.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace Slate
{

void CommitConfirmedImport(const char* Named,
                           ContentBrowserConfiguration& ContentBrowserApplied,
                           const SceneDirectoryRows& SceneDirectoryStorage,
                           SceneDirectoryContext& SceneApplied,
                           WorkspaceCodex& OpenedScene,
                           bool& OpenedSceneStanding)
{
    if (!ContentBrowserApplied.ImportConfirmed)
        return;

    if (ContentBrowserApplied.ImportTaken < ContentBrowserApplied.ImportEntryCount &&
        !ContentBrowserApplied.ImportEntries[ContentBrowserApplied.ImportTaken].Directory)
    {
        const std::filesystem::path ImportPath = std::filesystem::path(ContentBrowserApplied.ImportLocation) /
            ContentBrowserApplied.ImportEntries[ContentBrowserApplied.ImportTaken].Naming;
        if (MaterialImageFormatSupported(ImportPath.string()))
        {
            const Deliver<ImportedMaterialImage> Imported = ImportMaterialImageReference(ImportPath.string());
            if (!Imported.Resolved)
            {
                std::printf("%s — material image import refused (reason %u: %s)\n", Named,
                            static_cast<unsigned>(Imported.Error.DeclaredReason), Imported.Error.Detail);
            }
            else
            {
                if (!OpenedSceneStanding)
                {
                    OpenedScene = {};
                    OpenedScene.Naming = "Imported Material Scene";
                    OpenedSceneStanding = true;
                }
                EnsureWorkspaceMaterialRecords(OpenedScene);
                std::string MaterialReference = OpenedScene.Materials.empty()
                    ? std::string("ImportedImageMaterial") : OpenedScene.Materials.front().Reference;
                if (SceneApplied.EntityTaken < SceneDirectoryStorage.RowCount)
                {
                    const StableRowIdentity Identity = SceneDirectoryStorage.Rows[SceneApplied.EntityTaken].Identity;
                    if (Identity >= 6200u && Identity - 6200u < OpenedScene.Scene.size())
                        MaterialReference = OpenedScene.Scene[Identity - 6200u].MaterialReference;
                }
                if (OpenedScene.Materials.empty())
                    OpenedScene.Materials.push_back(DefaultWorkspaceMaterialRecord(MaterialReference));
                for (WorkspaceMaterialRecord& Material : OpenedScene.Materials)
                {
                    if (Material.Reference != MaterialReference)
                        continue;
                    const Deliver<std::uint32_t> Bound = BindWorkspaceMaterialImage(
                        Material, Imported.Resolve().SuggestedChannel, Imported.Resolve().Reference);
                    if (!Bound.Resolved)
                        std::printf("%s — material image bind refused (reason %u: %s)\n", Named,
                                    static_cast<unsigned>(Bound.Error.DeclaredReason), Bound.Error.Detail);
                    else
                        std::printf("%s — imported material image %s for channel %u on %s\n",
                                    Named, Imported.Resolve().Reference.ReferenceName.c_str(),
                                    static_cast<unsigned>(Imported.Resolve().SuggestedChannel),
                                    Material.Reference.c_str());
                    break;
                }
            }
        }
        else
        {
            const Deliver<ImportedSceneMesh> Imported = ImportSceneMeshFile(ImportPath.string());
            if (!Imported.Resolved)
            {
                std::printf("%s — mesh import refused (reason %u: %s)\n", Named,
                            static_cast<unsigned>(Imported.Error.DeclaredReason), Imported.Error.Detail);
            }
            else
            {
                if (!OpenedSceneStanding)
                {
                    OpenedScene = {};
                    OpenedScene.Naming = "Imported Mesh Scene";
                    OpenedSceneStanding = true;
                }
                OpenedScene.Scene.push_back(Imported.Resolve().Entry);
                OpenedScene.SceneMeshes.push_back(Imported.Resolve().Mesh);
                for (const WorkspaceMaterialRecord& Material : Imported.Resolve().MaterialRecords)
                    OpenedScene.Materials.push_back(Material);
                EnsureWorkspaceMaterialRecords(OpenedScene);
                SceneApplied.TransformSeeded = false;
                std::printf("%s — imported mesh %s (%zu vertices, %zu triangles, %zu material slots)\n",
                            Named, Imported.Resolve().Entry.Naming.c_str(),
                            Imported.Resolve().Mesh.Positions.size() / 3u,
                            Imported.Resolve().Mesh.Indices.size() / 3u,
                            Imported.Resolve().MaterialSlots.size());
            }
        }

    }

    ContentBrowserApplied.ImportConfirmed = false;
}

}   // namespace Slate
