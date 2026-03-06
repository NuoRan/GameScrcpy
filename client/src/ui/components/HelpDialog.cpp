#include "HelpDialog.h"

// =========================================================
//  1. 快速入门
// =========================================================
QWidget* HelpDialog::buildQuickStart() {
    return page(
        "<h1>快速入门</h1>"
        "<p>GameScrcpy 是一款安卓设备投屏与游戏控制工具，支持键鼠映射、脚本自动化、"
        "图像识别、自定义选区等功能。</p>"

        "<h2>1.1 概述</h2>"
        "<ul>"
        "<li><b>首页</b> — 设备连接、在线设备列表、快速投屏入口</li>"
        "<li><b>投屏窗口</b> — 实时画面显示、键鼠映射、脚本控制</li>"
        "<li><b>脚本编辑器</b> — JavaScript 自动化脚本编写与调试</li>"
        "<li><b>选区管理器</b> — 可视化创建选区、按钮、滑动实体</li>"
        "<li><b>设置</b> — 分辨率、码率、编码器等参数配置</li>"
        "<li><b>终端</b> — 实时日志与 ADB 命令行</li>"
        "</ul>"

        "<h2>1.2 基本工作流程</h2>"
        "<div class='card'>"
        "<ol>"
        "<li><b>连接设备</b> — 通过 USB 数据线或 WiFi 连接安卓设备</li>"
        "<li><b>启动投屏</b> — 双击设备列表中的设备即可打开投屏窗口</li>"
        "<li><b>加载键位</b> — 在投屏窗口工具栏中选择 .json 键位文件并启动映射</li>"
        "<li><b>编写脚本</b> — 使用脚本编辑器编写 JavaScript 自动化逻辑</li>"
        "<li><b>获取坐标</b> — 通过选区管理器可视化获取屏幕位置和区域</li>"
        "</ol>"
        "</div>"

        "<h2>1.3 设备要求</h2>"
        "<h3>硬件要求</h3>"
        "<ul>"
        "<li>安卓 5.0 (API 21) 及以上版本</li>"
        "<li>USB 数据线（非充电线）用于首次连接</li>"
        "<li>同一局域网环境（WiFi 连接时需要）</li>"
        "</ul>"
        "<h3>设备设置</h3>"
        "<ol>"
        "<li>打开手机 <b>设置 → 关于手机</b>，连续点击 <b>版本号</b> 7 次开启开发者模式</li>"
        "<li>进入 <b>设置 → 开发者选项</b>，开启 <b>USB 调试</b></li>"
        "<li>首次连接电脑时，在手机上确认 <b>允许 USB 调试</b> 弹窗</li>"
        "</ol>"
        "<div class='tip'><b>提示：</b>部分品牌手机可能需要额外开启「USB 调试(安全设置)」或「模拟点击」权限。</div>"

        "<h2>1.4 界面导航</h2>"
        "<table>"
        "<tr><th>页面</th><th>功能</th></tr>"
        "<tr><td>首页</td><td>设备列表、USB/WiFi 连接、快速投屏</td></tr>"
        "<tr><td>设置</td><td>投屏参数配置</td></tr>"
        "<tr><td>终端</td><td>运行日志查看、ADB 命令执行</td></tr>"
        "<tr><td>帮助</td><td>综合帮助文档（即本页面）</td></tr>"
        "</table>"
    );
}

