# vision

Finding pictures in the phone's picture: the next frame, or the picture already shown when none comes within 0.25 s.

## vision.find(image, options?)

Looks for a picture in the next frame, or in the picture already shown when no new frame comes within 0.25 s, and resolves with the best place; null when it is not there, or when there is no picture before the timeout.

```ts
vision.find(image: TemplateName | ImagePath | CapturedImage, options?: FindOptions): Promise<Found | null>
```

| Parameter | Type | Description |
|---|---|---|
| `image` | `TemplateName \| ImagePath \| CapturedImage` | A template name from res/resources.json, whose threshold, match mode, region and picture size are the defaults, a picture in the profile folder such as "res/chest.png" (png, jpg or bmp) cut from the mirrored picture at the same size, or an image from vision.capture(), compared in color and 0.9 alike by default like a template cut in the vision workbench; transparent pixels are ignored. |
| `options` (optional) | `FindOptions` | Where to look, how alike it must be, how many and how long to wait for a picture. |

Returns `Promise<Found | null>`. stable since API 1.

Errors:

- `GS-API-015` when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
- `GS-API-018` when there is no template or region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-016` when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
- `GS-API-037` when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-017` when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
- `GS-API-019` when match is not "color" or "grey". Write one of the two.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const chest = await vision.find("res/chest.png"); if (chest) await touch.tap(chest.x, chest.y);
```

## vision.findAll(image, options?)

Looks for every place a picture appears in the next frame (or the picture already shown), best first, without overlapping matches.

```ts
vision.findAll(image: TemplateName | ImagePath | CapturedImage, options?: FindOptions): Promise<Found[]>
```

| Parameter | Type | Description |
|---|---|---|
| `image` | `TemplateName \| ImagePath \| CapturedImage` | A template name from res/resources.json, whose threshold, match mode, region and picture size are the defaults, a picture in the profile folder such as "res/chest.png" (png, jpg or bmp) cut from the mirrored picture at the same size, or an image from vision.capture(), compared in color and 0.9 alike by default like a template cut in the vision workbench; transparent pixels are ignored. |
| `options` (optional) | `FindOptions` | Where to look, how alike it must be, how many and how long to wait for a picture. |

Returns `Promise<Found[]>`. stable since API 1.

Errors:

- `GS-API-015` when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
- `GS-API-018` when there is no template or region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-016` when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
- `GS-API-037` when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-017` when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
- `GS-API-019` when match is not "color" or "grey". Write one of the two.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const coins = await vision.findAll("res/coin.png", { max: 8 });
```

## vision.findColor(color, options?)

Looks for a color in the next frame (or the picture already shown) and resolves with its largest group of pixels: a pixel matches when its red, green and blue each differ from the color by at most tolerance times 255, and matching pixels that touch, diagonally too, form one group. x and y are the center of the group's bounding box, width and height its size, and score the share of matching pixels in that box; null when no pixel matches, or when there is no picture before the timeout.

```ts
vision.findColor(color: ColorName | HexColor, options?: ColorOptions): Promise<Found | null>
```

| Parameter | Type | Description |
|---|---|---|
| `color` | `ColorName \| HexColor` | A color written "#RRGGBB", or the name of a color in res/resources.json, whose tolerance and region are the defaults. |
| `options` (optional) | `ColorOptions` | Where to look, how far a pixel may be from the color and how long to wait for a picture. |

Returns `Promise<Found | null>`. stable since API 1.

Errors:

- `GS-API-019` when the color is not a string written "#RRGGBB" or a name. Write it as "#F5C542" or use a name.
- `GS-API-018` when there is no color or region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-017` when the tolerance is not from 0 to 1. Use a ratio.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const banner = await vision.findColor("#F5C542", { tolerance: 0.15 }); if (banner) await touch.tap(banner.x, banner.y);
```

## vision.pickColor(at, options?)

Reads the color of one pixel in the next frame, or in the picture already shown when no new frame comes within 0.25 s, and resolves with it written "#RRGGBB"; null when there is no picture before the timeout.

```ts
vision.pickColor(at: PicturePoint, options?: PixelOptions): Promise<HexColor | null>
```

| Parameter | Type | Description |
|---|---|---|
| `at` | `PicturePoint` | The pixel to read, as picture ratios. |
| `options` (optional) | `PixelOptions` | How long to wait for a picture. |

Returns `Promise<HexColor | null>`. stable since API 1.

Errors:

