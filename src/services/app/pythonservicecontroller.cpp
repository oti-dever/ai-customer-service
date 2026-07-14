#include "pythonservicecontroller.h"

#include "../ai/aiprovidercatalog.h"
#include "../../ipc/ipcservice.h"
#include "../../ipc/ipctypes.h"
#include "../../utils/appsettings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>

PythonServiceController& PythonServiceController::instance()
{
    static PythonServiceController controller;
    return controller;
}

PythonServiceController::PythonServiceController(QObject* parent)
    : QObject(parent)
    , m_startupPollTimer(new QTimer(this))
    , m_healthNetwork(new QNetworkAccessManager(this))
{
    m_startupPollTimer->setInterval(700);
    connect(m_startupPollTimer, &QTimer::timeout,
            this, &PythonServiceController::pollStartupHealth);
}

bool PythonServiceController::isManagedServiceRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool PythonServiceController::isBusy() const
{
    return m_state == State::Starting || m_state == State::Stopping;
}

QString PythonServiceController::stateText() const
{
    switch (m_state) {
    case State::Stopped:
        return QStringLiteral("Python 服务未运行");
    case State::Starting:
        return QStringLiteral("Python 服务正在启动");
    case State::Running:
        return QStringLiteral("Python 服务运行中（由当前客户端启动）");
    case State::ExternalRunning:
        return QStringLiteral("Python 服务运行中（外部启动）");
    case State::Stopping:
        return QStringLiteral("Python 服务正在停止");
    case State::Failed:
        return QStringLiteral("Python 服务启动失败");
    }
    return QStringLiteral("Python 服务状态未知");
}

void PythonServiceController::startService()
{
    qInfo() << "[PythonServiceController] start requested"
            << "state=" << static_cast<int>(m_state)
            << "managedRunning=" << isManagedServiceRunning();

    if (isBusy()) {
        appendHumanLog(QStringLiteral("服务正在处理上一条启动/停止请求，请稍等。"));
        return;
    }

    if (isManagedServiceRunning()) {
        finishAsConnected(State::Running, QStringLiteral("Python 服务已经在运行。"));
        return;
    }

    QString host;
    int port = 8765;
    if (!configuredEndpointIsLocal(&host, &port)) {
        appendHumanLog(QStringLiteral("当前服务地址不是本机地址，无法由客户端启动。请改为 http://127.0.0.1:8765 后再试。"));
        setState(State::Failed);
        return;
    }

    m_pendingStartHost = host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
    m_pendingStartPort = port;
    appendHumanLog(QStringLiteral("正在检查本地 Python 服务状态..."));
    setState(State::Starting);
    startHealthProbe(HealthProbePurpose::BeforeStart);
}

void PythonServiceController::startHealthProbe(HealthProbePurpose purpose, int timeoutMs)
{
    if (m_healthReply) {
        QNetworkReply* oldReply = m_healthReply;
        m_healthReply = nullptr;
        oldReply->disconnect(this);
        oldReply->abort();
        oldReply->deleteLater();
    }

    QNetworkRequest request{QUrl(Ipc::IpcService::instance().serviceEndpoint() + QStringLiteral("/api/health"))};
    request.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = m_healthNetwork->get(request);
    m_healthReply = reply;
    const int probeId = ++m_healthProbeId;
    connect(reply, &QNetworkReply::finished, this, [this, probeId, purpose, reply]() {
        handleHealthProbeFinished(probeId, purpose, reply);
    });
}

