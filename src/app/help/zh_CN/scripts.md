方案可以带上逻辑：一段用 TypeScript 或 JavaScript 写的小程序，在使用这个方案时运行。它能用方案里的键位，在手机上点击和滑动，等一会儿，在投屏窗口里提问，还能在手机画面里找图和找颜色。不写逻辑，GameScrcpy 的其他功能照样能用。

## 打开逻辑

1. **打开逻辑视图**：编辑方案时（投屏窗口工具栏的“编辑键位”），把顶栏中间的“画面”切到“逻辑”。也可以在投屏窗口工具栏的“更多”里选“编辑逻辑”，或在“方案”页方案卡片的“更多”（…）里选“编辑逻辑…”：有手机正在用这个方案时打开那台手机的编辑器，没有时单独开一个只有逻辑的窗口。
2. **添加逻辑**：方案还没有逻辑时，点“添加逻辑”。GameScrcpy 会写好 `logic.ts` 和 `tsconfig.json`，里面用到你方案里的第一个键位。

方案原来是一个 `.json` 文件时，GameScrcpy 会先把它变成同名的方案文件夹：里面有 `profile.json`（键位）和 `logic.ts`（逻辑）。扩展带来的方案不能有自己的逻辑，要先复制一份到你的方案里。

![逻辑视图](figure:script-workbench)

| 区域 | 说明 |
|---|---|
| :folder-open: 文件 | 左边：方案文件夹里的文件；可以新建文件。 |
| :code-xml: 编辑器 | 中间：打 `touch.` 列出能用的函数，打 `controls.` 列出方案里的组件；保存后自动检查。右上角的“蓝图 / 代码”切换显示方式（每个组件有自己的文件，见下面的“蓝图”）。 |
| :play: 运行与接口 | 右边：看逻辑在不在这台手机上跑；“运行”会保存改过的文件并重新启动逻辑。“接口”页可以搜索全部接口和例子。 |
| :terminal: 控制台与问题 | 下面：逻辑用 `log.info` 等写下的内容和出的错；点一条问题跳到出错的地方。 |

顶栏的“保存”同时保存方案和改过的代码文件，“完成”保存后退出编辑。

> [!TIP]
> **喜欢用别的编辑器？** 在顶栏的“更多”里选“用外部编辑器打开”，装了 Visual Studio Code 时会用它打开整个文件夹。在外面改了文件，逻辑视图会自动读入；你在逻辑视图里也改过同一个文件时，会让你选“重新载入”还是“保留我的”。

## 编写逻辑

方案里的每个键位在 `profile.json` 里都有一个 id，比如 `fire` 或 `jump`。在逻辑里，`controls.fire` 就是这个键位：

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

