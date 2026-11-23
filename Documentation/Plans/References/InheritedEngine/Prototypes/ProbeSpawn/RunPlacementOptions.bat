@echo off
REM ===========================================================================
REM  Placement-options explainer launcher
REM ---------------------------------------------------------------------------
REM  Unlike its ProbeSpawn siblings this page is SELF-CONTAINED: no ES modules,
REM  no fetch(), no .obj. It therefore opens straight off file:// with no static
REM  server, which is why this script is three lines and RunProbeSpawn.bat is not.
REM ===========================================================================
start "" "%~dp0PlacementOptionsExplained.html"
