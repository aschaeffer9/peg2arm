# peg2arm guided installer: builds Peggle 2 for your Android phone from your own copy of the game, step by step.
#
# Start it with install\peg2arm-setup.bat (double-click). It walks through:
#   1. Tools        Git, Python 3.12 (+ packages), LLVM clang 20, the VS 2022 C++ Build Tools, a JDK 21, the Android
#                   SDK pieces and NDK r28c. Whatever is already installed is used; the rest is installed.
#   2. Your game    The Xbox 360 package of Peggle 2 (you choose the file).
#   3. PC build     Translates the game's code and builds the PC version once (the Android build reuses it). About an
#                   hour the first time; the game window opens for 45 seconds near the end.
#   4. Android      Builds the APK.
#   5. Phone        Connects to your phone (USB debugging), installs the app, copies the game data made by the
#                   peggle2-nx installer page (you choose the zip).
# Every step is checked and remembered: run it again after a problem and it carries on where it stopped.
# Nothing from the game leaves your computer; nothing is uploaded.
param(
    [switch]$Check,        # only report what is installed and what would be done; change nothing
    [string]$ToolsDir = '' # where the Android SDK, NDK and the JDK go (default: <drive of the repo>:\peg2arm-tools)
)

$ErrorActionPreference = 'Continue'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Out = Join-Path $Repo 'out'
New-Item -ItemType Directory -Force $Out | Out-Null
$LogFile = Join-Path $Out 'install_log.txt'
$StateFile = Join-Path $Out 'install_state.json'
if (-not $ToolsDir) { $ToolsDir = Join-Path ([IO.Path]::GetPathRoot($Repo)) 'peg2arm-tools' }

# --- Helpers --------------------------------------------------------------------------------------------------------
function Log([string]$s) { Add-Content -Path $LogFile -Value ("[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $s) }
function Say([string]$s, [string]$color = 'Gray') { Write-Host $s -ForegroundColor $color; Log $s }
function Title([string]$s) { Write-Host ''; Write-Host "=== $s ===" -ForegroundColor Cyan; Log "=== $s ===" }
function Ok([string]$s) { Say "  OK    $s" 'Green' }
function Todo([string]$s) { Say "  TODO  $s" 'Yellow' }
function Fail([string]$s) {
    Say "  PROBLEM: $s" 'Red'
    Say "  The full log is $LogFile. Fix the problem above and run the installer again: it carries on from here." 'Red'
    if (-not $Check) { Read-Host '  Press Enter to close' | Out-Null }
    exit 1
}
function Pause-For([string]$s) { if (-not $Check) { Write-Host ''; Read-Host "  $s, then press Enter" | Out-Null } }
function Find-First([string[]]$paths) { foreach ($p in $paths) { if ($p -and (Test-Path $p)) { return (Resolve-Path $p).Path } }; return $null }

function Load-State { if (Test-Path $StateFile) { return Get-Content $StateFile -Raw | ConvertFrom-Json } else { return [pscustomobject]@{} } }
function Save-State($state) { $state | ConvertTo-Json | Set-Content $StateFile -Encoding UTF8 }
function Set-StateValue($state, [string]$name, $value) {
    if ($state.PSObject.Properties[$name]) { $state.$name = $value } else { $state | Add-Member -NotePropertyName $name -NotePropertyValue $value }
    Save-State $state
}

function Winget-Install([string]$id, [string[]]$extra) {
    if ($Check) { Todo "would install $id (winget)"; return }
    Say "  installing $id (this can take a while; Windows may ask for permission) ..."
    $wa = @('install', '-e', '--id', $id, '--accept-source-agreements', '--accept-package-agreements', '--disable-interactivity') + $extra
    & winget @wa | Out-Host
    Log "winget $id exit $LASTEXITCODE"
}

function Download([string]$url, [string]$file) {
    if ($Check) { Todo "would download $url"; return $false }
    Say "  downloading $url ..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    try { Invoke-WebRequest -Uri $url -OutFile $file -UseBasicParsing; return $true } catch { Log "download failed: $_"; return $false }
}

function Pick-File([string]$title, [string]$filter) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = $title
    $dialog.Filter = $filter
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { return $dialog.FileName }
    return $null
}

$state = Load-State
Write-Host ''
Write-Host 'peg2arm installer: Peggle 2 for Android, built from your own copy of the game' -ForegroundColor White
Write-Host "Repository: $Repo"
Write-Host "Tools folder: $ToolsDir"
Write-Host "Log: $LogFile"
if ($Check) { Write-Host 'Check mode: nothing is installed or changed.' -ForegroundColor Yellow }
Log "installer start (check: $Check)"

# --- 1. Tools -------------------------------------------------------------------------------------------------------
Title 'Step 1 of 5: tools'
$free = (Get-PSDrive -Name ([IO.Path]::GetPathRoot($Repo).Substring(0, 1))).Free / 1GB
if ($free -lt 40) { Say ("  Only {0:N0} GB free on the repository's drive; the build needs about 40 GB." -f $free) 'Yellow' } else { Ok ("{0:N0} GB free" -f $free) }
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "winget is missing. Install 'App Installer' from the Microsoft Store, then run this again." }