- `GS-API-002` when the position is not a point inside the picture. Pass a point such as point(0.5, 0.5).
- `GS-API-011` when the options are not an object. Pass a plain object.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const sky = await vision.pickColor(point(0.5, 0.05)); log.info("sky", sky);
```

## vision.matchColor(at, color, options?)

Checks one pixel in the next frame (or the picture already shown) against a color: true when its red, green and blue each differ from the color by at most tolerance times 255; false when they do not, or when there is no picture before the timeout.

```ts
vision.matchColor(at: PicturePoint, color: ColorName | HexColor, options?: PixelMatchOptions): Promise<boolean>
```

| Parameter | Type | Description |
|---|---|---|
| `at` | `PicturePoint` | The pixel to compare, as picture ratios. |
| `color` | `ColorName \| HexColor` | A color written "#RRGGBB", or the name of a color in res/resources.json, whose tolerance is the default. |
| `options` (optional) | `PixelMatchOptions` | How far the pixel may be from the color and how long to wait for a picture. |

Returns `Promise<boolean>`. stable since API 1.

Errors:

- `GS-API-002` when the position is not a point inside the picture. Pass a point such as point(0.5, 0.5).
- `GS-API-019` when the color is not a string written "#RRGGBB" or a name. Write it as "#F5C542" or use a name.
- `GS-API-018` when there is no color with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-011` when the options are not an object. Pass a plain object.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-017` when the tolerance is not from 0 to 1. Use a ratio.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
if (await vision.matchColor(point(0.9, 0.1), "#E53935")) await touch.tap(0.9, 0.1);
```

## vision.waitFor(image, options?)

Looks for a picture again and again until it appears and resolves with where it is; null when it has not appeared within timeoutMs.

```ts
vision.waitFor(image: TemplateName | ImagePath | CapturedImage, options?: WaitOptions): Promise<Found | null>
```

| Parameter | Type | Description |
|---|---|---|
| `image` | `TemplateName \| ImagePath \| CapturedImage` | A template name, a picture in the profile folder or an image from vision.capture(), as for find(). |
| `options` (optional) | `WaitOptions` | Where to look and how alike it must be, as for find(), and how long and how often to look. |

Returns `Promise<Found | null>`. stable since API 1.

Errors:

- `GS-API-015` when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
- `GS-API-018` when there is no template or region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-016` when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
- `GS-API-037` when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-017` when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
- `GS-API-019` when match is not "color" or "grey". Write one of the two.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const start = await vision.waitFor("res/start.png", { timeoutMs: 30000 }); if (start) await touch.tap(start);
```

## vision.waitGone(image, options?)

Looks for a picture again and again until it is gone; true when it went away, false when it was still there after timeoutMs.

```ts
vision.waitGone(image: TemplateName | ImagePath | CapturedImage, options?: WaitOptions): Promise<boolean>
```

| Parameter | Type | Description |
|---|---|---|
| `image` | `TemplateName \| ImagePath \| CapturedImage` | A template name, a picture in the profile folder or an image from vision.capture(), as for find(). |
| `options` (optional) | `WaitOptions` | Where to look and how alike it must be, as for find(), and how long and how often to look. |

Returns `Promise<boolean>`. stable since API 1.

Errors:

- `GS-API-015` when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
- `GS-API-018` when there is no template or region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-016` when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
- `GS-API-037` when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-017` when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
- `GS-API-019` when match is not "color" or "grey". Write one of the two.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await vision.waitGone("res/loading.png", { timeoutMs: 60000 });
```

## vision.findAndTap(image, options?)

Looks for a picture once and taps the middle of the best place; resolves with where it was, or null without tapping when it is not there.

```ts
vision.findAndTap(image: TemplateName | ImagePath | CapturedImage, options?: FindOptions): Promise<Found | null>
```

| Parameter | Type | Description |
|---|---|---|
| `image` | `TemplateName \| ImagePath \| CapturedImage` | A template name from res/resources.json, whose threshold, match mode, region and picture size are the defaults, a picture in the profile folder such as "res/chest.png" (png, jpg or bmp) cut from the mirrored picture at the same size, or an image from vision.capture(), compared in color and 0.9 alike by default like a template cut in the vision workbench; transparent pixels are ignored. |
| `options` (optional) | `FindOptions` | Where to look, how alike it must be, how many and how long to wait for a picture. |

Returns `Promise<Found | null>`. stable since API 1.

Errors:

