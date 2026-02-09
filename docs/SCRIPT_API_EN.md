# GameScrcpy Script API Reference

中文 | **English**

> **Version**: 2.2.1
> **Runtime**: QJSEngine (ES6)
> **Built-in Object**: `mapi`

All APIs are called via the global `mapi` object. Scripts run in independent threads, with each key binding having its own sandbox.

---

## Table of Contents

- [Touch Operations](#touch-operations)
  - [click](#click) — Single click
  - [holdpress](#holdpress) — Hold press
  - [release](#release) — Release touch
  - [releaseAll](#releaseall) — Release all touches
  - [slide](#slide) — Swipe
  - [pinch](#pinch) — Pinch zoom
- [Key Operations](#key-operations)
  - [key](#key) — Simulate key press
- [Viewport Control](#viewport-control)
  - [shotmode](#shotmode) — Toggle cursor/game mode
  - [resetview](#resetview) — Reset viewport
  - [resetwheel](#resetwheel) — Reset steer wheel
  - [setRadialParam](#setradialparam) — Set wheel offset coefficients
  - [setKeyUIPos](#setkeyuipos) — Set key UI position
- [State Queries](#state-queries)
  - [isPress](#ispress) — Check press state
  - [getmousepos](#getmousepos) — Get mouse position
  - [getkeypos](#getkeypos) — Get key position
  - [getKeyState](#getkeystate) — Get key state
- [Image Recognition](#image-recognition)
  - [findImage](#findimage) — Find image in region
  - [findImageByRegion](#findimagebyregion) — Find image by selection region
- [Flow Control](#flow-control)
  - [sleep](#sleep) — Delay
  - [isInterrupted](#isinterrupted) — Check interruption
  - [stop](#stop) — Stop script
- [Messages & Logging](#messages--logging)
  - [toast](#toast) — Floating toast
  - [log](#log) — Log output
- [Global State](#global-state)
  - [setGlobal](#setglobal) — Set global variable
  - [getGlobal](#getglobal) — Get global variable
- [Module Loading](#module-loading)
  - [loadModule](#loadmodule) — Import module

---

## Touch Operations

### click

Click at a specified position.

```js
mapi.click(x, y)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `x` | `double` | `-1` | Normalized X coordinate (0.0 ~ 1.0) |
| `y` | `double` | `-1` | Normalized Y coordinate (0.0 ~ 1.0) |

- Omitting parameters uses the key's anchor position
- Only executes when `isPress() == true`
- Internally sends DOWN + UP events

```js
// Click screen center
mapi.click(0.5, 0.5);

// Use key anchor position
mapi.click();
```

---

### holdpress

Hold press (press phase).

```js
mapi.holdpress(x, y)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `x` | `double` | `-1` | Normalized X coordinate |
| `y` | `double` | `-1` | Normalized Y coordinate |

- Sends a DOWN event on press and records the touch sequence
- Automatically releases the corresponding touch point on key release
- Limited by `maxTouchPoints` (default 10)

```js
// Hold press, auto-release on key up
mapi.holdpress(0.3, 0.7);
```

---

### release

Release the current key's touch point.

```js
mapi.release()
```

Sends an UP event. Typically called on key release.

---

### releaseAll

Release all touch points associated with the current key.

```js
mapi.releaseAll()
```

Iterates through all touch sequences bound to the current key and sends UP events one by one. Suitable for batch release during multi-touch.

---

### slide

Simulate a swipe operation.

```js
mapi.slide(sx, sy, ex, ey, delayMs, num)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `sx` | `double` | Start X |
| `sy` | `double` | Start Y |
| `ex` | `double` | End X |
| `ey` | `double` | End Y |
| `delayMs` | `int` | Total swipe duration (milliseconds) |
| `num` | `int` | Number of steps (minimum 10) |

- Uses a smooth curved path with random bending (anti-detection)
- Affected by the `slideCurve` config option

```js
// Swipe left to right, 200ms, 10 steps
mapi.slide(0.2, 0.5, 0.8, 0.5, 200, 10);
```

---

### pinch

Two-finger pinch zoom operation.

```js
mapi.pinch(centerX, centerY, scale, durationMs, steps)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `centerX` | `double` | — | Center point X |
| `centerY` | `double` | — | Center point Y |
| `scale` | `double` | — | Scale ratio (>1 zoom in, <1 zoom out) |
| `durationMs` | `int` | `300` | Duration (milliseconds) |
| `steps` | `int` | `10` | Animation steps |

```js
// Zoom in 2x at screen center
mapi.pinch(0.5, 0.5, 2.0, 300, 10);

// Zoom out to 0.5x
mapi.pinch(0.5, 0.5, 0.5, 300, 10);
```

---

## Key Operations

### key

Simulate a mapped key press.

```js
mapi.key(keyName, durationMs)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `keyName` | `string` | — | Key name |
| `durationMs` | `int` | `50` | Key press duration (milliseconds) |

Triggers the corresponding key's macro script.

**Supported key names**: `A`-`Z`, `0`-`9`, `SPACE`, `ENTER`/`RETURN`, `ESC`/`ESCAPE`, `TAB`, `BACKSPACE`, `SHIFT`, `CTRL`/`CONTROL`, `ALT`, `UP`/`DOWN`/`LEFT`/`RIGHT`, `F1`-`F12`, and symbol keys `` ` ``, `-`, `=`, `[`, `]`, `\`, `;`, `'`, `,`, `.`, `/`

```js
// Press W key for 50ms
mapi.key("W", 50);

// Press Tab key for 100ms
mapi.key("TAB", 100);
```

---

## Viewport Control

### shotmode

Toggle between cursor mode and game mode.

```js
mapi.shotmode(gameMode)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `gameMode` | `bool` | `true` = game mode (hide cursor), `false` = cursor mode |

```js
mapi.shotmode(false);  // Show cursor
mapi.shotmode(true);   // Game mode
```

---

### resetview

Reset mouse viewport control.

```js
mapi.resetview()
```

Used for FPS game viewport reset.

---

### resetwheel

Reset steer wheel state.

```js
mapi.resetwheel()
```

Used for wheel resync after scene transitions (e.g., pressing F to enter a vehicle while running).

---

### setRadialParam

Temporarily set wheel four-direction offset coefficients.

```js
mapi.setRadialParam(up, down, left, right)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `up` | `double` | Up direction coefficient |
| `down` | `double` | Down direction coefficient |
| `left` | `double` | Left direction coefficient |
| `right` | `double` | Right direction coefficient |

- Actual offset = original value × coefficient
- Default `(1, 1, 1, 1)` means no change
- Automatically reverts to default when script ends

```js
// Double the up direction offset
mapi.setRadialParam(2, 1, 1, 1);
```

---

### setKeyUIPos

Dynamically update a key's UI display position.

```js
mapi.setKeyUIPos(keyName, x, y, xoffset, yoffset)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `keyName` | `string` | — | Key name |
| `x` | `double` | — | Base X coordinate |
| `y` | `double` | — | Base Y coordinate |
| `xoffset` | `double` | `0` | X offset |
| `yoffset` | `double` | `0` | Y offset |

Final position = `(x + xoffset, y + yoffset)`. Used for multi-function key position indicators.

```js
// Move J key UI to screen center
mapi.setKeyUIPos("J", 0.5, 0.5);

// With offset
mapi.setKeyUIPos("J", 0.5, 0.5, 0.02, -0.03);
```

---

## State Queries

### isPress

Check current trigger state.

```js
mapi.isPress()
```

**Returns**: `bool` — `true` = pressed, `false` = released

```js
if (mapi.isPress()) {
    mapi.holdpress(0.5, 0.5);
} else {
    mapi.release();
}
```

---

### getmousepos

Get current mouse position.

```js
mapi.getmousepos()
```

**Returns**: `{x: double, y: double}` — Normalized coordinates, 4 decimal places

```js
var pos = mapi.getmousepos();
mapi.toast("x=" + pos.x + ", y=" + pos.y);
```

---

### getkeypos

Get the mapped position of a specified key.

```js
mapi.getkeypos(keyName)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `keyName` | `string` | Key display name (e.g., `LMB`, `Tab`, `=`) |

**Returns**: `{x: double, y: double, valid: bool}`

```js
var pos = mapi.getkeypos("LMB");
if (pos.valid) {
    mapi.click(pos.x, pos.y);
}
```

---

### getKeyState

Get the press state of a specified key.

```js
mapi.getKeyState(keyName)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `keyName` | `string` | Key name |

**Returns**: `int` — `0` = not pressed, `1` = currently pressed

```js
if (mapi.getKeyState("W")) {
    mapi.toast("W key is pressed");
}
```

---

## Image Recognition

> Requires `ENABLE_IMAGE_MATCHING` to be enabled at compile time, depends on OpenCV.

### findImage

Search for a template image within a specified normalized region.

```js
mapi.findImage(imageName, x1, y1, x2, y2, threshold)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `imageName` | `string` | — | Template image name (without extension) |
| `x1` | `double` | `0` | Search region top-left X |
| `y1` | `double` | `0` | Search region top-left Y |
| `x2` | `double` | `1` | Search region bottom-right X |
| `y2` | `double` | `1` | Search region bottom-right Y |
| `threshold` | `double` | `0.8` | Match confidence threshold (0.0 ~ 1.0) |

**Returns**: `{found: bool, x: double, y: double, confidence: double}`

- `found`: Whether a match was found
- `x`, `y`: Normalized center coordinates of the match
- `confidence`: Match confidence value

Template images are stored in the `keymap/images/` directory.

```js
var result = mapi.findImage("confirm_button", 0, 0, 1, 1, 0.8);
if (result.found) {
    mapi.click(result.x, result.y);
    mapi.toast("Confidence: " + result.confidence.toFixed(2));
}
```

---

### findImageByRegion

Search for a template image using a predefined selection region ID.

```js
mapi.findImageByRegion(imageName, regionId, threshold)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `imageName` | `string` | — | Template image name |
| `regionId` | `int` | — | Selection region ID |
| `threshold` | `double` | `0.8` | Match threshold |

**Returns**: Same as `findImage`

Selection regions are created via the Selection Editor tool and stored in `keymap/regions.json`.

```js
// Search in selection region #1
var result = mapi.findImageByRegion("enemy_icon", 1, 0.8);
if (result.found) {
    mapi.click(result.x, result.y);
}
```

---

## Flow Control

### sleep

Pause script execution.

```js
mapi.sleep(ms)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `ms` | `int` | Pause duration in milliseconds |

- Checks interrupt flag every 50ms, can be interrupted by `stop()`
- Also feeds the watchdog to prevent timeout

```js
mapi.sleep(100);  // Pause for 100ms
```

---

### isInterrupted

Check whether the script has been requested to stop.

```js
mapi.isInterrupted()
```

**Returns**: `bool` — `true` = interrupted

Used for early exit in long loops:

```js
while (true) {
    if (mapi.isInterrupted()) return;
    // ... loop logic ...
    mapi.sleep(100);
}
```

---

### stop

Actively stop the current script execution.

```js
mapi.stop()
```

Sets the interrupt flag. Subsequent `isInterrupted()` calls will return `true`, and `sleep()` will return immediately.

---

## Messages & Logging

### toast

Display a floating toast message.

```js
mapi.toast(msg, durationMs)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `msg` | `string` | — | Message content |
| `durationMs` | `int` | `3000` | Display duration (milliseconds, minimum 1ms) |

Messages from the same key update rather than stack; messages from different keys stack vertically.

```js
mapi.toast("Operation complete", 3000);
```

---

### log

Output a log message to the console.

```js
mapi.log(msg)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `string` | Log content |

Output format: `[Sandbox Script] msg`

```js
mapi.log("Debug info");
```

---

## Global State

### setGlobal

Set a cross-script shared global variable (thread-safe).

```js
mapi.setGlobal(key, value)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | Key name |
| `value` | `any` | Value |

- Only takes effect when `isPress() == true`
- Accessible from all sandboxes

```js
mapi.setGlobal("mode", "attack");
```

---

### getGlobal

Get a globally shared variable.

```js
mapi.getGlobal(key)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | Key name |

**Returns**: The stored value, or `undefined` if it doesn't exist

```js
var mode = mapi.getGlobal("mode");
if (mode === "attack") {
    // ...
}
```

---

## Module Loading

### loadModule

Load a JavaScript module from the scripts directory.

```js
mapi.loadModule(modulePath)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `modulePath` | `string` | Module file path (relative to `keymap/scripts/`) |

- Automatically appends `.js` suffix
- Has module caching — each module is loaded only once

**Function-style export**:

```js
// utils.js
function helper() { return 42; }

// Main script
var m = mapi.loadModule("utils.js");
m.helper();  // 42
```

**Object-style export**:

```js
// mymodule.js
function create() {
    return {
        doSomething: function() { /* ... */ }
    };
}

// Main script
var m = mapi.loadModule("mymodule.js");
var obj = new m.create();
obj.doSomething();
```

---

## Coordinate System

GameScrcpy uses a unified **normalized coordinate** system:

- `x`: 0.0 (left edge) → 1.0 (right edge)
- `y`: 0.0 (top edge) → 1.0 (bottom edge)
- Independent of device resolution

You can use the Selection Editor's "Get Position" feature to click on the video frame and obtain coordinates.

## Anti-Detection Features

Script execution includes built-in anti-detection mechanisms:

| Config Option | Range | Description |
|---------------|-------|-------------|
| `randomOffset` | 0-100 | Touch coordinate random offset |
| `slideCurve` | 0-100 | Swipe path curvature |
| `steerWheelSmooth` | 0-100 | Steer wheel smoothness |
| `steerWheelCurve` | 0-100 | Steer wheel path curvature |

These parameters are configured in `config.ini` or the settings UI.

## File Directory Structure

```
keymap/
├── images/         # Template images (.png/.jpg/.bmp)
├── scripts/        # JS module files
├── regions.json    # Custom selection region config
└── *.json          # Key mapping configs
```