$git = Find-First @("$env:ProgramFiles\Git\cmd\git.exe", "$env:LOCALAPPDATA\Programs\Git\cmd\git.exe")
if (-not $git) { Winget-Install 'Git.Git' @(); $git = Find-First @("$env:ProgramFiles\Git\cmd\git.exe") }
if ($git) { Ok "Git ($git)" } elseif (-not $Check) { Fail 'Git did not install.' }

$pyPaths = @("$env:LOCALAPPDATA\Programs\Python\Python312\python.exe", "$env:ProgramFiles\Python312\python.exe")
$py = Find-First $pyPaths
if (-not $py) { Winget-Install 'Python.Python.3.12' @(); $py = Find-First $pyPaths }
if ($py) {
    if (-not $Check) { & $py -m pip install --upgrade --quiet pip numpy Pillow lz4 xxhash | Out-Host }
    $mods = (& $py -c "import numpy, PIL, lz4, xxhash; print('ok')" 2>&1) -join ' '
    if ($mods -match 'ok') { Ok "Python 3.12 with numpy, Pillow, lz4, xxhash ($py)" } else { Todo "Python packages (numpy, Pillow, lz4, xxhash)" }
} elseif (-not $Check) { Fail 'Python 3.12 did not install.' }

$clang = Find-First @("$env:ProgramFiles\LLVM\bin\clang++.exe")
if ($clang -and ((& $clang --version | Select-Object -First 1) -notmatch 'version 20\.')) { Say '  LLVM is installed but not version 20; installing 20.1.8.' 'Yellow'; $clang = $null }
if (-not $clang) { Winget-Install 'LLVM.LLVM' @('--version', '20.1.8'); $clang = Find-First @("$env:ProgramFiles\LLVM\bin\clang++.exe") }
if ($clang) { Ok "LLVM clang 20 ($clang)" } elseif (-not $Check) { Fail 'LLVM clang 20 did not install.' }

$vsDir = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
if (-not (Test-Path "$vsDir\VC\Auxiliary\Build\vcvars64.bat")) {
    Say '  The VS 2022 C++ Build Tools are next: about 7 GB, 10 to 30 minutes.'
    Winget-Install 'Microsoft.VisualStudio.2022.BuildTools' @('--override', '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
}
$cmakeDir = "$vsDir\Common7\IDE\CommonExtensions\Microsoft\CMake"
if (Test-Path "$cmakeDir\CMake\bin\cmake.exe") { Ok 'VS 2022 C++ Build Tools (with CMake and Ninja)' } elseif (-not $Check) { Fail 'The VS 2022 C++ Build Tools did not install.' }

# JDK 21: one already in the tools folder, the user's JDKs, or Microsoft's build via winget.
$jdk = Find-First @((Get-ChildItem "$env:USERPROFILE\.jdks" -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -notlike '.*' } | Select-Object -First 1).FullName,
                    (Get-ChildItem "$env:ProgramFiles\Microsoft" -Directory -Filter 'jdk-21*' -ErrorAction SilentlyContinue | Select-Object -First 1).FullName)
