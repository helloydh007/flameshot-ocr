// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "flameshotdbusadapter.h"
#include "flameshot.h"
#include "src/core/flameshotdaemon.h"

FlameshotDBusAdapter::FlameshotDBusAdapter(QObject* parent)
  : QDBusAbstractAdaptor(parent)
{}

FlameshotDBusAdapter::~FlameshotDBusAdapter() = default;

// flameshot-ocr: kwin 脚本快捷键（F1）经 callDBus 直调，由 daemon 在
// 自身进程内打开截图界面 —— 不启动新进程，不触发启动反馈
void FlameshotDBusAdapter::captureGui()
{
    Flameshot::instance()->captureGui();
}

void FlameshotDBusAdapter::attachScreenshotToClipboard(const QByteArray& data)
{
    FlameshotDaemon::instance()->attachScreenshotToClipboard(data);
}

void FlameshotDBusAdapter::attachTextToClipboard(const QString& text,
                                                 const QString& notification)
{
    FlameshotDaemon::instance()->attachTextToClipboard(text, notification);
}

void FlameshotDBusAdapter::attachPin(const QByteArray& data)
{
    FlameshotDaemon::instance()->attachPin(data);
}