- `onPress` 在按下键时运行，`onRelease` 在松开键时运行，`whileHeld` 一直运行到松开键为止。
- 每个组件的方块列表只列对它有用的方块：摇杆上没有通用的“按下、按住、点一下”方块（用“推”和“走一段”），视角没有按键事件，键位的开关只有一个属性方块，不再另外有打开、关闭、切换三个。列表里没有的写法自己写照样能用：`trigger()`、`press()`、`hold()`、`enable()`、`disable()`、`toggle()` 还是原来的意思。
- 组件上的“按下时”是它的键，“开始触摸时 / 结束触摸时”是它按到屏幕上的手指。点击类的触摸只有一瞬间，要观察就接触摸事件，不要读 `touching` 这个状态（列表里也已经没有它了）。
- 逻辑接了点击、按住、长按、连发、滑动、多点同按的这三种按键事件，这个键就交给逻辑：按下它时组件自己不再按屏幕，只运行逻辑；要它按，就在逻辑里调它的函数（如 `controls.fire.press()`、`controls.jump.tap()`），放在哪一步就在哪一步按，会比组件自己按晚几毫秒。想让组件照常先按、再运行逻辑，写 `controls.fire.onPress(处理函数, { consume: false })`，蓝图里就是事件方块上“按这个键时”选“照常按下，再运行逻辑”。轮盘、视角、技能等组件照常工作，逻辑只是旁听；只有方案自己的逻辑会拿走按键，任务、扩展的脚本和 AI 运行的脚本只旁听，组件类里改写的事件方法照常让本体先按。
- id 里有连字符时，要写在方括号里：`controls["dash-move"]`。
- 位置用画面的比例表示：(0, 0) 是左上角，(1, 1) 是右下角，所以逻辑在任何分辨率下都能用。
- `ui.toast("Round 2")` 会在投屏窗口里显示一条短消息。
- [`clipboard.read()`](help:api/clipboard) 和 `clipboard.write(text)` 读写的是手机的剪贴板，不是电脑的。手机这时不能共享剪贴板时（比如在独立显示器上打开的应用窗口里），它们分别得到 `null` 和 `false`。
- `ui.mark(0.5, 0.8, { label: "这里" })` 在画面上用一个小圆圈标出一个点，对它调用 `dispose()` 或画它的任务结束时消失。
- `window.focused()` 说明投屏窗口是不是当前的活动窗口；投屏窗口每次变成活动窗口时运行 `window.onFocus`，不再是活动窗口时运行 `window.onBlur`。

在逻辑、任务和组件文件里，这些函数和对象也可以从内置模块 `gamescrcpy` 导入，和全局的是同一套：

```ts title="logic.ts"
import { controls, touch, log } from "gamescrcpy";

controls.fire.onPress(async () => {
    await touch.tap(0.5, 0.8);
    log.info("tapped");
});
```

> [!WARNING]
> **循环里一定要等待**：从不 `await` 的循环会卡住逻辑，会被停止。

## 流程与运算

除了“分支 / 循环 N 次 / 遍历 / While 循环”，选块器里还有这些，每样都写成普通的 TypeScript：

- **多路分支**：`switch (值) { case 1: … default: … }`，每个分支一个输出，另有一个“默认”；分支的值在方块的细节里改，个数不变。
- **序列**：`await sequence(async () => { … }, async () => { … })`，几个输出按先后一个接一个跑。
- **多路门**：`await multiGate("门", 3, false, false, …)`，每跑一次走下一个输出，“完成”在都跑过之后才跑；“循环”让它从第一个重来，“随机”把顺序打乱。
- **执行 N 次**：`if (doN("名字", 3)) { … }`，同一个名字前三次是“是”，之后是“否”，“重置执行 N 次”（`resetDoN("名字")`）让它重新数。
- **按范围循环**：`for (let i = 0; i <= 2; i++)`，索引是一个输出；**带索引遍历**：`for (const [索引, 项] of 列表.entries())`。
- **列表**：`list.add`、`list.insert`、`list.removeAt`、`list.remove`、`list.indexOf`、`list.at`、`list.setAt`、`list.size`、`list.has`、`list.sort`、`list.shuffle`、`list.clear`，改的就是那个列表本身。
- **字典**：`dict.make`、`dict.set`、`dict.get`、`dict.has`、`dict.remove`、`dict.keys`、`dict.values`、`dict.size`、`dict.clear`；**集合**：`set.make`、`set.add`、`set.has`、`set.remove`、`set.values`、`set.size`、`set.clear`。
- **文字**：`text.join`、`text.format`（填 `{0}`、`{1}`…）、`text.find`、`text.has`、`text.split`、`text.replace`、`text.upper`、`text.lower`、`text.trim`、`text.length`。
- **数学**：`math.round`、`math.floor`、`math.ceil`、`math.abs`、`math.min`、`math.max`，与原来的 `math.clamp`、`math.lerp`、`math.random`、`math.randomInt` 在一起。

## 蓝图

