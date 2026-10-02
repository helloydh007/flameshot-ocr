#!/usr/bin/env bash
# flameshot-ocr AppImage 打包入口（容器内执行；/work = 仓库挂载点）
#
# 流程：源码复制到可写目录 → CMake 构建 → 安装到 appdir →
#       补齐 Qt 运行时插件（platforms/imageformats/iconengines/wayland-*）
#       与 svg 图标引擎依赖 → linuxdeploy（ldd 闭包捆绑 + 自定义 AppRun）
#       → dist/flameshot-ocr-<版本>-x86_64.AppImage
set -euo pipefail

WORK=/work
SRC=/tmp/src
BUILD=/tmp/build
APPDIR=/tmp/appdir
TOOLS=/tmp/tools
OUT="$WORK/dist"
mkdir -p "$OUT"

echo "==> 复制源码（含 .git 以生成版本号）"
rm -rf "$SRC"
cp -a "$WORK/." "$SRC/"
rm -rf "$SRC/dist"

echo "==> CMake 配置与构建（$(nproc) 线程）"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_MANDIR=/usr/share/man
cmake --build "$BUILD" -j"$(nproc)"

echo "==> 安装到 appdir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"

QTDIR=/usr/lib/x86_64-linux-gnu
QTPLUGINS="$QTDIR/qt6/plugins"
APPPLUGINS="$APPDIR/usr/lib/qt6/plugins"

echo "==> 拷贝 Qt 运行时插件（AppImage 必须自带 QPA 插件）"
# libQt6Svg：主程序因 --as-needed 未直接链接，但托盘/应用图标（svg）
# 在运行时经 iconengine 动态加载，缺失会退化成空白图标
cp -a "$QTDIR"/libQt6Svg.so.* "$APPDIR/usr/lib/"
for d in platforms imageformats iconengines wayland-shell-integration \
         wayland-decoration-client wayland-graphics-integration-client; do
    if [ -d "$QTPLUGINS/$d" ]; then
        mkdir -p "$APPPLUGINS/$d"
        cp -a "$QTPLUGINS/$d/." "$APPPLUGINS/$d/"
    fi
done

echo "==> 补齐插件的动态库依赖（wayland/xkb 客户端栈，2 轮收敛）"
for _ in 1 2; do
    find "$APPPLUGINS" -type f -name '*.so*' -print0 | xargs -0 -r ldd 2>/dev/null \
        | awk '/=> \//{print $3}' | sort -u | while read -r dep; do
            base=$(basename "$dep")
            case "$dep" in
                *libQt6*|*libwayland*|*libxkb*|*libxcb-*|*libX11*|*libGLdispatch*|*libEGL*)
                    # X11/EGL 留给宿主（GPU 栈不放 AppImage），此处仅进程内解析需自洽，
                    # 因此全部拷贝，linuxdeploy 稍后统一去重
                    [ -e "$APPDIR/usr/lib/$base" ] || cp -aL "$dep" "$APPDIR/usr/lib/"
                    ;;
            esac
        done
done

echo "==> 写 qt.conf（插件路径相对二进制）+ Qt 自带翻译"
# Qt 自带翻译（qtbase_*.qm，否则运行时告警 "No Qt translation found"）
if [ -d /usr/share/qt6/translations ]; then
    mkdir -p "$APPDIR/usr/share/qt6/translations"
    cp -a /usr/share/qt6/translations/qtbase_*.qm \
          "$APPDIR/usr/share/qt6/translations/" 2>/dev/null || true
fi
rmdir "$APPPLUGINS/iconengines" 2>/dev/null || true   # Debian Qt6 无此目录（svg 引擎内建）
cat > "$APPDIR/usr/bin/qt.conf" <<'EOF'
[Paths]
Prefix=..
Plugins=lib/qt6/plugins
Translations=share/qt6/translations
EOF

echo "==> 下载 linuxdeploy"
mkdir -p "$TOOLS"
cd "$TOOLS"
if [ ! -x linuxdeploy.AppImage ]; then
    curl -fsSL -o linuxdeploy.AppImage \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy.AppImage
fi

export APPIMAGE_EXTRACT_AND_RUN=1   # 容器内无 FUSE，走解包运行
export ARCH=x86_64
VERSION=$(git -C "$SRC" describe --tags --always 2>/dev/null | sed 's/^v//' \
    || echo "13.3.0")
export VERSION
echo "==> 版本: $VERSION"

# 记录打包前所有 AppImage 路径，运行后取差集定位产物（不依赖 cwd 约定）
find / -xdev -name '*.AppImage' 2>/dev/null | sort > /tmp/appimages.before

echo "==> linuxdeploy 捆绑（递归 ldd 闭包 + 自定义 AppRun）"
"$TOOLS/linuxdeploy.AppImage" \
    --appdir "$APPDIR" \
    --custom-apprun "$SRC/packaging/appimage/AppRun" \
    --output appimage

BUILT=$(find / -xdev -name '*.AppImage' 2>/dev/null | sort \
    | comm -13 /tmp/appimages.before - | grep -v linuxdeploy | head -1)
if [ -z "$BUILT" ]; then
    echo "!! 未找到生成的 AppImage" >&2
    exit 1
fi
FINAL="$OUT/flameshot-ocr-$VERSION-x86_64.AppImage"
mv "$BUILT" "$FINAL"
chmod +x "$FINAL"

echo "==> 完成: $FINAL ($(du -h "$FINAL" | cut -f1))"
