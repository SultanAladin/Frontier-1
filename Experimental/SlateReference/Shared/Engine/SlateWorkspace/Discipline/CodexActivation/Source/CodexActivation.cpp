//============================================================================================================================================
//                                                        CODEXACTIVATION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/CodexActivation/Api/CodexActivation.h"

#include <algorithm>
#include <string>

namespace Slate
{

bool ContentRecordIsCodexScene(const ContentRecord& Record)
{
    if (Record.Archive != ContentArchive::Arrangement || Record.Extension == nullptr)
        return false;

    return std::string(Record.Extension) == ".codex" || std::string(Record.Extension) == "codex";
}

void CenterActivatedSceneAtWorldOrigin(ActivatedWorkspaceScene& Scene)
{
    bool Any = false;
    double Minimum[3] = { 0.0, 0.0, 0.0 };
    double Maximum[3] = { 0.0, 0.0, 0.0 };
    for (const CodexSceneEntry& Entry : Scene.Workspace.Scene)
    {
        if (Entry.Subject != CodexSceneSubject::Geometry)
            continue;
        if (Entry.Naming.find("Floor") != std::string::npos)
            continue;
        if (!Any)
        {
            for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
                Minimum[Axis] = Maximum[Axis] = Entry.Position[Axis];
            Any = true;
            continue;
        }
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            Minimum[Axis] = std::min(Minimum[Axis], Entry.Position[Axis]);
            Maximum[Axis] = std::max(Maximum[Axis], Entry.Position[Axis]);
        }
    }
    if (!Any)
        return;

    const double Centre[3] =
    {
        (Minimum[0] + Maximum[0]) * 0.5,
        (Minimum[1] + Maximum[1]) * 0.5,
        (Minimum[2] + Maximum[2]) * 0.5
    };

    for (CodexSceneEntry& Entry : Scene.Workspace.Scene)
    {
        if (Entry.Subject != CodexSceneSubject::Geometry)
            continue;
        if (Entry.Naming.find("Floor") == std::string::npos)
            for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
                Entry.Position[Axis] -= Centre[Axis];
    }
    for (ActivatedGeometryEntry& Entry : Scene.Geometry)
    {
        if (Entry.Entry.Subject != CodexSceneSubject::Geometry)
            continue;
        if (Entry.Entry.Naming.find("Floor") == std::string::npos)
            for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
                Entry.Entry.Position[Axis] -= Centre[Axis];
    }
}

CodexActivation ConsumeCodexActivation(ContentBrowserConfiguration& Applied,
                                                          const ContentLibrary& Library,
                                                          const std::filesystem::path& EngineContentRoot)
{
    CodexActivation Result;

    if (Applied.ActivationRequested >= Library.RecordCount)
        return Result;

    Result.Requested = true;
    const ContentRecord& Requested = Library.Records[Applied.ActivationRequested];
    Applied.ActivationRequested = ContentLibrary::AbsentIndex;

    if (!ContentRecordIsCodexScene(Requested))
    {
        Result.Error = { RefusalReason::ContentUnsupported, "the selected content record is not a codex scene" };
        return Result;
    }

    const std::string Extension = Requested.Extension != nullptr && Requested.Extension[0] == '.'
                                ? std::string(Requested.Extension)
                                : "." + std::string(Requested.Extension != nullptr ? Requested.Extension : "");
    Result.ScenePath = EngineContentRoot / (std::string(Requested.Naming) + Extension);

    WorkspaceSceneActivation Activating;
    Deliver<ActivatedWorkspaceScene> Activated = Activating.Open(Result.ScenePath.string(), EngineContentRoot.string());
    if (!Activated.Resolved)
    {
        Result.Error = Activated.Error;
        return Result;
    }

    ActivatedWorkspaceScene Loaded = Activated.Resolve();
    CenterActivatedSceneAtWorldOrigin(Loaded);
    Result.Scene = Loaded;
    Result.Resolved = true;
    Result.Error = {};
    return Result;
}


}   // namespace Slate