void PythonServiceController::handleHealthProbeFinished(int probeId,
                                                        HealthProbePurpose purpose,
                                                        QNetworkReply* reply)
{
    if (!reply)
        return;
    if (reply == m_healthReply)
        m_healthReply = nullptr;

    const bool current = probeId == m_healthProbeId;
    QString error;
    bool healthy = false;
    if (reply->error() == QNetworkReply::NoError) {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject())
            healthy = doc.object().value(QStringLiteral("healthy")).toBool(false);
        else
            error = QStringLiteral("invalid_json_response");
    } else {
        error = reply->errorString();
    }
    reply->deleteLater();

    if (!current)
        return;

    switch (purpose) {
    case HealthProbePurpose::BeforeStart:
        if (m_state != State::Starting)
            return;
        if (healthy) {
            finishAsConnected(
                State::ExternalRunning,
                QStringLiteral("检测到 Python 服务已经在运行，已直接连接。这个服务不是当前客户端启动的，关闭时不会被强制停止。"));
            Ipc::IpcService::instance().markServiceAvailable();
            return;
        }
        launchManagedService(m_pendingStartHost, m_pendingStartPort);
        return;

    case HealthProbePurpose::StartupPoll:
        if (m_state != State::Starting)
            return;
        if (healthy) {
            m_startupPollTimer->stop();
            finishAsConnected(State::Running, QStringLiteral("Python 服务已就绪，可以开始平台监听。"));
            Ipc::IpcService::instance().markServiceAvailable();
            return;
        }
        --m_startupPollsRemaining;
        if (m_startupPollsRemaining <= 0) {
            m_startupPollTimer->stop();
            appendHumanLog(QStringLiteral("服务启动超时：Python 进程已启动，但健康检查没有通过。%1")
                               .arg(error.left(160)));
            setState(State::Failed);
        }
        return;

    case HealthProbePurpose::RefreshState:
        if (healthy) {
            Ipc::IpcService::instance().markServiceAvailable();
            setState(isManagedServiceRunning() ? State::Running : State::ExternalRunning);
        } else {
            Ipc::IpcService::instance().markServiceUnavailable();
            setState(State::Stopped);
        }
        return;
    }
}

void PythonServiceController::launchManagedService(const QString& host, int port)
{
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    QSettings settings = AppSettings::create();
    const QString pythonExe = settings.value(QStringLiteral("pythonService/pythonExecutable"),
                                             QStringLiteral("python")).toString().trimmed();
    const QString mode = settings.value(QStringLiteral("pythonService/startMode"),
                                        QStringLiteral("debug")).toString().trimmed().toLower() == QLatin1String("formal")
                             ? QStringLiteral("formal")
                             : QStringLiteral("debug");
    const QString pythonDir = QDir(QStringLiteral(PROJECT_ROOT_DIR)).filePath(QStringLiteral("python"));
    if (!QFileInfo::exists(pythonDir)) {
        appendHumanLog(QStringLiteral("启动失败：找不到 Python 服务目录。"));
        setState(State::Failed);
        return;
    }

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(pythonDir);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("YY_PARENT_PID"), QString::number(QCoreApplication::applicationPid()));
    const QString bgeVlDir = QDir(QStringLiteral(PROJECT_ROOT_DIR)).filePath(
        QStringLiteral("database/models/bge-vl-base"));
    if (QFileInfo::exists(bgeVlDir))
        env.insert(QStringLiteral("AI_CUSTOMER_SERVICE_IMAGE_EMBEDDING_MODEL"), bgeVlDir);
    const AiProviderConfig doubaoConfig = loadAiProviderConfig(QStringLiteral("doubao:ark"));
    if (doubaoConfig.isValidForChat()) {
        env.insert(QStringLiteral("AI_CUSTOMER_SERVICE_IMAGE_ANALYZER"), QStringLiteral("ark"));
        env.insert(QStringLiteral("AI_CUSTOMER_SERVICE_IMAGE_ANALYSIS_BASE_URL"), doubaoConfig.baseUrl.trimmed());
        env.insert(QStringLiteral("AI_CUSTOMER_SERVICE_IMAGE_ANALYSIS_MODEL"), doubaoConfig.model.trimmed());
        env.insert(QStringLiteral("AI_CUSTOMER_SERVICE_IMAGE_ANALYSIS_API_KEY"), doubaoConfig.apiKey);
        env.insert(QStringLiteral("AI_CUSTOMER_SERVICE_IMAGE_RERANK_ENABLED"), QStringLiteral("1"));
        env.insert(QStringLiteral("AI_CUSTOMER_SERVICE_IMAGE_RERANK_PROVIDER"), QStringLiteral("ark"));
    }
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::started,
            this, &PythonServiceController::onProcessStarted);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PythonServiceController::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &PythonServiceController::onProcessError);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PythonServiceController::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &PythonServiceController::onReadyReadStandardError);

    QStringList args;
    args << QStringLiteral("-m") << QStringLiteral("service.server")
         << QStringLiteral("--host") << (host.isEmpty() ? QStringLiteral("127.0.0.1") : host)
         << QStringLiteral("--port") << QString::number(port)
         << QStringLiteral("--mode") << mode
         << QStringLiteral("--parent-pid") << QString::number(QCoreApplication::applicationPid());

    m_stopRequested = false;
    m_startupPollsRemaining = 50;
    appendHumanLog(QStringLiteral("正在启动 Python 服务（%1 模式），请稍候。").arg(mode));
    setState(State::Starting);
    m_process->start(pythonExe.isEmpty() ? QStringLiteral("python") : pythonExe, args);
}

