@echo off
rem Starts Fuyukaze as the host of a multiplayer game. Other players join with "Join Game.bat" and this PC's
rem IP address. Windows may ask to let the game through the firewall the first time: allow it on private networks.
set ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
echo Hosting Fuyukaze. Players on your network join with one of these addresses:
ipconfig | findstr /c:"IPv4"
echo.
if /i "%~1"=="windowed" (
    start "" "%ENGINE%" "%~dp0Fuyukaze.uproject" /Game/Fuyukaze/Maps/Fuyukaze?listen -game -windowed -ResX=1600 -ResY=900 -nosplash
) else (
    start "" "%ENGINE%" "%~dp0Fuyukaze.uproject" /Game/Fuyukaze/Maps/Fuyukaze?listen -game -fullscreen -nosplash
)
timeout /t 12 >nul
