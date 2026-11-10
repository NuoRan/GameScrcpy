# ui

Messages, questions and drawings in the phone's mirror window.

## ui.toast(message)

Shows a short message in the phone's mirror window.

```ts
ui.toast(message: string): void
```

| Parameter | Type | Description |
|---|---|---|
| `message` | `string` | What to show; longer than 100 characters is cut short. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the message or question is missing or not a string. Pass the text as a string.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
ui.toast("Round 2");
```

## ui.confirm(question, options?)

Asks a yes or no question in the phone's mirror window and resolves with the answer; cancelling the task closes the question.

```ts
ui.confirm(question: string, options?: ConfirmOptions): Promise<boolean>
```

| Parameter | Type | Description |
|---|---|---|
| `question` | `string` | The question, up to 200 characters. |
| `options` (optional) | `ConfirmOptions` | Other words for the two buttons. |

Returns `Promise<boolean>`. stable since API 1.

Errors:

- `GS-API-011` when the message or question is missing or not a string. Pass the text as a string.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
if (await ui.confirm("Sell the old gear?")) log.info("selling");
```

## ui.choose(question, choices)

Offers choices in the phone's mirror window and resolves with the index chosen, or null when it is closed.

```ts
ui.choose(question: string, choices: string[]): Promise<number | null>
```

| Parameter | Type | Description |
|---|---|---|
| `question` | `string` | The question, up to 200 characters. |
| `choices` | `string[]` | Two to eight choices, each up to 60 characters. |

Returns `Promise<number | null>`. stable since API 1.

Errors:

- `GS-API-011` when the question is missing or not a string. Pass the question as a string.
- `GS-API-012` when the choices are not two to eight strings. Pass between two and eight short strings.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const mode = await ui.choose("Mode", ["Easy", "Hard"]);
```

## ui.ask(question, options?)

Asks for a line of text in the phone's mirror window and resolves with it, or null when it is closed.

```ts
ui.ask(question: string, options?: AskOptions): Promise<string | null>
```

| Parameter | Type | Description |
|---|---|---|
| `question` | `string` | The question, up to 200 characters. |
| `options` (optional) | `AskOptions` | The text already in the box. |

Returns `Promise<string | null>`. stable since API 1.

Errors:

- `GS-API-011` when the message or question is missing or not a string. Pass the text as a string.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const team = await ui.ask("Team name");
```

## ui.box(x, y, width, height, options?)

Draws a box on the phone picture until it is disposed, its duration ends or the task that drew it ends.

```ts
ui.box(x: number, y: number, width: number, height: number, options?: BoxOptions): Drawing
```

| Parameter | Type | Description |
|---|---|---|
| `x` | `number` | Left edge, as a ratio of the picture width. |
| `y` | `number` | Top edge, as a ratio of the picture height. |
| `width` | `number` | Width as a ratio of the picture width; the box stays inside the picture. |
| `height` | `number` | Height as a ratio of the picture height. |
| `options` (optional) | `BoxOptions` | Color, label and how long it stays. |

Returns `Drawing`. stable since API 1.

Errors:

- `GS-API-002` when a position or size is outside 0 to 1, or the box leaves the picture. Positions and sizes are picture ratios.
- `GS-API-003` when the duration is not more than 0 and at most 600000. Pass a duration in milliseconds.
- `GS-API-010` when 64 drawings are already shown. Dispose the drawings you no longer need.
- `GS-API-011` when the text, label or options are not strings or an object. Pass strings and a plain object.
- `GS-API-014` when the color is not one of DrawColor. Use accent, good, warn, bad or white.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const found = ui.box(0.4, 0.3, 0.2, 0.1, { color: "good", label: "chest" });
```

## ui.text(x, y, text, options?)

Writes text on the phone picture until it is disposed, its duration ends or the task that drew it ends.

```ts
ui.text(x: number, y: number, text: string, options?: TextOptions): Drawing
```

| Parameter | Type | Description |
|---|---|---|
| `x` | `number` | Left edge, as a ratio of the picture width. |
| `y` | `number` | Top edge, as a ratio of the picture height. |
| `text` | `string` | What to write; longer than 100 characters is cut short. |
| `options` (optional) | `TextOptions` | Color and how long it stays. |

Returns `Drawing`. stable since API 1.

Errors:

- `GS-API-002` when a position or size is outside 0 to 1, or the box leaves the picture. Positions and sizes are picture ratios.
- `GS-API-003` when the duration is not more than 0 and at most 600000. Pass a duration in milliseconds.
- `GS-API-010` when 64 drawings are already shown. Dispose the drawings you no longer need.
- `GS-API-011` when the text, label or options are not strings or an object. Pass strings and a plain object.
- `GS-API-014` when the color is not one of DrawColor. Use accent, good, warn, bad or white.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
ui.text(0.05, 0.05, "Round 2", { durationMs: 3000 });
```

## ui.mark(x, y, options?)

Marks a point on the phone picture with a ring and a dot until it is disposed, its duration ends or the task that drew it ends; the label shows to the right of the ring.

```ts
ui.mark(x: number, y: number, options?: MarkOptions): Drawing
```

| Parameter | Type | Description |
|---|---|---|
| `x` | `number` | Where the point is, as a ratio of the picture width. |
| `y` | `number` | Where the point is, as a ratio of the picture height. |
| `options` (optional) | `MarkOptions` | Color, label and how long it stays. |

