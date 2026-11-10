# Global functions and values

## sleep(ms)

Waits without blocking other tasks. Rejects with an AbortError when the task is cancelled.

```ts
sleep(ms: number): Promise<void>
```

| Parameter | Type | Description |
|---|---|---|
| `ms` | `number` | How long to wait, in milliseconds (0 to 2147483647). |

Returns `Promise<void>`. stable since API 1.

Errors:

- `RangeError` when ms is not a number from 0 to 2147483647. Pass a whole number of milliseconds.

```ts
await sleep(500);
```

## nextFrame()

Waits until the next picture frame is shown.

```ts
nextFrame(): Promise<void>
```

Returns `Promise<void>`. stable since API 1.

```ts
await nextFrame();
```

## every(intervalMs, handler, options?)

Runs a handler again and again, waiting intervalMs before each run.

```ts
every(intervalMs: number, handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `intervalMs` | `number` | Milliseconds between runs, from 1 to 86400000. |
| `handler` | `(event: TaskContext) => void \| Promise<void>` | What to run; each run is its own task. |
| `options` (optional) | `ListenOptions` | What happens when the last run is still going. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-008` when the handler or action is not a function. Pass a function.

```ts
every(500, async () => { if (await vision.find("res/pick.png")) await controls.pick.tap(); });
```

## after(delayMs, handler, options?)

Runs a handler once after a delay, unless it is disposed first.

```ts
after(delayMs: number, handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `delayMs` | `number` | Milliseconds to wait, from 1 to 86400000. |
| `handler` | `(event: TaskContext) => void \| Promise<void>` | What to run, as its own task. |
| `options` (optional) | `ListenOptions` | What happens when it is still running from before. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-008` when the handler or action is not a function. Pass a function.

```ts
after(3000, () => ui.toast("Ready"));
```

## waitUntil(condition, options?)

Waits until a condition gives a truthy value and resolves with it; rejects with a TimeoutError (GS-API-030) when the time is up.

```ts
waitUntil(condition: () => unknown, options?: UntilOptions): Promise<NonNullable<unknown>>
```

| Parameter | Type | Description |
|---|---|---|
| `condition` | `() => unknown` | Checked once per frame until it gives a truthy value; may return a Promise. |
| `options` (optional) | `UntilOptions` | How long to wait, in milliseconds (5000 by default). |

Returns `Promise<NonNullable<unknown>>`. stable since API 1.

Errors:

- `GS-API-008` when the condition is not a function. Pass a function.
- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.

```ts
await waitUntil(() => vars.ready.value, { timeoutMs: 3000 });
```

## retry(times, action)

Runs an action and tries again when it throws, up to times more; a cancelled task is not retried.

```ts
retry(times: number, action: (attempt: number) => unknown): Promise<unknown>
```

| Parameter | Type | Description |
|---|---|---|
| `times` | `number` | How many more tries after the first, from 0 to 10. |
| `action` | `(attempt: number) => unknown` | What to try; it gets the number of the try, from 0. |

Returns `Promise<unknown>`. stable since API 1.

Errors:

- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-008` when the handler or action is not a function. Pass a function.

```ts
await retry(2, () => touch.tap(0.5, 0.9).until(() => vision.find("res/ok.png")));
```

## withTimeout(timeoutMs, action)

Runs an action with a time limit: when the time is up its signal aborts and the Promise rejects with a TimeoutError (GS-API-030).

```ts
withTimeout(timeoutMs: number, action: (context: TaskContext) => unknown): Promise<unknown>
```

| Parameter | Type | Description |
|---|---|---|
| `timeoutMs` | `number` | The time limit in milliseconds, from 1 to 600000. |
| `action` | `(context: TaskContext) => unknown` | What to run; its signal aborts when the time is up. |

Returns `Promise<unknown>`. stable since API 1.

Errors:

- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-008` when the handler or action is not a function. Pass a function.

```ts
await withTimeout(5000, async ({ signal }) => { while (!signal.aborted) await controls.fire.trigger(); });
```

## parallel(work)

Runs several things at the same time and waits for all of them.

```ts
parallel(...work: Work[]): Promise<unknown[]>
```