右上角的“蓝图”把逻辑画成方块。逻辑照 UE 的结构分文件放：主页 `logic.ts` 像关卡蓝图，放逻辑载入时、定时触发、变量事件和全局的逻辑；每个组件自己的逻辑放在 `controls/<代码名>.ts`，像摆在关卡里的 Actor 蓝图。各文件的变量互不相通，要共用就用方案变量（`vars`）或全局变量（`globals`）；组件之间照旧经 `controls.<代码名>` 互相读状态、调函数。每个函数也有一页。

左边“我的蓝图”里点一个组件就打开它自己的文件，页签标题是组件的名字和按键（如“开火 · F”），页签条最前面的“主页”回到 `logic.ts`；键位属性里的“在蓝图里引用它”也打开这个键自己的文件。点击、按住、长按、连发、滑动、多点同按这几类组件的文件一打开就摆好了它的按键事件，开头一块“默认动作”：键照常按下，后面接的方块在它之后运行；删掉默认动作，这个键就只运行逻辑，想在哪一步按就把“按下”方块放在那一步。只看不改时不会建文件，放了第一个方块才建。组件文件里的“点击屏幕”“长按屏幕”“按下触点”“滑动屏幕”不填位置时用这个组件的位置，引脚上写“本组件位置”。改代码名时它的文件跟着改名；删组件时选“连这些方块一起删”会连它的文件一起删。写在 `logic.ts` 里的组件事件照常运行，画在主页上。蓝图会停在上次的地方：同样的位置和缩放。

几个文件都要用的函数放进函数库 `library.ts`，像 UE 的蓝图函数库。“我的蓝图”里“函数库”一节列出它里面的函数，点“+”打开函数库（还没有就开一个新的，放了第一个函数保存时才建文件），在那里点“函数”一节的“+”新建函数，函数库里的函数会写成 `export function`。别的文件里，这些函数出现在选方块的列表里（角标“函数库”），放下时蓝图自动在文件开头写好 `import { 函数名 } from "./library.ts"`（组件文件里是 `../library.ts`）；双击这样的方块打开函数库里的那个函数。

要在两个已经连起来的方块中间插一个，三种办法都行：

- **把一个没接线的方块拖到执行线上**：拖动时指针下的执行线会变亮，松手就插在两者之间。
- **在线上右键**，选“在这里插入方块…”，再从列表里选一个方块。
- **从已经连着线的执行引脚往外拖**，在空白处松手，再选一个方块；它插在中间，原来那根线照旧。

插进去以后，后面的方块会自动往右让出位置；整个插入算一步撤销。

从带分支的方块（比如“查找图像”、事件、“遍历”、“循环 N 次”）上把值拖到一个没接线的方块上时，会先把这个方块接到值所在的那条分支末尾，所以“点击屏幕”能直接用查找图像给的位置。解析不了的游离代码会显示成一个带错误说明的代码块，在那个方块里改好就行。

## 等到画面变了再继续

点了一下，常常要等画面变了才能做下一步。在动作后面接 `.until`，写出“变好了”的条件：

![等到画面变了再继续](figure:until-flow)

```ts title="logic.ts"
const button = await touch.tap(0.9, 0.9)
    .until(() => vision.find("开始按钮"), { timeoutMs: 3000, retry: 2 });
```

- 动作做完后，每一帧检查一次条件，成立就返回它的结果，这里是找到的位置。
- 过了 `timeoutMs`（默认 5000 毫秒）还不成立，就再做一次动作，最多重做 `retry` 次。都不成立时抛出 `TimeoutError`，错误里写着是哪一行。
- 所有动作都能接 `.until`：点击、滑动、按键、`controls.fire.trigger()` 等。

## 让键位跟着状态开关

`bind` 让键位的开关跟着状态走：

```ts title="logic.ts"
controls.fire.bind("enabled", () => !vars.boat.value);
```

