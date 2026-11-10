# TypeScript 原生编译器的来源

- 项目：TypeScript（微软），https://github.com/microsoft/typescript-go
- 包：npm 的 `@typescript/typescript-win32-x64`，版本 7.0.2（`typescript@7.0.2` 的 Windows x64 原生编译器；`typescript` 包里的 `tsc` 只是启动它的 Node 脚本）
- 来源包：https://registry.npmjs.org/@typescript/typescript-win32-x64/-/typescript-win32-x64-7.0.2.tgz，9 776 626 字节，SHA-256 `61fc4e141d2bc687db580e71bbfa63b9c209f0310645d82ca1b457eb3a24fd19`，npm 登记的完整性 `sha512-0BQ3HkAHHlKLSp1qRvf3SUhGpGsDuhB/jgFw75guyqbxJqEaS0Cw/VFO8i2nHglJUzQCRtMMR/IBAKE3ETMC4g==`（下载后已核对一致）
- 许可：Apache-2.0；`LICENSE` 与 `NOTICE.txt`（它引用的第三方声明）取自来源包，原样未改
- 取得日期：2026-10-31
- 用途：脚本的类型检查（ADR-0065"修订（2026-10-31，用户）"：程序自带原生编译器，写脚本的人不用装 Node）；由 `gs check` 与脚本工作台调用，程序运行脚本不用它（运行时只用内置擦除器）
- 保留：来源包的 `LICENSE`、`NOTICE.txt` 与 `lib/` 整个目录（`tsc.exe` 与 108 个标准库类型声明 `lib.*.d.ts`，编译器在自己旁边找它们），原样未改；不带 `package.json`、`README.md`
- 构建：`src/cli/CMakeLists.txt` 把 `lib/` 复制到 `gs.exe` 旁边的 `typescript/`；许可证由 `tools/gen_licenses.py` 收进"关于"页
- 升级：`npm pack @typescript/typescript-win32-x64@<版本>` 下载，核对 `npm view @typescript/typescript-win32-x64@<版本> dist.integrity`，解开后用包里的 `LICENSE`、`NOTICE.txt`、`lib/` 整个替换本目录对应内容，更新本文件与 `third_party/README.md` 的版本和校验和，重新跑 `gs_cli` 的检查测试

| 文件 | SHA256 |
|---|---|
| `LICENSE` | `a7d00bfd54525bc694b6e32f64c7ebcf5e6b7ae3657be5cc12767bce74654a47` |
| `NOTICE.txt` | `f5c708b59114507b8b27b48181b6883d106bbca0c1634bbee45b5e344237b66b` |
| `lib/tsc.exe` | `f9ecfbdc93753d2c972d66a8d0d75f5bd737fd4a5f88b422d9091ea282bcb2c7` |
| `lib/lib.*.d.ts`（108 个） | 与来源包逐字节相同，按上面的来源包校验和重新解出即可核对 |
