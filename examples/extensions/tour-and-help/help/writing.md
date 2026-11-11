# Writing help for an extension

The help articles of an extension are Markdown files in its package, listed in `contributes.help` of its manifest. GameScrcpy draws them with the same parts as its own help, so they look like the rest of it.

## In the manifest

```json
"help": [
  { "id": "guide", "title": "The tour and help sample", "file": "help/guide.md",
    "locales": { "zh_CN": "help/guide.zh_CN.md" } }
]
```

`file` is the English text and `locales` gives the files of other languages; when the interface language has no file, the English one shows. The manifest also declares the extension point `help`.

## What an article can use

- Headings, paragraphs and lists. A numbered list shows as steps, and a **bold** start becomes the title of its step.
- `code`, **bold**, keys such as <kbd>F1</kbd>, and paths such as **Settings > Extensions**.
- Callouts that start with `> [!TIP]`, `> [!NOTE]` or `> [!WARNING]`.
- Tables. A table whose first column holds keys shows as a list of shortcuts, like this one:

| Keys | What they do |
|---|---|
| <kbd>F1</kbd> | Opens the help |
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | Opens the command palette |

## Links

- `help:` and the name of one of GameScrcpy's own articles, such as [Connecting a phone](help:connecting).
- `help:ext/` with the extension's id and the article's id, such as [The tour and help sample](help:ext/example.tour-and-help/guide).
- `action:tour:ext/` with the extension's id and the tour's id plays that tour.
- `https://` addresses open in the browser.

> [!WARNING]
> Pictures come only from the extension's own folder, written as relative paths such as `images/step.png`. A picture with a `:` in its path, or one outside the folder, is left out.
