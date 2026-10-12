# Runs the game without a window, flies the tour camera round the island saving a screenshot at each stop
# to Saved\Tour, then quits. Add -Bot to have the samurai fight on her own and log how the fights go.
param(
    [switch]$Bot,
    # with -Bot, how many extra seconds the samurai keeps playing after the fixed shots
    [int]$BotTime = 0,
    # with -Bot, fill the resolve meter the first time three enemies are close, so the ultimate is sure to be seen
    [switch]$Ultimate,
    # hour of the day to hold the clock at, e.g. 22.5 for night. Leave out to let the day run from mid afternoon
    [double]$Time = -1,
    # enemies stand idle instead of attacking, so shots of the samurai are not interrupted by a fight
    [switch]$Peace,
    [int]$Width = 1600,
    [int]$Height = 900
)
$Engine = "C:\Program Files\Epic Games\UE_5.8"
$Root = Split-Path $PSScriptRoot -Parent
$Out = Join-Path $Root "Saved\Tour"
New-Item -ItemType Directory -Force $Out | Out-Null
$Extra = @()
if ($Time -ge 0) { $Extra += "-FuyuTime=$Time"; $Extra += "-FuyuTimeStop" }
if ($Peace) { $Extra += "-FuyuPeace" }
if ($Bot) { $Extra += "-FuyuBot"; if ($BotTime -gt 0) { $Extra += "-FuyuBotTime=$BotTime" }; if ($Ultimate) { $Extra += "-FuyuBotUlt" } }
& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Root\Fuyukaze.uproject" -game -windowed "-ResX=$Width" "-ResY=$Height" -ForceRes -RenderOffscreen -unattended -nosplash -nosound "-FuyuTour=$Out" "-abslog=$Root\Saved\Logs\Tour.log" @Extra
exit $LASTEXITCODE
