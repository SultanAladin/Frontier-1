@echo off
REM RunSymbolIndex.bat  shell entry point forwarding to RunSymbolIndex.py.

setlocal
python "%~dp0RunSymbolIndex.py" %*
exit /b %ERRORLEVEL%
