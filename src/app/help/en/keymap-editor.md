The key mapping editor puts keys on the game's buttons. It opens inside the mirror window, over the live picture of the game.

![The key mapping editor](figure:editor-overview)

| Part | What it is |
|---|---|
| Top bar | The profile's name, whether it is saved, and the buttons **Undo**, **Redo**, **More**, **Save** and **Done**. |
| Picture | The live game; the keys sit on it. |
| Side panel | The **Library** and **Placed** tabs; with one component selected, its properties. |

## Open the editor

- In the mirror window, open **Key mapping options** on the toolbar and choose **Edit key mapping**. A profile must be in use; pick one in the same menu first.
- Or make a new profile with **Create and edit**. See [Profiles and sharing](help:profiles).

## Add a key

1. **Open the Library tab**: on the side panel; each component has a line under its name saying what it does, and resting the pointer on it says where to use it; **Search components** finds one by name or description.
2. **Drag a component onto the game's button**: or select a component and press <kbd>Enter</kbd> to place it in the middle of the picture.
3. **Press a key for it**: a key, a mouse button or the wheel. Select **Skip** to choose the key later.

![Add a key](figure:editor-components,editor-drag!,editor-capture)

The components; each does one thing:

| Group | Component | What it does |
|---|---|---|
| Basics | Tap | Each key press taps the screen once, however long the key is held. |
| | Hold | The screen stays pressed as long as the key is held. |
| | Long press | Each key press holds the screen for the time you set. |
| | Turbo | While the key is held, taps again and again at the rate you set. |
| | Swipe | Each key press swipes along the path you draw. |
| | Pinch | Two keys spread or pinch two fingers. |
| Movement | WASD stick | W, A, S and D push the stick on the screen, with walk and sprint keys. |
| | Right-click walk | A click walks the hero toward that spot. |
| | Gamepad stick | A gamepad's stick pushes the stick on the screen. |
| View | View | After the lock key, moving the mouse turns the view. |
| | Gamepad view | A gamepad's right stick turns the view. |
| | Free look | While the key is held, the mouse looks around without turning. |
| MOBA | Skill cast | Press to aim toward the pointer, release or click to cast. |

The phone's Back key, typing, screenshots and switching profiles are not in the library; logic does them with system nodes. See [Scripts](help:scripts).

### Example components and my components

The last two groups of the library are components made with logic; they go on the picture like the ones above:

| Group | Component | What it does |
|---|---|---|
| Example components | Toggle hold | Press to hold, press again to let go. |
| | Double tap | Taps twice for each press, with a gap you can set. |
| | Burst | Taps several times for each press; the count and the gap can be set. |
| | Tap in turn | Each press taps the next of a few places, in turn. |
| | Show all key hints while held | While its key is held, every key hint shows at full opacity, even when the hints are turned off; letting go brings back their usual look. It has no key at first: press one when you place it. |
| My components | The ones you make | The last tile, **New component…**: give it a name and you get a working component (one key that taps where you place it), with its logic open for you to change. |

- **Examples are for looking**: double-click one to open the **Edit component** window and see how it is made; to change it, select **Copy to my components** on the right and change your own copy.
- **One copy of each of my components**: it is kept in the library and every profile that uses it points at the same one, so changing it changes them all; when you save its code, the profiles using it on phones that are mirroring reload their logic at once. On the right of the **Edit component** window you can change its name, one-line description (shown in the library), icon and category, and see the profiles that use it.
- **On the picture**: a round badge like the other components, with its icon and first key; when it has more places (like the second and third place of **Tap in turn**), each has a small dot joined by a dashed line, and you can drag the dots while it is selected. Double-clicking the key on the badge presses a new key for it, as for other components.
- **Properties**: made from the properties the component declares: keys, numbers, texts, switches and another component; **Open component** at the top opens its logic.
- **Based on**: in the **New component** dialog, **Based on** starts it from scratch, from one of the 13 built-in components or from another of your components; based on a built-in component it presses the same way, and its code has a place ready for what else to do when it is pressed.
- **Groups**: a component that declares children goes on the picture as a whole group in one drag: the group has a badge of its own and each child is a built-in component with its own keys; dragging the group's badge moves them all, dragging a child moves only it, and **Placed** lists the children indented under their group.
- **The profile needs logic**: placing one of these turns the profile into a folder profile with logic; profiles that come with an extension cannot have logic, so they cannot use them.
- **Export and import**: exporting a profile packs the components of yours it uses (examples need not travel); importing a component with the same name as one of yours but different contents keeps both, names the imported one "Name 2" and points the imported profile at it, while your profiles keep yours.

