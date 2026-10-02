<div align="center">

# flameshot-ocr

**基于 KDE Flameshot 深度定制的截图工具：框选即出工具条、一键 OCR（微信/QQ 式结果面板）、钉图标注、取色器、序号递增……**

基于上游 [flameshot](https://github.com/flameshot-org/flameshot) 13.3.0（Debian 快照），为 KDE Plasma 6 / Wayland 125% 高分屏环境深度调优。

</div>

---

## 功能一览

| 功能 | 说明 |
|---|---|
| 🖱️ 框选截图 | F1（可改）唤出，鼠标拖框；误点选区外不会打断截图 |
| 📝 一键 OCR | 选区/全屏识别，微信式结果面板出现在选区旁，文字可选中、一键复制；连续识别自动取消旧任务 |
| 🎨 标注工具 | 画笔/荧光笔/箭头/矩形/椭圆/直线/序号递增/文字（支持拼音输入法）/马赛克/橡皮擦，8 色 3 粗细、填充开关 |
| 📌 钉图 | 截图钉在桌面，右键菜单带完整标注工具条；旋转/缩放/透明度（Ctrl+滚轮）/缩放百分比提示 |
| 🎨 取色器 | 放大镜像素级取色，格式可配（HEX / RGB） |
| ⌨️ Win11 风格预选工具条 | 框选前悬浮工具条，可先行选择标注工具 |
| 🔇 无启动反馈图标 | F1 经 kwin 脚本直调常驻进程，不会出现跳动的蓝色齿轮 |

## 安装

### 方式一：AppImage（推荐，免安装）

到 [Releases](https://github.com/helloydh007/flameshot-ocr/releases) 下载 `flameshot-ocr-<版本>-x86_64.AppImage`：

```bash
chmod +x flameshot-ocr-*-x86_64.AppImage
./flameshot-ocr-*-x86_64.AppImage          # 启动托盘常驻
./flameshot-ocr-*-x86_64.AppImage gui      # 直接框选截图
```

> 若系统未装 libfuse2 而无法双击运行，改用 `./xxx.AppImage --appimage-extract-and-run`，或 `sudo apt install libfuse2`。

**开机自启（可选）**：创建 `~/.config/autostart/flameshot-ocr-appimage.desktop`：

```ini
[Desktop Entry]
Type=Application
Exec=/绝对路径/flameshot-ocr-<版本>-x86_64.AppImage
Name=Flameshot OCR
X-GNOME-Autostart-enabled=true
```

### 方式二：deb 包（Debian 13 / Ubuntu 24.04+）

```bash
# 源码内构建 deb（详见"从源码构建"）
cd build && cpack -G DEB && sudo apt install ./flameshot-*.deb
```

安装后含系统托盘、登录自启动配置；`setup.sh`（仓库根目录）可一次性完成快捷键与自启动配置。

### 方式三：从源码构建

见下文 [从源码构建](#从源码构建)。

## 卸载

**AppImage**：删除文件即可，另清理两处用户级数据（可选）：

```bash
rm ~/.local/share/flameshot-ocr/f1-dbus-launch.js \
   ~/.local/share/flameshot-ocr/pin-keepabove.js   # kwin 辅助脚本
# 若配置过自启动：rm ~/.config/autostart/flameshot-ocr-appimage.desktop
```

**deb 包**：

```bash
sudo apt remove flameshot
# 清理配置残留（全局快捷键与启动反馈设置，可按需保留）
kwriteconfig6 --file kglobalshortcutsrc --group kwin --key FlameshotGuiF1 --delete
```

## 依赖

### 运行时依赖

| 组件 | 用途 | 必要性 |
|---|---|---|
| KDE Plasma 6 / KWin | 全局快捷键（kwin 脚本）、钉图置顶 | KDE 集成必需；纯截图功能在其他桌面也可用 |
| `tesseract-ocr` + 语言包 | OCR 引擎 | AppImage **已捆绑**（deb/源码方式需自装） |
| `qdbus6`（qt6-tools） | kwin 脚本加载、钉图置顶 | 仅 KDE 集成用；GNOME 下自动跳过 |
| `libfuse2` | AppImage 双击运行 | 仅 AppImage 方式 |

**AppImage：OCR 开箱即用**——包内自带 tesseract 5.x 与 `chi_sim`/`eng`/`osd`
语言包，无需安装任何东西。deb/源码方式需：

```bash
sudo apt install tesseract-ocr tesseract-ocr-chi-sim tesseract-ocr-eng
```

> OCR 引擎由 `ocrCommand` 配置项决定（默认 `tesseract %i stdout ...`）；
> AppImage 启动时把捆绑引擎置于 PATH 最前，也可替换为任意支持 `%i`
> 占位符的命令行引擎。

### GNOME 桌面

- **截图 / 标注 / OCR / 钉图**：全部可用（AppImage 内置 Wayland 与 X11
  双后端，GNOME Wayland 与 X11 会话均可直接运行）。
- **F1 快捷键**：KDE 专用的 kwin 脚本机制在 GNOME 上不生效（程序会自动
  跳过、无卡顿）。替代：GNOME「设置 → 键盘 → 自定义快捷键」添加命令
  `flameshot gui`（路径指向 AppImage 文件），绑定 F1。
- **托盘图标**：GNOME 需安装 AppIndicator 扩展（“AppIndicator and KStatusNotifierItem Support”）才会显示托盘；不装也不影响快捷键截图。
- **钉图置顶**：依赖 kwin，GNOME 下钉图不置顶（其余功能不受影响）。

### 从源码构建（Debian 13 为例）

```bash
sudo apt install build-essential cmake ninja-build git \
    qt6-base-dev qt6-base-dev-tools qt6-svg-dev qt6-tools-dev \
    qt6-tools-dev-tools qt6-l10n-tools qt6-wayland libgl1-mesa-dev
git clone https://github.com/helloydh007/flameshot-ocr.git
cd flameshot-ocr
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr    # 必须为 /usr，否则 deb 的 desktop Exec 路径错误
cmake --build build -j$(nproc)
sudo cmake --install build          # 或 cd build && cpack -G DEB 生成 deb
```

KDSingleApplication 默认经 FetchContent 自动拉取（构建机需可访问 GitHub）；其它第三方库（QtColorWidgets 等）已随源码捆绑。

## 原理（这套定制是怎么工作的）

```
F1 按键
  └─ kwin 内嵌 kglobalaccel 触发 kwin 脚本 f1-dbus-launch.js（daemon 启动时自动加载）
       └─ callDBus → 常驻 flameshot 进程 captureGui()
            └─ 在常驻进程内直接打开截图遮罩（不 fork 新进程）
```

- **F1 直调常驻进程**：传统方案由 kglobalaccel 启动新进程，KDE 启动器会申请 xdg-activation 令牌并在光标旁显示"启动反馈"齿轮；常驻进程直调后无进程启动，令牌与齿轮从机制上消除，且免去冷启动。
- **OCR 管线**：选区按设备像素从原图裁剪（与复制/固定同一条坐标语义）→ 2x Lanczos 放大 + 轻度 unsharp 锐化（屏幕小字识别率 6/10 → 10/10）→ 临时 PNG（任务结束即删）→ 外部引擎异步识别（可取消、30 秒超时、两阶段终止）→ 结果面板显示在选区旁。
- **钉图标注**：矢量形状存于底图原始像素坐标系，跟随旋转/缩放；显示时按 `1/displayScale` 反向映射（高 DPI 屏坐标不偏移）；复制/保存时直接合成到输出位图。
- **钉图置顶**：kwin 脚本对 caption 为 `flameshot-pin` 的窗口设置 keepAbove，daemon 启动时自愈式加载。
- **坐标体系**：选区、裁剪、标注全程使用设备像素（DPR 感知），避免 125%/150% 缩放屏上的双重换算偏移——这是上游在同环境下的高频 bug 源。

更详细的配置项、调试钩子（`FLAMESHOT_OCR_SELFTEST` 等 43 项运行时自测）见 [OCR.md](OCR.md)。

## 从源码构建

见上文 [依赖 → 从源码构建](#从源码构建deb-13-为例)。

## 打包 AppImage（Docker 内构建）

```bash
./packaging/appimage/build.sh        # 产物：dist/flameshot-ocr-<版本>-x86_64.AppImage
```

流程：`debian:trixie` 容器内 CMake 构建 → 安装到 appdir → 捆绑 Qt 运行库与
QPA 插件（xcb + wayland 双后端）+ **tesseract OCR 引擎与中/英语言包**（
`usr/ocr/`，AppRun 注入 `PATH`/`TESSDATA_PREFIX`）→ linuxdeploy 递归闭包 +
自定义 AppRun。
打 `v*` tag 推送后 CI（`.github/workflows/appimage.yml`）自动构建并发布 Release。

## 与上游的差异

本项目是 flameshot 的功能性分支（非补丁集）：新增 OCR 工具链、钉图标注层、
预选工具条、取色格式配置、kwin 直调快捷键机制等，并包含两轮外部代码审查的
生命周期/性能修复。完整差异清单与审查修复记录见 [OCR.md](OCR.md)。

## 许可

与上游一致：[GPL-3.0-or-later](LICENSE)。
