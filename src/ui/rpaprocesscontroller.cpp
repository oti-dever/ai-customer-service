#include "rpaprocesscontroller.h"

#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {

QString stripAnsiEscapes(QString s)
{
    static const QRegularExpression kAnsiRe(QStringLiteral("\x1B\\[[0-9;?]*[ -/]*[@-~]"));
    s.remove(kAnsiRe);
    return s;
}

} // namespace

RpaProcessController::RpaProcessController(QObject* parent)
    : QObject(parent)
{
}

QString RpaProcessController::processLog(const QString& platformId) const
{
    return m_processLogs.value(platformId);
}

void RpaProcessController::clearProcessLog(const QString& platformId)
{
    if (platformId.isEmpty())
        return;
    m_processLogs.remove(platformId);
}

QStringList RpaProcessController::runningPlatformIds() const
{
    QStringList out;
    for (auto it = m_processes.constBegin(); it != m_processes.constEnd(); ++it) {
        QProcess* process = it.value();
        if (process && process->state() == QProcess::Running)
            out.append(it.key());
    }
    return out;
}

void RpaProcessController::appendProcessLog(const QString& platformId, const QString& text)
{
    if (text.isEmpty())
        return;
    constexpr int kMaxRpaLogChars = 400000;
    QString& buf = m_processLogs[platformId];
    buf.append(text);
    if (buf.size() > kMaxRpaLogChars)
        buf.remove(0, buf.size() - kMaxRpaLogChars);
    emit logAppended(platformId, text);
}

void RpaProcessController::startPlatforms(const QStringList& platformIds)
{
    const QString pythonRoot = QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/python");
    for (const QString& id : platformIds) {
        if (id != QStringLiteral("wechat") && id != QStringLiteral("qianniu") && id != QStringLiteral("pdd"))
            continue;

        if (m_processes.contains(id)) {
            QProcess* existing = m_processes.value(id);
            if (existing && existing->state() == QProcess::Running)
                continue;
            if (existing) {
                m_processes.remove(id);
                existing->disconnect();
                existing->deleteLater();
            }
        }

        auto* proc = new QProcess(this);
        proc->setProgram(QStringLiteral("python"));
        proc->setArguments(QStringList() << QStringLiteral("-m") << QStringLiteral("rpa.main")
                                         << QStringLiteral("--platform") << id);
        proc->setWorkingDirectory(pythonRoot);
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
            env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
            proc->setProcessEnvironment(env);
        }
        m_consoleDecoders.insert(id, QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8));
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, id, proc]() {
            if (m_processes.value(id) != proc)
                return;
            const QByteArray chunk = proc->readAllStandardOutput();
            if (chunk.isEmpty())
                return;
            auto it = m_consoleDecoders.find(id);
            if (it == m_consoleDecoders.end())
                return;
            QString decoded = it.value()->decode(chunk);
            if (decoded.contains(QChar::ReplacementCharacter)) {
                const QString localDecoded = stripAnsiEscapes(QString::fromLocal8Bit(chunk));
                if (!localDecoded.isEmpty()) {
                    it.value() = QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8);
                    appendProcessLog(id, localDecoded);
                    return;
                }
            }
            const QString cleaned = stripAnsiEscapes(decoded);
            if (!cleaned.isEmpty())
                appendProcessLog(id, cleaned);
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, id, proc](int exitCode, QProcess::ExitStatus status) {
                    Q_UNUSED(status)
                    if (m_processes.value(id) != proc)
                        return;
                    const QByteArray tail = proc->readAllStandardOutput();
                    auto it = m_consoleDecoders.find(id);
                    if (it != m_consoleDecoders.end()) {
                        if (!tail.isEmpty()) {
                            QString decoded = it.value()->decode(tail);
                            if (decoded.contains(QChar::ReplacementCharacter)) {
                                const QString localDecoded = stripAnsiEscapes(QString::fromLocal8Bit(tail));
                                if (!localDecoded.isEmpty()) {
                                    it.value() = QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8);
                                    appendProcessLog(id, localDecoded);
                                } else {
                                    const QString cleaned = stripAnsiEscapes(decoded);
                                    if (!cleaned.isEmpty())
                                        appendProcessLog(id, cleaned);
                                }
                            } else {
                                const QString cleaned = stripAnsiEscapes(decoded);
                                if (!cleaned.isEmpty())
                                    appendProcessLog(id, cleaned);
                            }
                        }
                        const QString flushed = stripAnsiEscapes(it.value()->decode(QByteArray()));
                        if (!flushed.isEmpty())
                            appendProcessLog(id, flushed);
                    }
                    appendProcessLog(id, QStringLiteral("\n[进程已退出，退出码 %1]\n").arg(exitCode));
                    m_processes.remove(id);
                    m_consoleDecoders.remove(id);
                    proc->deleteLater();
                });
        connect(proc, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
            qWarning() << "[RPA] process error" << id << static_cast<int>(error);
            appendProcessLog(id, QStringLiteral("\n[进程错误] code=%1\n").arg(static_cast<int>(error)));
        });

        proc->start();
        if (!proc->waitForStarted(3000)) {
            qWarning() << "[RPA] failed to start" << id;
            appendProcessLog(id, QStringLiteral("[启动失败] 无法在 PATH 中找到可用的 python，或 3 秒内未能启动。\n"));
            emit statusMessageRequested(
                QStringLiteral("启动失败：请确保已安装 Python 并在 PATH 中可用（python）。"), 5000);
            proc->deleteLater();
            continue;
        }
        appendProcessLog(id, QStringLiteral("[RPA] 已启动: python -m rpa.main --platform %1\n").arg(id));
        m_processes.insert(id, proc);
        emit statusMessageRequested(QStringLiteral("已启动 RPA：%1").arg(id), 3000);
    }
}

void RpaProcessController::stopPlatforms(const QStringList& platformIds)
{
    for (const QString& id : platformIds) {
        QProcess* proc = m_processes.take(id);
        if (!proc)
            continue;
        proc->disconnect();
        appendProcessLog(id, QStringLiteral("\n[用户请求停止]\n"));
        proc->kill();
        proc->waitForFinished(3000);
        const QByteArray tail = proc->readAllStandardOutput();
        auto it = m_consoleDecoders.find(id);
        if (it != m_consoleDecoders.end() && !tail.isEmpty()) {
            QString decoded = it.value()->decode(tail);
            if (decoded.contains(QChar::ReplacementCharacter)) {
                const QString localDecoded = stripAnsiEscapes(QString::fromLocal8Bit(tail));
                if (!localDecoded.isEmpty()) {
                    it.value() = QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8);
                    appendProcessLog(id, localDecoded);
                } else {
                    const QString cleaned = stripAnsiEscapes(decoded);
                    if (!cleaned.isEmpty())
                        appendProcessLog(id, cleaned);
                }
            } else {
                const QString cleaned = stripAnsiEscapes(decoded);
                if (!cleaned.isEmpty())
                    appendProcessLog(id, cleaned);
            }
            const QString flushed = stripAnsiEscapes(it.value()->decode(QByteArray()));
            if (!flushed.isEmpty())
                appendProcessLog(id, flushed);
        }
        m_consoleDecoders.remove(id);
        proc->deleteLater();
        emit statusMessageRequested(QStringLiteral("已停止 RPA：%1").arg(id), 3000);
    }
}
