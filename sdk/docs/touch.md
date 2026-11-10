# touch

Touches on the phone picture. Positions are ratios of the picture: (0, 0) top left, (1, 1) bottom right.

## touch.tap(x, y?, durationMs?)

Taps the picture at a position; resolves when the finger is lifted, which is at once if the task is cancelled.

```ts
touch.tap(x: number | PicturePoint, y?: number, durationMs?: number): Action<void>
```

| Parameter | Type | Description |
|---|---|---|
| `x` | `number \| PicturePoint` | Where to tap: a ratio of the picture width, or a point. |
| `y` (optional) | `number` | A ratio of the picture height; with a point, the duration instead. |
| `durationMs` (optional) | `number` | How long the finger stays down, in milliseconds (default 60). |

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-002` when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
- `GS-API-003` when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await touch.tap(0.8, 0.9).until(() => vision.find("res/playing.png"), { timeoutMs: 10000, retry: 1 });
```

## touch.longPress(x, y?, durationMs?)

Presses and holds the picture at a position, then lifts the finger.

```ts
touch.longPress(x: number | PicturePoint, y?: number, durationMs?: number): Action<void>
```

| Parameter | Type | Description |
|---|---|---|
| `x` | `number \| PicturePoint` | Where to press: a ratio of the picture width, or a point. |
| `y` (optional) | `number` | A ratio of the picture height; with a point, the duration instead. |
| `durationMs` (optional) | `number` | How long the finger stays down, in milliseconds (default 800). |

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-002` when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
- `GS-API-003` when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await touch.longPress(point(0.5, 0.5), 1200);
```

## touch.swipe(fromX, fromY, toX?, toY?, durationMs?)

Swipes from one position to another; resolves when it ends. If the task is cancelled, the finger is lifted where it is.

```ts
touch.swipe(fromX: number | PicturePoint, fromY: number | PicturePoint, toX?: number, toY?: number, durationMs?: number): Action<void>
```

| Parameter | Type | Description |
|---|---|---|
| `fromX` | `number \| PicturePoint` | Start: a ratio of the picture width, or a point. |
| `fromY` | `number \| PicturePoint` | Start height as a ratio; with points, the end point. |
| `toX` (optional) | `number` | End, as a ratio of the picture width; with points, the duration. |
| `toY` (optional) | `number` | End, as a ratio of the picture height. |
| `durationMs` (optional) | `number` | How long the swipe takes, in milliseconds (default 300). |

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-002` when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
- `GS-API-003` when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await touch.swipe(0.5, 0.8, 0.5, 0.2, 250);
```

## touch.down(x, y?)

Puts a finger down and keeps it there until up() or the end of the task that put it down.

```ts
touch.down(x: number | PicturePoint, y?: number): Finger
```

| Parameter | Type | Description |
|---|---|---|
| `x` | `number \| PicturePoint` | Where to put the finger: a ratio of the picture width, or a point. |
| `y` (optional) | `number` | A ratio of the picture height; leave it out with a point. |

Returns `Finger`. stable since API 1.

Errors:

- `GS-API-002` when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
- `GS-API-003` when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const finger = touch.down(0.3, 0.7); await finger.move(0.6, 0.7, 300); finger.up();
```

## touch.pinch(center, from, to, durationMs?)

Pinches with two fingers around a position: from is how far apart they start and to how far apart they end, as ratios of the picture width, so a growing distance zooms in. If the task is cancelled, both fingers are lifted.

```ts
touch.pinch(center: PicturePoint, from: number, to: number, durationMs?: number): Action<void>
```

| Parameter | Type | Description |
|---|---|---|
| `center` | `PicturePoint` | The position between the two fingers. |
| `from` | `number` | How far apart the fingers start, from 0 to 1. |
| `to` | `number` | How far apart they end, from 0 to 1. |
| `durationMs` (optional) | `number` | How long the fingers take to move; 300 when not given. |

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-002` when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
- `GS-API-003` when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await touch.pinch(point(0.5, 0.5), 0.1, 0.4, 300);
```

## touch.releaseAll()

Lifts every finger the script has put down.

```ts
touch.releaseAll(): void
```

Returns `void`. stable since API 1.

Errors:

- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
touch.releaseAll();
```