在船上时“开火”键失效，下船后恢复。条件里读到的状态一变，开关就重新计算。在任务里建立的绑定，任务结束时撤销，键位回到原来的开关；直接设置 `controls.fire.enabled` 或调用 `mapping.setEnabled` 会结束绑定。

## 变量

变量就是文件最外层的一个 `let`。写上类型、初始值，并在它上面注明它是做什么的、在列表里归到哪一组：

```ts title="logic.ts"
/** 英雄的血量。 @category 战斗 */
let 血量: number = 100;

/** @category 战斗 */
let 目标: PicturePoint[] = [];
```

- 类型可以是 `boolean`、`number`、`string`、`PicturePoint`、`Region`、`HexColor`、`ImagePath`、`InputName`、组件（如 `Component`），或你自己写的类；列表在后面加 `[]`，字典写 `Map<string, number>`，集合写 `Set<number>`。
- 整数和小数在代码里都是 `number`：小数要写成带小数点的形式（`1.5`），编辑器会记住是哪一种。毫秒数注明 `@duration` 就是时长。
- `@category` 让它在“我的蓝图”里归到这个名字下面；注释里其余的文字在鼠标停在它上面时显示。
- 注明 `@keep` 时，初始值是上次运行留下来的值，下次运行时接着用：`let 最高分: number = (store.get("最高分") as number) ?? 0;`。在蓝图里设值时，会同时写回 `store.set`。
- 组件类里的变量还可以是对外属性：写进 `static props`（`最高分: prop.number(0, { label: "最高分" })`），摆放出来的每个组件都能在属性面板里改它；字段从那时的 `this.props.最高分` 开始。

蓝图里，“我的蓝图”的“变量”一栏按类型和分组列出它们。点这一栏的 **+** 新建：名字、类型、单个还是一组、初始值、分类和说明，以及 **下次打开还在** 和 **在属性面板里可改** 两个勾选框；保存时写出的代码会显示出来。在函数页里，这一栏是这个函数自己的变量，**+** 把它声明在函数体的开头。

- 把变量拖到画布上，选 **取值** 或 **设值**；拖动时按住 Ctrl 是取值，按住 Alt 是设值。拖到引脚上就直接填进那个引脚。
- 在输入引脚上右键，选 **提升为变量**，会按引脚里现在的值建一个变量并接上。
- 在列表里双击变量可以修改；改了名字时，用到它的地方都会跟着改，而且只改属于它的地方。
- 还在用的变量不能删除：先把那些地方改掉。

## 自定义组件

在逻辑里写一个继承 `Component` 的类，就是你自己的组件：`static props` 声明它的属性（`prop.key`、`prop.position`、`prop.number`、`prop.text`、`prop.toggle`、`prop.component`），`this.event()` 做对外的事件，`setup()` 里写它怎么工作，`this.finger` 是它自己的一根手指——按下后一直按着，直到 `up()` 或逻辑停止。在逻辑的最外层用 `new` 建出来：

```ts
class 按住切换 extends Component {
    static info = { name: "按住切换" };
    static props = { 按键: prop.key("F"), 位置: prop.position(point(0.5, 0.8)) };
    按下时 = this.event();
    松开时 = this.event();
    按着 = false;

    setup() {
        this.props.按键.onPress(() => {
            this.按着 = !this.按着;
            if (this.按着) this.finger.down(this.props.位置);
            else this.finger.up();
            (this.按着 ? this.按下时 : this.松开时).emit();
        });
    }
}

const 蹲 = new 按住切换({ 按键: "C", 位置: point(0.85, 0.75) });
蹲.按下时.on(() => log.info("蹲下了"));
```

再比如双击：

```ts
class 双击 extends Component {
    static props = { 按键: prop.key("G"), 位置: prop.position(point(0.5, 0.5)) };

    setup() {
        this.props.按键.onPress(async () => {
            await touch.tap(this.props.位置);
            await sleep(80);
            await touch.tap(this.props.位置);
        });
    }
}

new 双击();
```

