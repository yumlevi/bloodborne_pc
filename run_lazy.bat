@echo off
rem Compatibility entry point for the first-generation portable layout.
rem Keep all behavior in start_lazy.bat and preserve paths containing spaces.
call "%~dp0start_lazy.bat" %*
exit /b %ERRORLEVEL%
