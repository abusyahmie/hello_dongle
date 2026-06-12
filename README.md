# Zephyr example using buttons and leds on nRF52840 dongle

Sample app that uses the dk_buttons_and_leds library from nRF Connect.
This library handles button press events in a clever way: it switches
between gpio callback and scanning mode (using a system work queue). When
a button state change is detected, our `button_handler()` is called.
When the button is pressed we start a timer in order to detect a long press
event. The timer event handler can be used to react to long press events.
 
On the dongle we only have one green and one rgb led and one button.
In this example we always blink the green led with 1 Hz and a short button 
click cycles the rgb led from off -> r -> g -> b (like a mode select).  
A long press triggers fast cycling of the rgb led. The rtos makes possible 
that this occurrs in a seemingly concurrent manner.

This example has been extracted from the nRF Zigbee light switch sample.
It should also work on the other Nordic development kits.

Reference link: https://github.com/androbi-embedded/hello_dongle
