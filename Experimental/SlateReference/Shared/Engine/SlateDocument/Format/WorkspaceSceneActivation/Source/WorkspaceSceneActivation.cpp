//============================================================================================================================================
//                                                WORKSPACESCENEACTIVATION.CPP
//============================================================================================================================================

#include "SlateDocument/Format/WorkspaceSceneActivation/Api/WorkspaceSceneActivation.h"

#include "SlateDocument/Format/CodexInterchange/Api/CodexInterchange.h"

#include <utility>

namespace Slate
{
namespace
{

bool MeshReferenceDeclared(const WorkspaceCodex& Workspace, const std::string& Reference)
{
    for (const CodexSceneMesh& Mesh : Workspace.SceneMeshes)
        if (Mesh.Naming == Reference)
            return true;
    return false;
}

bool MaterialReferenceDeclared(const WorkspaceCodex& Workspace, const std::string& Reference)
{
    for (const WorkspaceMaterialRecord& Material : Workspace.Materials)
        if (Material.Reference == Reference)
            return true;
    return false;
}

bool MandatoryBaseLayerDeclared(const WorkspaceMaterialRecord& Material)
{
    if (Material.Layers.EntryCount() == 0u)
        return false;

    const LayerSpecification& First = Material.Layers.Entries().front();
    return First.Mandatory && First.Name == "Base Material" && First.ChannelMask != 0u;
}

}

Deliver<ActivatedWorkspaceScene> WorkspaceSceneActivation::Open(const std::string& CodexPath,
                                                                  const std::string& EngineContentPath) const
{
    (void)EngineContentPath;

    CodexInterchange Stream;
    WorkspaceCodexInterchange Typed;
    const Deliver<CodexDocument> Document = Stream.Open(CodexPath);
    if (!Document.Resolved) return Deliver<ActivatedWorkspaceScene>::Refuse(Document.Error);
    const Deliver<WorkspaceCodex> Decoded = Typed.DecodeWorkspace(Document.Resolve());
    if (!Decoded.Resolved) return Deliver<ActivatedWorkspaceScene>::Refuse(Decoded.Error);

    ActivatedWorkspaceScene Activated;
    Activated.Workspace = Decoded.Resolve();
    EnsureWorkspaceMaterialRecords(Activated.Workspace);
    Activated.Materials = Activated.Workspace.Materials;

    std::uint32_t SunCount = 0u;
    std::uint32_t SkyCount = 0u;
    std::uint32_t AtmosphereCount = 0u;

    for (const WorkspaceMaterialRecord& Material : Activated.Materials)
    {
        if (Material.Reference.empty() || !MandatoryBaseLayerDeclared(Material))
        {
            return Deliver<ActivatedWorkspaceScene>::Refuse(
                { RefusalReason::ContentUnsupported, "a workspace material lacks a mandatory Base Material layer" });
        }
    }

    for (const CodexSceneEntry& Entry : Activated.Workspace.Scene)
    {
        if (Entry.Subject == CodexSceneSubject::Sun) ++SunCount;
        else if (Entry.Subject == CodexSceneSubject::Sky) ++SkyCount;
        else if (Entry.Subject == CodexSceneSubject::Atmosphere) ++AtmosphereCount;
        else if (Entry.Subject == CodexSceneSubject::Geometry)
        {
            if (Entry.GeometryReference.empty() || Entry.MaterialReference.empty())
                return Deliver<ActivatedWorkspaceScene>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace geometry entry lacks a source or material reference" });
            if (!MeshReferenceDeclared(Activated.Workspace, Entry.GeometryReference))
                return Deliver<ActivatedWorkspaceScene>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace geometry entry references missing embedded mesh data" });
            if (!MaterialReferenceDeclared(Activated.Workspace, Entry.MaterialReference))
                return Deliver<ActivatedWorkspaceScene>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace geometry entry references a missing material" });

            ActivatedGeometryEntry Resolved;
            Resolved.Entry = Entry;
            // Geometry is carried by the binary workspace stream. Runtime activation must not resolve
            // WhiteTeaService entries back to editable OBJ files under EngineContent.
            Resolved.SourcePath = CodexPath + "#" + Entry.GeometryReference;
            Activated.Geometry.push_back(std::move(Resolved));
        }
    }

    if (SunCount != 1u || SkyCount != 1u || AtmosphereCount != 1u || Activated.Geometry.empty())
    {
        return Deliver<ActivatedWorkspaceScene>::Refuse(
            { RefusalReason::ContentUnsupported, "the workspace does not declare one environment and scene geometry" });
    }

    return Deliver<ActivatedWorkspaceScene>::Result(Activated);
}

} // namespace Slate
