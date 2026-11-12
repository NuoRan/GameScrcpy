# Key list (key-list)

[简体中文](README.md) · English

A pair of converters: it imports a made-up plain-text key list (`.keys`) as a profile and exports profiles in that format. It shows `contributes.converters` and the `convert` function a script exports, and asks for no permission.

## The format

```text
# Arena basics
J 0.82 0.71 Attack
Space 0.94 0.80 Dodge
1 0.40 0.92 Potion
```

- A first line that starts with `#` names the profile; later lines that start with `#` are comments.
- One key on each line: `key x y name`. The key is a letter, a digit, or a key name such as `Space`, `F1` or `ShiftLeft`; `x` and `y` are the position in the picture as ratios from 0 to 1; the name may be left out.
- An import makes a tap control of each line; an export writes the controls bound to a single key that have a position, and counts the others in a comment at the end. For profiles that are not stretched, positions are estimated for a 16:9 picture.

## Files

| File | Contents |
|:--|:--|
| `manifest.json` | Two converters, `keys` → `profile` (reading `.keys` files) and `profile` → `keys`, sharing one script |
| `src/main.ts` | `convert(input)`, telling the import from the export by `input.to` |
| `samples/arena.keys` | A key list to import |
| `tsconfig.json` | For `gs check` and editors |

## Try it

1. `gs validate examples\extensions\key-list`, `gs check examples\extensions\key-list`.
2. `gs pack examples\extensions\key-list -o key-list.gsext`, then "Install from file…" on the Extensions page, and turn it on.
3. Choose Import on the Profiles page: the file types now include "Key list (*.keys)". Pick `samples\arena.keys`, and a profile called "Arena basics" with five tap controls appears. Dropping a `.keys` file on the mirror window imports it too.
4. A profile card's menu now has "Export as Key list…"; export a profile and you get text in the same format.

## How it works

- The `script` of both converters is `src/main.ts`. For each file, GameScrcpy loads it in the logic sandbox and calls `convert` once, without a phone.
- Import: `input.text` is the file read as UTF-8; `profileFrom()` reads it line by line and returns `{ name, json }`, where `json` is a profile in version 3 of `gamescrcpy.profile` and `name` names the new profile. GameScrcpy checks it as it checks any file it imports.
- Export: `input.json()` is the profile's JSON; `textFrom()` writes the text, returned as `{ text }`.
- A line it cannot read throws an error, whose message is what the user sees.
