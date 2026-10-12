/*
 * nRF52840 Dongle LED/button demo using Nordic's DK Buttons and LEDs library.
 *
 * The board exposes one green status LED, one RGB LED, and one button. The
 * green LED blinks with a 1 Hz period (500 ms on, 500 ms off). A short button
 * click advances the RGB LED through off -> red -> green -> blue -> off. When
 * the button is held for the long-press threshold, the RGB LED advances
 * repeatedly at a faster rate until the button is released.
 *
 * The main thread blinks the status LED while button events and delayed work
 * are handled by Zephyr's system work queue, so the two activities progress
 * independently without blocking one another.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>

/*
 * The DK library's LED indices follow the board's LEDs devicetree order on
 * this target: index 0 is the single green status LED; indices 1, 2, and 3
 * are the red, green, and blue channels of the RGB LED.
 */
#define STATUS_LED              DK_LED1
#define RGB_RED_LED             DK_LED2
#define RGB_GREEN_LED           DK_LED3
#define RGB_BLUE_LED            DK_LED4
#define RGB_LED_MASK            (BIT(RGB_RED_LED) | BIT(RGB_GREEN_LED) | BIT(RGB_BLUE_LED))
#define BUTTON_MASK             DK_BTN1_MSK

#define STATUS_LED_HALF_PERIOD  K_MSEC(500)
#define LONG_PRESS_THRESHOLD    K_MSEC(600)
#define RGB_FAST_CYCLE_INTERVAL K_MSEC(150)

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

enum rgb_state
{
        RGB_OFF,
        RGB_RED,
        RGB_GREEN,
        RGB_BLUE,
};

struct app_context
{
        enum rgb_state rgb_state;
        bool button_pressed;
        bool long_press_started;
        struct k_work_delayable rgb_cycle_work;
};

static struct app_context app_ctx;

static void set_rgb_state(void)
{
        uint32_t leds_on = 0;

        switch (app_ctx.rgb_state)
        {
        case RGB_RED:
                leds_on = BIT(RGB_RED_LED);
                break;
        case RGB_GREEN:
                leds_on = BIT(RGB_GREEN_LED);
                break;
        case RGB_BLUE:
                leds_on = BIT(RGB_BLUE_LED);
                break;
        case RGB_OFF:
        default:
                break;
        }

        int err = dk_set_leds_state(leds_on, RGB_LED_MASK);
        if (err)
        {
                LOG_ERR("Cannot set RGB LED state (err: %d)", err);
        }
}

static void cycle_rgb_led(void)
{
        app_ctx.rgb_state = (app_ctx.rgb_state + 1) % 4;
        set_rgb_state();
}

/**@brief Callback for button events.
 *
 * @param[in]   button_state  Bitmask containing buttons state.
 * @param[in]   has_changed   Bitmask containing buttons that has
 *                            changed their state.
 */
static void button_handler(uint32_t button_state, uint32_t has_changed)
{
        if (!(has_changed & BUTTON_MASK))
        {
                return;
        }

        bool pressed = (button_state & BUTTON_MASK) != 0;
        if (pressed == app_ctx.button_pressed)
        {
                return;
        }

        app_ctx.button_pressed = pressed;
        if (pressed)
        {
                app_ctx.long_press_started = false;
                k_work_reschedule(&app_ctx.rgb_cycle_work, LONG_PRESS_THRESHOLD);
                LOG_DBG("Button pressed");
        }
        else
        {
                k_work_cancel_delayable(&app_ctx.rgb_cycle_work);
                if (!app_ctx.long_press_started)
                {
                        LOG_DBG("Short click");
                        cycle_rgb_led();
                }
                else
                {
                        LOG_DBG("Long press released");
                }
        }
}

/** Handle the long-press threshold and repeat RGB cycling while held. */
static void rgb_cycle_work_handler(struct k_work *work)
{
        ARG_UNUSED(work);

        if (!app_ctx.button_pressed)
        {
                return;
        }

        if (!app_ctx.long_press_started)
        {
                app_ctx.long_press_started = true;
                LOG_INF("Long press detected; starting fast RGB cycling");
        }

        cycle_rgb_led();
        k_work_reschedule(&app_ctx.rgb_cycle_work, RGB_FAST_CYCLE_INTERVAL);
}

/** Initialize the DK LED/button library and the long-press work item. */
static int configure_io(void)
{
        int err = dk_leds_init();
        if (err)
        {
                LOG_ERR("Cannot initialize LEDs (err: %d)", err);
                return err;
        }

        k_work_init_delayable(&app_ctx.rgb_cycle_work, rgb_cycle_work_handler);

        err = dk_buttons_init(button_handler);
        if (err)
        {
                LOG_ERR("Cannot initialize buttons (err: %d)", err);
                return err;
        }

        return 0;
}

int main(void)
{
        LOG_INF("Starting nRF52840 Dongle LED/button demo");

        int err = configure_io();
        if (err)
        {
                return err;
        }

        /* Keep the green status LED blinking at 1 Hz (500 ms per half-cycle). */
        while (true)
        {
                dk_set_led_on(STATUS_LED);
                k_sleep(STATUS_LED_HALF_PERIOD);
                dk_set_led_off(STATUS_LED);
                k_sleep(STATUS_LED_HALF_PERIOD);
        }
}
