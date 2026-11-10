A profile can carry logic: a small program in TypeScript or JavaScript that runs while the profile is in use. Logic can use the profile's keys, tap and swipe on the phone, wait, ask questions in the mirror window and find pictures and colors in the phone's picture. Everything else in GameScrcpy works without it.

## Open the logic

1. **Open the logic view**: while editing a profile (**Edit key mapping** on the mirror window's toolbar), switch the middle of the top bar from **Picture** to **Logic**. You can also choose **Edit logic** under **More** on the mirror window's toolbar, or **Edit logic…** under **More** (…) on the profile's card on the **Profiles** page: when a phone uses the profile, its editor opens; when none does, a window with only the logic opens.
2. **Add logic**: when the profile has no logic yet, select **Add logic**. GameScrcpy writes `logic.ts` and `tsconfig.json`, using the first key of your profile.

When the profile is a single `.json` file, GameScrcpy first turns it into a profile folder of the same name, with `profile.json` (the keys) and `logic.ts` (the logic). Profiles that come with an extension cannot have logic of their own; copy one to your profiles first.

![The logic view](figure:script-workbench)

| Part | What it is |
|---|---|
| :folder-open: Files | On the left: the files of the profile folder; you can add files. |
| :code-xml: Editor | In the middle: type `touch.` to see its functions, or `controls.` to see the components of the profile; saving checks the logic. **Blueprint / Code** at the top right switches how it is shown (each component has a file of its own, see **The blueprint** below). |
| :play: Run and API | On the right: see whether the logic runs on this phone; **Run** saves the changed files and restarts the logic. **API** searches the whole API with examples. |
| :terminal: Console and problems | At the bottom: what the logic writes with `log.info` and the like, and its errors; select a problem to go to it. |

**Save** on the top bar saves the profile and the changed code files together; **Done** saves and stops editing.

> [!TIP]
> **Prefer another editor?** Choose **Open in external editor** under **More** on the top bar; when Visual Studio Code is installed, it opens the whole folder. The logic view reads files changed outside by itself; when you changed the same file in the logic view too, it lets you choose **Reload** or **Keep mine**.

## Write the logic

Each key of the profile has an id in `profile.json`, such as `fire` or `jump`. In the logic, `controls.fire` is that key:

```ts title="logic.ts"
controls.fire.onPress(async () => {
    log.info("fire pressed");
});

controls.jump.whileHeld(async ({ signal }) => {
    while (!signal.aborted) {
        await touch.tap(0.5, 0.8);
        await sleep(100);
    }
});
```

- `onPress` runs when the key goes down, `onRelease` when it comes up, and `whileHeld` until it comes up.
- Every component's page lists only the blocks that do something for that component: a stick has no **Press**, **Hold down for** or **Press and let go**, a camera has no key events, and the **On** switch is a single block instead of separate turn-on and turn-off ones. The calls that are not listed still work when you write them yourself: `trigger()`, `press()`, `hold()`, `enable()`, `disable()` and `toggle()` mean what they always did.
- **When pressed** on a component is its key; **When touching starts** and **When touching ends** are the finger it puts on the screen. A tap's finger is down for a moment, so listen to the touch events rather than reading `touching`.
- When the logic listens to one of these three key events of a tap, hold, long press, turbo, swipe or multi-press component, the key belongs to the logic: pressing it no longer makes the component press the screen, and only the logic runs. To press it, call its functions in the logic (such as `controls.fire.press()` or `controls.jump.tap()`) at the step where it should press; it comes a few milliseconds later than the component's own press would. To let the component press first as usual and then run the logic, write `controls.fire.onPress(handler, { consume: false })`; in the blueprint, choose **Press it as usual, then run the logic** under **When the key is pressed** on the event block. Sticks, cameras, skills and the other components keep working, and the logic only listens. Only the profile's own logic takes keys: tasks, extension scripts and scripts an AI client runs only listen, and the event methods a component class overrides still let the built-in component press first.
- An id with a dash is written in brackets: `controls["dash-move"]`.
- Positions are fractions of the picture: (0, 0) is the top left corner and (1, 1) the bottom right one, so logic works at any resolution.
- `ui.toast("Round 2")` shows a short message in the mirror window.
- [`clipboard.read()`](help:api/clipboard) and `clipboard.write(text)` read and write the phone's clipboard, not the computer's. When the phone cannot share its clipboard now, for example in the window of an app on an independent display, they give `null` and `false`.
- `ui.mark(0.5, 0.8, { label: "here" })` marks a point on the picture with a small ring, until you call `dispose()` on it or the task that drew it ends.
- `window.focused()` says whether the mirror window is the active window; `window.onFocus` and `window.onBlur` run each time it becomes or stops being the active window.

In logic, task and component files you can also import these functions and objects from the built-in module `gamescrcpy`; they are the same as the globals:

```ts title="logic.ts"
import { controls, touch, log } from "gamescrcpy";

controls.fire.onPress(async () => {
    await touch.tap(0.5, 0.8);
    log.info("tapped");
});
```

> [!WARNING]
> **Always wait inside a loop**: a loop that never uses `await` holds up the logic, and is stopped.

## Flow and operations

Beside `if`, `for` and `while`, the block list has these, each written as plain TypeScript:

- **Switch on Value** – `switch (value) { case 1: … default: … }`, one output per case and one for the default; the case values are changed in the block's details, and the number of cases stays.
- **Sequence** – `await sequence(async () => { … }, async () => { … })`: the outputs run one after another, in order.
- **MultiGate** – `await multiGate("gate", 3, false, false, …)`: each run goes on to the next output, and *Done* runs once the others have all run; *Start over* makes it begin again at the first, *Random order* mixes them.
- **Do N** – `if (doN("name", 3)) { … }`: yes the first three times with that name, no after that, and **Reset Do N** (`resetDoN("name")`) starts over.
- **For Loop** – `for (let i = 0; i <= 2; i++)` with the index as an output; **For Each Loop with Index** – `for (const [index, item] of list.entries())`.
- **Lists** – `list.add`, `list.insert`, `list.removeAt`, `list.remove`, `list.indexOf`, `list.at`, `list.setAt`, `list.size`, `list.has`, `list.sort`, `list.shuffle` and `list.clear`, which change the list itself.
- **Dictionaries** – `dict.make`, `dict.set`, `dict.get`, `dict.has`, `dict.remove`, `dict.keys`, `dict.values`, `dict.size`, `dict.clear`; **sets** – `set.make`, `set.add`, `set.has`, `set.remove`, `set.values`, `set.size`, `set.clear`.
- **Text** – `text.join`, `text.format` (fills `{0}`, `{1}` and so on), `text.find`, `text.has`, `text.split`, `text.replace`, `text.upper`, `text.lower`, `text.trim`, `text.length`.
- **Math** – `math.round`, `math.floor`, `math.ceil`, `math.abs`, `math.min` and `math.max`, next to `math.clamp`, `math.lerp`, `math.random` and `math.randomInt`.

## The blueprint

**Blueprint** at the top right draws the logic as blocks. The logic is split into files the way Unreal does it: the main page `logic.ts` is like the level blueprint, with the load event, the timers, the variable events and the logic for the whole profile; each component's own logic lives in `controls/<code name>.ts`, like an actor blueprint placed in the level. The files do not see each other's variables: share data through the profile variables (`vars`) or the globals (`globals`), and reach other components through `controls.<code name>` as before. Every function has a page of its own.

In **My blueprint** on the left, select a component to open its own file; its tab is titled with the component's name and key (such as `Fire · F`), and **Main page** at the front of the tabs goes back to `logic.ts`. **Use it in logic** in a key's properties opens that key's file too. The file of a tap, hold, long press, turbo, swipe or multi-press component opens with its key events already in place, each starting with a **Default Action** block: the key presses as usual and the blocks after it run next. Delete the default action and the key only runs the logic; put a press block where it should press. Looking does not create a file; the first block you place does. In a component's file, **Tap Screen**, **Long Press Screen**, **Touch Down** and **Swipe Screen** press where the component sits unless you give a place, and the pin says **This Component's Position**. Renaming the code name renames its file; deleting the component with **Delete those blocks too** deletes its file as well. Component events written in `logic.ts` keep working and are drawn on the main page. The blueprint opens where you left it, at the same place and zoom.

Functions that several files need go in the function library `library.ts`, like a blueprint function library in Unreal. **Function Library** in **My blueprint** lists its functions; its **+** opens the library (a new one if there is none yet; the file is created when you save it with a function in it), and **+** on **Functions** there adds a function, written as `export function`. In the other files these functions are in the block list, marked **Library**, and placing one writes `import { name } from "./library.ts"` at the top of the file for you (`../library.ts` in a component's file); double-click such a block to open that function in the library.

To put a block between two connected blocks, use any of these:

- **Drag a loose block onto a wire**: while you drag a block that is not connected to anything, the wire under the pointer lights up; let go and the block goes between the two.
- **Right-click a wire** and choose **Insert a block here…**, then pick a block from the list.
- **Drag from an exec output that already has a wire** and let go on empty canvas, then pick a block; it goes in between and the wire that was there stays.

The blocks after the insertion point move right to make room, and the whole insertion is one undo step.

Dragging a value from a block that owns branches (such as **Find Image**, an event, **For Each Loop** or **Repeat N Times**) onto a loose block first moves that block into the branch where the value lives, so a **Tap Screen** can use the position a find gave back. Loose code that no longer parses is shown as a code block with the error on it: fix the code in the block.

## Wait until the picture changes

After a tap you often need to wait for the picture to change before the next step. Add `.until` to the action, with the condition that tells it worked:

![Wait until the picture changes](figure:until-flow)

```ts title="logic.ts"
const button = await touch.tap(0.9, 0.9)
    .until(() => vision.find("Start button"), { timeoutMs: 3000, retry: 2 });
```

- After the action, the condition is checked once a frame. When it comes true, `until` gives its value; here, the place that was found.
- When it is still not true after `timeoutMs` (5000 milliseconds by default), the action is done again, at most `retry` times. When it never comes true, a `TimeoutError` is thrown that names the line.
- Every action has `.until`: taps, swipes, key presses, `controls.fire.trigger()` and the others.

## Turn a key on and off with a state

`bind` makes a key follow states:

```ts title="logic.ts"
controls.fire.bind("enabled", () => !vars.boat.value);
```

The fire key stops working on a boat and comes back after. Whenever a state the condition read changes, it is worked out again. A binding made in a task ends with the task, and the key goes back to how it was; setting `controls.fire.enabled` directly or calling `mapping.setEnabled` ends the binding.

## Variables

A variable is a `let` at the top of the file. Write the type, the starting value and, above it, what it is for and which group it belongs to in the list:

```ts title="logic.ts"
/** Health of the hero. @category Battle */
let health: number = 100;

/** @category Battle */
let targets: PicturePoint[] = [];
```

- The type is `boolean`, `number`, `string`, `PicturePoint`, `Region`, `HexColor`, `ImagePath`, `InputName`, a component such as `Component`, or a class of your own; a list adds `[]`, a map is `Map<string, number>` and a set `Set<number>`.
- A whole number and a decimal number are both `number`; write the decimal one with a point (`1.5`), and the editor remembers which it is. A number of milliseconds is a duration when it says `@duration`.
- `@category` groups the variable in **My blueprint** under that name; the rest of the comment is shown when the pointer rests on it.
- With `@keep` the starting value is what the last run saved, so the variable carries over to the next run: `let best: number = (store.get("best") as number) ?? 0;`. Setting it from the blueprint writes it back with `store.set`.
- In a component class a variable can also be a public property: write it in `static props` (`best: prop.number(0, { label: "Best" })`) and it can be changed in the property panel of every placed component; the field then starts from `this.props.best`.

In the blueprint, **Variables** in **My blueprint** lists them with their type and group. Press **+** there to make one: name, type, one or many, starting value, category and description, and the two switches **Keep it for next time** and **Change it in the property panel**; the code it writes is shown when you save. In a function page the same list has the variables of that function, and **+** declares one at the top of its body.

- Drag a variable onto the canvas and pick **Get** or **Set**; hold Ctrl while dragging for **Get**, Alt for **Set**. Drop it on a pin to fill that pin.
- Right-click an input pin and choose **Promote to variable** to make a variable from the value in it and connect it.
- Double-click a variable in the list to change it; giving it another name changes every place that uses it, and only those.
- A variable that is still used cannot be deleted; change those places first.

## Your own components

A class in the logic that extends `Component` is a component of your own: `static props` declares its properties (`prop.key`, `prop.position`, `prop.number`, `prop.text`, `prop.toggle`, `prop.component`), `this.event()` makes events others can listen to, `setup()` says how it works, and `this.finger` is its own finger, which stays down until `up()` or until the logic stops. Create it with `new` at the top level of the logic:

```ts
class HoldToggle extends Component {
    static info = { name: "Hold toggle" };
    static props = { key: prop.key("F"), at: prop.position(point(0.5, 0.8)) };
    pressed = this.event();
    released = this.event();
    holding = false;

    setup() {
        this.props.key.onPress(() => {
            this.holding = !this.holding;
            if (this.holding) this.finger.down(this.props.at);
            else this.finger.up();
            (this.holding ? this.pressed : this.released).emit();
        });
    }
}

const crouch = new HoldToggle({ key: "C", at: point(0.85, 0.75) });
crouch.pressed.on(() => log.info("crouching"));
```

Or a double tap:

```ts
class DoubleTap extends Component {
    static props = { key: prop.key("G"), at: prop.position(point(0.5, 0.5)) };

    setup() {
        this.props.key.onPress(async () => {
            await touch.tap(this.props.at);
            await sleep(80);
            await touch.tap(this.props.at);
        });
    }
}

new DoubleTap();
```

`this.props` comes ready to use: keys are Key objects (with `onPress`, `onLongPress` and the rest), positions are points, and a component id becomes that component.

The components above are written in a profile's own logic. To keep one in the component library and place it on the picture, select **New component…** in the keymap editor's library (see [Keymap editor](help:keymap-editor)): it is kept in the `components/<folder>/` folder of the data directory, where the default export of `component.ts` is the class and `component.json` holds its name, one-line description, icon and category. For a component placed on the picture, the profile gives its keys, property values and places: `this.position` is where it sits on the picture, as ratios, and its position properties are the other places. Good to know:

- `this.position` and the position properties are worked out for the phone's picture when the logic starts; they stay the same after the phone turns, until the logic loads again.
- Key properties of a class do not take `claim`; a component file cannot use top-level `await` and imports only files in its own folder by relative paths, another of your components with `../<folder>/component.ts`, and the built-in module `gamescrcpy`.

### Add to a built-in component

A component can also extend one of the 13 built-in components (`Tap`, `Hold`, `LongPress`, `Turbo`, `Swipe`, `Pinch`, `Wasd`, `RightWalk`, `PadStick`, `Camera`, `PadCamera`, `FreeLook`, `Skill`). On the picture it is that built-in component: it presses the screen the same way, just as fast; the event methods the class writes run as well each time the event comes, whether or not they call `super.onPressed()`. How the built-in one presses cannot change; to press another way, start from `Component`. Its `static props` show in the property panel as usual; a key property with the name of one of the built-in component's keys is left out.

```ts
export default class ScopeNote extends Hold {
    static props = { note: prop.text("Scoped") };

    onPressed() {
        ui.toast(this.props.note);
    }
}
```

To extend another of your components, import it from its folder, such as `import Combo from "../combo/component.ts";`; its properties and children come along.

### Children

A field with `this.child(BuiltInClass)` gives a component children. It goes on the picture as a whole group, and each child is a built-in component with keys of its own:

```ts
export default class Combo extends Component {
    static props = { key: prop.key("R") };
    cancel = this.child(Tap);
    skills = [this.child(Skill), this.child(Skill)];

    setup() {
        this.props.key.onPress(async () => {
            for (const skill of this.skills) {
                await skill.trigger();
            }
        });
        this.cancel.onPress(() => this.skills[0].cancel());
    }
}
```

`this.child()` takes the children of the profile in the order the class declares them, and gives `null` when the profile has none. When a class gains a child, the keymap editor places it beside the group the next time it opens.

### Interfaces

Components of different kinds can implement the same interface, and logic can then reach them all at once:

```ts
const Fire = defineInterface<{ fire(): void }>("fire");

export default class Triple extends Turbo {
    static implements = [Fire];

    fire() {
        this.burst(3);
    }
}
```

In the logic, `for (const shooter of Fire.all()) shooter.fire();` (or `mapping.all(Fire)`) fires every component that implements it; `defineInterface` with the same name gives the same interface. A property such as `prop.component(Skill)` lists only skill components in the property panel.

## Structures, enums and classes

Besides components, the logic can declare the three things Unreal Blueprints have and the blueprints draw as blocks. All of them are plain TypeScript in the same file, so typing them by hand and making them in the blueprint give the same blocks.

**Structures** group a few typed fields. Click **+** on **Structures** in **My blueprint**; the editor lists the fields, their types and whether a field may be left out (`名字?: string`). The blueprints have **Make** (one input per field, gives the structure) and **Break** (the structure in, one output per field); a field read is written `起点.x`, and making one is `{ x: 0.5, y: 0.5 }`.

```ts
interface 位置 {
  x: number
  y: number
}
```

**Enums** are a fixed set of options. TypeScript's `enum` cannot be stripped, so an enum is a `const` object with a union type next to it; the editor writes both and keeps them the same when you rename it:

```ts
const 阵营 = { 红: "红", 蓝: "蓝" } as const
type 阵营 = typeof 阵营[keyof typeof 阵营]
```

The blueprints have one block per option (`阵营.红`) and **Switch on** the enum, which has one output per option; compare two values with the usual equality block. A variable or a field whose type is the enum can only be given its options.

**Classes** hold data and functions and are not on the picture. Click **+** on **Classes**; the editor adds variables, functions and dispatchers, marks any of them private, gives the class a constructor or a parent class, and each function gets a page in the blueprint like a component's functions do.

```ts
class 敌人 {
  名字 = ""
  private 生命 = 100
  倒下 = this.event()
  constructor() {
    this.名字 = "小兵"
  }
  受伤(数值: number) {
    this.生命 -= 数值
  }
}
class 精英 extends 敌人 {
  受伤(数值: number) {
    super.受伤(数值 * 2)
  }
}
```

`private` is TypeScript's own modifier: it is stripped when the logic runs, so it keeps the code readable and the editor tidy rather than locking the field at run time. A subclass function that overrides its parent's shows **Call parent** in the blueprint (`super.受伤(…)`).

The blueprints have **New** (the constructor's arguments, gives the object), **Cast to** (`对象 instanceof 敌人` with "is" and "is not" outputs), reading and setting a member, calling a method, and **Fire** / **Bind** / **Unbind** for the event dispatchers a class declares (`this.event()`; binding gives a subscription object, unbinding calls its `dispose()`).

## Tasks

A task is logic that runs on its own, such as daily check-ins or farming: it does not change with the profile and keeps going when you switch games; your keys keep working, so you can play by hand while it runs. Select **New task** on the hub's **Tasks** page, and GameScrcpy writes a `task.ts` that runs as it is into the task's folder:

```ts
export default class Farm extends Task {
    static params = { rounds: param.number(10, { label: "Rounds", min: 1 }) };

    async run({ signal }: TaskContext) {
        for (let round = 1; round <= this.params.rounds && !signal.aborted; round++) {
            this.progress("Round " + round);
            await touch.tap(0.5, 0.8);
            await sleep(5000);
        }
    }
}
```

- The settings declared in `static params` (`param.number`, `param.text`, `param.toggle`) show on the right of the Tasks page; the values you choose are in `this.params`.
- What `this.progress(…)` writes shows on the task's card and in the **Tasks** menu on the mirror window's toolbar.
- On the Tasks page, tick the phones to run it on; each runs its own copy. **How it starts** can be a time every day or once; at that time it starts on the ticked phones that are mirroring. When it fails, it can run once more.
- A task can use the components of the profile in use on that phone, such as `controls.pick.tap()`; their live state and key events belong to the profile's logic.
- **Edit logic** on the Tasks page opens its code; **Run** starts the task again on the chosen phone.

## The vision workbench

Choose **Vision resources…** under **More** on a profile's card, or **Vision workbench** under **More** on the mirror window's toolbar. The window shows the live picture of the chosen phone:

1. **Freeze the picture**: select **Frozen** at the top to hold the current picture.
2. **Mark what to find**: with the tools below the picture, draw a template (a picture to find), a region, a point or a path, or click the picture with the color tool to pick a color.
3. **Name it and tune it**: on the right, name it, and set the similarity threshold, how to compare (color or grey) and the region to look in.
4. **Test it**: select **Test find** at the top to see whether it is found in the picture, how many times and how fast.

![The vision workbench](figure:vision-freeze,vision-draw,vision-name,vision-test!)

When an AI client works on a profile and needs a picture, a color, a region or a point that is not there yet, it can ask you for them instead of guessing: GameScrcpy brings its window forward and opens this workbench on the AI's draft of the profile, with the AI's list on the right. Frame, pick or tap each item, select **Skip** for the ones you do not want, or answer one with a resource the profile already has; then select **Give to AI**. They are saved in the AI's draft, not in the profile you use, and the AI gets their names and values.

They are kept in the `res` folder of the profile folder. Logic uses them by name; the editor suggests the names and the check reports a misspelt one:

```ts title="logic.ts"
const start = await vision.find("Start button");
const banner = await vision.findColor("Event banner");
const entry = res.point("Event entry");
await touch.tap(entry.x, entry.y);
```

To look at a single pixel, `vision.pickColor(point(0.5, 0.1))` gives its color, written `"#RRGGBB"`, and `vision.matchColor(point(0.5, 0.1), "Event banner")` says whether it is that color.

To do something else once a template or a color shows up, write it with these functions.

## Do several things at the same time (blueprint)

In the blueprint, drag a second wire from an exec output that already has one onto another block's exec input: from then on both runs start together and neither waits for the other. There is no block for this; the code writes it as `await parallel(...)`, one arrow function per wire:

```ts
await parallel(async () => {
    await controls.move.reset();
}, async () => {
    await controls.view.reset();
});
```

Dragging a third wire from the same output adds another arrow function. Removing one of the wires (select it and delete it, or Alt-click the output to remove them all) removes that arrow function too, and when only one is left the code goes back to plain statements. Dragging from an exec output that has no wire yet still puts the new block right after it.

## Check and run

- Use the profile on a mirrored phone. The logic starts by itself. Saving `logic.ts` reloads it; you do not need to reconnect.
- If the logic fails, the mirror window shows the error, with the file and the line. The logic view shows it in **Console** too.
- Lines written with `log.info`, `log.warn` and `log.error` also go to GameScrcpy's log file, in the `logs` folder next to the `profiles` folder.

## The command-line tool

`gs.exe` is the GameScrcpy command-line tool, in the same folder as `GameScrcpy.exe`. The logic view checks the logic with it, and you can run it yourself:

| Command | What it does |
|---|---|
| `.\gs.exe check <folder>` | Checks the logic, until it says "no problems". |
| `.\gs.exe types <folder>` | Updates the type files after you change `profile.json` or the resources, so an outside editor knows the new keys and names. |
| `.\gs.exe run <folder> --replay <session.json>` | Plays a session file (key presses with their times) and prints what the logic would do. |
| `.\gs.exe test <folder>` | Runs the tests in the folder's `tests` folder. |
| `.\gs.exe new <new folder> --profile <profile.json>` | Makes a profile folder with logic from an exported profile. |

A profile folder made with `gs new` holds an `AGENTS.md` file that explains these commands.

## Learn the script API

The reference is made from the program itself, so it always matches your version of GameScrcpy.

- [Script API overview](help:api)
- [touch](help:api/touch): taps and swipes
- [mapping](help:api/mapping): the keys of the profile in use
- [ui](help:api/ui): messages, questions and drawings in the mirror window
- [window](help:api/window): whether the mirror window is the active window
- [vision](help:api/vision): finding pictures and colors in the phone's picture
- [res](help:api/res): the points, regions and paths named in the vision workbench
- [store](help:api/store): values kept between runs
