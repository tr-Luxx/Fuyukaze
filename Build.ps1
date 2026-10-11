# Builds the Fuyukaze editor target. Close the Fuyukaze editor first (its module DLL is locked while open).
# Usage: .\Build.ps1 [-Target FuyukazeEditor] [-Config Development] [-IgnoreLiveCoding]
param(
    [string]$Target = "FuyukazeEditor",
    [string]$Config = "Development",
    # Another Unreal Editor from the same engine with Live Coding on blocks command-line builds,
    # even when it has a different project open. Pass this switch to build anyway.
    [switch]$IgnoreLiveCoding
)
$Engine = "C:\Program Files\Epic Games\UE_5.8"
# UnrealBuildTool refuses to set up editor targets without the .NET Framework SDK. If it is not
# installed, point UBT's AutoSDK lookup at the placeholder in Build\AutoSDK (see mscoree.h there).
if (-not (Test-Path "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\NETFXSDK")) {
    $env:UE_SDKS_ROOT = Join-Path $PSScriptRoot "Build\AutoSDK"
}
$Extra = @()
if ($IgnoreLiveCoding) { $Extra += "-NoHotReloadFromIDE" }
& "$Engine\Engine\Build\BatchFiles\Build.bat" $Target Win64 $Config "-Project=$PSScriptRoot\Fuyukaze.uproject" -WaitMutex -NoHotReload @Extra
exit $LASTEXITCODE
