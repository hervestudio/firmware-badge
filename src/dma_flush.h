// Flush ecran ASYNCHRONE via le driver spi_master d'ESP-IDF (DMA + file de
// transactions), en remplacement du flush bloquant d'Arduino_GFX (registres
// CPU). Principe :
//   - l'init du panneau reste faite par Arduino_GFX (canvas->begin, SPI2) ;
//   - ensuite dmafInit() prend les broches via la matrice GPIO sur SPI3 et
//     TOUT le trafic ecran passe par ici (fenetre 2A/2B/2C, pixels, sleep) ;
//   - les pixels partent par chunks DMA depuis deux tampons internes en
//     ping-pong : le swap d'octets RGB565 (SPI = MSB first) du chunk N se
//     fait PENDANT que le chunk N-1 est sur le fil -> le CPU ne paie que la
//     copie (~1 ms/chunk), pas le transfert.
// Les transactions se terminent DANS L'ORDRE de la file : un simple compteur
// sequence queued/done suffit pour savoir quand un tampon est reutilisable.
// Suppose definis avant inclusion : TFT_SCLK/TFT_MOSI/TFT_CS/TFT_DC,
// SPI_FREQ, W/H, Serial0.
#pragma once
#include <driver/spi_master.h>

#define DMAF_CHUNK_PX 16380 // pixels par transaction (~32 Ko, 45 lignes)
#define DMAF_QUEUE 8

static spi_device_handle_t dmafDev = nullptr;
static uint16_t *dmafPing[2] = {nullptr, nullptr};
static spi_transaction_t dmafTrans[DMAF_QUEUE];
static int dmafHead = 0, dmafInFlight = 0;
static uint32_t dmafSeqQueued = 0, dmafSeqDone = 0;
static uint32_t dmafBufSeq[2] = {0, 0}; // derniere transaction de chaque tampon

// DC pilote par transaction : user = niveau (0 commande, 1 donnees)
static void IRAM_ATTR dmafPreCb(spi_transaction_t *t)
{
  gpio_set_level((gpio_num_t)TFT_DC, (int)(intptr_t)t->user);
}

static void dmafReapOne()
{
  spi_transaction_t *done;
  spi_device_get_trans_result(dmafDev, &done, portMAX_DELAY);
  dmafInFlight--;
  dmafSeqDone++;
}

static spi_transaction_t *dmafNextTrans()
{
  if (dmafInFlight >= DMAF_QUEUE)
    dmafReapOne();
  spi_transaction_t *t = &dmafTrans[dmafHead];
  dmafHead = (dmafHead + 1) % DMAF_QUEUE;
  memset(t, 0, sizeof(*t));
  return t;
}

static void dmafQueue(spi_transaction_t *t)
{
  spi_device_queue_trans(dmafDev, t, portMAX_DELAY);
  dmafInFlight++;
  dmafSeqQueued++;
}

static void dmafCmd(uint8_t cmd)
{
  spi_transaction_t *t = dmafNextTrans();
  t->flags = SPI_TRANS_USE_TXDATA;
  t->length = 8;
  t->tx_data[0] = cmd;
  t->user = (void *)0;
  dmafQueue(t);
}

static void dmafData4(const uint8_t *d, int n) // n <= 4
{
  spi_transaction_t *t = dmafNextTrans();
  t->flags = SPI_TRANS_USE_TXDATA;
  t->length = 8 * n;
  memcpy(t->tx_data, d, n);
  t->user = (void *)1;
  dmafQueue(t);
}

static void dmafWindow(int x, int y, int w, int h)
{
  uint8_t ca[4] = {(uint8_t)(x >> 8), (uint8_t)x,
                   (uint8_t)((x + w - 1) >> 8), (uint8_t)(x + w - 1)};
  uint8_t ra[4] = {(uint8_t)(y >> 8), (uint8_t)y,
                   (uint8_t)((y + h - 1) >> 8), (uint8_t)(y + h - 1)};
  dmafCmd(0x2A); // CASET
  dmafData4(ca, 4);
  dmafCmd(0x2B); // RASET
  dmafData4(ra, 4);
  dmafCmd(0x2C); // RAMWR
}