`this.props` 已经换好了：按键是按键对象（能 `onPress`、`onLongPress`），位置是点，填了 id 的组件就是那个组件。

上面是在方案自己的逻辑里写的组件。要放进组件库、摆到画面上，在键位编辑器的组件库里点“新建组件…”（详见[键位编辑器](help:keymap-editor)）：组件存在数据目录的 `components/<文件夹>/` 里，`component.ts` 的默认导出就是这个类，`component.json` 记着名字、一句说明、图标和分类。摆到画面上的组件，按键、属性值和位置都来自方案：`this.position` 是它在画面上的位置（画面比例），位置属性是其余几处。几点要知道：

- `this.position` 和位置属性按逻辑启动时这台手机的画面算，转屏后不跟着变，重新载入逻辑才更新。
- 类里的按键属性不支持 `claim`；组件文件里不能用顶层 `await`，只能相对导入自己文件夹里的文件、用 `../<文件夹>/component.ts` 导入你的另一个组件，以及内置模块 `gamescrcpy`。

### 在本体组件上加功能

组件也可以继承 13 个本体组件（`Tap`、`Hold`、`LongPress`、`Turbo`、`Swipe`、`Pinch`、`Wasd`、`RightWalk`、`PadStick`、`Camera`、`PadCamera`、`FreeLook`、`Skill`）。摆到画面上，它就是那个本体组件：按法和延迟都和本体一样，照样立刻跟手；类里写的同名事件方法在本体的事件发生时再跑一遍，写不写 `super.onPressed()` 都一样。本体怎么按改不了，要改按法就从 `Component` 写起。`static props` 照常出现在属性面板上；和本体按键同名的按键属性不用。

```ts
export default class 开镜提示 extends Hold {
    static props = { 提示: prop.text("开镜") };

    onPressed() {
        ui.toast(this.props.提示);
    }
}
```

继承你的另一个组件时从它的文件夹导入，如 `import 连击 from "../连击/component.ts";`，属性和子组件一起继承下来。

### 子组件

字段里写 `this.child(本体组件类)`，组件就带上子组件。拖到画面上时整组一起放下，每个子组件是一个本体组件，各有各的键：

```ts
export default class 连招 extends Component {
    static props = { 按键: prop.key("R") };
    取消 = this.child(Tap);
    技能 = [this.child(Skill), this.child(Skill)];

    setup() {
        this.props.按键.onPress(async () => {
            for (const 技能 of this.技能) {
                await 技能.trigger();
            }
        });
        this.取消.onPress(() => this.技能[0].cancel());
    }
}
```

`this.child()` 按类里声明的先后取方案里的子组件，方案里没有时是 `null`。类里新加了子组件，下次打开键位编辑器时会放到组旁边。

### 接口

不同种类的组件可以实现同一个接口，逻辑里一次叫到它们全部：

```ts
const 开火 = defineInterface<{ 开火(): void }>("开火");

export default class 三连发 extends Turbo {
    static implements = [开火];

    开火() {
        this.burst(3);
    }
}
```

逻辑里写 `for (const 组件 of 开火.all()) 组件.开火();`（或 `mapping.all(开火)`），每个实现了“开火”的组件都会开火；同名的 `defineInterface` 拿到的是同一个接口。`prop.component(Skill)` 这样的属性在属性面板里只列技能组件。

## 结构、枚举与类

除了组件，逻辑里还能声明 UE 蓝图有、我们的蓝图也画成方块的这三样。它们都是同一个文件里的普通 TypeScript，手敲和用蓝图建出来的方块一样。

**结构**是一组带类型的字段。在“我的蓝图”的“结构”一节点 **+**，编辑窗里填字段名、类型、以及这个字段可不可以不填（`名字?: string`）。蓝图里有“合成”（每个字段一个输入，给出这个结构）和“拆开”（结构进去，每个字段一个输出）；读一个字段写成 `起点.x`，合成写成 `{ x: 0.5, y: 0.5 }`。

