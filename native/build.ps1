param([string]$NdkRoot=$env:ANDROID_NDK_HOME)
$ErrorActionPreference='Stop'
if(-not $NdkRoot) {throw 'Set ANDROID_NDK_HOME or pass -NdkRoot.'}
$toolchain=Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64'
$clang=Join-Path $toolchain 'bin\clang.exe'
$sysroot=Join-Path $toolchain 'sysroot'
if(-not(Test-Path -LiteralPath $clang)) {throw 'Android NDK Windows LLVM toolchain was not found.'}
$build=Join-Path $PSScriptRoot 'build'
$assets=Join-Path (Split-Path -Parent $PSScriptRoot) 'android\assets'
New-Item -ItemType Directory -Force $build,"$build\temp",$assets | Out-Null
$env:TMP="$build\temp";$env:TEMP="$build\temp"
& $clang --target=armv7a-linux-androideabi21 "--sysroot=$sysroot" -shared -fPIC -O2 -Wall -Wextra -Werror "$PSScriptRoot\tsd.c" "$PSScriptRoot\tsd_psi.c" -ldl -llog -o "$build\libtsd.so"
if($LASTEXITCODE -ne 0) {throw 'Native TV library build failed.'}
& $clang --target=armv7a-linux-androideabi21 "--sysroot=$sysroot" -O2 -Wall -Wextra -Werror "$PSScriptRoot\tsctl.c" -o "$build\tsctl"
if($LASTEXITCODE -ne 0) {throw 'Local TV control tool build failed.'}
Copy-Item -LiteralPath "$build\libtsd.so" -Destination "$assets\libtsd.so"
Copy-Item -LiteralPath "$build\tsctl" -Destination "$assets\tsctl"
Write-Output 'Native TV components built and copied into APK assets.'
