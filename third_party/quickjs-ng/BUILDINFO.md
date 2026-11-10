# QuickJS-ng 的来源

- 项目：QuickJS-ng，https://github.com/quickjs-ng/quickjs
- 版本：v0.17.0（GitHub 发布，2026-09-18；标签指向提交 `6d46d07d04041b40f4f49eaa7fdebe44c314c699`）
- 来源包：https://github.com/quickjs-ng/quickjs/archive/refs/tags/v0.17.0.tar.gz，SHA256 `559bc4c420475e55c7ab4510adbc562f55d7524d75e8e89d79ce4bb02f5687d9`
- 许可：MIT；`LICENSE` 取自来源包，原样未改
- 取得日期：2026-10-30
- 用途：脚本引擎（ADR-0007、0065，PLAN 4.8）；目标 `gs::quickjs`（静态库，不套用本项目的"警告即错误"），由 `gs_script` 链接
- 保留：编译引擎所需的四个源文件（`quickjs.c`、`dtoa.c`、`libregexp.c`、`libunicode.c`）及其头文件；除下面"本地补丁"一处外原样未改。`builtin-*.h` 是上游用 qjsc 从同名 `.js` 生成的引擎内置字节码（`Array.fromAsync`、`Iterator.zip`），属于引擎自身，不是外来字节码
- 本地补丁（2026-10-31，ADR-0065 实施说明）：`quickjs.c` 的 `JS_CallInternal` 里 `OP_goto`、`OP_goto16`、`OP_goto8`、`OP_if_true`、`OP_if_false`、`OP_if_true8`、`OP_if_false8` 七处，把 `js_poll_interrupts` 从跳转之后挪到跳转之前。原因：中断在回跳之后触发时，`exception:` 处按 `pc - 1` 查行号，指到循环开头之前的那条指令，看门狗报的是函数头而不是死循环所在的行；挪到前面后 `pc - 1` 就是跳转指令本身。中断异常不可捕获，挪动不改变语义。同一目的的第二处：编译器只在表达式、调用处记源码位置，空循环（`while (true) {}`、`for (;;) {}`）里的跳转查不到自己的行，所以在 `js_parse_statement_or_decl` 里，`while` 在条件开头与回跳前、`do…while` 在回跳前、经典 `for` 在循环体结束后各加一句 `emit_source_loc_at(循环关键字的行列)`（只进行号表）。升级时先看上游是否已改，没改就照此重做两处，并更新下表 `quickjs.c` 的两个校验和
- 不带：`quickjs-libc`（文件、进程、网络、Worker）、`qjs` / `qjsc` 命令行、REPL、测试、示例、文档、test262、Unicode 表生成器与上游的构建文件
- 构建：`CMakeLists.txt` 是本项目自己写的；编译选项照上游 CMake 对 MSVC 的设置（C11、`/experimental:c11atomics`、`QUICKJS_NG_BUILD`、`_GNU_SOURCE`、`WIN32_LEAN_AND_MEAN`、`_WIN32_WINNT=0x0601`），另关掉 MSVC 在 `quickjs.c` 里报的 C4098（void 函数返回 void 表达式，上游写法）
- 升级：下载新版本的来源包并记下 SHA256，按下表的文件名覆盖，核对上游 `CMakeLists.txt` 中 `qjs` 目标的源文件与编译选项有无变化，更新本文件与 `third_party/README.md`，重新跑 `gs_script_tests`

| 文件 | SHA256 |
|---|---|
| `LICENSE` | `96f73f9d2a16c21a36b418f06073be26e7d6d5e7c1bc99756b21a4f2c74ef171` |
| `builtin-array-fromasync.h` | `f41eb2eb028832cff9a5f8d8d2cb438c27771a9d7d85c30dfd16aa78f34fe6de` |
| `builtin-iterator-zip-keyed.h` | `24446c6157a043cfeb0b0255c7892f5146809bd3098c70028bfa1ee5311b91f4` |
| `builtin-iterator-zip.h` | `fdaef760ecce82c1f8b62dca8d0d424ae05ef88a0037e32221858ab7ac8994c9` |
| `cutils.h` | `dffec8bea17f08108da5a91c1178804754698bbbb6202dc100268e78e8c5b770` |
| `dtoa.c` | `a8d2cf2d04db406e0c16acdd95ab0e709d309902865722f41dac07a1ba5676af` |
| `dtoa.h` | `1c4a4540c0038632c8a4c225472fbc21bd476fb35c4da4a857a11965b58231b9` |
| `libregexp-opcode.h` | `2a98d646089f3a72f25480116297c5fdb5b28fd9f815447273951f4ade4a115e` |
| `libregexp.c` | `af13e996abb1767fe0cdfda123c45b811d53688d403a2ac47142e97bf79f1fae` |
| `libregexp.h` | `53ff95a038f7001b752c455d195d998e2955f0ae4ed62a002c13eca798db3ceb` |
| `libunicode-table.h` | `5840a921ac11664b505ee37b1219f518fff2a04972fe848b4bdf1e85c5a0bd83` |
| `libunicode.c` | `d755606498ff707dc3a93826713c749f77b9222d8a6b066621d72a308460bf2d` |
| `libunicode.h` | `ad13f66aaea3fab1616e84b9c048f2a315e1bdbab628b966617a7a3e55bcba2b` |
| `list.h` | `cb6e24e8ee54bccdd66234fe54cd702d1160b9841db2b9b9adeb5003afe301d5` |
| `quickjs-atom.h` | `66654ae6533d15073ea527979c453709d7ed83162142c656095a10dcef737a60` |
| `quickjs-c-atomics.h` | `29883c742a5c512278fce54d19b1b9af949006e1d7dfc9ffa2e0946b184d8c5c` |
| `quickjs-opcode.h` | `0054b0c45fd091afa308c3fb93b5862e0b071235918d3d73e413d23fce388bbc` |
| `quickjs.c` | 打补丁后 `6a2d3c7d6ed317169ffe52edad68b35f1d35397fb8f33c0582f124f80a85e00d`（来源包原文件 `9fd0e0e68856d165e2872140ac38889845b53eb1bc55150f8598a53eb86948f5`） |
| `quickjs.h` | `747a77444ff04a910b57ca51dd998d7ee46895b1b9592d7d6051fd180317c44d` |
