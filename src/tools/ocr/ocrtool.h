// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#pragma once

#include "src/tools/abstractactiontool.h"

// Action tool that runs OCR on the current selection and shows the
// recognized text in a panel beside the selection (WeChat/QQ style).
// The capture GUI stays open.
class OcrTool : public AbstractActionTool
{
    Q_OBJECT
public:
    explicit OcrTool(QObject* parent = nullptr);

    bool closeOnButtonPressed() const override;
    QIcon icon(const QColor& background, bool inEditor) const override;
    QString name() const override;
    CaptureTool::Type type() const override;
    QString description() const override;
    CaptureTool* copy(QObject* parent) override;

public slots:
    void pressed(CaptureContext& context) override;
};
