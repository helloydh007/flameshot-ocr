// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// Panel shown beside the selection with OCR results (WeChat/QQ style):
// selectable text plus a copy button.
class OcrPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OcrPanel(QWidget* parent = nullptr);

    // Position next to the selection and show the "recognizing" state.
    void showLoading(const QRect& selection);
    // Show recognized text.
    void showText(const QString& text);
    // Show a failure message; `detail` carries technical output (optional).
    void showFailure(const QString& detail = QString());

    // Bilingual string helper for the OCR UI (Chinese UI when applicable).
    static QString tr2(const char* zh, const char* en);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void positionBeside(const QRect& selection);

    QLabel* m_titleLabel;
    QPlainTextEdit* m_textEdit;
    QPushButton* m_copyButton;
    QLabel* m_statusLabel;
};
