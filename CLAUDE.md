# threejs.paris badge — firmware

Electronic badge for the speakers of the **threejs.paris conference
(Sept 10-11, 2026)**. Run of **40 badges** (+10 spares). Round screen, games,
animations, badge-to-badge encounters over radio, configuration from the
phone. The printed enclosure and production plates live in the CAD repo
`~/projects/speaker-badge` (GitHub `hervestudio/speaker-badges`, branch
**Latest**).

## Hardware

- **ESP32-S3 N16R8** (devkit) — 16 MB QIO flash, 8 MB OPI PSRAM
- **Round 2.1" GC9B72 screen, 360×360, TFT** (NOT an AMOLED: constant
  backlight, driven by GPIO for turning it off) on a round Ø59 PCB
- 3 tactile 6×6 buttons (prev/next/center) + the board's BOOT button
- LiPo + TP4056/boost USB-C; battery gauge via 100k/100k divider bridge
- **Production** wiring (ribbon, since 2026-08-14): SCLK 14 / MOSI 13 /
  DC 11 / CS 10 / RST 12 / TE 3 / BL 9 (+4 legacy in parallel); buttons
  19/20/21; VBAT 1 / VBUS 2. The **first prototype** (loose wires) has a
  different wiring → env `proto` (see below).

## Build & flash

```
pio run -e esp32-s3-devkitc-1 -t upload   # production badge (default)
pio run -e ota -t upload                  # WiFi flash: badge in OTA mode, network "badge-threejs"
pio run -e proto -t upload                # FIRST PROTOTYPE only (old wiring)
```

- `ARDUINO_USB_CDC_ON_BOOT` disabled: GPIO 19/20 (USB D±) are used as
  buttons. **Flash and logs go through the CH343 UART bridge** (`Serial0`),
  not native USB.
- OTA mode: hold PREV (GPIO 20) — or BOOT — at power-on, or
  Settings > OTA flash mode. The badge creates the AP `badge-threejs` /
  `threejs2026`.
- Boot ~4 s (Three.JS Conf logo + loader); textures generated between
  splash frames, generation task pinned to **core 1** (TG1WDT otherwise).

## Architecture (src/)

- `main.cpp` — orchestrator: pins, boot, UI loop (`UiMode` enum), BOOT
  button with 3 functions (short = next · 0.5-2 s = center · ≥2 s = power
  off), battery, anim dispatch (`ACTIVE[]` table).
- `dma_flush.h` — **asynchronous** screen flush, SPI3+DMA at 80 MHz (queue,
  ping-pong, byte-swap in flight) → ~19 fps. `badgeFlush()` non-blocking.
- `anims_extra.h` — all the Watch animations (idle "Conf Buddy" sphere,
  Disco, Globe, DVD, Points, Warp, Solar System…). **Portable**: only the
  `canvas->` API (compiled as-is by the emulator, which does not have all
  of Arduino_GFX — no `drawEllipse` for example). Sphere: RGB888 pipeline +
  ordered dither (NO RGBX8888: PSRAM bandwidth + ipc1 crash).
- `menu_ui.h` — menus + screens (Play/Watch/Meet/More bubbles, lists,
  Settings, Encounters, Leaderboard, Proximity, Batt calibration).
  **Shared firmware/emulator** — a single source for both renderers.
- `games.h` — Snake, Pong, Sphere Run, Roundtris (NVS records). `tama.h` =
  Sphere Pet, removed from the menu but kept.
- `social.h` / `social_ui.h` — ESP-NOW encounters (see below).
- `qr_screen.h` — Meet > QR Code: inverted full-screen QR (black
  background, white modules, ECC HIGH, vendored Nayuki qrcodegen) +
  animated buddy medallion.
- `setup_mode.h` — phone Setup flow (see below).
- `draw_mode.h` — collaborative drawing over WiFi.
- `avatars.h` — 40 avatars (identity + face), same faces in Setup and
  Settings.

## Menus

- **Play**: Snake, Pong, Sphere Run, Roundtris (+ Back)
- **Watch**: Conf Buddy, Snake, Disco, Globe, Three Conf, DVD, Points,
  Warp, Solar System — Warp and Solar are faithful ports of
  `~/projects/speaker-badge-anims/experience/src/screen-anims.js`
- **Meet**: Schedule, QR Code, Encounters, Leaderboard
- **More**: Draw (WiFi), Setup (WiFi), Auto cycle, Settings
- **Settings** (PIN code): Avatar, Proximity, Rotate screen, OTA flash
  mode, Batt (→ **gauge calibration** screen, per-badge NVS factor — the
  100k bridges have ±5 % tolerance). Final PIN: **2010** (year three.js
  was created, `UI_PIN_CODE` in menu_ui.h).

## Social (ESP-NOW)

Radio active ONLY during the Conf Buddy anim (and the Proximity screen in
probe mode), **and only if the badge has an identity** (`bname` non-empty,
written by Setup or Settings): unconfigured badge = stable RANDOM buddy
(NVS `rhue`/`rface`, custom not persisted) and silent radio — no anonymous
detection (review 2026-09-08). Broadcast beacon ~1 Hz on channel 1, TX
capped at 8.5 dBm (current spikes → brownouts otherwise). Payload: identity
(name/avatar/color) + the 4 game records (backward-compatible addition at
the end of the packet).

