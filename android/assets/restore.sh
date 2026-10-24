#!/system/bin/sh
set -eu
BACKUP=/data/adb/tcl-bondriver-backup
TMP=/data/local/tmp
[ -f "$BACKUP/complete" ] || { echo '復元用のバックアップがありません。';exit 1; }
network_pid=$(cat /data/local/tmp/tcl-pvr-net.pid 2>/dev/null || true)
case "$network_pid" in ''|*[!0-9]*) ;; *) if grep -q '/data/local/tmp/tcl-pvr-net.sh' "/proc/$network_pid/cmdline" 2>/dev/null; then kill "$network_pid";fi;;esac
for item in libtsd.so run_tsd.sh tsctl tsd_token tcl-pvr-net.sh; do
 if [ -f "$BACKUP/$item" ]; then cp -p "$BACKUP/$item" "$TMP/$item.restore";mv "$TMP/$item.restore" "$TMP/$item";
 elif [ -f "$BACKUP/$item.absent" ]; then rm -f "$TMP/$item";fi
done
for item in post-fs-data.d/90-tsd-inject.sh service.d/91-tcl-pvr.sh; do
 name=$(basename "$item")
 if [ -f "$BACKUP/$name" ]; then cp -p "$BACKUP/$name" "/data/adb/$item.restore";mv "/data/adb/$item.restore" "/data/adb/$item";
 elif [ -f "$BACKUP/$name.absent" ]; then rm -f "/data/adb/$item";fi
done
echo 'セットアップ前の状態に戻しました。テレビを再起動してください。'
