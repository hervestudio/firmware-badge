// Badge-to-badge encounters (ESP-NOW): while the Conf Buddy is displayed,
// the badge sends a broadcast beacon (~1 Hz, channel 1) with its identity
// and listens for the others'. A badge heard with a strong RSSI (= at
// 1-3 m) and not seen recently triggers the buddy reaction (social_ui.h)
// and is added to the encounter log in NVS ("met", for the future social
// screen).
//
// The radio is active ONLY during the Conf Buddy anim (socialStart/Stop
// driven by the loop): no battery impact in menus/games, and no
// interference with the Draw/Setup/OTA WiFi APs.
// Assumed defined before inclusion: qrName, AVATARS/g_avatarIdx,
// g_buddyCustom/g_buddyCustomDef, prefs, Serial0, social_ui.h (via include).
#pragma once
#include <esp_now.h>
#include <esp_wifi.h>
#include "social_ui.h"

#define SOCIAL_CHANNEL 1
// Tight beacon (600 ms): with the radio DUTY CYCLING (see main.cpp), each
// 3 s listening window must contain several transmissions so that two
// badges with desynchronized windows find each other fast (~10 s).
#define SOCIAL_BEACON_MS 600
// Listen duty cycling during Conf Buddy (battery life, review 2026-09-01
// (Romain)): continuous listening costs ~90 mA, the mode's top consumer.
// 3 s of listening per 12 s period = ~75% of the radio budget saved,
// encounter detected in ~10-15 s (imperceptible: people cross paths longer
// than that). The Proximity screen keeps continuous listening for its live
// gauge.
#define SOCIAL_DUTY_ON 3000
#define SOCIAL_DUTY_PERIOD 12000
// ADJUSTABLE proximity threshold (Settings > Proximity, NVS "prox"):
// 4 levels from Touch (badges nearly touching) to Far (~5 m).
// -62 = "Normal" by default.
static int8_t socialRssiNear = -62;  // active value (UI_PROX_LEVELS)
static bool socialProbeOnly = false; // Proximity screen: listen, no reaction
#define SOCIAL_FRESH_MS 2500   // "still here" beacon
#define SOCIAL_COOLDOWN_MS 60000    // per encountered badge
#define SOCIAL_GLOBAL_MS 25000      // between two reactions, all badges
#define SOCIAL_MAXPEERS 40          // the whole series without eviction

struct __attribute__((packed)) SocialBeacon
{
  uint32_t magic; // 'TJC1'
  uint8_t avatar;
  uint8_t face;
  uint8_t cust;
  uint8_t sat100;
  int16_t hue;
  char name[20];
  // best scores (Meet > Leaderboard), LB_GAME_NAMES order: Snake, Pong,
  // Sphere Run, Roundtris. Field ADDED at the end of the packet: old
  // firmwares (short packet) are still accepted, scores set to 0.
  uint16_t scores[LB_GAMES];
};
#define SOCIAL_MAGIC 0x314A4354u // "TCJ1" little-endian
#define SOCIAL_BEACON_V1_LEN offsetof(SocialBeacon, scores)

struct SocialPeer
{
  uint8_t mac[6];
  char name[24];
  uint8_t avatar;
  float rssi; // EMA
  uint32_t lastSeen;
  uint32_t lastReact;
  uint16_t scores[LB_GAMES]; // last announced scores (0 if old firmware)
};

