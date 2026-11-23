@echo off
REM ======================================================================
REM   LaunchRockFormation.bat - serve the prototype and open it
REM ======================================================================
REM
REM   The editor is split into ES modules, which the browser fetches under
REM   CORS rules. A file:// page is an opaque origin, so every import is
REM   blocked and the page hangs on "provisioning WebGPU" having run no
REM   code at all. It MUST be served over http.
REM

setlocal
set PORT=8777
set PAGE=RockFormation/RockFormationConstructionTree.html

REM -- serve from Prototypes/ so the page sits one folder down --
pushd "%~dp0.."

where python >nul 2>nul
if errorlevel 1 (
    echo [31mpython not found on PATH - cannot serve the prototype.[0m
    popd
    exit /b 1
)

echo Serving %CD% on http://127.0.0.1:%PORT%
echo Opening %PAGE%
echo.
echo Leave this window open while the prototype is in use.
echo Close it or press Ctrl+C to stop the server.
echo.

REM -- open the browser first; the server call below blocks --
start "" "http://127.0.0.1:%PORT%/%PAGE%"

python -m http.server %PORT% --bind 127.0.0.1

popd
endlocal
