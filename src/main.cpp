#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

int main()
{
    LOG_INF("Hello, world!");

    while (true)
    {
        k_msleep(1000);
        LOG_INF("Hi!");
    }
}