void PythonServiceController::stopService()
{
    qInfo() << "[PythonServiceController] stop requested"
            << "state=" << static_cast<int>(m_state)
            << "managedRunning=" << isManagedServiceRunning();

    if (isBusy()) {
        appendHumanLog(QStringLiteral("服务正在处理上一条启动/停止请求，请稍等。"));
        return;
    }

    if (!isManagedServiceRunning()) {
        if (Ipc::IpcService::instance().isServiceAvailable()) {
            appendHumanLog(QStringLiteral("当前连接的是外部启动的 Python 服务。为避免误关其他窗口启动的服务，客户端不会强制停止它。"));
            setState(State::ExternalRunning);
        } else {
            appendHumanLog(QStringLiteral("Python 服务当前没有运行。"));
            setState(State::Stopped);
        }
        return;
    }

    m_stopRequested = true;
    setState(State::Stopping);
    appendHumanLog(QStringLiteral("正在停止由当前客户端启动的 Python 服务。"));
    m_process->terminate();
    scheduleForceKill();
}

void PythonServiceController::stopManagedServiceOnExit()
{
    if (m_healthReply) {
        QNetworkReply* reply = m_healthReply;
        m_healthReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_startupPollTimer->stop();

    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;

    qInfo() << "[PythonServiceController] stop managed service on exit"
            << "pid=" << m_process->processId();
    m_stopRequested = true;
    m_process->terminate();
    if (!m_process->waitForFinished(1500)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void PythonServiceController::refreshConnectionState()
{
    if (isBusy())
        return;
    if (isManagedServiceRunning()) {
        setState(State::Running);
        return;
    }
    startHealthProbe(HealthProbePurpose::RefreshState);
}

void PythonServiceController::onProcessStarted()
{
    qInfo() << "[PythonServiceController] process started"
            << "pid=" << (m_process ? m_process->processId() : 0);
    appendHumanLog(QStringLiteral("Python 进程已启动，正在等待服务就绪。"));
    m_startupPollTimer->start();
}

void PythonServiceController::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess* finishedProcess = qobject_cast<QProcess*>(sender());
    qInfo() << "[PythonServiceController] process finished"
            << "exitCode=" << exitCode
            << "exitStatus=" << exitStatus
            << "stopRequested=" << m_stopRequested;
    m_startupPollTimer->stop();
    if (finishedProcess) {
        const QByteArray stdoutTail = finishedProcess->readAllStandardOutput();
        const QByteArray stderrTail = finishedProcess->readAllStandardError();
        qInfo() << "[PythonServiceController] process final output"
                << "stdoutBytes=" << stdoutTail.size()
                << "stderrBytes=" << stderrTail.size();
        appendProcessOutput(stdoutTail);
        appendProcessOutput(stderrTail);
        finishedProcess->disconnect(this);
        if (finishedProcess == m_process)
            m_process = nullptr;
        finishedProcess->deleteLater();
    }
    const bool normalStop = m_stopRequested || (exitStatus == QProcess::NormalExit && exitCode == 0);
    if (normalStop) {
        appendHumanLog(QStringLiteral("Python 服务已停止。"));
        setState(State::Stopped);
        Ipc::IpcService::instance().markServiceUnavailable();
    } else {
        appendHumanLog(QStringLiteral("Python 服务意外退出：exitCode=%1，exitStatus=%2。请查看上方服务输出定位原因。")
                           .arg(exitCode)
                           .arg(static_cast<int>(exitStatus)));
        setState(State::Failed);
        QTimer::singleShot(0, this, []() {
            PythonServiceController::instance().refreshConnectionState();
        });
    }
    m_stopRequested = false;
}

void PythonServiceController::onProcessError(QProcess::ProcessError error)
{
    if (m_stopRequested) {
        qInfo() << "[PythonServiceController] process error while stopping"
                << "error=" << error;
    } else {
        qWarning() << "[PythonServiceController] process error"
                   << "error=" << error
                   << "stopRequested=" << m_stopRequested;
    }
    if (error == QProcess::FailedToStart) {
        appendHumanLog(QStringLiteral("启动失败：没有找到 Python，或 Python 无法运行。请确认已安装 Python 并加入 PATH。"));
        setState(State::Failed);
        return;
    }
    if (error == QProcess::Crashed && !m_stopRequested) {
        appendHumanLog(QStringLiteral("Python 服务异常退出。"));
        setState(State::Failed);
    }
}

void PythonServiceController::onReadyReadStandardOutput()
{
    if (m_process)
        appendProcessOutput(m_process->readAllStandardOutput());
}

void PythonServiceController::onReadyReadStandardError()
{
    if (m_process)
        appendProcessOutput(m_process->readAllStandardError());
}

void PythonServiceController::pollStartupHealth()
{
    if (m_state != State::Starting) {
        m_startupPollTimer->stop();
        return;
    }

    if (!m_healthReply)
        startHealthProbe(HealthProbePurpose::StartupPoll);
}

void PythonServiceController::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void PythonServiceController::appendHumanLog(const QString& line)
{
    const QString cleaned = line.trimmed();
    if (cleaned.isEmpty())
        return;
    const QString stamped = QStringLiteral("%1  %2")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")),
                                     cleaned);
    m_humanLogs.append(stamped);
    while (m_humanLogs.size() > 200)
        m_humanLogs.removeFirst();
    emit logAppended(stamped);
}

void PythonServiceController::appendProcessOutput(const QByteArray& chunk)
{
    if (!chunk.isEmpty())
        qInfo().noquote() << "[PythonServiceController] process output chunk"
                          << QString::fromUtf8(chunk).left(4000);
    const QString text = QString::fromUtf8(chunk);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString translated = translateProcessLine(line);
        if (!translated.isEmpty())
            appendHumanLog(translated);
    }
}

