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
- 2.1.0 does not check for or download updates by itself: tell users about new versions on your release page, repository or web site, best with an install link (see below).
- Do not change the `id`: a new id is another extension, and settings, permissions and the signature do not carry over.

## Signing

A signed package shows users a verified publisher, and every later update must be signed by the same person: a package someone else changed, or an update published in your name, does not install. An unsigned native extension gets a more prominent warning in the install dialog.

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

## Install links

Put a link on a web page, a forum or a README, and clicking it opens the install dialog in GameScrcpy:

```html
<a href="gamescrcpy://install?url=https%3A%2F%2Fexample.com%2Fdownloads%2Falice.lap-timer-1.2.0.gsext&sha256=9f2b5c0e4d7a8b1c3e5f6a7b8c9d0e1f2a3b4c5d6e7f8091a2b3c4d5e6f70812">Install in GameScrcpy</a>
```

- `url` is where the package downloads from, an `https://` address, URL-encoded; `sha256` is the package's SHA-256 as 64 hex digits. Each appears once, with no other parameters and no `#`; the whole link is at most 4096 characters.
- GameScrcpy downloads the package (following at most 8 redirects, all of them https, up to 200 MiB) and installs nothing when its SHA-256 does not match the link; when it matches, the usual install dialog opens and says where it was downloaded from and that the SHA-256 matches, and the checks and consent are the same as for a file.
- `gamescrcpy://extension/<extension id>` opens the Extensions page at the extension's card, which suits setup instructions.
- The installer registers `gamescrcpy://`; the portable build registers it with one click on the "Extension links" card in Settings › General. When GameScrcpy is already running, the link goes to that copy.

PowerShell computes the SHA-256 and encodes the URL:

```powershell
(Get-FileHash .\alice.lap-timer-1.2.0.gsext -Algorithm SHA256).Hash.ToLower()
[uri]::EscapeDataString("https://example.com/downloads/alice.lap-timer-1.2.0.gsext")
```

The SHA-256 changes with every release, so the link has to change with it.

## Bundles

"Export bundle…" on the Extensions page exports the extensions that are on, with their settings, into one `.gsbundle`, and "Install from file…" installs the whole set at once. It is a zip too: `bundle.json` (`schemaVersion` 1, `name`, `description`, and `extensions` with the `id`, `version`, whether it is built in, whether it is on and the values of its settings for each) plus `extensions/<extension id>-<version>.gsext`, up to 64 extensions. `gs validate <bundle>` checks every package in it.

## Before you publish

- `gs validate` reports no errors, ideally no warnings; scripts pass `gs check`; tests pass `gs ext test` (and native extensions once more with `--hosted`).
- The `id` has a prefix of your own, and `license`, `authors` and `repository` are filled in; `description` says in a sentence or two what the extension does.
- Every permission's `reason` is something users understand.
- Names, descriptions, panel titles, tours and help have translations (`l10n` and `locales`).
- Native extensions: the DLL links the runtime statically with `/MT`, and `gs validate` shows that it exports `gs_ext_main`; ship an arm64 DLL too when you can.
- The package is signed with `gs pack --sign`, and the secret key is backed up.
- Install it once in GameScrcpy from the file, turn it on, allow its permissions and walk through what it does.
