param([string]$TvRows,[string]$RealIni,[string]$FixtureIni)
$ErrorActionPreference='Stop'
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory $build -Force | Out-Null
& $csc /nologo /utf8output /out:$build\ChannelCatalogTests.exe "$PSScriptRoot\..\ChannelCatalog.cs" "$PSScriptRoot\ChannelCatalogTests.cs"
if($LASTEXITCODE -ne 0){throw 'Channel test build failed'}
$arguments=@();if($TvRows){$arguments=@($TvRows,$RealIni,$FixtureIni)}
& "$build\ChannelCatalogTests.exe" @arguments
if($LASTEXITCODE -ne 0){throw 'Channel tests failed'}