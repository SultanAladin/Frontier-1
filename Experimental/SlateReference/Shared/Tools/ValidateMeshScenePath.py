#!/usr/bin/env python3
"""Validate the mesh import / scene-path MVP wiring without needing external DCC files."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(path: str, needle: str) -> None:
    text = (ROOT / path).read_text()
    if needle not in text:
        raise AssertionError(f"{path} does not contain expected marker: {needle}")


PROXY_UNIT = "Engine/SlateWorkspace/Discipline/CodexSceneProxy/Source/CodexSceneProxy.cpp"


def main() -> None:
    checks: list[str] = []

    import_cpp = "Engine/SlateDocument/Format/SceneMeshImport/Source/SceneMeshImport.cpp"
    # 🔴 `ParametricSketchHost` is deleted. Its two concerns had different homes: the IMPORT is now a
    #    unit, and the scene-directory WIRING stayed with the host that ships. Pointing both at one
    #    file was what made this gate fail with "missing marker" rather than "missing behaviour".
    import_unit = "Engine/SlateWorkspace/Discipline/ContentImportCommit/Source/ContentImportCommit.cpp"
    host_cpp = "Engine/Application/EditorHost/Source/EditorHost.cpp"

    for ext, marker in [
        ("OBJ", "ImportObj"),
        ("glTF", "ImportGltfText"),
        ("GLB", "ImportGlb"),
        ("FBX", "ImportAsciiFbx"),
        ("STL", "ImportStl"),
        ("PLY", "ImportAsciiPly"),
    ]:
        require(import_cpp, marker)
        checks.append(f"{ext} import route stands")

    require(import_cpp, "MaterialSlots")
    require(import_cpp, "MaterialRecords")
    require(import_cpp, "DefaultWorkspaceMaterialRecord")
    checks.append("material slot capture and default material records stand")

    # 📝 The import directory listing moved to `SlateRuntime/Session/HostEnvironment`, which is how BOTH
    #    hosts came to recognise mesh formats — the editor host's private copy of this never did.
    require("Engine/SlateRuntime/Session/HostEnvironment/Source/HostEnvironment.cpp", "SceneMeshFormatSupported(Current.path().string())")
    checks.append("Content Browser import directory recognises mesh formats")

    require(import_unit, "ImportSceneMeshFile(ImportPath.string())")
    checks.append("host imports selected mesh files into the workspace scene")

    require(host_cpp, "BuildSceneDirectoryRows(OpenedScene, WorkspaceSceneRows)")
    checks.append("imported meshes flow through Scene Directory rows")

    # 📝 Step 10 moved the proxy behaviour out of the host and into a unit, so these three are checked
    #    where they now live. The host still CALLS them — the line above proves that — but a host that
    #    defined them again would be the defect this whole step exists to remove.
    require(host_cpp, "SelectSceneMeshAtPointer")
    require(PROXY_UNIT, "SelectSceneMeshAtPointer")
    checks.append("viewport mesh selection path stands, in the unit that owns it")

    require(host_cpp, "SynchroniseCodexTransformsFromSceneDirectory")
    require(PROXY_UNIT, "SynchroniseCodexTransformsFromSceneDirectory")
    checks.append("scene transform edits feed back to mesh entries")

    require("Engine/SlateVulkan/Device/WorkspaceScenePass/Api/WorkspaceScenePass.h", "class WorkspaceScenePass")
    require("Engine/SlateVulkan/Device/WorkspaceScenePass/Source/WorkspaceScenePass.cpp", "WorkspaceScenePass::Upload")
    checks.append("dedicated WorkspaceScenePass upload boundary stands")

    print(f"[MeshScenePath] {len(checks)} checks passed")
    for check in checks:
        print(f"[MeshScenePath] ✓ {check}")


if __name__ == "__main__":
    main()
