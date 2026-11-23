@echo off
REM ============================================================================
REM  Build.bat - compile the Project Zero code image (ProjectZero.dll).
REM
REM  Mirrors the upstream MSVC batch idiom (SultanAladin Frontier, Windows by
REM  default): vswhere + vcvars64, cl /std:c++17. Needs the host SDK include
REM  root that carries ProjectInterchange.h:
REM      set FRONTIER_SDK=<path-to-upstream-checkout>\Engine\Interface
REM  (or wherever the host build placed the interchange header).
REM
REM  After linking, engine content this project actually uses (listed in
REM  ContentManifest.toml) is staged beside the executable - referenced
REM  content is copied only then, and only what is used.
REM ============================================================================
setlocal EnableDelayedExpansion

set "ROOT=%~dp0"
set "OUT=%ROOT%Build\Output"
if not exist "%OUT%" mkdir "%OUT%"

if not defined FRONTIER_SDK (
    echo FRONTIER_SDK is not set; point it at the host SDK root containing ProjectInterchange.h.
    exit /b 1
)
if not exist "%FRONTIER_SDK%\ProjectInterchange.h" (
    echo ProjectInterchange.h not found under %FRONTIER_SDK%.
    exit /b 1
)

REM ---- MSVC toolchain via vswhere (same discovery as the upstream batches) ---
set "PFX86=%ProgramFiles(x86)%"
set "VSWHERE=%PFX86%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
)
set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo MSVC x64 toolchain not found via vswhere.
    exit /b 1
)
call "%VCVARS%" >nul || exit /b 1

REM ---- Code image ------------------------------------------------------------
pushd "%OUT%"
cl /nologo /std:c++17 /EHsc /MD /utf-8 /O2 /LD /DPROJECTZERO_BUILD /I"%FRONTIER_SDK%" ^
   "%ROOT%Source\ProjectZeroInterchange.cpp" ^
   /link /OUT:"ProjectZero.dll" || (popd & exit /b 1)
popd

REM ---- Stage used engine content beside the executable -----------------------
set "BINARIES=%ROOT%..\..\Binaries"
set "ENGINE_CONTENT=%ROOT%..\..\..\EngineContent"
for /f "usebackq tokens=*" %%E in (`powershell -NoProfile -Command "$t = Get-Content -Raw '%ROOT%ContentManifest.toml'; [regex]::Matches($t, '\"([^\"]+)\"') | ForEach-Object { $_.Groups[1].Value }"`) do (
    echo Staging %%E
    robocopy "%ENGINE_CONTENT%\%%~pE" "%BINARIES%\EngineContent\%%~pE" "%%~nxE" /E /NFL /NDL /NJH /NJS >nul
    if !ERRORLEVEL! GEQ 8 (echo Staging failed for %%E & exit /b 1)
)

echo ProjectZero.dll built: %OUT%\ProjectZero.dll
exit /b 0
