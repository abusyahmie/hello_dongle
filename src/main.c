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
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
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
#define BT_UUID_APP_BUTTON_SERVICE_VAL \
        BT_UUID_128_ENCODE(0x9d7a0001, 0x6a5b, 0x4c3d, 0x8e2f, 0x1a0b3c4d5e6f)
#define BT_UUID_APP_BUTTON_CHARACTERISTIC_VAL \
        BT_UUID_128_ENCODE(0x9d7a0002, 0x6a5b, 0x4c3d, 0x8e2f, 0x1a0b3c4d5e6f)
#define BT_UUID_APP_BUTTON_SERVICE \
        BT_UUID_DECLARE_128(BT_UUID_APP_BUTTON_SERVICE_VAL)
#define BT_UUID_APP_BUTTON_CHARACTERISTIC \
        BT_UUID_DECLARE_128(BT_UUID_APP_BUTTON_CHARACTERISTIC_VAL)

#define STATUS_LED_HALF_PERIOD  K_MSEC(500)
#define LONG_PRESS_THRESHOLD    K_MSEC(600)
#define RGB_FAST_CYCLE_INTERVAL K_MSEC(150)
#define BLE_ADV_RETRY_DELAY     K_MSEC(250)

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
        bool physical_button_pressed;
        bool remote_button_pressed;
        bool long_press_started;
        struct k_mutex state_mutex;
        struct k_work_delayable rgb_cycle_work;
        struct k_work_delayable advertising_restart_work;
};

static struct app_context app_ctx;

static const char device_name[] = CONFIG_BT_DEVICE_NAME;

static const struct bt_data advertising_data[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA(BT_DATA_NAME_COMPLETE, device_name, sizeof(device_name) - 1),
};

static const struct bt_data scan_response_data[] = {
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_APP_BUTTON_SERVICE_VAL),
};

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

static void button_input_update(bool remote, bool pressed)
{
        k_mutex_lock(&app_ctx.state_mutex, K_FOREVER);

        bool was_pressed = app_ctx.button_pressed;
        if (remote)
        {
                app_ctx.remote_button_pressed = pressed;
        }
        else
        {
                app_ctx.physical_button_pressed = pressed;
        }

        app_ctx.button_pressed = app_ctx.physical_button_pressed ||
                                 app_ctx.remote_button_pressed;
        if (app_ctx.button_pressed == was_pressed)
        {
                k_mutex_unlock(&app_ctx.state_mutex);
                return;
        }

        if (app_ctx.button_pressed)
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

        k_mutex_unlock(&app_ctx.state_mutex);
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

        button_input_update(false, (button_state & BUTTON_MASK) != 0);
}

/** Handle the long-press threshold and repeat RGB cycling while held. */
static void rgb_cycle_work_handler(struct k_work *work)
{
        ARG_UNUSED(work);

        k_mutex_lock(&app_ctx.state_mutex, K_FOREVER);
        if (!app_ctx.button_pressed)
        {
                k_mutex_unlock(&app_ctx.state_mutex);
                return;
        }

        if (!app_ctx.long_press_started)
        {
                app_ctx.long_press_started = true;
                LOG_INF("Long press detected; starting fast RGB cycling");
        }

        cycle_rgb_led();
        k_work_reschedule(&app_ctx.rgb_cycle_work, RGB_FAST_CYCLE_INTERVAL);
        k_mutex_unlock(&app_ctx.state_mutex);
}

static ssize_t button_characteristic_write(struct bt_conn *conn,
                                           const struct bt_gatt_attr *attr,
                                           const void *buf, uint16_t len,
                                           uint16_t offset, uint8_t flags)
{
        ARG_UNUSED(conn);
        ARG_UNUSED(attr);
        ARG_UNUSED(flags);

        if (offset != 0)
        {
                return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
        }
        if (len != 1)
        {
                return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }

        const uint8_t *value = buf;
        if (value[0] > 1)
        {
                return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
        }

        button_input_update(true, value[0] == 1);
        return len;
}

BT_GATT_SERVICE_DEFINE(app_button_service,
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_APP_BUTTON_SERVICE),
                       BT_GATT_CHARACTERISTIC(BT_UUID_APP_BUTTON_CHARACTERISTIC,
                                              BT_GATT_CHRC_WRITE |
                                              BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE,
                                              NULL,
                                              button_characteristic_write,
                                              NULL));

static int start_advertising(void)
{
        int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertising_data,
                                  ARRAY_SIZE(advertising_data),
                                  scan_response_data,
                                  ARRAY_SIZE(scan_response_data));

        return err;
}

static void advertising_restart_work_handler(struct k_work *work)
{
        ARG_UNUSED(work);

        int err = start_advertising();
        if (err == -ENOMEM)
        {
                LOG_WRN("BLE resources are still releasing; retrying advertising");
                k_work_reschedule(&app_ctx.advertising_restart_work,
                                  BLE_ADV_RETRY_DELAY);
        }
        else if (err)
        {
                LOG_ERR("Cannot restart BLE advertising (err: %d)", err);
        }
        else
        {
                LOG_INF("BLE advertising restarted as %s", device_name);
        }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
        ARG_UNUSED(conn);

        if (err)
        {
                LOG_WRN("BLE connection failed (err: 0x%02x)", err);
                return;
        }

        LOG_INF("BLE client connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
        ARG_UNUSED(conn);

        LOG_INF("BLE client disconnected (reason: 0x%02x)", reason);
        button_input_update(true, false);
        k_work_reschedule(&app_ctx.advertising_restart_work, K_MSEC(100));
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
        .connected = connected,
        .disconnected = disconnected,
};

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
        k_work_init_delayable(&app_ctx.advertising_restart_work,
                              advertising_restart_work_handler);
        k_mutex_init(&app_ctx.state_mutex);

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

        err = bt_enable(NULL);
        if (err)
        {
                LOG_ERR("Cannot initialize Bluetooth (err: %d)", err);
                return err;
        }

        err = start_advertising();
        if (err)
        {
                LOG_ERR("Cannot start initial BLE advertising (err: %d)", err);
                return err;
        }
        LOG_INF("BLE advertising started as %s", device_name);

        /* Keep the green status LED blinking at 1 Hz (500 ms per half-cycle). */
        while (true)
        {
                dk_set_led_on(STATUS_LED);
                k_sleep(STATUS_LED_HALF_PERIOD);
                dk_set_led_off(STATUS_LED);
                k_sleep(STATUS_LED_HALF_PERIOD);
        }
}
