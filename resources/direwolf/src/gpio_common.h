#ifndef GPIO_COMMON_H
#define GPIO_COMMON_H

#if USE_GPIOD		// Skip all if no libgpiod available

#if LIBGPIOD_VERSION_MAJOR >= 2		// Someday we might have v1 and v2 sections

#ifdef __cplusplus
extern "C" {
#endif

#include <gpiod.h>
#include <stdint.h>

// Types
typedef uint16_t gpio_num_t;

// Return values
#define GPIO_COMMON_UNKNOWN           UINT16_MAX
#define GPIO_COMMON_OK                0
#define GPIO_COMMON_ERR               -1


// Public functions

void gpio_common_init(void);
gpio_num_t gpio_common_open_line(const char *chip_name, unsigned int line, bool active_low);
int gpio_common_close(void);
int gpio_common_set(gpio_num_t gpio_num, bool val);

#ifdef __cplusplus
}
#endif

#endif		// LIBGPIOD_VERSION_MAJOR >= 2

#endif 		// USE_GPIOD

#endif // GPIO_COMMON_H