```ts
interface 位置 {
  x: number
  y: number
}
```

**枚举**是一组固定的选项。TypeScript 的 `enum` 过不了擦除检查，所以枚举写成一个 `const` 对象加旁边的联合类型；编辑窗两行一起写、改名时一起改：

```ts
const 阵营 = { 红: "红", 蓝: "蓝" } as const
type 阵营 = typeof 阵营[keyof typeof 阵营]
```

蓝图里每个选项一个方块（`阵营.红`），还有“按选项分支”，每个选项一个执行输出；比较两个值用普通的相等方块。类型是这个枚举的变量或字段只能填它的选项。

**类**只装数据和函数，不在画面上。在“类”一节点 **+**，编辑窗里加成员变量、函数、事件分派器，可以勾“私有”，可以给类一个构造函数或父类；每个函数跟组件类一样在蓝图里有自己的一页。

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

私有用的是 TypeScript 自己的 `private`：运行前会被擦掉，所以它让代码读起来清楚、编辑窗里标出来，而不是在运行时真的挡住这个字段。子类改写父类的函数时，蓝图里有“调用父类”（`super.受伤(…)`）。

蓝图里有“新建”（构造函数的参数，给出对象）、“转换为某个类”（`对象 instanceof 敌人`，带“是”“不是”两个输出）、读取与设置成员、调用函数，以及类声明的事件分派器的“触发”“绑定”“解绑”（分派器写 `this.event()`；绑定给出订阅对象，解绑调它的 `dispose()`）。

## 任务

任务是独立运行的逻辑，比如每日签到、挂机刷本：它不跟方案切换，换游戏也照样跑；方案的键位照常可用，可以边挂机边手动操作。在中枢的“任务”页点“新建任务”，GameScrcpy 会在任务文件夹里写好一个能直接运行的 `task.ts`：

```ts
export default class 刷本 extends Task {
    static params = { 局数: param.number(10, { label: "局数", min: 1 }) };

    async run({ signal }: TaskContext) {
        for (let 局 = 1; 局 <= this.params.局数 && !signal.aborted; 局++) {
            this.progress("第 " + 局 + " 局");
            await touch.tap(0.5, 0.8);
            await sleep(5000);
        }
    }
}
```

- `static params` 里声明的设置（`param.number`、`param.text`、`param.toggle`）会出现在任务页右边，填的值在 `this.params` 里。
- `this.progress(…)` 写的进度显示在任务卡片和投屏窗口工具栏的“任务”菜单里。
- 在任务页勾选在哪些手机上跑，每台各跑一份；“怎么开始”可以选每天或只一次的时间，到点时在勾选的、正在投屏的手机上开始。出错时可以选再跑一次。
- 任务能调这台手机当前方案的组件，比如 `controls.拾取.tap()`；组件的实时状态和按键事件归方案的逻辑。
- 任务页的“编辑逻辑”打开它的代码；“运行”在选的手机上重新开始这个任务。

## 识图工作台

在“方案”页方案卡片的“更多”里选“识图资源…”，或在投屏窗口工具栏的“更多”里选“识图工作台”。窗口里是所选手机的实时画面：

1. **冻结画面**：点上方的“冻结画面”，把当前画面停住。
2. **框出要找的东西**：用画面下方工具条里的工具框出模板（要找的图）、区域、点位和路径，或用取色工具在画面上点一下取一个颜色。
3. **起名字、调参数**：在右边给它起个名字，调相似度阈值、匹配方式（彩色或灰度）和只在哪个区域里找。
4. **测试找图**：点上方的“测试找图”，看它在画面里能不能找到、找到几处、要多久。

![识图工作台](figure:vision-freeze,vision-draw,vision-name,vision-test!)

