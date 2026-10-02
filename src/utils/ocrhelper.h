// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <functional>

class QProcess;
class QTemporaryFile;
class QTimer;

// 一次可取消、带超时的 OCR 任务（由 OcrHelper::run 创建，父对象为调用方
// 提供的 guard，guard 销毁时任务随之销毁并终止外部进程）。
//
// - cancel()：两阶段终止（terminate() → 宽限 → kill()），此后完成回调
//   不再触发（用于“新任务取代旧任务”，避免连续 OCR 时进程堆积）
// - 默认 30 秒超时：超时按失败回调（ocrCommand 可配置，引擎可能卡死）
class OcrTask : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(bool ok, const QString& result)>;

    explicit OcrTask(QObject* parent = nullptr);
    ~OcrTask() override;

    // 两阶段取消：回调不会再触发
    void cancel();

    // 以下供 OcrHelper::run 组装使用（写临时图 → 解析命令 → 启动进程）
    void start(const QImage& image, const QString& command);

    OcrTask::Callback m_callback;

private:
    void finish(bool ok, const QString& result, bool invokeCallback);
    void terminateWithGrace();

    QProcess* m_proc = nullptr;
    QTemporaryFile* m_tmp = nullptr;
    QTimer* m_timeoutTimer = nullptr;
    QString m_program;
    bool m_done = false;
    bool m_superseded = false;
};

namespace OcrHelper
{
// OCR 前预处理：屏幕文字通常仅 12~20px 高，2x 平滑放大 + 轻度 unsharp
// 锐化可显著提升 tesseract 识别率（实测关键词命中 6/10 → 10/10，并
// 修复 psm6 下的行首丢失）；超大图跳过放大防内存膨胀。
QImage preprocessForOcr(const QImage& image);

// 异步对图像运行 OCR（读取 flameshot 的 ocrCommand 配置，默认 tesseract）。
// 返回任务句柄（父对象为 guard）：调用方可用 cancel() 终止旧任务，
// 保证同一界面最多只有一个活动的 OCR 引擎进程。
// 完成后在主线程回调：ok=true 时 result 为识别文本（可能为空），
// ok=false 时 result 为错误信息。guard 销毁时自动放弃未完成的任务。
OcrTask* run(const QImage& image,
             QObject* guard,
             const OcrTask::Callback& callback);
}
