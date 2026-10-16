#include <array>
#include <cmath>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

// Panel dimensions
static constexpr int PANEL_WIDTH  = 128; // Number of columns
static constexpr int PANEL_HEIGHT = 64;  // Number of rows (for 32-row panel)
static constexpr int NUM_ROWS =
    PANEL_HEIGHT / 2; // Number of row pairs (top/bottom displayed simultaneously)

// GPIO pins
static gpio_dt_spec gpio_r1    = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin1), gpios);
static gpio_dt_spec gpio_g1    = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin2), gpios);
static gpio_dt_spec gpio_b1    = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin3), gpios);
static gpio_dt_spec gpio_r2    = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin4), gpios);
static gpio_dt_spec gpio_g2    = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin5), gpios);
static gpio_dt_spec gpio_b2    = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin6), gpios);
static gpio_dt_spec gpio_e     = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin7), gpios);
static gpio_dt_spec gpio_a     = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin8), gpios);
static gpio_dt_spec gpio_b     = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin9), gpios);
static gpio_dt_spec gpio_c     = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin10), gpios);
static gpio_dt_spec gpio_d     = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin11), gpios);
static gpio_dt_spec gpio_clk   = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin12), gpios);
static gpio_dt_spec gpio_latch = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin13), gpios);
static gpio_dt_spec gpio_oe    = GPIO_DT_SPEC_GET(DT_ALIAS(gpiopin14), gpios);

static auto all_gpio = std::to_array<gpio_dt_spec*>(
    {&gpio_r1, &gpio_g1, &gpio_b1, &gpio_r2, &gpio_g2, &gpio_b2, &gpio_e, &gpio_a, &gpio_b, &gpio_c,
     &gpio_d, &gpio_clk, &gpio_latch, &gpio_oe});

// Timing constants for ICN2038S (all in microseconds)
// ICN2038S requires nanosecond-level precision, so we use very small delays
static constexpr uint32_t CLK_HIGH_TIME_US = 1; // Clock high: ~20ns (use 1us for safety)
static constexpr uint32_t CLK_LOW_TIME_US  = 1; // Clock low: ~20ns (use 1us for safety)
static constexpr uint32_t LATCH_SETUP_US   = 1; // Setup time before latch
static constexpr uint32_t OE_BLANKING_US   = 2; // Keep OE high during row transitions

// Simple busy-wait microsecond delay (for nanosecond-precision timing)
static inline void delay_us(uint32_t us)
{
    if (us == 0)
        return;
    // Zephyr k_busy_wait for microsecond precision
    k_busy_wait(us);
}

// Set row address lines (A, B, C, D, E) to select which row pair to display
static inline void set_row_address(uint32_t row)
{
    // row: 0-31 (for 64-row panel) or 0-15 (for 32-row panel)
    gpio_pin_set_dt(&gpio_a, (row >> 0) & 1);
    gpio_pin_set_dt(&gpio_b, (row >> 1) & 1);
    gpio_pin_set_dt(&gpio_c, (row >> 2) & 1);
    gpio_pin_set_dt(&gpio_d, (row >> 3) & 1);
    gpio_pin_set_dt(&gpio_e, (row >> 4) & 1);
}

// Clock pulse: toggle CLK high then low
static inline void clock_pulse()
{
    gpio_pin_set_dt(&gpio_clk, true);
    delay_us(CLK_HIGH_TIME_US);
    gpio_pin_set_dt(&gpio_clk, false);
    delay_us(CLK_LOW_TIME_US);
}

// Set color data pins for one pixel (top and bottom half)
static inline void set_pixel_data(bool r1, bool g1, bool b1, bool r2, bool g2, bool b2)
{
    gpio_pin_set_dt(&gpio_r1, r1);
    gpio_pin_set_dt(&gpio_g1, g1);
    gpio_pin_set_dt(&gpio_b1, b1);
    gpio_pin_set_dt(&gpio_r2, r2);
    gpio_pin_set_dt(&gpio_g2, g2);
    gpio_pin_set_dt(&gpio_b2, b2);
}

