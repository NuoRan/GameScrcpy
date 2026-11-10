A capture card turns the picture a phone sends over HDMI into a picture on this computer. GameScrcpy shows it like a phone's picture, with key mapping, vision and recording, even when USB debugging is off. Touches go to the phone over AOA, through the ESP32 extension, or not at all.

## What to buy

- :usb: **A USB 3.0 capture card**: gives the raw picture, 1080p at 60 frames per second with almost no delay. Look for "1080p60" and "USB 3.0" on the box; GameScrcpy picks the best format by itself.
- :plug: **A cheap USB 2.0 card**: sends the picture as MJPEG. Most still reach 1080p at 60 frames per second; the graphics card unpacks it, and the picture is a little softer.
- :smartphone: **A USB-C to HDMI adapter**: the phone must send video over its USB-C port (DisplayPort Alt Mode). Many flagship phones do, most budget phones do not.

> [!NOTE]
> GameScrcpy reads capture cards the way the Windows Camera app does. A card that works only in OBS through a driver of its own does not show up.

## Connect it

1. **Phone, adapter, HDMI cable, capture card, computer**: plug the capture card into a USB 3.0 port of this computer.
2. **Turn the phone's screen on**: most phones show their screen over HDMI as soon as the adapter is in.
3. **Look at the Devices page**: a "Capture card" card appears by itself, one for each card. Select **Mirror**.

A webcam can show up too. Choose **More > This is not a capture card, hide it** on its card and it moves into the **Other video devices** row at the end of the device list; devices whose names do not look like a capture card (virtual cameras, webcams) start out there. Expand that row and select **Treat as capture card** to bring one back.

## Choose how touches reach the phone

Select **Settings** on the capture card's card and choose in the **Touch** group of the settings window:

- :monitor-play: **No touch**: you watch the picture, and vision and recording work; the keyboard and mouse do nothing on the phone.
- :usb: **AOA over USB**: plug the phone into this computer with a USB cable as well and choose it under **Phone on USB**. The phone needs the WinUSB driver once, as [AOA touch and the WinUSB driver](help:aoa-driver) shows.
- :plug: **ESP32**: the ESP32 extension gives the touches to the phone as a USB touch screen. Turn the extension on first; while it is off, touch stays off and the mirror window says so.

**Profile** in the **Key mapping** group of the settings window chooses the profile: without adb GameScrcpy cannot tell which game is in front.

## Black bars and the picture area

Phones send a picture in the shape of their screen, and the capture card fills the rest of its wide frame with black. GameScrcpy finds the phone's screen in the frame by itself and shows only that part; key hints, vision, screenshots and recordings also see only the phone's screen. Dark scenes do not make the area smaller, and after the phone turns, the new area takes over within about a second.

If the area is wrong, choose **More > Adjust the picture area…** in the mirror window, drag the box onto the phone's screen and select **Apply**. **Back to automatic** lets GameScrcpy find it again.

## Turned left or right

Without adb nothing tells which way a landscape phone is turned, so touches could land upside down. **Which way the phone turns** on the mirror window's toolbar chooses **Landscape (turned left)** or **Landscape (turned right)**. Each capture card remembers it; turned left comes first.

## Sound

When the capture card has a sound device of its own, GameScrcpy finds it, plays it and records it with the picture. The volume and mute buttons work as they do for a phone. Turn **Sound** off in the **Picture and sound** group of the settings window to leave the sound alone.
