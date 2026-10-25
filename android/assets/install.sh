#!/system/bin/sh
set -eu
STAGE=$1
BACKUP=/data/adb/tcl-bondriver-backup
TMP=/data/local/tmp
check_hash() {
 actual=$(sha256sum "$1" | cut -d ' ' -f 1)
 [ "$actual" = "$2" ] || { echo "対応ファームウェアを確認できません: $1"; exit 10; }
}
check_hash /vendor/tvos/libBionic/libfpp.so e31b65a665f9d7cd8c68a882bc23e9213c40ac30d388222bdd7a6c421b7c907b
check_hash /vendor/tvos/libBionic/libisdb.so f389445d2868bd091f842175592f77539480b2a6357ecc143a276149efe1a215
check_hash /vendor/lib/libmi3.so 05b6276bdc96e6ae769e0ce4f4a99e77a2181f964fddeb414c0f0770679b9aa6
if grep -q 'LD_PRELOAD=' /vendor/tvos/bin/run.sh && ! grep -q 'LD_PRELOAD=/data/local/tmp/libtsd.so' /vendor/tvos/bin/run.sh; then
 echo '既存の起動設定を確認してください。';exit 11
fi
grep -q '/vendor/tvos/bin/sitatvservice' /vendor/tvos/bin/run.sh || { echo '対応する起動設定が見つかりません。';exit 12; }
mkdir -p "$BACKUP" /data/adb/post-fs-data.d /data/adb/service.d
chmod 700 "$BACKUP"
if [ ! -f "$BACKUP/complete" ]; then
 for item in libtsd.so run_tsd.sh tsctl tsd_token tcl-pvr-net.sh; do
  if [ -f "$TMP/$item" ]; then cp -p "$TMP/$item" "$BACKUP/$item"; else touch "$BACKUP/$item.absent"; fi
 done
 for item in post-fs-data.d/90-tsd-inject.sh service.d/91-tcl-pvr.sh; do
  name=$(basename "$item")
  if [ -f "/data/adb/$item" ]; then cp -p "/data/adb/$item" "$BACKUP/$name"; else touch "$BACKUP/$name.absent"; fi
 done
 touch "$BACKUP/complete"
fi
cp "$STAGE/libtsd.so" "$TMP/libtsd.apk-new.so"
chmod 755 "$TMP/libtsd.apk-new.so"
chcon u:object_r:vendor_file:s0 "$TMP/libtsd.apk-new.so"
mv "$TMP/libtsd.apk-new.so" "$TMP/libtsd.so"
cp "$STAGE/tsctl" "$TMP/tsctl.new"
chmod 755 "$TMP/tsctl.new"
mv "$TMP/tsctl.new" "$TMP/tsctl"
if [ ! -s "$TMP/tsd_token" ]; then
 cp "$STAGE/token" "$TMP/tsd_token.new"
 chmod 600 "$TMP/tsd_token.new"
 chown 0:0 "$TMP/tsd_token.new"
 mv "$TMP/tsd_token.new" "$TMP/tsd_token"
fi
if grep -q 'LD_PRELOAD=/data/local/tmp/libtsd.so' /vendor/tvos/bin/run.sh; then
 cp /vendor/tvos/bin/run.sh "$TMP/run_tsd.sh.new"
else
 if grep -q 'LD_PRELOAD=' /vendor/tvos/bin/run.sh; then echo '既存の起動設定を確認してください。';exit 11;fi
 sed 's#^/vendor/tvos/bin/sitatvservice &#LD_PRELOAD=/data/local/tmp/libtsd.so /vendor/tvos/bin/sitatvservice \&#' /vendor/tvos/bin/run.sh >"$TMP/run_tsd.sh.new"
fi
grep -q 'LD_PRELOAD=/data/local/tmp/libtsd.so' "$TMP/run_tsd.sh.new"
chmod 755 "$TMP/run_tsd.sh.new"
chcon u:object_r:vendor_file:s0 "$TMP/run_tsd.sh.new"
mv "$TMP/run_tsd.sh.new" "$TMP/run_tsd.sh"
cp "$STAGE/90-tsd-inject.sh" /data/adb/post-fs-data.d/90-tsd-inject.sh.new
chmod 755 /data/adb/post-fs-data.d/90-tsd-inject.sh.new
mv /data/adb/post-fs-data.d/90-tsd-inject.sh.new /data/adb/post-fs-data.d/90-tsd-inject.sh
cp "$STAGE/tcl-pvr-net.sh" "$TMP/tcl-pvr-net.sh.new"
chmod 755 "$TMP/tcl-pvr-net.sh.new"
mv "$TMP/tcl-pvr-net.sh.new" "$TMP/tcl-pvr-net.sh"
cp "$STAGE/91-tcl-pvr.sh" "$TMP/91-tcl-pvr.sh.new"
chmod 755 "$TMP/91-tcl-pvr.sh.new"
mv "$TMP/91-tcl-pvr.sh.new" "$TMP/91-tcl-pvr.sh"
cp "$STAGE/91-tcl-pvr.sh" /data/adb/service.d/91-tcl-pvr.sh.new
chmod 755 /data/adb/service.d/91-tcl-pvr.sh.new
mv /data/adb/service.d/91-tcl-pvr.sh.new /data/adb/service.d/91-tcl-pvr.sh
sync
echo 'セットアップ完了。再起動すると有効になります。'
