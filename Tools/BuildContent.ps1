# Regenerates Fuyukaze's materials, blueprints and level by running Tools\build_content.py in a headless editor.
# The C++ module must be built first (.\Build.ps1), and the Fuyukaze editor must be closed.
$Engine = "C:\Program Files\Epic Games\UE_5.8"
$Project = Join-Path (Split-Path $PSScriptRoot -Parent) "Fuyukaze.uproject"
$Script = Join-Path $PSScriptRoot "build_content.py"
& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $Project "-ExecutePythonScript=$Script" -unattended -nosplash -nullrhi -stdout -FullStdOutLogOutput
exit $LASTEXITCODE
