# Launches Fuyukaze as a standalone game using the editor executable (no packaging needed).
# Usage: .\Play.ps1 [-Windowed] [-Width 1920] [-Height 1080]
param(
    [switch]$Windowed,
    [int]$Width = 1920,
    [int]$Height = 1080
)
$Engine = "C:\Program Files\Epic Games\UE_5.8"
$Mode = if ($Windowed) { "-windowed" } else { "-fullscreen" }
& "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" "$PSScriptRoot\Fuyukaze.uproject" -game $Mode "-ResX=$Width" "-ResY=$Height" -nosplash
