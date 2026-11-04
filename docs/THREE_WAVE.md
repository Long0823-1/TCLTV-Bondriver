# 地デジ・BS・110度CS対応

## 移植元と設計

移植元: https://github.com/2m3k/TCLTV-Bondriver-3wave （`2c7bb42`、参照HEAD `c5f121e`、MIT）。
衛星は demod mode=8、LバンドIF周波数(kHz)、帯域欄0、satellite ID=59(BS)/60(CS)を使用します。
地デジは mode=4、帯域欄2を維持します。周波数・satellite IDはTVのスキャンDBから取得します。
局のID・周波数を地域固定の表に置き換えません。

ベンダーDBはグローバルなルート（地デジ4／衛星8）を持ちます。
両チューナーの検索を共通mutexで直列化し、対象ルートで番組・多重を検索した後、
成功時も失敗時も元のルートへ戻します。未対応ルート・衛星ID・周波数は選局エラーになります。
衛星の未契約局スキャン向けに、PAT/PMT解析後に映像・音声が復号できなくても選局を受理できます。
`av_clear=0` を返すので、受信・復号成功と区別できます。地デジの従来タイムアウトは維持します。

## 必要な環境と更新

対象は従来同様、ハッシュが一致するTCL G06/32S5400の対応ファームウェアです。
BS/CSは対応アンテナとB-CASカード、TV本体でのスキャンが必要です。4K/8Kは対象外です。
アプリでセットアップしてTVを再起動し、クライアント側の局一覧も再取得してください。
2チューナーは地デジ専用／衛星専用の固定割り当てではありません。
BonDriver版とMirakurun版は共有の配信本体を使用するため、同じチューナーを同時使用しないでください。

## 検証範囲

BS/CSアンテナのない実機環境です。BS/CS映像・音声・有料局・混在同時受信・長時間安定性は未検証です。
フォーク作者による受信報告を、このリポジトリでの実受信確認と同一視しません。

自動試験は、衛星選局設定・DBルート復元（成功／番組取得失敗／多重取得失敗／ルート変更失敗）・
地デジ／衛星カタログ・衛星のみのカタログ・旧INI互換・不正入力を対象にします。
BonDriverのXMLTV衛星自動巡回は競合回避のため対象外です。Mirakurun側のEPG収集はMirakurun標準の割り当てに従います。

## 再現するテスト

BonDriver版: `native/tests/build.ps1` で15件の選局・ルート試験、`windows/tests/run.ps1` でカタログ試験を実行します。
`bondriver/build.ps1 -Architecture x64` / `x86` でDLLと`test_three_wave.exe`を生成します。
`test_three_wave.exe DLLのパス 地デジの期待件数 衛星の期待件数` はネットワーク選局なしで列挙を確認します。
Mirakurun版: `javac -encoding UTF-8 -d .local/channel-tests android/src/io/github/long0823/tclmirakurun/ChannelCatalog.java tests/ChannelCatalogTest.java`、
`java -cp .local/channel-tests io.github.long0823.tclmirakurun.ChannelCatalogTest` で12件の局設定試験を実行します。
Mirakurunのnative試験ソースは`vendor/tcl/native/tests`にも同梱します。
