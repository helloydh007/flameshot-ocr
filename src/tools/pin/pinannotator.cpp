// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#include "pinannotator.h"
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

namespace
{
constexpr qreal MARKER_ALPHA = 110.0;
constexpr qreal ARROW_HEAD = 14.0;   // 箭头头部基准长度（基础坐标像素）
} // namespace

PinAnnotator::PinAnnotator(QWidget* parent)
  : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setCursor(Qt::ArrowCursor);
}

void PinAnnotator::setTool(Tool tool)
{
    m_tool = tool;
    setCursor(m_tool == None ? Qt::ArrowCursor : Qt::CrossCursor);
}

void PinAnnotator::setColor(const QColor& color)
{
    m_color = color;
}

void PinAnnotator::setWidth(int width)
{
    m_width = width;
}

bool PinAnnotator::hasShapes() const
{
    return !m_shapes.isEmpty();
}

void PinAnnotator::undo()
{
    if (!m_shapes.isEmpty()) {
        m_shapes.removeLast();
        update();
    }
}

void PinAnnotator::clearShapes()
{
    m_shapes.clear();
    m_drawing = false;
    update();
}

void PinAnnotator::applyRotateRight(int oldWidth, int oldHeight)
{
    // QTransform().rotate(90) 的映射：(x, y) -> (oldHeight - y, x)
    for (PinShape& s : m_shapes) {
        for (QPointF& p : s.points) {
            p = QPointF(oldHeight - p.y(), p.x());
        }
    }
    update();
}

void PinAnnotator::applyRotateLeft(int oldWidth, int oldHeight)
{
    // QTransform().rotate(270) 的映射：(x, y) -> (y, oldWidth - x)
    for (PinShape& s : m_shapes) {
        for (QPointF& p : s.points) {
            p = QPointF(p.y(), oldWidth - p.x());
        }
    }
    update();
}

QImage PinAnnotator::renderToImage(const QSize& baseSize) const
{
    QImage image(baseSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderShapes(painter, 1.0);
    painter.end();
    return image;
}

QPointF PinAnnotator::toBase(const QPoint& widgetPos) const
{
    return QPointF(widgetPos) / m_displayScale;
}

void PinAnnotator::renderShapes(QPainter& painter, qreal scale) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.save();
    painter.scale(scale, scale);

    QVector<PinShape> all = m_shapes;
    if (m_drawing) {
        all.append(m_current);
    }
    for (const PinShape& s : all) {
        QPen pen(s.color, s.width, Qt::SolidLine, Qt::RoundCap,
                 Qt::RoundJoin);
        if (s.type == Marker) {
            QColor c = s.color;
            c.setAlphaF(MARKER_ALPHA / 255.0);
            pen.setColor(c);
            pen.setWidthF(s.width * 4.0);
        }
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        switch (s.type) {
            case Pencil:
            case Marker:
                if (s.points.size() >= 2) {
                    for (int i = 1; i < s.points.size(); ++i) {
                        painter.drawLine(s.points[i - 1], s.points[i]);
                    }
                } else if (!s.points.isEmpty()) {
                    painter.drawPoint(s.points.first());
                }
                break;
            case Line:
                if (s.points.size() >= 2) {
                    painter.drawLine(s.points.first(), s.points.last());
                }
                break;
            case Arrow:
                if (s.points.size() >= 2) {
                    const QPointF from = s.points.first();
                    const QPointF to = s.points.last();
                    painter.drawLine(from, to);
                    const qreal dx = to.x() - from.x();
                    const qreal dy = to.y() - from.y();
                    const qreal len = std::hypot(dx, dy);
                    if (len > 1) {
                        const qreal headLen =
                          qMax(ARROW_HEAD, s.width * 3.0);
                        const qreal ux = dx / len;
                        const qreal uy = dy / len;
                        const qreal px = -uy;
                        const qreal py = ux;
                        QPolygonF head;
                        head << to
                             << QPointF(to.x() - headLen * ux + headLen * 0.4 * px,
                                        to.y() - headLen * uy + headLen * 0.4 * py)
                             << QPointF(to.x() - headLen * ux - headLen * 0.4 * px,
                                        to.y() - headLen * uy - headLen * 0.4 * py);
                        painter.setBrush(s.color);
                        painter.drawPolygon(head);
                    }
                }
                break;
            case Rectangle:
                if (s.points.size() >= 2) {
                    painter.drawRect(QRectF(s.points.first(), s.points.last()).normalized());
                }
                break;
            case Ellipse:
                if (s.points.size() >= 2) {
                    painter.drawEllipse(QRectF(s.points.first(), s.points.last()).normalized());
                }
                break;
            default:
                break;
        }
    }
    painter.restore();
}

void PinAnnotator::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    renderShapes(painter, m_displayScale);
}

void PinAnnotator::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_tool != None) {
        m_current = PinShape{ m_tool, m_color, m_width, { toBase(event->pos()) } };
        m_drawing = true;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        // 未选工具：交给 PinWidget 移动窗口
        emit moveRequested();
        event->accept();
        return;
    }
    event->accept();
}

void PinAnnotator::mouseMoveEvent(QMouseEvent* event)
{
    if (m_drawing) {
        const QPointF p = toBase(event->pos());
        if (m_current.type == Pencil || m_current.type == Marker) {
            m_current.points.append(p);
        } else if (!m_current.points.isEmpty()) {
            m_current.points[1] = p;
        }
        update();
        event->accept();
        return;
    }
    event->accept();
}

void PinAnnotator::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_drawing) {
        m_drawing = false;
        m_shapes.append(m_current);
        m_current = PinShape{};
        update();
    }
    event->accept();
}

void PinAnnotator::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_tool == None) {
        // 无工具时保持官方行为：双击关闭钉图
        emit closeRequested();
    }
    event->accept();
}

void PinAnnotator::wheelEvent(QWheelEvent* event)
{
    // 忽略以冒泡给 PinWidget 处理缩放
    event->ignore();
}

void PinAnnotator::contextMenuEvent(QContextMenuEvent* event)
{
    // 右键转发给 PinWidget 弹出上下文菜单
    if (parentWidget()) {
        emit contextMenuRequested(mapTo(parentWidget(), event->pos()));
    }
    event->accept();
}
