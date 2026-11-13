# Notes (notes-panel)

[简体中文](README.md) · English

A Notes page in the main window's left bar: notes are text files in a folder you choose (`files.folder`), and one click fetches the newest GameScrcpy release from GitHub and keeps it as a note (`net.fetch`).

## Files

| File | Contents |
|:--|:--|
| `manifest.json` | An app script and a main window page; the permissions `files.paths` (declaring the folder `notes`) and `network` (listing only `api.github.com`) |
| `src/main.ts` | Reading and writing notes, fetching the newest release, and drawing the page |
| `tsconfig.json` | For `gs check` and editors |

## Try it

1. `gs validate examples\extensions\notes-panel`, `gs check examples\extensions\notes-panel`.
2. `gs pack examples\extensions\notes-panel -o notes-panel.gsext`, then "Install from file…" on the Extensions page, turn it on and allow its permissions.
3. The left bar has a Notes page. The first time you open it, GameScrcpy asks you to choose a folder for it (the dialog shows the `purpose` from the manifest) and remembers it; the extension's settings can forget it.
4. Fill in a title and a note and choose Add: the folder gets a `title.txt`. Pick a note in the list to read all of it; "Remove the note" removes the file. A `.txt` you put into the folder yourself shows up after Reload. The panel's text comes from the script and does not go through the manifest's `l10n`, so it is in English.
5. Choose "Add the newest GameScrcpy release": the extension asks `api.github.com` for the newest release and keeps it as a note named "GameScrcpy v…".

## How it works

- `files.folder("notes")` opens the folder `notes` the manifest declares; the first time, GameScrcpy asks the user where it really is. The handle reaches only what is inside that folder: `list()`, `text(path)`, `write(path, text)`, `remove(path)`. The page opens the folder only when it is first shown; when the user cancels, it shows why and offers a "Choose the folder" button.
- `net.fetch(url, { headers })` has GameScrcpy ask GitHub's API; `hosts` of `network` in the manifest lists only `api.github.com`, so any other host, also one reached by a redirect, is refused (`GS-API-155`). The answer comes whatever its status, so the script checks `status` itself.
- Errors show at the top of the page instead of in a dialog.
