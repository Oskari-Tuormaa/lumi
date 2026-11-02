#include "hub75_lcd.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <esp_clk_tree.h>
#include <esp_memory_utils.h>
#include <hal/dma_types.h>
#include <hal/gdma_ll.h>
#include <hal/lcd_ll.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_esp32.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hub75_lcd, LOG_LEVEL_INF);

namespace hub75
{

namespace
{

constexpr size_t Width  = CONFIG_LUMI_PANEL_WIDTH;
constexpr size_t Height = CONFIG_LUMI_PANEL_HEIGHT;
constexpr size_t Rows   = Height / 2; // R1G1B1 and R2G2B2 are driven simultaneously
constexpr size_t Planes = CONFIG_HUB75_LCD_BIT_DEPTH;

constexpr uint32_t PclkHz = CONFIG_HUB75_LCD_PCLK_HZ;

static_assert(Height % 2 == 0 && Height <= 64, "Height must be even and <= 64");
static_assert(Rows >= 1 && Rows <= 32, "Unsupported number of scan rows");

// Bits of the 16 bit data word
constexpr uint16_t BitColorMask = 0x003f;
constexpr uint16_t BitLat       = 1 << 6;
constexpr uint16_t BitOe        = 1 << 7;
constexpr unsigned AddrShift    = 8;

constexpr uint16_t OeOff = IS_ENABLED(CONFIG_HUB75_LCD_OE_ACTIVE_LOW) ? BitOe : 0;
constexpr uint16_t OeOn  = IS_ENABLED(CONFIG_HUB75_LCD_OE_ACTIVE_LOW) ? 0 : BitOe;

// Number of clocks OE is asserted after the data of a plane has been latched
constexpr size_t oeClocks(size_t plane)
{
    return static_cast<size_t>(CONFIG_HUB75_LCD_OE_BASE_CLOCKS) << plane;
}

constexpr size_t OePoolWords = oeClocks(Planes - 1);

static_assert(Width * sizeof(uint16_t) <= DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED,
              "Panel row does not fit in one DMA descriptor");
static_assert(OePoolWords * sizeof(uint16_t) <= DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED,
              "OE period does not fit in one DMA descriptor");

// Every (row, plane) segment is: shift `Width` words, then hold OE for oeClocks(plane) words
constexpr size_t DescPerFrame = Rows * Planes * 2;

// LCD_CAM source clock is divided by this before the pixel clock prescaler
constexpr uint32_t LcdClockDivider = 2;

constexpr int TxDmaChannel = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(lcd_cam), tx, channel);

struct Frame
{
    uint16_t         pixels[Rows][Planes][Width];
    dma_descriptor_t desc[DescPerFrame];
};

alignas(4) Frame g_frames[2];
alignas(4) uint16_t g_oePool[Rows][OePoolWords];

// Gamma table, already reduced to `Planes` bits
uint8_t g_gamma[256];

size_t g_active = 0;

const device* g_dmaDev = DEVICE_DT_GET(DT_DMAS_CTLR_BY_NAME(DT_NODELABEL(lcd_cam), tx));

lcd_cam_dev_t* g_lcd  = LCD_LL_GET_HW(0);
gdma_dev_t*    g_gdma = GDMA_LL_GET_HW(0);

constexpr uint32_t gdmaChannel()
{
    return TxDmaChannel / 2;
}

void buildGamma()
{
    constexpr double gamma = CONFIG_HUB75_LCD_GAMMA_X100 / 100.0;
    constexpr double max   = (1u << Planes) - 1;

    for (size_t i = 0; i < 256; i++) {
        g_gamma[i] = static_cast<uint8_t>(std::lround(std::pow(i / 255.0, gamma) * max));
    }
}

void buildOePool()
{
    for (size_t row = 0; row < Rows; row++) {
        for (auto& word : g_oePool[row]) {
            word = static_cast<uint16_t>((row << AddrShift) | OeOn);
        }
    }
}

void initDescriptor(dma_descriptor_t& d, const void* buf, size_t bytes, dma_descriptor_t* next)
{
    d.dw0.size    = bytes;
    d.dw0.length  = bytes;
    d.dw0.owner   = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
    d.dw0.suc_eof = 0;
    d.buffer      = const_cast<void*>(buf);
    d.next        = next;
}

void buildFrame(Frame& f)
{
    for (size_t row = 0; row < Rows; row++) {
        for (size_t plane = 0; plane < Planes; plane++) {
            for (size_t x = 0; x < Width; x++) {
                uint16_t word = static_cast<uint16_t>((row << AddrShift) | OeOff);
                // The panel latches on the falling edge of LAT, so it is raised together
                // with the last pixel and falls with the first word of the display period
                if (x == Width - 1) {
                    word |= BitLat;
                }
                f.pixels[row][plane][x] = word;
            }
        }
    }

    size_t i = 0;
    for (size_t row = 0; row < Rows; row++) {
        for (size_t plane = 0; plane < Planes; plane++) {
            initDescriptor(f.desc[i], f.pixels[row][plane], Width * sizeof(uint16_t),
                           &f.desc[i + 1]);
            i++;
            initDescriptor(f.desc[i], g_oePool[row], oeClocks(plane) * sizeof(uint16_t),
                           &f.desc[i + 1]);
            i++;
        }
    }

    // Loop on itself until another frame is linked in
    f.desc[DescPerFrame - 1].next = &f.desc[0];
}

void convert(Frame& f, std::span<const display::Color> fb)
{
    for (size_t row = 0; row < Rows; row++) {
        const display::Color* top = &fb[row * Width];
        const display::Color* bot = &fb[(row + Rows) * Width];

        for (size_t x = 0; x < Width; x++) {
            const uint8_t r1 = g_gamma[top[x].R];
            const uint8_t g1 = g_gamma[top[x].G];
            const uint8_t b1 = g_gamma[top[x].B];
            const uint8_t r2 = g_gamma[bot[x].R];
            const uint8_t g2 = g_gamma[bot[x].G];
            const uint8_t b2 = g_gamma[bot[x].B];

            for (size_t p = 0; p < Planes; p++) {
                const uint16_t color = ((r1 >> p) & 1) << 0 | ((g1 >> p) & 1) << 1 |
                                       ((b1 >> p) & 1) << 2 | ((r2 >> p) & 1) << 3 |
                                       ((g2 >> p) & 1) << 4 | ((b2 >> p) & 1) << 5;

                uint16_t& word = f.pixels[row][p][x];
                word           = (word & ~BitColorMask) | color;
            }
        }
    }
}

int configureLcd()
{
    uint32_t srcHz = 0;
    int      ret = esp_clk_tree_src_get_freq_hz(static_cast<soc_module_clk_t>(LCD_CLK_SRC_DEFAULT),
                                                ESP_CLK_TREE_SRC_FREQ_PRECISION_APPROX, &srcHz);
    if (ret != ESP_OK) {
        LOG_ERR("Failed to get LCD clock source frequency [%d]", ret);
        return -EINVAL;
    }

    const uint32_t resolutionHz = srcHz / LcdClockDivider;
    const uint32_t prescale     = resolutionHz / PclkHz;
    if (prescale == 0 || prescale > LCD_LL_PCLK_DIV_MAX) {
        LOG_ERR("Unsupported pixel clock %u Hz (LCD clock %u Hz)", PclkHz, resolutionHz);
        return -EINVAL;
    }
    LOG_INF("Pixel clock %u Hz (requested %u Hz)", resolutionHz / prescale, PclkHz);

    lcd_cam_dev_t* dev = g_lcd;

    lcd_ll_enable_clock(dev, true);
    lcd_ll_select_clk_src(dev, LCD_CLK_SRC_DEFAULT);
    lcd_ll_set_group_clock_coeff(dev, LcdClockDivider, 0, 0);
    lcd_ll_set_pixel_clock_prescale(dev, prescale);

    // Data changes on the first half of a PCLK cycle and is stable on the rising edge
    lcd_ll_set_clock_idle_level(dev, false);
    lcd_ll_set_pixel_clock_edge(dev, false);

    lcd_ll_reset(dev);
    lcd_ll_fifo_reset(dev);

    lcd_ll_enable_rgb_mode(dev, false);
    lcd_ll_enable_color_convert(dev, false);
    lcd_ll_set_dma_read_stride(dev, 16);
    lcd_ll_set_data_wire_width(dev, 16);
    lcd_ll_reverse_dma_data_bit_order(dev, false);
    lcd_ll_swap_dma_data_byte_order(dev, false);
    lcd_ll_enable_swizzle(dev, false);
    lcd_ll_set_data_delay_ticks(dev, 0);

    // Data phase only; it never ends as long as the DMA keeps delivering words
    lcd_ll_set_phase_cycles(dev, 0, 0, 1);
    lcd_ll_set_blank_cycles(dev, 0, 0);
    lcd_ll_enable_output_always_on(dev, true);

    lcd_ll_enable_interrupt(dev, LCD_LL_EVENT_TRANS_DONE, false);
    lcd_ll_clear_interrupt_status(dev, LCD_LL_EVENT_TRANS_DONE);

    return 0;
}

int configureDma()
{
    if (!device_is_ready(g_dmaDev)) {
        LOG_ERR("DMA device not ready");
        return -ENODEV;
    }

    // Let the Zephyr GDMA driver claim the channel and route it to LCD_CAM. It can only
    // build linear, EOF terminated descriptor lists, so the real descriptor chain is
    // handed to the hardware directly in start().
    struct dma_block_config block = {};
    block.source_address          = reinterpret_cast<uint32_t>(g_frames[0].pixels);
    block.block_size              = sizeof(uint16_t);

    struct dma_config cfg = {};
    cfg.channel_direction = MEMORY_TO_PERIPHERAL;
    cfg.dma_slot          = ESP_GDMA_TRIG_PERIPH_LCD0;
    cfg.block_count       = 1;
    cfg.head_block        = &block;

    int ret = dma_config(g_dmaDev, TxDmaChannel, &cfg);
    if (ret < 0) {
        LOG_ERR("Failed to configure DMA channel %d [%d]", TxDmaChannel, ret);
        return ret;
    }

    return 0;
}

void start()
{
    gdma_ll_tx_enable_interrupt(g_gdma, gdmaChannel(), GDMA_LL_TX_EVENT_MASK, false);
    gdma_ll_tx_clear_interrupt_status(g_gdma, gdmaChannel(), GDMA_LL_TX_EVENT_MASK);
    gdma_ll_tx_set_desc_addr(g_gdma, gdmaChannel(),
                             reinterpret_cast<uint32_t>(&g_frames[g_active].desc[0]));
    gdma_ll_tx_start(g_gdma, gdmaChannel());

    lcd_ll_fifo_reset(g_lcd);
    lcd_ll_start(g_lcd);
}

bool descriptorInFrame(uint32_t addr, const Frame& f)
{
    const auto base = reinterpret_cast<uint32_t>(&f.desc[0]);
    return addr >= base && addr < base + sizeof(f.desc);
}

} // namespace

