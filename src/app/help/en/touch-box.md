A touch box is a small board that a phone takes for a plugged-in touch screen and keyboard, so you can control the phone even with USB debugging off. GameScrcpy does not drive any board by itself: an extension adds the touch box, and the box then gets a card of its own on the Devices page.

## The ESP32 touch box

The ESP32 touch box that was built into GameScrcpy 2.0 is now the community extension `community.esp32`, from the GameScrcpy-ESP32 project of the GameScrcpy community. It still turns an ESP32 development board into the phone's touch screen, keyboard, mouse and gamepad, and it brings the firmware, flashing and the Wi-Fi setup along.

1. **Get the extension**: download `community.esp32-<version>.gsext` from the GameScrcpy-ESP32 project.
2. **Install it**: on the **Extensions** page select **Install from file…**, choose the file and accept the permissions it asks for.
3. **Read its help**: the extension's own articles, about wiring, flashing, when to hold BOOT and what its messages mean, appear in this help under its name.

> [!NOTE]
> Nothing carries over from GameScrcpy 2.0. The ESP32 settings are dropped, and a phone that touched through the ESP32 touch box touches the normal way until you choose a touch method again. Set up the board's Wi-Fi once more in the new extension.

## A touch box on the Devices page

- :mouse-pointer-click: **Control**: opens a control window without a picture; your taps, keys and mouse reach the phone through the box.
- :sliders-horizontal: **Settings**: opens the box's own window, when the extension has one.
- :ellipsis: **More**: whatever else the extension offers for the box, such as flashing the firmware.

The line under the box's name says how it is doing. "In use by …" means another session or extension has its port open; close that first.

## Touch a mirrored phone through a box

Open the phone's **Settings**, go to **Input & key mapping** and choose the box's method under **Touch method from an extension**. When the method asks for a device, choose it under **Device for the touch method**. Both take effect the next time the phone connects, and while the extension is off or fails, touches go the normal way. Settings the extension keeps for each phone, such as which port to use, are in the same window under the extension's name.

## Pair a box with a capture card

A box that can work with a capture card is offered in the capture card's settings: select **Settings** on the capture card's card and choose the box under **Touch method** in the **Touch** group. The picture then comes from the capture card and the touches go through the box, all without USB debugging. [Capture cards](help:capture-card) has the rest.

> [!TIP]
> A box shows up there only while its extension is on and the box is connected. When the chosen box is not there as the capture card starts mirroring, touch stays off and the mirror window says so.
