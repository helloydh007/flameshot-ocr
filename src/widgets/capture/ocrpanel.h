// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

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
    // 面板及其子控件未消费的输入事件在此 accept，防止冒泡到 CaptureWidget
    // （否则会触发隐藏面板、取色器、笔刷大小转轮等画布行为）
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void positionBeside(const QRect& selection);
    void setMinimized(bool minimized);

    QLabel* m_titleLabel;
    QToolButton* m_minButton;
    QToolButton* m_closeButton;
    QWidget* m_body;
    QPlainTextEdit* m_textEdit;
    QPushButton* m_copyButton;
    QLabel* m_statusLabel;
    bool m_minimized = false;
    QSize m_expandedSize;
};
