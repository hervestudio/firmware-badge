#!/bin/bash
# Fleet OTA flashing — watches the Mac's WiFi: as soon as it gets an IP in
# 192.168.4.x (= connected to the AP of a badge in OTA flash mode), starts
# the upload, waits for the badge to reboot (IP lost), re-arms for the next.
# (IP-based detection: macOS hides the SSID without location permission.)
# The memory wipe is done BY the firmware on first boot
# (RESET_GEN in main.cpp) — nothing else to do.
#
# Usage:  ./tools/flash_fleet.sh   (from the repo root, Ctrl-C to stop)
# On each badge: hold PREV at power-on -> OTA mode -> the Mac joins
# "badge-threejs" / threejs2026 (automatic from the 2nd badge on) -> flash.
cd "$(dirname "$0")/.." || exit 1
PIO="$HOME/.platformio/penv/bin/pio"
IF=$(networksetup -listallhardwareports 2>/dev/null |
     awk '/Wi-Fi|AirPort/{getline; print $2; exit}')
IF=${IF:-en0}
on_badge() { [[ "$(ipconfig getifaddr "$IF" 2>/dev/null)" == 192.168.4.* ]]; }
count=0
echo "watching $IF — put a badge in OTA mode and connect to badge-threejs"
while true; do
  if on_badge; then
    echo ""
    echo "=== badge detected (IP $(ipconfig getifaddr "$IF")) — OTA flash ==="
    if "$PIO" run -e ota -t upload; then
      count=$((count + 1))
      echo "=== OK — badge #$count flashed, rebooting (waiting for disconnect) ==="
      say "badge $count flashed" 2>/dev/null
      while on_badge; do sleep 2; done
      echo "--- ready for the next badge"
    else
      echo "=== FAILED — retrying in 5 s (badge still in OTA mode?) ==="
      say "failed" 2>/dev/null
      sleep 5
    fi
  fi
  sleep 2
done
