# input

What the user holds and points at, readable only while the phone's mirror window is in front.

## input.source(name)

An input source of this logic, such as a device it reads: the keys it presses and the axes it moves reach the phone's key mapping as the input Ext.<extension id>.<name>.<key>, or Ext.mine.<name>.<key> for your own logic, which profiles bind like any key. While the key editor waits for a key, a press is taken as that key.

```ts
input.source(name: string): InputSource
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The name of the input source: 1 to 32 lowercase letters, digits or -. |

Returns `InputSource`. stable since API 1.

Errors:

- `GS-API-085` when the name is not 1 to 32 lowercase letters, digits or -. Use a name such as "deck" or "pedal-board".
- `GS-API-080` when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.

```ts
const deck = input.source("deck"); deck.press("k1"); deck.release("k1");
```

## input.cursor()

Where the pointer is over the phone picture, as ratios; null when the phone's mirror window is not in front or the pointer is outside the picture.

```ts
input.cursor(): PicturePoint | null
```

Returns `PicturePoint | null`. stable since API 1.

Errors:

- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.
- `GS-API-098` when this logic may not read your keys and cursor yet. Allow it when the phone's mirror window asks; logic made in GameScrcpy and profiles imported with confirmation may read them from the start.

```ts
const at = input.cursor();
```

## input.isDown(name)

Whether a key or mouse button is held; always false when the phone's mirror window is not in front.

```ts
input.isDown(name: InputName): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `InputName` | A key or mouse button, written as in profiles: "KeyW", "Space", "Mouse.Left". |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-013` when the name is not a string. Write the key as in profiles, such as "KeyW".
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.
- `GS-API-098` when this logic may not read your keys and cursor yet. Allow it when the phone's mirror window asks; logic made in GameScrcpy and profiles imported with confirmation may read them from the start.

```ts
if (input.isDown("ShiftLeft")) log.info("walking");
```