// =========================================================
//  2. 连接设备
// =========================================================
QWidget* HelpDialog::buildConnection() {
    return page(
        "<h1>连接设备</h1>"

        "<h2>2.1 USB 有线连接</h2>"
        "<h3>准备工作</h3>"
        "<ul>"
        "<li>确保手机已开启 USB 调试模式</li>"
        "<li>使用 <b>数据线</b>（非充电线）连接手机到电脑</li>"
        "<li>首次连接时在手机上确认 USB 调试授权弹窗</li>"
        "</ul>"
        "<h3>连接步骤</h3>"
        "<ol>"
        "<li>插入 USB 数据线</li>"
        "<li>在首页点击 <b>USB 连接</b> 按钮</li>"
        "<li>设备自动出现在设备列表中</li>"
        "<li>双击设备即可启动投屏</li>"
        "</ol>"
        "<div class='tip'><b>提示：</b>如果设备未出现，尝试更换数据线或检查驱动安装。"
        "可在终端执行 <code>adb devices</code> 确认连接状态。</div>"

        "<h2>2.2 WiFi 无线连接</h2>"
        "<h3>方式一：自动切换（推荐）</h3>"
        "<ol>"
        "<li>先通过 USB 连接设备（建立信任关系）</li>"
        "<li>在设备列表中选中该设备</li>"
        "<li>点击首页的 <b>WiFi 连接</b> 按钮</li>"
        "<li>系统自动执行 <code>adb tcpip 5555</code></li>"
        "<li>自动获取设备 IP 并连接</li>"
        "<li>连接成功后可安全拔掉 USB 数据线</li>"
        "</ol>"

        "<h3>方式二：手动 IP 连接</h3>"
        "<ol>"
        "<li>确保手机和电脑在同一局域网</li>"
        "<li>在手机设置中查看 IP 地址</li>"
        "<li>当没有 USB 设备在线时点击 WiFi 连接</li>"
        "<li>输入 IP 地址（如 <code>192.168.1.100</code>）</li>"
        "</ol>"

        "<div class='warn'><b>注意：</b>"
        "<ul>"
        "<li>WiFi 连接需手机和电脑在同一局域网内</li>"
        "<li>部分路由器有 AP 隔离功能需关闭</li>"
        "<li>手机锁屏可能导致 WiFi 连接断开</li>"
        "</ul></div>"

        "<h2>2.3 多设备连接</h2>"
        "<p>支持同时连接多台安卓设备，双击不同设备可分别开启投屏窗口。</p>"

        "<h2>2.4 故障排除</h2>"
        "<table>"
        "<tr><th>问题</th><th>解决方案</th></tr>"
        "<tr><td>设备未出现</td><td>检查 USB 调试；更换数据线；安装驱动</td></tr>"
        "<tr><td>连接后断开</td><td>确认授权弹窗；取消「仅充电」模式</td></tr>"
        "<tr><td>WiFi 连接失败</td><td>确认同一局域网；检查 AP 隔离</td></tr>"
        "<tr><td>unauthorized</td><td>撤销 USB 调试授权后重新插拔</td></tr>"
        "</table>"
    );
}

// =========================================================
//  3. 投屏窗口
// =========================================================
QWidget* HelpDialog::buildVideoWindow() {
    return page(
        "<h1>投屏窗口</h1>"
        "<p>双击设备后打开投屏窗口，实时显示手机画面。</p>"

        "<h2>3.1 基本交互</h2>"
        "<h3>鼠标操作</h3>"
        "<table>"
        "<tr><th>操作</th><th>映射效果</th></tr>"
        "<tr><td>左键点击</td><td>模拟手指触摸</td></tr>"
        "<tr><td>左键拖动</td><td>模拟手指滑动</td></tr>"
        "<tr><td>鼠标滚轮</td><td>模拟上下滚动</td></tr>"
        "<tr><td>右键点击</td><td>打开功能菜单</td></tr>"
        "</table>"
        "<h3>窗口管理</h3>"
        "<ul>"
        "<li><b>双击标题栏</b> — 全屏/还原</li>"
        "<li><b>拖动边缘</b> — 调整窗口大小（保持画面比例）</li>"
        "<li><b>关闭窗口</b> — 可选择最小化到系统托盘</li>"
        "</ul>"

        "<h2>3.2 工具栏</h2>"
        "<h3>键位映射区</h3>"
        "<ul>"
        "<li><b>键位文件选择</b> — 下拉框选择 .json 键位配置</li>"
        "<li><b>开始/停止</b> — 启用或禁用键鼠映射</li>"
        "<li><b>编辑模式</b> — 进入可视化键位编辑界面</li>"
        "</ul>"
        "<h3>脚本区</h3>"
        "<ul>"
        "<li><b>脚本选择</b> — 选择 .js 脚本文件</li>"
        "<li><b>运行/停止脚本</b> — 控制脚本的执行</li>"
        "<li><b>打开编辑器</b> — 打开脚本编辑器窗口</li>"
        "</ul>"

        "<h2>3.3 右键菜单</h2>"
        "<table>"
        "<tr><th>选项</th><th>功能</th></tr>"
        "<tr><td>全屏模式</td><td>切换全屏</td></tr>"
        "<tr><td>窗口置顶</td><td>保持最前</td></tr>"
        "<tr><td>截图保存</td><td>保存当前画面</td></tr>"
        "<tr><td>键位覆盖</td><td>显示/隐藏键位标签</td></tr>"
        "</table>"

        "<h2>3.4 键位编辑模式</h2>"
        "<ul>"
        "<li><b>拖动</b> — 调整键位触摸位置</li>"
        "<li><b>双击</b> — 进入按键录入模式</li>"
        "<li><b>Delete</b> — 删除选中键位</li>"
        "<li><b>Ctrl+Z / Ctrl+Y</b> — 撤销/重做</li>"
        "</ul>"
        "<div class='tip'><b>提示：</b>退出编辑模式时配置自动保存并应用。</div>"
    );
}

