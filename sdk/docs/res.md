# res

The named points, regions and paths of the profile, kept in res/resources.json by the vision workbench.

## res.point(name)

A named point of the profile, as picture ratios.

```ts
res.point(name: PointName): PicturePoint
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `PointName` | The name of a point in res/resources.json. |

Returns `PicturePoint`. stable since API 1.

Errors:

- `GS-API-018` when the name is not a string or there is no such resource. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.

```ts
const entry = res.point("Event entry"); await touch.tap(entry.x, entry.y);
```

## res.region(name)

A named region of the profile, as picture ratios.

```ts
res.region(name: RegionName): Region
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `RegionName` | The name of a region in res/resources.json. |

Returns `Region`. stable since API 1.

Errors:

- `GS-API-018` when the name is not a string or there is no such resource. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.

```ts
const corner = res.region("Bottom right"); const found = await vision.find("res/coin.png", { region: corner });
```

## res.path(name)

A named path of the profile: its points as picture ratios and how long a swipe along it takes.

```ts
res.path(name: PathName): PicturePath
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `PathName` | The name of a path in res/resources.json. |

Returns `PicturePath`. stable since API 1.

Errors:

- `GS-API-018` when the name is not a string or there is no such resource. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.

```ts
const drag = res.path("Drag map"); const end = drag.points[drag.points.length - 1]; await touch.swipe(drag.points[0].x, drag.points[0].y, end.x, end.y, drag.durationMs);
```
