# Voice commands (voice-commands)

[简体中文](README.md) · English

Say a command, and the extension presses the key you chose for it and answers out loud: `speech.listen` hears the commands, `input.source` feeds the key into the key mapping, and `speech.say` reads the answer.

## Files

| File | Contents |
|:--|:--|
| `manifest.json` | An app script, a page of the main window, and the permissions `speech.listen` and `hardware` |
| `src/main.ts` | Listening, pressing and answering, and the page that manages the commands |
| `tsconfig.json` | For `gs check` and your editor |

## Try it

1. `gs validate examples\extensions\voice-commands`, `gs check examples\extensions\voice-commands`.
2. `gs pack examples\extensions\voice-commands -o voice-commands.gsext`, then "Install from file…" on the Extensions page; turn it on and allow its permissions.
3. The left column gets a "Voice commands" page. The first time, it sets up three commands in the language it listens in: "open map", "open bag" and "reload" in English, "打开地图", "打开背包" and "换弹" in Chinese. The page picks the engine, the language and how sure the engine must be, and adds and removes commands (what to say, which key to press, what to answer).
4. In the key mapping editor, bind `Ext.example.voice-commands.voice.map` to the button that opens the map: while it waits for a key, say "open map". From then on, saying "open map" in the mirror window presses that button and answers "Map".

The engine built into Windows listens only for a list of commands, in the speech recognition languages installed on the computer. Without a microphone, or when the engine does not know the language, the top of the page says why.

## How it works

- When it starts or a setting changes, `restart()` closes the old listener and calls `speech.listen` again with the current commands, engine, language and confidence.
- For each command heard, `heard()` holds the command's key for 80 milliseconds (`keys.press` / `keys.release`) and answers with `speech.say(…, { interrupt: true })`, cutting off an answer still being read.
- The commands, the engine, the language and the switch are kept in `store` across restarts. The logic of an extension cannot read the manifest's settings, so its options live on its own page.
- An app script has no phone, so the keys go to the phone whose mirror window is in front.