// =========================================================
//  4. 键鼠映射
// =========================================================
QWidget* HelpDialog::buildKeyMap() {
    return page(
        "<h1>键鼠映射</h1>"
        "<p>通过 JSON 配置文件将键盘鼠标操作映射为安卓触摸事件。</p>"

        "<h2>4.1 映射类型</h2>"
        "<h3>4.1.1 点击映射 (click)</h3>"
        "<p>按键 → 点击屏幕指定位置。用于技能按钮等。</p>"
        "<table>"
        "<tr><th>参数</th><th>说明</th><th>示例</th></tr>"
        "<tr><td><code>key</code></td><td>触发按键</td><td><code>\"Q\"</code></td></tr>"
        "<tr><td><code>pos</code></td><td>触摸位置</td><td><code>{\"x\":0.2,\"y\":0.8}</code></td></tr>"
        "</table>"

        "<h3>4.1.2 方向轮盘 (steerWheel)</h3>"
        "<p>WASD → 虚拟摇杆。用于角色移动。</p>"
        "<table>"
        "<tr><th>参数</th><th>说明</th></tr>"
        "<tr><td><code>centerPos</code></td><td>摇杆中心位置</td></tr>"
        "<tr><td><code>key_up/down/left/right</code></td><td>方向按键绑定</td></tr>"
        "</table>"

        "<h3>4.1.3 视角拖拽 (mouseMoveMap)</h3>"
        "<p>鼠标移动 → 屏幕滑动。用于 FPS 视角。</p>"
        "<table>"
        "<tr><th>参数</th><th>说明</th></tr>"
        "<tr><td><code>startPos</code></td><td>触摸起始位置</td></tr>"
        "<tr><td><code>speedRatioX/Y</code></td><td>灵敏度系数</td></tr>"
        "</table>"

        "<h3>4.1.4 宏脚本 (macro)</h3>"
        "<p>按键 → 执行预定义脚本函数。用于连招等。</p>"

        "<h2>4.2 JSON 配置示例</h2>"
        "<pre>"
        "{\n"
        "  \"switchKey\": \"~\",\n"
        "  \"mouseMoveMap\": {\n"
        "    \"startPos\": {\"x\":0.6,\"y\":0.5},\n"
        "    \"speedRatioX\": 3.0,\n"
        "    \"speedRatioY\": 3.0\n"
        "  },\n"
        "  \"keyMapNodes\": [\n"
        "    {\"type\":\"click\",\"key\":\"Q\",\"pos\":{\"x\":0.2,\"y\":0.8}},\n"
        "    {\"type\":\"steerWheel\",\"centerPos\":{\"x\":0.18,\"y\":0.72},\n"
        "     \"key_up\":\"W\",\"key_down\":\"S\",\"key_left\":\"A\",\"key_right\":\"D\"}\n"
        "  ]\n"
        "}"
        "</pre>"

        "<h2>4.3 坐标系统</h2>"
        "<div class='card'>"
        "<p>所有坐标使用 <b>0.0~1.0 相对坐标</b>：</p>"
        "<ul>"
        "<li><code>(0.0, 0.0)</code> — 左上角</li>"
        "<li><code>(1.0, 1.0)</code> — 右下角</li>"
        "<li><code>(0.5, 0.5)</code> — 正中心</li>"
        "</ul>"
        "</div>"

        "<h2>4.4 使用步骤</h2>"
        "<ol>"
        "<li>将 .json 文件放入 <code>keymap/</code> 文件夹</li>"
        "<li>在投屏窗口下拉框中选择</li>"
        "<li>点击「开始」启用映射</li>"
        "<li>按 <code>~</code> 键切换鼠标捕获模式</li>"
        "</ol>"
    );
}

