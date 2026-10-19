@echo off
setlocal EnableExtensions
chcp 65001 >nul

rem 生成面向玩家的单文件中文安装/启动器。
rem 只需要 Windows 自带的 PowerShell 和 .NET Framework 4 编译器。
set "ROOT=%~dp0"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
    echo [错误] 找不到 .NET Framework 4 C# 编译器，请在“启用或关闭 Windows 功能”中启用 .NET Framework 4.8。
    pause
    exit /b 1
)

echo [1/2] 正在整理并压缩玩家版运行环境……
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\setup\BuildPortable.ps1" -Root "%ROOT%."
if errorlevel 1 (
    echo [错误] 整理运行环境失败。
    pause
    exit /b 1
)

if not exist "%ROOT%dist" mkdir "%ROOT%dist"
echo [2/2] 正在生成单文件 EXE……
"%CSC%" /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 ^
    /out:"%ROOT%dist\Bloodborne-PC-中文启动器.exe" ^
    /r:System.Windows.Forms.dll /r:System.Drawing.dll ^
    /r:System.Security.dll /r:System.Web.Extensions.dll ^
    /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll ^
    /resource:"%ROOT%packaging\windows-portable-payload.zip",bbport_payload.zip ^
    /resource:"%ROOT%scripts\bbport_settings_table.inc",bbport_settings_table.inc ^
    /resource:"%ROOT%tools\setup\assets\launcher-background.png",bbport_launcher_background.png ^
    "%ROOT%tools\setup\BbportPortable.cs"
if errorlevel 1 (
    echo [错误] 编译启动器失败。
    pause
    exit /b 1
)

echo.
echo 已生成：%ROOT%dist\Bloodborne-PC-中文启动器.exe
if /I "%~1"=="--build-only" exit /b 0
start "" "%ROOT%dist\Bloodborne-PC-中文启动器.exe"
