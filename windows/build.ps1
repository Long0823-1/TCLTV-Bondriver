$ErrorActionPreference='Stop'
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(-not(Test-Path -LiteralPath $csc)) {throw '.NET Framework 4.x compiler is required.'}
New-Item -ItemType Directory -Force "$PSScriptRoot\build" | Out-Null
& $csc /nologo /target:winexe /platform:anycpu /optimize+ /utf8output /r:System.Windows.Forms.dll /r:System.Drawing.dll "/out:$PSScriptRoot\build\TCL-BonDriver-Setup.exe" "$PSScriptRoot\Setup.cs" "$PSScriptRoot\ChannelCatalog.cs"
if($LASTEXITCODE -ne 0) {throw 'Windows setup build failed.'}
Write-Output 'Windows setup build passed.'
