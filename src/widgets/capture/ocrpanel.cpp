// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#include "ocrpanel.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

OcrPanel::OcrPanel(QWidget* parent)
  : QWidget(parent)
{
    setObjectName(QStringLiteral("ocrPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(6);

    m_titleLabel = new QLabel(tr2("文字识别", "OCR"), this);
    m_titleLabel->setObjectName(QStringLiteral("ocrTitle"));

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setObjectName(QStringLiteral("ocrText"));
    m_textEdit->setReadOnly(true);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_textEdit->setPlaceholderText(
      tr2("识别结果将显示在这里", "Recognized text appears here"));

    m_copyButton = new QPushButton(tr2("复制文字", "Copy text"), this);
    m_copyButton->setObjectName(QStringLiteral("ocrCopy"));
    m_copyButton->setEnabled(false);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("ocrStatus"));

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_textEdit, 1);
    auto* footer = new QHBoxLayout();
    footer->addWidget(m_statusLabel, 1);
    footer->addWidget(m_copyButton);
    layout->addLayout(footer);

    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_textEdit->toPlainText());
        m_statusLabel->setText(tr2("已复制 ✓", "Copied ✓"));
    });

    // 中文右键菜单（复制/全选），替代 QPlainTextEdit 默认的英文菜单
    m_textEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_textEdit,
            &QWidget::customContextMenuRequested,
            this,
            [this](const QPoint& pos) {
                QMenu menu(m_textEdit);
                QAction* copyAct = menu.addAction(tr2("复制", "Copy"));
                copyAct->setEnabled(m_textEdit->textCursor().hasSelection());
                QAction* selectAllAct =
                  menu.addAction(tr2("全选", "Select all"));
                selectAllAct->setEnabled(!m_textEdit->toPlainText().isEmpty());
                QAction* chosen = menu.exec(m_textEdit->mapToGlobal(pos));
                if (chosen == copyAct) {
                    QApplication::clipboard()->setText(
                      m_textEdit->textCursor().selectedText());
                    m_statusLabel->setText(tr2("已复制 ✓", "Copied ✓"));
                } else if (chosen == selectAllAct) {
                    m_textEdit->selectAll();
                }
            });

    setStyleSheet(QStringLiteral(
      "#ocrTitle { color: #e6e6e6; font-weight: bold; font-size: 13px; }"
      "#ocrText { background-color: #26262b; color: #ececec; border: none; "
      "border-radius: 6px; padding: 6px; font-size: 13px; "
      "selection-background-color: #5842a3; }"
      "#ocrStatus { color: #9a9aa2; font-size: 11px; }"
      "#ocrCopy { background-color: #6c4fd8; color: #ffffff; border: none; "
      "border-radius: 5px; padding: 6px 16px; font-size: 12px; }"
      "#ocrCopy:hover { background-color: #7d63e0; }"
      "#ocrCopy:disabled { background-color: #3c3c44; color: #77777f; }"));
}

QString OcrPanel::tr2(const char* zh, const char* en)
{
    return QLocale::system().name().startsWith(QLatin1String("zh"))
             ? QString::fromUtf8(zh)
             : QString::fromUtf8(en);
}

void OcrPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(63, 63, 70), 1));
    painter.setBrush(QColor(26, 26, 31, 245));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
}

void OcrPanel::wheelEvent(QWheelEvent* event)
{
    // 滚轮只用于滚动识别结果，不让画布把滚动当成“调整笔刷大小”
    event->accept();
}

void OcrPanel::mousePressEvent(QMouseEvent* event)
{
    // 点在面板上不隐藏面板、不触发取色器
    event->accept();
}

void OcrPanel::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}

void OcrPanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    // 双击选词仍由 QPlainTextEdit 处理，这里只阻断向画布的冒泡
    event->accept();
}

void OcrPanel::contextMenuEvent(QContextMenuEvent* event)
{
    // 文本框有自己的自定义菜单；面板其余区域不弹画布菜单
    event->accept();
}

void OcrPanel::showLoading(const QRect& selection)
{
    m_textEdit->clear();
    m_copyButton->setEnabled(false);
    m_statusLabel->setText(tr2("识别中…", "Recognizing…"));
    int w = qBound(260, selection.width(), 400);
    int h = qBound(200, selection.height(), 400);
    resize(w, h);
    positionBeside(selection);
    show();
    raise();
}

void OcrPanel::showText(const QString& text)
{
    m_textEdit->setPlainText(text);
    m_copyButton->setEnabled(!text.isEmpty());
    m_statusLabel->setText(tr2("共 %1 个字符，可框选后点击“复制文字”",
                               "%1 characters — click Copy text")
                             .arg(text.length()));
    raise();
}

void OcrPanel::showFailure(const QString& detail)
{
    QString message = tr2("未能识别出文字", "No text recognized");
    if (!detail.isEmpty()) {
        message += QStringLiteral("\n\n") + detail;
    }
    m_textEdit->setPlainText(message);
    m_copyButton->setEnabled(false);
    m_statusLabel->setText(tr2("识别失败", "Failed"));
    raise();
}

void OcrPanel::positionBeside(const QRect& selection)
{
    QWidget* parent = parentWidget();
    if (!parent) {
        return;
    }
    const QRect area = parent->rect();
    int x = selection.right() + 1 + 12;
    if (x + width() > area.right() - 4) {
        // 右侧放不下时翻到选区左侧
        x = selection.left() - 12 - width();
    }
    if (x < 4) {
        x = qMax(4, area.right() - width() - 4);
    }
    int y = qBound(4, selection.top(), qMax(4, area.bottom() - height() - 4));
    move(x, y);
}
