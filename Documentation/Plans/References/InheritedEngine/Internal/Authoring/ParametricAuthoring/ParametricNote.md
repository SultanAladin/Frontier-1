1) need to add extrude + Transform Tools

2)Need Inset faces

Cannot execute — static review only
No shell or filesystem tool is attached to this session, so I cannot compile, run a Python checker, or launch anything. Everything below is a line-by-line read of the three files you pasted. The Build.bat is at the end, complete.

Findings
#	Sev	File	Issue
1	🔴	BoundarySweep.cpp	EnforceSweepDistance drifts on every symmetric re-drag — the "original base" is reconstructed from the new offset, which is circular
2	🔴	BoundarySweep.cpp	A negative Distance inverts the whole solid (caps + walls face inward) — and the extrude modal explicitly produces negative depths on a downward drag
3	🔴	BoundarySweep.cpp	EnforceSweepDistance only moves the outer ring — a holed profile tears on re-drag
4	🚩	BoundarySweep.cpp	DistanceEpsilon (1e-6) is looser than WeldTolerance (1e-4) → a 1e-5 mm sweep welds start ring into end ring, collapsing the body
5	🚩	BoundaryTessellation.cpp	Naive ear-clip + hole bridge — holed cap faces will mis-triangulate; PointInTriangle is boundary-inclusive, so the doubled seam vertices block valid ears
6	🚩	BoundarySweep.h	Header promises full rollback on failure; ValidationFault does not roll back (the .cpp comment even says the opposite)
7	🚩	BoundarySweep.cpp	ResolveRingMiterNormals wraps % Count — an open profile gets garbage miters at both ends when draft ≠ 0
8	✔️	BoundaryTopology.cpp	Missing #include <cstdarg> — confirmed, as flagged
9	✔️	BoundarySweep.cpp	Redundant first Base loop — confirmed; it computes Along - (Along - StartOffset) which is literally StartOffset, i.e. identical to the second loop
10	✔️	BoundarySweep.cpp	ResolvePlaneFrame's Right/Up are computed then (void)-discarded — dead
Items 8 and 9 are the two the previous assistant caught. Items 1–7 it did not.

The three blockers, in detail
1 · Symmetric drag drifts
EnforceSweepDistance recovers the base ring as Current − Axis·StartOffset, but StartOffset comes from the new distance while Current was placed with the old one.

Trace, +Z axis, profile at z = 0, symmetric:

Call	StartOffset	"Base" recovered	Start z	End z	Centre
build D=10	−5	0 (true)	−5	+5	0 ✔️
drag D=20	−10	+5	−5	+15	+5 🔴
drag D=30	−15	+10	−5	+25	+10 🔴
Height is right; the centring creeps by half the delta each frame — exactly the failure the comment above it claims to prevent. Non-symmetric is unaffected (StartOffset is always 0 there), which is why it will look fine until someone ticks Symmetric.

2 · Negative distance inverts the body
The start cap is built as the reversed ring (normal along −Axis) and the end cap as-authored (normal along +Axis). With Distance < 0 the end plane sits below the start plane, so both cap normals point into the solid, and the wall quad [A0, B0, B1, A1] reverses with them. The result is a fully inside-out prism. SketchModelExtrudeModal documents downward drags as producing a negative height, so this fires on ordinary use.

3 · Holes are abandoned on re-drag
StartRingVertices / EndRingVertices are the outer ring only. EnforceSweepDistance iterates exactly those, so hole-wall vertices keep the arm-time height while the outer wall moves — the cavity walls detach from the caps.

