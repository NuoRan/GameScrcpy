GameScrcpy Companion is a small app on the phone that works with GameScrcpy on your computer: it shows the computer's cursor and the key hints on the phone's own screen, lets you type into the phone with the computer's keyboard (Chinese included), and connects over Wi-Fi when there is no USB debugging. Mirroring and key mapping work without it; it only does these few things.

## What it does {#features}

- :mouse-pointer-click: **Cursor on the phone**: while the mouse is over the picture on the computer, the phone's own screen shows where it points.
- :keyboard: **Key hints**: draws the keys of the current profile over the game on the phone, as in the mirror window. Without a picture, such as over AOA, they show by themselves.
- :type: **Computer keyboard input**: while mirroring, what you type on the computer, Chinese included, goes into the phone's input boxes through the GameScrcpy Companion input method.
- :wifi: **Auto connect**: on the same Wi-Fi, the phone connects by itself to the computers it was paired with by scanning, also after the connection drops.

## Install the companion {#install}

Over USB debugging there is no package to look for. The first time you turn on one of the features above, the mirror window says something like "Install GameScrcpy Companion on the phone to show the cursor there": choose **Install**, and allow it on the phone if the phone asks. When GameScrcpy later brings a newer companion, it replaces the one on the phone by itself.

Without USB debugging, such as when AOA or a touch box controls the phone, scan the code in the control window to download it: [AOA touch and the WinUSB driver](help:aoa-driver)

## Switch it on either side {#switches}

The four switches and the opacity of the key hints are this phone's settings in GameScrcpy. Change them on the computer or on the phone; a change on one side shows on the other.

- **On the computer**: **Settings > Input & key mapping > GameScrcpy Companion on the phone** has "Type with this computer's keyboard", "Show key hints on the phone" with their opacity, "Show the cursor on the phone" and "Connect again by itself over Wi-Fi". With the phone connected, choose this phone to change them for it alone; a change made on the phone is kept for this phone too.
- **On the phone**: the Quick switches on the companion's **Home**, or the switch on each card of the **Features** page.

A switch changed while the phone is not connected says "Sent when the computer connects" and syncs once it is. When GameScrcpy on the computer is older, the phone keeps what you changed and sends it to a computer that can take it.

> [!NOTE]
> **Options that stay on the phone**: the cursor's Size and Style, and whether the bar shown while typing sits at the Bottom or the Top (Hint bar), are kept on this phone only and are not synced to the computer.

## Opacity of the key hints {#opacity}

The key hints on the phone show in full first, then fade to the opacity you set. On the computer it is **Opacity of the key hints on the phone**; on the phone it is **Features > Key hints > Opacity once faded**; both are the same value. The key hints in the mirror window have an opacity of their own, under **Settings > Input & key mapping > Key hints**.

## Connect over Wi-Fi {#wifi}

1. **Pair by scanning**: on the companion's Home, tap **Scan to connect** and scan the code in GameScrcpy's control window. Only the first time needs a scan.
2. **Reconnects by itself**: while Auto connect is on (it is by default), the companion finds the computers it was paired with on the same Wi-Fi and connects, also after the connection drops.
3. **Your computers**: the companion's **Computers** page lists the computers this phone has connected to. **Connect now** tries again right away; **Remove** makes the phone forget the computer and the settings it sent, so connecting over Wi-Fi needs a new scan.

If it does not connect, check that the computer and the phone are on the same Wi-Fi and that Windows Firewall allows GameScrcpy.

## Permissions the companion needs {#permissions}

The companion's **Permissions** page tells what each permission is for and whether it is allowed; tap **Open** on a missing one.

| Permission | What for |
|---|---|
| Display over other apps | Shows the cursor and the key hints over games. |
| Notifications | Shows that the companion is connected, with Stop at hand. |
| Camera | Only for scanning the code in GameScrcpy's control window. |
| Input method | Lets the computer type into the phone. Over USB debugging GameScrcpy turns it on and chooses it by itself; without USB debugging, turn it on here once, then tap **Choose** to pick it. |
| Screen capture | When the computer asks for a screenshot, Android asks you to allow the capture. |

> [!TIP]
> **Privacy**: the companion talks only to GameScrcpy on your own computers, over USB debugging or your Wi-Fi. It collects nothing and sends nothing anywhere else.
