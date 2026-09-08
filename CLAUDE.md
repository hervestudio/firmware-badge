# Badge threejs.paris — firmware

Badge électronique des speakers de la conférence **threejs.paris (10-11 sept. 2026)**.
Série de **40 badges** (+10 de marge). Écran rond, jeux, animations, rencontres
entre badges par radio, configuration depuis le téléphone. Le boîtier imprimé et
les plaques de production vivent dans le repo CAD `~/projects/speaker-badge`
(GitHub `hervestudio/speaker-badges`, branche **Latest**).

## Matériel

- **ESP32-S3 N16R8** (devkit) — 16 MB flash QIO, 8 MB PSRAM OPI
- **Écran rond 2,1" GC9B72, 360×360, TFT** (PAS un AMOLED : rétroéclairage
  constant, piloté par GPIO pour l'extinction) sur PCB rond Ø59
- 3 boutons tactiles 6×6 (prev/next/central) + bouton BOOT de la carte
- LiPo + TP4056/boost USB-C ; jauge par pont diviseur 100k/100k
- Câblage **série** (nappe, depuis 2026-08-14) : SCLK 14 / MOSI 13 / DC 11 /
  CS 10 / RST 12 / TE 3 / BL 9 (+4 legacy en parallèle) ; boutons 19/20/21 ;
  VBAT 1 / VBUS 2. Le **premier prototype** (fils volants) a un câblage
  différent → env `proto` (voir plus bas).

## Build & flash

```
pio run -e esp32-s3-devkitc-1 -t upload   # badge série (défaut)
pio run -e ota -t upload                  # flash WiFi : badge en mode OTA, réseau "badge-threejs"
pio run -e proto -t upload                # PREMIER PROTOTYPE uniquement (ancien câblage)
```

- `ARDUINO_USB_CDC_ON_BOOT` désactivé : GPIO 19/20 (D±USB) servent de boutons.
  **Flash et logs passent par le pont UART CH343** (`Serial0`), pas l'USB natif.
- Mode OTA : maintenir PREV (GPIO 20) — ou BOOT — à l'allumage, ou
  Settings > OTA flash mode. Le badge crée l'AP `badge-threejs` / `threejs2026`.
- Boot ~4 s (logo Three.JS Conf + loader) ; textures générées entre les frames
  du splash, tâche de génération épinglée **cœur 1** (TG1WDT sinon).

## Architecture (src/)

- `main.cpp` — orchestrateur : pins, boot, boucle UI (enum `UiMode`), bouton
  BOOT 3 fonctions (court = suivant · 0,5-2 s = central · ≥2 s = extinction),
  batterie, dispatch des anims (table `ACTIVE[]`).
- `dma_flush.h` — flush écran **asynchrone** SPI3+DMA à 80 MHz (queue,
  ping-pong, byte-swap pendant le vol) → ~19 fps. `badgeFlush()` non bloquant.
- `anims_extra.h` — toutes les animations Watch (sphère idle "Conf Buddy",
  Disco, Globe, DVD, Points, Warp, Solar System…). **Portable** : uniquement
  l'API `canvas->` (compilé tel quel par l'émulateur, qui n'a pas tout
  Arduino_GFX — pas de `drawEllipse` par ex.). Sphère : pipeline RGB888 +
  dither ordonné (PAS de RGBX8888 : bande passante PSRAM + crash ipc1).
- `menu_ui.h` — menus + écrans (bulles Play/Watch/Meet/More, listes, Settings,
  Encounters, Leaderboard, Proximity, calibration Batt). **Partagé
  firmware/émulateur** — une seule source pour les deux rendus.
- `games.h` — Snake, Pong, Sphere Run, Roundtris (records NVS). `tama.h` =
  Sphere Pet, retiré du menu mais conservé.
- `social.h` / `social_ui.h` — rencontres ESP-NOW (voir plus bas).
- `qr_screen.h` — Meet > QR Code : QR inversé plein écran (fond noir, modules
  blancs, ECC HIGH, qrcodegen Nayuki vendoré) + médaillon buddy animé.
- `setup_mode.h` — parcours Setup sur téléphone (voir plus bas).
- `draw_mode.h` — dessin collaboratif via WiFi.
- `avatars.h` — 40 avatars (identité + visage), mêmes visages dans Setup et
  Settings.

## Menus

- **Play** : Snake, Pong, Sphere Run, Roundtris (+ Back)
- **Watch** : Conf Buddy, Snake, Disco, Globe, Three Conf, DVD, Points, Warp,
  Solar System — Warp et Solar sont des ports fidèles de
  `~/projects/speaker-badge-anims/experience/src/screen-anims.js`
- **Meet** : Schedule, QR Code, Encounters, Leaderboard
- **More** : Draw (WiFi), Setup (WiFi), Auto cycle, Settings
- **Settings** (code PIN) : Avatar, Proximity, Rotate screen, OTA flash mode,
  Batt (→ écran de **calibration de la jauge**, facteur NVS par badge — les
  ponts 100k ont ±5 % de tolérance). ⚠️ PIN temporaire `00000`, à remettre à
  `39193` avant la série (`UI_PIN_CODE`).

## Social (ESP-NOW)

Radio active UNIQUEMENT pendant l'anim Conf Buddy (et l'écran Proximity en
mode sonde), **et seulement si le badge a une identité** (`bname` non vide,
écrit par Setup ou Settings) : badge non configuré = buddy ALÉATOIRE stable
(NVS `rhue`/`rface`, custom non persisté) et radio muette — pas de détection
anonyme (revue 2026-09-08). Beacon broadcast ~1 Hz canal 1, TX bridée 8,5 dBm (pics de
courant → brownouts sinon). Contenu : identité (nom/avatar/couleur) + les
4 records de jeux (ajout rétro-compatible en fin de paquet).

