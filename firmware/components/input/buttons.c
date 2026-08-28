#include "buttons.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"

#define BTN_DEBOUNCE_MS 20
#define BTN_LONG_MS     600
#define BTN_POLL_MS     20

static gpio_num_t s_select = GPIO_NUM_NC;
static gpio_num_t s_ok     = GPIO_NUM_NC;

void Buttons_Init(gpio_num_t select_pin, gpio_num_t ok_pin)
{
    s_select = select_pin;
    s_ok     = ok_pin;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << select_pin) | (1ULL << ok_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

void Buttons_EnableWakeup(void)
{
    gpio_wakeup_enable(s_select, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(s_ok, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

btn_event_t Buttons_ReadEvent(void)
{
    vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));

    bool select_down = gpio_get_level(s_select) == 0;
    bool ok_down     = gpio_get_level(s_ok) == 0;
    if (!select_down && !ok_down) {
        return BTN_EV_NONE;   /* bounce or a wake from something else */
    }

    /* If somehow both are down, Select wins. */
    const bool       is_select = select_down;
    const gpio_num_t pin       = is_select ? s_select : s_ok;

    int held_ms = BTN_DEBOUNCE_MS;
    while (gpio_get_level(pin) == 0 && held_ms < BTN_LONG_MS) {
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
        held_ms += BTN_POLL_MS;
    }

    const bool is_long = held_ms >= BTN_LONG_MS;

    /* Drain the remainder of the press. */
    while (gpio_get_level(pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }

    if (is_select) {
        return is_long ? BTN_EV_SELECT_LONG : BTN_EV_SELECT_SHORT;
    }
    return is_long ? BTN_EV_OK_LONG : BTN_EV_OK_SHORT;
}
