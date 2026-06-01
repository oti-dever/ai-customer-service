#include "rpaprocessmanager.h"
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace Rpa {

RpaProcessManager& RpaProcessManager::instance()
{
    static RpaProcessManager s_instance;
    return s_instance;
}

RpaProcessManager::RpaProcessManager(QObject* parent)
    : QObject(parent)
    , m_pythonRoot(QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/python"))
{
}

RpaProcessManager::~RpaProcessManager()
{
    shutdown();
}

void RpaProcessManager::initialize()
{
    qInfo() << "[RpaProcessManager] 初始化，Python 根目录:" << m_pythonRoot;
}

void RpaProcessManager::shutdown()
{
    stopAll();
    qInfo() << "[RpaProcessManager] 已关闭";
}

PlatformStatus RpaProcessManager::status(const QString& platformId) const
{
    PlatformStatus status;
    status.platformId = platformIdFromString(platformId);
    status.displayName = platformId;

    auto it = m_processes.find(platformId);
    if (it != m_processes.end()) {
        status.state = it->state;
        status.startedAt = it->startedAt;
        status.lastError = it->lastError;
    } else {
        status.state = ProcessState::Stopped;
    }

    return status;
}

QVector<PlatformStatus> RpaProcessManager::allStatuses() const
{
    QVector<PlatformStatus> result;
    for (const QString& id : allPlatformIds()) {
        result.append(status(id));
    }
    return result;
}

QStringList RpaProcessManager::runningPlatformIds() const
{
    QStringList result;
    for (auto it = m_processes.constBegin(); it != m_processes.constEnd(); ++it) {
        if (it->state == ProcessState::Running) {
            result.append(it.key());
        }
    }
    return result;
}

QString RpaProcessManager::processLog(const QString& platformId) const
{
    return m_logs.value(platformId);
}

void RpaProcessManager::clearProcessLog(const QString& platformId)
{
    m_logs.remove(platformId);
}

void RpaProcessManager::startPlatform(const QString& platformId)
{
    startPlatforms({platformId});
}

void RpaProcessManager::stopPlatform(const QString& platformId)
{
    stopPlatforms({platformId});
}

void RpaProcessManager::startPlatforms(const QStringList& platformIds)
{
    for (const QString& id : platformIds) {
        if (!isValidPlatformId(id)) {
            qWarning() << "[RpaProcessManager] 无效的平台 ID:" << id;
            continue;
        }

        auto it = m_processes.find(id);
        if (it != m_processes.end()) {
            if (it->process && it->process->state() == QProcess::Running) {
                qDebug() << "[RpaProcessManager] 平台已在运行:" << id;
                continue;
            }
            if (it->process) {
                it->process->disconnect();
                it->process->deleteLater();
            }
            m_processes.remove(id);
        }

        auto* proc = new QProcess(this);
        proc->setProgram(QStringLiteral("python"));
        proc->setArguments({QStringLiteral("-m"), QStringLiteral("rpa.main"),
                           QStringLiteral("--platform"), id});
        proc->setWorkingDirectory(m_pythonRoot);

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
        env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
        proc->setProcessEnvironment(env);
        proc->setProcessChannelMode(QProcess::MergedChannels);

        ProcessInfo info;
        info.process = proc;
        info.decoder = QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8);
        info.state = ProcessState::Starting;
        info.startedAt = QDateTime::currentDateTime();
        m_processes[id] = info;

        updateState(id, ProcessState::Starting);

        connect(proc, &QProcess::readyReadStandardOutput, this, [this, id, proc]() {
            auto it = m_processes.find(id);
            if (it == m_processes.end() || it->process != proc) return;

            const QByteArray chunk = proc->readAllStandardOutput();
            if (chunk.isEmpty()) return;

            QString decoded = it->decoder->decode(chunk);
            if (decoded.contains(QChar::ReplacementCharacter)) {
                const QString localDecoded = QString::fromLocal8Bit(chunk);
                it->decoder = QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8);
                appendProcessLog(id, stripAnsiEscapes(localDecoded));
            } else {
                appendProcessLog(id, stripAnsiEscapes(decoded));
            }
        });

        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, id, proc](int exitCode, QProcess::ExitStatus) {
                    auto it = m_processes.find(id);
                    if (it == m_processes.end() || it->process != proc) return;

                    const QByteArray tail = proc->readAllStandardOutput();
                    if (!tail.isEmpty()) {
                        QString decoded = it->decoder->decode(tail);
                        if (decoded.contains(QChar::ReplacementCharacter)) {
                            const QString localDecoded = QString::fromLocal8Bit(tail);
                            it->decoder = QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8);
                            appendProcessLog(id, stripAnsiEscapes(localDecoded));
                        } else {
                            appendProcessLog(id, stripAnsiEscapes(decoded));
                        }
                    }

                    const QString flushed = it->decoder->decode(QByteArray());
                    if (!flushed.isEmpty()) {
                        appendProcessLog(id, stripAnsiEscapes(flushed));
                    }

                    appendProcessLog(id, QStringLiteral("\n[进程已退出，退出码 %1]\n").arg(exitCode));

                    m_processes.remove(id);
                    proc->deleteLater();

                    updateState(id, ProcessState::Stopped);
                });

        connect(proc, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
            auto it = m_processes.find(id);
            if (it != m_processes.end()) {
                it->lastError = QStringLiteral("进程错误 code=%1").arg(static_cast<int>(error));
            }
            qWarning() << "[RpaProcessManager] 进程错误:" << id << static_cast<int>(error);
            appendProcessLog(id, QStringLiteral("\n[进程错误] code=%1\n").arg(static_cast<int>(error)));
            emit errorOccurred(id, QStringLiteral("进程错误 code=%1").arg(static_cast<int>(error)));
        });

        proc->start();
        if (!proc->waitForStarted(3000)) {
            qWarning() << "[RpaProcessManager] 启动失败:" << id;
            appendProcessLog(id, QStringLiteral("[启动失败] 无法在 PATH 中找到可用的 python，或 3 秒内未能启动。\n"));
            m_processes[id].lastError = QStringLiteral("启动失败");
            m_processes.remove(id);
            proc->deleteLater();
            updateState(id, ProcessState::Error);
            emit errorOccurred(id, QStringLiteral("启动失败：请确保已安装 Python 并在 PATH 中可用"));
            continue;
        }

        appendProcessLog(id, QStringLiteral("[RPA] 已启动: python -m rpa.main --platform %1\n").arg(id));
        m_processes[id].state = ProcessState::Running;
        updateState(id, ProcessState::Running);
        qInfo() << "[RpaProcessManager] 已启动平台:" << id;
    }
}

