@echo off
REM ============================================================================
REM  ProjectZero.bat - open Project Zero in Frontier.
REM
REM  This is the script every project folder carries: it finds the Frontier
REM  executable and hands it this project's descriptor on the command line,
REM  so the project opens directly (no project browser). Extra arguments are
REM  forwarded, e.g.  ProjectZero.bat --headless
REM
REM  Executable resolution order:
REM    1. %FRONTIER_EXE% when set
REM    2. ..\..\Binaries\Frontier.exe  (this checkout's build output)
REM    3. Frontier.exe on PATH
REM ============================================================================
setlocal

set "PROJECT=%~dp0ProjectZero.frontier"

if defined FRONTIER_EXE (
    set "EXE=%FRONTIER_EXE%"
) else if exist "%~dp0..\..\Binaries\Frontier.exe" (
    set "EXE=%~dp0..\..\Binaries\Frontier.exe"
) else (
    set "EXE=Frontier.exe"
)

"%EXE%" --project "%PROJECT%" %*
exit /b %ERRORLEVEL%
