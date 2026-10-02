// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#include "pinannotator.h"
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLineF>
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
    if (m_tool == None) {
        setCursor(Qt::ArrowCursor);
    } else if (m_tool == Eraser) {
        setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

void PinAnnotator::setColor(const QColor& color)
{
    m_color = color;
}

void PinAnnotator::setWidth(int width)
{
    m_width = width;
}

void PinAnnotator::setFill(bool filled)
{
    m_fill = filled;
    // 与截图工具栏行为一致：切换后已有矩形/椭圆同步更新
    for (PinShape& s : m_shapes) {
        if (s.type == Rectangle || s.type == Ellipse) {
            s.filled = filled;
        }
    }
    if (m_current.type == Rectangle || m_current.type == Ellipse) {
        m_current.filled = filled;
    }
    update();
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
    m_preedit.clear();
    m_current = PinShape{};
    setAttribute(Qt::WA_InputMethodEnabled, false);
    clearFocus();
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
    Q_UNUSED(oldHeight)
    // QTransform().rotate(90) 的映射：(x, y) -> (oldHeight - y, x)
    for (PinShape& s : m_shapes) {
        for (QPointF& p : s.points) {
            p = QPointF(oldHeight - p.y(), p.x());
        }
        // 旋转后点集变化：bbox 缓存与马赛克像素缓存全部失效
        s.boundsValid = false;
        s.pixelateCacheRev = -1;
    }
    invalidateBaseCache();
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
        s.boundsValid = false;
        s.pixelateCacheRev = -1;
    }
    invalidateBaseCache();
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
    // displayScale = 底图 raw 像素 / 逻辑像素（含 DPR），
    // widget 逻辑坐标 × displayScale = base raw 坐标
    return QPointF(widgetPos) * m_displayScale;
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
            // 审查报告 §9：bbox 缓存——提交后的形状几何不再变化（仅旋转
            // 时整体失效），橡皮擦的 O(形状数×点数) 扫描降为首次计算后
            // 的 O(形状数) 查表；m_current（绘制中）不缓存
            if (&s != &m_current && s.boundsValid) {
                return s.cachedBounds.adjusted(-s.width, -s.width, s.width,
                                               s.width);
            }
            QRectF r(s.points.first(), s.points.first());
            for (const QPointF& p : s.points) {
                r = r.united(QRectF(p, p));
            }
            if (&s != &m_current) {
                s.cachedBounds = r;
                s.boundsValid = true;
            }
            return r.adjusted(-s.width, -s.width, s.width, s.width);
        }
        case Text: {
            QFont font;
            font.setPixelSize(TEXT_FONT_PX);
            const QFontMetrics metrics(font);
            QString shown = s.text;
            if (&s == &m_current && m_textEditing) {
                shown += m_preedit + QStringLiteral("|");
            }
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

// 单个形状渲染（renderShapes 的循环体；const：缓存写入走 mutable 成员）
void PinAnnotator::renderOneShape(QPainter& painter, const PinShape& s) const
{
    if (s.type == Text) {
        QFont font;
        font.setPixelSize(TEXT_FONT_PX);
        painter.setFont(font);
        painter.setPen(QPen(s.color));
        const QString shown =
          s.text + (m_textEditing ? QStringLiteral("|") : QString());
        painter.drawText(s.points.first() + QPointF(0, TEXT_FONT_PX * 0.8),
                         shown);
        return;
    }
    if (s.type == Number) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(s.color));
        painter.drawEllipse(s.points.first(), NUMBER_RADIUS, NUMBER_RADIUS);
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
        return;
    }
    QPen pen(s.color, s.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
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
                    const qreal headLen = qMax(ARROW_HEAD, s.width * 3.0);
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
                painter.setPen(QPen(s.color, qMax(2, s.width), Qt::SolidLine,
                                    Qt::SquareCap, Qt::RoundJoin));
                painter.setBrush(s.filled ? QBrush(s.color) : Qt::NoBrush);
                painter.drawRect(
                  QRectF(s.points.first(), s.points.last()).normalized());
            }
            break;
        case Ellipse:
            if (s.points.size() >= 2) {
                painter.setBrush(s.filled ? QBrush(s.color) : Qt::NoBrush);
                painter.drawEllipse(
                  QRectF(s.points.first(), s.points.last()).normalized());
            }
            break;
        case Pixelate:
            if (s.points.size() >= 2 && m_basePixmap &&
                !m_basePixmap->isNull()) {
                const QRectF r =
                  QRectF(s.points.first(), s.points.last()).normalized();
                const QRect src =
                  r.toRect().intersected(m_basePixmap->rect());
                if (!src.isEmpty()) {
                    const int block = qMax(6, s.width * 4);
                    // 审查报告 §6：马赛克结果缓存——提交后的形状在
                    // 后续 repaint（移动/透明度/其它标注编辑）中直接
                    // 复用像素，仅当底图版本/区域尺寸/块大小变化时重建
                    if (s.pixelateCacheRev != m_baseRev ||
                        s.pixelateCacheSize != src.size() ||
                        s.pixelateCacheBlock != block ||
                        s.pixelateCache.isNull()) {
                        const QSize smallSize(qMax(1, src.width() / block),
                                              qMax(1, src.height() / block));
                        const QImage region =
                          m_basePixmap->toImage().copy(src);
                        s.pixelateCache =
                          region.scaled(smallSize, Qt::IgnoreAspectRatio,
                                        Qt::FastTransformation)
                            .scaled(src.size(), Qt::IgnoreAspectRatio,
                                    Qt::FastTransformation);
                        s.pixelateCacheRev = m_baseRev;
                        s.pixelateCacheSize = src.size();
                        s.pixelateCacheBlock = block;
                    }
                    painter.drawImage(src, s.pixelateCache);
                }
            }
            break;
        default:
            break;
    }
}