// =========================================================
//  5. 脚本编辑器
// =========================================================
QWidget* HelpDialog::buildScriptEditor() {
    return page(
        "<h1>脚本编辑器</h1>"
        "<p>编写 JavaScript 自动化脚本，通过 <code>mapi</code> 对象与设备交互。</p>"

        "<h2>5.1 编辑器功能</h2>"
        "<h3>5.1.1 代码编辑</h3>"
        "<ul>"
        "<li><b>语法高亮</b> — 关键字、字符串、数字、注释着色</li>"
        "<li><b>行号显示</b> — 左侧行号方便定位</li>"
        "<li><b>括号匹配</b> — 自动高亮匹配括号</li>"
        "<li><b>自动缩进</b> — 回车保持缩进层级</li>"
        "</ul>"
        "<h3>5.1.2 自动补全</h3>"
        "<p>输入 <code>mapi.</code> 弹出方法列表，选择后插入完整模板。</p>"
        "<h3>5.1.3 文件操作</h3>"
        "<ul>"
        "<li><b>新建</b> — 创建空白脚本</li>"
        "<li><b>打开</b> — 从 scripts/ 目录加载</li>"
        "<li><b>保存</b> — 保存到 scripts/ 目录</li>"
        "<li><b>运行</b> — 立即执行当前脚本</li>"
        "</ul>"

        "<h2>5.2 快捷指令面板</h2>"
        "<p>左侧面板按类别提供代码片段，点击插入到光标位置：</p>"
        "<h3>5.2.1 触摸操作</h3>"
        "<table>"
        "<tr><th>指令</th><th>代码</th><th>说明</th></tr>"
        "<tr><td>click</td><td><code>mapi.click(x,y)</code></td><td>单击</td></tr>"
        "<tr><td>holdpress</td><td><code>mapi.holdpress(x,y)</code></td><td>持续按压</td></tr>"
        "<tr><td>release</td><td><code>mapi.release()</code></td><td>释放触摸点</td></tr>"
        "<tr><td>slide</td><td><code>mapi.slide(...)</code></td><td>滑动操作</td></tr>"
        "<tr><td>pinch</td><td><code>mapi.pinch(...)</code></td><td>双指缩放</td></tr>"
        "</table>"
        "<h3>5.2.2 按键与视角</h3>"
        "<table>"
        "<tr><th>指令</th><th>说明</th></tr>"
        "<tr><td>key</td><td>执行按键宏脚本</td></tr>"
        "<tr><td>resetview</td><td>重置鼠标视角</td></tr>"
        "<tr><td>resetwheel</td><td>重置轮盘状态</td></tr>"
        "<tr><td>shotmode</td><td>切换鼠标模式</td></tr>"
        "</table>"
        "<h3>5.2.3 状态查询</h3>"
        "<table>"
        "<tr><th>指令</th><th>返回值</th></tr>"
        "<tr><td>getmousepos</td><td><code>{x,y}</code></td></tr>"
        "<tr><td>getkeypos</td><td><code>{x,y,valid}</code></td></tr>"
        "<tr><td>getKeyState</td><td><code>0</code> 或 <code>1</code></td></tr>"
        "<tr><td>getbuttonpos</td><td><code>{x,y,valid,name}</code></td></tr>"
        "</table>"
        "<h3>5.2.4 工具函数</h3>"
        "<table>"
        "<tr><th>指令</th><th>说明</th></tr>"
        "<tr><td>sleep</td><td>暂停（可中断）</td></tr>"
        "<tr><td>toast</td><td>浮动提示</td></tr>"
        "<tr><td>log</td><td>输出日志</td></tr>"
        "<tr><td>stop</td><td>停止脚本</td></tr>"
        "</table>"

        "<h2>5.3 获取工具</h2>"
        "<p>点击「获取工具」打开选区管理器，可获取坐标、创建按钮/选区/滑动实体。</p>"
    );
}

