#include "buttons.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_attr.h"
#include "esp_log.h"

#define BTN_DEBOUNCE_MS      20
#define BTN_DEBOUNCE_POLL_MS 4
#define BTN_LONG_MS          600
#define BTN_POLL_MS          20

static const char *TAG = "Buttons";

static gpio_num_t s_select = GPIO_NUM_NC;
static gpio_num_t s_ok     = GPIO_NUM_NC;

/* Set by the ISR on a press, consumed by Buttons_PressPending(). A real
   interrupt rather than a level poll, so a tap during the awake portion
   of the caller's loop still registers - see the header comment. */
static volatile bool s_press_pending = false;

static void IRAM_ATTR button_isr(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(intptr_t)arg;
    /* Level-triggered (required for light-sleep wakeup, see
       Buttons_EnableWakeup): without this it re-fires continuously for as
       long as the button is held. Buttons_ReadEvent() re-enables both
       pins once the press has been fully drained. */
    gpio_intr_disable(pin);
    s_press_pending = true;
}

void Buttons_Init(gpio_num_t select_pin, gpio_num_t ok_pin)
{
    s_select = select_pin;
    s_ok     = ok_pin;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << select_pin) | (1ULL << ok_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_LOW_LEVEL,
    };
    gpio_config(&cfg);

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
    }
    gpio_isr_handler_add(select_pin, button_isr, (void *)(intptr_t)select_pin);
    gpio_isr_handler_add(ok_pin, button_isr, (void *)(intptr_t)ok_pin);
}

void Buttons_EnableWakeup(void)
{
    gpio_wakeup_enable(s_select, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(s_ok, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

bool Buttons_PressPending(void)
{
    if (!s_press_pending) {
        return false;
    }
    s_press_pending = false;
    return true;
}

btn_event_t Buttons_ReadEvent(void)
{
    /* Debounce by sampling across the settle window rather than trusting
       one snapshot at the end of it: a single sample can land in a
       contact-bounce trough right as the switch settles and misread a
       genuine press as a release, silently dropping the whole event. */
    bool select_down = false;
    bool ok_down      = false;
    for (int waited = 0; waited < BTN_DEBOUNCE_MS; waited += BTN_DEBOUNCE_POLL_MS) {
        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_POLL_MS));
        select_down |= (gpio_get_level(s_select) == 0);
        ok_down     |= (gpio_get_level(s_ok) == 0);
    }
    if (!select_down && !ok_down) {
        gpio_intr_enable(s_select);
        gpio_intr_enable(s_ok);
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

    gpio_intr_enable(s_select);
    gpio_intr_enable(s_ok);

    if (is_select) {
        return is_long ? BTN_EV_SELECT_LONG : BTN_EV_SELECT_SHORT;
    }
    return is_long ? BTN_EV_OK_LONG : BTN_EV_OK_SHORT;
}
