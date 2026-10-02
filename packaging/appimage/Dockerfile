# flameshot-ocr AppImage 构建环境（Debian 13 trixie / Qt 6.8）
#
# 用法（仓库根目录执行）：
#   ./packaging/appimage/build.sh
# 或手动：
#   docker build -t flameshot-ocr-builder packaging/appimage
#   docker run --rm -v "$PWD":/work flameshot-ocr-builder
# 产物：dist/flameshot-ocr-<版本>-x86_64.AppImage
FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive

# 构建依赖与 linuxdeploy 运行所需的工具；qt6-wayland 提供 Wayland 平台插件
# （AppImage 需同时带 xcb 与 wayland 两套 QPA 插件）
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        curl \
        file \
        xz-utils \
        qt6-base-dev \
        qt6-base-dev-tools \
        qt6-svg-dev \
        qt6-tools-dev \
        qt6-tools-dev-tools \
        qt6-l10n-tools \
        qt6-wayland \
        qt6-svg-plugins \
        libgl1-mesa-dev \
        # OCR 引擎随包捆绑（开箱即用，无需宿主机安装）
        tesseract-ocr \
        tesseract-ocr-chi-sim \
        tesseract-ocr-eng \
    && rm -rf /var/lib/apt/lists/*

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

WORKDIR /work
ENTRYPOINT ["entrypoint.sh"]
