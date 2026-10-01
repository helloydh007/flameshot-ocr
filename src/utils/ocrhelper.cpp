// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#include "ocrhelper.h"
#include "src/utils/confighandler.h"
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace OcrHelper
{
void run(const QImage& image,
         QObject* guard,
         std::function<void(bool ok, const QString& result)> callback)
{
    if (!guard) {
        return;
    }
    auto* proc = new QProcess(guard);
    auto* tmp = new QTemporaryFile(guard);
    tmp->setFileTemplate(
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/flameshot-ocr-XXXXXX.png"));
    if (!tmp->open() || !image.save(tmp, "PNG")) {
        callback(false, QStringLiteral("Cannot write temporary image"));
        tmp->deleteLater();
        proc->deleteLater();
        return;
    }

    QString command = ConfigHandler().ocrCommand();
    command.replace(QStringLiteral("%i"), tmp->fileName());
    QStringList args = QProcess::splitCommand(command);
    if (args.isEmpty()) {
        callback(false, QStringLiteral("Empty ocrCommand"));
        tmp->deleteLater();
        proc->deleteLater();
        return;
    }
    const QString program = args.takeFirst();

    QObject::connect(proc,
                     &QProcess::errorOccurred,
                     proc,
                     [callback](QProcess::ProcessError error) {
                         if (error == QProcess::FailedToStart) {
                             callback(false,
                                      QStringLiteral("tesseract: "
                                                     "FailedToStart"));
                         }
                     });
    QObject::connect(
      proc,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      proc,
      [proc, tmp, callback](int exitCode, QProcess::ExitStatus status) {
          if (status != QProcess::NormalExit || exitCode != 0) {
              const QString err =
                QString::fromLocal8Bit(proc->readAllStandardError())
                  .simplified();
              callback(false, err.left(300));
              return;
          }
          QString text = QString::fromUtf8(proc->readAllStandardOutput());
          text.remove(QChar('\f'));
          text.replace(QRegularExpression(QStringLiteral("\\n{3,}")),
                       QStringLiteral("\n\n"));
          callback(true, text.trimmed());
      });

    proc->start(program, args);
}
}