static SocialPeer socialPeers[SOCIAL_MAXPEERS];
static int socialNPeers = 0;
static bool socialOn = false;
static uint32_t socialNextBeacon = 0;
static uint16_t socialMyBest[LB_GAMES]; // my records, cached at socialStart
static portMUX_TYPE socialMux = portMUX_INITIALIZER_UNLOCKED;
static const uint8_t SOCIAL_BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---- Leaderboard: NVS persistence of the lb* tables (lbMerge merge in
// menu_ui.h, shared with the emulator).
// "lb1": "name\tsnake\tpong\trun\ttetris\n".
static void lbSave()
{
  String m;
  char line[64];
  for (int i = 0; i < lbN; i++)
  {
    snprintf(line, sizeof(line), "%s\t%u\t%u\t%u\t%u\n", lbNames[i],
             lbScores[i][0], lbScores[i][1], lbScores[i][2], lbScores[i][3]);
    m += line;
  }
  prefs.putString("lb1", m);
  lbDirty = false;
}

static void lbLoad()
{
  String m = prefs.getString("lb1", "");
  lbN = 0;
  int pos = 0;
  while (pos < (int)m.length() && lbN < MET_MAX)
  {
    int nl = m.indexOf('\n', pos);
    if (nl < 0)
      break;
    int tab = m.indexOf('\t', pos);
    if (tab > pos && tab < nl)
    {
      snprintf(lbNames[lbN], sizeof(lbNames[0]), "%s",
               m.substring(pos, tab).c_str());
      int p2 = tab + 1;
      for (int g = 0; g < LB_GAMES; g++)
      {
        lbScores[lbN][g] = (uint16_t)m.substring(p2, nl).toInt();
        int t2 = m.indexOf('\t', p2);
        if (t2 < 0 || t2 > nl)
          break;
        p2 = t2 + 1;
      }
      lbN++;
    }
    pos = nl + 1;
  }
}

// ESP-NOW callback (WiFi task): updates the table under spinlock, kept short.
static void socialRecvCb(const esp_now_recv_info *info, const uint8_t *data,
                         int len)
{
  if (len < (int)SOCIAL_BEACON_V1_LEN)
    return;
  const SocialBeacon *b = (const SocialBeacon *)data;
  if (b->magic != SOCIAL_MAGIC)
    return;
  bool hasScores = len >= (int)sizeof(SocialBeacon);
  int8_t rssi = info->rx_ctrl ? info->rx_ctrl->rssi : -100;
  uint32_t now = millis();
  portENTER_CRITICAL(&socialMux);
  int idx = -1, oldest = 0;
  for (int i = 0; i < socialNPeers; i++)
  {
    if (memcmp(socialPeers[i].mac, info->src_addr, 6) == 0)
    {
      idx = i;
      break;
    }
    if (socialPeers[i].lastSeen < socialPeers[oldest].lastSeen)
      oldest = i;
  }
  if (idx < 0)
  {
    idx = socialNPeers < SOCIAL_MAXPEERS ? socialNPeers++ : oldest;
    memcpy(socialPeers[idx].mac, info->src_addr, 6);
    socialPeers[idx].rssi = rssi;
    socialPeers[idx].lastReact = 0;
  }
  SocialPeer &p = socialPeers[idx];
  p.rssi = p.rssi * 0.6f + rssi * 0.4f; // smooths the RSSI noise
  p.lastSeen = now;
  p.avatar = b->avatar;
  memcpy(p.name, b->name, sizeof(b->name));
  p.name[sizeof(b->name)] = 0;
  if (hasScores)
    memcpy(p.scores, b->scores, sizeof(p.scores));
  else
    memset(p.scores, 0, sizeof(p.scores));
  portEXIT_CRITICAL(&socialMux);
}

static void socialStart()
{
  if (socialOn)
    return;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // capped TX power: (1) short range wanted for proximity detection,
  // (2) reduces the radio current spike that can drop the power rail
  // (POWERON reset observed on some boards)
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  esp_wifi_set_channel(SOCIAL_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK)
  {
    Serial0.println("social: esp_now_init failed");
    WiFi.mode(WIFI_OFF);
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, SOCIAL_BCAST, 6);
  peer.channel = SOCIAL_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer);
  esp_now_register_recv_cb(socialRecvCb);
  socialNPeers = 0;
  socialNextBeacon = 0;
  // scores announced in the beacon: re-read every time the radio turns on
  // (a game played in between is thus reflected when returning to idle)
  socialMyBest[0] = prefs.getUShort("snakeBest", 0);
  socialMyBest[1] = prefs.getUShort("pongBest", 0);
  socialMyBest[2] = prefs.getUShort("runBest", 0);
  socialMyBest[3] = prefs.getUShort("tetroBest", 0);
  socialOn = true;
  Serial0.println("social: radio ON (Conf Buddy)");
}

static void socialStop()
{
  if (!socialOn)
    return;
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  socialOn = false;
  if (lbDirty)
    lbSave(); // last scores learned during this radio session
  // CLEAN stop: the radio did not kill the badge -> disarm the boot-loop
  // breaker marker (otherwise, leaving idle within 8 s left the marker
  // armed and the next boot wrongly blocked the social feature)
  prefs.putUChar("socboot", 0);
  Serial0.println("social: radio OFF");
}

// Persistence of the encounter counters (shared metNames/metCounts table
// from menu_ui.h) in NVS "met2": lines "name\tcount\n"
static void socialMetSave()
{
  String m;
  for (int i = 0; i < metN; i++)
  {
    m += metNames[i];
    m += '\t';
    m += String(metCounts[i]);
    m += '\n';
  }
  prefs.putString("met2", m);
}

static void socialMetLoad()
{
  String m = prefs.getString("met2", "");
  metN = 0;
  int pos = 0;
  while (pos < (int)m.length() && metN < MET_MAX)
  {
    int nl = m.indexOf('\n', pos);
    if (nl < 0)
      break;
    int tab = m.indexOf('\t', pos);
    if (tab > pos && tab < nl)
    {
      snprintf(metNames[metN], sizeof(metNames[0]), "%s",
               m.substring(pos, tab).c_str());
      metCounts[metN] = (uint16_t)m.substring(tab + 1, nl).toInt();
      metN++;
    }
    pos = nl + 1;
  }
}