if (-not $jdk) {
    Winget-Install 'Microsoft.OpenJDK.21' @()
    $jdk = (Get-ChildItem "$env:ProgramFiles\Microsoft" -Directory -Filter 'jdk-21*' -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if ($jdk) { Ok "JDK ($jdk)"; $env:JAVA_HOME = $jdk } elseif (-not $Check) { Fail 'No JDK 21.' }

# Android SDK (platform-tools, build-tools 36, platform android-36) and NDK r28c.
$sdk = Find-First @($env:ANDROID_HOME, "$env:LOCALAPPDATA\Android\Sdk", "$ToolsDir\android-sdk")
if (-not $sdk) { $sdk = "$ToolsDir\android-sdk" }
$sdkmanager = Find-First @("$sdk\cmdline-tools\latest\bin\sdkmanager.bat")
$needSdk = -not (Test-Path "$sdk\platform-tools\adb.exe") -or -not (Test-Path "$sdk\platforms\android-36\android.jar") -or -not (Get-ChildItem "$sdk\build-tools" -Directory -ErrorAction SilentlyContinue)
if ($needSdk) {
    if (-not $sdkmanager) {
        $zip = Join-Path $Out 'cmdline-tools.zip'
        $got = $false
        foreach ($build in '13114758', '11076708') {
            if (Download "https://dl.google.com/android/repository/commandlinetools-win-${build}_latest.zip" $zip) { $got = $true; break }
        }
        if ($got) {
            Expand-Archive $zip -DestinationPath "$sdk\cmdline-tools\tmp" -Force
            New-Item -ItemType Directory -Force "$sdk\cmdline-tools" | Out-Null
            Move-Item "$sdk\cmdline-tools\tmp\cmdline-tools" "$sdk\cmdline-tools\latest" -Force
            Remove-Item -Recurse -Force "$sdk\cmdline-tools\tmp"
            $sdkmanager = "$sdk\cmdline-tools\latest\bin\sdkmanager.bat"
        } elseif (-not $Check) { Fail 'Could not download the Android command-line tools. Install Android Studio instead, then run this again.' }
    }
    if ($sdkmanager -and -not $Check) {
        Say '  Accepting the Android SDK licences and installing platform-tools, build-tools 36 and android-36 ...'
        ("y`n" * 30) | & $sdkmanager --sdk_root=$sdk --licenses | Out-Null
        & $sdkmanager --sdk_root=$sdk 'platform-tools' 'build-tools;36.0.0' 'platforms;android-36' | Out-Host
    }
}
if (Test-Path "$sdk\platform-tools\adb.exe") { Ok "Android SDK ($sdk)" } elseif (-not $Check) { Fail 'The Android SDK is incomplete.' }
$adb = "$sdk\platform-tools\adb.exe"

$ndk = Find-First @($state.ndk, $env:ANDROID_NDK_HOME, "$ToolsDir\android-ndk-r28c", "$sdk\ndk\28.2.13676358")
if (-not $ndk) {
    $zip = Join-Path $Out 'android-ndk-r28c.zip'
    if (Download 'https://dl.google.com/android/repository/android-ndk-r28c-windows.zip' $zip) {
        Say '  unpacking the NDK (about 2 GB) ...'
        Expand-Archive $zip -DestinationPath $ToolsDir -Force
        $ndk = Find-First @("$ToolsDir\android-ndk-r28c")
    }
}
if ($ndk) { Ok "Android NDK r28c ($ndk)"; if (-not $Check) { Set-StateValue $state 'ndk' $ndk } } elseif (-not $Check) { Fail 'No Android NDK r28c.' }

# --- 2. Your game ---------------------------------------------------------------------------------------------------
Title 'Step 2 of 5: your copy of the game'
$pkg = $state.package
if (-not $pkg -or -not (Test-Path $pkg)) {
    Say '  Choose the Xbox 360 package of Peggle 2: the file named 2452FB99DD4F3E052C919B780FEAE8F8E8281CF058'
    Say '  (on your console under Content\0000000000000000\58411436\000D0000\).'
    if ($Check) { Todo 'would ask for the Xbox 360 package' } else {
        $pkg = Pick-File 'The Xbox 360 package of Peggle 2 (2452FB99DD4F3E052C919B780FEAE8F8E8281CF058)' 'Xbox 360 package|2452FB99DD4F3E052C919B780FEAE8F8E8281CF058;*|All files|*.*'
        if (-not $pkg) { Fail 'No package chosen.' }
        Set-StateValue $state 'package' $pkg
    }
}
if ($pkg) { Ok "Xbox 360 package ($pkg)" }

# --- 3. PC build ----------------------------------------------------------------------------------------------------
Title 'Step 3 of 5: the PC build (the Android build reuses it)'
Set-Location $Repo
$env:PEGGLE2_LLVM = "$env:ProgramFiles\LLVM"
$env:PEGGLE2_VSDIR = $vsDir
if ($py) { $env:PATH = "$(Split-Path $py);$(Split-Path $py)\Scripts;$env:PATH" }
if ($git) { $env:PATH = "$(Split-Path $git);$env:PATH" }
function Step([string]$name, [string]$log, [string]$cmd) {
    if ($Check) { Todo "would run: $name"; return }
    Say "  $name ..." 'White'
    $t = Get-Date
    & $env:ComSpec /c "$cmd > `"$log`" 2>&1"
    $code = $LASTEXITCODE
    Log "$name exit $code after $([int]((Get-Date) - $t).TotalMinutes) min ($log)"
    if ($code -ne 0) { Get-Content $log -Tail 25 | ForEach-Object { Say "    $_" 'DarkGray' }; Fail "$name failed (log: $log)" }
    Ok ("{0} ({1:N0} min)" -f $name, ((Get-Date) - $t).TotalMinutes)
}
if (Test-Path 'out\pc\peggle2.exe') { Ok 'PC build done before' } else {
    Say '  About an hour the first time. You can leave it running.'
    if (-not (Test-Path 'assets\game_root\default.xex')) { Step 'Extract the Xbox 360 package' 'out\extract.log' "`"$py`" tools\stfs_extract.py `"$pkg`" assets\game_root" }
    if (-not (Test-Path 'out\thirdparty.done')) { Step 'Fetch the SDK''s third-party sources' 'out\thirdparty.log' "`"$py`" tools\fetch_thirdparty.py"; if (-not $Check) { Set-Content 'out\thirdparty.done' 'ok' } }
    if (-not $Check) { . (Join-Path $Repo 'tools\win\env.ps1'); $ErrorActionPreference = 'Continue'; Set-Location $Repo }
    Step 'Configure the code generator' 'out\host_configure.log' 'cmake -S sdk -B out/host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2'
    Step 'Build the code generator' 'out\host_build.log' 'cmake --build out/host --target rexglue'
    $rex = Get-ChildItem 'sdk\out' -Recurse -Filter 'rexglue*.exe' -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^rexglue(rd|d)?\.exe$' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
    if (-not (Test-Path 'app\generated\rexglue.cmake')) { Step 'Translate the game''s code to C++' 'out\codegen_wrapper.log' "powershell -NoProfile -ExecutionPolicy Bypass -File tools\codegen.ps1 -Rexglue `"$rex`"" }
    if (-not (Test-Path 'out\renut_shader_dump')) { Step 'Dump the game''s shaders' 'out\shader_dump.log' "`"$py`" tools\renut_shader_dump.py --image out\default_image.bin --out out\renut_shader_dump" }
    $dxc = 'app\thirdparty\reblue-XenosRecomp\thirdparty\dxc-bin'
    if (-not (Test-Path "$dxc\bin\x64\dxc.exe")) {
        Step 'Fetch the DXC shader compiler' 'out\dxc_fetch.log' 'git init -q out\dxc-bin && git -C out\dxc-bin fetch -q --depth 1 https://github.com/renderbag/dxc-bin.git 12395e36295f950361ca842d8cc461492412facc && git -C out\dxc-bin checkout -q FETCH_HEAD'
        if (-not $Check) { foreach ($f in 'bin\x64\dxc.exe', 'bin\x64\dxcompiler.dll', 'bin\x64\dxil.dll', 'lib\x64\dxcompiler.lib') { New-Item -ItemType Directory -Force (Split-Path "$dxc\$f") | Out-Null; Copy-Item "out\dxc-bin\$f" "$dxc\$f" -Force } }
    }
    if (-not (Test-Path 'out\glslang\StandAlone\glslangValidator.exe')) { Step 'Build glslangValidator' 'out\glslang_wrapper.log' 'powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_glslang.ps1' }
    $pyFwd = "$py".Replace('\', '/')
    Step 'Configure the PC build' 'out\pc_configure.log' "cmake --preset win-pc -S app `"-DPython3_EXECUTABLE=$pyFwd`" `"-DPYTHON_EXECUTABLE=$pyFwd`""
    Step 'Build the PC version' 'out\pc_build.log' 'cmake --build out/pc'
    Say '  The game now opens for 45 seconds, so it can record the shaders it makes while running. Leave it alone.'
    if (-not $Check) { & powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\boot_test.ps1 | Out-File -Encoding utf8 'out\boot_test.log' }
    Step 'Rebuild with the recorded shaders' 'out\pc_build2.log' 'cmake --build out/pc'
}

# --- 4. Android -----------------------------------------------------------------------------------------------------
Title 'Step 4 of 5: the Android app'
$apk = Join-Path $Repo 'out\android\peggle2.apk'
if ($Check) { Todo 'would run tools\android\build_android.ps1' } else {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Repo 'tools\android\build_android.ps1') -Ndk $ndk -Sdk $sdk -Python $py -CMakeDir "$cmakeDir\CMake\bin;$cmakeDir\Ninja" | Out-Host
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $apk)) { Fail 'The Android build failed (out\android_report.txt says where).' }
    Ok "APK: $apk"
}

