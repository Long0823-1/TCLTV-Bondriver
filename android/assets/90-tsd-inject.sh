#!/system/bin/sh
# Magisk post-fs-data: change only the vendor TV service launch environment.
count=0
while [ ! -s /data/local/tmp/run_tsd.sh ] || [ ! -s /data/local/tmp/libtsd.so ]; do
 count=$((count+1));[ "$count" -ge 300 ] && exit 1
 sleep 0.2
done
chmod 755 /data/local/tmp/run_tsd.sh /data/local/tmp/libtsd.so
chcon u:object_r:vendor_file:s0 /data/local/tmp/run_tsd.sh /data/local/tmp/libtsd.so 2>/dev/null
magiskpolicy --live 'permissive tcl_sitatvservice' 2>/dev/null
mount --bind /data/local/tmp/run_tsd.sh /vendor/tvos/bin/run.sh
