# TCLTV-Bondriver

## Magisk適用済みbootイメージの公開（2026-10-27）

TCL 32S5400（G06 / MT5867）向けの **Magisk 25.2（25200）適用済みboot.img** を、別リポジトリで公開しました。ユーザーによる実機起動試験で正常動作を確認しています。

[対応条件・導入説明](https://github.com/Long0823-1/TCL-G06-Magisk-Boot) · [bootイメージとSHA-256をダウンロード](https://github.com/Long0823-1/TCL-G06-Magisk-Boot/releases/tag/magisk-25.2-20261027)

**改変bootを書き込む前に、必ずvbmetaのAVB検証を無効化してください。** ブートローダーのアンロックとは別の操作です。対象TVと同じファームウェアのvbmetaを使用し、元のboot・vbmeta等のバックアップと復旧手段を確保してください。手順は[導入・復旧の説明](https://github.com/Long0823-1/twrp-tcl-g06-mt5867#導入と復旧)を参照してください。機種名が同じでも別基板・別ファームウェア・他個体は未検証です。スマホ視聴アプリを使うスマートフォン自身のroot化は不要です。


TCL G06日本向けAndroid TVの地デジを、WindowsのBonDriver対応ソフトから受信するためのツールです。TVでYouTubeなど別のアプリを使いながら、**独立した2チューナーで異なる局の映像・音声を受信**できます。

TV用セットアップAPK、Windows用セットアップツール、x86/x64のBonDriverを用意しています。初回設定後の選局・TS転送はTV内の常駐ネイティブサービスが行い、PC側のPythonやADB常駐は不要です。

**配信本体・TV用APK・Windowsセットアップ一式 v0.6.0（XMLTV番組表・消灯時の通信維持・IPTV）。実験的な対応機種限定版です。** 対応ファームウェアの実機で検証しています。他のTCL TV・ファームウェアへの適用は確認していません。

Androidスマートフォン向けには、独立アプリ[TCLTV-Android-Viewer](https://github.com/Long0823-1/TCLTV-Android-Viewer)を用意しています。番組表・アプリ内再生・システムバーを隠す全画面・対応局の実ロゴ表示に対応し、TVセットアップAPK 0.6.0では確認コードで接続情報・スキャン済み局一覧をスマホへ渡せます。
## 0.6.0の更新

XMLTV形式の番組表とgzip配信を追加。M3UへEPG URLを埋め込み、空きチューナーで番組情報を自動収集し、外部からの視聴要求で中断します。IPTV視聴中にもEITを蓄積し、再起動後も保存済み情報を配信します。番組表の取得だけでは選局しません。スマホ版0.4.1からXMLTV URLをコピーできます。

[XMLTVの登録・収集手順](docs/XMLTV.md) · [検証記録](validation/v0.6.0.md)

## 0.5.0の更新

- 通常のリモコン短押しで画面を消しても、Wi-Fi・有線LANと配信処理を維持する常駐サービスを追加。
- TCLの常駐許可はこのアプリだけを追加し、停止操作で追加分と変更した電源設定を戻します。
- 第1チューナーが消灯時に一時停止した際もTS接続を保持して再捕捉します。再捕捉中は短い映像・音声の停止が起こる場合があります。
- TVでM3UプレイリストとHTTP MPEG-TSを配信。TVのスキャン結果から局一覧を生成し、一般IPTVプレイヤー向けに指定番組だけを送ります。
- スマホ版0.4.0ではTVからIPTV専用情報を取得し、M3U URLを表示・コピーできます。

通信維持中は通常の省電力待機より消費電力が増え、AC給電中の自動消灯も抑制されます。リモコンでの画面消灯は可能です。対象は対応ファームウェアの通常待機です。

[更新・消灯時の通信維持](docs/SLEEP_IPTV.md) · [IPTVの使い方](docs/IPTV.md) · [0.5.0実機検証](validation/v0.5.0.md)

## ダウンロードと手順

- [Windows・初回TVセットアップ一式 ZIP（v0.6.0）](https://github.com/Long0823-1/TCLTV-Bondriver/releases/download/v0.6.0/TCLTV-Bondriver-v0.6.0.zip)
- [TVセットアップAPK 0.6.0（XMLTV・通信維持・IPTV）](https://github.com/Long0823-1/TCLTV-Bondriver/releases/tag/v0.6.0)
- [Androidスマホ用APK 0.4.1](https://github.com/Long0823-1/TCLTV-Android-Viewer/releases/tag/v0.4.1)
- [スマホの接続手順](docs/mobile-pairing.md)
- **[スクショ付きセットアップ手順](docs/SETUP.md)**
- [ビルド方法](docs/BUILD.md)
- [構成とプロトコル](docs/ARCHITECTURE.md)
- [実機での検証結果](validation/README.md)

ZIPはすべて展開して使用してください。TVTestとADBは同梱していません。ADBがない場合は、[Google公式Platform-Tools](https://developer.android.com/tools/releases/platform-tools)を入手し、ZIP内の `platform-tools` フォルダーをセットアップEXEの隣へ展開してください。既存のPATH上のADBも使用できます。

## 対応環境

- 実機検証したTCL G06日本向けファームウェア、Android 11、32bit ARM。
- Magisk導入済みのTVと、そのTVで地デジを受信できる環境。
- Windows 10/11、x86またはx64のBonDriver対応ソフト。TVTest.exeのビット数に合うDLLをツールが選びます。
- TVとPC間の有線LANまたはWi-Fi。初回のAPK導入・接続設定にはUSBまたはネットワークADBが必要です。
- 地デジの局一覧はTV本体のスキャン結果から取得します。地域固定のプリセットは使いません。TV側でスキャン後、Windowsツールで接続設定・局一覧を更新してください。周波数はTVのチャンネルDBから取得します。

APKはroot化・ブートローダー変更・ファームウェアの書き換えを行いません。対応するベンダーライブラリー3個のSHA-256を検査し、不一致ならセットアップを中止します。検査値は `android/assets/install.sh` に記載しています。

## 使い方

1. Windowsの `TCL-BonDriver-Setup.exe` からAPKをTVへ導入します。
2. TVのAPKでセットアップを実行し、Magiskでこのアプリへのroot許可を与え、TVを再起動します。
3. PC側でTVのIPとTVTestのフォルダーを指定し、「2チューナー分」を有効にして接続設定・DLLを配置します。
4. TVTestを2つ起動し、それぞれ `BonDriver_TCLG06.dll` と `BonDriver_TCLG06_1.dll` を使用します。
5. TVTestでチャンネルスキャンします。信号欄にはチューナー実測のC/N（dB）を表示します。

TVを再起動すると保存済みの選局・配信を復旧します。TV画面で地デジアプリを開く必要はありません。各チューナーのTSは1クライアント用です。同じチューナー番号を複数のBonDriverから同時に開く操作は拒否します。

## 現時点の制限

- PATにある全サービスのPMTと、映像・音声サービスのPIDを取得し、枝番号・追加音声・データ放送に対応しました。実機検証はNHK総合・ETVを中心に実施しています。異なるECMを使うサービス群や、放送途中のPMT変更への自動追従は未検証です。変更時は再選局してください。
- 信号表示は実測C/Nを約1秒間隔で取得します。ロックなし・切断時は0dBです。電波強度の絶対値（dBm）ではありません。
- 対応上限はPAT 1セクション・32サービス・PID合計63・第1系統の映像・音声2 PID＋追加復号ES 12・第2系統の追加復号PID 12です。上限超過時は選局エラーにします。
- 視聴サービス内のデータ放送は配信しますが、独立したデータ専用サービスのESはPID枠確保のため対象外です。
- 2系統を使用中は、TV本体側の録画など別のチューナー使用機能との同時利用は検証していません。
- 受信不能・カード応答不良などの条件では再選局やTVの再起動が必要になることがあります。
- 元の設定に戻す場合は、TVのAPKで「セットアップ前の状態に戻す」を実行してから再起動します。初回導入前から開発用設定があった場合は、その状態へ戻ります。

## 接続情報の扱い

機器固有の接続キーはTVのroot専用ファイルと、PC側のINIに保存します。セットアップツールはINIとそのバックアップを現在のWindowsユーザーおよびSYSTEMだけが読めるように設定します。実際のINI、接続キー、署名秘密鍵、放送TS、ベンダーライブラリーはリポジトリに含めません。

開発：Long0823-1。自作部分は [MIT License](LICENSE)。TVTestなど外部ソフトはそれぞれのライセンスに従って入手してください。

## v0.4.1への更新

TVTestを閉じて新しいAPKでセットアップし、TVを再起動します。Windowsツールの「2. 接続設定・局一覧を更新」を実行し、TVTestのチャンネルスキャンをやり直してください。v0.3.0の局一覧は利用できます。旧DLLだけの差し替えではTV側の復号処理は更新されないため、APKも更新してください。

## Androidスマートフォンで視聴

独立アプリ[TCLTV-Android-Viewer](https://github.com/Long0823-1/TCLTV-Android-Viewer)を使用できます。TVセットアップAPK 0.6.0では、スマホへ確認コードで接続情報・局一覧を渡す機能を追加しました。[接続手順](docs/mobile-pairing.md)を参照してください。
