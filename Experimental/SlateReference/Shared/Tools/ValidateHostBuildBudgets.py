#!/usr/bin/env python3
"""Validation for host build-budget regressions found by the Windows build."""
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def offstack(host: str) -> bool:
    """Whether a host keeps ViewportSequence's ~406 KB out of its automatic frame.

    A Windows thread is handed one megabyte and the budget these hosts assert against is a quarter of
    it. ViewportSequence alone is over four hundred kilobytes, so it has to live in static storage or
    the prologue's stack probe faults before main runs a statement -- no window, no log line.

    Two spellings satisfy that. A host may hold the sequence directly, or hold a SessionSequence that
    holds it; SlateRuntime owns the bring-up from step 7 onward, so the second is what a lifted host
    looks like. This tests the MECHANISM -- 406 KB off the stack -- rather than one of its spellings,
    because the earlier literal check reported a regression when a host was lifted correctly.
    """
    return ("static ViewportSequence Viewport;" in host or
            "static SessionSequence Session;" in host)


def main() -> int:
    motion = read("Engine/SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h")
    match = re.search(r"EaseCapacity\s*=\s*(\d+)u", motion)
    require(match is not None, "MotionIntegrator must declare EaseCapacity")
    require(int(match.group(1)) >= 8192, "MotionIntegrator eased capacity must cover runtime editor startup registrations")
    require("static storage in the windowed hosts" in motion, "MotionIntegrator comment must record why the reserve is safe")

    editor = read("Engine/Application/EditorHost/Source/EditorHost.cpp")
    require("constexpr std::size_t AutomaticUiBytes = sizeof(ShaderCodec) + sizeof(WorkspaceOverlayPass);" in editor,
            "EditorHost stack assertion must only count members still on automatic storage")
    require(offstack(editor), "EditorHost must keep the motion-heavy viewport sequence off the stack")
    for needle in [
        "static WorkspaceIndex          Workspaces;",
        "static WorkspacePanel          Workspace;",
        "static EditorPanel             WorkspacePanels;",
        "static ControlCentrePanel      ControlCentre;",
        "static SceneDirectoryPanel     SceneDirectory;",
        "static TexturingPanel        Texturing;",
        "static ParametricWorkspacePanel SketchDirectory;",
        "static ParametricToolsPanel    ParametricTools;",
    ]:
        require(needle in editor, f"EditorHost missing static storage move {needle!r}")
    # 📝 `HomeProfilePath` was defined identically in two hosts and now lives in
    #    `SlateRuntime/Session/HostEnvironment`, so the MSVC guard is checked there — one place, both hosts.
    Environment = read("Engine/SlateRuntime/Session/HostEnvironment/Source/HostEnvironment.cpp")
    require("_dupenv_s(&Home" in Environment,
            "HostEnvironment must avoid MSVC getenv warning on Windows")
    require("_dupenv_s(&Home" not in editor,
            "EditorHost must not carry its own copy of the home profile lookup")
    require("std::strncpy" not in editor, "EditorHost must avoid MSVC strncpy warning")
    require("ResizedGeometryOffering" in editor and "ResizedGeometryDelivery" in editor,
            "EditorHost resize path should not shadow geometry construction locals")
    # 📝 `ConsumeSharedCodexActivation` was an `inline` in `Application/Api/SharedViewportHostBridge.h`,
    #    which is deleted. It is now `ConsumeCodexActivation` in `SlateWorkspace/Discipline/CodexActivation`.
    require("ConsumeCodexActivation" in editor,
            "EditorHost activation must use the shared codex activation unit")

    # 🔴 `PaintHost` AND `ParametricSketchHost` ARE DELETED. Their claims were about behaviour the
    #    PRODUCT must have, not about those two files, so they are re-aimed at `EditorHost` -- the one
    #    subject all three products build from. Two claims below pin the deletion.
    require(not (ROOT / "Engine/Application/PaintHost").exists(),
            "PaintHost is deleted; TextureAuthoring builds from EditorHost")
    require(not (ROOT / "Engine/Application/ParametricSketchHost").exists(),
            "ParametricSketchHost is deleted; ParametricAuthoring builds from EditorHost")
    require("EditorCameraComponent" in editor and "CameraInput" in editor,
            "the editor viewport must use the shared camera component and hotkey path")
    require("std::strncpy" not in editor, "EditorHost must avoid MSVC strncpy warning")

    # 🔴 THE WIRING THAT KEPT `ParametricSketchHost` ALIVE. The hosts held ZERO definitions, so
    #    deleting them broke no compile and tripped no gate -- while quietly removing the product's
    #    ability to draw. These claims are what make that impossible to repeat.
    require("ApplyWorkplaneTool(" in editor,
            "the editor must dispatch the workplane tool")
    require("DriveDrawingWithModifiers(" in editor,
            "the editor must dispatch the sketch drawing tools")
    require(editor.index("ApplyWorkplaneTool(") < editor.index("DriveDrawingWithModifiers("),
            "the workplane tool must be offered the press before the drawing tools")
    require("HostHasFeature(FeatureParametric)" in editor,
            "the parametric wiring must be gated by the product feature, not an #ifdef")

    # 🔴 THE TOOL PANEL MUST ADVANCE BEFORE THE VIEWPORT READS THE ACTIVE TOOL. `ParametricSketchHost`
    #    advanced at line 644 and drew at 776, and a gate asserted that order -- but the gate named the
    #    host BY ITS PATH, so deleting the host deleted the CLAIM instead of re-aiming it. The editor
    #    then advanced at the END of the frame: the click that picked Line was not visible to the
    #    drawing code until the next frame, so picking a shape and clicking the grid drew nothing.
    require(editor.index("ParametricTools.Advance(") < editor.index("DriveDrawingWithModifiers("),
            "the tool panel must advance before the viewport reads the active tool")

    # 🔴 RECORDING A CURVE IS NOT DRAWING IT. The editor took the presses and built the sketch, then
    #    showed nothing, because the projection and the overlays stayed behind in the deleted host.
    #    Each of these is a separate way for the viewport to go blank while every gate stays green.
    # 🔴 THE WORLD-BACKED PATH IS THE ONE THAT SHIPS. `ProjectWorldBackedSketchRendering` takes the
    #    place of the one-plane `ProjectSketchRendering`, and `ProjectWorldPlacementPreview` takes the
    #    place of `ProjectPlacementPreview`; the claims are re-aimed at the mechanism, not a spelling.
    for Drawn, Why in (
            ("ProjectWorldBackedSketchRendering(", "world sketch curves must be projected into the CAD packet"),
            ("ProjectWorldPlacementPreview(", "the world-backed tool in flight must show its preview"),
            ("ResolvePlacementCurves(", "every curve subject must preview, not the seven with a branch")):
        require(Drawn in editor, f"{Why} -- {Drawn} is not called by the host that ships")

    # 🔴 THE PREVIEW IS NOT DRAWN ON THE CPU. `RecordPlacementPreview` walked the shape under the
    #    pointer into ImGui draw lists every frame -- the most frequently redrawn geometry in the
    #    editor was the last thing still tessellated per frame by the CPU -- and it named one subject
    #    per branch, so Hermite, basis spline and NURBS previewed as nothing at all. It is replaced by
    #    a projection into the same packet the GPU pass already rasterises.
    require("RecordPlacementPreview(" not in editor,
            "the CPU placement preview is gone; the preview belongs in the CAD packet")

    # 🔴 SNAPPING IS HELD, NOT SUFFERED. Control used to SUSPEND snapping, so every pointer move was
    #    dragged onto the nearest endpoint, midpoint or grid corner whether the artist wanted it or
    #    not. It is off by default and Control turns it on, which is the opposite arrangement.
    interaction = read("Engine/SlateWorkspace/Discipline/SketchInteraction/Source/SketchInteraction.cpp")
    require("Modifiers.Commanded\n        ? ResolveNearestWorldSnap(" in interaction
            and "ResolveCompatibilitySnap(WorldPlacement, Mapping)" in interaction,
            "world drawing must resolve snapping against the active world sketch")
    world_snap = read("Engine/SlateShape/World/WorldSketchSnap/Source/WorldSketchSnap.cpp")
    world_snap_api = read("Engine/SlateShape/World/WorldSketchSnap/Api/WorldSketchSnap.h")
    require("ResolveNearestWorldSnap(" in world_snap and "const WorldPlacementFrame& ActiveFrame" in world_snap_api,
            "world snapping must accept the active support frame explicitly")
    require("SketchStructure/Api" not in world_snap and "ResolveSketchBasis" not in world_snap,
            "world snapping must not depend on the compatibility sketch or sketch basis")

    # 🔴 CONSTRAINTS FOLLOW SNAP'S AUTHORITY BOUNDARY. A world constraint that is merely mirrored into
    #    the old solver is still sketch-native in practice: the next solve can move the compatibility
    #    geometry away from the world model. These checks name the world declaration, semantic authoring,
    #    and world solve seams independently so an implementation cannot satisfy the integration call by
    #    routing through `SketchStructure`.
    world_structure = read("Engine/SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h")
    world_authoring = read("Engine/SlateWorkspace/Discipline/WorldSketchConstraintAuthoring/Source/WorldSketchConstraintAuthoring.cpp")
    world_constraint_solver = read("Engine/SlateShape/World/WorldSketchConstraintSolver/Source/WorldSketchConstraintSolver.cpp")
    require("WorldConstraintName" in world_structure and "DeclareConstraint" in world_structure
            and "Resolve(WorldConstraintName" in world_structure,
            "the world sketch must own constraint identifiers, storage, and resolution")
    require("const WorldPick& Primary" in read("Engine/SlateWorkspace/Discipline/WorldSketchConstraintAuthoring/Api/WorldSketchConstraintAuthoring.h")
            and "DeclareWorldConstraintFrom" in world_authoring
            and "SketchStructure/Api" not in world_authoring,
            "world constraint authoring must consume semantic world picks without the compatibility sketch")
    require("ApplyWorldConstraint(" in world_constraint_solver
            and "EnforceWorldSketchPoint" in world_constraint_solver
            and "SketchStructure/Api" not in world_constraint_solver,
            "world constraints must solve against world geometry rather than the sketch solver")
    require("ApplyViewportWorldConstraintTool(" in interaction
            and "ApplyWorldSketchToSketch(World, Mapping, Sketch)" in interaction
            and "const WorldSketchStructure WorldBefore = World" in interaction
            and "const auto Rollback = [&]()" in interaction,
            "the viewport must apply world constraints before mirroring compatibility geometry and roll back failed commits")
    require("IsConstraintTool(ParametricToolsApplied.ActiveSubject)" in editor
            and "ParametricToolsApplied.ActiveSubject," in editor,
            "constraint tools must enter the world-backed interaction arm")

    # 🔴 DRIVING DIMENSIONS FOLLOW THE SAME HANDOFF. Placement and text editing must not leave a second
    #    sketch-only dimension authority behind the world model, or changing a value would immediately
    #    diverge the two documents again.
    world_dimension_authoring = read("Engine/SlateWorkspace/Discipline/WorldSketchDimensionAuthoring/Source/WorldSketchDimensionAuthoring.cpp")
    world_dimension_solver = read("Engine/SlateShape/World/WorldSketchDimensionSolver/Source/WorldSketchDimensionSolver.cpp")
    require("WorldDimensionName" in world_structure and "DeclareDimension" in world_structure
            and "Resolve(WorldDimensionName" in world_structure,
            "the world sketch must own driving-dimension identifiers, storage, and resolution")
    require("DeclareWorldDimensionFrom" in world_dimension_authoring
            and "SketchStructure/Api" not in world_dimension_authoring,
            "world dimension authoring must remain independent of the compatibility sketch")
    require("ApplyWorldDimension(" in world_dimension_solver
            and "ResolveWorldDimensionValue" in world_dimension_solver
            and "SketchStructure/Api" not in world_dimension_solver,
            "world dimensions must measure and drive exact world geometry")
    require("ApplyViewportWorldDimensionTextEdit" in interaction
            and "ResolveWorldDimensionForSketchDimension" in interaction
            and "WorldDimensionMapped" in interaction
            and "else if (ApplyDimensionTextEdit" in interaction
            and "if (Sealed.Subject == SketchSubject::Dimension)" in interaction,
            "dimension text edits and placement must refuse world failures without legacy fallback")

    # 📝 The plural is checked above: a Hermite and a polyline draw more than one span, and asking for
    #    the singular is what drew the first two Hermite points and left the rest as bare points.

    # 🔴 EXACTLY ONE GRID, AND IT IS THE ANALYTIC ONE. `RecordViewportGridOverlay` draws a SECOND
    #    lattice as CPU line segments on top of the ground the overlay pass already renders, and
    #    projects it through the orbit camera while the real grid uses the free editor camera -- so it
    #    tracked correctly up and down and ran the wrong way left and right. The existing grid is the
    #    grid. This claim previously demanded the opposite and was wrong.
    require("RecordViewportGridOverlay(" not in editor,
            "the host must not draw a second CPU grid over the analytic one")

    # 🔴 SKETCH GEOMETRY RASTERISES ON THE GPU. Drawing it through the interface's draw lists makes
    #    ImGui tessellate every segment on the CPU each frame -- the viewport slows as shapes
    #    accumulate, and each fill triangle shows its own anti-aliased edges as a wireframe.
    require("CadPass.Upload(" in editor and "CadPass.RecordAround(" in editor,
            "the sketch must be uploaded to and recorded by the GPU CAD pass")
    require("RecordCadFallback(" not in editor,
            "the retired CPU CAD fallback must stay deleted from the shipping host")
    require("GPU CAD is required for sketch rendering" in editor,
            "the host must report that sketch rendering now requires the GPU CAD pass")

    # 🔴 ONE CAMERA. The sketch used orbit angles copied off the editor camera, which resolves a
    #    genuinely different frame -- geometry sat on a surface that slid out from under it.
    require("ResolveOrbitStandingFromFree(" in editor,
            "sketch geometry must project through the same camera as the scene")

    # 🔴 A DRAWER HIDES THE STRIP IT COVERS, NOT THE WHOLE VIEWPORT. The overlay pass records after the
    #    interface, so an open Control Centre / Content Browser cannot occlude it -- the first fix was to
    #    skip the overlay entirely while a drawer stood, which is why the grid and the sketch vanished
    #    the moment either was opened. The loop must clip to the uncovered band instead of suppressing.
    require("ForegroundDrawerStanding" not in editor,
            "the overlay must clip to the uncovered band, not be suppressed while a drawer stands")
    require("UncoveredTop" in editor and "UncoveredBottom" in editor,
            "the GPU passes must compute the band no drawer covers")
    require("CadClipY0" in editor and "CadClipY1" in editor,
            "the CAD pass must clip to the drawer-free band too, or sketch fills draw over the drawers")

    # 🔴 THE CAMERA RECTANGLE AND THE SCISSOR ARE TWO DIFFERENT THINGS. `LeafRect` is pushed to the
    #    shader, where the fragment stage maps the camera's field of view across it -- so passing the
    #    drawer-clipped box for both does not clip the grid, it SQUASHES the camera into the remaining
    #    space. One value served both roles, which is why hiding the covered strip squashed the grid
    #    and un-squashing it un-hid the strip: the two symptoms traded back and forth for several
    #    attempts because they were the same number. The record call must pass the WHOLE leaf first.
    #    The two roles are now separated by `RecordOverlayAround`, which takes the leaf ONCE and passes it
    #    through to every band unchanged: the call site states the leaf and the scissor separately, and the
    #    helper is the only thing that reaches `Overlay.Record`.
    OverlayRecord = editor[editor.index("Overlay.RecordAround("):]
    OverlayRecord = OverlayRecord[:OverlayRecord.index(";")]
    require("LeafRect.MinimumY," in OverlayRecord and "ScissorY0," in OverlayRecord,
            "the overlay must receive the whole leaf as its camera rect and the visible band as its scissor")
    require(OverlayRecord.index("LeafRect.MinimumY,") < OverlayRecord.index("ScissorY0,"),
            "the leaf box must precede the scissor in the overlay record call")

    CadRecord = editor[editor.index("CadPass.RecordAround("):]
    CadRecord = CadRecord[:CadRecord.index(";")]
    require("CadClip.MinimumY" in CadRecord and "CadWithheld.MinimumY" in CadRecord,
            "the CAD pass must receive the drawer-clipped band and the popup withheld box separately")
    require(CadRecord.index("CadClip.MinimumY") < CadRecord.index("CadWithheld.MinimumY"),
            "the CAD clip band must precede the withheld popup in the CAD record call")

    # 🔴 THE FOOTER'S ORTHO/PERSPECTIVE BUTTON MUST REACH THE CAMERA. `PanelConfiguration[].Perspective`
    #    stored the artist's choice while the camera was resolved perspective unconditionally and the
    #    overlays were passed a literal `true`, so the button moved a label and nothing else.
    require("LeafPerspective" in editor,
            "the viewport leaf must read the panel's Ortho/Perspective choice")
    require(editor.count("LeafPerspective") >= 6,
            "the projection mode must reach the camera and every sketch overlay, not just one of them")
    require(", true," not in editor.split("ResolveFreeCamera(")[1].split(";")[0],
            "the free camera must not be resolved with a hardcoded projection mode")

    validation = read("Engine/Application/InterfaceValidationHost/Source/InterfaceValidationHost.cpp")
    require("std::strncpy" not in validation, "InterfaceValidationHost must avoid MSVC strncpy warning")

    parametric = editor
    require("_dupenv_s(&Home" not in editor,
            "the editor must not carry its own copy of the home profile lookup")
    require("ConsumeCodexActivation" in parametric,
            "ParametricSketchHost activation must use the shared codex activation unit")

    # 🔴 THE BRIDGE HEADER IS GONE AND MUST STAY GONE. 675 `inline` lines under `Application/` that three
    #    hosts included — the orientation widget, the camera seed, the content root and the codex
    #    activation, none of which were bridges and none of which could be tested where they sat.
    require(not (ROOT / "Engine" / "Application" / "Api" / "SharedViewportHostBridge.h").exists(),
            "SharedViewportHostBridge.h must not come back")
    for Host, Body in (("EditorHost", editor),):
        require("SharedViewportHostBridge" not in Body,
                f"{Host} must not include the deleted bridge header")

    # 🔴 This block used to demand the literal spelling `SharedCadDrawingController.h` in both hosts. That
    #    asserted a FILE NAME, not a property, so it reported a regression the moment the duplicated
    #    dispatch was correctly unified into `SlateToolset` — the same defect that made this validator
    #    demand `static ViewportSequence Viewport;` after the session was lifted. What actually matters is
    #    that the draft vocabulary is declared ONCE, in a unit, and that no host re-declares it.
    toolset = read("Engine/SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h")
    require("enum class SketchSubject" in toolset and "DeclaredPlacement" in toolset,
            "SketchToolset must own the shape-and-method dispatch")
    for host_name, host in (("EditorHost", editor),):
        require("enum class ParametricDraftSubject" not in host and
                "enum class SharedCadDraftSubject" not in host and
                "enum class SketchSubject" not in host,
                f"{host_name} must not re-declare the sketch subject vocabulary")
    # 🔴 The cast bridge is the specific defect this replaces: two identical enumerations reconciled by
    #    casting through the underlying integer, correct only while both stayed in the same order.
    require("static_cast<SharedCadDraftSubject>" not in parametric and
            "static_cast<ParametricDraftSubject>" not in parametric,
            "ParametricSketchHost must not cast between duplicate draft enumerations")
    require("SketchPlacement" in parametric,
            "ParametricSketchHost must drive drawing through the SketchToolset placement")
    # 🔴 The banned words. `Draft`/`Draught` name a provisional state rather than a mechanism, and
    #    `Paint` names the artist's gesture rather than the texels written.
    #
    # 🔴 THE SWEEP IS DONE, SO THE GATE NOW COVERS THE SWEPT GROUND. It used to check only the two
    #    toolsets, because 690 uses stood in SlateUI/SlateCompute/SlateDocument and a gate that fails
    #    on day one is a gate someone deletes. 610 identifier occurrences were renamed, the two
    #    directories moved, and these units are gated from here on.
    #
    # ⚠️ `PaintHost` is EXCLUDED BY NAME, not overlooked. It is deleted at step 11, and until the
    #    parametric wiring it holds is moved into `EditorHost` that deletion would take drawing with
    #    it — see References/RemainingWorkPlan.md. Excluding a known exception by name is honest;
    #    quietly widening the pattern until it passes is not.
    SweptUnits = ("SketchToolset", "TextureToolset", "SlateWorkspace", "SlateCompute", "SlateUI")
    for Unit in SweptUnits:
        for Source in (ROOT / "Engine" / Unit).rglob("*.*"):
            if Source.suffix not in (".h", ".cpp"):
                continue
            Body = Source.read_text(encoding="utf-8", errors="ignore")
            # 🔴 THE TWO EXEMPTIONS THAT USED TO LIVE HERE WERE BOTH WRONG, AND EACH KEPT A BANNED
            #    WORD ALIVE FOR A REASON THAT DID NOT HOLD.
            #    ① The CAD angle was said to have "no synonym". It has one, and it is the word the
            #       standards use: TAPER. The property now reads `"Taper", "0°"` and means the same
            #       thing to the artist.
            #    ② The profile enumerator was said to be "matched against documents already on
            #       disk". It is not: `ProfileOf` classifies a PATH by its suffix and the enumerator
            #       name never reaches the file. The VALUE `10u` is what is persisted, and it has not
            #       moved. `.draft` is still classified, so old documents still open — only the
            #       C++ spelling changed, to `CodexProfile::Parametric`.
            #    An exemption is a claim about the code, and a claim nobody rechecks becomes a place
            #    for exactly the thing the gate exists to prevent.
            for Line in Body.splitlines():
                Code = Line.split("//", 1)[0]
                Stripped = Code.strip()
                if not Stripped:
                    continue
                # 📝 `.draft` survives as a READ-ONLY suffix so documents saved under it keep
                #    opening. It is a string compared against a path, not a name in the source.
                if '".draft"' in Code:
                    continue
                for Banned in ("Draught", "Draft"):
                    require(Banned not in Code,
                            f"{Source.name} carries the banned word {Banned}: {Stripped[:70]}")
            # 🔴 `"PaintStroke"` was exempted here as a "persisted revision label ... compared by
            #    string against documents already written". It is neither persisted nor compared:
            #    `RevisionSequence::Open` takes it as `OperationName`, "the mechanism's spelling",
            #    to caption the undo entry, and no encoder writes it. It is now `"TextureStroke"`.
            #    The word survives in this engine only where prose explains why it was retired.
            for Line in Body.splitlines():
                # 📝 A trailing comment is prose too. Checking the whole line flagged
                #    `TextureFacetCount = 8u;  // [-] - Paint … Filter`, where the code is clean and
                #    only the note carries the word — a false positive that would have been "fixed"
                #    by mangling a perfectly good declaration.
                Code = Line.split("//", 1)[0]
                Stripped = Code.strip()
                if not Stripped:
                    continue
                require("Paint" not in Code,
                        f"{Source.name} carries the banned word Paint in code: {Stripped[:70]}")
    # 📝 Step 10 moved the interaction out of the host and into
    #    `SlateWorkspace/Discipline/SketchInteraction`, so the guarantee is checked where it now lives.
    #    ⚠️ The host is checked NOT to define it: a host that grew its own copy back is exactly the
    #    regression this file exists to catch, and looking only in the host could not tell the two apart.
    Interaction = (ROOT / "Engine" / "SlateWorkspace" / "Discipline" / "SketchInteraction"
                        / "Source" / "SketchInteraction.cpp").read_text(encoding="utf-8")
    require("ResolveGizmoHandle" in Interaction
            and "StartTransformSession" in Interaction
            and "UpdateTransformSession" in Interaction,
            "transform gizmo handles must remain selectable and movable")
    require("void DriveViewportSelectionAndTransform(" not in editor,
            "the editor must not define the interaction it now calls")

    revision_history = read("Engine/SlateWorkspace/Discipline/SketchRevisionHistory/Source/SketchRevisionHistory.cpp")
    require("SketchRevisionSnapshot ResolveSketchRevisionSnapshot" in revision_history
            and "void ApplySketchRevisionSnapshot" in revision_history
            and "void RecordSketchRevisionSnapshot" in revision_history
            and "bool RetreatSketchRevision" in revision_history
            and "bool ReinstateSketchRevision" in revision_history
            and "struct SketchRevisionSnapshot" not in editor
            and "bool RetreatSketchRevision" not in editor,
            "revision snapshot state and undo/redo mechanics must live in a workspace unit, not the host")

    # 🔴 THIS CLAIM'S NEIGHBOUR ABOVE SAID "the interaction it now calls" AND NEVER CHECKED THAT IT DID.
    #    It did not. Selection, the gizmo, the transform sessions and the whole Blender-style G/R/S
    #    parser were implemented, unit-proven and wired to nothing — `DriveViewportSelectionAndTransform`
    #    had zero call sites outside its own translation unit, so the Select tool did nothing at runtime
    #    and no gate noticed. A guarantee that a host does not DEFINE something is worth very little
    #    without the matching guarantee that it USES it: together they say the code lives in one place
    #    AND runs.
    #
    # ⚠️ Every entry point below is reachable from the viewport arm. Extracting a function into a unit
    #    and forgetting the call is a silent regression — the build stays green, the gates stay green,
    #    and the feature is simply absent.
    for Entry in ("DriveViewportSelectionAndTransform",
                  "DriveDrawingWithModifiers",
                  "ApplyWorkplaneTool"):
        require(f"{Entry}(" in editor,
                f"the editor must actually CALL {Entry} — an uncalled interaction is an absent feature")

    # 🔴 Q IS THE SELECTION TOOL, and a shortcut is only a shortcut if something reads it.
    require("KeySubject::ChooseSelect" in editor
            and "ParametricToolSubject::Select" in editor,
            "Q must reach the host and make Select the active tool")

    # 🔴 AND NOTHING ELSE MAY OWN Q AT THE SAME TIME. The fly camera bound Q to "descend" and read its
    #    keys whenever the artist was not typing, so pressing Q to choose the Select tool ALSO sank the
    #    camera — two features silently sharing one key. The fly keys now belong to the fly gesture,
    #    which is what every reference fly-cam this was modelled on does and why an editor can afford
    #    to spend letters on tools at all.
    seam = read("Engine/SlateUI/Interface/InterfaceExchange/Source/InterfaceExchange.cpp")
    require("if (!Typing && Current.LookHeld)" in seam,
            "WASDEQ must be read only while the look gesture is held, or Q drives two features at once")
    look_input = read("Engine/SlateWorkspace/Discipline/ViewportLookInput/Source/ViewportLookInput.cpp")
    require("IsViewportLookNavigationKey" in look_input
            and "FilterViewportLookTextInput" in look_input
            and "TextInputCondition FilterViewportLookTextInput" not in editor
            and "bool IsViewportLookNavigationKey" not in editor
            and "if (!ViewportLookHeld && Viewport.Seam().KeyPressed(KeySubject::ChooseSelect))" in editor,
            "when RMB-look owns the viewport, WASDEQ must be filtered in the workspace and withheld from sketch tools and Select")

    # 🔴 ORTHOGRAPHIC ZOOM MUST BE DRIVEN BY SOMETHING. `OrthoScale` was written in exactly one place in
    #    the whole tree — a function with no call sites — so a wheel notch in a parallel view changed
    #    nothing at all, in every one of the seven orientations.
    require("SketchView.OrthoScale = std::clamp(" in editor,
            "the host must drive OrthoScale from the wheel, or a parallel view cannot zoom")
    require("THE BASIS IS READ AFTER THE ACTIVE PLANE IS SYNCHRONISED" in editor,
            "Front and Side views must re-read the sketch basis immediately after switching planes")

    overlay_fragment = read("Engine/SlateVulkan/Device/WorkspaceOverlayPass/Shader/WorkspaceOverlayFragment.slang")
    require("PlaneNormal = Real32x3(0.0, 0.0, 1.0)" in overlay_fragment
            and "PlaneNormal = Real32x3(1.0, 0.0, 0.0)" in overlay_fragment,
            "the GPU overlay must draw grids for the XY and YZ orthographic planes, not only XZ")

    # 🔴 AND THE OVERLAY MUST BE KEPT OFF AN OPEN MENU. Both GPU passes record AFTER the interface and
    #    scissor to the whole viewport leaf, so the grid, the axes and the sketch drew straight over any
    #    dropdown opened from the footer -- which reads as a transparent dropdown, because what shows
    #    through is the viewport behind it. `AnyPopupStanding` existed for this, with a comment naming the
    #    defect, and nothing ever called it: a negative gate cannot tell a wired predicate from a dead one,
    #    so this one names both passes.
    require("AnyPopupStanding()" in editor and "PopupExtent()" in editor,
            "the host must ask the panel whether a menu stands and where, or the overlay draws over it")
    require("Overlay.RecordAround(" in editor and "CadPass.RecordAround(" in editor,
            "BOTH GPU passes must record around the open menu -- the grid and the sketch alike")

    # 🔴 THE CONTEXT MENU MUST BE REACHABLE, PLACED AND AVOIDING SOMETHING. Four times now a correct
    #    function has sat in this tree with no call site, so each half of the wiring is named here: the
    #    menu is opened by a gesture, recorded each tick, and told what not to cover.
    require("SketchContextMenu.Open(" not in editor and "SketchContextMenu.Record(" not in editor,
            "operations are visual-only for now, so the retired construction popup must stay dormant")
    require("SketchContextMenu.Avoid(SketchToolOptions.Occupies())" in editor,
            "selection still keeps the context menu away from the floating tool options widget")
    require("SecondaryClickTravel" in editor and "SecondaryReleased" in editor,
            "the stationary secondary-click gesture remains partitioned from camera look")
    require("SketchBuildTool = ParametricToolSubject::Select;" in editor
            and "SketchBuildApply = false;" in editor,
            "operation picks must disarm the legacy build/apply path while the tiles stay visual-only")
    require("ApplyViewportEditTool(" not in editor,
            "the shipping host must not call the retired operation algorithm while operations are disabled")

    # 📐 And the catalogue must still expose the parked operation subjects.
    for tool in ("ParametricToolSubject::Fillet", "ParametricToolSubject::Chamfer",
                 "ParametricToolSubject::Trim", "ParametricToolSubject::Cut",
                 "ParametricToolSubject::Extend"):
        require(tool in editor, f"the construction catalogue must reach {tool}")

    # 🔴 THE EDIT TOOL'S CORNER ARM MUST DO THE CORNER. It used to call `CutCurve` and label the revision
    #    "Fillet Preparation" -- a name that admits it never filleted anything.
    interaction = read("Engine/SlateWorkspace/Discipline/SketchInteraction/Source/SketchInteraction.cpp")
    require("return false;" in interaction.split("bool ApplyViewportEditTool(", 1)[1],
            "the retired viewport edit algorithm must be stubbed out while operations are visual-only")

    # 📝 These five claims outlived the file they were written against. The orientation widget now lives in
    #    `SlateWorkspace/Discipline/OrientationCube` and the codex activation in
    #    `SlateWorkspace/Discipline/CodexActivation`; the guarantees are unchanged, only their address is.
    orientation = read("Engine/SlateWorkspace/Discipline/OrientationCube/Source/OrientationCube.cpp")
    require("RecordOrientationWidget" in orientation and "HitOrientationWidget" in orientation,
            "the orientation unit must own the one-at-a-time gizmo dispatch")
    require("Extent.MinimumX + 52.0f" not in orientation and "Extent.MaximumX - 70.0f" in orientation,
            "both Blender and CAD viewport gizmos must be anchored at the top-right of the viewport")
    require("CubeAxisDepth" in orientation,
            "viewport gizmos must use the HTML-reference camera-forward depth ordering")
    require("DrawFaceLabel" in orientation and "TextRun(Face" not in orientation,
            "CAD cube face labels must be projected face strokes, not hovering screen-space text")

    activation = read("Engine/SlateWorkspace/Discipline/CodexActivation/Source/CodexActivation.cpp")
    require("CenterActivatedSceneAtWorldOrigin" in activation
            and "CenterActivatedSceneAtWorldOrigin(Loaded)" in activation,
            "codex scene activation must recenter loaded geometry at the world origin")

    cad_shader = read("Engine/SlateVulkan/Device/WorkspaceCadPass/Shader/WorkspaceCadVertex.slang")
    require("ClipWorkspaceCadFillTriangleNear(" in cad_shader,
            "the CAD shader must near-clip fill triangles rather than dropping or stretching them")
    require('"Shared/WorkspaceCadNearClip.slang.h"' in cad_shader,
            "the CAD shader must include the shared near-plane clipper")
    cad_shared = read("Engine/Shared/WorkspaceCadNearClip.slang.h")
    require("WorkspaceCadNearDepth" in cad_shared,
            "the shared CAD near-plane threshold must be declared once")
    require("ClipWorkspaceCadFillTriangleNear(" in cad_shared
            and "ClipWorkspaceCadSegmentNear(" in cad_shared
            and "IntersectWorkspaceCadNear(" in cad_shared,
            "the shared CAD near-plane clipper must define fill, segment and intersection paths")
    require("ClipWorkspaceCadSegmentNear(" in cad_shader,
            "the CAD shader must near-clip outline segments as well as fills")
    cad_pass = read("Engine/SlateVulkan/Device/WorkspaceCadPass/Source/WorkspaceCadPass.cpp")
    require("DrawCategory(WorkspaceCadDraw::Fill, PacketFillCount, 6u);" in cad_pass,
            "CAD fills must spend two triangles per record so a clipped quad can be emitted")

    # 🔴 THE WORKPLANE TOOL MUST BE DISPATCHED, NOT MERELY DEFINED. `ApplyWorkplaneTool` was written in
    #    full — screen-space placement, catalogue write, directory record, sealed revision — and called
    #    from nowhere. It compiled, two proofs covered its arithmetic, and picking the tool in the
    #    viewport did nothing at all, because a function nothing calls is indistinguishable from a
    #    function that does not work. No gate noticed: they all asserted the behaviour EXISTS.
    #
    # ⚠️ The ordering matters as much as the call. The workplane tool must run BEFORE the drawing tools
    #    and consume the press, or the click that places a plane is also read as the first point of a
    #    curve — drawn onto the plane that press just replaced.
    # 📝 The host these claims were written against is deleted; the wiring moved into `EditorHost`,
    #    and the claims moved with it. They are asserted once, above, beside the deletion pins.
    require("PointerTaken = PointerTaken || ApplyWorkplaneTool(" in editor,
            "placing a workplane must consume the press so no curve starts on the replaced plane")

    interaction = read("Engine/SlateWorkspace/Discipline/SketchInteraction/Source/SketchInteraction.cpp")
    require("Workplanes.Declare(Placed" in interaction and
            "Sketch.DeclarePlane(ResolveSketchPlaneFromWorkplane(Workplanes.Active()))" in interaction,
            "a placed workplane must join the catalogue and be adopted, never overwrite the sketch plane blind")

    # 🔴 The compatibility path is still needed for dimensions and legacy refusal fallback, but it must
    #    not silently choose its plane from the sketch document when an active workplane is available.
    placement = read("Engine/SlateWorkspace/Discipline/PlacementCommit/Source/PlacementCommit.cpp")
    require("const SketchPlane& Plane" in placement and "ResolvePlacementBasis(Plane)" in placement,
            "placement declarers must receive and measure against an explicit authoring plane")
    require("ResolveSketchBasis(" not in placement,
            "placement commit must not resolve its active basis through the sketch-basis compatibility unit")
    sketch_structure = read("Engine/SlateShape/Sketch/SketchStructure/Source/SketchStructure.cpp")
    for ExplicitProfile, Why in (
            ("DeclareCircleProfile(const CircleCurve& Declared,", "circle profiles must accept an explicit active plane"),
            ("DeclareEllipseProfile(const EllipseCurve& Declared,", "ellipse profiles must accept an explicit active plane"),
            ("DeclareRegularPolygon(const SpatialPoint& Centre,", "polygon profiles must accept an explicit active plane"),
            ("DeclareSlot(const SpatialPoint& StartPoint,", "slot profiles must accept an explicit active plane")):
        require(ExplicitProfile in sketch_structure and "const SketchPlane& ActivePlane" in sketch_structure,
                Why)
    for ExplicitCall, Why in (
            ("Sketch.DeclareCircleProfile(Circle, Plane)", "circle placement must pass its active plane to profile declaration"),
            ("Sketch.DeclareEllipseProfile(Ellipse, Plane)", "ellipse placement must pass its active plane to profile declaration"),
            ("Placed.Anchors[0], Radius, Sides, Plane,", "polygon placement must pass its active plane to profile declaration"),
            ("Sketch.DeclareSlot(Placed.Anchors[0], Placed.Anchors[1], Radius, Plane)",
             "slot placement must pass its active plane to profile declaration")):
        require(ExplicitCall in placement, Why)
    require("CommitPlacement(Naming, Sketch, ResolveSketchPlaneFromWorkplane(Workplanes.Active())" in interaction,
            "drawing fallback commits must pass the active workplane plane explicitly")
    bridge = read("Engine/SlateWorkspace/Discipline/WorldSketchBridge/Source/WorldSketchBridge.cpp")
    require("DeclareWorldDimensionFrom(WorldDimensionSubject::Aligned" in bridge
            and "MirrorWorldDimensionIntoSketch" in bridge,
            "world bridge dimension commits must declare the world dimension before its compatibility record")
    world_commit = bridge[bridge.index("bool CommitPlacementWorldBacked("):]
    require("Declared.CurveCount() == 0u && SketchHasCommittedGeometry(Sketch)" in world_commit,
            "world-backed placement may import the sketch only while the world model is unseeded")
    require("Declared.CurveCount() != static_cast<std::uint32_t>(Sketch.Curves().size())" not in world_commit,
            "world-backed placement must not rebuild live world geometry from a partial sketch mirror")
    require("const WorldSketchStructure WorldBefore = Declared" in world_commit
            and "!MirrorSketchIntoWorldSketch(Sketch, Declared, Mapping)" in world_commit
            and "return Rollback();" in world_commit,
            "world-backed placement must roll back failed bootstrap and compatibility handoffs")

    interaction_world = interaction[interaction.index("void DriveViewportSelectionAndTransformWorldBacked("):]
    require("World.CurveCount() == 0u && SketchHasCommittedGeometry(Sketch)" in interaction_world,
            "world-backed selection may seed from a legacy sketch only while the world model is empty")
    require(interaction_world.count("MirrorSketchIntoWorldSketch(Sketch, World, Mapping)") == 2,
            "world interaction must limit sketch-to-world mirroring to bootstrap and explicit dimension compatibility")

    overlay = read("Engine/SlateWorkspace/Discipline/SketchViewportOverlay/Source/SketchViewportOverlay.cpp")
    require("const SpatialBasis& Basis" in overlay
            and "RecordViewportGridOverlay(Overlay, Extent, Basis, View, Perspective, Configuration)" in overlay,
            "compatibility overlays must expose an explicit world basis entry point")

    # 🔴 `DeviceOffering` and `InterfaceAttachment` were nine identical fields in two units, with a
    #    hand-written `Attach` copying each one across — and `Attach` itself was DEFINED TWICE, which
    #    only became visible once the types were unified. A field added to one side and forgotten on
    #    the other compiles perfectly and arrives as a null handle.
    device_attachment = read("Engine/Shared/DeviceAttachment.slang.h")
    require("using DeviceOffering = DeviceAttachment;" in device_attachment
            and "using InterfaceAttachment = DeviceAttachment;" in device_attachment,
            "the device handles must be one type; the two old names are aliases, not copies")
    for Twin in ("Engine/SlateVulkan/Device/HostLifecycle/Api/HostLifecycle.h",
                 "Engine/SlateUI/Interface/InterfaceExchange/Api/InterfaceExchange.h"):
        Body = read(Twin)
        require("struct DeviceOffering" not in Body and "struct InterfaceAttachment" not in Body,
                f"{Twin} must not redeclare the shared device attachment")
    require("InterfaceAttachment Attach(const DeviceOffering&" not in read(
                "Engine/SlateRuntime/Session/HostEnvironment/Api/HostEnvironment.h"),
            "Attach converted a type to itself once the twins merged; it is deleted")

    content_browser = read("Engine/SlateUI/Interface/ContentBrowserPanel/Source/ContentBrowserPanel.cpp")
    require("ActivationRequested = Library.Taken" in content_browser and "ActivationRequested = Index" in content_browser,
            "Content Browser card and Import button must request codex activation")
    require("ImportPressed" in content_browser and "Sampled.ContactReleased" in content_browser,
            "Content Browser import button must visibly press and activate on click release")

    tools = read("Engine/SlateUI/Interface/ParametricTools/Source/ParametricToolsPanel.cpp")
    require("static_cast<void>(Elapsed);" in tools, "ParametricToolsPanel must mark Elapsed as intentionally unused")
    require("Bezier" in tools and "Hermite" in tools and "NURBS Curve" in tools,
            "Sketch Draw must expose the full primary curve set")

    parametric_spec = read("Engine/SlateUI/Interface/ParametricWorkspace/Api/ParametricWorkspaceSpecification.h")
    parametric_panel = read("Engine/SlateUI/Interface/ParametricWorkspace/Source/ParametricWorkspacePanel.cpp")
    # 🔴 These three strings were asserted against `Engine/Application/Api/ParametricWorkspaceBridge.h`,
    #    which no longer exists: it was 347 `inline` lines in the APPLICATION layer that `SlateWorkspace`
    #    had to reach UP into, inverting the dependency arrow. The behaviour now lives in the unit that
    #    owns the transformation. The claim is about the DATA the inspector shows, so it survives the move
    #    unchanged — only its address changes.
    parametric_bridge = read("Engine/SlateWorkspace/Discipline/SketchDirectoryPresentation/Source/SketchDirectoryPresentation.cpp")
    require("ExtrusionCapToggleDemand" in parametric_spec and "Extrude Caps" in parametric_panel,
            "closed profile properties must expose a capped/wall extrusion toggle")
    require("Curve Closure" in parametric_bridge and "Extrude Result" in parametric_bridge and "Extrude Caps" in parametric_bridge,
            "closed profile inspector data must distinguish closed-loop rendering from capped solid extrusion")

    # 🔴 Pins the deletion: a unit must never again depend on the application layer above it. If either
    #    file returns, this refuses instead of silently re-inverting the graph — the failure `make sequence`
    #    reported as "the unit graph holds a cycle".
    require(not (ROOT / "Engine/Application/Api/ParametricWorkspaceBridge.h").exists(),
            "ParametricWorkspaceBridge.h is deleted; SlateWorkspace must not reach up into Application/")
    require(not (ROOT / "Engine/Application/Api/SharedViewportHostBridge.h").exists(),
            "SharedViewportHostBridge.h is deleted; its four concerns live in the units that own them")

    theme = read("Engine/SlateUI/Interface/ThemeInterchange/Source/ThemeInterchange.cpp")
    require("std::strncpy" not in theme, "ThemeInterchange must avoid MSVC strncpy warning")

    # 🔴 SHARED SHADER SOURCE MUST NOT USE C++ REFERENCE PARAMETERS. Slang on the Windows build rejects
    #    `const T& Name` in these shared/shader entry points, reading the parameter name as an undefined
    #    identifier and stopping the entire SlateVulkan phase before any C++ unit is reached. The near
    #    clipper is small POD-by-value arithmetic, so value parameters are the correct shared spelling.
    cad_near = read("Engine/Shared/WorkspaceCadNearClip.slang.h")
    cad_packet = read("Engine/Shared/WorkspaceCadPacket.slang.h")
    cad_vertex = read("Engine/SlateVulkan/Device/WorkspaceCadPass/Shader/WorkspaceCadVertex.slang")
    require("const WorkspaceCadProjectedPoint&" not in cad_near,
            "the shared CAD near clipper must not use C++ reference parameters Slang rejects")
    require("ProjectScreen(const WorkspaceCadProjectedPoint&" not in cad_vertex,
            "the CAD vertex shader helper must not use a C++ reference parameter")
    require("static_cast<" not in cad_near,
            "the shared CAD near clipper must not use C++ static_cast syntax Slang rejects")
    require("static_cast<" not in cad_packet,
            "the shared CAD packet must not use C++ static_cast syntax Slang rejects")

    print("[HostBuildBudgets] editor build budgets and warning fixes hold")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[HostBuildBudgets] failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
