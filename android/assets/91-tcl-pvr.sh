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
while ! "$CTL" pvrstatus >/dev/null 2>&1;do
 count=$((count+1));[ "$count" -ge 30 ] && exit 1;sleep 2
done
if "$CTL" pvrstatus | grep -q 'running=1';then
 echo 'PVR already running; preserving current channel.';exit 0
fi
# Restore the saved channel, otherwise use this TV's scan results.
tuple=
pids=0000
if [ -r /data/local/tmp/tsd_last_channel ];then
 saved_tuple=$(sed -n '1p' /data/local/tmp/tsd_last_channel)
 saved_pids=$(sed -n '2p' /data/local/tmp/tsd_last_channel)
 case "$saved_tuple" in ''|*[!0-9a-fA-F]*) saved_tuple=;;esac
 case "$saved_pids" in ''|*[!0-9a-fA-F]*) saved_pids=;;esac
 [ "${#saved_tuple}" -eq 12 ] && tuple=$saved_tuple
 if [ -n "$saved_pids" ] && [ "${#saved_pids}" -le 252 ];then pids=$saved_pids;fi
fi
valid=
if [ "${#tuple}" -eq 12 ];then
 "$CTL" "pokek 3 0 $tuple" >/dev/null
 valid=$("$CTL" 'call db_dtv_get_prog_info_by_triplet @4 @3')
fi
if [ "$valid" != 'ret=0x1 (1)' ];then
 tuple=$(first_scanned_tuple)
 [ "${#tuple}" -eq 12 ] || { echo 'No visible terrestrial channel: scan channels on the TV first.';exit 2; }
 pids=0000
 "$CTL" "pokek 3 0 $tuple" >/dev/null
 valid=$("$CTL" 'call db_dtv_get_prog_info_by_triplet @4 @3')
fi
[ "$valid" = 'ret=0x1 (1)' ] || { echo 'The scanned channel is not in the native DB; rescan on the TV.';exit 2; }
"$CTL" 'call tvhdtv_play_program_by_triplet @3'
count=0
while :;do
 "$CTL" 'call fpi_demod_get_lock_status 0 @6' >/dev/null
 locked=$("$CTL" 'peekk 6 1')
 [ "$locked" = '01' ] && break
 count=$((count+1));[ "$count" -ge 30 ] && { echo 'Native tuner lock timed out.';exit 3; };sleep 1
done
"$CTL" 'call mif_tsi_pvr_init' "pvrset $pids" 'pvrstream 40556' pvrstatus
if "$CTL" pvrstatus | grep -q 'running=1';then
 echo 'TCL PVR boot initialization finished.'
else
 echo 'PVR initialization failed.';exit 4
fi