| Parameter | Type | Description |
|---|---|---|
| `work` | `...Work[]` | Promises, or functions to start at once. |

Returns `Promise<unknown[]>`. stable since API 1.

```ts
await parallel(controls.skill1.trigger(), controls.skill2.trigger());
```

## sequence(work)

Runs several things one after another, in order, waiting for each before it starts the next.

```ts
sequence(...work: Work[]): Promise<unknown[]>
```

| Parameter | Type | Description |
|---|---|---|
| `work` | `...Work[]` | Functions to run one after another, waiting for each before the next. |

Returns `Promise<unknown[]>`. stable since API 1.

```ts
await sequence(async () => { await ui.toast("one"); }, async () => { await ui.toast("two"); });
```

## multiGate(name, times, loop, random, work)

Runs the next of several functions each time it is called, in turn or in random order, and one more function after they have all run.

```ts
multiGate(name: string, times: number, loop: boolean, random: boolean, ...work: Work[]): Promise<unknown>
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The gate's name; the same name keeps the same count. |
| `times` | `number` | How many outputs it has, from 1 to 1000. |
| `loop` | `boolean` | Whether it starts over at the first output after the last one. |
| `random` | `boolean` | Whether the outputs come in a random order. |
| `work` | `...Work[]` | One function per output, then one more for after the last. |

Returns `Promise<unknown>`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.
- `GS-API-017` when times is not a whole number from 1 to 1000. Pass a whole number of outputs.

```ts
await multiGate("door", 2, false, false, async () => {}, async () => {}, async () => {});
```

## race(work)

Runs several things at the same time and waits for the first to finish.

```ts
race(...work: Work[]): Promise<unknown>
```

| Parameter | Type | Description |
|---|---|---|
| `work` | `...Work[]` | Promises, or functions to start at once. |

Returns `Promise<unknown>`. stable since API 1.

```ts
await race(sleep(2000), waitUntil(() => keys.Space.isDown));
```

## doOnce(name, action)

Runs an action only the first time it is called with this name.

```ts
doOnce(name: string, action: () => unknown): unknown
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | Which one; each name runs once until resetOnce. |
| `action` | `() => unknown` | What to run the first time. |

Returns `unknown`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.

```ts
doOnce("greet", () => ui.toast("Hello"));
```

## resetOnce(name)

Lets doOnce run the action with this name again.

```ts
resetOnce(name: string): void
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The name you gave it. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.

```ts
resetOnce("greet");
```

## doN(name, times)

Says yes for the first times calls with this name, and no after that, until resetDoN with the same name.

```ts
doN(name: string, times: number): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | Which count; the same name with the same limit keeps counting. |
| `times` | `number` | How many times it passes before it says no, from 1 to 1000000. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.
- `GS-API-017` when times is not a whole number from 1 to 1000000. Pass a whole number of times.

```ts
if (doN("dash", 3)) await controls.dash.tap();
```

## resetDoN(name)

Lets doN with this name pass that many times again.

```ts
resetDoN(name: string): void
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The name you gave it. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.

```ts
resetDoN("dash");
```

## gate(name, startClosed?)

A named gate that lets an action through only while it is open.

```ts
gate(name: string, startClosed?: boolean): Gate
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The gate's name; the same name gives the same gate. |
| `startClosed` (optional) | `boolean` | Whether a new gate starts closed (open by default). |

Returns `Gate`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.

```ts
gate("loot").close();
```

## flipFlop(name)

Gives "A" and "B" in turn, starting with "A", for each name.

```ts
flipFlop(name: string): "A" | "B"
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The name you gave it. |

Returns `"A" | "B"`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.

```ts
if (flipFlop("light") === "A") controls.light.press(); else controls.light.release();
```

## throttle(intervalMs, action)

Makes a function that runs the action at most once per interval and ignores calls in between.

```ts
throttle(intervalMs: number, action: (...values: unknown[]) => unknown): (...values: unknown[]) => unknown
```

| Parameter | Type | Description |
|---|---|---|
| `intervalMs` | `number` | The shortest time between two runs, from 1 to 86400000 milliseconds. |
| `action` | `(...values: unknown[]) => unknown` | What to run. |

Returns `(...values: unknown[]) => unknown`. stable since API 1.

Errors:

- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-008` when the handler or action is not a function. Pass a function.