int init()
{
    if (!esp_ptr_dma_capable(&g_frames[0]) || !esp_ptr_dma_capable(g_oePool)) {
        LOG_ERR("DMA buffers are not in DMA capable memory");
        return -ENOMEM;
    }

    buildGamma();
    buildOePool();
    buildFrame(g_frames[0]);
    buildFrame(g_frames[1]);

    int ret = configureLcd();
    if (ret < 0) {
        return ret;
    }

    ret = configureDma();
    if (ret < 0) {
        return ret;
    }

    g_active = 0;
    start();

    LOG_INF("HUB75 %zux%zu, %zu scan rows, %zu bit planes, %zu descriptors/frame", Width, Height,
            Rows, Planes, DescPerFrame);
    return 0;
}

void update(std::span<const display::Color> framebuffer)
{
    if (framebuffer.size() < Width * Height) {
        LOG_ERR("Framebuffer too small (%zu < %zu)", framebuffer.size(), Width * Height);
        return;
    }

    Frame& active = g_frames[g_active];
    Frame& idle   = g_frames[g_active ^ 1];

    convert(idle, framebuffer);

    // The DMA follows the link at the end of the active frame, so the idle frame is
    // picked up at the next frame boundary
    active.desc[DescPerFrame - 1].next = &idle.desc[0];
    __sync_synchronize();

    // Once the DMA is fetching descriptors of the new frame, the old one is no longer read
    const int64_t deadline = k_uptime_get() + 100;
    while (!descriptorInFrame(gdma_ll_tx_get_prefetched_desc_addr(g_gdma, gdmaChannel()), idle)) {
        if (k_uptime_get() > deadline) {
            // Leave the link in place, the switch will still happen and the next update
            // renders into the same idle frame again
            LOG_WRN("Timed out waiting for the DMA to switch frames");
            return;
        }
        k_usleep(200);
    }

    active.desc[DescPerFrame - 1].next = &active.desc[0];
    g_active ^= 1;
}

} // namespace hub75