Returns `Drawing`. stable since API 1.

Errors:

- `GS-API-002` when a position or size is outside 0 to 1, or the box leaves the picture. Positions and sizes are picture ratios.
- `GS-API-003` when the duration is not more than 0 and at most 600000. Pass a duration in milliseconds.
- `GS-API-010` when 64 drawings are already shown. Dispose the drawings you no longer need.
- `GS-API-011` when the text, label or options are not strings or an object. Pass strings and a plain object.
- `GS-API-014` when the color is not one of DrawColor. Use accent, good, warn, bad or white.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const spot = ui.mark(0.5, 0.8, { color: "warn", label: "tap here", durationMs: 2000 });
```

## ui.image(image, region?, options?)

Shows a picture on the phone picture in a thin frame until it is disposed, its duration ends or the task that drew it ends. An image from vision.capture() that is no longer kept shows nothing.

```ts
ui.image(image: TemplateName | ImagePath | CapturedImage, region?: Region | RegionName, options?: BoxOptions): Drawing
```

| Parameter | Type | Description |
|---|---|---|
| `image` | `TemplateName \| ImagePath \| CapturedImage` | A template name, a picture in the profile folder or an image from vision.capture(). |
| `region` (optional) | `Region \| RegionName` | Where to show it, stretched to fit; an image from vision.capture() goes where it was taken by default. |
| `options` (optional) | `BoxOptions` | The frame's color, a label and how long it stays. |

Returns `Drawing`. stable since API 1.

Errors:

- `GS-API-015` when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
- `GS-API-018` when there is no template or region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-002` when the region is missing for a template or a picture file, or is outside the picture. Regions are picture ratios.
- `GS-API-003` when the duration is not more than 0 and at most 600000. Pass a duration in milliseconds.
- `GS-API-010` when 64 drawings are already shown. Dispose the drawings you no longer need.
- `GS-API-011` when the label or options are not a string or an object. Pass strings and a plain object.
- `GS-API-014` when the color is not one of DrawColor. Use accent, good, warn, bad or white.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const score = await vision.capture(res.region("Score")); if (score) ui.image(score, { x: 0.7, y: 0.02, width: 0.28, height: 0.08 });
```

## ui.progress(title, value?)

Shows a card at the top of the phone picture with the title, a bar and the percentage. Each logic and each task has one card: calling again updates it, and it goes away with ui.progress(null) or when the logic or task stops.

```ts
ui.progress(title: string | null, value?: number): void
```

| Parameter | Type | Description |
|---|---|---|
| `title` | `string \| null` | What the card says, up to 80 characters; null takes the card away. |
| `value` (optional) | `number` | How far along, from 0 to 1; leave it out only with null. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-017` when the progress is not a number from 0 to 1. Pass a ratio such as done / total.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
ui.progress("Rounds", done / total);
```

## ui.notify(title, text?)

Shows a Windows notification; clicking it opens the phone's mirror window. Each logic and each task shows at most one every 10 seconds; false means this one was not shown.

```ts
ui.notify(title: string, text?: string): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `title` | `string` | The title, up to 64 characters. |
| `text` (optional) | `string` | The line under the title, up to 256 characters. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
ui.notify("Done", "All 30 rounds finished");
```

## ui.panel(name)

Gives a panel of this extension, declared in contributes.panels: a side panel of the mirror window or a page of the main window. GameScrcpy draws the tree given to render() with its own controls; no interface code of the extension runs.

```ts
ui.panel(name: string): UiPanel
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The name the extension's manifest gives the panel in contributes.panels. |

Returns `UiPanel`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass the name from contributes.panels.

```ts
const stats = ui.panel("stats"); stats.render({ type: "text", text: "Rounds: 3" });
```

## ui.menu(items)

Puts this extension's group of items into the right-click menu on the phone picture, in place of the last ones; ui.menu.on("select", handler) hears the item picked.

```ts
ui.menu(items: readonly UiMenuItem[]): void
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `readonly UiMenuItem[]` | Up to 16 items; an empty list takes the group away. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-075` when the items are not a list of up to 16 items with an id and a title each; the message names the item that is wrong. Fix the item the message names.
- `GS-API-076` when the extension declares no right-click menu for this logic, or the logic is not an extension's. Declare a panel with where "menu" whose script is this one.

```ts
ui.menu([{ id: "reset", title: "Reset the counter" }]);
```

## ui.status(name)

Gives a status widget of this extension, declared in contributes.panels: a little text, icons and progress shown with the status of the mirror window.

```ts
ui.status(name: string): UiStatus
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The name the extension's manifest gives the widget in contributes.panels. |

Returns `UiStatus`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass the name from contributes.panels.

```ts
ui.status("fps").render({ type: "text", text: "60" });
```

## ui.menu.on(type, handler, options?)

Starts a task each time an item of this extension's group in the right-click menu is picked.

```ts
ui.menu.on(type: "select", handler: (event: UiEvent) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `type` | `"select"` | The event: select, when an item of the group is picked. |
| `handler` | `(event: UiEvent) => void \| Promise<void>` | What to run; event.id is the item's id, and event.value its new checked state or null. |
| `options` (optional) | `ListenOptions` | What happens when it comes again while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-017` when the event is not select. Listen to "select".
- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
ui.menu.on("select", ({ id }) => log.info("picked", id));
```
