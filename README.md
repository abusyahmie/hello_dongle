# nRF52840 Dongle Button and LED Demo

The green status LED blinks at 1 Hz. A short press of the physical button
cycles the RGB LED through off, red, green, and blue. Holding the button for
600 ms starts faster repeated cycling; release stops it.

The app also advertises as **Hello Dongle Button** and exposes a writable BLE
button characteristic. Connect with a BLE client and write one byte to the
characteristic:

- `01` presses the virtual button.
- `00` releases it.

Keep the value at `01` to trigger long-press cycling, then write `00` to stop.
The service UUID is `9d7a0001-6a5b-4c3d-8e2f-1a0b3c4d5e6f`; its button
characteristic UUID is `9d7a0002-6a5b-4c3d-8e2f-1a0b3c4d5e6f`.

The BLE button and physical button share the same behavior. A BLE disconnect
releases any virtual press and restarts advertising.
