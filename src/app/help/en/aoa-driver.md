With AOA the phone takes GameScrcpy for a touch screen on the USB cable, together with a mouse and a keyboard. It works even when USB debugging is off. On Windows the phone needs the WinUSB driver once; the free Zadig installs it as shown below.

## Install the WinUSB driver with Zadig {Only once}

1. **Download and open Zadig**: get it from the [Zadig website](https://zadig.akeo.ie) and double-click it; there is nothing to install. When Windows asks to allow changes, choose Yes: Zadig installs the driver, GameScrcpy itself needs no administrator rights.
2. **List every device**: plug the phone into this computer, then choose Options > List All Devices in Zadig.
3. **Pick your phone**: in the drop-down list, choose the phone's name, often with "(Interface 0)" or "MTP". Do not pick "ADB Interface"; that one is for USB debugging.
4. **Change it to WinUSB**: check that WinUSB is to the right of the green arrow, click Replace Driver (Install Driver the first time) and wait until it says it is done.
5. **Back in GameScrcpy**: there is no need to restart. It notices by itself and says the driver is installed; the phone's card shows the connection "USB · AOA".

![Install the WinUSB driver with Zadig](figure:zadig-open,zadig-list,zadig-pick,zadig-install!,driver-done)

> [!WARNING]
> **Once it is installed**: on Windows this phone cannot use USB debugging while AOA is in use, and copying files in File Explorer may stop working. To go back, open Device Manager, right-click the phone, choose Uninstall device, tick "Attempt to remove the driver for this device", then plug the phone in again.

## Choose AOA

In Settings > Input & key mapping > Touch, set the touch method to AOA, for every phone or for one phone. While you choose, the features that do not work without a picture are listed; it takes effect once you confirm, the next time the phone connects.

## With a picture, or without one

- **Wireless debugging on**: the picture comes over wireless debugging and touch goes over AOA on the cable. The phone's card shows "Wireless · picture + USB · AOA touch".
- **Wireless debugging off**: a control window opens instead, a dark canvas in the shape of the phone's screen with the key hints and the words "No picture · Watch the phone". The keyboard and mouse turn into touches only while the control window is in front.
- **The pointer on the phone**: without a picture the phone shows a mouse pointer of its own; with a free cursor, look at the phone and click. While the camera is locked the pointer stops moving and the phone hides it.
- **How the phone is held**: without adb nothing tells whether the phone is upright or turned. Choose Portrait, Landscape (turned left) or Landscape (turned right) under "How the phone is held" in the control window's toolbar. It starts from the profile's orientation and each phone remembers the last choice.

## Connect GameScrcpy Companion by scanning

Without USB debugging, GameScrcpy Companion on the phone connects to this computer over Wi-Fi. It then draws the key hints on the phone's own screen and lets you type into the phone's input boxes with this computer's keyboard, Chinese included.

1. **The same Wi-Fi**: the computer and the phone must be on the same Wi-Fi or local network.
2. **Install the companion**: if the phone does not have GameScrcpy Companion yet, scan the code in the control window with the phone's own camera or scanner. It opens a page on this computer to download the app; install it, and allow installing from this source if the phone asks.
3. **Scan to connect**: open GameScrcpy Companion on the phone, tap Scan to connect, allow the camera and scan the code in the control window. The code goes away once the phone is connected.
4. **Type with the computer's keyboard**: on the companion's **Permissions** page, tap **Open** on the Input method row and switch on GameScrcpy Companion, then come back and tap **Choose** to pick it. When an input box opens on the phone, a thin bar says "Typing with the computer's keyboard"; tap it to get the phone's keyboard back. The companion's **Features** page can move the bar to the top.

> [!TIP]
> **Next time**: both sides remember each other. On the same Wi-Fi the companion connects again by itself as soon as the control window is open; there is nothing to scan (the companion's Auto connect is on by default).

The companion's other features, switches and permissions: [GameScrcpy Companion](help:companion)

> [!WARNING]
> **Windows Firewall**: the first time, Windows asks whether GameScrcpy may accept connections from the network. Allow it for private networks, otherwise the phone cannot connect. GameScrcpy only listens while a control window shows the code or waits for a paired phone.

## What needs a picture

Vision and the vision workbench, `vision.*` in logic and scripts (which answers null, [] or false), recording, clipboard sharing, starting apps, the latency test, and sound, turning the screen off and rotation. Keys, the camera lock, editing the key mapping and the touches of logic all work as usual.