// =========================================================
//  6. mapi API 参考
// =========================================================
QWidget* HelpDialog::buildApi() {
    return page(
        "<h1>mapi API 参考</h1>"
        "<p><code>mapi</code> 是脚本中的内置全局对象。</p>"

        "<h2>6.1 触摸操作</h2>"
        "<h3>mapi.click(x, y)</h3>"
        "<p>模拟单击。参数可省略（使用锚点位置）。</p>"
        "<pre>mapi.click(0.5, 0.5);  // 点击中心\nmapi.click();          // 锚点位置</pre>"

        "<h3>mapi.holdpress(x, y)</h3>"
        "<p>按下不松手，配合 <code>release()</code>。</p>"
        "<pre>mapi.holdpress(0.3, 0.7);\nmapi.sleep(2000);\nmapi.release();</pre>"

        "<h3>mapi.release() / releaseAll()</h3>"
        "<p>释放当前按键触摸点 / 释放所有触摸点。</p>"

        "<h3>mapi.slide(x0,y0,x1,y1,ms,steps)</h3>"
        "<p>从起点滑动到终点。</p>"
        "<table>"
        "<tr><th>参数</th><th>说明</th></tr>"
        "<tr><td>x0,y0 / x1,y1</td><td>起/终点坐标</td></tr>"
        "<tr><td>ms</td><td>持续时间</td></tr>"
        "<tr><td>steps</td><td>步数(越多越平滑)</td></tr>"
        "</table>"

        "<h3>mapi.pinch(cx,cy,scale,ms,steps)</h3>"
        "<p>双指缩放。<code>scale>1</code> 放大，<code>&lt;1</code> 缩小。</p>"

        "<hr>"
        "<h2>6.2 按键操作</h2>"
        "<h3>mapi.key(name, ms)</h3>"
        "<p>触发按键宏脚本。</p>"

        "<hr>"
        "<h2>6.3 视角控制</h2>"
        "<table>"
        "<tr><th>方法</th><th>说明</th></tr>"
        "<tr><td><code>mapi.resetview()</code></td><td>重置视角</td></tr>"
        "<tr><td><code>mapi.resetwheel()</code></td><td>重置轮盘</td></tr>"
        "<tr><td><code>mapi.shotmode(v)</code></td><td>true=游戏 false=光标</td></tr>"
        "<tr><td><code>mapi.setRadialParam(u,d,l,r)</code></td><td>设置轮盘偏移系数</td></tr>"
        "<tr><td><code>mapi.setKeyUIPos(key,x,y)</code></td><td>动态更新按键UI位置</td></tr>"
        "</table>"

        "<hr>"
        "<h2>6.4 状态查询</h2>"
        "<table>"
        "<tr><th>方法</th><th>返回值</th><th>说明</th></tr>"
        "<tr><td><code>getmousepos()</code></td><td>{x,y}</td><td>鼠标位置</td></tr>"
        "<tr><td><code>getkeypos(key)</code></td><td>{x,y,valid}</td><td>按键映射位置</td></tr>"
        "<tr><td><code>getKeyState(key)</code></td><td>0 或 1</td><td>按键是否按下</td></tr>"
        "<tr><td><code>getbuttonpos(id)</code></td><td>{x,y,valid,name}</td><td>预定义按钮位置</td></tr>"
        "<tr><td><code>isPress()</code></td><td>bool</td><td>当前是按下还是松开</td></tr>"
        "<tr><td><code>isInterrupted()</code></td><td>bool</td><td>脚本是否被中断</td></tr>"
        "</table>"
        "<pre>while (!mapi.isInterrupted()) {\n  mapi.click(0.5,0.5);\n  mapi.sleep(1000);\n}</pre>"

        "<hr>"
        "<h2>6.5 工具函数</h2>"
        "<table>"
        "<tr><th>方法</th><th>说明</th></tr>"
        "<tr><td><code>mapi.sleep(ms)</code></td><td>暂停（可被中断）</td></tr>"
        "<tr><td><code>mapi.toast(msg,ms)</code></td><td>浮动提示</td></tr>"
        "<tr><td><code>mapi.log(msg)</code></td><td>输出日志</td></tr>"
        "<tr><td><code>mapi.stop()</code></td><td>停止脚本</td></tr>"
        "<tr><td><code>mapi.swipeById(id,ms,steps)</code></td><td>执行预定义滑动</td></tr>"
        "</table>"

        "<hr>"
        "<h2>6.6 全局状态</h2>"
        "<table>"
        "<tr><th>方法</th><th>说明</th></tr>"
        "<tr><td><code>setGlobal(key,val)</code></td><td>设置全局变量(线程安全)</td></tr>"
        "<tr><td><code>getGlobal(key)</code></td><td>获取全局变量</td></tr>"
        "<tr><td><code>loadModule(file)</code></td><td>从 scripts/ 加载模块</td></tr>"
        "</table>"
    );
}