```ts
const shout = throttle(1000, () => ui.toast("Fire"));
```

## debounce(waitMs, action)

Makes a function that runs the action only after the calls have stopped for waitMs.

```ts
debounce(waitMs: number, action: (...values: unknown[]) => unknown): (...values: unknown[]) => void
```

| Parameter | Type | Description |
|---|---|---|
| `waitMs` | `number` | How long it must stay quiet before running, from 1 to 86400000 milliseconds. |
| `action` | `(...values: unknown[]) => unknown` | What to run, with the values of the last call. |

Returns `(...values: unknown[]) => void`. stable since API 1.

Errors:

- `GS-API-003` when the time is out of range. Pass milliseconds in the range given.
- `GS-API-008` when the handler or action is not a function. Pass a function.

```ts
const save = debounce(500, () => store.set("x", 1));
```

## stopwatch()

A stopwatch that starts now.

```ts
stopwatch(): Stopwatch
```

Returns `Stopwatch`. stable since API 1.

```ts
const watch = stopwatch();
```

## point(x, y)

A position on the picture, as ratios.

```ts
point(x: number, y: number): PicturePoint
```

| Parameter | Type | Description |
|---|---|---|
| `x` | `number` | Horizontal position as a ratio of the picture width: 0 is the left edge, 1 the right edge. |
| `y` | `number` | Vertical position as a ratio of the picture height: 0 is the top edge, 1 the bottom edge. |

Returns `PicturePoint`. stable since API 1.

Errors:

- `GS-API-002` when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.

```ts
await touch.tap(point(0.5, 0.9));
```

## Component

The base class of components made in logic: extend it, declare static props and set them up in setup(); create them with new at the top level of the logic.

```ts
declare const Component: ComponentClass;
```

```ts
class Toggle extends Component { static props = { key: prop.key("F") }; }
```

## Tap

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const Tap: BuiltInClass<Tap, ComponentOverrides>;
```

```ts
export default class Jump extends Tap { onPress() { log.info("jump"); } }
```

## Hold

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const Hold: BuiltInClass<Hold, HoldOverrides>;
```

```ts
export default class Scope extends Hold { onPressed() { log.info("scoped"); } }
```

## LongPress

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const LongPress: BuiltInClass<LongPress, StartEndOverrides>;
```

```ts
export default class Bag extends LongPress { onEnd() { log.info("bag open"); } }
```

## Turbo

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const Turbo: BuiltInClass<Turbo, ComponentOverrides>;
```

```ts
export default class Shots extends Turbo { onRelease() { log.info("stop"); } }
```

## Swipe

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const Swipe: BuiltInClass<Swipe, StartEndOverrides>;
```

```ts
export default class Slide extends Swipe { onEnd() { log.info("slid"); } }
```

## Pinch

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const Pinch: BuiltInClass<Pinch, ComponentOverrides>;
```

```ts
export default class Zoom extends Pinch { onTouchEnd() { log.info("zoomed"); } }
```

## Wasd

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const Wasd: BuiltInClass<Wasd, StickOverrides>;
```

```ts
export default class Move extends Wasd { onStop() { log.info("stopped"); } }
```

## RightWalk

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const RightWalk: BuiltInClass<RightWalk, RightWalkOverrides>;
```

```ts
export default class Walk extends RightWalk { onStop() { log.info("arrived"); } }
```

## PadStick

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const PadStick: BuiltInClass<PadStick, StickOverrides>;
```

```ts
export default class Stick extends PadStick { onMoveStart() { log.info("go"); } }
```

## Camera

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const Camera: BuiltInClass<Camera, CameraOverrides>;
```

```ts
export default class View extends Camera { onLock() { hints.hide(); } }
```

## PadCamera

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const PadCamera: BuiltInClass<PadCamera, LookOverrides>;
```

```ts
export default class PadView extends PadCamera { onTurnStart() { log.info("turn"); } }
```

## FreeLook

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const FreeLook: BuiltInClass<FreeLook, StartEndOverrides>;
```

```ts
export default class Peek extends FreeLook { onStart() { log.info("peek"); } }
```

