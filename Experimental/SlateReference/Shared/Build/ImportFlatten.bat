@echo off
REM ImportFlatten.bat  the shell entry point. Forwards to Scripts\RunImportFlatten.py, which copies
REM every nested .cpp and .h of Import\ up to the Import top level, flat, without deleting anything.
REM
REM   Build\ImportFlatten.bat

setlocal

python "%~dp0..\Scripts\RunImportFlatten.py"

exit /b %ERRORLEVEL%
