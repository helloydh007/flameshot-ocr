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
#include <QTimer>

namespace
{
// OCR 引擎超时（ocrCommand 可配置，引擎可能卡死；30s 足够整屏 tesseract）
constexpr int OCR_TIMEOUT_MS = 30'000;
// terminate() 后等待正常退出的宽限期，超过则 SIGKILL
constexpr int OCR_GRACE_MS = 1'500;
// 识别输出上限（防异常 ocrCommand 输出巨量数据推高 GUI 内存）
constexpr qsizetype OCR_OUTPUT_CAP = 8 * 1024 * 1024;

// Collapse 3+ consecutive newlines in OCR output to one blank line.
// static：避免每次识别都重新编译正则（审查报告 3.2）
void normalizeOcrText(QString& text)
{
    static const QRegularExpression blankLines(QStringLiteral("\\n{3,}"));
    text.remove(QChar('\f'));
    text.replace(blankLines, QStringLiteral("\n\n"));
    if (text.size() > OCR_OUTPUT_CAP) {
        text.truncate(OCR_OUTPUT_CAP);
        text += QStringLiteral("\n…(输出过大已截断)");
    }
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

OcrTask::OcrTask(QObject* parent)
  : QObject(parent)
{
}

OcrTask::~OcrTask()
{
    // guard 销毁路径：立即终止外部进程，回收临时文件（子对象随父销毁）
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
}

void OcrTask::cancel()
{
    if (m_done) {
        return;
    }
    m_superseded = true;
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        // finished 信号稍后到达 → finish(invokeCallback=false) 静默回收
        terminateWithGrace();
    } else {
        finish(false, QString(), /*invokeCallback=*/false);
    }
}

void OcrTask::terminateWithGrace()
{
    if (!m_proc || m_proc->state() == QProcess::NotRunning) {
        return;
    }
    m_proc->terminate();
    auto* grace = new QTimer(this);
    grace->setSingleShot(true);
    connect(grace, &QTimer::timeout, this, [this, grace]() {
        grace->deleteLater();
        if (!m_done && m_proc &&
            m_proc->state() != QProcess::NotRunning) {
            m_proc->kill(); // 引擎忽略 SIGTERM 时兜底强杀
        }
    });
    grace->start(OCR_GRACE_MS);
}

void OcrTask::finish(bool ok, const QString& result, bool invokeCallback)
{
    if (m_done) {
        return;
    }
    m_done = true;
    if (m_timeoutTimer) {
        m_timeoutTimer->stop();
    }
    if (invokeCallback && m_callback) {
        m_callback(ok, result);
    }
    // 置空非常关键：对象 deleteLater 后指针悬空，~OcrTask（guard 销毁
    // 路径）或后续 finish 再访问会 use-after-free（实测段错误）
    if (m_tmp) {
        m_tmp->deleteLater();
        m_tmp = nullptr;
    }
    if (m_proc) {
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

void OcrTask::start(const QImage& image, const QString& command)
{
    // m_proc 此处必为 nullptr（start 只会被调用一次，重复调用无入口）
    // cppcheck-suppress publicAllocationError
    m_proc = new QProcess(this);
    // 审查报告 4.1：文件名带 PID，便于多实例时排查与清理
    m_tmp = new QTemporaryFile(this);
    m_tmp->setFileTemplate(
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/flameshot-ocr-%1-XXXXXX.png")
        .arg(QCoreApplication::applicationPid()));
    if (!m_tmp->open() || !image.save(m_tmp, "PNG")) {
        finish(false, QStringLiteral("Cannot write temporary image"), true);
        return;
    }
    // 确保数据落盘后外部进程立即可读；提前关闭句柄（文件仍由对象管理）
    m_tmp->flush();
    m_tmp->close();

    // 审查报告 §12：先按 shell 规则拆分命令，再做 token 内的 %i 替换，
    // 临时路径含空格时不会被二次拆分
    QStringList args = QProcess::splitCommand(command);
    if (args.isEmpty()) {
        finish(false, QStringLiteral("Empty ocrCommand"), true);
        return;
    }
    for (QString& arg : args) {
        arg.replace(QStringLiteral("%i"), m_tmp->fileName());
    }
    m_program = args.takeFirst();

    // 审查报告 §2.2/§3：世代计数只丢弃旧结果、不停止旧进程——这里把
    // 任务升级为可取消对象，并提供超时保护。连接用 Qt::QueuedConnection：
    // 若 guard 在 OCR 进行中销毁，~QProcess 内部 kill 会同步发出 finished，
    // 直接连接会让回调触摸已销毁的面板（实测段错误）；排队连接则会被
    // Qt 的事件清除机制安全吞掉。
    QObject::connect(
      m_proc,
      &QProcess::errorOccurred,
      m_proc,
      [this](QProcess::ProcessError error) {
          if (error == QProcess::FailedToStart) {
              finish(false,
                     QStringLiteral("OCR %1: failed to start "
                                    "(检查 ocrCommand 及已安装的引擎)")
                       .arg(m_program),
                     !m_superseded);
          }
          // 其它错误之后仍会发出 finished，由 finished 统一清理
      },
      Qt::QueuedConnection);
    QObject::connect(
      m_proc,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      m_proc,
      [this](int exitCode, QProcess::ExitStatus status) {
          if (status != QProcess::NormalExit || exitCode != 0) {
              const QString err =
                QString::fromLocal8Bit(m_proc->readAllStandardError());
              finish(false, formatOcrError(m_program, exitCode, err),
                     !m_superseded);
          } else {
              QString text =
                QString::fromUtf8(m_proc->readAllStandardOutput());
              normalizeOcrText(text);
              finish(true, text.trimmed(), !m_superseded);
          }
      },
      Qt::QueuedConnection);

    // 超时：terminate → 宽限 → kill，立即按失败回调（除非已被新任务取代）
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_done) {
            return;
        }
        terminateWithGrace();
        finish(false,
               QStringLiteral("OCR %1: 超时（%2 秒）已终止")
                 .arg(m_program)
                 .arg(OCR_TIMEOUT_MS / 1000),
               !m_superseded);
    });
    m_timeoutTimer->start(OCR_TIMEOUT_MS);

    m_proc->start(m_program, args);
}

namespace OcrHelper
{
OcrTask* run(const QImage& image,
             QObject* guard,
             const OcrTask::Callback& callback)
{
    if (!guard) {
        return nullptr;
    }
    auto* task = new OcrTask(guard);
    task->m_callback = callback;
    task->start(image, ConfigHandler().ocrCommand());
    return task;
}
}
