@echo off
REM RunImportFlatten.bat  shell entry point forwarding to RunImportFlatten.py.

setlocal
python "%~dp0RunImportFlatten.py" %*
exit /b %ERRORLEVEL%