QString PythonServiceController::translateProcessLine(const QString& line) const
{
    const QString cleaned = line.trimmed();
    if (cleaned.isEmpty())
        return {};
    const QString lower = cleaned.toLower();

    if (lower.contains(QStringLiteral("python service truth db schema ready")))
        return QStringLiteral("服务数据库已准备好。");
    if (lower.contains(QStringLiteral("python ai service listening on http://")))
        return QStringLiteral("服务端口已打开，正在等待客户端连接。");
    if (lower.contains(QStringLiteral("python rpa command websocket listening")))
        return QStringLiteral("平台命令通道已准备好。");
    if (lower.contains(QStringLiteral("managed by parent pid")))
        return QStringLiteral("服务已绑定当前客户端，客户端退出后会自动停止。");
    if (lower.contains(QStringLiteral("running standalone")))
        return QStringLiteral("服务正在独立运行。");
    if (lower.contains(QStringLiteral("parent process watchdog started")))
        return QStringLiteral("已开启客户端退出自动停止保护。");
    if (lower.contains(QStringLiteral("address already in use"))
        || lower.contains(QStringLiteral("only one usage of each socket address"))) {
        return QStringLiteral("启动失败：服务端口已被占用，可能已经有一个 Python 服务在运行。");
    }
    if (lower.contains(QStringLiteral("no module named")))
        return QStringLiteral("启动失败：Python 依赖不完整，请先安装服务端依赖。");
    if (lower.contains(QStringLiteral("traceback")))
        return QStringLiteral("服务运行时出现异常，详细原因请查看开发日志。");
    if (lower.contains(QStringLiteral("stopped by user")))
        return QStringLiteral("Python 服务已收到停止请求。");
    if (lower.contains(QStringLiteral("command websocket server error")))
        return QStringLiteral("平台命令通道启动失败，请检查端口是否被占用。");

    return QStringLiteral("服务输出：%1").arg(cleaned);
}

bool PythonServiceController::configuredEndpointIsLocal(QString* hostOut, int* portOut) const
{
    const QUrl url(Ipc::IpcService::instance().serviceEndpoint());
    const QString host = url.host().isEmpty() ? QStringLiteral("127.0.0.1") : url.host();
    const int port = url.port(8765);
    const QString lower = host.toLower();
    const bool local = lower == QLatin1String("127.0.0.1")
                       || lower == QLatin1String("localhost")
                       || lower == QLatin1String("::1");
    if (hostOut)
        *hostOut = host;
    if (portOut)
        *portOut = port;
    return local;
}

void PythonServiceController::finishAsConnected(State connectedState, const QString& message)
{
    appendHumanLog(message);
    setState(connectedState);
}

void PythonServiceController::scheduleForceKill()
{
    QTimer::singleShot(3000, this, [this]() {
        if (!m_process || m_process->state() == QProcess::NotRunning)
            return;
        appendHumanLog(QStringLiteral("服务停止较慢，正在强制结束后台进程。"));
        m_process->kill();
    });
}
