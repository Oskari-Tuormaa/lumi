#pragma once

#include "display.hpp"

#include <functional>
#include <span>
#include <utility>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

static constexpr int32_t RenderPeriodMs = 1000 / 60;

template <size_t W, size_t H>
class RenderEngine
{
  public:
    using RenderCallback = std::function<void(std::span<display::Color>)>;

    void setRenderCallback(RenderCallback renderCallback)
    {
        m_renderCallback = renderCallback;
    }

    void run()
    {
        LOG_MODULE_DECLARE(render_engine);

        auto bufA = &m_buf1;
        auto bufB = &m_buf2;

        while (true) {
            int32_t t0 = k_cyc_to_ms_ceil32(k_cycle_get_32());
            if (m_renderCallback) {
                m_renderCallback(*bufA);
                display::update_frame(*bufA);
            }

            std::swap(bufA, bufB);

            int32_t t1        = k_cyc_to_ms_ceil32(k_cycle_get_32());
            int32_t msToSleep = RenderPeriodMs - (t1 - t0);
            if (msToSleep > 0) {
                k_msleep(msToSleep);
            }
        }
    }

  private:
    std::array<display::Color, W * H> m_buf1{}, m_buf2{};
    RenderCallback                    m_renderCallback;
};
