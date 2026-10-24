param([ValidateSet('x86','x64')][string]$Architecture='x64',[string]$VsRoot='C:\Program Files\Microsoft Visual Studio\2022\Community')
$ErrorActionPreference='Stop'
$vcvars=Join-Path $VsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
if(-not(Test-Path -LiteralPath $vcvars)) {throw 'Visual Studio C++ build tools were not found. Set -VsRoot.'}
$build=Join-Path $PSScriptRoot "build\$Architecture"
New-Item -ItemType Directory -Force $build | Out-Null
$script=Join-Path $build 'compile.cmd'
$lines=@('@echo off',"call `"$vcvars`" $Architecture",'if errorlevel 1 exit /b 1',"cd /d `"$PSScriptRoot`"", "cl /nologo /LD /EHsc /std:c++17 /W4 /O2 /MT /DUNICODE /D_UNICODE BonDriver_TCLG06.cpp /Fo:`"$build\BonDriver_TCLG06.obj`" /Fe:`"$build\BonDriver_TCLG06.dll`" /link /def:BonDriver_TCLG06.def ws2_32.lib",'if errorlevel 1 exit /b 1',"cl /nologo /EHsc /std:c++17 /W4 /O2 /MT /DUNICODE /D_UNICODE test_background.cpp /Fo:`"$build\test_background.obj`" /Fe:`"$build\test_background.exe`"",'exit /b %errorlevel%')
[System.IO.File]::WriteAllLines($script,$lines,[System.Text.Encoding]::Default)
& $env:ComSpec /d /c $script
if($LASTEXITCODE -ne 0) {throw 'BonDriver build failed.'}
Write-Output "BonDriver $Architecture build passed."
