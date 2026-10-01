#include "display.hpp"
#include "render_engine.hpp"

#include <cmath>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

static constexpr size_t W = 64;
static constexpr size_t H = 32;

int main()
{
    if (display::init() < 0) {
        LOG_ERR("Failed to initialize display");
        return -1;
    }

    RenderEngine<W, H> engine;

    int a = 0;
    engine.setRenderCallback([&a](auto buf) {
        for (size_t y = 0; y < H; y++) {
            uint8_t G = 0xff * (H * (0.5 + 0.5 * sin(a / 10.)) + y) / H;

            for (size_t x = 0; x < W; x++) {
                size_t  i = x + y * W;
                uint8_t R = 0xff * (W * (0.5 + 0.5 * sin(a / 11.)) + x) / W;
                buf[i]    = display::Color(R, G, 0);
            }
        }
        a++;
    });
    engine.run();
}
