// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#include "pinannotator.h"
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

namespace
{
constexpr qreal MARKER_ALPHA = 110.0;
constexpr qreal ARROW_HEAD = 14.0;   // 箭头头部基准长度（基础坐标像素）
constexpr int TEXT_FONT_PX = 20;     // 文字基准字号（基础坐标像素）
constexpr int NUMBER_RADIUS = 14;    // 序号气泡半径（基础坐标像素）
} // namespace

PinAnnotator::PinAnnotator(QWidget* parent)
  : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::ArrowCursor);
}

void PinAnnotator::setTool(Tool tool)
{
    if (m_textEditing) {
        commitPendingText();
    }
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
    if (m_textEditing) {
        commitPendingText();
    }
    if (!m_shapes.isEmpty()) {
        m_redoShapes.append(m_shapes.takeLast());
        update();
    }
}

void PinAnnotator::redo()
{
    if (!m_redoShapes.isEmpty()) {
        m_shapes.append(m_redoShapes.takeLast());
        update();
    }
}

void PinAnnotator::commitPendingText()
{
    if (m_textEditing && m_current.type == Text &&
        !m_current.text.trimmed().isEmpty()) {
        m_shapes.append(m_current);
    }
    m_textEditing = false;
    m_current = PinShape{};
    update();
}

void PinAnnotator::clearShapes()
{
    m_shapes.clear();
    m_redoShapes.clear();
    m_drawing = false;
    m_textEditing = false;
    m_current = PinShape{};
    m_counter = 1;
    update();
}

// 删除位置 index 的形状并压入重做栈（橡皮擦用，redo 可恢复）
void PinAnnotator::pushCurrentToRedo(int index)
{
    if (index < 0 || index >= m_shapes.size()) {
        return;
    }
    m_redoShapes.append(m_shapes.takeAt(index));
}

void PinAnnotator::applyRotateRight(int oldWidth, int oldHeight)
{
    Q_UNUSED(oldWidth)
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
    Q_UNUSED(oldHeight)
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

// 形状包围盒（命中测试/橡皮擦/旋转共用；基础坐标系）
QRectF PinAnnotator::shapeRect(const PinShape& s) const
{
    switch (s.type) {
        case Pencil:
        case Marker:
        case Arrow:
        case Rectangle:
        case Ellipse:
        case Line: {
            QRectF r(s.points.first(), s.points.first());
            for (const QPointF& p : s.points) {
                r = r.united(QRectF(p, p));
            }
            return r.adjusted(-s.width, -s.width, s.width, s.width);
        }
        case Text: {
            QFont font;
            font.setPixelSize(TEXT_FONT_PX);
            const QFontMetrics metrics(font);
            const QString shown =
              s.text + (m_textEditing && s.type == Text ? QStringLiteral("|")
                                                        : QString());
            // 与绘制锚点一致：boundingRect 以基线为原点，
            // 绘制基线在 points.first() + (0, 字号*0.8)
            QRectF r = metrics.boundingRect(shown);
            r.translate(s.points.first() +
                        QPointF(0, TEXT_FONT_PX * 0.8));
            return r.adjusted(-4, -4, 4, 4);
        }
        case Number: {
            return QRectF(s.points.first().x() - NUMBER_RADIUS,
                          s.points.first().y() - NUMBER_RADIUS,
                          NUMBER_RADIUS * 2, NUMBER_RADIUS * 2);
        }
        default:
            break;
    }
    return QRectF();
}

void PinAnnotator::commitCurrent()
{
    if (m_drawing) {
        m_drawing = false;
        m_shapes.append(m_current);
        m_redoShapes.clear();
        m_current = PinShape{};
        update();
    }
}

void PinAnnotator::renderShapes(QPainter& painter, qreal scale) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.save();
    painter.scale(scale, scale);

    QVector<PinShape> all = m_shapes;
    if (m_drawing || m_textEditing) {
        all.append(m_current);
    }
    for (const PinShape& s : all) {
        if (s.type == Text) {
            QFont font;
            font.setPixelSize(TEXT_FONT_PX);
            painter.setFont(font);
            painter.setPen(QPen(s.color));
            const QString shown =
              s.text + (m_textEditing ? QStringLiteral("|") : QString());
            painter.drawText(s.points.first() +
                               QPointF(0, TEXT_FONT_PX * 0.8),
                             shown);
            continue;
        }
        if (s.type == Number) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush(s.color));
            painter.drawEllipse(s.points.first(), NUMBER_RADIUS,
                                NUMBER_RADIUS);
            QFont font;
            font.setPixelSize(NUMBER_RADIUS + 6);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(QPen(Qt::white));
            painter.drawText(
              QRectF(s.points.first().x() - NUMBER_RADIUS,
                     s.points.first().y() - NUMBER_RADIUS,
                     NUMBER_RADIUS * 2, NUMBER_RADIUS * 2),
              Qt::AlignCenter, QString::number(s.number));
            continue;
        }
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
    if (event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }
    // 文字编辑中：点击其它位置 = 提交当前文字
    if (m_textEditing) {
        commitPendingText();
    }
    switch (m_tool) {
        case None:
            // 未选工具：交给 PinWidget 移动窗口
            emit moveRequested();
            break;
        case Eraser: {
            // 删除点击位置（包围盒命中）的最上层形状，redo 可恢复
            for (int i = m_shapes.size() - 1; i >= 0; --i) {
                if (shapeRect(m_shapes.at(i))
                      .contains(toBase(event->pos()))) {
                    pushCurrentToRedo(i);
                    break;
                }
            }
            update();
            break;
        }
        case Text:
            m_current = PinShape{ Text, m_color, m_width,
                                  { toBase(event->pos()) } };
            m_textEditing = true;
            m_drawing = false;
            setFocus();
            update();
            break;
        case Number: {
            PinShape s{ Number, m_color, m_width,
                        { toBase(event->pos()) } };
            s.number = m_counter++;
            m_shapes.append(s);
            m_redoShapes.clear();
            update();
            break;
        }
        default:
            m_current = PinShape{ m_tool, m_color, m_width,
                                  { toBase(event->pos()) } };
            m_drawing = true;
            break;
    }
    event->accept();
}

void PinAnnotator::mouseMoveEvent(QMouseEvent* event)
{
    if (m_drawing) {
        const QPointF p = toBase(event->pos());
        if (m_current.type == Pencil || m_current.type == Marker) {
            m_current.points.append(p);
        } else if (m_current.points.size() >= 2) {
            m_current.points[1] = p;
        } else if (!m_current.points.isEmpty()) {
            // 两点图形（矩形/椭圆/箭头/直线）：按下时只有 1 个点，
            // 第一次移动必须 append 出第二个点，否则 points[1] 越界
            // （曾导致堆损坏、钉图工具条点击清空后闪退）
            m_current.points.append(p);
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
        commitCurrent();
    }
    event->accept();
}

void PinAnnotator::keyPressEvent(QKeyEvent* event)
{
    // 文字编辑：可打印字符入栈、退格删除、回车/Esc 提交
    if (m_textEditing && m_current.type == Text) {
        const QString text = event->text();
        if (!text.isEmpty() && text.at(0).isPrint()) {
            m_current.text += text;
        } else if (event->key() == Qt::Key_Backspace) {
            m_current.text.chop(1);
        } else if (event->key() == Qt::Key_Return ||
                   event->key() == Qt::Key_Enter ||
                   event->key() == Qt::Key_Escape) {
            commitPendingText();
        } else {
            event->ignore();
            return;
        }
        update();
        event->accept();
        return;
    }
    event->ignore();
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
