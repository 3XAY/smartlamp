# smartlamp

An advanced, Wi-Fi enabled  smart lamp featuring C++ dual-core asynchronous firmware, a custom web control panel, hardware-level power-loss recovery, and an IR remote interface.

## Key Features
- **Dual-Core Asynchronous Firmware (C++):** Dedicated core for the Wi-Fi/Web server stack and a separate core for real-time Neopixel lighting control.
- **Power-Loss Recovery & Brown-Out Protection:** Utilizes large filtering capacitors and voltage detection resistors to safely save the last operational state to flash memory right before total power collapse.
- **Local Web UI & IR Control:** Includes a responsive web control panel for scheduling, night-light mode, and brightness scaling, alongside physical IR remote input.
- **Custom Hardware Design:** Built using an ESP32-WROOM-32, custom KiCad schematics, and an optimized 3D-printed modular enclosure featuring brass heat-set inserts.

For more info, see the [portfolio](https://github.com/3XAY/smartlamp/blob/main/Lamp%20Portfolio.pdf) or the [notebook](https://github.com/3XAY/smartlamp/blob/main/Lamp%20Notebook.pdf).
