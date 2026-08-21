// Rencontres entre badges (ESP-NOW) : quand le Conf Buddy est affiche, le
// badge diffuse un beacon broadcast (~1 Hz, canal 1) avec son identite et
// ecoute ceux des autres. Un badge recu avec un RSSI fort (= a 1-3 m) et pas
// vu recemment declenche la reaction du buddy (social_ui.h) et s'ajoute au
// journal des rencontres en NVS ("met", pour le futur ecran social).
//
// La radio n'est active QUE pendant l'anim Conf Buddy (socialStart/Stop
// pilotes par la boucle) : pas d'impact batterie dans les menus/jeux, et
// aucune interference avec les AP WiFi de Draw/Setup/OTA.
// Suppose definis avant inclusion : qrName, AVATARS/g_avatarIdx,
// g_buddyCustom/g_buddyCustomDef, prefs, Serial0, social_ui.h (via include).
#pragma once
#include <esp_now.h>
#include <esp_wifi.h>
#include "social_ui.h"

#define SOCIAL_CHANNEL 1
#define SOCIAL_BEACON_MS 1000
#define SOCIAL_RSSI_NEAR (-58) // ~1-3 m (EMA, a calibrer avec 2 badges)
#define SOCIAL_FRESH_MS 2500   // beacon "encore la"
#define SOCIAL_COOLDOWN_MS 60000    // par badge croise
#define SOCIAL_GLOBAL_MS 25000      // entre deux reactions, tous badges
#define SOCIAL_MAXPEERS 40          // toute la serie sans eviction

struct __attribute__((packed)) SocialBeacon
{
  uint32_t magic; // 'TJC1'
  uint8_t avatar;
  uint8_t face;
  uint8_t cust;
  uint8_t sat100;
  int16_t hue;
  char name[20];
};
#define SOCIAL_MAGIC 0x314A4354u // "TCJ1" little-endian

struct SocialPeer
{
  uint8_t mac[6];
  char name[24];
  uint8_t avatar;
  float rssi; // EMA
  uint32_t lastSeen;
  uint32_t lastReact;
};

static SocialPeer socialPeers[SOCIAL_MAXPEERS];
static int socialNPeers = 0;
static bool socialOn = false;
static uint32_t socialNextBeacon = 0;
static portMUX_TYPE socialMux = portMUX_INITIALIZER_UNLOCKED;
static const uint8_t SOCIAL_BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Callback ESP-NOW (tache WiFi) : met a jour la table sous spinlock, court.
static void socialRecvCb(const esp_now_recv_info *info, const uint8_t *data,
                         int len)
{
  if (len < (int)sizeof(SocialBeacon))
    return;
  const SocialBeacon *b = (const SocialBeacon *)data;
  if (b->magic != SOCIAL_MAGIC)
    return;
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
  p.rssi = p.rssi * 0.6f + rssi * 0.4f; // lisse le bruit du RSSI
  p.lastSeen = now;
  p.avatar = b->avatar;
  memcpy(p.name, b->name, sizeof(b->name));
  p.name[sizeof(b->name)] = 0;
  portEXIT_CRITICAL(&socialMux);
}

static void socialStart()
{
  if (socialOn)
    return;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // puissance d'emission bridee : (1) portee courte voulue pour la detection
  // de proximite, (2) reduit le pic de courant radio qui peut faire chuter
  // le rail d'alim (reset POWERON observe sur certaines cartes)
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  esp_wifi_set_channel(SOCIAL_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK)
  {
    Serial0.println("social : esp_now_init KO");
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
  socialOn = true;
  Serial0.println("social : radio ON (Conf Buddy)");
}

static void socialStop()
{
  if (!socialOn)
    return;
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  socialOn = false;
  // arret PROPRE : la radio n'a pas tue le badge -> desarme le marqueur du
  // briseur de boucle (sinon, quitter l'idle dans les 8 s laissait le
  // marqueur arme et le boot suivant bloquait le social a tort)
  prefs.putUChar("socboot", 0);
  Serial0.println("social : radio OFF");
}

// Persistance des compteurs de rencontres (table partagee metNames/metCounts
// de menu_ui.h) en NVS "met2" : lignes "nom\tcompte\n"
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

// A appeler chaque frame quand la radio est active : beacon periodique +
// detection de rencontre (RSSI fort, cooldown par badge)
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
    esp_now_send(SOCIAL_BCAST, (const uint8_t *)&b, sizeof(b));
  }
  // diagnostic : pairs entendus + RSSI lisse (toutes les 3 s)
  static uint32_t socialLogMs = 0;
  if (now - socialLogMs > 3000)
  {
    socialLogMs = now;
    // copie sous verrou, impression HORS section critique (jamais d'UART
    // avec les interruptions coupees)
    SocialPeer snap[SOCIAL_MAXPEERS];
    int nsnap;
    portENTER_CRITICAL(&socialMux);
    nsnap = socialNPeers;
    memcpy(snap, socialPeers, sizeof(SocialPeer) * nsnap);
    portEXIT_CRITICAL(&socialMux);
    for (int i = 0; i < nsnap; i++)
      if (now - snap[i].lastSeen < 5000)
        Serial0.printf("social : \"%s\" rssi %.0f (seuil %d) vu il y a %lu ms\n",
                       snap[i].name, snap[i].rssi, SOCIAL_RSSI_NEAR,
                       (unsigned long)(now - snap[i].lastSeen));
  }

  // rencontre : parmi les pairs frais/proches/hors cooldown, on salue LE
  // PLUS PROCHE (meilleur RSSI), au plus une reaction toutes les 25 s — dans
  // une grappe de badges, le buddy salue calmement au lieu d'enchainer
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
    if (now - p.lastSeen < SOCIAL_FRESH_MS && p.rssi > SOCIAL_RSSI_NEAR &&
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
    Serial0.printf("social : rencontre avec \"%s\" (rssi %.0f)\n", reactName,
                   bestRssi);
  }
}