// =========================================================
//  7. 图像识别
// =========================================================
QWidget* HelpDialog::buildImageMatch() {
    return page(
        "<h1>图像识别</h1>"
        "<p>基于 OpenCV 模板匹配实现屏幕内容检测。</p>"

        "<h2>7.1 概述</h2>"
        "<p>图像识别功能可以截取当前画面与模板图片进行匹配，"
        "返回匹配位置和置信度，用于自动化脚本中的条件判断。</p>"

        "<h2>7.2 脚本调用方式</h2>"
        "<h3>mapi.matchTemplate(templatePath, threshold)</h3>"
        "<table>"
        "<tr><th>参数</th><th>类型</th><th>说明</th></tr>"
        "<tr><td>templatePath</td><td>string</td><td>模板图片路径(相对于 templates/ 目录)</td></tr>"
        "<tr><td>threshold</td><td>number</td><td>匹配阈值 0.0~1.0, 推荐 0.8+</td></tr>"
        "</table>"
        "<pre>var r = mapi.matchTemplate(\"btn_ok.png\", 0.85);\n"
        "if (r.found) {\n"
        "    mapi.click(r.x, r.y);  // 点击匹配位置\n"
        "}</pre>"

        "<h2>7.3 返回值</h2>"
        "<table>"
        "<tr><th>属性</th><th>类型</th><th>说明</th></tr>"
        "<tr><td>found</td><td>bool</td><td>是否匹配成功</td></tr>"
        "<tr><td>x, y</td><td>number</td><td>匹配中心坐标(相对值 0~1)</td></tr>"
        "<tr><td>confidence</td><td>number</td><td>匹配置信度 0~1</td></tr>"
        "</table>"

        "<h2>7.4 最佳实践</h2>"
        "<ul>"
        "<li>模板图应裁剪到最小必要区域，减少误匹配</li>"
        "<li>阈值建议设为 0.8~0.9 之间</li>"
        "<li>避免使用包含动态内容的区域作为模板</li>"
        "<li>分辨率变化可能导致匹配失败，建议固定投屏分辨率</li>"
        "</ul>"
        "<div class='tip'><b>提示：</b>可在选区管理器中截取选区并保存为模板图片。</div>"
    );
}

// =========================================================
//  8. 自定义选区
// =========================================================
QWidget* HelpDialog::buildSelection() {
    return page(
        "<h1>自定义选区</h1>"
        "<p>选区管理器用于可视化创建矩形选区、按钮、滑动实体。</p>"

        "<h2>8.1 打开方式</h2>"
        "<ul>"
        "<li>投屏窗口工具栏 → 获取工具</li>"
        "<li>脚本编辑器 → 获取工具</li>"
        "</ul>"

        "<h2>8.2 实体类型</h2>"
        "<h3>8.2.1 选区 (Region)</h3>"
        "<p>矩形区域，用于图像识别的模板截取范围。</p>"
        "<ul>"
        "<li>拖拽四角/边缘调整大小</li>"
        "<li>拖拽内部移动位置</li>"
        "<li>顶部显示坐标信息</li>"
        "</ul>"
        "<h3>8.2.2 按钮 (Button)</h3>"
        "<p>标记的圆形触摸点位，在脚本中通过 ID/名称引用。</p>"
        "<ul>"
        "<li>点击创建后拖拽移动</li>"
        "<li>可设置名称标签</li>"
        "<li><code>mapi.getbuttonpos(id)</code> 获取坐标</li>"
        "</ul>"
        "<h3>8.2.3 滑动 (Swipe)</h3>"
        "<p>由起点终点组成的滑动路径。</p>"
        "<ul>"
        "<li>拖拽端点调整方向和距离</li>"
        "<li><code>mapi.swipeById(id, ms, steps)</code> 执行</li>"
        "</ul>"

        "<h2>8.3 工具栏操作</h2>"
        "<table>"
        "<tr><th>按钮</th><th>功能</th></tr>"
        "<tr><td>新建选区</td><td>添加矩形区域</td></tr>"
        "<tr><td>新建按钮</td><td>添加触摸点标记</td></tr>"
        "<tr><td>新建滑动</td><td>添加滑动路径</td></tr>"
        "<tr><td>删除</td><td>删除选中实体</td></tr>"
        "<tr><td>导出</td><td>保存选区配置</td></tr>"
        "</table>"

        "<h2>8.4 图层管理</h2>"
        "<p>左下角图层按钮控制各类实体的显示/隐藏：</p>"
        "<ul>"
        "<li><b>选区层</b> — 显示/隐藏矩形选区</li>"
        "<li><b>按钮层</b> — 显示/隐藏按钮标记</li>"
        "<li><b>滑动层</b> — 显示/隐藏滑动路径</li>"
        "</ul>"
    );
}

