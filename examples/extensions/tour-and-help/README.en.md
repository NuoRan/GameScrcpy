# Tour and help (tour-and-help)

[简体中文](README.md) · English

An extension can bring its own guided tours (`contributes.tours`) and help articles (`contributes.help`). This example has a tour of five steps, two help articles in English and Chinese, and a page of the main window and a card in Settings for the tour to point at.

## Files

| File | Contents |
|:--|:--|
| `manifest.json` | The extension points `script`, `panel`, `help` and `tour`; two panels, a tour and two help articles; Chinese translations of the tour and the titles |
| `src/main.ts` | Draws the page (`where: "page"`) and the card in Settings (`where: "settings"`) |
| `help/guide.md`, `help/guide.zh_CN.md` | About the sample, with a link that replays the tour |
| `help/writing.md`, `help/writing.zh_CN.md` | What the help articles of an extension can use |
| `tsconfig.json` | For `gs check` and your editor |

## Try it

1. `gs validate examples\extensions\tour-and-help`, `gs check examples\extensions\tour-and-help`.
2. `gs pack examples\extensions\tour-and-help -o tour-and-help.gsext`, then "Install from file…" on the Extensions page, and turn it on.
3. Once the extension is on and loaded for the first time, the tour starts by itself: a card in the middle, then the extension's card on the Extensions page, the "Tour sample" page in the left column and its card in Settings › Extensions, and finally a pointer to the help.
4. Press F1: the Extensions group in the left column of the help page holds "The tour and help sample" and "Writing help for an extension"; "Play the tour" in the first one replays the tour. Details on the extension's card has a replay button too.

## Points to note

- A step's `target` is `card`, `device`, `page:<panel name>`, `settings:<panel name>` or left out; the manifest check makes sure the panel exists and is in the right place.
- The page and the card in Settings that the tour lights up are drawn by the app script; a step whose target is not there is skipped, so the script renders them as soon as it starts.
- Tour texts are translated in `l10n` as `tours.intro.title`, `tours.intro.<step>.title` and `tours.intro.<step>.text` (steps counted from 0); help titles as `help.<id>.title`, and the articles themselves by the files in `locales`.
- Links in help articles: `help:ext/example.tour-and-help/writing` opens the other article, and `action:tour:ext/example.tour-and-help/intro` replays the tour.
