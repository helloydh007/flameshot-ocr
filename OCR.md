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

## 许可

与上游一致：GPL-3.0-or-later。
