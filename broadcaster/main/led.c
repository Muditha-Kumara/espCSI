/*
 * LED Status Indicator - Broadcaster
 */

#include "led.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "led";

static esp_timer_handle_t s_blink_timer = NULL;
static bool s_led_state = false;
static int s_on_ms = 0;
static int s_off_ms = 0;
static bool s_blink_phase = false;

static void blink_timer_cb(void *arg)
{
    s_blink_phase = !s_blink_phase;
    if (s_blink_phase) {
        gpio_set_level(CONFIG_LED_GPIO, 1);
        esp_timer_start_once(s_blink_timer, s_on_ms * 1000);
    } else {
        gpio_set_level(CONFIG_LED_GPIO, 0);
        esp_timer_start_once(s_blink_timer, s_off_ms * 1000);
    }
}

esp_err_t led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LED GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(CONFIG_LED_GPIO, 0);
    s_led_state = false;

    const esp_timer_create_args_t timer_args = {
        .callback = blink_timer_cb,
        .name = "led_blink",
    };
    ret = esp_timer_create(&timer_args, &s_blink_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create blink timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LED initialized on GPIO %d", CONFIG_LED_GPIO);
    return ESP_OK;
}

void led_on(void)
{
    led_stop_blink();
    gpio_set_level(CONFIG_LED_GPIO, 1);
    s_led_state = true;
}

void led_off(void)
{
    led_stop_blink();
    gpio_set_level(CONFIG_LED_GPIO, 0);
    s_led_state = false;
}

void led_toggle(void)
{
    led_stop_blink();
    s_led_state = !s_led_state;
    gpio_set_level(CONFIG_LED_GPIO, s_led_state ? 1 : 0);
}

void led_blink(int on_ms, int off_ms)
{
    if (s_blink_timer == NULL) return;

    esp_timer_stop(s_blink_timer);
    s_on_ms = on_ms;
    s_off_ms = off_ms;
    s_blink_phase = false;

    gpio_set_level(CONFIG_LED_GPIO, 1);
    s_blink_phase = true;
    esp_timer_start_once(s_blink_timer, s_on_ms * 1000);
}

void led_stop_blink(void)
{
    if (s_blink_timer != NULL) {
        esp_timer_stop(s_blink_timer);
    }
    gpio_set_level(CONFIG_LED_GPIO, 0);
    s_led_state = false;
}