// Shift in one row of data and latch it
static void shift_and_latch_row(bool top_r[], bool top_g[], bool top_b[], bool bot_r[],
                                bool bot_g[], bool bot_b[])
{
    // Clock in all 64 pixels
    for (size_t col = 0; col < PANEL_WIDTH; col++) {
        set_pixel_data(top_r[col], top_g[col], top_b[col], bot_r[col], bot_g[col], bot_b[col]);
        delay_us(LATCH_SETUP_US);
        clock_pulse();
    }

    // Disable output during latching to avoid glitches
    gpio_pin_set_dt(&gpio_oe, true);
    delay_us(OE_BLANKING_US);

    // Clock latch
    delay_us(LATCH_SETUP_US);
    gpio_pin_set_dt(&gpio_latch, true);
    delay_us(CLK_HIGH_TIME_US);
    gpio_pin_set_dt(&gpio_latch, false);
    delay_us(CLK_LOW_TIME_US);

    // Re-enable output
    gpio_pin_set_dt(&gpio_oe, false);
    delay_us(OE_BLANKING_US);
}

int main()
{
    int res;

    LOG_INF("HUB75E LED Matrix Driver - ICN2038S");

    // Initialize and configure all GPIO pins
    for (auto gpio : all_gpio) {
        if (!device_is_ready(gpio->port)) {
            LOG_ERR("GPIO %s pin %d not ready!", gpio->port->name, gpio->pin);
            return -1;
        }

        res = gpio_pin_configure_dt(gpio, GPIO_OUTPUT | GPIO_PUSH_PULL);
        if (res < 0) {
            LOG_ERR("Failed to configure GPIO %s pin %d [%d]", gpio->port->name, gpio->pin, res);
            return res;
        }
    }
    LOG_INF("All GPIO pins configured!");

    // Initialize control pins to safe state
    gpio_pin_set_dt(&gpio_oe, true);     // Outputs disabled
    gpio_pin_set_dt(&gpio_latch, false); // Latch off
    gpio_pin_set_dt(&gpio_clk, false);   // Clock off

    LOG_INF("Starting display refresh loop at ~100Hz");
    LOG_INF("Panel: %ux%u (rows: %u pairs)", PANEL_WIDTH, PANEL_HEIGHT, NUM_ROWS);

    int t = 0;
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;

    // Main display refresh loop
    // For proper operation, need to refresh all rows rapidly (~100+ Hz)
    // Each frame shows one row pair at a time, cycling through all rows
    while (1) {
        // Cycle through all row pairs
        for (int row = 0; row < NUM_ROWS; row++) {
            // Set row address
            set_row_address(row);
            delay_us(LATCH_SETUP_US);

            // Create test pattern data for this row
            // Top half (row): Red
            // Bottom half (row + 16): Green
            std::array<bool, PANEL_WIDTH> top_r, top_g, top_b;
            std::array<bool, PANEL_WIDTH> bot_r, bot_g, bot_b;

            // Fill with test pattern
            for (int col = 0; col < PANEL_WIDTH; col++) {
                auto dxa  = abs(a - col);
                auto dyb1 = abs(b - row);
                auto dyb2 = abs(b - (row + NUM_ROWS));

                auto dxc  = abs(c - col);
                auto dyd1 = abs(d - row);
                auto dyd2 = abs(d - (row + NUM_ROWS));

                bool inab1 = (dxa + dyb1) < 20;
                bool inab2 = (dxa + dyb2) < 20;
                bool incd1 = (dxc + dyd1) < 20;
                bool incd2 = (dxc + dyd2) < 20;

                // Top half: red
                top_r[col] = inab1 & !incd1;
                top_g[col] = incd1 & !inab1;
                top_b[col] = inab1 & incd1;

                // Bottom half: green
                bot_r[col] = inab2 & !incd2;
                bot_g[col] = incd2 & !inab2;
                bot_b[col] = inab2 & incd2;
            }

            // Shift in and latch this row
            shift_and_latch_row(top_r.data(), top_g.data(), top_b.data(), bot_r.data(),
                                bot_g.data(), bot_b.data());
        }

        a = PANEL_WIDTH / 2. + 10 * std::cos(t / 8.) - 10;
        b = PANEL_HEIGHT / 2. + 10 * std::sin(t / 8.);
        c = PANEL_WIDTH / 2. + 10 * std::cos(t / 9.) + 10;
        d = PANEL_HEIGHT / 2. + 10 * std::sin(t / 9.);
        t++;
    }

    return 0;
}
