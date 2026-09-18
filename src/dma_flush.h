// ASYNCHRONOUS screen flush via the ESP-IDF spi_master driver (DMA +
// transaction queue), replacing the blocking Arduino_GFX flush (CPU
// registers). Principle:
//   - panel init is still done by Arduino_GFX (canvas->begin, SPI2);
//   - then dmafInit() takes the pins over via the GPIO matrix on SPI3 and
//     ALL screen traffic goes through here (2A/2B/2C window, pixels, sleep);
//   - pixels leave as DMA chunks from two internal ping-pong buffers: the
//     RGB565 byte swap (SPI = MSB first) of chunk N happens WHILE chunk
//     N-1 is on the wire -> the CPU only pays for the copy (~1 ms/chunk),
//     not the transfer.
// Transactions complete IN QUEUE ORDER: a simple queued/done sequence
// counter is enough to know when a buffer is reusable.
// Assumes defined before inclusion: TFT_SCLK/TFT_MOSI/TFT_CS/TFT_DC,
// SPI_FREQ, W/H, Serial0.
#pragma once
#include <driver/spi_master.h>

#define DMAF_CHUNK_PX 16380 // pixels per transaction (~32 KB, 45 lines)
#define DMAF_QUEUE 8

static spi_device_handle_t dmafDev = nullptr;
static uint16_t *dmafPing[2] = {nullptr, nullptr};
static spi_transaction_t dmafTrans[DMAF_QUEUE];
static int dmafHead = 0, dmafInFlight = 0;
static uint32_t dmafSeqQueued = 0, dmafSeqDone = 0;
static uint32_t dmafBufSeq[2] = {0, 0}; // last transaction of each buffer

// DC driven per transaction: user = level (0 command, 1 data)
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

// copy + byte swap 2 pixels at a time (the panel expects MSB first)
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

// Sends a rectangle (src with stride in pixels). waitEnd = true: blocks
// until the last byte (partial flushes, power-off); false: returns as soon
// as the last chunk is queued (the rest goes out over DMA while the next
// frame is being rendered).
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
    while (dmafSeqDone < dmafBufSeq[b]) // buffer still on the wire
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

// Simple blocking command (0x28 display off / 0x10 sleep in at power-off)
static void dmafCmdBlocking(uint8_t cmd)
{
  dmafCmd(cmd);
  while (dmafInFlight)
    dmafReapOne();
}

// Call ONCE after canvas->begin(): switches the pins over to SPI3;
// the Arduino_GFX bus (SPI2) must never be used again afterwards.
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
    Serial0.println("dmaf: spi_bus_initialize failed");
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
    Serial0.println("dmaf: spi_bus_add_device failed");
    return false;
  }
  for (int i = 0; i < 2; i++)
  {
    dmafPing[i] = (uint16_t *)heap_caps_malloc(DMAF_CHUNK_PX * 2, MALLOC_CAP_DMA);
    if (!dmafPing[i])
    {
      Serial0.println("dmaf: DMA buffer alloc failed");
      return false;
    }
  }
  Serial0.println("dmaf: async DMA flush active (SPI3)");
  return true;
}
