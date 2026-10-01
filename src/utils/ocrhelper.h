// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#pragma once

#include <QString>
#include <functional>

class QImage;
class QObject;

namespace OcrHelper
{
// 异步对图像运行 OCR（读取 flameshot 的 ocrCommand 配置，默认 tesseract）。
// 完成后在主线程回调：ok=true 时 result 为识别文本（可能为空），
// ok=false 时 result 为错误信息。guard 销毁时自动放弃未完成的任务。
void run(const QImage& image,
         QObject* guard,
         std::function<void(bool ok, const QString& result)> callback);
}
