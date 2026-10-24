#!/system/bin/sh
# Keep the point-to-point wired alias if Android's DHCP service resets eth0.
echo $$ >/data/local/tmp/tcl-pvr-net.pid
trap 'rm -f /data/local/tmp/tcl-pvr-net.pid' EXIT
while :; do
 if ip link show eth0 >/dev/null 2>&1; then
  ip link set eth0 up 2>/dev/null
  if ! ip -4 addr show eth0 | grep -q '169.254.95.200/16'; then
   ip addr add 169.254.95.200/16 dev eth0 2>/dev/null
  fi
 fi
 sleep 2
done
