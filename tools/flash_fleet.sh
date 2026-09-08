#!/bin/bash
# Flash de flotte OTA — veille le WiFi du Mac : des qu'il obtient une IP en
# 192.168.4.x (= connecte a l'AP d'un badge en mode flash OTA), lance
# l'upload, attend le reboot du badge (IP perdue), se rearme pour le suivant.
# (Detection par IP : macOS masque le SSID sans permission de localisation.)
# La remise a zero memoire est faite PAR le firmware au premier boot
# (RESET_GEN dans main.cpp) — rien d'autre a faire.
#
# Usage :  ./tools/flash_fleet.sh   (depuis la racine du repo, Ctrl-C pour finir)
# Sur chaque badge : maintenir PREV a l'allumage -> mode OTA -> le Mac
# rejoint "badge-threejs" / threejs2026 (auto des le 2e badge) -> flash.
cd "$(dirname "$0")/.." || exit 1
PIO="$HOME/.platformio/penv/bin/pio"
IF=$(networksetup -listallhardwareports 2>/dev/null |
     awk '/Wi-Fi|AirPort/{getline; print $2; exit}')
IF=${IF:-en0}
on_badge() { [[ "$(ipconfig getifaddr "$IF" 2>/dev/null)" == 192.168.4.* ]]; }
count=0
echo "veille sur $IF — mets un badge en mode OTA et connecte-toi a badge-threejs"
while true; do
  if on_badge; then
    echo ""
    echo "=== badge detecte (IP $(ipconfig getifaddr "$IF")) — flash OTA ==="
    if "$PIO" run -e ota -t upload; then
      count=$((count + 1))
      echo "=== OK — badge n°$count flashe, il reboote (attente de deconnexion) ==="
      say "badge $count flashé" 2>/dev/null
      while on_badge; do sleep 2; done
      echo "--- pret pour le badge suivant"
    else
      echo "=== ECHEC — nouvel essai dans 5 s (badge toujours en OTA ?) ==="
      say "échec" 2>/dev/null
      sleep 5
    fi
  fi
  sleep 2
done
