GameScrcpy shows your Android phone on this computer, and you control it with the keyboard and mouse. Connect it once with a USB cable; after that you can also connect over Wi‑Fi.

## Prepare the phone {Only once}

Every brand hides these switches in a different place. Pick your phone, and the steps and the picture next to them follow; rest the pointer on a step to see it on the phone.

![Prepare the phone](figure:setup-steps)

> [!TIP]
> **Cannot find these options?** Search the phone's settings for "developer". Or open the getting-started guide, which walks you through it step by step.
> [Open the guide](action:guide)

## Connect with a USB cable

1. **Plug the phone into this computer**: use a cable that carries data; some cables only charge.
2. **Choose "File transfer"**: when the phone asks what the USB connection is for.
3. **Allow this computer on the phone**: unlock the phone. When it asks "Allow USB debugging?", tick "Always allow from this computer" and tap **Allow**.
4. **Select Mirror on the Devices page**: the phone shows up there by itself. Closing the mirror window ends mirroring.

![Connect with a USB cable](figure:phone-usb,phone-usbmode,phone-allow!,phone-ready)

![The first connection takes a few seconds. The row of steps on the phone's card shows how far it got; when the picture arrives, the mirror window opens.](figure:device-stepper)

The phone does not show up? [Questions · The phone does not show up on the Devices page](help:faq#no-phone)

## Pair over Wi‑Fi {Android 11 or later}

The phone and this computer must be on the same network. On the **Devices** page, select **Wireless pairing**; the **Wireless debugging** section at the bottom of the page opens.

### With a pairing code {icon=type}

1. **Open the pairing dialog on the phone**: go to **Developer options > Wireless debugging > Pair device with pairing code**.
2. **Wait for GameScrcpy to find the phone**: it looks for a phone that shows its pairing dialog. If it finds none, select **Type the address** and enter the IP address and port shown in the dialog.
3. **Type the six-digit code**: the digits the phone shows. Pairing starts as soon as all six are in.

![Pair with a pairing code](figure:phone-pairing,,!)

### With a QR code {icon=qr-code}

1. **Select Use a QR code in GameScrcpy**: the **Wireless debugging** section shows a QR code.
2. **Scan it with the phone**: go to **Developer options > Wireless debugging > Pair device with QR code** and scan the code on the screen. Pairing starts by itself.

![Pair with a QR code](figure:pair-qr,phone-scan!)

### After pairing {#after-pairing}

After pairing, the phone usually connects by itself. If it does not, look at the main **Wireless debugging** screen on the phone, enter the IP address and port shown there (not the pairing port) under **Recent connections**, and select **Connect**. Phones you connected before stay under **Recent connections**; next time one click is enough.

> [!WARNING]
> **Phones before Android 11 cannot pair**: connect them over USB, run `adb tcpip 5555` once, then connect to the phone's IP address with port 5555.

## Choose how the picture travels

Before you select **Mirror**, you can choose the connection on the phone's card:

- :plug: **adb tunnel**: through the USB cable or wireless debugging. It works everywhere.
- :wifi: **WiFi direct**: the picture goes straight over Wi‑Fi. The phone must be connected over wireless debugging, and the router must let its devices reach each other.
- :sparkles: **Auto**: GameScrcpy chooses.

## Several phones

You can mirror several phones at the same time. Each phone gets its own mirror window, and the **Devices** page lists them all.

## Settings for one phone

The settings button on a phone's card opens **Device settings**. There the phone can have its own resolution, frame rate, video codec, bit rate and sound settings; everything you do not change follows the global settings.

## More on the phone's card

The **More** button (…) on a mirrored phone's card has **Apps**, the phone's screen and orientation, its Back, Home and Recent apps keys and the diagnostics overlay. Phones on Wi‑Fi also have **End wireless debugging connection**.
