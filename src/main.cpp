#include "display.hpp"
#include "render_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

static constexpr int PanelWidth  = CONFIG_LUMI_PANEL_WIDTH;
static constexpr int PanelHeight = CONFIG_LUMI_PANEL_HEIGHT;

static constexpr int BlobRadius = 20;

static RenderEngine<PanelWidth, PanelHeight> engine;

// Two orbiting blobs, red and green, which turn blue where they overlap, with a soft edge
static void render(std::span<display::Color> fb)
{
    static int t = 0;

    const float cx = PanelWidth / 2.f;
    const float cy = PanelHeight / 2.f;

    const float ax = cx + 10 * std::cos(t / 8.f) - 10;
    const float ay = cy + 10 * std::sin(t / 8.f);
    const float bx = cx + 10 * std::cos(t / 9.f) + 10;
    const float by = cy + 10 * std::sin(t / 9.f);
    t++;

    auto intensity = [](float dist) {
        return static_cast<uint8_t>(255.f * std::clamp(1.f - dist / BlobRadius, 0.f, 1.f));
    };

    for (int y = 0; y < PanelHeight; y++) {
        for (int x = 0; x < PanelWidth; x++) {
            const uint8_t a = intensity(std::abs(ax - x) + std::abs(ay - y));
            const uint8_t b = intensity(std::abs(bx - x) + std::abs(by - y));

            fb[y * PanelWidth + x] = {
                .R = static_cast<uint8_t>(a - std::min(a, b)),
                .G = static_cast<uint8_t>(b - std::min(a, b)),
                .B = std::min(a, b),
            };
        }
    }
}

int main()
{
    LOG_INF("HUB75E LED Matrix Driver - %dx%d", PanelWidth, PanelHeight);

    int res = display::init();
    if (res < 0) {
        LOG_ERR("Failed to initialise display [%d]", res);
        return res;
    }

    engine.setRenderCallback(render);
    engine.run();

    return 0;
}
