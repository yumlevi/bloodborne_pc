@echo off
setlocal EnableExtensions

rem ============================================================
rem Bloodborne PC - Portable / Prebuilt Launcher
rem
rem Directory layout:
rem
rem   run_lazy.bat
rem   Games\
rem       eboot.bin
rem       sce_sys\
rem       sce_module\
rem       dvdroot_ps4\
rem   out\
rem       bb-probe.exe
rem       bb-gpu-capabilities.exe
rem   python\
rem       python.exe
rem   runtime\
rem       SDL3.dll
rem       libc++.dll
rem       ...
rem   scripts\
rem       run_game.py
rem       ...
rem
rem No compilation.
rem No MSYS2 / Git / CMake / Ninja required for the end user.
rem ============================================================


rem ------------------------------------------------------------
rem Root directory = directory containing this BAT file.
rem %~dp0 already ends with a backslash.
rem ------------------------------------------------------------
set "ROOT=%~dp0"

cd /d "%ROOT%"


rem ------------------------------------------------------------
rem Force UTF-8 for Python.
rem Prevents errors such as:
rem 'gbk' codec can't encode character ...
rem ------------------------------------------------------------
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"


rem ------------------------------------------------------------
rem Use prebuilt executables.
rem Prevent run_game.py from calling build.sh.
rem ------------------------------------------------------------
set "BB_PREBUILT=1"


rem ------------------------------------------------------------
rem Data directory.
rem out / user / mods / patches / config are based here.
rem ------------------------------------------------------------
set "BB_DATA_DIR=%ROOT%"


rem ------------------------------------------------------------
rem Game directory:
rem
rem   <launcher directory>\Games
rem
rem Example:
rem
rem   D:\BloodbornePC\run_lazy.bat
rem   D:\BloodbornePC\Games\eboot.bin
rem
rem ------------------------------------------------------------
set "BB_GAME_DIR=%ROOT%Games"


rem ------------------------------------------------------------
rem Portable Python.
rem ------------------------------------------------------------
set "BB_PYTHON=%ROOT%python\python.exe"


rem ------------------------------------------------------------
rem Portable runtime DLL directory.
rem ------------------------------------------------------------
set "BB_RUNTIME=%ROOT%runtime"

rem Put packaged DLLs first in PATH.
set "PATH=%BB_RUNTIME%;%PATH%"


rem ============================================================
rem CHECK: Game directory
rem ============================================================

if not exist "%BB_GAME_DIR%\" (
    echo.
    echo ============================================================
    echo ERROR: Game directory not found.
    echo.
    echo Expected:
    echo   %BB_GAME_DIR%
    echo ============================================================
    echo.
    pause
    exit /b 1
)


rem ============================================================
rem CHECK: eboot.bin
rem ============================================================

if not exist "%BB_GAME_DIR%\eboot.bin" (
    echo.
    echo ============================================================
    echo ERROR: eboot.bin not found.
    echo.
    echo Expected:
    echo   %BB_GAME_DIR%\eboot.bin
    echo.
    echo Games must be the decrypted Bloodborne game root.
    echo ============================================================
    echo.
    pause
    exit /b 1
)


rem ============================================================
rem CHECK: param.sfo
rem ============================================================

if not exist "%BB_GAME_DIR%\sce_sys\param.sfo" (
    echo.
    echo ============================================================
    echo ERROR: sce_sys\param.sfo not found.
    echo.
    echo Expected:
    echo   %BB_GAME_DIR%\sce_sys\param.sfo
    echo ============================================================
    echo.
    pause
    exit /b 1
)


rem ============================================================
rem CHECK: Portable Python
rem ============================================================

if not exist "%BB_PYTHON%" (
    echo.
    echo ============================================================
    echo ERROR: Portable Python not found.
    echo.
    echo Expected:
    echo   %BB_PYTHON%
    echo ============================================================
    echo.
    pause
    exit /b 1
)


rem ============================================================
rem CHECK: bb-probe.exe
rem ============================================================

