# clipboard

The phone's clipboard, not the computer's.

## clipboard.read()

Reads the text on the phone's clipboard, an empty string when it holds no text; null when the phone cannot share its clipboard now, such as when it is not mirroring or mirrors an app on its own display, and in replays.

```ts
clipboard.read(): Promise<string | null>
```

Returns `Promise<string | null>`. stable since API 1.

Errors:

- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const copied = await clipboard.read(); if (copied) log.info("copied", copied);
```

## clipboard.write(text)

Puts text on the phone's clipboard; the computer's clipboard stays as it is. Resolves with true when the text was sent to the phone, false when the phone cannot share its clipboard now and in replays.

```ts
clipboard.write(text: string): Promise<boolean>
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | What to put on the phone's clipboard, up to 262000 bytes as UTF-8. |

Returns `Promise<boolean>`. stable since API 1.

Errors:

- `GS-API-011` when the text is not a string. Pass the text as a string.
- `GS-API-017` when the text is longer than 262000 bytes as UTF-8. Pass shorter text.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await clipboard.write("Room 4421");
```