## Move and arrange keys

- Click a key to select it. <kbd>Ctrl</kbd>-click or <kbd>Shift</kbd>-click adds more keys; drag over an empty place to select an area. <kbd>Esc</kbd> clears the selection.
- Drag keys to move them. They snap to other keys and to the center; hold <kbd>Alt</kbd> while dragging to place them freely, or turn off **Snap to other keys and the center**.
- The arrow keys move the selected keys a little; with <kbd>Shift</kbd> they move further.
- Double-click a key's label on the picture to press a new key for it.
- With two or more keys selected, the side shows **Align** and **Distribute**.
- **Freeze the picture** stops the picture, so you can place keys on a button that shows only for a moment.
- Zoom with the zoom buttons or <kbd>Ctrl</kbd> and the mouse wheel. Click the percentage to fit the picture again.

## Change a key

Select one component, and its properties open on the side, in one column:

- **Name**: at the top, with the kind of component and what it does under it. **More** (…) has **Rename**, **Duplicate** (puts a copy next to it), **Copy** and **Paste in the middle**, **Set as default for new keys**, **Restore factory default** and **Delete**. Select components on the picture and press Ctrl+C to copy them, then Ctrl+V to paste them where the pointer is (or in the middle when the pointer is not over the picture); copied components stay until the program closes, can be pasted in another phone's editor too, and keep their keys and settings.
- **Key**: the keys the component uses; click a key box and press a key to change it. For a mouse button you can also choose **When it applies**.
- **Position** and the settings of its kind, such as how long a long press lasts, the turbo rate or the view's sensitivity. The **Anchor** is the corner or edge of the picture the component keeps its distance to: when the screen has another size or shape (another phone, a tablet), the component stays as far from that corner or edge as it is now, like the game's own buttons. Pick the side the game's button sits on, such as **Bottom right** for a skill button in the bottom right corner; dragging the component on the picture picks the nearest one by itself. When a WASD stick has a sprint key, **How far sprint pushes** says how many times the radius the finger goes while the sprint key is held; it is the dashed ring on the picture, and you can drag the dot on the ring too.
- **On at start**: when this is off, the component does nothing after the profile loads until logic turns it on.
- **Code name**: under **Name**, the name the logic uses for the component, written as `controls.fire`. A new component takes it from its key: a hold on F is `holdF`, one on the left mouse button is `holdMouseLeft`, and `2` is added when the name is taken. Changing the key changes the code name and every place in the logic that uses it. Type in the box to choose another name; from then on the code name stays as you set it. It uses English letters, digits, `_` and `-`, starts with a letter, and must be free; the logic and the blueprint change with it, in one undo step.
- **Applies when**: always, or only while a variable is on or off.
- **Add-ons**: add-ons from extensions show here. To use one on every key (random landing, for example), turn it on in that extension's settings on the Extensions page, once for every profile; it then shows here as global, and you can turn it off for a single key.
- **Usable in logic**: the events (red) and functions (blue) the component has in logic. **Use it in logic** opens this component's own blueprint page, at once, adding nothing to the code.
- **Keys the logic takes**: when the logic listens to the component's press, release or held event, a small logic badge sits at the top right of its keycap and the property panel says that pressing the key only runs the logic: the component no longer presses the screen by itself, and the logic decides when it presses. To let it press first as usual, set **When the key is pressed** on the blueprint's event block to **Press it as usual, then run the logic**.