- `GS-API-015` when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
- `GS-API-018` when there is no template or region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-016` when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
- `GS-API-037` when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-017` when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
- `GS-API-019` when match is not "color" or "grey". Write one of the two.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await vision.findAndTap("res/close.png");
```

## vision.watch(image, onAppear, onGone?, options?)

Keeps looking for a picture, starting onAppear each time it appears and onGone each time it goes away, each run as its own task; dispose() stops looking.

```ts
vision.watch(image: TemplateName | ImagePath | CapturedImage, onAppear: (event: TaskContext & Found) => void | Promise<void>, onGone?: (event: TaskContext) => void | Promise<void>, options?: WatchOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `image` | `TemplateName \| ImagePath \| CapturedImage` | A template name, a picture in the profile folder or an image from vision.capture(), as for find(). |
| `onAppear` | `(event: TaskContext & Found) => void \| Promise<void>` | What to run when the picture appears; the event says where it is. |
| `onGone` (optional) | `(event: TaskContext) => void \| Promise<void>` | What to run when the picture goes away. |
| `options` (optional) | `WatchOptions` | Where to look and how alike it must be, as for find(), how often to look and what happens when it comes again while the last task is still running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-015` when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
- `GS-API-018` when there is no template or region with that name. Use a name from res/resources.json.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
vision.watch("res/revive.png", (found) => touch.tap(found), () => log.info("gone"));
```

## vision.capture(region?, options?)

Takes part of the next frame, or of the picture already shown when no new frame comes within 0.25 s, as an image: look for it later with find(), show it with ui.image() or keep it with save(). Resolves with null when there is no picture before the timeout. A script keeps its 8 latest images; older ones can no longer be used.

```ts
vision.capture(region?: Region | RegionName, options?: PixelOptions): Promise<CapturedImage | null>
```

| Parameter | Type | Description |
|---|---|---|
| `region` (optional) | `Region \| RegionName` | The part to take; the whole picture by default. |
| `options` (optional) | `PixelOptions` | How long to wait for a picture. |

Returns `Promise<CapturedImage | null>`. stable since API 1.

Errors:

- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-018` when there is no region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-011` when the options are not an object. Pass a plain object.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const before = await vision.capture(res.region("Score"));
```

## vision.changed(region?, options?)

Looks at part of the next frame (or the picture already shown) and compares it with how it looked the last time changed() looked at the same region: true when it changed. The first look only remembers it and resolves with false, as does a look without a picture before the timeout.

```ts
vision.changed(region?: Region | RegionName, options?: ChangeOptions): Promise<boolean>
```

| Parameter | Type | Description |
|---|---|---|
| `region` (optional) | `Region \| RegionName` | The part to look at; the whole picture by default. |
| `options` (optional) | `ChangeOptions` | How much must change and how long to wait for a picture. |

Returns `Promise<boolean>`. stable since API 1.

Errors:

- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-018` when there is no region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-011` when the options are not an object. Pass a plain object.
- `GS-API-017` when the threshold is not from 0 to 1. Use a ratio.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
if (await vision.changed(res.region("Score"))) log.info("the score changed");
```

## vision.onChange(region, handler, options?)

Keeps looking at part of the picture and starts a task each time it changed since the last look; the first look only remembers it. dispose() stops looking.

```ts
vision.onChange(region: Region | RegionName, handler: (event: TaskContext) => void | Promise<void>, options?: OnChangeOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `region` | `Region \| RegionName` | The part to keep looking at. |
| `handler` | `(event: TaskContext) => void \| Promise<void>` | What to run each time it changed. |
| `options` (optional) | `OnChangeOptions` | How much must change, how often to look and what happens when it changes again while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-018` when there is no region with that name. Use a name from res/resources.json.
- `GS-API-017` when the threshold is not from 0 to 1. Use a ratio.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
vision.onChange(res.region("Chat"), () => ui.notify("New message"));
```

## vision.readText(options?)

Reads the text in the next frame, or in the picture already shown when no new frame comes within 0.25 s, with the text recognition that comes with Windows, and resolves with each line and where it is; null when there is no picture before the timeout. Chinese and Japanese characters are joined without spaces, other words with one. Name the language the game is written in: Chinese recognition reads English words poorly and English recognition cannot read Chinese. Reading a whole picture can take a few tenths of a second, so give a region when you can.

```ts
vision.readText(options?: ReadTextOptions): Promise<TextResult | null>
```

| Parameter | Type | Description |
|---|---|---|
| `options` (optional) | `ReadTextOptions` | Where to read, in which language and how long to wait for a picture. |

Returns `Promise<TextResult | null>`. stable since API 1.

Errors:

- `GS-API-011` when the options are not an object, or the language is not a tag such as "zh-Hans". Pass a plain object and a language tag.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-018` when there is no region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-039` when this computer has no text recognition for the language, or Windows could not read the picture. Add the language in Windows Settings, Time & language, Language & region, or name one the computer has.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const gold = await vision.readText({ region: res.region("Gold") }); if (gold) log.info(gold.text);
```

## vision.findText(text, options?)

Reads the text in the next frame (or the picture already shown) and resolves with where a line says it: a line that says only that comes first, then the first line from the top that contains it, ignoring capitals and spaces. The place covers the words that say it and score is always 1; null when no line says it, or when there is no picture before the timeout.

```ts
vision.findText(text: string, options?: ReadTextOptions): Promise<Found | null>
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text to look for, up to 200 characters; capitals and spaces do not matter. |
| `options` (optional) | `ReadTextOptions` | Where to look, in which language and how long to wait for a picture. |

Returns `Promise<Found | null>`. stable since API 1.

Errors:

- `GS-API-011` when the text is not a string of 1 to 200 characters that are not all spaces, the options are not an object, or the language is not a tag such as "zh-Hans". Pass the words to find, a plain object and a language tag.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-018` when there is no region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-003` when the timeout is not more than 0 and at most 10000. Pass milliseconds.
- `GS-API-039` when this computer has no text recognition for the language, or Windows could not read the picture. Add the language in Windows Settings, Time & language, Language & region, or name one the computer has.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const ok = await vision.findText("OK"); if (ok) await touch.tap(ok);
```
