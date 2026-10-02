// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#include "uitheme.h"
#include "src/utils/confighandler.h"

#include <QApplication>
#include <QGuiApplication>
#include <QStyle>
#include <QStyleHints>

namespace
{
QString normalizedTheme()
{
    const QString v = ConfigHandler().uiTheme().toLower();
    return (v == QLatin1String("light") || v == QLatin1String("dark"))
             ? v
             : QStringLiteral("system");
}

QString normalizedPanelPos()
{
    const QString v = ConfigHandler().ocrPanelPosition().toLower();
    return (v == QLatin1String("left") || v == QLatin1String("right") ||
            v == QLatin1String("top") || v == QLatin1String("bottom"))
             ? v
             : QStringLiteral("auto");
}
}

namespace UiTheme
{
Mode themeMode()
{
    const QString v = normalizedTheme();
    if (v == QLatin1String("light")) {
        return Mode::Light;
    }
    if (v == QLatin1String("dark")) {
        return Mode::Dark;
    }
    return Mode::System;
}

bool isDarkTheme()
{
    switch (themeMode()) {
        case Mode::Dark:
            return true;
        case Mode::Light:
            return false;
        case Mode::System:
        default:
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            if (auto* hints = QGuiApplication::styleHints()) {
                return hints->colorScheme() == Qt::ColorScheme::Dark;
            }
#endif
            return true; // 无法检测时保持原暗色观感
    }
}

QColor panelBg()
{
    return isDarkTheme() ? QColor(26, 26, 31) : QColor(246, 246, 248);
}

QColor panelBorder()
{
    return isDarkTheme() ? QColor(63, 63, 70) : QColor(198, 198, 206);
}

QColor panelFg()
{
    return isDarkTheme() ? QColor(236, 236, 236) : QColor(36, 36, 40);
}

QColor panelFgDim()
{
    return isDarkTheme() ? QColor(154, 154, 162) : QColor(109, 109, 118);
}

QColor hoverBg()
{
    return isDarkTheme() ? QColor(63, 63, 70) : QColor(224, 224, 230);
}

QColor inputBg()
{
    return isDarkTheme() ? QColor(38, 38, 43) : QColor(255, 255, 255);
}

QColor copyTextFg()
{
    // 强调（accent）按钮上的文字：亮色主题下 accent 多为深色底，仍用白字
    return QColor(255, 255, 255);
}

int iconSize()
{
    return ConfigHandler().toolbarIconSize();
}

QString iconDir()
{
    return isDarkTheme() ? QStringLiteral(":/img/material/white/")
                         : QStringLiteral(":/img/material/black/");
}

void applyApplicationPalette()
{
    // CLI 模式（flameshot full/config 直通）是 QCoreApplication，
    // setStyle/setPalette 是 Widgets API——在那种进程里直接跳过
    if (!qobject_cast<QApplication*>(qApp)) {
        return;
    }
    // light/dark：Fusion + 定制调色板（设置窗口等原生控件随之变色）。
    // system：恢复 Fusion 默认调色板，让平台主题接管。
    if (themeMode() != Mode::System) {
        qApp->setStyle(QStringLiteral("Fusion"));
    }
    QPalette pal;
    if (themeMode() == Mode::Dark) {
        pal.setColor(QPalette::Window, QColor(37, 37, 41));
        pal.setColor(QPalette::WindowText, QColor(232, 232, 236));
        pal.setColor(QPalette::Base, QColor(28, 28, 32));
        pal.setColor(QPalette::AlternateBase, QColor(37, 37, 41));
        pal.setColor(QPalette::Text, QColor(232, 232, 236));
        pal.setColor(QPalette::Button, QColor(45, 45, 50));
        pal.setColor(QPalette::ButtonText, QColor(232, 232, 236));
        pal.setColor(QPalette::Highlight, QColor(0, 122, 255));
        pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        pal.setColor(QPalette::ToolTipBase, QColor(45, 45, 50));
        pal.setColor(QPalette::ToolTipText, QColor(232, 232, 236));
        pal.setColor(QPalette::PlaceholderText, QColor(140, 140, 148));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 126));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(120, 120, 126));
    } else {
        pal.setColor(QPalette::Window, QColor(245, 245, 247));
        pal.setColor(QPalette::WindowText, QColor(30, 30, 34));
        pal.setColor(QPalette::Base, QColor(255, 255, 255));
        pal.setColor(QPalette::AlternateBase, QColor(245, 245, 247));
        pal.setColor(QPalette::Text, QColor(30, 30, 34));
        pal.setColor(QPalette::Button, QColor(255, 255, 255));
        pal.setColor(QPalette::ButtonText, QColor(30, 30, 34));
        pal.setColor(QPalette::Highlight, QColor(0, 122, 255));
        pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        pal.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
        pal.setColor(QPalette::ToolTipText, QColor(30, 30, 34));
        pal.setColor(QPalette::PlaceholderText, QColor(140, 140, 148));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(150, 150, 156));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(150, 150, 156));
    }
    qApp->setPalette(pal);
}

PanelPos panelPosFromString(const QString& s)
{
    const QString v = s.toLower();
    if (v == QLatin1String("left")) {
        return PanelPos::Left;
    }
    if (v == QLatin1String("right")) {
        return PanelPos::Right;
    }
    if (v == QLatin1String("top")) {
        return PanelPos::Top;
    }
    if (v == QLatin1String("bottom")) {
        return PanelPos::Bottom;
    }
    return PanelPos::Auto;
}

PanelPos ocrPanelPosition()
{
    return panelPosFromString(normalizedPanelPos());
}
}