void RpaProcessManager::stopPlatforms(const QStringList& platformIds)
{
    for (const QString& id : platformIds) {
        auto it = m_processes.find(id);
        if (it == m_processes.end() || !it->process) continue;

        QProcess* proc = it->process;
        proc->disconnect();

        updateState(id, ProcessState::Stopping);
        appendProcessLog(id, QStringLiteral("\n[用户请求停止]\n"));

        proc->kill();
        proc->waitForFinished(3000);

        const QByteArray tail = proc->readAllStandardOutput();
        if (!tail.isEmpty()) {
            QString decoded = it->decoder->decode(tail);
            if (decoded.contains(QChar::ReplacementCharacter)) {
                const QString localDecoded = QString::fromLocal8Bit(tail);
                appendProcessLog(id, stripAnsiEscapes(localDecoded));
            } else {
                appendProcessLog(id, stripAnsiEscapes(decoded));
            }
        }

        const QString flushed = it->decoder->decode(QByteArray());
        if (!flushed.isEmpty()) {
            appendProcessLog(id, stripAnsiEscapes(flushed));
        }

        m_processes.remove(id);
        proc->deleteLater();

        updateState(id, ProcessState::Stopped);
        qInfo() << "[RpaProcessManager] 已停止平台:" << id;
    }
}

void RpaProcessManager::stopAll()
{
    stopPlatforms(m_processes.keys());
}

void RpaProcessManager::appendProcessLog(const QString& platformId, const QString& text)
{
    if (text.isEmpty()) return;

    QString& buf = m_logs[platformId];
    buf.append(text);
    if (buf.size() > m_maxLogChars) {
        buf.remove(0, buf.size() - m_maxLogChars);
    }
    emit logAppended(platformId, text);
}

void RpaProcessManager::updateState(const QString& platformId, ProcessState state)
{
    emit platformStateChanged(platformId, state);
}

QString RpaProcessManager::stripAnsiEscapes(const QString& text) const
{
    static const QRegularExpression kAnsiRe(QStringLiteral("\x1B\\[[0-9;?]*[ -/]*[@-~]"));
    QString result = text;
    result.remove(kAnsiRe);
    return result;
}

bool RpaProcessManager::isValidPlatformId(const QString& platformId) const
{
    return platformId == QLatin1String("wechat")
        || platformId == QLatin1String("qianniu");
}

} // namespace Rpa
