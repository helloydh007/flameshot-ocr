// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#pragma once

#include <QColor>

class QString;

// 界面主题令牌：亮色/暗色/跟随系统（uiTheme 配置）。
// 自定义绘制的 UI（OCR 面板、预选工具条、钉图工具条等）从这里取色，
// 不再各自硬编码；窗口为短生命周期（每次截图重建），取值即取即用。
namespace UiTheme
{
enum class Mode
{
    System,
    Light,
    Dark
};

enum class PanelPos
{
    Auto,
    Left,
    Right,
    Top,
    Bottom
};

Mode themeMode();
// 当前应使用暗色：显式 dark，或 system 且系统配色为暗
bool isDarkTheme();

// 面板与工具条令牌
QColor panelBg();      // 面板底色（不透明）
QColor panelBorder();  // 面板描边
QColor panelFg();      // 主文字
QColor panelFgDim();   // 次要文字/状态栏
QColor hoverBg();      // 悬停底色
QColor inputBg();      // 文本输入底色
QColor copyTextFg();   // 强调按钮上的文字

// 工具条图标基准尺寸（toolbarIconSize 配置，16-48，默认 24）
int iconSize();

// OCR 结果面板相对选区的位置（ocrPanelPosition 配置）
PanelPos ocrPanelPosition();
PanelPos panelPosFromString(const QString& s);
}