// Smoothed RSSI of the most recent/strongest peer (Proximity screen live
// gauge); returns -100 if nobody has been heard for 3 s
static float socialNearestRssi()
{
  float best = -100;
  uint32_t now = millis();
  portENTER_CRITICAL(&socialMux);
  for (int i = 0; i < socialNPeers; i++)
    if (now - socialPeers[i].lastSeen < 3000 && socialPeers[i].rssi > best)
      best = socialPeers[i].rssi;
  portEXIT_CRITICAL(&socialMux);
  return best;
}

// Call every frame while the radio is active: periodic beacon +
// encounter detection (strong RSSI, per-badge cooldown)
static void socialLoop(uint32_t now)
{
  if (!socialOn)
    return;
  if (now >= socialNextBeacon)
  {
    socialNextBeacon = now + SOCIAL_BEACON_MS + (esp_random() & 255);
    SocialBeacon b = {};
    b.magic = SOCIAL_MAGIC;
    b.avatar = g_avatarIdx;
    const AvatarDef &av = g_buddyCustom ? g_buddyCustomDef : AVATARS[g_avatarIdx];
    b.face = av.face;
    b.cust = g_buddyCustom ? 1 : 0;
    b.sat100 = (uint8_t)(av.sat * 100 + 0.5f);
    b.hue = av.hue;
    snprintf(b.name, sizeof(b.name), "%s",
             qrName[0] ? qrName : AVATARS[g_avatarIdx].name);
    memcpy(b.scores, socialMyBest, sizeof(b.scores));
    esp_now_send(SOCIAL_BCAST, (const uint8_t *)&b, sizeof(b));
  }
  // diagnostics: peers heard + smoothed RSSI (every 3 s)
  static uint32_t socialLogMs = 0;
  if (now - socialLogMs > 3000)
  {
    socialLogMs = now;
    // copy under lock, print OUTSIDE the critical section (never touch
    // the UART with interrupts disabled)
    SocialPeer snap[SOCIAL_MAXPEERS];
    int nsnap;
    portENTER_CRITICAL(&socialMux);
    nsnap = socialNPeers;
    memcpy(snap, socialPeers, sizeof(SocialPeer) * nsnap);
    portEXIT_CRITICAL(&socialMux);
    for (int i = 0; i < nsnap; i++)
      if (now - snap[i].lastSeen < 5000)
      {
        Serial0.printf("social: \"%s\" rssi %.0f (threshold %d) seen %lu ms ago\n",
                       snap[i].name, snap[i].rssi, (int)socialRssiNear,
                       (unsigned long)(now - snap[i].lastSeen));
        // leaderboard: merges the announced scores (main task, never in
        // the WiFi callback — NVS stays outside the critical section)
        lbMerge(snap[i].name, snap[i].scores);
      }
    static uint32_t lbSaveMs = 0;
    if (lbDirty && now - lbSaveMs > 30000)
    {
      lbSaveMs = now;
      lbSave();
      Serial0.println("social: leaderboard saved (NVS)");
    }
  }

  // encounter: among the fresh/close/off-cooldown peers, greet THE CLOSEST
  // one (best RSSI), at most one reaction every 25 s — in a cluster of
  // badges the buddy greets calmly instead of chaining reactions
  if (socialProbeOnly) // Proximity screen: listens/sends but never reacts
    return;
  if (now < socialReactUntil)
    return;
  static uint32_t socialLastReact = 0;
  if (socialLastReact && now - socialLastReact < SOCIAL_GLOBAL_MS)
    return;
  char reactName[24] = "";
  float bestRssi = -1000;
  int best = -1;
  portENTER_CRITICAL(&socialMux);
  for (int i = 0; i < socialNPeers; i++)
  {
    SocialPeer &p = socialPeers[i];
    if (now - p.lastSeen < SOCIAL_FRESH_MS && p.rssi > socialRssiNear &&
        (p.lastReact == 0 || now - p.lastReact > SOCIAL_COOLDOWN_MS) &&
        p.rssi > bestRssi)
    {
      bestRssi = p.rssi;
      best = i;
    }
  }
  if (best >= 0)
  {
    socialPeers[best].lastReact = now;
    snprintf(reactName, sizeof(reactName), "%s",
             socialPeers[best].name[0]
                 ? socialPeers[best].name
                 : AVATARS[socialPeers[best].avatar % AVATAR_N].name);
  }
  portEXIT_CRITICAL(&socialMux);
  if (reactName[0])
  {
    socialLastReact = now;
    socialReactTrigger(reactName, now);
    socialMetSave();
    Serial0.printf("social: encounter with \"%s\" (rssi %.0f)\n", reactName,
                   bestRssi);
  }
}
