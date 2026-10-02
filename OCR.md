# Flameshot OCR —— 给 Flameshot 加上微信/QQ 截图式的 OCR

这是 [Flameshot](https://github.com/flameshot-org/flameshot) 的一个功能分支（基于上游
`1837c8a4`，即 Debian `13.3.0+git20251204` 快照），新增了一键 OCR：

- 截图工具栏新增 **OCR 按钮**（快捷键 `O`），点击后对**当前选区**（未选区则整屏）运行 OCR；
- 识别结果以 **微信/QQ 截图风格的面板** 显示在选区旁（右侧放不下自动翻到左侧）；
- 面板内文字**可直接选中**，点「复制文字」一键进剪贴板；
- 识别期间截图界面保持打开，可继续标注、重新框选、再次 OCR；
- OCR 引擎通过 `ocrCommand` 配置项可替换，默认使用本地 tesseract（零额外依赖）。

## 使用

1. 截图（`flameshot gui`），框选要识别的文字区域；
2. 点击工具栏的 OCR 按钮（放大镜+文本图标，或按 `O`）；
3. 选区旁出现文字面板，选中文字或点「复制文字」即可。

## 配置

OCR 引擎默认为：

```
tesseract %i stdout -l chi_sim+eng --psm 6
```

`%i` 是临时图片路径。想换成其他本地引擎（如 RapidOCR/PaddleOCR 的命令行封装），
编辑 `~/.config/flameshot/flameshot.ini`：

```ini
[General]
ocrCommand=rapidocr %i
```

需要 `tesseract-ocr` 及语言包：

```bash
sudo apt install tesseract-ocr tesseract-ocr-chi-sim tesseract-ocr-eng
```

### flameshot-ocr 新增配置项（均在 `[General]` 段）

| 键 | 默认 | 说明 |
|---|---|---|
| `ocrCommand` | `tesseract %i stdout -l chi_sim+eng --psm 6` | OCR 引擎命令，`%i`=临时图片 |
| `colorPickFormat` | `hex` | 取色复制格式：`hex`(#RRGGBB) / `rgb`(r, g, b) |
| `shapeFill` | `false` | 矩形/椭圆填充开关（false=只显示边框） |
| `resizeSensitivity` | `50` | 缩放增益百分比（1-100）；本机设为 20（比鼠标慢 5 倍） |
| `pinShowToolbar` | `false` | 钉图快捷工具条默认显隐（钉图右键菜单可开关并记忆） |

### 调试/验证钩子（仅环境变量门控，平时零影响）

- `FLAMESHOT_OCR_SELFTEST=1`：运行 25 项状态机自测（合成鼠标/滚轮事件驱动真实
  处理器：绘制/选中/框外缩放/内部拖动/自适应增益/橡皮擦包围盒删除/钉图画矩形/
  清空/撤销/确认框），完毕后优雅退出（供 LeakSanitizer 出报告）
- `FLAMESHOT_OCR_AUTOTEST=1|2`：唤出截图后自动跑整屏 OCR（2=先设半屏选区）


## 构建与安装

```bash
# 注意：必须指定 CMAKE_INSTALL_PREFIX=/usr，否则 CPack 生成的 deb 里
# desktop 文件的 Exec 会指向构建默认前缀 /usr/local/bin（文件不存在），
# 导致桌面快捷键（如 F1）唤出失败——本机曾因此踩坑
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
cpack --config build/CPackConfig.cmake -G DEB   # 可选：生成 deb
```

## 与上游的差异（OCR 补丁清单）

| 文件 | 改动 |
|---|---|
| `src/tools/ocr/ocrtool.{h,cpp}` | 新增：OCR 工具（点击发出 `REQ_OCR`，不关闭截图界面） |
| `src/tools/pin/pinannotator.{h,cpp}` | 新增：钉图标注层（画笔/荧光笔/箭头/矩形/椭圆/直线，8 色 3 粗细，Ctrl+Z 撤销；矢量点存于 m_pixmap 坐标系，跟随旋转/缩放） |
| `src/widgets/capture/ocrpanel.{h,cpp}` | 新增：QQ 风格 OCR 结果面板（可选文字 + 复制按钮 + 状态栏；支持拖动/边缘缩放/最小化/关闭；可作顶层窗口伴随钉图） |
| `src/utils/ocrhelper.{h,cpp}` | 新增：异步 OCR 公共管线（钉图使用；截图界面预留迁移） |
| `src/tools/pin/pinwidget.{h,cpp}` | 右键菜单新增 OCR 与「标注」子菜单；复制/保存合成标注；旋转/缩放同步标注层 |
| `data/img/material/{black,white}/ocr.svg` | 新增：OCR 按钮图标（取景框 + 文本行 + 放大镜） |
| `src/tools/capturetool.h` | 新增 `TYPE_OCR = 25`、`REQ_OCR` |
| `src/tools/toolfactory.cpp` | 注册 `OcrTool` |
| `src/widgets/capture/capturetoolbutton.cpp` | 按钮枚举表/排序表加入 OCR |
| `src/utils/confighandler.{h,cpp}` | 新增 `ocrCommand` 配置项与 `TYPE_OCR` 快捷键（默认 `O`） |
| `src/widgets/capture/capturewidget.{h,cpp}` | OCR 执行逻辑：选区裁剪（含 DPI 缩放换算）→ 临时 PNG → `QProcess` 异步调用引擎 → 面板展示；新的鼠标按下/切换工具时隐藏面板 |
| `src/tools/CMakeLists.txt`、`src/widgets/capture/CMakeLists.txt` | 加入新源文件 |

辅助调试：环境变量 `FLAMESHOT_OCR_AUTOTEST=1` 时，截图界面打开约 1.2 秒后自动对
整屏跑一次 OCR（用于无头验证整条链路）。

## 代码审查修复记录（2026-10-02，DeepSeek/千问双报告）

两份外部审查报告指出的问题及修复方式（全部已落地并以 40 项运行时断言 +
ASan/LSan + 临时文件残留检查验证）：

| 审查问题 | 修复 |
|---|---|
| **[高] `OcrHelper::run` 泄漏**：QProcess/QTemporaryFile 以调用方为父对象，`finished`/`errorOccurred` 回调后从不销毁——钉图每点一次 OCR 就累积一对对象和临时文件 | 每条结束路径（成功/失败/FailedToStart/参数错误）均 `deleteLater()`，任务结束立即回收；验证：OCR 后 `/tmp/flameshot-ocr-*` 零残留 |
| **[高] 主线程阻塞**：`CaptureWidget::runOcr` 里 `kill()` + `waitForFinished(3000)` 同步等待，最长冻结 UI 3 秒 | 截图界面整段内联管道删除，改走 `OcrHelper` 公共管线（纯异步，无任何 wait），重复触发用世代计数器 `m_ocrGeneration` 丢弃过期结果（钉图同样加固） |
| **[审查未发现的衍生崩溃]**：OCR 进行中关闭钉图 → `~QProcess` 析构内 kill+waitForFinished 同步发出 `finished` → 回调触摸已销毁的面板 → 段错误 | `OcrHelper` 两个连接改 `Qt::QueuedConnection`：析构期发出的信号被 Qt 事件清除机制吞掉，回调只经事件循环投递到存活对象；gdb 复现→修复→ASan 确认无 use-after-free |
| **[中] 临时文件写入**：保存后未 flush 即启动外部进程，理论上有数据滞留缓冲区的风险 | `save()` 后显式 `flush()`；文件名加入进程 PID 便于多实例排查 |
| **[中] 正则重复编译**：每次识别重新编译 `\n{3,}` | 提取为 `normalizeOcrText()` 内 `static const QRegularExpression`（两处调用点合一） |
| **[中] 整屏 `toImage()` 深拷贝**：先整屏转 QImage 再裁剪，4K 屏浪费数十 MB | 先 `QPixmap::copy(deviceRect)` 裁小图再 `toImage()`；分数缩放下坐标用 `qRound` 显式取整 |
| **[中] 未定义行为死代码**：`src/utils/waylandutils.cpp` 非 void 函数无 return | 文件未在任何 CMakeLists 注册（本就未编译），直接删除 |
| **[低] 错误信息不统一 / 语言包缺失提示**：FailedToStart 报错硬编码 "tesseract"，与实际引擎无关；缺语言包时 stderr 天书 | 统一 `formatOcrError()`（`OCR <程序>: <错误>` 格式）；stderr 命中语言包特征时追加 `apt install tesseract-ocr-chi-sim` 安装提示 |

OcrPanel 内部资源（审查存疑项）：面板无图片缓存，`showLoading`/`showText` 均
`clear()` 旧文本，无累积——审查报告的疑虑实测排除。

## 修复：全局快捷键唤起时光标旁出现跳动的蓝色 KDE 齿轮（2026-10-02 终版）

**现象**：按 F1 唤出截图时，鼠标旁出现持续跳动的蓝色 KDE 齿轮图标，直到截图
界面关闭。

**根因**（读 kglobalacceld 6.3.6 / KIO 6.13 / qtwayland 6.8 / kwin 6.3.6 源码
+ 实测定位）：F1 原先绑定在 kglobalshortcutsrc 的 services `_launch` 上，按键
时 kwin 内嵌的 kglobalaccel 经 `KIO::ApplicationLauncherJob` 启动新进程，
`KProcessRunner` 会向 kwin 申请 xdg-activation token——**该请求以 kwin 自己
的 appId（org.kde.kwin）发出**，kwin 端 `XdgActivationV1Integration::requestToken`
按该 appId 找桌面文件读 `StartupNotify`（默认 true）→ 显示启动反馈，图标正是
kwin 的蓝色齿轮。截图遮罩窗口不抢焦点，token 无人消费，反馈一直挂到超时。
（曾尝试在 `CaptureWidget::showEvent` 里显式 `requestActivate()` 消费 token，
实测仅消费不取消反馈，故改用下面的釜底抽薪方案。）

**终版修复——F1 改走「kwin 脚本 + DBus 直调」，不再启动任何进程**：

1. `FlameshotDBusAdapter::captureGui()`（新增 DBus 方法）：在常驻 daemon 进程
   内直接打开截图界面。
2. `FlameshotDaemon::ensureF1ShortcutScript()`（自愈式）：daemon 启动时自动
   写出并加载 kwin 脚本 `~/.local/share/flameshot-ocr/f1-dbus-launch.js`，
   脚本 `registerShortcut` 把 F1 注册为 `callDBus → org.flameshot.Flameshot.
   captureGui`。
3. 键归属迁移（一次性，已在本机执行并持久化）：用 kglobalaccel 标准方法
   `setShortcutKeys` 先清空 services `_launch` 的 F1、再赋给 kwin 组件的
   `FlameshotGuiF1`（注意：kglobalaccel 6.3 对冲突键是「跳过」而非「抢夺」，
   必须先释放后赋值；`setShortcutKeys` 是所有 KDE 应用运行时改键的标准
   方法，与曾导致 kwin 崩溃的已废弃 `setForeignShortcut` 无关）。

效果：F1 按下 → kwin 脚本 → DBus → daemon 开界面。无进程启动、无 token
申请、无启动反馈，图标在机制上不可能出现；且省去进程冷启动，唤出更快。
登录自愈链：autostart 启动 daemon → daemon 加载 kwin 脚本 → 脚本按
kglobalshortcutrc `[kwin] FlameshotGuiF1=F1` 恢复绑定。

**补充（实测收尾）**：改为 DBus 直调后用户仍见齿轮跳动约 2-3 秒（此时
F1 已零进程启动，托盘点击同样出现）——齿轮与「截图窗口打开」绑定：
窗口 show 时 Qt 的 requestActivate 回退分支会带输入 serial 向 kwin 申领
新的 activation token，kwin 铸币即触发内置 StartupFeedback 效果（弹跳
齿轮）。该效果为 internal 编译项，运行时无法卸载，且其 klaunchrc 热加载
与全局 reconfigure 实测均不刷新（`supportInformation` 始终 type:1），
仅在 kwin 启动时读取配置。因此写入

```ini
# ~/.config/klaunchrc
[FeedbackStyle]
BusyCursor=false
```

后**需要注销重登一次**（无需重启整机）才生效——kwin 启动时读到
BusyCursor=false → `m_type = NoFeedback`（startupfeedback.cpp:153），
此后无论谁铸币都不再绘制齿轮。等价于 系统设置 → 通知 → 应用启动反馈
设为「无」。

## 许可

与上游一致：GPL-3.0-or-later。

## 修复：注销重登后托盘/图标失效（2026-10-02）

重登后 `app-Flameshot@autostart.service` 失败退出（exit 2）：历史遗留的
`/usr/local/bin/flameshot` 包装脚本会无条件追加 `gui` 参数，把登录自启的
daemon 模式劫持成截图 GUI 模式并立即失败。修复：删除该包装脚本（其兼容
使命已被 kwin 脚本方案取代），`~/.config/autostart/Flameshot.desktop` 的
Exec 改为绝对路径 `/usr/bin/flameshot`。同轮验证：重登后
`startupfeedback` 效果 `type: 0`（NoFeedback）——齿轮渲染器已随
`klaunchrc BusyCursor=false` 生效关闭。
