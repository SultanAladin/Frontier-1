@echo off
REM RunUploadTransfer.bat  shell entry point forwarding to RunUploadTransfer.py.

setlocal
python "%~dp0RunUploadTransfer.py" %*
exit /b %ERRORLEVEL%
