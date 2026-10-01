// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "pinannotator.h"
#include <QPair>
#include <QVector>
#include <QWidget>

class QLabel;
class QVBoxLayout;
class QGestureEvent;
class QPinchGesture;
class QGraphicsDropShadowEffect;
class OcrPanel;
class QResizeEvent;
class QToolButton;

class PinWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PinWidget(const QPixmap& pixmap,
                       const QRect& geometry,
                       QWidget* parent = nullptr);

    // 复制到剪贴板（含标注合成）
    void copyToClipboard();

protected:
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    bool gestureEvent(QGestureEvent* event);
    bool scrollEvent(QWheelEvent* e);
    void pinchTriggered(QPinchGesture*);
    void closePin();

    // 带标注合成的最终图像（复制/保存用）
    QPixmap compositedPixmap() const;
    void positionAnnotator();
    void buildToolBar();

    void rotateLeft();
    void rotateRight();

    void increaseOpacity();
    void decreaseOpacity();

    QPixmap m_pixmap;
    QVBoxLayout* m_layout;
    QLabel* m_label;
    QGraphicsDropShadowEffect* m_shadowEffect;
    OcrPanel* m_ocrPanel = nullptr;
    PinAnnotator* m_annotator = nullptr;
    QWidget* m_toolBarRow = nullptr;
    QToolButton* m_colorButton = nullptr;
    QVector<QPair<PinAnnotator::Tool, QToolButton*>> m_toolButtons;
    QColor m_baseColor, m_hoverColor;

    bool m_expanding{ false };
    qreal m_scaleFactor{ 1 };
    qreal m_opacity{ 1 };
    unsigned int m_rotateFactor{ 0 };
    qreal m_currentStepScaleFactor{ 1 };
    bool m_sizeChanged{ false };

private slots:
    void showContextMenu(const QPoint& pos);
    void saveToFile();
    void runOcr();
};
