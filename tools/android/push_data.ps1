# peg2arm - copies the game data to the phone, into the app's folder (Android/data/<package>/files), once the app
# is installed (the folder must belong to the app). Files already there with the same size are skipped.
#
# The data is what the peggle2-nx installer page makes from your own copy of the game (README "How to install"):
# its peggle2-nx.zip, or a folder with the files taken out of it.
#
#   powershell -ExecutionPolicy Bypass -File tools\android\push_data.ps1 -Data C:\path\peggle2-nx.zip -Device 192.168.1.20:5555
param(
    [Parameter(Mandatory = $true)][string]$Data,               # peggle2-nx.zip, or a folder holding the files
    [string]$Device = '',                                      # adb serial or ip:port (default: the only device)
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$Package = 'com.ari.peggle2'
)
$ErrorActionPreference = 'Continue'
$Adb   = "$Sdk\platform-tools\adb.exe"
$Dest  = "/sdcard/Android/data/$Package/files"
$Files = @('game_root.nxpack', 'game_root.nxidx', 'peggle2_hd.pack', 'peggle2_shaders.mesp', 'peggle2_shaders.mesp.idx')
if (-not (Test-Path $Adb)) { "no adb at $Adb (-Sdk)"; exit 1 }
$dev = if ($Device) { @('-s', $Device) } else { @() }

# 1. From the zip (its entries are stored, not compressed: a straight copy) into a folder next to it.
$dir = $Data
if ((Test-Path $Data -PathType Leaf) -and $Data -like '*.zip') {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $dir = Join-Path (Split-Path $Data) 'peg2arm_data'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $zip = [System.IO.Compression.ZipFile]::OpenRead($Data)
    foreach ($f in $Files) {
        $entry = $zip.Entries | Where-Object { $_.Name -eq $f } | Select-Object -First 1
        if (-not $entry) { "not in the zip: $f"; $zip.Dispose(); exit 1 }
        $out = Join-Path $dir $f
        if ((Test-Path $out) -and (Get-Item $out).Length -eq $entry.Length) { "have $f"; continue }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $out, $true)
        "extracted $f ($($entry.Length) bytes)"
    }
    $zip.Dispose()
}
foreach ($f in $Files) { if (-not (Test-Path (Join-Path $dir $f))) { "missing $f in $dir"; exit 1 } }

# 2. To the phone.
if ($Device -match ':') { & $Adb connect $Device | Out-Null }
$installed = (& $Adb @dev shell pm list packages $Package) -join ''
if ($installed -notmatch [regex]::Escape($Package)) { "the app ($Package) is not installed yet: install the APK first"; exit 2 }
# Start the app once so Android makes its folder (owned by the app), then stop it.
& $Adb @dev shell monkey -p $Package -c android.intent.category.LAUNCHER 1 | Out-Null
Start-Sleep 3
& $Adb @dev shell am force-stop $Package
& $Adb @dev shell mkdir -p $Dest
foreach ($f in $Files) {
    $local = Join-Path $dir $f
    $size = (Get-Item $local).Length
    $remote = ((& $Adb @dev shell "stat -c %s $Dest/$f 2>/dev/null") -join '').Trim()
    if ($remote -eq "$size") { "on the phone already: $f"; continue }
    $t = Get-Date
    & $Adb @dev push $local "$Dest/$f"
    if ($LASTEXITCODE) { "push failed: $f"; exit 1 }
    "pushed {0} ({1:N0} MB in {2:N1} min)" -f $f, ($size / 1MB), ((Get-Date) - $t).TotalMinutes
}
"--- on the phone"
& $Adb @dev shell ls -l $Dest
exit 0