- Encounter: best RSSI > threshold (`socialRssiNear`, adjustable in
  Settings > Proximity: Touch −30 / Close −55 / Normal −62 / Far −70),
  60 s cooldown per badge + 25 s global → buddy reaction
  (Happy/Wow/Love, gaze freeze, bounce, heartbeat).
- **Leaderboard**: merge-by-maximum of heard scores, keyed by name
  (`lbMerge`, shared), throttled NVS — never a write in the WiFi
  callback, never a `printf` under a spinlock (copy then print).
- `socboot` loop breaker (NVS): if the badge dies < 8 s after the radio
  turns on, social is blocked at the next boot (PORs wipe the RTC, hence
  NVS). Disarmed by a clean `socialStop`.

## Phone Setup (More > Setup)

WiFi AP `badge-<Name>` / `threejs2026`, single gzipped page (~136 KB,
Dingos + Inter in base64, source `tools/setupapp_src.html`), WebSocket on
port 81 (text protocol N/C/M/A/R/U/B/S). 3 steps: name/company/message
(15/20 chars, ASCII transliteration — the badge fonts cover 32..126),
custom Conf Buddy (hue/saturation/face), QR URL (live preview on the
badge, "under construction" by default). Captive portal: **homemade
synchronous DNS responder (WiFiUDP)** — ⚠️ NEVER reintroduce `DNSServer`
(core 3.x = AsyncUDP) nor `ESPmDNS`: reboot when opening the iOS network
details sheet.

## NVS (prefs namespace)

`bname bcomp bmsg qrurl` (setup) · `bcust bhue bsat bface` (custom buddy) ·
`avatar` (identity chosen in Settings) · `prox` (social threshold) · `met2`
(encounters name→count) · `lb1` (leaderboard name→4 scores) · `socboot`
(loop breaker) · `snakeBest pongBest runBest tetroBest` (records) ·
`petFood petFun petNrj` (Sphere Pet) · `rotDeg` (screen rotation) · `vcal`
(gauge calibration, per-mille).

## Emulator (tools/emulator/)

Compiles the **real firmware sources** to WASM (emscripten, `./build.sh`,
`emu.js` is a git-ignored artifact). `index.html`: 3D badge + live screen,
`?phone=1|setup` for the WiFi apps, key `m` = simulated encounter
(triggers a reaction + pseudo-random scores for the Leaderboard),
localStorage = pseudo-NVS. `headless.js` (node): button sequences + PPM
captures — used to check every screen without hardware
(1 = prev, 2 = next, 4 = center; full boot ≈ 160 frames of 50 ms).

## Known pitfalls (do not fall into them again)

- Flickering screen + LED pulsing in sync = **marginal power supply**
  (3V3 < 3.25 V under load), not a software bug. Production QC: 3V3 ≥
  3.25 V during an anim. Diagnostic tree (reviews 2026-09-07/08): measure
  5V (VIN) → if < 4.6 V, resolder the boost→devkit wire; otherwise 3V3 on
  the devkit PIN, screen wire unplugged → if < 3.25 V unloaded, **devkit
  regulator dead → replace the board** (2 cases in the run: one regulator
  dead at 2.98 V, one module shorted and burning hot). Double boot logo =
  brownout on 1st boot; flicker that appears EXACTLY on Conf Buddy =
  ESP-NOW radio spikes.
- **Devkit QC BEFORE soldering** (30 s/board): USB plugged in, 3V3 ≥
  3.25 V on the multimeter, chip barely warm — otherwise reject the board.
  Avoids assembling a complete badge around a weak regulator.
- Devkit USB ports: flash/logs via the **UART (CH343) port** only. The
  native port = GPIO 19/20 (buttons): on a badge with wired buttons it no
  longer enumerates, and USB traffic triggers OTA mode at boot.
- `Serial0.printf` forbidden under `portENTER_CRITICAL` (copy first).
- NVS writes forbidden in WiFi/ESP-NOW callbacks.
- Heavy generation on core 0 + PSRAM rendering on core 1 = TG1WDT on weak
  boards → pin generation tasks to core 1.
- SPI 80 MHz validated on a soldered ribbon; drop back to 40 MHz if
  artifacts on loose wires.
- Deep sleep wake-up: real GPIO 21 only.
- At the top of the round screen the usable chord is short: wide titles →
  menu font and y ≥ 46 (cf. Encounters/Leaderboard).
- Headers shared with the emulator (`menu_ui.h`, `anims_extra.h`,
  `qr_screen.h`…): basic `canvas->` API only, no ESP-IDF/Arduino-specific
  calls.

## Conventions

- Comments in English, ASCII only in the C sources (fonts/tools).
- Decisions and reviews are dated in comments right next to the code they
  concern ("review YYYY-MM-DD (Romain): …") — this is the project's design
  history, keep it.
