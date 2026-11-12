# files

Files the user picks and folders the user chooses for the logic. The logic of an extension needs the permission files.pick to pick files, and files.paths, which names its folders, to open folders.

## files.pick(options?)

Shows the Windows dialog to open a file, or with save to save one, and resolves with a handle to the file the user picked, or with null when the user cancels. The handle reads and writes only that file, up to 64 MiB, and lasts until the logic stops. The logic of an extension needs the permission files.pick.

```ts
files.pick(options?: FilePickOptions): Promise<FileHandle | null>
```

| Parameter | Type | Description |
|---|---|---|
| `options` (optional) | `FilePickOptions` | Whether to save, which kinds of file to offer and the name to start with. |

Returns `Promise<FileHandle | null>`. stable since API 1.

Errors:

- `GS-API-160` when the logic belongs to an extension whose manifest does not ask for the permission files.pick. Add "files.pick" to the permissions in the extension's manifest, with a reason.
- `GS-API-161` when the filters or the suggested name are not right. Pass filters such as { name: "Images", extensions: ["png", "jpg"] } and a file name without a folder.
- `GS-API-163` when the logic has 64 files and folders open, or 8 requests for files and folders are waiting. Close the handles you no longer need, or wait for the user to answer.

```ts
const file = await files.pick({ filters: [{ name: "Profiles", extensions: ["json"] }] }); if (file) log.info(file.name, (await file.text()).length);
```

## files.folder(name)

Opens a folder chosen for this logic. The first time, GameScrcpy asks the user to choose the real folder and remembers it; the user can forget it in the extension's settings, or for your own logic in Settings, General. The handle lists, reads, writes and removes what is inside that folder and nothing else, and lasts until the logic stops.

```ts
files.folder(name: string): Promise<FolderHandle>
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | A folder that the extension's manifest names under the permission files.paths, such as "exports"; your own logic may use any name of 1 to 32 lowercase letters, digits and hyphens. |

Returns `Promise<FolderHandle>`. stable since API 1.

Errors:

- `GS-API-160` when the logic belongs to an extension whose manifest does not name the folder under the permission files.paths. Name the folder in folders of the permission files.paths, such as { "name": "exports", "purpose": "Where it saves your recordings." }.
- `GS-API-161` when the name is not 1 to 32 lowercase letters, digits and hyphens. Use the name as the manifest declares it.
- `GS-API-163` when the logic has 64 files and folders open, or 8 requests for files and folders are waiting. Close the handles you no longer need, or wait for the user to answer.
- `GS-API-164` when the user closed the dialog without choosing a folder. Ask again when the user wants it, and say in the purpose why the extension needs the folder.

```ts
const exports = await files.folder("exports"); await exports.write("scores/today.csv", "time,score\n");
```
