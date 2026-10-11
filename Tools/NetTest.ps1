# Multiplayer smoke test on one PC: starts a host and a joining player without windows, lets bots play both,
# then prints what each side logged. The host's bot fights enemies and accepts duels; the joiner's bot follows
# the same input paths a person would, challenges the host to a duel, and saves Saved\Tour\client_view.png.
param(
    [int]$Seconds = 110
)
$Engine = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Root = Split-Path $PSScriptRoot -Parent
$Out = Join-Path $Root "Saved\Tour"
New-Item -ItemType Directory -Force $Out | Out-Null
$Common = @("-game", "-windowed", "-ResX=1280", "-ResY=720", "-ForceRes", "-RenderOffscreen", "-unattended", "-nosplash", "-nosound")

$HostArgs = @("`"$Root\Fuyukaze.uproject`"", "/Game/Fuyukaze/Maps/Fuyukaze?listen") + $Common + @("-FuyuBot", "`"-FuyuTour=$Out`"", "-FuyuBotTime=$($Seconds + 40)", "`"-abslog=$Root\Saved\Logs\Host.log`"")
$HostProcess = Start-Process -FilePath $Engine -ArgumentList $HostArgs -PassThru

# give the host time to load the island before the second player knocks
Start-Sleep -Seconds 45

$ClientArgs = @("`"$Root\Fuyukaze.uproject`"", "127.0.0.1") + $Common + @("-FuyuClientBot", "-FuyuClientBotTime=$Seconds", "`"-abslog=$Root\Saved\Logs\Client.log`"")
$ClientProcess = Start-Process -FilePath $Engine -ArgumentList $ClientArgs -PassThru

$ClientProcess.WaitForExit(($Seconds + 240) * 1000) | Out-Null
if (-not $ClientProcess.HasExited) { $ClientProcess.Kill() }
Start-Sleep -Seconds 3
if (-not $HostProcess.HasExited) { $HostProcess.Kill() }
