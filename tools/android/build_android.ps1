# peg2arm - the Android build: libmain.so (NDK r28c, arm64-v8a) and a signed APK (out\android\peggle2.apk).
# Run from the repository root after the PC build (README "Build"): Android reuses its shader cache.
#   1. The Android shader cache from the PC build's (tools/renut_switch_shader_cache.py: SPIR-V only).
#   2. The direct-call step again (tools/direct_calls.py --redo), so every hooked game function, the touch layer's
#      included, is called through its hook.
#   3. Configure + build out\android (the NDK's CMake toolchain, CMake + Ninja).
#   4. APK (tools/android/package_apk.py), then adb install when -Device is given.
# Summary in out\android_report.txt; every step's full log in out\android_*.log.
param(
    [string]$Ndk = $env:ANDROID_NDK_HOME,                        # Android NDK r28c
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",             # Android SDK (build-tools, platform android-36)
    [string]$Python = '',                                        # Python 3.12 (default: the one on PATH)
    [string]$CMakeDir = '',                                      # folder with cmake.exe and ninja.exe (default: PATH)
    [string]$Device = '',                                        # adb serial or ip:port to install on (optional)
    [string]$Keystore = '',                                      # signing key (default: out\android-debug.keystore)
    [int]$MinSdk = 30                                            # android-30 = Android 11 (Amazon Fire OS 8); the
                                                                 # NDK platform and the APK's minSdkVersion both
                                                                 # come from it, so they cannot disagree
)

$ErrorActionPreference = 'Continue'
$Src    = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Report = Join-Path $Src 'out\android_report.txt'
$Adb    = "$Sdk\platform-tools\adb.exe"
$Py     = if ($Python) { $Python } else { (Get-Command python -ErrorAction SilentlyContinue).Source }
if (-not $Keystore) { $Keystore = Join-Path $Src 'out\android-debug.keystore' }
if (-not $CMakeDir) {
    # The VS Build Tools carry CMake and Ninja; otherwise they must be on PATH.
    $vs = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake"
    if (Test-Path $vs) { $env:PATH = "$vs\CMake\bin;$vs\Ninja;" + $env:PATH }
} else { $env:PATH = "$CMakeDir;" + $env:PATH }
$Lines  = New-Object System.Collections.Generic.List[string]
function Log([string]$s) { Write-Host $s; $Lines.Add($s); $Lines | Set-Content -Path $Report -Encoding UTF8 }

function Step([string]$name, [string]$log, [string]$cmd, [string]$errorPattern = 'error:|FAILED:|CMake Error|fatal') {
    $t = Get-Date
    Log ("[{0}] {1} ..." -f $t.ToString('HH:mm'), $name)
    & $env:ComSpec /c "$cmd > `"$log`" 2>&1"
    $code = $LASTEXITCODE
    Log ("  exit {0} after {1:N1} min  (log: {2})" -f $code, ((Get-Date) - $t).TotalMinutes, $log)
    if ($code -ne 0) {
        Log "  ---- first errors ----"
        Select-String -Path $log -Pattern $errorPattern | Select-Object -First 40 | ForEach-Object { Log ("  " + $_.Line) }
        Log "  ---- last 25 lines ----"
        Get-Content $log -Tail 25 | ForEach-Object { Log "  $_" }
        Log "FAILED at: $name"
        exit 1
    }
}

Log "peg2arm Android build - $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
foreach ($p in $Ndk, $Sdk, $Py) { if (-not $p -or -not (Test-Path $p)) { Log "ERROR: missing '$p' (see -Ndk, -Sdk, -Python)"; exit 1 } }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -or -not (Get-Command ninja -ErrorAction SilentlyContinue)) { Log "ERROR: cmake / ninja not found (-CMakeDir)"; exit 1 }
Set-Location $Src
New-Item -ItemType Directory -Force 'out' | Out-Null

# 1. The Android shader cache from the PC build's (out\pc\renut_gpu\shader_cache.cpp, with any shaders its runs captured).
if (-not (Test-Path 'out\pc\renut_gpu\shader_cache.cpp')) { Log "ERROR: no PC build in out\pc (README: Build)"; exit 1 }
$cache = 'out\renut_switch\shader_cache_switch.cpp'
if (-not (Test-Path $cache) -or (Get-Item 'out\pc\renut_gpu\shader_cache.cpp').LastWriteTime -gt (Get-Item $cache).LastWriteTime) {
    Step 'Android shader cache' 'out\android_shader_cache.log' "`"$Py`" tools\renut_switch_shader_cache.py --pc out\pc"
} else { Log "[skip] Android shader cache up to date" }

# 2. Calls to hooked game functions go through their hooks (the codegen step may predate a hook).
Step 'Direct calls (hooks respected)' 'out\android_direct_calls.log' "`"$Py`" tools\direct_calls.py --redo"

# 3. Configure + build. The NDK platform is the APK's minSdkVersion: one number for both, so the library's API level
#    and the manifest's cannot contradict each other (the build used to say 31 while a Fire tablet needs 30).
Log ("Android API level: android-{0}" -f $MinSdk)
$PyFwd = $Py.Replace('\', '/')
$NdkFwd = $Ndk.Replace('\', '/')
$configure = "cmake -S app -B out/android -G Ninja " +
    "-DCMAKE_TOOLCHAIN_FILE=$NdkFwd/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a " +
    "-DANDROID_PLATFORM=android-$MinSdk -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=RelWithDebInfo " +
    "-DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_VULKAN=ON " +
    "`"-DPython3_EXECUTABLE=$PyFwd`" `"-DPYTHON_EXECUTABLE=$PyFwd`""
Step 'Configure Android build' 'out\android_configure.log' $configure
Step 'Build libmain.so' 'out\android_build.log' 'cmake --build out/android --target peggle2'
$so = Get-ChildItem 'out\android' -Recurse -Filter 'libmain.so' | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $so) { Log "FAILED: no libmain.so under out\android"; exit 1 }
Log ("  OK  {0} ({1:N1} MB)" -f $so.FullName, ($so.Length / 1MB))

# 4. APK.
$apk = Join-Path $Src 'out\android\peggle2.apk'
Step 'Package APK' 'out\android_package.log' "`"$Py`" tools\android\package_apk.py --so `"$($so.FullName)`" --out `"$apk`" --ndk `"$Ndk`" --keystore `"$Keystore`" --min-sdk $MinSdk" 'package_apk:|error|Error|FAILED'
Log ("  OK  {0} ({1:N1} MB)" -f $apk, ((Get-Item $apk).Length / 1MB))

if (-not $Device) { Log "Done (not installed: pass -Device to install over adb)."; exit 0 }
if ($Device -match ':') { & $Adb connect $Device | Out-Null }
$state = (& $Adb -s $Device get-state 2>&1) -join ' '
if ($state -notmatch 'device') { Log "device not reachable over adb ($state): APK built, not installed"; exit 0 }
Step 'Install on the device' 'out\android_install.log' "`"$Adb`" -s $Device install -r -g `"$apk`"" 'Failure|failed|error'
Log "Done."
exit 0
