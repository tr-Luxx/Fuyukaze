@echo off
rem Joins a Fuyukaze game that someone is hosting. Type the host's IP address when asked, or just press Enter
rem to join a game hosted on this same PC (for trying multiplayer with two windows).
set ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set HOSTIP=127.0.0.1
set /p HOSTIP=Host IP address (Enter for this PC):
if /i "%~1"=="windowed" (
    start "" "%ENGINE%" "%~dp0Fuyukaze.uproject" %HOSTIP% -game -windowed -ResX=1600 -ResY=900 -nosplash
) else (
    start "" "%ENGINE%" "%~dp0Fuyukaze.uproject" %HOSTIP% -game -fullscreen -nosplash
)