// copie + swap d'octets 2 pixels a la fois (le panneau attend du MSB first)
static void dmafSwapCopy(uint16_t *dst, const uint16_t *src, int npx)
{
  const uint32_t *s = (const uint32_t *)src;
  uint32_t *d = (uint32_t *)dst;
  int n = npx >> 1;
  for (int i = 0; i < n; i++)
  {
    uint32_t v = s[i];
    d[i] = ((v >> 8) & 0x00FF00FFu) | ((v << 8) & 0xFF00FF00u);
  }
  if (npx & 1)
    dst[npx - 1] = __builtin_bswap16(src[npx - 1]);
}

// Envoie un rectangle (src avec stride en pixels). waitEnd = true : bloque
// jusqu'au dernier octet (flush partiels, extinction) ; false : rend la main
// des le dernier chunk mis en file (le reste part en DMA pendant le rendu de
// la frame suivante).
static void dmafFlush(int x, int y, int w, int h, const uint16_t *src,
                      int stride, bool waitEnd)
{
  dmafWindow(x, y, w, h);
  int rowsPerChunk = DMAF_CHUNK_PX / w;
  if (rowsPerChunk < 1)
    rowsPerChunk = 1;
  int b = 0, row = 0;
  while (row < h)
  {
    int rows = h - row < rowsPerChunk ? h - row : rowsPerChunk;
    int npx = rows * w;
    while (dmafSeqDone < dmafBufSeq[b]) // tampon encore sur le fil
      dmafReapOne();
    if (stride == w)
      dmafSwapCopy(dmafPing[b], src + row * stride, npx);
    else
      for (int r = 0; r < rows; r++)
        dmafSwapCopy(dmafPing[b] + r * w, src + (row + r) * stride + 0, w);
    spi_transaction_t *t = dmafNextTrans();
    t->length = (size_t)npx * 16;
    t->tx_buffer = dmafPing[b];
    t->user = (void *)1;
    dmafQueue(t);
    dmafBufSeq[b] = dmafSeqQueued;
    b ^= 1;
    row += rows;
  }
  if (waitEnd)
    while (dmafInFlight)
      dmafReapOne();
}

// Commande simple bloquante (0x28 display off / 0x10 sleep in a l'extinction)
static void dmafCmdBlocking(uint8_t cmd)
{
  dmafCmd(cmd);
  while (dmafInFlight)
    dmafReapOne();
}

// A appeler UNE fois apres canvas->begin() : bascule les broches sur SPI3 ;
// le bus Arduino_GFX (SPI2) ne doit plus jamais etre utilise ensuite.
static bool dmafInit()
{
  spi_bus_config_t bus = {};
  bus.mosi_io_num = TFT_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = TFT_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = DMAF_CHUNK_PX * 2 + 8;
  if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK)
  {
    Serial0.println("dmaf : spi_bus_initialize KO");
    return false;
  }
  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = SPI_FREQ;
  dev.mode = 0;
  dev.spics_io_num = TFT_CS;
  dev.queue_size = DMAF_QUEUE;
  dev.pre_cb = dmafPreCb;
  if (spi_bus_add_device(SPI3_HOST, &dev, &dmafDev) != ESP_OK)
  {
    Serial0.println("dmaf : spi_bus_add_device KO");
    return false;
  }
  for (int i = 0; i < 2; i++)
  {
    dmafPing[i] = (uint16_t *)heap_caps_malloc(DMAF_CHUNK_PX * 2, MALLOC_CAP_DMA);
    if (!dmafPing[i])
    {
      Serial0.println("dmaf : alloc tampon DMA KO");
      return false;
    }
  }
  Serial0.println("dmaf : flush DMA asynchrone actif (SPI3)");
  return true;
}