# --- 5. Phone -------------------------------------------------------------------------------------------------------
Title 'Step 5 of 5: your phone'
Say '  On the phone:'
Say '    1. Settings > About phone > Software information: tap "Build number" 7 times (Developer options appear).'
Say '    2. Settings > Developer options: turn on "USB debugging".'
Say '    3. Connect the phone to this PC with a USB cable, unlock it, and allow USB debugging when it asks.'
Pause-For 'Do that'
$device = $null
for ($i = 0; $i -lt 20 -and -not $device -and (Test-Path $adb); $i++) {
    $device = (& $adb devices | Select-String -Pattern '\tdevice$' | Select-Object -First 1) -replace '\tdevice$', ''
    if (-not $device -and -not $Check) { Start-Sleep 3 }
    if ($Check) { break }
}
if ($device) { Ok "phone: $device" } elseif ($Check) { Todo 'would wait for the phone' } else { Fail 'No phone found over USB (check the cable, unlock the phone, allow USB debugging).' }
if (-not $Check) {
    Say '  Installing the app ...'
    $r = (& $adb -s $device install -r -g $apk 2>&1) -join ' '
    if ($r -notmatch 'Success') { Fail "adb install: $r" }
    Ok 'app installed'
}
$zip = $state.datazip
if (-not $zip -or -not (Test-Path $zip)) {
    Say '  The game data comes from the peggle2-nx installer page, made from your own Xbox 360 and PS4 copies:'
    Say '    https://chanseyisthebest.github.io/peggle2-nx-installer/  (press "Create peggle2-nx.zip")'
    if ($Check) { Todo 'would ask for peggle2-nx.zip' } else {
        Start-Process 'https://chanseyisthebest.github.io/peggle2-nx-installer/'
        Pause-For 'Make peggle2-nx.zip on that page'
        $zip = Pick-File 'peggle2-nx.zip from the installer page' 'peggle2-nx.zip|peggle2-nx*.zip|Zip files|*.zip'
        if (-not $zip) { Fail 'No zip chosen.' }
        Set-StateValue $state 'datazip' $zip
    }
}
if (-not $Check) {
    Say '  Copying the game data to the phone (2.7 GB; over USB a few minutes) ...'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Repo 'tools\android\push_data.ps1') -Data $zip -Device $device -Sdk $sdk | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail 'Copying the game data failed.' }
    Ok 'game data on the phone'
    Write-Host ''
    Say 'Done. Peggle 2 is on your phone: look for "Peggle 2" in the app drawer.' 'Green'
    Say 'Optional: the cover launcher, see launcher\README.md.' 'Green'
    Read-Host '  Press Enter to close' | Out-Null
} else {
    Write-Host ''
    Say 'Check finished: the TODO lines are what a real run would do.' 'Yellow'
}
exit 0