if not exist "%ROOT%out\bb-probe.exe" (
    echo.
    echo ============================================================
    echo ERROR: bb-probe.exe not found.
    echo.
    echo Expected:
    echo   %ROOT%out\bb-probe.exe
    echo ============================================================
    echo.
    pause
    exit /b 1
)


rem ============================================================
rem CHECK: bb-gpu-capabilities.exe
rem ============================================================

if not exist "%ROOT%out\bb-gpu-capabilities.exe" (
    echo.
    echo ============================================================
    echo ERROR: bb-gpu-capabilities.exe not found.
    echo.
    echo Expected:
    echo   %ROOT%out\bb-gpu-capabilities.exe
    echo ============================================================
    echo.
    pause
    exit /b 1
)


rem ============================================================
rem CHECK: run_game.py
rem ============================================================

if not exist "%ROOT%scripts\run_game.py" (
    echo.
    echo ============================================================
    echo ERROR: scripts\run_game.py not found.
    echo.
    echo Expected:
    echo   %ROOT%scripts\run_game.py
    echo ============================================================
    echo.
    pause
    exit /b 1
)


rem ============================================================
rem CHECK: Runtime directory
rem ============================================================

if not exist "%BB_RUNTIME%\" (
    echo.
    echo ============================================================
    echo ERROR: Runtime directory not found.
    echo.
    echo Expected:
    echo   %BB_RUNTIME%
    echo ============================================================
    echo.
    pause
    exit /b 1
)


rem ============================================================
rem CHECK: Important runtime DLLs
rem ============================================================

if not exist "%BB_RUNTIME%\SDL3.dll" (
    echo.
    echo ============================================================
    echo ERROR: SDL3.dll not found.
    echo.
    echo Expected:
    echo   %BB_RUNTIME%\SDL3.dll
    echo ============================================================
    echo.
    pause
    exit /b 1
)

if not exist "%BB_RUNTIME%\libc++.dll" (
    echo.
    echo ============================================================
    echo ERROR: libc++.dll not found.
    echo.
    echo Expected:
    echo   %BB_RUNTIME%\libc++.dll
    echo ============================================================
    echo.
    pause
    exit /b 1
)

if not exist "%BB_RUNTIME%\libxxhash.dll" (
    echo.
    echo ============================================================
    echo ERROR: libxxhash.dll not found.
    echo.
    echo Expected:
    echo   %BB_RUNTIME%\libxxhash.dll
    echo ============================================================
    echo.
    pause
    exit /b 1
)

if not exist "%BB_RUNTIME%\libZydis.dll" (
    echo.
    echo ============================================================
    echo ERROR: libZydis.dll not found.
    echo.
    echo Expected:
    echo   %BB_RUNTIME%\libZydis.dll
    echo ============================================================
    echo.
    pause
    exit /b 1
)


rem ============================================================
rem START
rem ============================================================

echo.
echo ============================================================
echo Bloodborne PC - Portable Launcher
echo ============================================================
echo.
echo Root:
echo   %ROOT%
echo.
echo Game:
echo   %BB_GAME_DIR%
echo.
echo Python:
echo   %BB_PYTHON%
echo.
echo Runtime:
echo   %BB_RUNTIME%
echo.
echo Build:
echo   PREBUILT
echo.
echo Starting Bloodborne...
echo.


rem ------------------------------------------------------------
rem The game path is supplied automatically.
rem Any arguments given to run_lazy.bat are forwarded afterwards.
rem ------------------------------------------------------------

"%BB_PYTHON%" "%ROOT%scripts\run_game.py" --game-dir "%BB_GAME_DIR%" %*

set "EXITCODE=%ERRORLEVEL%"


rem ============================================================
rem EXIT
rem ============================================================

echo.
echo ============================================================
echo Bloodborne PC exited with code %EXITCODE%
echo ============================================================
echo.


rem Normal exit: close automatically.
if "%EXITCODE%"=="0" (
    exit /b 0
)


rem Error exit: keep the console visible.
pause

exit /b %EXITCODE%
