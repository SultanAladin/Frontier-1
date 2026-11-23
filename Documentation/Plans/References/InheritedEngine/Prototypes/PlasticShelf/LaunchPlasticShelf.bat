@echo off
REM ============================================================================
REM  LaunchPlasticShelf.bat - open the WebGPU plastic-shelf prototype.
REM  Chrome/Edge need a real GPU adapter for WebGPU; a plain double-click on the
REM  .html works too, but this picks a browser known to support it.
REM ============================================================================

setlocal
set "PAGE=%~dp0PlasticShelfDrawer.html"

if not exist "%PAGE%" (
    echo [x] PlasticShelfDrawer.html not found next to this script.
    pause
    exit /b 1
)

set "CHROME=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if not exist "%CHROME%" set "CHROME=%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe"
set "EDGE=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
if not exist "%EDGE%" set "EDGE=%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"

if exist "%CHROME%" (
    echo [^>] Chrome  %PAGE%
    start "" "%CHROME%" --new-window "file:///%PAGE:\=/%"
    exit /b 0
)

if exist "%EDGE%" (
    echo [^>] Edge  %PAGE%
    start "" "%EDGE%" --new-window "file:///%PAGE:\=/%"
    exit /b 0
)

echo [!] Chrome/Edge not found - opening with the default browser.
echo     WebGPU needs Chrome or Edge 113+.
start "" "%PAGE%"
exit /b 0
