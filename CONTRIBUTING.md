# 贡献指南

感谢你对 QtScrcpy 项目的关注！我们欢迎任何形式的贡献。

## 📋 目录

- [行为准则](#行为准则)
- [如何贡献](#如何贡献)
- [开发环境](#开发环境)
- [代码规范](#代码规范)
- [提交规范](#提交规范)
- [Pull Request 流程](#pull-request-流程)

## 行为准则

请在参与项目时保持友善和尊重。我们致力于为所有人提供一个开放、包容的环境。

## 如何贡献

### 报告 Bug

1. 首先搜索 [Issues](../../issues) 确认问题未被报告
2. 使用 Bug 报告模板创建新 Issue
3. 提供以下信息：
   - 操作系统和版本
   - Qt 版本
   - Android 设备型号和系统版本
   - 复现步骤
   - 预期行为和实际行为
   - 相关日志或截图

### 功能建议

1. 搜索现有 Issues 确认建议未被提出
2. 使用功能请求模板创建新 Issue
3. 清晰描述功能需求和使用场景

### 代码贡献

1. Fork 本仓库
2. 创建功能分支：`git checkout -b feature/your-feature`
3. 提交更改：`git commit -m 'feat: add some feature'`
4. 推送分支：`git push origin feature/your-feature`
5. 创建 Pull Request

## 开发环境

### 必需工具

- **Qt**: 6.2+ 或 5.15+
- **CMake**: 3.19+
- **编译器**: 
  - Windows: MSVC 2019+
  - macOS: Clang 12+
  - Linux: GCC 9+
- **Android SDK**: 用于构建服务端

### 环境配置

```bash
# 克隆仓库
git clone https://github.com/YOUR_USERNAME/QtScrcpy.git
cd QtScrcpy

# 客户端构建
cd client
mkdir build && cd build
cmake ..
cmake --build .

# 服务端构建
cd ../../server
./gradlew build
```

### IDE 推荐

- **Qt Creator** - 推荐用于客户端开发
- **Android Studio** - 推荐用于服务端开发
- **VS Code** + CMake 插件 - 轻量级选择

## 代码规范

### C++ 代码风格

- 使用 C++17 标准
- 遵循 Qt 编码规范
- 类名使用 PascalCase：`VideoForm`
- 函数和变量使用 camelCase：`startServer()`
- 常量使用全大写：`MAX_BUFFER_SIZE`
- 私有成员使用 `m_` 前缀：`m_device`

```cpp
// 示例
class VideoDecoder {
public:
    explicit VideoDecoder(QObject *parent = nullptr);
    
    void startDecode();
    bool isRunning() const;

private:
    void processFrame();
    
    bool m_running = false;
    QByteArray m_buffer;
};
```

### Java 代码风格

- 遵循 Google Java Style Guide
- 使用 4 空格缩进
- 类名使用 PascalCase
- 方法和变量使用 camelCase

### 注释规范

- 公共 API 必须有文档注释
- 复杂逻辑需要添加解释性注释
- 使用中文或英文注释均可

## 提交规范

使用 [Conventional Commits](https://www.conventionalcommits.org/) 规范：

```
<type>(<scope>): <subject>

<body>

<footer>
```

### Type 类型

| 类型 | 说明 |
|------|------|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `docs` | 文档更新 |
| `style` | 代码格式（不影响功能） |
| `refactor` | 重构（不是新功能或修复） |
| `perf` | 性能优化 |
| `test` | 测试相关 |
| `chore` | 构建/工具变更 |

### 示例

```
feat(transport): add KCP protocol support

- Implement KCP client and server
- Add configuration options for KCP
- Update documentation

Closes #123
```

## Pull Request 流程

1. **创建 PR 前**
   - 确保代码能够编译通过
   - 运行测试确保没有破坏现有功能
   - 更新相关文档

2. **PR 描述**
   - 清晰描述更改内容
   - 关联相关 Issue
   - 如有 UI 变更，附上截图

3. **代码审查**
   - 响应审查意见
   - 必要时进行修改
   - 保持讨论友善

4. **合并**
   - 通过审查后由维护者合并
   - 使用 Squash and Merge 保持历史整洁

## 🎉 感谢

感谢所有贡献者的付出！每一个贡献，无论大小，都对项目有重要意义。
