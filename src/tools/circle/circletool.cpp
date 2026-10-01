// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "circletool.h"
#include "src/utils/confighandler.h"
#include <QPainter>

CircleTool::CircleTool(QObject* parent)
  : AbstractTwoPointTool(parent)
{
    m_supportsDiagonalAdj = true;
}

QIcon CircleTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "circle-outline.svg");
}
QString CircleTool::name() const
{
    return tr("Circle");
}

CaptureTool::Type CircleTool::type() const
{
    return CaptureTool::TYPE_CIRCLE;
}

QString CircleTool::description() const
{
    return tr("Set the Circle as the paint tool");
}

CaptureTool* CircleTool::copy(QObject* parent)
{
    auto* tool = new CircleTool(parent);
    copyParams(this, tool);
    return tool;
}

void CircleTool::process(QPainter& painter, const QPixmap& pixmap)
{
    Q_UNUSED(pixmap)
    QBrush orig_brush = painter.brush();
    painter.setPen(QPen(color(), size()));
    // flameshot-ocr: 填充开关（关 = 只显示边框）
    painter.setBrush(ConfigHandler().shapeFill() ? QBrush(color())
                                                 : Qt::NoBrush);
    painter.drawEllipse(QRect(points().first, points().second).normalized());
    painter.setBrush(orig_brush);
}

void CircleTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
}
