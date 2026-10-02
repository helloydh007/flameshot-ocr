// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#pragma once

#include <QColor>
class QPainter;
#include <QPoint>
#include <QPointF>
#include <QWidget>
#include <QVector>

// 钉图标注层：透明子控件，覆盖在钉图 QLabel 上方。
// - 矢量形状存放在“PinWidget 当前 m_pixmap 坐标系”，跟随旋转/缩放
// - 未选工具时，按下交给 PinWidget 移动窗口；右键弹出钉图菜单
// - 工具集与截图界面一致：画笔/荧光笔/箭头/矩形/椭圆/直线/序号/文字/
//   马赛克/橡皮擦；文字输入支持拼音等输入法（IME）
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
        Line,
        Text,
        Number,
        Eraser,
        Pixelate
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
    int shapeCount() const { return m_shapes.size(); }
    void undo();
    void redo();
    bool canRedo() const { return !m_redoShapes.isEmpty(); }
    // 提交正在输入的文字（复制/保存/切换工具前调用）
    void commitPendingText();
    void clearShapes();

    // 形状填充开关（矩形/椭圆；与截图工具栏共享 shapeFill 配置）
    void setFill(bool filled);
    bool fill() const { return m_fill; }

    // 马赛克需要采样底图（指向 PinWidget 的 m_pixmap，旋转时内容原地更新）
    void setBasePixmap(const QPixmap* base)
    {
        m_basePixmap = base;
        invalidateBaseCache();
    }

    // 底图内容变化（旋转/换图）时使渲染缓存失效；PinWidget 旋转后调用
    void invalidateBaseCache() { ++m_baseRev; }

    // 直接把全部标注画到给定 painter 上（目标应为 m_pixmap 尺寸 1:1）。
    // 复制/保存合成用——替代“先渲染整张透明 overlay 再合成”的旧路径
    // （审查报告 §7：4K 下省一张 ~32MB 的临时 QImage）
    void paintAnnotations(QPainter& painter) const;

    qreal displayScale() const { return m_displayScale; }
    QString lastShapeText() const
    {
        return m_shapes.isEmpty() ? QString() : m_shapes.last().text;
    }

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
    void keyPressEvent(QKeyEvent* event) override;
    // 输入法（拼音等）：提交串 + 预编辑串
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct PinShape
    {
        Tool type = None;
        QColor color;
        int width = 4;
        QVector<QPointF> points;
        QString text;
        int number = 0;
        bool filled = false;

        // 渲染/命中缓存（mutable：renderShapes/shapeRect 为 const）
        // 马赛克像素缓存：底图版本、区域尺寸、块大小任一变化即重建
        mutable QImage pixelateCache;
        mutable QSize pixelateCacheSize;
        mutable int pixelateCacheBlock = 0;
        mutable qint64 pixelateCacheRev = -1;
        // 几何包围盒缓存（不含笔宽 padding；旋转时整体失效）
        mutable QRectF cachedBounds;
        mutable bool boundsValid = false;
    };

    QRectF shapeRect(const PinShape& s) const;
    void renderShapes(QPainter& painter, qreal scale) const;
    void renderOneShape(QPainter& painter, const PinShape& s) const;
    QPointF toBase(const QPoint& widgetPos) const;
    void commitCurrent();
    void pushCurrentToRedo(int index);

    QVector<PinShape> m_shapes;
    QVector<PinShape> m_redoShapes;
    PinShape m_current;
    bool m_drawing = false;
    bool m_textEditing = false;
    bool m_fill = false;
    Tool m_tool = None;
    QColor m_color = Qt::red;
    int m_width = 4;
    int m_counter = 1;
    qreal m_displayScale = 1.0;
    QString m_preedit;
    const QPixmap* m_basePixmap = nullptr;
    // 底图版本号：旋转/换底图时 +1，马赛克缓存据此失效
    qint64 m_baseRev = 0;
};
