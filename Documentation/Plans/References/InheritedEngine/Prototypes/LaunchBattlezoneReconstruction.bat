@echo off
REM ==================================================================================================
REM   LAUNCHBATTLEZONERECONSTRUCTION.BAT
REM   Serves the WebGPU reconstruction over a local origin and opens it in the default browser.
REM   A served origin is mandatory: ES modules and the .wgsl fetch are both blocked on file://.
REM ==================================================================================================

set SERVEPORT=8787
set SERVEROOT=%~dp0BattlezoneReconstruction
set PAGEURL=http://localhost:%SERVEPORT%/BattlezoneReconstruction.html

if not exist "%SERVEROOT%\BattlezoneReconstruction.html" (
    echo [error] Reconstruction folder not found next to this launcher.
    echo         Expected: %SERVEROOT%
    pause
    exit /b 1
)

where python >nul 2>nul
if errorlevel 1 (
    echo [error] python was not found on PATH; it is required to serve the page.
    pause
    exit /b 1
)

echo Serving %SERVEROOT%
echo Opening %PAGEURL%
echo Close this window to stop the server.
echo.

start "" "%PAGEURL%"
python -m http.server %SERVEPORT% --directory "%SERVEROOT%"
