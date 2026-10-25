param([string]$VsRoot='C:\Program Files\Microsoft Visual Studio\2022\Community')
$ErrorActionPreference='Stop'
$vcvars=Join-Path $VsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$script=Join-Path $PSScriptRoot 'build-test.cmd'
$source=Join-Path (Split-Path $PSScriptRoot -Parent) 'tsd_psi.c'
$lines=@('@echo off',"call `"$vcvars`"",'if errorlevel 1 exit /b 1',"cd /d `"$PSScriptRoot`"","cl /nologo /LD /TC /std:c11 /W4 /WX /O2 /MT `"$source`" /Fe:test_psi.dll /link /EXPORT:tsd_crc32 /EXPORT:tsd_psi_reset /EXPORT:tsd_psi_feed /EXPORT:tsd_pat_pmt /EXPORT:tsd_pmt_pids /EXPORT:tsd_pid_add",'if errorlevel 1 exit /b 1','cl /nologo /TC /std:c11 /W4 /WX /O2 /MT ..\tsd_psi.c test_mux.c /Fe:test_mux.exe','if errorlevel 1 exit /b 1','test_mux.exe','exit /b %errorlevel%')
[IO.File]::WriteAllLines($script,$lines,[Text.Encoding]::Default)
& $env:ComSpec /d /c $script
if($LASTEXITCODE -ne 0){throw 'Parser test library build failed.'}
Write-Output 'Run: python native/tests/test_parser.py'