void PinAnnotator::renderShapes(QPainter& painter, qreal scale) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.save();
    painter.scale(scale, scale);

    // 审查报告 §5.1：直接遍历原容器，不再每次 repaint 复制整个
    // QVector<PinShape>（旧实现 m_shapes → all 的拷贝是绘制热路径上
    // 最稳定的无谓开销）
    for (const PinShape& s : m_shapes) {
        renderOneShape(painter, s);
    }
    if (m_drawing || m_textEditing) {
        renderOneShape(painter, m_current);
    }
    painter.restore();
}

void PinAnnotator::paintAnnotations(QPainter& painter) const
{
    renderShapes(painter, 1.0);
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
            m_current.filled = m_fill;
            m_textEditing = true;
            m_drawing = false;
            setAttribute(Qt::WA_InputMethodEnabled, true);
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
            if (m_tool == Rectangle || m_tool == Ellipse) {
                m_current.filled = m_fill;
            }
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
            // 审查报告 §8：点抽稀——高采样率鼠标轨迹仅在距上点
            // ≥1.5 基础像素时记录，长笔画点数降一个数量级且视觉无损
            if (m_current.points.isEmpty() ||
                QLineF(m_current.points.last(), p).length() >= 1.5) {
                m_current.points.append(p);
            }
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
        // 抽稀补偿：收笔时补记最终位置，保证线条终点与指针一致
        if (m_current.type == Pencil || m_current.type == Marker) {
            const QPointF p = toBase(event->pos());
            if (m_current.points.isEmpty() ||
                QLineF(m_current.points.last(), p).length() > 0.01) {
                m_current.points.append(p);
            }
        }
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
            if (!m_preedit.isEmpty()) {
                m_preedit.clear();
            } else {
                m_current.text.chop(1);
            }
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

// 输入法（拼音等）：预编辑串实时渲染，提交串落盘
void PinAnnotator::inputMethodEvent(QInputMethodEvent* event)
{
    if (!m_textEditing || m_current.type != Text) {
        event->ignore();
        return;
    }
    if (!event->commitString().isEmpty()) {
        m_current.text += event->commitString();
    }
    m_preedit = event->preeditString();
    update();
    event->accept();
}

QVariant PinAnnotator::inputMethodQuery(Qt::InputMethodQuery query) const
{
    // 输入法候选窗定位：跟随文字末尾
    if (m_textEditing && m_current.type == Text) {
        switch (query) {
            case Qt::ImEnabled:
                return true;
            case Qt::ImCursorRectangle: {
                QFont font;
                font.setPixelSize(TEXT_FONT_PX);
                const QFontMetrics metrics(font);
                const qreal w = metrics.horizontalAdvance(
                  m_current.text + m_preedit);
                return QRectF(m_current.points.first() +
                                QPointF(0, TEXT_FONT_PX * 0.8),
                              QSizeF(w + 4, TEXT_FONT_PX * 1.4));
            }
            case Qt::ImSurroundingText:
                return m_current.text;
            case Qt::ImCursorPosition:
                return m_current.text.length();
            default:
                break;
        }
    }
    return QWidget::inputMethodQuery(query);
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
