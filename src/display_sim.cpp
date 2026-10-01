#include "display.hpp"

#include <cstdlib>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display);

static const struct device* display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static display_buffer_descriptor buf_desc;
static display_capabilities      capabilities;

static size_t number_of_pixels;
static size_t buf_size;

static uint8_t* output_buf;

namespace display
{

int init()
{
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display not available");
        return -1;
    }

    display_get_capabilities(display_dev, &capabilities);

    number_of_pixels = capabilities.x_resolution * capabilities.y_resolution;

    buf_size = number_of_pixels;
    buf_size *= DISPLAY_BITS_PER_PIXEL(capabilities.current_pixel_format);
    buf_size = DIV_ROUND_UP(DIV_ROUND_UP(buf_size, NUM_BITS(uint8_t)), sizeof(uint8_t));

    buf_desc.buf_size         = buf_size;
    buf_desc.width            = capabilities.x_resolution;
    buf_desc.height           = capabilities.y_resolution;
    buf_desc.pitch            = capabilities.x_resolution;
    buf_desc.frame_incomplete = false;

    output_buf = (uint8_t*) malloc(buf_size);
    if (output_buf == nullptr) {
        LOG_ERR("Failed to allocate");
        return -1;
    }

    return 0;
}

void update_frame(std::span<Color> framebuffer)
{
    for (size_t i = 0; i < framebuffer.size(); i++) {
        Color c               = framebuffer[i];
        output_buf[i * 4 + 0] = c.B;
        output_buf[i * 4 + 1] = c.G;
        output_buf[i * 4 + 2] = c.R;
        output_buf[i * 4 + 3] = 0xff;
    }

    int ret = display_write(display_dev, 0, 0, &buf_desc, output_buf);
    if (ret < 0) {
        LOG_ERR("Failed to write [%d]", ret);
    }

    ret = display_blanking_off(display_dev);
    if (ret < 0) {
        LOG_ERR("Failed to blanking off [%d]", ret);
    }
}

} // namespace display
