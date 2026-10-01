#pragma once

#include <cstdint>
#include <span>

namespace display
{

struct Color
{
    uint8_t R;
    uint8_t G;
    uint8_t B;
};

int  init();
void update_frame(std::span<Color> framebuffer);

} // namespace display
