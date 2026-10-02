// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "globalvalues.h"
#include "src/utils/confighandler.h"
#include <QApplication>
#include <QFontMetrics>

#if defined(Q_OS_MACOS)
#include <QOperatingSystemVersion>
#endif

int GlobalValues::buttonBaseSize()
{
    // flameshot-ocr: toolbarIconSize 配置优先（16-48）；未定制时保持
    // 上游的字体联动尺寸
    const int configured = ConfigHandler().toolbarIconSize();
    if (configured >= 16 && configured <= 48) {
        return configured;
    }
    return QFontMetrics(qApp->font()).lineSpacing() * 2.2;
}

QString GlobalValues::versionInfo()
{
    return QStringLiteral("Flameshot " APP_VERSION " (" FLAMESHOT_GIT_HASH ")"
                          "\nCompiled with Qt " QT_VERSION_STR);
}

QString GlobalValues::iconPath()
{
#if USE_MONOCHROME_ICON
    return QString(":img/app/flameshot.monochrome.svg");
#else
    return { ":img/app/flameshot.svg" };
#endif
}

QString GlobalValues::iconPathPNG()
{
#if USE_MONOCHROME_ICON
    return QString(":img/app/flameshot.monochrome.png");
#else
    return { ":img/app/flameshot.png" };
#endif
}

QString GlobalValues::trayIconPath()
{
#if USE_MONOCHROME_ICON
#if defined(Q_OS_MACOS)
    auto currentMacOsVersion = QOperatingSystemVersion::current();
    if (currentMacOsVersion >= QOperatingSystemVersion::MacOSBigSur) {
        return { ":img/app/flameshot.mask.png" };
    } else {
        return { ":img/app/flameshot.monochrome.png" };
    }
#else
    return { ":img/app/flameshot.monochrome.png" };
#endif
#else
    return { ":img/app/flameshot.png" };
#endif
}
