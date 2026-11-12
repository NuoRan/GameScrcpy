# Read aloud (read-aloud)

[简体中文](README.md) · English

A Read aloud button on the mirror window's toolbar: one press, and the extension reads the text in a part of the phone's picture with the text recognition of Windows (`vision.recognize("system", "ocr", …)`) and reads it out (`speech.say`). The part and the language are set in the side panel.

## Files

| File | Contents |
|:--|:--|
| `manifest.json` | A session script; two panels, the toolbar button `read` and the side panel `area`; the permission `device.screen` |
| `src/main.ts` | Recognizing, reading out, and drawing the side panel |
| `tsconfig.json` | For `gs check` and editors |

## Try it

1. `gs validate examples\extensions\read-aloud`, `gs check examples\extensions\read-aloud`.
2. `gs pack examples\extensions\read-aloud -o read-aloud.gsext`, then "Install from file…" on the Extensions page, turn it on and allow its permission.
3. Open the mirror window of a phone (without a phone, the demo device of `GameScrcpy.exe --demo-device tests\data\video\emulator_h265.media` will do). The toolbar has a speaker button, and the side panel has Read aloud.
4. In the side panel, the sliders Left, Top, Width and Height frame the part to read (as ratios of the picture; by default the chat corner at the bottom left), and you can choose the language of the text.
5. Press the toolbar button or "Read now" in the panel: the text shows under "Last read" and is read out. The button shows as pressed while it reads.

The text recognition of Windows uses the OCR languages installed on the system; "As in Windows" follows the user's languages.

## How it works

- `vision.recognize("system", "ocr", region, { language })` recognizes the part of the next frame, giving pieces of text with their positions and confidence; `"text" in part` keeps only the text. Without a picture within 2 seconds it gives `null`.
- `speech.say(text, { interrupt: true })` reads it out, cutting off the sentence before; reading aloud needs no permission.
- Only session scripts can recognize (app scripts have no phone), so the manifest's script is `"run": "session"`, with one copy for each mirrored phone; the part and the language are kept in the script's `store`.
- `ui.toolbar("read").set({ checked: true, … })` shows the button pressed while reading, and `set(null)` brings back what the manifest declares.
