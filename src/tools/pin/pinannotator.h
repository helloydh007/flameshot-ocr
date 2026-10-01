// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#pragma once

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QWidget>
#include <QVector>

// 钉图标注层：透明子控件，覆盖在钉图 QLabel 上方。
// - 矢量形状存放在“PinWidget 当前 m_pixmap 坐标系”，跟随旋转/缩放
// - 未选工具时，按下交给 PinWidget 移动窗口；右键弹出钉图菜单
class PinAnnotator : public QWidget
{
    Q_OBJECT
public:
    enum Tool
    {
        None,
        Pencil,
        Marker,
        Arrow,
        Rectangle,
        Ellipse,
        Line
    };
    Q_ENUM(Tool)

    explicit PinAnnotator(QWidget* parent = nullptr);

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    QColor color() const { return m_color; }
    void setColor(const QColor& color);
    int width() const { return m_width; }
    void setWidth(int width);

    bool hasShapes() const;
    void undo();
    void clearShapes();

    // 旋转形状点（PinWidget 先旋转 m_pixmap，再带上旋转前的尺寸调用）
    void applyRotateRight(int oldWidth, int oldHeight);
    void applyRotateLeft(int oldWidth, int oldHeight);

    // 显示缩放 = 钉图 QLabel 显示尺寸 / m_pixmap 尺寸
    void setDisplayScale(qreal scale) { m_displayScale = scale > 0 ? scale : 1.0; }

    // 按 m_pixmap 尺寸渲染所有标注（透明背景），用于复制/保存时合成
    QImage renderToImage(const QSize& baseSize) const;

signals:
    // 转发给 PinWidget 的画布行为
    void moveRequested();
    void closeRequested();
    void contextMenuRequested(const QPoint& posInPin);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct PinShape
    {
        Tool type = None;
        QColor color;
        int width = 4;
        QVector<QPointF> points;
    };

    void renderShapes(QPainter& painter, qreal scale) const;
    QPointF toBase(const QPoint& widgetPos) const;

    QVector<PinShape> m_shapes;
    PinShape m_current;
    bool m_drawing = false;
    Tool m_tool = None;
    QColor m_color = Qt::red;
    int m_width = 4;
    qreal m_displayScale = 1.0;
};
