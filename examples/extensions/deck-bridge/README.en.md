# Deck bridge (deck-bridge)

[简体中文](README.md) · English

Lets programs on this computer, such as Stream Deck, press keys that profiles bind: the extension opens an HTTP server on `127.0.0.1` (`net.listen`), feeds what the requests ask for into the key mapping through the input source `deck` (`input.source`), and offers two services to other extensions (`ext.provide`).

## Files

| File | Contents |
|:--|:--|
| `manifest.json` | An app script, a page of the main window, two services, and the permissions `network.listen` and `hardware` |
| `src/main.ts` | The HTTP server, the keys, the page and the services |
| `tsconfig.json` | For `gs check` and your editor |

## Try it

1. `gs validate examples\extensions\deck-bridge`, `gs check examples\extensions\deck-bridge`.
2. `gs pack examples\extensions\deck-bridge -o deck-bridge.gsext`, then "Install from file…" on the Extensions page; turn it on and allow its permissions.
3. The left column gets a "Deck bridge" page saying `Listening on http://127.0.0.1:8765`. The port can be changed on the page; press "Listen again" afterwards.
4. In the key mapping editor, bind a key to a tap component: while it waits for a key, run the command below, and it records `Ext.example.deck-bridge.deck.k1`.

   ```powershell
   Invoke-RestMethod -Method Post http://127.0.0.1:8765/tap/k1
   ```

5. In Stream Deck, use an action (a plugin) that sends HTTP requests, with a POST to `http://127.0.0.1:8765/tap/k1`. The keys go to the phone whose mirror window is in front.

## The interface

| Request | What it does |
|:--|:--|
| `GET /` | Answers `{ listening, port, held }` |
| `POST /tap/<key>?ms=80` | Presses the key and lets it go after `ms` milliseconds (10 to 2000, 80 by default) |
| `POST /press/<key>` | Holds the key down |
| `POST /release/<key>` | Lets the key go |

A key is 1 to 32 letters, digits, `_` or `-`. Requests that press keys must be POST: a web page can make a browser send a GET without an `Origin` (an image, for one), and taking only POST keeps random pages from pressing keys.

Other extensions that list `example.deck-bridge` in their `dependencies` can call its services:

```ts
const state = await ext.call("example.deck-bridge/status");
await ext.call("example.deck-bridge/tap", { key: "k1", ms: 120 });
```

A service runs with the permissions both extensions have, so an extension that calls `tap` needs the permission `hardware` itself; anyone can call `status`.

## Notes

- Users can turn "Accept connections from programs on this computer" off in the extension's details, which closes the server; after turning it on again, press "Listen" on the page.
- When the logic stops (the extension is turned off, the program quits), every key held comes up.
