@echo off
REM =====================================================================================
REM  Serve the repo over HTTP and open the WebGPU painting prototype.
REM
REM  The page fetches /EngineContent/... with absolute paths, so file:// cannot work --
REM  a static server rooted at the repo is required, not a convenience.
REM =====================================================================================

setlocal

REM This script lives four levels below the repo root, but the SERVER must still be rooted
REM at the repo -- the page requests /EngineContent/... and /Documentation/... as absolute
REM paths. Serving this folder instead would 404 every one of them and the model would
REM never load, so the root is walked back explicitly rather than taken from %~dp0.
REM   %~dp0 = ...\Documentation\Prototypes\PaintingSurface\  ->  ..\..\..
for %%R in ("%~dp0..\..\..") do set "RepoRoot=%%~fR"

set "ServePort=8000"
set "PagePath=/Documentation/Prototypes/PaintingSurface/PaintingSurface.html"

REM Fail loudly if the walk-back landed somewhere unexpected (someone moved the script).
REM Without this the server starts happily and the only symptom is a blank viewport.
if not exist "%RepoRoot%\EngineContent" (
    echo   Repo root looked wrong: "%RepoRoot%"
    echo   Expected an EngineContent folder there. Is this script still four levels deep?
    exit /b 1
)

REM Find an interpreter. The launcher is preferred; plain python is the fallback.
set "Interpreter="
where py >nul 2>&1 && set "Interpreter=py -3"
if not defined Interpreter where python >nul 2>&1 && set "Interpreter=python"

if not defined Interpreter (
    echo   Python was not found on PATH. Install it, or serve the repo some other way.
    exit /b 1
)

REM Refuse to start a second server on a port already in use -- otherwise this window
REM dies instantly with a bind error and the browser opens against whatever is there.
netstat -ano | findstr /r /c:"LISTENING" | findstr /c:":%ServePort% " >nul
if not errorlevel 1 (
    echo   Port %ServePort% is already serving. Opening the page against it.
    start "" "http://127.0.0.1:%ServePort%%PagePath%"
    exit /b 0
)

echo   Serving  %RepoRoot%
echo   Page     http://127.0.0.1:%ServePort%%PagePath%
echo.
echo   LMB paint  .  RMB orbit  .  MMB/shift pan
echo   Tab layers .  U atlas    .  E erase  .  Ctrl+Z undo  .  Ctrl+Y redo
echo.
echo   Close this window to stop the server.
echo.

REM Open the browser in a moment, then hand this window to the server. The delay lets the
REM socket bind first; opening immediately can race and land on a connection-refused page.
start "" /b cmd /c "timeout /t 2 /nobreak >nul & start "" "http://127.0.0.1:%ServePort%%PagePath%""

cd /d "%RepoRoot%"
%Interpreter% -m http.server %ServePort% --bind 127.0.0.1

endlocal