// =========================================================
//  9. 设置参数
// =========================================================
QWidget* HelpDialog::buildSettings() {
    return page(
        "<h1>设置参数</h1>"

        "<h2>9.1 投屏设置</h2>"
        "<h3>画面质量</h3>"
        "<table>"
        "<tr><th>参数</th><th>说明</th><th>默认值</th></tr>"
        "<tr><td>最大分辨率</td><td>投屏画面短边像素</td><td>1280</td></tr>"
        "<tr><td>比特率</td><td>编码码率 (Mbps)</td><td>8</td></tr>"
        "<tr><td>帧率限制</td><td>最大 FPS</td><td>60</td></tr>"
        "</table>"

        "<h3>音视频编码</h3>"
        "<table>"
        "<tr><th>参数</th><th>说明</th></tr>"
        "<tr><td>视频编码器</td><td>H264 / H265 / 设备默认</td></tr>"
        "<tr><td>音频转发</td><td>是否转发设备音频</td></tr>"
        "<tr><td>显示方向</td><td>自动 / 强制横屏 / 强制竖屏</td></tr>"
        "</table>"

        "<h2>9.2 连接设置</h2>"
        "<table>"
        "<tr><th>参数</th><th>说明</th></tr>"
        "<tr><td>窗口置顶</td><td>投屏窗口保持最前</td></tr>"
        "<tr><td>显示 FPS</td><td>在窗口标题显示帧率</td></tr>"
        "<tr><td>关闭时最小化</td><td>窗口关闭后最小化到托盘</td></tr>"
        "</table>"

        "<h2>9.3 配置文件</h2>"
        "<p>配置保存在 <code>config/</code> 目录下：</p>"
        "<ul>"
        "<li><code>config.ini</code> — 程序主配置</li>"
        "<li><code>userdata.ini</code> — 用户数据、设备信息</li>"
        "</ul>"
        "<div class='tip'><b>提示：</b>修改设置后需要重新启动投屏才能生效。</div>"
    );
}

// =========================================================
//  10. 终端 / ADB
// =========================================================
QWidget* HelpDialog::buildTerminal() {
    return page(
        "<h1>终端 / ADB</h1>"

        "<h2>10.1 日志面板</h2>"
        "<p>终端页面显示运行时日志：</p>"
        "<ul>"
        "<li><b>连接日志</b> — 设备连接/断开事件</li>"
        "<li><b>投屏日志</b> — 启动/停止信息</li>"
        "<li><b>脚本日志</b> — <code>mapi.log()</code> 输出</li>"
        "<li><b>错误信息</b> — 连接失败、兼容性问题等</li>"
        "</ul>"

        "<h2>10.2 常用 ADB 命令</h2>"
        "<table>"
        "<tr><th>命令</th><th>说明</th></tr>"
        "<tr><td><code>adb devices</code></td><td>列出已连接设备</td></tr>"
        "<tr><td><code>adb tcpip 5555</code></td><td>启用 WiFi 调试</td></tr>"
        "<tr><td><code>adb connect IP:5555</code></td><td>WiFi 连接设备</td></tr>"
        "<tr><td><code>adb disconnect</code></td><td>断开所有 WiFi 连接</td></tr>"
        "<tr><td><code>adb shell input tap x y</code></td><td>模拟屏幕点击</td></tr>"
        "<tr><td><code>adb shell screencap -p /sdcard/s.png</code></td><td>截图</td></tr>"
        "<tr><td><code>adb push local remote</code></td><td>推送文件到设备</td></tr>"
        "<tr><td><code>adb pull remote local</code></td><td>从设备拉取文件</td></tr>"
        "</table>"

        "<h2>10.3 故障诊断</h2>"
        "<p>遇到问题时，查看终端日志是首要排查手段：</p>"
        "<ul>"
        "<li><b>server error</b> — scrcpy-server 启动失败，检查安卓版本</li>"
        "<li><b>connection refused</b> — 设备未授权或端口占用</li>"
        "<li><b>EOF</b> — 连接中断，检查 USB 线或网络稳定性</li>"
        "</ul>"
    );
}

