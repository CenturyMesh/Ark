@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-ARK.ps1" -DesktopShortcut
if errorlevel 1 (
    echo ARK installation failed. See the error above.
    pause
    exit /b 1
)
echo ARK is ready. Open the ARK desktop shortcut.
pause
