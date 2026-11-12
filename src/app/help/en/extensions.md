Extensions add features to GameScrcpy: touch boxes, new ways to reach phones, places pictures and sound come from and go to, reading aloud and voice commands, interface languages, AI model services, importing and exporting other formats, and panels, components, themes and picture filters. GameScrcpy comes with no extensions; each comes from its own author. Look at what one asks for before installing it; once installed, it can do only what you allow.

Where the things extensions add show up: [What extensions add](help:extension-features)

## Install an extension {#install}

1. **Get the package**: an extension comes as a `.gsext` file, and a bundle that installs a set of extensions at once is a `.gsbundle`. Download it from the author's project or release page, or choose an install link on a web page (see below).
2. **Install it**: on the **Extensions** page choose **Install from file…** and pick the file, or drag it straight into the GameScrcpy window.
3. **Look before installing**: the install dialog lists the version, the authors, the signature, the permissions it asks for and what it depends on. If all is well, choose **Install**; for an extension with native code the button says **Trust and install**.
4. **Turn it on**: an installed extension has a card on the **Extensions** page. It loads only once you turn on the switch on its card, and turning it on allows the permissions it asks for.

> [!WARNING]
> **Install only extensions you trust**: within the permissions it asks for, an extension can control the phone, read the picture, use the network or read and write the files you give it. For one of unknown origin, first look at its project page to see who made it.

## Install links on web pages {#links}

An author can put a `gamescrcpy://` link on a web page; choosing it hands the link to GameScrcpy, or to the copy already running.

- :import: **Install links**: GameScrcpy first downloads the package; the bottom of the main window shows "Downloading an extension from …" with the progress, and **Cancel** stops it. Only when the package's SHA-256 matches the link does the usual install dialog open, saying "Downloaded from …; its SHA-256 matches the link", and from there it is just like installing from a file; when it does not match, nothing is installed. Downloads use https only, and a package can be at most 200 MiB.
- :link: **Extension links**: `gamescrcpy://extension/` followed by an extension's id opens the **Extensions** page at that extension's card.

The installed version takes `gamescrcpy://` links by itself. A portable copy needs **Register gamescrcpy:// links** under **Settings > General > Extension links**. When that card says "Another copy of GameScrcpy opens these links now.", the links go to another copy on this computer; choose **Register gamescrcpy:// links** again to have this copy open them. To stop, choose **Remove gamescrcpy:// links**.

> [!NOTE]
> **A link goes to GameScrcpy on its own**: when you open a link from a command line or a shortcut, the link must be the only thing after `GameScrcpy.exe`. Given together with other arguments, GameScrcpy neither opens the link nor starts.

## Signatures and publishers {#signing}

An author can sign a package. The install dialog and the extension's details tell how it is signed:

- :shield: **Verified publisher**: the signature is valid and uses the same key as when this computer installed the extension before, so it comes from the same author.
- :info: **Signed with key …, which this computer has not seen before**: the signature is valid and the files are unchanged, but the key is new here, so GameScrcpy cannot tell whose it is. Compare the key with the one the author shows on their project page. Once installed, every update of the extension has to be signed with the same key.
- :triangle-alert: **Unsigned**: there is no way to tell who made it or whether it was changed. Install it only if you trust where it came from.

A package whose signature does not match its files, an unsigned package for an installed version that is signed, and an update signed with a different key are all refused. When an author really changes keys, remove the extension first, then install the new package.

## Extensions with native code {#native}

Some extensions bring Windows code of their own (DLLs), such as those that drive touch boxes. Before installing, GameScrcpy checks that code and shows what it found in the install dialog:

- :shield: **Restricted native code**: when an extension declares that it runs restricted, its DLL runs in a separate process without rights of its own: it cannot open your files, use the network or start other programs, and it reaches devices only through GameScrcpy, as far as you allow. The dialog says "Restricted: it can only do what it declares".
- :triangle-alert: **Native code**: a DLL that is not restricted has full rights on this computer. The dialog lists which of the checked system functions its code uses (network, files, keyboard and mouse, other programs), and adds a clear warning when it is not signed. Install it only if you trust where it came from.
- :circle-x: **Not installed, for your safety**: GameScrcpy refuses code that can inject itself into other programs, code in which Windows Security found a threat, code this computer's security settings block, and code whose signature does not match its files, and says why. When the check finds nothing, the dialog says "Windows Security found no threat".