// =========================================================
//  11. 快捷键
// =========================================================
QWidget* HelpDialog::buildShortcuts() {
    return page(
        "<h1>快捷键</h1>"

        "<h2>11.1 全局快捷键</h2>"
        "<table>"
        "<tr><th>快捷键</th><th>功能</th></tr>"
        "<tr><td><code>Ctrl+Q</code></td><td>退出程序</td></tr>"
        "</table>"

        "<h2>11.2 投屏窗口</h2>"
        "<table>"
        "<tr><th>快捷键</th><th>功能</th></tr>"
        "<tr><td><code>~</code> (波浪号)</td><td>切换鼠标捕获/释放</td></tr>"
        "<tr><td><code>F11</code></td><td>全屏切换</td></tr>"
        "<tr><td><code>Esc</code></td><td>退出全屏/退出编辑模式</td></tr>"
        "</table>"

        "<h2>11.3 键位编辑模式</h2>"
        "<table>"
        "<tr><th>快捷键</th><th>功能</th></tr>"
        "<tr><td><code>Ctrl+Z</code></td><td>撤销</td></tr>"
        "<tr><td><code>Ctrl+Y</code></td><td>重做</td></tr>"
        "<tr><td><code>Delete</code></td><td>删除选中键位</td></tr>"
        "<tr><td><code>双击键位</code></td><td>进入按键录入模式</td></tr>"
        "</table>"

        "<h2>11.4 脚本编辑器</h2>"
        "<table>"
        "<tr><th>快捷键</th><th>功能</th></tr>"
        "<tr><td><code>Ctrl+S</code></td><td>保存脚本</td></tr>"
        "<tr><td><code>Ctrl+Enter</code></td><td>运行脚本</td></tr>"
        "<tr><td><code>Ctrl+N</code></td><td>新建脚本</td></tr>"
        "<tr><td><code>Ctrl+O</code></td><td>打开脚本</td></tr>"
        "</table>"

        "<h2>11.5 选区管理器</h2>"
        "<table>"
        "<tr><th>快捷键</th><th>功能</th></tr>"
        "<tr><td><code>Delete</code></td><td>删除选中实体</td></tr>"
        "<tr><td><code>Ctrl+C</code></td><td>复制坐标到剪贴板</td></tr>"
        "</table>"
    );
}

// =========================================================
//  12. 常见问题
// =========================================================
QWidget* HelpDialog::buildFAQ() {
    return page(
        "<h1>常见问题 (FAQ)</h1>"

        "<h2>Q1: 设备连接后没有画面?</h2>"
        "<div class='card'>"
        "<p><b>A:</b></p>"
        "<ol>"
        "<li>确认设备开启了 USB 调试</li>"
        "<li>确认手机上授权了 USB 调试弹窗</li>"
        "<li>尝试更换数据线或 USB 端口</li>"
        "<li>在终端执行 <code>adb devices</code> 检查设备状态</li>"
        "<li>部分设备需开启「USB 调试(安全设置)」</li>"
        "</ol></div>"

        "<h2>Q2: WiFi 连接失败?</h2>"
        "<div class='card'>"
        "<p><b>A:</b> 确认以下条件：</p>"
        "<ul>"
        "<li>手机和电脑在同一局域网</li>"
        "<li>路由器未开启 AP 隔离</li>"
        "<li>先通过 USB 连接一次再切换 WiFi</li>"
        "<li>检查防火墙是否阻止了 ADB 端口</li>"
        "</ul></div>"

        "<h2>Q3: 键鼠映射没有效果?</h2>"
        "<div class='card'>"
        "<p><b>A:</b></p>"
        "<ul>"
        "<li>确认已加载 .json 键位文件并点击「开始」</li>"
        "<li>按 <code>~</code> 键进入鼠标捕获模式</li>"
        "<li>检查 JSON 格式是否正确（终端会报错误信息）</li>"
        "<li>坐标是否在有效范围 (0.0~1.0)</li>"
        "</ul></div>"

        "<h2>Q4: 脚本运行没有反应?</h2>"
        "<div class='card'>"
        "<p><b>A:</b></p>"
        "<ul>"
        "<li>检查终端日志是否有错误信息</li>"
        "<li>确认投屏窗口已启动且正在运行</li>"
        "<li>确认脚本语法正确，没有拼写错误</li>"
        "<li>使用 <code>mapi.log()</code> 输出调试信息</li>"
        "</ul></div>"

        "<h2>Q5: 画面卡顿或延迟?</h2>"
        "<div class='card'>"
        "<p><b>A:</b></p>"
        "<ul>"
        "<li>降低分辨率（如 1280 → 720）</li>"
        "<li>降低码率（如 8Mbps → 4Mbps）</li>"
        "<li>使用 USB 连接（比 WiFi 延迟更低）</li>"
        "<li>尝试切换视频编码器</li>"
        "<li>关闭手机省电模式</li>"
        "</ul></div>"

        "<h2>Q6: 进退编辑模式画面变黑?</h2>"
        "<div class='card'>"
        "<p><b>A:</b> 已修复。如果仍出现此问题，触摸手机屏幕产生新帧即可恢复。"
        "此问题是因为 OpenGL 在无新帧时不重绘导致，当前版本已处理此边界情况。</p>"
        "</div>"

        "<h2>Q7: 选区坐标不准确?</h2>"
        "<div class='card'>"
        "<p><b>A:</b> 选区使用相对坐标 (0.0~1.0)，确保投屏分辨率与游戏分辨率匹配。"
        "不同分辨率可能导致坐标偏移。</p></div>"
    );
}
