// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#pragma once

#include "src/tools/capturetool.h"
#include <QWidget>

class QToolButton;

// Win11 风格的预选区悬浮工具条：截图激活后立即显示在屏幕顶部中央，
// 开始拖动/绘制时隐藏，松开后按状态重现。取色结果按所选格式自动复制。
class PreToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit PreToolbar(QWidget* parent = nullptr);

    // NONE = 框选模式（与外部状态同步勾选态）
    void setToolChecked(CaptureTool::Type type);
    void setEraserChecked(bool checked);
    void setDrawColorPreview(const QColor& color);

signals:
    void selectModeRequested();
    void toolRequested(CaptureTool::Type type);
    void drawColorChanged(const QColor& color);
    void colorGrabRequested();
    void fullscreenCopyRequested();
    void saveRequested();
    void settingsRequested();
    void undoRequested();
    void redoRequested();
    void eraserRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QToolButton* m_selectBtn = nullptr;
    QVector<QPair<CaptureTool::Type, QToolButton*>> m_toolButtons;
    QToolButton* m_colorBtn = nullptr;
    QToolButton* m_eraserBtn = nullptr;
    QColor m_drawColor;
};
