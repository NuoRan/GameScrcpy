The ESP32 touch box is an ESP32 development board with the GameScrcpy firmware on it: the phone takes it for a plugged-in touch screen, keyboard, mouse and gamepad, so it works even with USB debugging off. This computer sends the touches to it over a serial port (or over Wi-Fi).

## Pick a board

- **ESP32-S3, ESP32-S2, ESP32-P4**: plug it into the phone with a USB cable and the phone takes it for a hardware touch screen. The P4 uses high-speed USB for the least delay; it has no Wi-Fi, so it talks to this computer over the serial port only.
- **Classic ESP32, ESP32-C3, ESP32-C6**: these cannot be a USB device, so they reach the phone over Bluetooth. Pair "GameScrcpy XXXX" once in the phone's Bluetooth settings; afterwards it reconnects by itself, a few milliseconds slower than USB.
- The board needs at least 4 MB of flash; common development boards have that.

## Wiring {Two cables}

1. **To the phone**: on the S3 and S2 use the native port marked "USB", on the P4 the "USB 2.0 OTG HS" port, with an OTG cable to the phone. Bluetooth boards pair with the phone instead.
2. **To this computer**: use the board's other port (the USB-to-serial one, often marked "UART" or "COM"). The C3, C6 and P4 can also use the chip's own USB serial port. Windows has the drivers for these ports already.
3. **Connect in GameScrcpy**: on the Extensions page turn on the ESP32 touch box extension, click Settings at the bottom of its card (Settings on the board's card on the Devices page opens the same window), then click Connect at the top of the window. With the serial port in the Connection group on Automatic it tries the Espressif, CH34x and CP210x ports one after another; you can also pick the COM port.

> [!TIP]
> A small S2 board with only the native USB port needs a USB-to-serial adapter on GPIO43 (TX) and GPIO44 (RX) for this computer.

## Flash or upgrade the firmware

In the ESP32 settings window, click Flash firmware… in the Firmware group and choose the serial port the board is plugged into. GameScrcpy brings the firmware and the flashing tool along and usually puts the board into download mode by itself; writing takes about half a minute, and after the check the board restarts. When an update of GameScrcpy brings newer firmware, the button reads Upgrade firmware….

- **When to hold BOOT**: if it says the board did not enter download mode, hold the board's BOOT button (IO0 on some boards) while plugging in the cable (or press RST / EN once), let go of BOOT, then flash again.
- **Old firmware**: for a board that still runs the old QineTouch firmware, the settings window shows "Old firmware: flashing the new firmware is recommended". The old firmware only touches; keys, the mouse, the gamepad and Wi-Fi need the new one.

## Connect over Wi-Fi

While the board is on the serial port, click Set up Wi-Fi… in the Connection group of the settings window and enter the network name and password; GameScrcpy also makes a key that only this computer knows. Then choose Wi-Fi in the Connection group: with the address left empty it finds the board by itself, or enter its IP address. Wi-Fi adds 1 to 3 ms of delay and sometimes jitters.

## What the messages mean

- **"Nothing answers on this port"**: the board sent nothing at all. On an ESP32-S3 or S2 with two USB ports, the computer must use the port marked "COM" or "UART" (the USB-to-serial chip); the port marked "USB" goes to the phone. Check the cable too.
- **"The board keeps restarting (reset reason …)"**: it prints its boot messages again and again instead of answering. Flash the firmware again; if it keeps happening, check that the board has 4 MB of flash and enough power.
- **"The board is in download mode"**: it waits for a firmware writer. Press RST (EN) on the board once, then connect again.
- **"The bytes on this port do not look like the ESP32 touch box"**: something answers, but it is not this protocol: the wrong port, a serial speed the cable cannot keep, or a board that restarts before it can talk.
- **"The board runs the old QineTouch firmware"**: only touches can be sent before the new firmware is flashed.
- The line under the status strip is what the board says about itself: **USB to the phone**, **Bluetooth** and **Wi-Fi**, each ready, not started or not on this board.

## Safety and tips

- When nothing comes from this computer for 2 seconds, the board lifts every finger, lets go of every key and mouse button and centres the gamepad, so no finger stays stuck on the phone.
- Try it taps the middle of the phone's screen once, to check that the phone side is connected.
- While a mirrored phone uses the board, the settings window cannot connect to it (the serial port is busy); disconnect that phone first.
- With the ESP32's keyboard attached, Android stops showing the on-screen keyboard by default; turn on "Show virtual keyboard" in the phone's physical keyboard settings when you need it.
