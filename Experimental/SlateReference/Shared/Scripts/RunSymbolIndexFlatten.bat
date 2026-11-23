@echo off
REM RunSymbolIndexFlatten.bat  shell entry point forwarding to RunSymbolIndexFlatten.py.

setlocal
python "%~dp0RunSymbolIndexFlatten.py" %*
exit /b %ERRORLEVEL%
