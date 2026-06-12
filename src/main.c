/*
 * Sample app that uses the dk_buttons_and_leds library from nRF Connect.
 * This library handles button press events in a clever way: it switches
 * between gpio callback and scanning mode (using a system work queue). When
 * a button state change is detected, our `button_handler()` is called.
 * When the button is pressed we start a timer in order to detect a long press
 * event. The timer event handler can be used to react to long press events.
 *
 * On the dongle we only have one green and one rgb led and one button.
 * In this example we always blink the green led with 1 Hz and a short button
 * click cycles the rgb led from off -> r -> g -> b (like a mode select).
 * A long press triggers fast cycling of the rgb led. The rtos makes possible
 * that this occurrs in a seemingly concurrent manner.
 *
 * This example has been extracted from the nRF Zigbee light switch sample
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include "dk_buttons_and_leds.h"

/*
 * The header file "dk_buttons_and_leds.h" is not part of the mainline
 * Zephyr Project tree. It is a Nordic Semiconductor-specific library
 * bundled exclusively with the nRF Connect SDK.
 *
 * File Location 
 * 
 * In your nRF Connect SDK installation folder, the file is located 
 * at:[nRF_Connect_SDK_Path]/nrf/include/dk_buttons_and_leds.h(e.g., 
 * typically inside v2.x.x/nrf/include/ depending on your specific 
 * SDK version). 
 *  
 * How to use it in your code: 
 * 
 * 1. Include Syntax: Use #include <dk_buttons_and_leds.h>. 
 * 
 * 2. Configuration: Ensure that the DK library is enabled in 
 *    your prj.conf file by adding: CONFIG_DK_LIBRARY=y 
 * 
 * Alternative (Direct Zephyr GPIOs) 
 * 
 * If you are transitioning away from a Nordic Development Kit to 
 * a custom board or mainline Zephyr, you cannot use this library directly. 
 * Instead, use the native Zephyr API by including <zephyr/drivers/gpio.h> 
 * and controlling the LEDs via Devicetree aliases. 
 *  
 * To explore the library's available functions, callbacks, and button/LED masks, 
 * view the official DK Button and LEDs - Technical Documentation.
 * https://docs.nordicsemi.com/bundle/nrf-connect-sdk/page/sdk_applications_peripheral/dk_buttons_and_leds.html
 * 
*/

#define GREEN_LED_BLINK_TIME K_MSEC(500)
#define BUTTON_LONG_POLL_TIME K_MSEC(200)
#define ATOMIC_FALSE 0U
#define ATOMIC_TRUE 1U

LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);

struct buttons_context
{
        uint32_t state;
        atomic_t long_poll;
        struct k_timer timer;
};

static struct buttons_context buttons_ctx;

static int current_state = 0; // 0->off 1-> led2, 2->led3, 3->led4

// go to next led state off->1(led2)->2(led3)->3(led4)
static void cycle_led()
{
        if (current_state == 3)
        { // led 4 is on
                dk_set_led_off(DK_LED4);
                current_state = 0;
        }
        else if (current_state == 0)
        { // all off
                current_state = 1;
                dk_set_led_on(current_state);
        }
        else
        { // led 2 or 3 on
                dk_set_led_off(current_state);
                current_state++;
                dk_set_led_on(current_state);
        }
}

/**@brief Callback for button events.
 *
 * @param[in]   button_state  Bitmask containing buttons state.
 * @param[in]   has_changed   Bitmask containing buttons that has
 *                            changed their state.
 */
static void button_handler(uint32_t button_state, uint32_t has_changed)
{
        switch (has_changed)
        {
        case DK_BTN1_MSK:
                LOG_DBG("button 1 changed");
                break;
        default:
                LOG_DBG("Unhandled button");
                return;
        }

        switch (button_state)
        {
        case DK_BTN1_MSK:
                LOG_DBG("Button pressed");
                buttons_ctx.state = button_state;
                // start a timer to check for long pess
                atomic_set(&buttons_ctx.long_poll, ATOMIC_FALSE);
                k_timer_start(&buttons_ctx.timer, BUTTON_LONG_POLL_TIME, K_NO_WAIT);
                break;
        case 0:
                LOG_DBG("Button released");
                k_timer_stop(&buttons_ctx.timer);
                if (atomic_get(&buttons_ctx.long_poll) == ATOMIC_FALSE)
                { // no long press
                        cycle_led();
                }
        }
}

/**@brief Timer event handler for long press
 *
 * @param[in]   timer         pointer to timer
 */
static void button_timer_handler(struct k_timer *timer)
{

        if (dk_get_buttons() & buttons_ctx.state)
        {
                // button that started the timer is still pressed -> we have a long press
                atomic_set(&buttons_ctx.long_poll, ATOMIC_TRUE);
                // here we can handle the long press event
                LOG_DBG("Long press detected");
                cycle_led();
                // start timer again so the long press action can be repeated
                k_timer_start(&buttons_ctx.timer, BUTTON_LONG_POLL_TIME, K_NO_WAIT);
        }
        else
        {
                // button is not pressed any more
                atomic_set(&buttons_ctx.long_poll, ATOMIC_FALSE);
        }
}

/**@brief Function for initializing LEDs and Buttons. */
static void configure_gpio(void)
{
        int err;

        err = dk_buttons_init(button_handler);
        if (err)
        {
                LOG_ERR("Cannot init buttons (err: %d)", err);
        }

        err = dk_leds_init();
        if (err)
        {
                LOG_ERR("Cannot init LEDs (err: %d)", err);
        }
}

void main(void)
{
        LOG_INF("Starting main");

        // configure gpio for buttons and leds
        configure_gpio();
        // init timer for long press events
        k_timer_init(&buttons_ctx.timer, button_timer_handler, NULL);

        // blink green led to show we are alive
        while (1)
        {
                dk_set_led_on(DK_LED1);
                k_sleep(GREEN_LED_BLINK_TIME);
                dk_set_led_off(DK_LED1);
                k_sleep(GREEN_LED_BLINK_TIME);
        }
}
