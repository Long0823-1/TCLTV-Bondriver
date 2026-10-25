#!/system/bin/sh
# Magisk late-start service. Restore the native tuner without opening a TV app.
# Select only a visible terrestrial AV service from this TV's scan results.
first_scanned_tuple() {
 content query --uri content://android.media.tv/channel --projection original_network_id:transport_stream_id:service_id:type:service_type:browsable 2>/dev/null |
 grep -E 'type=TYPE_ISDB_T, service_type=(EN_TCL_DTV|SERVICE_TYPE_AUDIO_VIDEO), browsable=1$' |
 sed -n 's/^Row: [0-9][0-9]* original_network_id=\([0-9][0-9]*\), transport_stream_id=\([0-9][0-9]*\), service_id=\([0-9][0-9]*\),.*$/\1 \2 \3/p' |
 while read -r onid tsid sid; do
  [ "$onid" -ge 1 ] && [ "$onid" -le 65535 ] && [ "$tsid" -ge 1 ] && [ "$tsid" -le 65535 ] && [ "$sid" -ge 1 ] && [ "$sid" -le 65535 ] || continue
  printf '%02x%02x%02x%02x%02x%02x\n' "$((sid & 255))" "$((sid >> 8))" "$((tsid & 255))" "$((tsid >> 8))" "$((onid & 255))" "$((onid >> 8))"
  break
 done
}
if [ "$1" = '--channel-only' ];then first_scanned_tuple;exit;fi
CTL=/data/local/tmp/tsctl
LOG=/data/local/tmp/tcl-pvr-boot.log
exec >>"$LOG" 2>&1
printf '\nTCL PVR boot %s\n' "$(date)"
count=0
while [ "$(getprop sys.boot_completed)" != '1' ] || [ ! -x "$CTL" ]; do
 count=$((count+1)); [ "$count" -ge 90 ] && exit 1
 sleep 2
done
if grep -q 'name="enabled" value="true"' /data/user/0/io.github.long0823.tclbondriver/shared_prefs/network-service.xml 2>/dev/null;then
 am start-foreground-service -n io.github.long0823.tclbondriver/.NetworkService >/dev/null 2>&1
fi
network_pid=$(cat /data/local/tmp/tcl-pvr-net.pid 2>/dev/null)
network_running=0
case "$network_pid" in
 ''|*[!0-9]*) ;;
 *) if kill -0 "$network_pid" 2>/dev/null && grep -q '/data/local/tmp/tcl-pvr-net.sh' "/proc/$network_pid/cmdline" 2>/dev/null;then network_running=1;fi;;
esac
if [ "$network_running" -eq 0 ] && [ -f /data/local/tmp/tcl-pvr-net.sh ];then
 (trap '' HUP;exec /system/bin/sh /data/local/tmp/tcl-pvr-net.sh) </dev/null >/data/local/tmp/tcl-pvr-net.log 2>&1 &
fi
ip link set eth0 up
ip -4 addr show eth0 | grep -q '169.254.95.200/16' || ip addr add 169.254.95.200/16 dev eth0
count=0
while ! "$CTL" --status >/dev/null 2>&1;do
 count=$((count+1));[ "$count" -ge 30 ] && exit 1;sleep 2
done
if "$CTL" --status | grep -Eq ' running=1 .* service=[1-9][0-9]* ';then
 echo 'Record-only tuner already running; preserving current channel.';exit 0
fi
# Restore the record-only tuple, otherwise use this TV's scan results.
onid=;tsid=;sid=
if [ -r /data/local/tmp/tsd_last_record_channel_0 ];then
 read -r onid tsid sid < /data/local/tmp/tsd_last_record_channel_0
fi
valid_id() { case "$1" in ''|*[!0-9]*) return 1;;esac;[ "$1" -ge 1 ] && [ "$1" -le 65535 ]; }
if ! valid_id "$onid" || ! valid_id "$tsid" || ! valid_id "$sid";then
 tuple=$(first_scanned_tuple)
 [ "${#tuple}" -eq 12 ] || { echo 'Scan terrestrial channels on the TV first.';exit 2; }
 u16hex() { printf '%d' "0x$(printf '%s' "$1" | cut -c3-4)$(printf '%s' "$1" | cut -c1-2)"; }
 sid=$(u16hex "$(printf '%s' "$tuple" | cut -c1-4)")
 tsid=$(u16hex "$(printf '%s' "$tuple" | cut -c5-8)")
 onid=$(u16hex "$(printf '%s' "$tuple" | cut -c9-12)")
fi
"$CTL" --tune "$onid" "$tsid" "$sid" || { echo 'Record-only initialization failed.';exit 5; }
echo 'TCL record-only tuner boot initialization finished.'
