@echo off
REM ===========================================================================
REM  ProbeSpawn prototype launcher
REM ---------------------------------------------------------------------------
REM  Both prototypes use ES modules + fetch(), which browsers BLOCK over
REM  file:// (every path is a unique opaque origin, so CORS refuses the module
REM  and the .obj). They must be served over http://. This script starts a
REM  throwaway static server on 8777 and opens both pages against it.
REM
REM  Close this window to stop the server.
REM ===========================================================================

cd /d "%~dp0"

set PORT=8777

echo Serving %CD% on http://localhost:%PORT%
echo.

REM Free the port if a previous run left a server holding it.
for /f "tokens=5" %%P in ('netstat -ano ^| findstr ":%PORT%" ^| findstr LISTENING') do (
    echo Reclaiming port %PORT% from PID %%P
    taskkill /F /PID %%P >nul 2>&1
)

where python >nul 2>&1
if errorlevel 1 (
    echo ERROR: python not found on PATH. Needed to serve the files.
    echo        Install Python or serve this folder with any static server.
    pause
    exit /b 1
)

REM Start the server in this window's background, give it a moment to bind.
start "ProbeSpawn server" /min cmd /c "python -m http.server %PORT%"
timeout /t 2 /nobreak >nul

echo Opening A - tile election  ^(current pipeline^)
start "" "http://localhost:%PORT%/ProbeSpawnA_TileElection.html"
timeout /t 1 /nobreak >nul

echo Opening B - micro raster   ^(surface-space^)
start "" "http://localhost:%PORT%/ProbeSpawnB_MicroRaster.html"

echo.
echo Both pages opened. Compare them side by side.
echo Press any key to STOP the server and exit.
pause >nul

for /f "tokens=5" %%P in ('netstat -ano ^| findstr ":%PORT%" ^| findstr LISTENING') do taskkill /F /PID %%P >nul 2>&1
echo Server stopped.