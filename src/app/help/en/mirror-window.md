When mirroring starts, the phone opens in its own window. What you do in the picture goes to the phone.

![The mirror window](figure:mirror-overview)

| Part | What it is |
|---|---|
| Toolbar | Pinned above the picture by default, with the status on its right. |
| Key hints | Show in full for a few seconds when a profile opens, then fade. |
| Status capsule | Frame rate, delay and connection; stays while there is a problem. |

## Use the phone with the mouse and keyboard

While key mapping is off, the window works like a touch screen:

- Click to tap, drag to swipe; the mouse wheel scrolls.
- What you type goes to the phone.

While key mapping is on, the keys of the profile in use play the game instead. See [Profiles and sharing](help:profiles) and [The key mapping editor](help:keymap-editor).

## Show the toolbar

By default the toolbar is pinned in a bar above the picture, with the phone, profile and frame rate on its right. You can change this in **Settings > Mirroring & quality > Display > Toolbar**:

- :pin: **Pinned above the picture**: the default. The pin at the right end of the toolbar unpins it.
- :mouse-pointer-click: **Rest the pointer at the top**: the toolbar appears when the pointer rests at the top of the window, or when you touch the grip there.
- :keyboard: **Shortcut only**: the toolbar appears only with the key of **Show or hide the toolbar**. This command has no key until you give it one in **Settings > Keyboard shortcuts**.

## What the toolbar does

From left to right. When the window is narrow, some buttons move into **More**.

![The toolbar](figure:mirror-toolbar)

| Button | What it does |
|---|---|
| :keyboard: Key mapping | Turns key mapping on or off. |
| :chevron-down: Key mapping options | Key hints and their opacity, the profile to use, **Edit key mapping**, **New profile…** and **Manage profiles…** |
| :house: Back, Home, Recent apps | The phone's own navigation keys. |
| :rotate-cw: Orientation | **Follow the device's rotation**, **Keep portrait** or **Keep landscape**. |
| :power: Power | Turns only the phone's own screen off; the picture here goes on. Select it again to turn the screen on. |
| :volume-2: Volume | Mutes the phone's sound on this computer, or sets how loud it plays. |
| :bot: Tasks | Starts or stops [tasks](help:scripts) on this phone; running ones show how far they are. The button is lit while a task runs. |
| Record | Records the picture and sound to a video; select it again to stop. The button is red while it records. Your recordings are under **More > Recordings…** |
| :puzzle: Extension panels | Shows only when an extension comes with a panel, and opens that panel beside the picture. Extensions can also add buttons of their own to the toolbar. |
| :activity: Diagnostics overlay | Shows the frame rate, the delay and more figures over the picture. |
| :maximize-2: Full screen | Fills the screen with the picture; press <kbd>F11</kbd> to leave. |
| :ellipsis: More | The status capsule, **Recordings…**, **Edit logic**, **Vision workbench**, **Measure the real latency**, **Shrink to a mini window on top**, **Device settings** and **Disconnect**. With extensions that offer them, also **Extensions reading the picture** and **Send to …**; see [What extensions add](help:extension-features#media). |
| :pin: Pin | Keeps the toolbar above the picture. |

## Status capsule

The capsule in the bottom right corner shows the frame rate, the delay and the connection. Click it to show the diagnostics overlay; right-click it to choose when it shows: **Automatic**, **Always shown** or **Hidden**. Automatic shows it for a few seconds when something changes, and keeps it while there is a problem.

## Key hints

While key mapping is on, key hints show on the picture which key does what. When a profile opens, they show in full for a few seconds, then fade. Turn them off or change their opacity in **Key mapping options**, or in **Settings > Input & key mapping**. To see them all in full only while you hold a key, place the example component **Show all key hints while held** in the key editor.

## Lock the camera

In shooting and action games, a profile with a **View** key turns the camera with the mouse. Press the camera lock key once: the pointer disappears and moving the mouse turns the view. Press it again to get the pointer back.

![Lock the camera](figure:camera-lock)

The camera lock key is the key above Tab, left of the 1 key. When the profile sets another key, that one counts; you can change it in the profile's **View** key.

While the camera is locked, the edges of the window glow so you can tell; turn off **Glow while the camera is locked** in **Settings > Mirroring & quality > Display** if you do not want it. The pointer cannot leave the window while the camera is locked.

## Type into the phone

While key mapping is on, an input box that opens on the phone puts the window into typing: what you type goes into the box, and a note over the picture says "Typing on the phone: Enter sends, Esc goes back to the game". This needs [GameScrcpy Companion](help:companion), which tells GameScrcpy that the box is open.

## Full screen and the mini window

![Full screen and the mini window](figure:window-modes)

- **Full screen** (<kbd>F11</kbd>) fills the screen with the picture; press <kbd>F11</kbd> again to leave.
- **Shrink to a mini window on top** turns the window into a small window that stays above other windows, for example while you wait in a queue. Drag it to move it; double-click it, or select **Restore**, to bring the full window back.

## Measure the real latency

Choose **More > Measure the real latency**. While it runs, the phone shows a dot wherever it is touched; press keys or tap the picture as usual. GameScrcpy times how long each touch takes to show in the picture, and shows the median of your presses.

> [!TIP]
> If it cannot see the dots, press keys over darker parts of the picture.

## Apps

On the **Devices** page, **More > Apps** on a mirrored phone lists its apps and games. Start one on the phone or, where the phone supports it, on an independent display: it opens in its own window and the phone's screen is not affected.

## Closing

Closing the mirror window ends mirroring for that phone. While you edit the key mapping, the close button asks in the middle of the window whether to **Leave editing** (which saves, like **Done**, and keeps the window open) or **Close the mirror window**; tick **Remember my choice** and it stops asking, and you can change it back under **Closing while editing keys** in **Settings > Mirroring & quality > Display**. If you choose to close with changes that are not saved, GameScrcpy asks about saving first.