An extension's DLL always runs in a process of its own; when that process stops three times within a minute, the extension is turned off (see "When something goes wrong" below).

## Turn on, set up and remove {#manage}

Each extension has a card on the **Extensions** page that tells what it adds and what it may do.

- :power: **Switch**: turned off, the extension no longer loads and what it adds goes away; turned on again, it comes back.
- :sliders-horizontal: **Settings**: an extension with settings has **Settings** on its card, and changes take effect at once; the same settings are listed under **Settings > Extensions**. Some settings can differ from phone to phone: with the phone connected, choose it on the Settings page to change them for it alone.
- :info: **Details**: everything it adds, the permissions it asks for, the tours it brings, the problems it ran into and its updates.
- :ellipsis: **More**: **Export…** saves the extension as a `.gsext` file; **Remove…** deletes it from this computer.

**Export bundle…** puts the extensions you use, with their settings, into one `.gsbundle` file, to install on another computer or share. When you install a bundle, the dialog lists each extension in it; choose **Install all**, or **Trust and install all** when any of them has native code.

The help articles an extension brings are in the "Extensions" group of this help, marked with the extension's name. The tours it brings are in its **Details**; "Tour: …" starts one: [Guided tours](help:tours#extension-tours)

## Update extensions {#updates}

When an author names an update list in the extension, GameScrcpy can look for new versions for you:

- :refresh-cw: **Check by itself**: turn on **Check extensions for updates** under **Settings > General > Updates** to check at most once a day at startup. It is off by default; a check only reads the lists the extensions name and sends nothing about what you have installed.
- :search: **Check now**: choose **Check for updates** on the **Extensions** page at any time.
- :circle-check: **Update**: when there is a new version, the card says "Update available: …"; choose **Update** to download it, with the progress shown and **Cancel** at hand. The usual install dialog then opens, saying "Downloaded from …; its size and SHA-256 match the update list", together with "What changes from version …".
- :x: **Skip**: to pass on this version, choose **Skip this version** in the extension's **Details**; a still newer version is offered again.

When the new version asks for more permissions or dependencies than before, the install dialog says "It asks for more than before", and after installing the extension stays off, with "Turn it on again to allow what's new" on its card; turning on the switch allows what's new. Updates of a signed extension have to be signed with the same key.

For an extension without an update list, download the new version from its author and install it from the file or a link as usual. The install dialog says "Replaces version …", and the extension's settings and the permissions you allowed are kept.

## When something goes wrong {#trouble}

- :circle-x: **Turned off by itself**: when an extension's process stops three times within a minute, or its attachments or behaviors fail three times within 10 seconds, GameScrcpy turns it off and the card says why. Once the author fixes it and you update to the new version, turn the switch on to try again.
- :activity: **Health**: when an extension stopped in the last 7 days, its card says how many times. The **Health** part of its **Details** lists what happened each time, when, in which version and the crash report; **Clear the record** empties it.
- :search: **Find a problem extension**: when you do not know which extension causes a problem, choose **Find a problem extension** on the **Extensions** page. Each step loads only some of your extensions; try what went wrong and answer **Still happens** or **Gone**. After a few steps it names the extension, and you can **Turn it off** or **Keep it on**.
- :shield: **Safe mode**: start `GameScrcpy.exe --safe-mode` to load no extension this time; start it without that argument to load them again.

When GameScrcpy ends unexpectedly while extensions are loaded, the next start loads none and the Extensions page says "No extension is loaded this time", with **Find the extension** to look for the cause; the start after that loads them as usual. When reporting a problem, attach a diagnostics file (**Settings > About > Export diagnostics…**), which lists your extensions and their latest crash reports.