## Skill

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const Skill: BuiltInClass<Skill, SkillOverrides>;
```

```ts
export default class Blink extends Skill { onCast() { log.info("cast"); } }
```

## Dial

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const Dial: BuiltInClass<Dial, DialOverrides>;
```

```ts
export default class Weapons extends Dial { onChoose(event?: CellEvent) { log.info(event?.cell); } }
```

## MultiPress

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const MultiPress: BuiltInClass<MultiPress, HoldOverrides>;
```

```ts
export default class PeekFire extends MultiPress { onPressed() { log.info("peek and fire"); } }
```

## TempCursor

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const TempCursor: BuiltInClass<TempCursor, TempCursorOverrides>;
```

```ts
export default class Pointer extends TempCursor { onShow() { hints.hide(); } }
```

## ActionWheel

A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.

```ts
declare const ActionWheel: BuiltInClass<ActionWheel, ActionWheelOverrides>;
```

```ts
export default class Items extends ActionWheel { onChoose(event?: WheelChoice) { log.info(event?.name); } }
```

## defineInterface

Makes an interface, or gives back the one with this name, so components of different kinds can answer the same call: list it in static implements of their classes, then mapping.all(it) or it.all() gives every component that implements it. Throws GS-API-011 for a name that is not a string or is empty.

```ts
declare const defineInterface: <T>(name: string) => Interface<T>;
```

```ts
const Fire = defineInterface<{ fire(): void }>("fire");
```

## Task

The base class of a task. The task.ts of a task exports a class that extends it: static params lists its settings and run() does the work.

```ts
declare const Task: TaskClass;
```

```ts
export default class Daily extends Task { async run() { log.info(this.params.rounds); } }
```

## controls

The components of the profile in use, by id; empty without a phone. In a task they are the components of the profile the phone uses now, and are made again from the new profile when the phone switches.

```ts
declare const controls: Controls;
```

```ts
controls.jump.onPress(() => log.info("jump"));
```

## vars

The variables of the profile in use, by id; empty without a phone. In a task they follow the phone's profile, as controls do.

```ts
declare const vars: Vars;
```

```ts
if (vars.onBoat.value) await controls.drive.hold(500);
```

## keys

Keys, mouse buttons and gamepad buttons by name: keys.F, keys.Space, keys.MouseLeft, keys.GamepadA.

```ts
declare const keys: Keys;
```

```ts
keys.F.onTap(() => controls.pick.tap());
```

## globals

Values shared by every profile and task on this phone.

```ts
declare const globals: Globals;
```

```ts
globals.set("afk", true);
```

## machine

Runs a state machine: each member of states is a state, an async function that receives to and returns to.<next state>(reason) to move on, or nothing to end the machine. The first member is the start unless options.start names another. The editor shows the state it is in and every transition with its reason. Throws GS-API-008 when states is not an object of functions and GS-API-002 for an unknown start.

```ts
declare const machine: <S extends string>(states: { readonly [K in S]: (to: MachineTo<S>) => MachineStep | void | Promise<MachineStep | void> }, options?: MachineOptions<S>) => Promise<void>;
```

```ts
await machine({ async lobby(to) { await vision.findAndTap("start"); return to.queue("tapped start"); }, async queue(to) { return (await vision.waitFor("inGame", { timeoutMs: 120000 })) ? to.game() : to.lobby("timed out"); }, async game() {} }, { name: "farm" });
```

## ext

The system nodes that native extensions add: ext.<extension>.<node>(...) runs a node at once and gives its result. <extension> is the extension id with each part in lowerCamelCase, so the nodes of gamescrcpy.pc-clock are ext.gamescrcpy.pcClock. Throws GS-API-060 when the extension is not on, GS-API-061 for values of the wrong type, GS-API-062 when the node fails, GS-API-063 naming a permission the extension lacks and GS-API-064 when the node takes longer than 100 ms. A job the extension offers, ext.<extension>.<job>({ ... }), starts at once with its arguments by name and gives an ExtensionJob; starting throws GS-API-060, GS-API-061 and GS-API-063 as a node does, and GS-API-067 when the extension runs as many of its jobs as it may.

```ts
declare const ext: Extensions;
```

```ts
log.info(ext.gamescrcpy.pcClock.now());
```
