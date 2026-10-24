# TCLTV-Bondriver

TCL G06日本向けAndroid TVの地デジを、WindowsのBonDriver対応ソフトから受信するためのツールです。TVでYouTubeなど別のアプリを使いながら、**独立した2チューナーで異なる局の映像・音声を受信**できます。

TV用セットアップAPK、Windows用セットアップツール、x86/x64のBonDriverを用意しています。初回設定後の選局・TS転送はTV内の常駐ネイティブサービスが行い、PC側のPythonやADB常駐は不要です。

**v0.2.0 / 実験的な対応機種限定版です。** 対応ファームウェアの実機で検証しています。他のTCL TV・ファームウェアへの適用は確認していません。

## ダウンロードと手順

- [セットアップ一式 ZIP](https://github.com/Long0823-1/TCLTV-Bondriver/releases/download/v0.2.0/TCLTV-Bondriver-v0.2.0.zip)
- **[スクショ付きセットアップ手順](docs/SETUP.md)**
- [ビルド方法](docs/BUILD.md)
- [構成とプロトコル](docs/ARCHITECTURE.md)
- [実機での検証結果](validation/README.md)

ZIPはすべて展開して使用してください。TVTestとADBは同梱していません。ADBがない場合は、[Google公式Platform-Tools](https://developer.android.com/tools/releases/platform-tools)を入手し、ZIP内の `platform-tools` フォルダーをセットアップEXEの隣へ展開してください。既存のPATH上のADBも使用できます。

## 対応環境

- 実機検証したTCL G06日本向けファームウェア、Android 11、32bit ARM。
- Magisk導入済みのTVと、そのTVで地デジを受信できる環境。
- Windows 10/11、x86またはx64のBonDriver対応ソフト。TVTest.exeのビット数に合うDLLをツールが選びます。
- TVとPC間の有線LAN。初回のAPK導入・接続設定にはUSBまたはネットワークADBが必要です。
- 現在のBonDriverの局一覧は東京・関東の検証用プリセットです。周波数はTVのチャンネルDBから取得します。TV側のチャンネル設定を先に済ませてください。

APKはroot化・ブートローダー変更・ファームウェアの書き換えを行いません。対応するベンダーライブラリー3個のSHA-256を検査し、不一致ならセットアップを中止します。検査値は `android/assets/install.sh` に記載しています。

## 使い方

1. Windowsの `TCL-BonDriver-Setup.exe` からAPKをTVへ導入します。
2. TVのAPKでセットアップを実行し、Magiskでこのアプリへのroot許可を与え、TVを再起動します。
3. PC側でTVのIPとTVTestのフォルダーを指定し、「2チューナー分」を有効にして接続設定・DLLを配置します。
4. TVTestを2つ起動し、それぞれ `BonDriver_TCLG06.dll` と `BonDriver_TCLG06_1.dll` を使用します。
5. 初回のチャンネルスキャンでは **「信号レベルを無視する」** を有効にします。

TVを再起動すると保存済みの選局・配信を復旧します。TV画面で地デジアプリを開く必要はありません。各チューナーのTSは1クライアント用です。同じチューナー番号を複数のBonDriverから同時に開く操作は拒否します。

## 現時点の制限

- 確認対象は地デジの主映像・主音声です。データ放送、追加音声、ワンセグ、選択サービス以外の復号は保証していません。
- 信号強度の取得は未実装のため表示は0dBです。受信可否は映像・TSの状態で確認してください。
- 2系統を使用中は、TV本体側の録画など別のチューナー使用機能との同時利用は検証していません。
- 受信不能・カード応答不良などの条件では再選局やTVの再起動が必要になることがあります。
- 元の設定に戻す場合は、TVのAPKで「セットアップ前の状態に戻す」を実行してから再起動します。初回導入前から開発用設定があった場合は、その状態へ戻ります。

## 接続情報の扱い

機器固有の接続キーはTVのroot専用ファイルと、PC側のINIに保存します。セットアップツールはINIとそのバックアップを現在のWindowsユーザーおよびSYSTEMだけが読めるように設定します。実際のINI、接続キー、署名秘密鍵、放送TS、ベンダーライブラリーはリポジトリに含めません。

開発：Long0823-1。自作部分は [MIT License](LICENSE)。TVTestなど外部ソフトはそれぞれのライセンスに従って入手してください。
