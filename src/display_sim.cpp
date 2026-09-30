#include <cstdlib>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(display);

static const struct device* display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

int run()
{
    display_capabilities capabilities;
    display_buffer_descriptor buf_desc;

    if (!device_is_ready(display)) {
        LOG_ERR("Display not available");
        return -1;
    }

    display_get_capabilities(display, &capabilities);

    size_t number_of_pixels = capabilities.x_resolution * capabilities.y_resolution;

    uint8_t *buf;
    size_t buf_size = number_of_pixels;
    buf_size *= DISPLAY_BITS_PER_PIXEL(capabilities.current_pixel_format);
    buf_size = DIV_ROUND_UP(DIV_ROUND_UP(buf_size, NUM_BITS(uint8_t)), sizeof(uint8_t));

    buf = (uint8_t*)malloc(buf_size);

    if (buf == nullptr)
    {
        LOG_ERR("Failed to allocate");
        return -1;
    }

    for (size_t i = 0; i < number_of_pixels; i++)
    {
        buf[i*4 + 0] = (0xff * (i % capabilities.x_resolution)) / capabilities.x_resolution;
        buf[i*4 + 1] = (0xff * (i / capabilities.x_resolution)) / capabilities.y_resolution;
        buf[i*4 + 2] = 0x00;
        buf[i*4 + 3] = 0xff;
    }

    buf_desc.buf_size = buf_size;
    buf_desc.pitch = capabilities.x_resolution;
    buf_desc.width = capabilities.x_resolution;
    buf_desc.height = capabilities.y_resolution;
    buf_desc.frame_incomplete = false;

    int ret = display_write(display, 0, 0, &buf_desc, buf);
    if (ret < 0) {
        LOG_ERR("Failed to write [%d]", ret);
        return ret;
    }

    ret = display_blanking_off(display);
    if (ret < 0) {
        LOG_ERR("Failed to off blanking [%d]", ret);
        return ret;
    }

    return ret;
}
