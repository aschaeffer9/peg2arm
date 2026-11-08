# peg2arm - updating an Android build you already made, without losing your data.
#
# This is build_android.ps1 with the checks an update needs, for a phone that already has the game installed:
#   * the APK is signed with the key your first build made (out\android-debug.keystore), so installing it over the
#     installed app keeps the 2.7 GB of game data and your save. A different key cannot be installed over it: Android
#     refuses (INSTALL_FAILED_UPDATE_INCOMPATIBLE) and the only way in is an uninstall, which deletes the data.
#   * the PC build in out\pc must still be there (the Android build reuses its shader cache and generated code).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\android\update_android.ps1
#   powershell -ExecutionPolicy Bypass -File tools\android\update_android.ps1 -Ndk C:\android\android-ndk-r28c -Device <serial>
#
# Then open Downloads/peggle2.apk on the phone and tap Install. Do NOT uninstall the old app first.
param(
    [string]$Ndk = $env:ANDROID_NDK_HOME,
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$Python = '',
    [string]$CMakeDir = '',
    [string]$Device = '',
    [string]$Keystore = ''
)

$ErrorActionPreference = 'Stop'
$Src = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $Src
if (-not $Keystore) { $Keystore = Join-Path $Src 'out\android-debug.keystore' }

Write-Host ''
Write-Host 'peg2arm - Android update' -ForegroundColor Cyan
Write-Host ''

# The APK's signature is what keeps the data. An update built with a new key cannot be installed over the old app.
if (-not (Test-Path $Keystore)) {
    Write-Host "WARNING: no signing key at $Keystore" -ForegroundColor Yellow
    Write-Host '  This build will make a new one, so it CANNOT be installed over an existing install: Android'
    Write-Host '  rejects it and an uninstall deletes the game data (2.7 GB) and the save. If the games are already'
    Write-Host '  on the phone, find the key your first build used (it is out\android-debug.keystore in the tree you'
    Write-Host '  built with) and pass it with -Keystore.'
    Write-Host ''
}

if (-not (Test-Path 'out\pc\renut_gpu\shader_cache.cpp')) {
    Write-Host 'ERROR: no PC build in out\pc.' -ForegroundColor Red
    Write-Host '  The Android build reuses it (shader cache, generated code). Build the PC build first (docs/building.md),'
    Write-Host '  or use the tree you built the installed APK with.'
    exit 1
}

$forward = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'build_android.ps1'))
foreach ($pair in @(@('Ndk', $Ndk), @('Sdk', $Sdk), @('Python', $Python), @('CMakeDir', $CMakeDir),
                    @('Device', $Device), @('Keystore', $Keystore))) {
    if ($pair[1]) { $forward += @("-$($pair[0])", $pair[1]) }
}
& powershell @forward
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$apk = Join-Path $Src 'out\android\peggle2.apk'
Write-Host ''
if (-not $Device) {
    Write-Host 'Next: copy out\android\peggle2.apk to the phone (USB, or any way you like), open it in the file' -ForegroundColor Green
    Write-Host 'manager and tap Install. Do NOT uninstall the old app first: installing over it keeps your data.'
} else {
    Write-Host 'Installed. If the game still does not start, launch it once more: it writes its logs and a device' -ForegroundColor Green
    Write-Host 'report to Downloads/Peggle2-logs-*.zip. Attach that to an issue: https://github.com/aschaeffer9/peg2arm/issues'
}
Write-Host ''
