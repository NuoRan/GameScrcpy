What an extension adds shows up where you would use it anyway: on the Devices page, in the mirror window, in Settings and on the Profiles page. This article goes place by place through what extensions can bring and where to find it. An extension's **Details** list everything it adds; when you turn the extension off, all of that goes away with it.

How to install, turn on and update extensions: [Extensions](help:extensions)

## New devices on the Devices page {#devices}

- :plug: **Touch boxes**: an extension that drives touch boxes gives each box a card of its own on the Devices page, showing its state and what it is doing, such as the progress of flashing its firmware; the card's **More** holds the actions the extension offers. The phone takes touches through the box, so it can be controlled without USB debugging; see [Touch boxes](help:touch-box).
- :link: **New ways to connect**: some extensions reach phones their own way. A phone connected through one shows up on the Devices page like any other, with that connection method's icon before the card's summary; when the connection drops, GameScrcpy gives the reason the extension reports. The extension's **Details** say "Connection methods: …".
- :monitor-smartphone: **Picture sources**: an extension that brings pictures and sound, such as from a capture device of some kind, shows up on the Devices page like a capture card and can provide the picture for a phone; see [Capture cards](help:capture-card).

## Pictures and sound {#media}

- :share: **Send elsewhere**: an extension that takes pictures or sound, such as a virtual camera, shows up in the mirror window's **More** as "Send to …". Select it to start sending this phone's picture there, and again to stop; when something goes wrong on the extension's side, GameScrcpy says "Stopped sending to …: …".
- :history: **Export recordings**: an extension that exports recordings adds "Export as …" to the **More** menu of each recording under **More > Recordings…**, to save the recording in a format it supports.
- :eye: **Extensions reading the picture**: extensions that read the picture, for example to recognize things in it, are listed under "Extensions reading the picture" in the mirror window's **More**, turned on or off for each phone. One can mark what it sees on the picture.

## Reading aloud and voice commands {#speech}

Logic can read text aloud with `speech.say` and listen for voice commands with `speech.listen`. Windows' own speech is used by default; extensions can provide other engines. Choose them under **Settings > Audio > Reading aloud and voice commands**:

- :volume-2: **Reading aloud**: **Speech engine**, **Voice**, **Speaking rate** and **Speaking volume**; on the **Try it** row, **Read aloud** reads a sentence with these settings.
- :audio-lines: **Voice commands**: **Voice command engine** and **Voice command language**; without a language, it is "Same as the interface".

> [!NOTE]
> **Windows' own voices and speech recognition**: when the card says that Windows has no voice or no speech recognizer, add a speech language in Windows Settings under Time & language, Speech.

The logic in your own profiles can read aloud and listen right away. The logic of an extension needs the permission "Listen for voice commands you say" to listen, and the install dialog shows it.

## Interface languages {#languages}

A language pack extension adds an interface language to GameScrcpy, and some also bring translations of this help; articles without a translation show in English. Once it is installed and on, choose it under **Interface language** in **Settings > Appearance > Language**, where it shows as "language · extension".

While the extension is off, the interface follows Windows until it is on again; once the extension is removed, the interface follows Windows.

## Files and folders for logic {#files}

Logic cannot read or write the files on your computer as it likes. When it needs to open or save a file or use a folder, it asks you to pick one yourself:

- :doc-page: **Open or save a file**: the file dialog says "Your logic wants to open a file" or "Your logic wants to save a file" (for an extension's logic, the extension's name). It can use only the one file you picked; Cancel gives it nothing.
- :folder-open: **Use a folder**: the folder dialog says "Choose the folder … for your logic". A folder you chose is remembered, so it does not ask again.

Remembered folders are listed under **Settings > General > Folders your own logic may use**, and an extension's under "Folders it may use" in its **Settings**; choose **Forget** to be asked again the next time. An extension that asks for these abilities shows "Open files you choose" or "Read and write folders you choose for it" in the install dialog.

## AI model services {#ai}

An extension that provides an AI model service shows up under **Model service** in **Settings > AI > AI model**, as "service (extension)". With it chosen, there is no address or API key to fill in; choose **Get** on the **Model** row to list the models the extension offers. The assistant and `ai.ask` in logic both use the service chosen here.

Once the extension is turned off or removed, the service shows "(not available)", and the assistant and logic that ask AI get an error until the extension is back or you choose another service.

## Import and export other formats {#converters}

Converter extensions let GameScrcpy read and write other formats:

- :import: **Import**: choose **Import…** on the **Profiles** page; the file dialog gains "All files that can be imported" and the formats the extensions know. Dragging a file into the window works too. What you import becomes a profile of yours.
- :share: **Export**: the **More** menu of a profile card, of a task's settings and of a recording gains "Export as …".

The extension's **Details** show "File types it converts". Files larger than 32 MiB cannot be converted, and a conversion that takes too long stops with a reason.

## Panels, components and more {#more}

- :panel-top: **Panels**: an extension's panel can show beside the mirror window (**Extension panels** on the toolbar), as a page of the main window, as a section in Settings, in its own settings window, in the **More** menu of cards or in the tray menu.
- :puzzle: **Components and add-ons**: extensions can add components to the library of the key mapping editor, add-ons to keys, and nodes and commands to logic. Add-ons for every profile are turned on and off in the extension's **Settings**.
- :gamepad-2: **Input devices**: an extension can bring devices of its own into the key mapping, such as a special controller or a pedal; in the key mapping editor you press their buttons just like keys and gamepad buttons.
- :crosshair: **Recognition engines**: extensions can give logic engines that recognize text, things or what the picture shows.
- :plug: **Connections from programs on this computer**: an extension that asks to "Accept connections from programs on this computer" has logic that other programs on this computer can connect to. To stop that, turn off "Accept connections from programs on this computer" in the extension's **Details**.
- :navigation: **Help and tours**: the help articles an extension brings are in the "Extensions" group of this help; the tours it brings play from its **Details**; see [Guided tours](help:tours#extension-tours).
