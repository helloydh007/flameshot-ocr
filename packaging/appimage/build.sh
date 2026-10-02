#!/usr/bin/env bash
# flameshot-ocr 一键 AppImage 打包（Docker 内构建，产物输出到仓库 dist/）
# 用法：./packaging/appimage/build.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

docker build -q -t flameshot-ocr-builder:latest packaging/appimage
mkdir -p dist
docker run --rm -v "$ROOT":/work flameshot-ocr-builder:latest

echo
echo "产物："
ls -lh dist/*.AppImage