Combined fix
All three (plus #4 and #7) resolve with one structural change — store the raised rings, not just the outer, and normalise the sign at build time.

Copy// BoundarySweep.h — replace the two loose vertex runs
struct SweepRingRecord
{
    std::vector<BoundaryVector> BasePositions;   // [mm] - profile-plane source, never re-derived
    std::vector<VertexToken>    StartRing;
    std::vector<VertexToken>    EndRing;
    bool                        HoleRing = false;
};

struct SweepOutcome
{
    // …
    std::vector<SweepRingRecord> Rings;               // [-] - outer first, then each hole
    BoundaryVector               ResolvedAxis;        // [-] - sign-normalised sweep axis
    double                       ResolvedDistance = 0.0;  // [mm] - always positive
    // StartRingVertices / EndRingVertices stay as Rings[0]'s runs, for callers already using them
};
Copy// ExtrudeProfileIntoBrep — sign normalisation, right after the axis resolve
BoundaryVector Axis = NormalizeBoundaryVector(Specification.Direction, AxisResolved);
double         Distance = Specification.Distance;
if (Distance < 0.0) { Axis = ScaleBoundaryVector(Axis, -1.0); Distance = -Distance; }

// …and the degeneracy gate keyed off the weld radius, not a loose epsilon
if (Distance <= Body.WeldTolerance * 2.0) { /* ZeroDistance */ }
Copy// EnforceSweepDistance — every ring, from the stored base
bool EnforceSweepDistance(FullBrepBody& Body, const SweepOutcome& Outcome,
                          const SweepSpecification& Specification, double DistanceMm)
{
    if (Outcome.Rings.empty()) return false;

    BoundaryVector Axis = Outcome.ResolvedAxis;
    double         Distance = DistanceMm;
    if (Distance < 0.0) { Axis = ScaleBoundaryVector(Axis, -1.0); Distance = -Distance; }

    SweepSpecification Live = Specification;
    Live.Distance = Distance;
    double StartOffset = 0.0, EndOffset = 0.0;
    ResolveSweepOffsets(Live, StartOffset, EndOffset);
    const double DraftReach = Distance * std::tan(Live.DraftRadians);

    BoundaryVector Right, Up;
    ResolvePlaneFrame(Axis, Right, Up);

    for (const SweepRingRecord& Ring : Outcome.Rings)
    {
        const size_t Count = Ring.BasePositions.size();
        if (Count == 0 || Ring.StartRing.size() != Count || Ring.EndRing.size() != Count) continue;

        const std::vector<BoundaryVector> Normals =
            (std::fabs(DraftReach) > 1.0e-9) ? ResolveRingMiterNormals(Ring.BasePositions, Axis, Right, Up)
                                             : std::vector<BoundaryVector>(Count, BoundaryVector{});

        for (size_t Index = 0; Index < Count; ++Index)
        {
            const BoundaryVector Base = Ring.BasePositions[Index];   // 🔴 the stored original — never re-derived
            EnforceVertexPosition(Body, Ring.StartRing[Index],
                                  AddBoundaryVector(Base, ScaleBoundaryVector(Axis, StartOffset)));
            const BoundaryVector Drafted = AddBoundaryVector(Base, ScaleBoundaryVector(Normals[Index], DraftReach));
            EnforceVertexPosition(Body, Ring.EndRing[Index],
                                  AddBoundaryVector(Drafted, ScaleBoundaryVector(Axis, EndOffset)));
        }
    }
    return true;
}
Copy
For #7, pass a ClosedRing flag into ResolveRingMiterNormals and clamp the endpoint normals to their single adjacent span — the same construction SolveOpenCurveOffset already uses in ParametricSketchTransform.cpp.

For #5, you already vendor mapbox::earcut and %EARCUT% is already on the include path. AssembleBoundaryRenderStream should route through it exactly as AssembleParametricSketchSolidBodies does; the hand-rolled ClipEars / BridgeHole pair is the weakest code in the three files and it is solving a problem you have a proven solution for two directories away.

Full SketchModelViewport\Build.bat
Two changes: BREPROOT defined off the existing %APDIR%, added to INCLUDES, plus a cheap existence guard so a missing Boundary\ folder is reported rather than surfacing as a header-not-found.

Copy@echo off
REM ============================================================================
REM  SketchModelViewport\Build.bat - build SketchModelViewport.exe: a standalone
REM  Vulkan validation host for the sketch-model matcap viewport. Opens one native
REM  Vulkan window + ImGui interface, brings up the SvgIconRegistry against the
REM  host, registers both icon tiers, builds one caller-owned perspective viewport
REM  (grid + axis + the SpatialCompass overlay) and drives ConstructViewportPanel
REM  each frame until the window closes.
REM
REM  The viewport panel + theme + icons all live in EngineContext.lib; the Vulkan
REM  host + ImGui interface live in Graphics.lib; the window + surface + relay live
REM  in Platform.lib. So this app rebuilds the shared pillar libs first via their
REM  own Build.bat, then compiles ONLY its own units and links EngineContext +
REM  Graphics + Platform plus vulkan-1.lib + the OS libs. Phase 1 runs the GPU analytic
REM  ground grid, so it STAGES the two AnalyticGroundPlane .spv modules into
REM  %OUTDIR%\Shaders (the runtime resolves them via the relative "Shaders" dir). It
REM  writes its OWN exe under Binaries\Validation and never touches the others.
REM
REM  Run this through the PowerShell tool, never Bash.
REM ============================================================================
setlocal EnableDelayedExpansion EnableExtensions
set "APPDIR=%~dp0"
if "%APPDIR:~-1%"=="\" set "APPDIR=%APPDIR:~0,-1%"
for %%I in ("%APPDIR%\..\..\..") do set "ROOT=%%~fI"

set "NAME=SketchModelViewport"
set "OUTDIR=%ROOT%\Binaries\Validation"
set "LIBDIR=%ROOT%\Build\lib"
set "OBJ=%ROOT%\Build\obj\App_%NAME%"
set "OUTPUT=%OUTDIR%\%NAME%.exe"
set "DEFINE=FRONTIER_SKETCH_MODEL_VIEWPORT_VALIDATION"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJ%" mkdir "%OBJ%"

REM --- Activate MSVC if cl.exe is not already visible --------------------------
where cl >nul 2>nul
if errorlevel 1 (
    set "VCVARS="
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
    if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    if not exist "!VCVARS!" (
        echo [%NAME%] could not find vcvars64.bat - edit the VCVARS fallback.
        endlocal ^& exit /b 1
    )
    call "!VCVARS!" >nul
)

REM --- Ensure the shared pillar libs exist + are current ----------------------
REM  Platform (window + surface + relay), Graphics (Vulkan host + ImGui interface),
REM  EngineContext (viewport panel/theme/icons + vendored ImGui core).
call "%ROOT%\Internal\Platform\Build.bat"
if errorlevel 1 goto :fail
call "%ROOT%\Internal\Graphics\Build.bat"
if errorlevel 1 goto :fail
call "%ROOT%\Internal\EngineContext\Build.bat"
if errorlevel 1 goto :fail
REM  AuthoringParametric (the standalone Workplane construction-plane model the viewport overlay draws,
REM  PLUS the FullBREP boundary structure under Boundary\ that the extrude sweeps into). That Build.bat
REM  compiles by /R recursion, so the three Boundary units land in the lib with no edit there.
call "%ROOT%\Internal\Authoring\ParametricAuthoring\Build.bat"
if errorlevel 1 goto :fail
if not exist "%LIBDIR%\AuthoringParametric.lib" (
    echo [%NAME%] AuthoringParametric.lib missing after build - aborting.
    goto :fail
)
if not exist "%LIBDIR%\EngineContext.lib" (
    echo [%NAME%] EngineContext.lib missing after build - aborting.
    goto :fail
)
if not exist "%LIBDIR%\Graphics.lib" (
    echo [%NAME%] Graphics.lib missing after build - aborting.
    goto :fail
)
if not exist "%LIBDIR%\Platform.lib" (
    echo [%NAME%] Platform.lib missing after build - aborting.
    goto :fail
)

REM --- Include roots (pillar-rooted headers + ImGui + Vulkan; no GLFW) ---------
set "IMGUI=%ROOT%\ExternalPackages\imgui"
set "VULKAN=%VULKAN_SDK%"
if not defined VULKAN set "VULKAN=C:\VulkanSDK\1.4.335.0"
REM  The two embedded validation surfaces (SceneDirectoryInspector card on Tab, ConstructionCatalogue
REM  console on right-click) are compiled IN PLACE from the sibling app folders, so their headers must be
REM  on the include path. Their own Host.cpp files carry main() and are skipped in the compile loop below.
set "SDIDIR=%APPDIR%\..\SceneDirectoryInspectorValidation"
set "CCDIR=%APPDIR%\..\ConstructionCatalogueValidation"
REM  The standalone Workplane construction-plane model the viewport overlay draws (AuthoringParametric.lib).
set "APDIR=%ROOT%\Internal\Authoring\ParametricAuthoring"
REM  The FullBREP boundary structure the extrude builds into. BoundaryTopology.h / BoundarySweep.h /
REM  BoundaryTessellation.h include each other UNROOTED ("BoundaryTopology.h"), so the folder must be an
REM  include root of its own - exactly the way MATHROOT / GEOMROOT are handled below.
set "BREPROOT=%APDIR%\Boundary"
if not exist "%BREPROOT%\BoundaryTopology.h" (
    echo [%NAME%] WARNING: %BREPROOT%\BoundaryTopology.h not found - the BREP extrude path will not compile.
)
REM  mapbox::earcut - 2D triangulation with holes, the fill path for a boolean-result Profile (header-only).
set "EARCUT=%ROOT%\ExternalPackages\earcut"
REM  The matcap solid pass (ParametricSketchSolidSequence.h) reaches PolygonCluster.h, which includes its neighbours UNROOTED
REM  ("LinearAlgebra_Float64.h"), so those two folders must be include roots exactly as Internal\Graphics\Build.bat sets them.
set "MATHROOT=%ROOT%\Internal\EngineContext\Math"
set "GEOMROOT=%ROOT%\Internal\Authoring\Geometry\Modeling"
set "INCLUDES=/I"%ROOT%\Internal" /I"%IMGUI%" /I"%IMGUI%\backends" /I"%VULKAN%\Include" /I"%SDIDIR%" /I"%CCDIR%" /I"%APDIR%" /I"%BREPROOT%" /I"%EARCUT%" /I"%MATHROOT%" /I"%GEOMROOT%""
REM  FRONTIER_DEVELOPMENT_PROFILE keeps Trace/Notice diagnostics AND turns on the
REM  Vulkan validation layer by default. Swap to FRONTIER_SHIPPING_PROFILE for lean builds.
set "DEFINES=/DUNICODE /D_UNICODE /D%DEFINE% /DFRONTIER_DEVELOPMENT_PROFILE"
set "CXXFLAGS=/nologo /c /std:c++17 /EHsc /MD /utf-8 /Zi /FS /O2 /W3 /wd4244 /wd4267"

REM --- Compile this app's own units --------------------------------------------
set "OBJRSP=%OBJ%\link_objs.rsp"
if exist "%OBJRSP%" del /Q "%OBJRSP%"
for /R "%APPDIR%" %%F in (*.cpp) do (
    set "UNIT=%%~nF"
    echo [compile] !UNIT!
    cl %CXXFLAGS% %DEFINES% %INCLUDES% "%%F" /Fo"%OBJ%\!UNIT!.obj" /Fd"%OBJ%\%NAME%.pdb"
    if errorlevel 1 (
        echo [%NAME%] COMPILE FAILED
        goto :fail
    )
    echo "%OBJ%\!UNIT!.obj">>"%OBJRSP%"
)

REM --- Compile the two embedded surfaces' units, in place from the sibling folders --------------
REM  Every .cpp EXCEPT each folder's own *Host.cpp (those carry main()). The data model, glyph pack,
REM  panel, console bridge, content profile, and generated catalogue table all link into this exe.
for /R "%SDIDIR%" %%F in (*.cpp) do (
    set "UNIT=%%~nF"
    echo !UNIT! | findstr /I /C:"ValidationHost" >nul
    if errorlevel 1 (
        echo [compile] !UNIT!
        cl %CXXFLAGS% %DEFINES% %INCLUDES% "%%F" /Fo"%OBJ%\!UNIT!.obj" /Fd"%OBJ%\%NAME%.pdb"
        if errorlevel 1 (
            echo [%NAME%] COMPILE FAILED
            goto :fail
        )
        echo "%OBJ%\!UNIT!.obj">>"%OBJRSP%"
    )
)
for /R "%CCDIR%" %%F in (*.cpp) do (
    set "UNIT=%%~nF"
    echo !UNIT! | findstr /I /C:"ValidationHost" >nul
    if errorlevel 1 (
        echo [compile] !UNIT!
        cl %CXXFLAGS% %DEFINES% %INCLUDES% "%%F" /Fo"%OBJ%\!UNIT!.obj" /Fd"%OBJ%\%NAME%.pdb"
        if errorlevel 1 (
            echo [%NAME%] COMPILE FAILED
            goto :fail
        )
        echo "%OBJ%\!UNIT!.obj">>"%OBJRSP%"
    )
)

REM --- Link the shared libs + thorvg + Vulkan / system libs (no GLFW) ----------
REM  thorvg.lib is the vendored static SVG rasterizer SvgRasterizer.obj (inside
REM  EngineContext.lib) calls into for the icon glyphs.
set "THORVGLIB=%ROOT%\ExternalPackages\thorvg\lib\thorvg.lib"
set "LINKLIBS="%LIBDIR%\EngineContext.lib" "%LIBDIR%\Graphics.lib" "%LIBDIR%\Platform.lib" "%LIBDIR%\AuthoringParametric.lib" "%THORVGLIB%""
set "SYSLIBS="%VULKAN%\Lib\vulkan-1.lib" user32.lib gdi32.lib shell32.lib dwmapi.lib"

echo [%NAME%] linking -^> %OUTPUT%
link /nologo /DEBUG /SUBSYSTEM:CONSOLE @"%OBJRSP%" %LINKLIBS% %SYSLIBS% /OUT:"%OUTPUT%"
if errorlevel 1 (
    echo [%NAME%] LINK FAILED
    goto :fail
)
REM --- Stage the analytic-grid SPIR-V next to the exe (runtime resolves relative "Shaders") ---
set "SHADERSRC=%ROOT%\Internal\Graphics\Grid\Shaders"
set "SHADEROUT=%OUTDIR%\Shaders"
if not exist "%SHADEROUT%" mkdir "%SHADEROUT%"
copy /Y "%SHADERSRC%\AnalyticGroundPlane.vert.spv" "%SHADEROUT%" >nul
copy /Y "%SHADERSRC%\AnalyticGroundPlane.frag.spv" "%SHADEROUT%" >nul
if not exist "%SHADEROUT%\AnalyticGroundPlane.vert.spv" (
    echo [%NAME%] WARNING: grid vertex SPIR-V not staged - the grid pass will be unavailable at runtime.
)
if not exist "%SHADEROUT%\AnalyticGroundPlane.frag.spv" (
    echo [%NAME%] WARNING: grid fragment SPIR-V not staged - the grid pass will be unavailable at runtime.
)

REM --- Stage the matcap SPIR-V + the chrome matcap disc for the EXTRUDE solid pass ------------
REM  The extruded prisms / thin walls render through ParametricSketchSolidSequence, which loads these two
REM  modules from the relative "Shaders" dir and the matcap PNG from a path under the exe's EngineContent.
REM  A missing PNG or module disables the whole pass (the app falls back to the pure-2D sketch), so both
REM  are staged here and a miss is reported loudly rather than showing up as "extrude renders nothing".
set "SURFACESHADERSRC=%ROOT%\Internal\Graphics\Render\Surface\Shaders"
copy /Y "%SURFACESHADERSRC%\ParametricSketchMatcap.vert.spv" "%SHADEROUT%" >nul
copy /Y "%SURFACESHADERSRC%\ParametricSketchMatcap.frag.spv" "%SHADEROUT%" >nul
if not exist "%SHADEROUT%\ParametricSketchMatcap.vert.spv" (
    echo [%NAME%] WARNING: matcap vertex SPIR-V not staged - extruded bodies will not render.
)
if not exist "%SHADEROUT%\ParametricSketchMatcap.frag.spv" (
    echo [%NAME%] WARNING: matcap fragment SPIR-V not staged - extruded bodies will not render.
)
robocopy "%ROOT%\EngineContent\ReferenceMaterials\Matcaps" "%OUTDIR%\EngineContent\ReferenceMaterials\Matcaps" Matcap-Chrome.png /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 goto :fail
if not exist "%OUTDIR%\EngineContent\ReferenceMaterials\Matcaps\Matcap-Chrome.png" (
    echo [%NAME%] WARNING: chrome matcap not staged - extruded bodies will not render.
)

REM --- Stage the UI-scale config next to the exe so the theme resolver finds it ---------------
robocopy "%ROOT%\EngineContent\Config" "%OUTDIR%\EngineContent\Config" InterfaceScale.config /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 goto :fail

echo [%NAME%] OK -^> %OUTPUT%
endlocal & exit /b 0

:fail
echo [%NAME%] BUILD FAILED
endlocal & exit /b 1
Copy
One thing I could not verify: whether Internal\Authoring\ParametricAuthoring\Build.bat really does sweep with for /R. You did not paste it. If its loop is for %%F in (*.cpp) rather than for /R, the three Boundary units will not be compiled into AuthoringParametric.lib and you will get unresolved externals at link, not a header error.

On the extrude integration
Redirecting SketchModelExtrudeModal to drive a SweepOutcome is the right next step, but I would fix items 1–3 first. As it stands, wiring the modal to EnforceSweepDistance would surface the symmetric drift immediately — the modal reads SymmetricEnabled straight off the console's Direction row, so the very first symmetric extrude would creep.

Say the word and I will write the corrected BoundarySweep.{h,cpp} in full, plus the earcut swap in BoundaryTessellation.cpp. I can also give you the Python static-check script you asked for — an AST-free scan for the specific patterns here (missing <cstdarg>, arena pointers held across an ArenaAttach, ring walks lacking a step budget) — but it will be text in chat unless you want it saved to _ClaudeScratch/tmp/.