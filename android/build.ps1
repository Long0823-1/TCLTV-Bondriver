param([string]$SdkRoot=$env:ANDROID_HOME,[string]$JdkRoot=$env:JAVA_HOME,[switch]$RecoverAaptZip)
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent $PSScriptRoot
if (-not $SdkRoot) { $SdkRoot=Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
if (-not $JdkRoot) { $javaCommand=Get-Command javac.exe -ErrorAction Stop; $JdkRoot=Split-Path -Parent (Split-Path -Parent $javaCommand.Source) }
$platform=Join-Path $SdkRoot 'platforms\android-35\android.jar'
$buildTools=Join-Path $SdkRoot 'build-tools\36.0.0'
if (-not (Test-Path -LiteralPath $platform)) { throw 'Android SDK Platform 35 is required.' }
if (-not (Test-Path -LiteralPath $buildTools)) { throw 'Android SDK Build-Tools 36.0.0 is required.' }
$env:JAVA_HOME=$JdkRoot
$build=Join-Path $PSScriptRoot 'build'
$local=Join-Path $repository '.local'
New-Item -ItemType Directory -Force -Path $build,"$build\classes","$build\aapt2-generated","$build\dex","$build\compiled",$local | Out-Null
function Invoke-Tool([string]$executable,[string[]]$arguments) {
 & $executable @arguments
 if ($LASTEXITCODE -ne 0) { throw "Tool failed: $([System.IO.Path]::GetFileName($executable)), exit $LASTEXITCODE" }
}
$resourceFiles=@(Get-ChildItem "$PSScriptRoot\res" -File -Recurse | ForEach-Object FullName)
foreach ($resource in $resourceFiles) { Invoke-Tool "$buildTools\aapt2.exe" @('compile',$resource,'-o',"$build\compiled") }
$linkArguments=@('link','-o',"$build\base.apk",'-I',$platform,'--manifest',"$PSScriptRoot\AndroidManifest.xml",'--java',"$build\aapt2-generated")
Get-ChildItem "$build\compiled" -Filter '*.flat' | ForEach-Object { $linkArguments+=@('-R',$_.FullName) }
& "$buildTools\aapt2.exe" @linkArguments
$linkExit=$LASTEXITCODE
if ($linkExit -ne 0) {
 if (-not $RecoverAaptZip -or $linkExit -ne -1073741819) { throw "AAPT2 link failed: $linkExit" }
 Write-Warning 'AAPT2 crashed while closing its output ZIP. Checking all local entries before rebuilding the central directory.'
 Invoke-Tool 'python' @("$PSScriptRoot\recover-aapt-zip.py","$build\base.apk")
 Invoke-Tool "$buildTools\aapt2.exe" @('dump','badging',"$build\base.apk")
}
Invoke-Tool "$JdkRoot\bin\jar.exe" @('uf',"$build\base.apk",'-C',$PSScriptRoot,'assets')
$javaSources=@(Get-ChildItem "$PSScriptRoot\src","$build\aapt2-generated" -Filter '*.java' -Recurse | ForEach-Object FullName)
Invoke-Tool "$JdkRoot\bin\javac.exe" (@('-encoding','UTF-8','-source','8','-target','8','-classpath',$platform,'-d',"$build\classes")+$javaSources)
$classFiles=@(Get-ChildItem "$build\classes" -Filter '*.class' -Recurse | ForEach-Object FullName)
Invoke-Tool "$buildTools\d8.bat" (@('--release','--min-api','26','--lib',$platform,'--output',"$build\dex")+$classFiles)
Invoke-Tool "$JdkRoot\bin\jar.exe" @('uf',"$build\base.apk",'-C',"$build\dex",'classes.dex')
& "$buildTools\zipalign.exe" -f 4 "$build\base.apk" "$build\aligned.apk"
$alignExit=$LASTEXITCODE
if ($alignExit -ne 0) {
 if (-not $RecoverAaptZip -or $alignExit -ne -1073741819) { throw "Zipalign failed: $alignExit" }
 Invoke-Tool 'python' @("$PSScriptRoot\recover-aapt-zip.py","$build\aligned.apk")
}
Invoke-Tool "$buildTools\zipalign.exe" @('-c','4',"$build\aligned.apk")
$passwordFile=Join-Path $local 'signing.pass'
$keyFile=Join-Path $local 'signing.jks'
if (-not (Test-Path -LiteralPath $passwordFile)) {
 $random=New-Object byte[] 32;$rng=[System.Security.Cryptography.RandomNumberGenerator]::Create();$rng.GetBytes($random);$rng.Dispose()
 [System.IO.File]::WriteAllText($passwordFile,([System.BitConverter]::ToString($random)).Replace('-',''),[System.Text.UTF8Encoding]::new($false))
}
if (-not (Test-Path -LiteralPath $keyFile)) {
 Invoke-Tool "$JdkRoot\bin\keytool.exe" @('-genkeypair','-keystore',$keyFile,'-storepass:file',$passwordFile,'-keypass:file',$passwordFile,'-alias','tclbondriver','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=Long0823-1')
}
$output=Join-Path $build 'TCL-BonDriver-Setup.apk'
Invoke-Tool "$buildTools\apksigner.bat" @('sign','--ks',$keyFile,'--ks-pass',"file:$passwordFile",'--ks-key-alias','tclbondriver','--out',$output,"$build\aligned.apk")
Invoke-Tool "$buildTools\apksigner.bat" @('verify','--verbose',$output)
Write-Output "APK ready: $output"
