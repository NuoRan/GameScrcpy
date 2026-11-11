# media

The phone's picture as frames, for analysis and AI; the logic of an extension needs the permission media.read.

## media.frames(handler, options?)

Hands the phone's picture to a handler: the frame shown when it starts, then new ones, at most fps a second (2 by default), made smaller so that the long side is at most size pixels (640 by default), and cut to region when one is given. The image is a CapturedImage as vision.capture() gives; it stays usable while its handler runs and until two newer frames came. Frames taken in a task stop when the task ends, ones taken at the top level when the logic stops.

```ts
media.frames(handler: (frame: TaskContext & { readonly image: CapturedImage; readonly time: number }) => void | Promise<void>, options?: MediaFrameOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `handler` | `(frame: TaskContext & { readonly image: CapturedImage; readonly time: number }) => void \| Promise<void>` | Runs as a task for each frame with its picture and the time the phone made it, in milliseconds of the phone's own clock; a frame that comes while it still runs is left out. |
| `options` (optional) | `MediaFrameOptions` | How often, how large and which part of the picture. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-120` when fps is not from 0.1 to 10, size is not a whole number from 64 to 1920, or the region is not a part of the picture. Pass fps from 0.1 to 10, size from 64 to 1920 and a region in ratios from 0 to 1.
- `GS-API-121` when the phone shows no picture, such as a touch box without a capture card. Take frames on a phone whose picture is shown.
- `GS-API-122` when this logic takes frames with 4 subscriptions already. Dispose a subscription it no longer needs.
- `GS-API-072` when the logic belongs to an extension whose manifest does not ask for the permission media.read. Add "media.read" to the permissions in the extension's manifest, with a reason.
- `GS-API-073` when the logic has no phone, such as the app script of an extension. Take frames in a session script.

```ts
media.frames(({ image, time }) => log.info(time, image.width, image.height), { fps: 1, size: 320 });
```
