// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "pathinfo.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>

const QString PathInfo::whiteIconPath()
{
    return QStringLiteral(":/img/material/white/");
}

const QString PathInfo::blackIconPath()
{
    return QStringLiteral(":/img/material/black/");
}

QStringList PathInfo::translationsPaths()
{
    QString binaryPath =
      QFileInfo(qApp->applicationDirPath()).absoluteFilePath();
    QString trPath = QDir::toNativeSeparators(binaryPath + "/translations");
#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    // flameshot-ocr: AppImage 挂载点布局 <mount>/usr/bin/flameshot——
    // 二进制旁的 ../share/flameshot/translations 优先于编译期前缀
    // （/usr 在 AppImage 内不存在；宿主恰好有 deb 安装时会误加载旧翻译）
    return QStringList()
           << QDir::toNativeSeparators(binaryPath +
                                       "/../share/flameshot/translations")
           << QStringLiteral(APP_PREFIX) + "/share/flameshot/translations"
           << trPath << QStringLiteral("/usr/share/flameshot/translations")
           << QStringLiteral("/usr/local/share/flameshot/translations");
#endif
    return QStringList() << trPath;
}
