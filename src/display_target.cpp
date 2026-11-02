#include "display.hpp"
#include "hub75_lcd.hpp"

namespace display
{

int init()
{
    return hub75::init();
}

void update_frame(std::span<Color> framebuffer)
{
    hub75::update(framebuffer);
}

} // namespace display
