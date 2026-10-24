# ビルド

必要なもの：Windows、PowerShell、Visual Studio 2022 C++ツール、.NET Framework 4.x、Android SDK Platform 35、Build-Tools 36.0.0、Android NDK、JDK。

```powershell
./native/build.ps1 -NdkRoot <Android-NDK-folder>
./bondriver/build.ps1 -Architecture x86 -VsRoot <Visual-Studio-folder>
./bondriver/build.ps1 -Architecture x64 -VsRoot <Visual-Studio-folder>
./windows/build.ps1
./android/build.ps1 -SdkRoot <Android-SDK-folder> -JdkRoot <JDK-folder>
```

nativeビルドはAPKへ入れるlibtsd.soとtsctlも生成します。BonDriverは視聴ソフトのビット数に合わせます。実機検証ではNDK 29.0.14206865とJDK 25を使用しています。

署名鍵とパスワードは `.local/` に生成され、公開しません。同じAPKを上書き更新する場合は、その鍵を保持してください。

一部のWindows実行環境でAAPT2とzipalignがZIP終端の書込み時に0xc0000005で終了する現象を確認しました。その場合に限り、次を使用できます。

```powershell
./android/build.ps1 -SdkRoot <Android-SDK-folder> -JdkRoot <JDK-folder> -RecoverAaptZip
```

このオプションは全エントリーのCRC・長さ・リソースを検査し、オフセットを保ったままZIP終端を補います。通常のコンパイルエラーは無視せず、最後にAPKの署名検証を行います。配布APKは署名検証済みです。

開発用テストハーネスは `bondriver/test_background.cpp`、PSI解析の試験は `native/tests` に配置しています。実機は対応するファームウェアのハッシュ検査を通る機器でのみ確認してください。