AI 客户端在方案里干活、缺了图、颜色、区域或点位时，可以向你要素材，而不是自己猜：GameScrcpy 把窗口调到前面，在这个 AI 该方案的草稿上打开识图工作台，右边列着它要的清单。逐项框选、取色或点位置；不想要的点“跳过”，也可以改选方案里已有的同类素材，最后点“交给 AI”。这些存进 AI 的草稿，不动你在用的方案；AI 会拿到每项的名字和值。

这些都存在方案文件夹的 `res` 文件夹里。逻辑里直接用名字，编辑器会提示名字，写错了检查时会报出来：

```ts title="logic.ts"
const start = await vision.find("开始按钮");
const banner = await vision.findColor("活动横幅");
const entry = res.point("活动入口");
await touch.tap(entry.x, entry.y);
```

只看一个像素时，`vision.pickColor(point(0.5, 0.1))` 给出它的颜色（写成 `"#RRGGBB"`），`vision.matchColor(point(0.5, 0.1), "活动横幅")` 判断它是不是这个颜色。

找到某个模板或颜色就切到另一套做法，用这些接口自己写就行。

## 同时做几件事（蓝图）

在蓝图里，从一个已经有线的执行输出再拖一根线到别的方块的执行输入：从这时起两条线同时开始，谁也不等谁。蓝图上没有这个方块，代码里写成 `await parallel(...)`，每根线一个箭头函数：

```ts
await parallel(async () => {
    await controls.move.reset();
}, async () => {
    await controls.view.reset();
});
```

在同一个执行输出上再拖第三根线就再多一个箭头函数。去掉其中一根线（选中它删掉，或者按住 Alt 点这个执行输出一次去掉全部），对应的箭头函数也去掉；只剩一根时就变回普通的串行语句。从还没有线的执行输出拖线，仍然是把新方块接在它后面。

## 检查和运行

- 在投屏中的手机上选用这个方案，逻辑会自动开始运行。保存 `logic.ts` 后逻辑会重新加载，不用重新连接。
- 逻辑出错时，投屏窗口会显示错误，以及出错的文件和行号；逻辑视图的“控制台”里也有。
- 用 `log.info`、`log.warn` 和 `log.error` 写的内容还会进入 GameScrcpy 的日志文件，在 `profiles` 文件夹旁边的 `logs` 文件夹里。

## 命令行工具

`gs.exe` 是 GameScrcpy 的命令行工具，和 `GameScrcpy.exe` 在同一个文件夹里。逻辑视图的检查就是用它做的，你也可以自己运行：

| 命令 | 作用 |
|---|---|
| `.\gs.exe check <文件夹>` | 检查逻辑，直到显示“no problems”。 |
| `.\gs.exe types <文件夹>` | 改了 `profile.json` 或资源之后更新类型文件，外部编辑器才认得新的键位和名字。 |
| `.\gs.exe run <文件夹> --replay <session.json>` | 回放一个会话文件（带时间的按键记录），并打印逻辑会做什么。 |
| `.\gs.exe test <文件夹>` | 运行这个文件夹里 `tests` 文件夹中的测试。 |
| `.\gs.exe new <新文件夹> --profile <方案.json>` | 用导出的方案新建一个带逻辑的方案文件夹。 |

用 `gs new` 建的方案文件夹里有 `AGENTS.md`，写着这些命令的说明。

## 学习脚本接口

接口文档由程序本身生成，所以总是和你用的 GameScrcpy 版本一致。说明是中文的；函数名、参数名和示例保持写进代码时的样子。

- [脚本接口总览](help:api)
- [touch](help:api/touch)：点击和滑动
- [mapping](help:api/mapping)：当前方案的键位
- [ui](help:api/ui)：投屏窗口里的消息、提问和绘制
- [window](help:api/window)：投屏窗口是不是当前的活动窗口
- [vision](help:api/vision)：在手机画面里找图和找颜色
- [res](help:api/res)：识图工作台里起了名字的点位、区域和路径
- [store](help:api/store)：多次运行之间保留的数据
