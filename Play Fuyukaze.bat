@echo off
rem Double-click to play Fuyukaze in borderless fullscreen. Esc pauses; Enter then quits.
rem Add the word "windowed" after the file name to run in a 1600x900 window instead.
set ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
if not exist "%ENGINE%" (
    echo Unreal Engine 5.8 was not found at "%ENGINE%".
    pause
    exit /b 1
)
if /i "%~1"=="windowed" (
    start "" "%ENGINE%" "%~dp0Fuyukaze.uproject" -game -windowed -ResX=1600 -ResY=900 -nosplash
) else (
    start "" "%ENGINE%" "%~dp0Fuyukaze.uproject" -game -fullscreen -nosplash
)