- Rencontre : meilleur RSSI > seuil (`socialRssiNear`, réglable dans
  Settings > Proximity : Touch −30 / Close −55 / Normal −62 / Far −70),
  cooldown 60 s par badge + 25 s global → réaction du buddy
  (Happy/Wow/Love, gel du regard, rebond, battement de cœur).
- **Leaderboard** : fusion par maximum des scores entendus, par nom
  (`lbMerge`, partagé), NVS throttlée — jamais d'écriture dans le callback
  WiFi, jamais de `printf` sous spinlock (copie puis impression).
- Briseur de boucle `socboot` (NVS) : si le badge meurt < 8 s après
  l'allumage radio, le social est bloqué au boot suivant (les POR effacent la
  RTC, d'où la NVS). Désarmé par `socialStop` propre.

## Setup téléphone (More > Setup)

AP WiFi `badge-<Nom>` / `threejs2026`, page unique gzippée (~136 KB, Dingos +
Inter en base64, source `tools/setupapp_src.html`), WebSocket port 81
(protocole texte N/C/M/A/R/U/B/S). 3 étapes : nom/société/message (15/20
chars, translittération ASCII — les polices badge couvrent 32..126), Conf
Buddy custom (teinte/saturation/visage), URL du QR (préview live sur le
badge, « en construction » par défaut). Portail captif : **répondeur DNS
maison synchrone (WiFiUDP)** — ⚠️ ne JAMAIS réintroduire `DNSServer` (core
3.x = AsyncUDP) ni `ESPmDNS` : reboot à l'ouverture de la fiche réseau iOS.

## NVS (namespace prefs)

`bname bcomp bmsg qrurl` (setup) · `bcust bhue bsat bface` (buddy custom) ·
`avatar` (identité choisie dans Settings) · `prox` (seuil social) · `met2`
(rencontres nom→compte) · `lb1` (leaderboard nom→4 scores) · `socboot`
(briseur de boucle) · `snakeBest pongBest runBest tetroBest` (records) ·
`petFood petFun petNrj` (Sphere Pet) · `rotDeg` (rotation écran) · `vcal`
(calibration jauge, pour-mille).

## Émulateur (tools/emulator/)

Compile les **vraies sources** du firmware en WASM (emscripten, `./build.sh`,
`emu.js` est un artefact ignoré par git). `index.html` : badge 3D + écran
live, `?phone=1|setup` pour les apps WiFi, touche `m` = rencontre simulée
(déclenche réaction + scores pseudo-aléatoires pour le Leaderboard),
localStorage = pseudo-NVS. `headless.js` (node) : séquences de boutons +
captures PPM — utilisé pour vérifier chaque écran sans matériel
(1 = prev, 2 = next, 4 = centre ; boot complet ≈ 160 frames de 50 ms).

## Pièges connus (ne pas re-tomber dedans)

- Écran qui scintille + LED en rythme = **alim marginale** (3V3 < 3,25 V sous
  charge), pas un bug logiciel. QC série : 3V3 ≥ 3,25 V pendant une anim.
  Arbre de diagnostic (revues 2026-09-07/08) : mesurer 5V (VIN) → si < 4,6 V,
  fil boost→devkit à ressouder ; sinon 3V3 sur la BROCHE du devkit, fil écran
  débranché → si < 3,25 V à vide, **régulateur du devkit HS → remplacer la
  carte** (2 cas en série : un régulateur mort à 2,98 V, un module en
  court-circuit brûlant). Double logo de boot = brownout au 1er boot ;
  scintillement qui apparaît PILE sur Conf Buddy = pics de la radio ESP-NOW.
- **QC devkit AVANT soudure** (30 s/carte) : USB branché, 3V3 ≥ 3,25 V au
  multimètre, chip à peine tiède — sinon carte écartée. Évite d'assembler un
  badge complet autour d'un régulateur faiblard.
- Ports USB du devkit : flash/logs par le port **UART (CH343)** uniquement.
  Le port natif = GPIO 19/20 (boutons) : sur un badge aux boutons câblés il
  n'énumère plus, et le trafic USB déclenche le mode OTA au boot.
- `Serial0.printf` interdit sous `portENTER_CRITICAL` (copie d'abord).
- Écritures NVS interdites dans les callbacks WiFi/ESP-NOW.
- Génération lourde sur cœur 0 + rendu PSRAM cœur 1 = TG1WDT sur cartes
  faibles → épingler les tâches de génération au cœur 1.
- SPI 80 MHz validé sur nappe soudée ; repasser 40 MHz si artefacts sur fils
  volants.
- Réveil deep sleep : GPIO 21 réel uniquement.
- En haut de l'écran rond, la corde utile est courte : titres larges → police
  menu et y ≥ 46 (cf. Encounters/Leaderboard).
- Headers partagés avec l'émulateur (`menu_ui.h`, `anims_extra.h`,
  `qr_screen.h`…) : API `canvas->` de base uniquement, pas d'appels
  ESP-IDF/Arduino spécifiques.

## Conventions

- Commentaires en français, sans accents dans les sources C (polices/outils).
- Les décisions et revues sont datées dans les commentaires au plus près du
  code concerné (« revue Romain AAAA-MM-JJ : … ») — c'est l'historique de
  design du projet, le conserver.