A component without keys is drawn as a dashed circle with its name on the picture; it is there for logic only. The **Placed** tab lists every component with what it is bound to.

## Conflicts

When two keys use the same key, the top bar shows how many conflicts there are. Click it to select the keys in conflict, then give one of them another key.

![Two keys both use Q: the top bar shows the conflict and both keys turn red.](figure:editor-conflict)

## Delete a key

**Delete** in **More** (or <kbd>Delete</kbd>) removes the selected components. When the logic uses one of them, a dialog offers three choices:

| Choice | What happens |
|---|---|
| Delete those blocks too | The component goes, and the statements in the logic that used it go with it, in the same undo step. |
| Delete only the component | Only the component goes; its blocks stay in the logic. |
| Cancel | Nothing changes. |

A block whose component is gone is drawn with a red outline and a warning mark and says **Cannot find the component “…” : it was deleted**; a warning strip over the blueprint says how many there are. Each such block has two buttons: **Replace with another component…** points it at the component you choose, and **Delete block** takes it out of the logic. <kbd>Ctrl</kbd>+<kbd>Z</kbd> takes back the deletion or a fix.

## Picture and logic

**Picture / Logic** in the middle of the top bar switches between the editor's two views: **Picture** places components and changes their properties; **Logic** is the profile's code (see [Getting started with scripts](help:scripts)). When you switch to the logic and the window is small, it grows, and it goes back when you return to the picture. **Save** and **Done** cover both the profile and the code.

## Try the keys

Select **Try** (the flask) on the top bar to play with the keys on the live game without leaving the editor; it stays lit while you try. Select it again to go back to editing. In the logic view it switches back to the picture first.

![While you try, the picture is no longer dimmed and the keys work as usual.](figure:editor-try)

## Preview other screens

**Screen shape to preview** shows how the keys sit on a phone with another screen shape, such as 20:9 or 21:9. It is a preview only; switch back to the phone's own shape to edit.

## Save

- **Save**: keeps your changes, including the code files changed in the logic. **Done** saves and closes the editor.
- **Save as**: **More > Save as…** saves the changes as a new profile and goes on editing it; the original stays as it was last saved.
- **Discard**: **More > Discard changes and exit** closes the editor without saving.
- **Profiles from extensions**: your changes are kept separately, and **More > Restore original…** removes them.

If you close the mirror window with changes that are not saved (to the profile or the code), GameScrcpy asks whether to save them.

## An AI's draft

When an AI client connected through MCP changes a profile, the changes wait in that client's draft. The profile's card on the Profiles page then shows a button such as **Claude's draft**: it opens this editor on the draft, without changing anything yet.

The editor is read-only in that mode. A bar at the top says whose draft it is, with **Apply**, **Discard** and **Close**. On the picture, keys the draft adds are outlined in green, keys it changes in orange, and keys it removes in red, drawn without colour at the place they used to be. The side panel lists every changed key, the logic view outlines the blocks that changed and lists the blocks the draft removes, and the code view shows the changed files as a line-by-line diff. **Apply** makes the draft the profile (your click is the confirmation), **Discard** throws it away, and **Close** leaves the preview and keeps the draft for later.

## Shortcuts in the editor

| Keys | What they do |
|---|---|
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Undo the last edit |
| <kbd>Ctrl</kbd>+<kbd>Y</kbd> | Redo the edit |
| <kbd>Ctrl</kbd>+<kbd>S</kbd> | Save the profile |
| <kbd>Ctrl</kbd>+<kbd>D</kbd> | Duplicate the selected keys |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | Select every key |
| <kbd>Delete</kbd> | Delete the selected keys |

You can change these keys in **Settings > Keyboard shortcuts**.
