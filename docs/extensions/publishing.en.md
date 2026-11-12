# Packing and publishing

[简体中文](publishing.md) · English

An extension is published as a `.gsext` file: a zip with `manifest.json` and the extension's other files at the top. Users install it with "Install from file…" on the Extensions page, or by clicking a `gamescrcpy://install` link on a web page.

## Packing: `gs pack`

```powershell
gs pack .\lap-timer
gs pack .\lap-timer -o .\dist\alice.lap-timer-1.2.0.gsext
gs pack .\lap-timer --sign $env:USERPROFILE\keys\alice.key
```

- It first runs every check of `gs validate` and packs nothing when there are errors.
- By default it writes `<extension id>-<version>.gsext` next to the folder; `-o` picks another place, but never inside the folder being packed.
- Every file in the folder goes into the package, except files and folders whose names start with a dot (`.git`, `.build`, `.vscode` and the like). Whether sources, tests and a `README` ship with it is up to you; move out what should not ship, or keep it in a folder that starts with a dot.
- A package holds at most 4096 files, each at most 64 MiB, at most 256 MiB unpacked in all, compressed at most 100 to 1.
- `--official` is for GameScrcpy's own extensions only (it allows ids starting with `gamescrcpy.`).

## Versions and updates

- Raise `version` with every release (semantic versioning: the patch number for fixes, the minor number for new features, the major number for incompatible changes). Installing a new version replaces the old one; settings and allowed permissions stay with the `id`.
- When a new version asks for more permissions (or more hosts in `network`, more folders in `files.paths`) or for new required dependencies, users have to turn the extension on again and agree before it loads. Ask for what you need at once, with specific reasons.
- For an extension whose manifest has `updates.index`, GameScrcpy can tell users about new versions (see [Update lists](#update-lists) below); for the others, tell users on your release page, repository or web site, best with an install link (see below).
- Do not change the `id`: a new id is another extension, and settings, permissions and the signature do not carry over.

## Signing

The first time this computer sees the key of a signed package, the install dialog and the card say "Signed with key …, which this computer has not seen before"; only later versions signed with the same key show "Verified publisher · key …". Both name the key, never the name the author gives in the manifest. Every later update must be signed with the same key: a package someone else changed, or an update published in your name, does not install. An unsigned native extension gets a prominent warning in the install dialog; an unsigned restricted one gets only a note.

1. Make a key pair (once):

   ```powershell
   gs ext keygen $env:USERPROFILE\keys\alice.key
   ```

   It writes the secret key `alice.key` and the public key `alice.pub`, and prints the line to put into the manifest, such as `"authorKey": "RWQ…"`. Keep the secret key to yourself: whoever has it can sign extensions as you. `gs` cannot use a minisign secret key with a password; take the password off a copy with `minisign -C -W`.
2. Put `authorKey` into `manifest.json`.
3. Sign while packing: `gs pack <folder> --sign <secret key file>`. A key that does not match `authorKey` gives `signing-key-mismatch`. After packing, `gs` checks the signature once more and prints "signed with key <key id>".

The signature is minisign-compatible, so you can sign with your own tools too: put `SHA512SUMS` into the package (one line for every other file of the package: the SHA-512 as 128 hex digits, two spaces and the package path) and `SHA512SUMS.minisig` (a minisign signature of `SHA512SUMS`), with the matching public key as `authorKey` in the manifest.

How GameScrcpy treats signatures:

- Files that do not match `SHA512SUMS`, files added or missing, or a signature made with another key than `authorKey`: it does not install (`signature-invalid`).
- An installed version that is signed and a new package that is not: it does not install (`signature-missing`).
- A new package signed with another key: it does not install (`publisher-changed`). To switch keys, users have to remove the old extension first and install again, so back up your secret key; if it is lost, existing users can only remove and reinstall.
- The key in the package is trusted from the first install on and every later version is checked against it, so publish your public key somewhere else as well (in your repository's README, for example) for users to compare the first time.
- An older version of the same id can be installed again (to go back), with the same signature rules.

## Install links

Put a link on a web page, a forum or a README, and clicking it opens the install dialog in GameScrcpy:

```html
<a href="gamescrcpy://install?url=https%3A%2F%2Fexample.com%2Fdownloads%2Falice.lap-timer-1.2.0.gsext&sha256=9f2b5c0e4d7a8b1c3e5f6a7b8c9d0e1f2a3b4c5d6e7f8091a2b3c4d5e6f70812">Install in GameScrcpy</a>
```

- `url` is where the package downloads from, an `https://` address with a host and without a user name, URL-encoded and at most 2048 characters once decoded; `sha256` is the package's SHA-256 as 64 hex digits. Each appears once, with no other parameters and no `#`; the whole link is printable ASCII, at most 4096 characters.
- GameScrcpy downloads the package (following at most 8 redirects, all of them https, up to 200 MiB) and installs nothing when its SHA-256 does not match the link; when it matches, the usual install dialog opens and says where it was downloaded from and that the SHA-256 matches, and the checks and consent are the same as for a file. The source it shows is the host the download ended at (after redirects), with internationalized names in their ASCII form (`xn--…`), so that look-alike letters cannot pass for another site.
- `gamescrcpy://extension/<extension id>` opens the Extensions page at the extension's card, which suits setup instructions.
- The installer registers `gamescrcpy://`; the portable build registers it with one click on the "Extension links" card in Settings › General. When GameScrcpy is already running, the link goes to that copy.
- A link has to be the only argument on the command line: with anything after `GameScrcpy.exe "gamescrcpy://…"`, GameScrcpy exits with a failure and does nothing, so a web page cannot use a link to add options to the program. Browsers and Windows pass a link as the only argument.
- One at a time: while the package of an earlier link waits to be installed, a new link is refused with a hint to finish the first; a package that arrives while an install dialog is open is let go, with a hint to close the dialog and open the link again.

PowerShell computes the SHA-256 and encodes the URL:

```powershell
(Get-FileHash .\alice.lap-timer-1.2.0.gsext -Algorithm SHA256).Hash.ToLower()
[uri]::EscapeDataString("https://example.com/downloads/alice.lap-timer-1.2.0.gsext")
```

The SHA-256 changes with every release, so the link has to change with it.

## Update lists

For an extension whose manifest has `updates.index` (see the [manifest reference](manifest.en.md#updates)), GameScrcpy fetches an update list from that address to see whether there is a newer version. One list can serve several of your extensions:

```json
{
  "format": "gamescrcpy.extension-index",
  "version": 1,
  "extensions": [
    {
      "id": "alice.lap-timer",
      "versions": [
        {
          "version": "1.3.0",
          "url": "https://example.com/downloads/alice.lap-timer-1.3.0.gsext",
          "sha256": "9f2b5c0e4d7a8b1c3e5f6a7b8c9d0e1f2a3b4c5d6e7f8091a2b3c4d5e6f70812",
          "size": 48213,
          "publisher": "RWQ…",
          "requires": { "script": "^1.1", "panel": "^1.0" },
          "released": "2026-12-01",
          "notes": "Laps can be named."
        }
      ]
    }
  ]
}
```

| Field | Rule |
|:--|:--|
| `format`, `version` | Always `gamescrcpy.extension-index` and `1` |
| `extensions` | Up to 256 extensions, each `id` listed once |
| `versions` | 1 to 128 versions for each extension, each version listed once |
| `url` | Where the package downloads from, by the rules of `updates.index`: https, with a host, without a user name |
| `sha256` | The package's SHA-256 as 64 hex digits |
| `size` | The package's size in bytes, from 1 byte to 200 MiB |
| `publisher` | Optional: the minisign public key that signs the package (as `authorKey` in the manifest) |
| `requires` | Optional, written as `requires` in the manifest (up to 32 entries); a version that needs an extension point or version this GameScrcpy does not offer is not offered |
| `released` | Optional: the release date as `YYYY-MM-DD` |
| `notes` | Optional: what is new, at most 4096 bytes, shown in the extension's Details |

- A list is at most 1 MiB and 8 levels deep and is read strictly as a whole: an extra field, a repeated field or a wrong value voids the whole list, and the extension's Details show the problem and where it is (such as `$.extensions[0].versions[1].sha256: not 64 hexadecimal digits`).
- Which version is offered: the highest one that is newer than both the installed version and the version the user skipped and that can run on this computer; prerelease versions (such as `1.3.0-beta.1`) are offered only to users who have a prerelease installed.
- When it is fetched: when the user chooses "Check for updates" on the Extensions page, and, with "Check extensions for updates" on in Settings › General (off by default), at most once a day when GameScrcpy starts. Only https, with redirects only to https and at most 8 of them, through the system proxy, without cookies or a cache; 30 seconds without data count as a failure, and the request does not carry the extension's id. Failures do not bother the user: one log line, the reason in the extension's Details, and an earlier offer stays; when the list no longer has the extension, its offer goes away.
- Downloading and installing: only when the user chooses "Update" (one download at a time, with progress and a way to cancel). The package has to match the size and SHA-256 of the list, contain the id and version the list gives and be able to run on this computer; when the list gives a `publisher`, the package must be signed with that key; when the installed version is signed, the update must be signed with the same key, and a changed key is always refused (to accept a new key, users remove the old extension first and install again). When the installed version is not signed, a signed update installs as usual and its key is checked from then on. Only then does the usual install dialog open, with the button "Update" and "Changes since version X" (permissions and dependencies added or removed, interface versions); when it asks for more than before, it warns that the extension stays off until the user turns it on and agrees. Nothing installs by itself.
- Publishing a new version: pack and sign it, upload the package to its address, add the version to `versions` in the list (with the `size` and `sha256` of the final package), and upload the list last. With the package up before the list, users never see a version they cannot download.

## Bundles

"Export bundle…" on the Extensions page exports the extensions that are on, with their settings, into one `.gsbundle`, and "Install from file…" installs the whole set at once. It is a zip too: `bundle.json` (`schemaVersion` 1, `name`, `description`, and `extensions` with the `id`, `version`, whether it is built in, whether it is on and the values of its settings for each) plus `extensions/<extension id>-<version>.gsext`, up to 64 extensions. `gs validate <bundle>` checks every package in it.

## Before you publish

- `gs validate` reports no errors, ideally no warnings; scripts pass `gs check`; tests pass `gs ext test` (and native extensions once more with `--hosted`).
- `requires` names the version of each extension point you wrote against, with the new minor version wherever you use something new (native extensions write `"native": "^2.0"`).
- The `id` has a prefix of your own, and `license`, `authors` and `repository` are filled in; `description` says in a sentence or two what the extension does.
- Every permission's `reason` is something users understand.
- Names, descriptions, panel titles, tours and help have translations (`l10n` and `locales`).
- Native extensions: the DLL links the runtime statically with `/MT`, and `gs validate` shows that it exports `gs_ext_main`; ship an arm64 DLL too when you can. When it needs no files, network or other programs, consider `"sandbox": "restricted"`, which users can install with more confidence.
- The package is signed with `gs pack --sign`, the secret key is backed up, and the public key is published in your repository or on your site.
- With `updates.index`: once the new package is uploaded, the update list has the new version.
- Install it once in GameScrcpy from the file, turn it on, allow its permissions and walk through what it does.
