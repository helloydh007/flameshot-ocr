// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 flameshot-ocr contributors

#include "ocrhelper.h"
#include "src/utils/confighandler.h"
#include <QCoreApplication>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace
{
// Collapse 3+ consecutive newlines in OCR output to one blank line.
// static：避免每次识别都重新编译正则（审查报告 3.2）
void normalizeOcrText(QString& text)
{
    static const QRegularExpression blankLines(QStringLiteral("\\n{3,}"));
    text.remove(QChar('\f'));
    text.replace(blankLines, QStringLiteral("\n\n"));
}

// 审查报告 4.3：tesseract 缺语言包时的 stderr 特征 → 追加安装提示
QString languagePackHint(const QString& stderrText)
{
    const QLatin1String markers[] = {
        QLatin1String("opening data file"),
        QLatin1String("couldn't load any languages"),
        QLatin1String("failed loading language"),
        QLatin1String("read_params_file"),
    };
    const QString lower = stderrText.toLower();
    for (const auto& marker : markers) {
        if (lower.contains(marker)) {
            return QStringLiteral(
              "\n提示：可能缺少 OCR 语言包，可安装：\n"
              "  sudo apt install tesseract-ocr-chi-sim tesseract-ocr-eng");
        }
    }
    return QString();
}

// 审查报告 4.2：统一的错误信息格式，便于排查
QString formatOcrError(const QString& program,
                       int exitCode,
                       const QString& stderrText)
{
    QString err = stderrText.simplified();
    if (err.isEmpty()) {
        err = QStringLiteral("exit code %1").arg(exitCode);
    }
    QString msg = QStringLiteral("OCR %1: %2").arg(program, err.left(300));
    msg += languagePackHint(stderrText);
    return msg;
}
}

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
    // 审查报告 4.1：文件名带 PID，便于多实例时排查与清理
    auto* tmp = new QTemporaryFile(guard);
    tmp->setFileTemplate(
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/flameshot-ocr-%1-XXXXXX.png")
        .arg(QCoreApplication::applicationPid()));
    if (!tmp->open() || !image.save(tmp, "PNG")) {
        callback(false, QStringLiteral("Cannot write temporary image"));
        tmp->deleteLater();
        proc->deleteLater();
        return;
    }
    // 确保数据落盘后外部进程立即可读（审查报告 2.1）
    tmp->flush();

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

    // 审查报告 1.1（高优先级）：QProcess/QTemporaryFile 以 guard 为父对象
    // 但回调后从不销毁 —— 每次识别都会在钉图存活期间累积一对对象和临时
    // 文件。现在每条结束路径都 deleteLater，任务完成后立即回收。
    //
    // 连接必须用 Qt::QueuedConnection：若用户在 OCR 进行中关闭钉图/截图，
    // ~QProcess 会在析构内 kill+waitForFinished 并同步发出 finished，
    // 直接连接会让回调触摸已销毁的面板（实测段错误）。排队连接把回调
    // 投递到事件循环，而正在析构的对象的待投递事件会被 Qt 自动清除，
    // 从根本上保证销毁路径安全。
    QObject::connect(
      proc,
      &QProcess::errorOccurred,
      proc,
      [proc, tmp, callback, program](QProcess::ProcessError error) {
          if (error == QProcess::FailedToStart) {
              callback(false,
                       QStringLiteral("OCR %1: failed to start "
                                      "(检查 ocrCommand 及已安装的引擎)")
                         .arg(program));
              tmp->deleteLater();
              proc->deleteLater();
          }
          // 其它错误（Crashed/Timedout 等）之后仍会发出 finished，
          // 由 finished 回调统一清理，避免双重销毁。
      },
      Qt::QueuedConnection);
    QObject::connect(
      proc,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      proc,
      [proc, tmp, callback, program](int exitCode,
                                     QProcess::ExitStatus status) {
          if (status != QProcess::NormalExit || exitCode != 0) {
              const QString err =
                QString::fromLocal8Bit(proc->readAllStandardError());
              callback(
                false, formatOcrError(program, exitCode, err));
          } else {
              QString text =
                QString::fromUtf8(proc->readAllStandardOutput());
              normalizeOcrText(text);
              callback(true, text.trimmed());
          }
          tmp->deleteLater();
          proc->deleteLater();
      },
      Qt::QueuedConnection);

    proc->start(program, args);
}
}
