#include "ipcservice.h"
#include "../utils/appsettings.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QHostAddress>
#include <QTimer>
#include <QUrlQuery>
#include <QSettings>
#include <QWebSocket>
#include <QWebSocketServer>

namespace Ipc {

IpcService& IpcService::instance()
{
    static IpcService s_instance;
    return s_instance;
}

IpcService::IpcService(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_endpoint(QStringLiteral("http://127.0.0.1:8765"))
{
    loadConnectionSettings();
    connect(m_network, &QNetworkAccessManager::finished,
            this, &IpcService::onRequestFinished);
    startEventWebSocketServer();
}

IpcService::~IpcService()
{
    shutdown();
}

void IpcService::initialize()
{
    qInfo() << "[IpcService] 初始化，端点:" << m_endpoint;
    auto response = checkHealth();
    m_serviceAvailable = (response.status == ResponseStatus::Success && response.healthy);
    emit serviceStatusChanged(m_serviceAvailable);
}

void IpcService::shutdown()
{
    for (auto& pending : m_pendingRequests) {
        if (pending.timer) {
            pending.timer->stop();
            pending.timer->deleteLater();
        }
        if (pending.reply) {
            pending.reply->abort();
        }
    }
    m_pendingRequests.clear();
    stopCommandWebSocketClient();
    stopEventWebSocketServer();
    stopManagedService();
    qInfo() << "[IpcService] 已关闭";
}

void IpcService::setServiceEndpoint(const QString& endpoint)
{
    const QString normalized = normalizedEndpoint(endpoint);
    if (m_endpoint != normalized) {
        m_endpoint = normalized;
        qInfo() << "[IpcService] 更新端点:" << endpoint;
    }
}

QString IpcService::normalizedEndpoint(const QString& endpoint) const
{
    QString value = endpoint.trimmed();
    if (value.isEmpty())
        return QStringLiteral("http://127.0.0.1:8765");
    if (!value.startsWith(QStringLiteral("http://")) && !value.startsWith(QStringLiteral("https://")))
        value.prepend(QStringLiteral("http://"));
    while (value.endsWith('/'))
        value.chop(1);
    return value;
}

void IpcService::setManageServiceLifecycle(bool enabled)
{
    m_manageServiceLifecycle = enabled;
}

void IpcService::loadConnectionSettings()
{
    QSettings settings = AppSettings::create();
    m_endpoint = normalizedEndpoint(settings.value(QStringLiteral("rpa/serviceEndpoint"), m_endpoint).toString());
    m_manageServiceLifecycle = settings.value(QStringLiteral("rpa/manageServiceLifecycle"), false).toBool();
}

void IpcService::saveConnectionSettings() const
{
    QSettings settings = AppSettings::create();
    settings.setValue(QStringLiteral("rpa/serviceEndpoint"), m_endpoint);
    settings.setValue(QStringLiteral("rpa/manageServiceLifecycle"), m_manageServiceLifecycle);
}

bool IpcService::connectToConfiguredService(QString* errorOut)
{
    m_serviceAvailable = false;
    emit serviceStatusChanged(false);
    const bool available = ensureServiceAvailable(errorOut);
    if (available)
        startCommandWebSocketClient();
    return available;
}

bool IpcService::isManagedServiceRunning() const
{
    return m_serviceProcess && m_serviceProcess->state() != QProcess::NotRunning;
}

QString IpcService::eventWebSocketUrl() const
{
    return QStringLiteral("ws://127.0.0.1:%1").arg(m_eventPort);
}

bool IpcService::restartManagedService(QString* errorOut)
{
    if (!m_manageServiceLifecycle)
        return connectToConfiguredService(errorOut);

    qInfo() << "[IpcService] 正在重启 Python sidecar";
    stopManagedService();
    stopExternalManagedServiceProcesses();
    m_serviceAvailable = false;
    emit serviceStatusChanged(false);
    return startManagedService(errorOut);
}

bool IpcService::startManagedService(QString* errorOut)
{
    if (!m_manageServiceLifecycle)
        return connectToConfiguredService(errorOut);

    HealthCheckResponse health = checkHealth();
    if (health.status == ResponseStatus::Success && health.healthy) {
        m_serviceAvailable = true;
        emit serviceStatusChanged(true);
        startCommandWebSocketClient();
        return true;
    }

    if (isManagedServiceRunning())
        return ensureServiceAvailable(errorOut);

    auto* proc = new QProcess(this);
    proc->setProgram(QStringLiteral("python"));
    proc->setArguments({
        QStringLiteral("-m"),
        QStringLiteral("service.server"),
        QStringLiteral("--host"),
        QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"),
        QStringLiteral("8765"),
    });
    proc->setWorkingDirectory(QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/python"));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("YY_PARENT_PID"), QString::number(QCoreApplication::applicationPid()));
    env.insert(QStringLiteral("YY_RPA_EVENT_WS_URL"), eventWebSocketUrl());
    env.insert(QStringLiteral("YY_RPA_COMMAND_WS_URL"), QStringLiteral("ws://127.0.0.1:%1").arg(m_commandPort));
    proc->setProcessEnvironment(env);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        appendServiceLog(proc->readAllStandardOutput());
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus) {
        appendServiceLog(proc->readAllStandardOutput());
        qInfo() << "[IpcService] Python sidecar 已退出，退出码:" << exitCode;
        if (m_serviceProcess == proc)
            m_serviceProcess = nullptr;
        m_serviceAvailable = false;
        emit serviceStatusChanged(false);
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qWarning() << "[IpcService] Python sidecar 进程错误:" << static_cast<int>(error);
        m_serviceAvailable = false;
        emit serviceStatusChanged(false);
    });

    m_serviceProcess = proc;
    proc->start();
    if (!proc->waitForStarted(3000)) {
        const QString error = QStringLiteral("无法启动 Python AI 服务，请确认 python 在 PATH 中可用");
        if (errorOut)
            *errorOut = error;
        qWarning() << "[IpcService]" << error;
        proc->deleteLater();
        m_serviceProcess = nullptr;
        m_serviceAvailable = false;
        emit serviceStatusChanged(false);
        return false;
    }

    qInfo() << "[IpcService] 已启动 Python sidecar: python -m service.server";
    return ensureServiceAvailable(errorOut);
}

RpaCommandResponse IpcService::sendRpaCommandViaWebSocket(const RpaCommandRequest& request, int timeoutMs)
{
    RpaCommandResponse response;
    response.requestId = request.requestId;
    response.respondedAt = QDateTime::currentDateTime();

    startCommandWebSocketClient();
    if (!m_commandSocket) {
        response.status = ResponseStatus::Error;
        response.errorMessage = QStringLiteral("command_websocket_not_connected");
        return response;
    }

    if (m_commandSocket->state() != QAbstractSocket::ConnectedState) {
        QEventLoop connectLoop;
        QString socketError;
        const QMetaObject::Connection connectedConn = QObject::connect(
            m_commandSocket, &QWebSocket::connected, &connectLoop, &QEventLoop::quit);
        const QMetaObject::Connection errorConn = QObject::connect(
            m_commandSocket, &QWebSocket::errorOccurred, &connectLoop, [&](QAbstractSocket::SocketError) {
                socketError = m_commandSocket ? m_commandSocket->errorString() : QStringLiteral("command_websocket_closed");
                connectLoop.quit();
            });
        const QMetaObject::Connection disconnectedConn = QObject::connect(
            m_commandSocket, &QWebSocket::disconnected, &connectLoop, &QEventLoop::quit);
        QTimer::singleShot(timeoutMs, &connectLoop, &QEventLoop::quit);
        connectLoop.exec();
        QObject::disconnect(connectedConn);
        QObject::disconnect(errorConn);
        QObject::disconnect(disconnectedConn);

        if (!m_commandSocket || m_commandSocket->state() != QAbstractSocket::ConnectedState) {
            response.status = ResponseStatus::Error;
            response.errorMessage = socketError.isEmpty()
                ? QStringLiteral("command_websocket_not_connected")
                : socketError;
            stopCommandWebSocketClient();
            return response;
        }
    }

    QEventLoop responseLoop;
    QString rawResponse;
    QString socketError;
    const QMetaObject::Connection messageConn = QObject::connect(
        m_commandSocket, &QWebSocket::textMessageReceived, &responseLoop, [&](const QString& message) {
        rawResponse = message;
        responseLoop.quit();
    });
    const QMetaObject::Connection errorConn = QObject::connect(
        m_commandSocket, &QWebSocket::errorOccurred, &responseLoop, [&](QAbstractSocket::SocketError) {
        socketError = m_commandSocket ? m_commandSocket->errorString() : QStringLiteral("command_websocket_closed");
        responseLoop.quit();
    });
    const QMetaObject::Connection disconnectedConn = QObject::connect(
        m_commandSocket, &QWebSocket::disconnected, &responseLoop, &QEventLoop::quit);

    const QJsonObject payload = buildRpaCommandPayload(request);
    qInfo() << "[IpcService] command WebSocket send"
            << "requestId=" << request.requestId
            << "command=" << request.commandType
            << "taskId=" << request.taskId
            << "clientMessageId=" << request.parameters.value(QStringLiteral("client_message_id")).toString();
    m_commandSocket->sendTextMessage(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));

    QTimer::singleShot(timeoutMs, &responseLoop, &QEventLoop::quit);
    responseLoop.exec();
    QObject::disconnect(messageConn);
    QObject::disconnect(errorConn);
    QObject::disconnect(disconnectedConn);

    if (rawResponse.isEmpty()) {
        response.status = socketError.isEmpty() ? ResponseStatus::Timeout : ResponseStatus::Error;
        response.errorMessage = socketError.isEmpty() ? QStringLiteral("request_timeout") : socketError;
        return response;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(rawResponse.toUtf8());
    if (!doc.isObject()) {
        response.status = ResponseStatus::Error;
        response.errorMessage = QStringLiteral("invalid_json_response");
        return response;
    }

    const QJsonObject json = doc.object();
    if (!json.value(QStringLiteral("request_id")).toString().isEmpty()
        && json.value(QStringLiteral("request_id")).toString() != request.requestId) {
        qWarning() << "[IpcService] command WebSocket response request_id mismatch"
                   << "expected=" << request.requestId
                   << "actual=" << json.value(QStringLiteral("request_id")).toString();
    }
    response.status = responseStatusFromString(json.value(QStringLiteral("status")).toString(QStringLiteral("success")));
    response.errorMessage = json.value(QStringLiteral("error")).toString();
    response.result = json.value(QStringLiteral("result")).toObject();
    qInfo() << "[IpcService] command WebSocket response"
            << "requestId=" << request.requestId
            << "status=" << json.value(QStringLiteral("status")).toString()
            << "error=" << response.errorMessage;
    return response;
}

void IpcService::stopManagedService()
{
    if (!m_manageServiceLifecycle)
        return;

    if (!m_serviceProcess)
        return;

    QProcess* proc = m_serviceProcess;
    m_serviceProcess = nullptr;
    proc->disconnect(this);
    proc->terminate();
    if (!proc->waitForFinished(2000))
        proc->kill();
    proc->deleteLater();
    m_serviceAvailable = false;
    emit serviceStatusChanged(false);
}

void IpcService::startEventWebSocketServer()
{
    if (m_eventServer)
        return;

    m_eventServer = new QWebSocketServer(
        QStringLiteral("yy-ai-customer-service-rpa-events"),
        QWebSocketServer::NonSecureMode,
        this);
    if (!m_eventServer->listen(QHostAddress::LocalHost, m_eventPort)) {
        qWarning() << "[IpcService] event WebSocket listen failed port=" << m_eventPort;
        m_eventServer->deleteLater();
        m_eventServer = nullptr;
        return;
    }

    connect(m_eventServer, &QWebSocketServer::newConnection, this, [this]() {
        while (m_eventServer && m_eventServer->hasPendingConnections()) {
            QWebSocket* socket = m_eventServer->nextPendingConnection();
            if (!socket)
                continue;
            m_eventSockets.insert(socket);
            connect(socket, &QWebSocket::textMessageReceived,
                    this, &IpcService::onEventSocketTextMessageReceived);
            connect(socket, &QWebSocket::disconnected,
                    this, &IpcService::onEventSocketDisconnected);
            qInfo() << "[IpcService] event WebSocket connected count=" << m_eventSockets.size();
            emit rpaEventBridgeStateChanged(true);
        }
    });

    qInfo() << "[IpcService] event WebSocket listening" << eventWebSocketUrl();
}

void IpcService::stopEventWebSocketServer()
{
    const auto sockets = m_eventSockets.values();
    m_eventSockets.clear();
    for (QWebSocket* socket : sockets) {
        if (!socket)
            continue;
        socket->close();
        socket->deleteLater();
    }

    if (!m_eventServer)
        return;
    m_eventServer->close();
    m_eventServer->deleteLater();
    m_eventServer = nullptr;
}

void IpcService::startCommandWebSocketClient()
{
    if (m_commandSocket
        && (m_commandSocket->state() == QAbstractSocket::ConnectedState
            || m_commandSocket->state() == QAbstractSocket::ConnectingState)) {
        return;
    }

    stopCommandWebSocketClient();

    m_commandSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_commandSocket, &QWebSocket::connected, this, [this]() {
        qInfo() << "[IpcService] command WebSocket connected"
                << QStringLiteral("ws://127.0.0.1:%1").arg(m_commandPort);
    });
    connect(m_commandSocket, &QWebSocket::textMessageReceived,
            this, &IpcService::onCommandSocketTextMessageReceived);
    connect(m_commandSocket, &QWebSocket::disconnected,
            this, &IpcService::onCommandSocketDisconnected);
    connect(m_commandSocket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_commandSocket)
            qWarning() << "[IpcService] command WebSocket error:" << m_commandSocket->errorString();
    });
    m_commandSocket->open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(m_commandPort)));
}

void IpcService::stopCommandWebSocketClient()
{
    if (!m_commandSocket)
        return;

    QWebSocket* socket = m_commandSocket;
    m_commandSocket = nullptr;
    socket->disconnect(this);
    socket->close();
    socket->deleteLater();
}

void IpcService::handleEventSocketPayload(const QJsonObject& payload)
{
    const QString type = payload.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("rpa_event")) {
        const QJsonObject event = payload.value(QStringLiteral("event")).toObject();
        if (!event.isEmpty())
            emit rpaEventReceived(event);
        return;
    }
    if (type == QLatin1String("hello")) {
        qInfo() << "[IpcService] event WebSocket hello"
                << payload.value(QStringLiteral("platform")).toString()
                << payload.value(QStringLiteral("account_id")).toString();
        emit rpaEventBridgeStateChanged(true);
        return;
    }
    qInfo() << "[IpcService] event WebSocket message type=" << type;
}

void IpcService::onEventSocketTextMessageReceived(const QString& message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        qWarning() << "[IpcService] event WebSocket invalid JSON";
        return;
    }
    handleEventSocketPayload(doc.object());
}

void IpcService::onEventSocketDisconnected()
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket)
        return;
    m_eventSockets.remove(socket);
    qInfo() << "[IpcService] event WebSocket disconnected count=" << m_eventSockets.size();
    emit rpaEventBridgeStateChanged(!m_eventSockets.isEmpty());
    socket->deleteLater();
}

void IpcService::onCommandSocketTextMessageReceived(const QString& message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        qWarning() << "[IpcService] command WebSocket async invalid JSON";
        return;
    }
    const QJsonObject json = doc.object();
    qInfo() << "[IpcService] command WebSocket async message"
            << "requestId=" << json.value(QStringLiteral("request_id")).toString()
            << "status=" << json.value(QStringLiteral("status")).toString()
            << "error=" << json.value(QStringLiteral("error")).toString();
}

void IpcService::onCommandSocketDisconnected()
{
    qInfo() << "[IpcService] command WebSocket disconnected";
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (socket && socket == m_commandSocket) {
        m_commandSocket = nullptr;
        socket->deleteLater();
    }
}

void IpcService::stopExternalManagedServiceProcesses()
{
#ifdef Q_OS_WIN
    QProcess killer;
    killer.setProgram(QStringLiteral("powershell"));
    killer.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-Command"),
        QStringLiteral(
            "$procs = Get-CimInstance Win32_Process | Where-Object { "
            "$_.CommandLine -and "
            "$_.CommandLine -match 'python' -and "
            "$_.CommandLine -match 'service\\.server' -and "
            "$_.CommandLine -match '--port' -and "
            "$_.CommandLine -match '8765' "
            "}; "
            "$procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }; "
            "Write-Output (($procs | Measure-Object).Count)"
        ),
    });
    killer.setProcessChannelMode(QProcess::MergedChannels);
    killer.start();
    if (!killer.waitForFinished(3000)) {
        killer.kill();
        qWarning() << "[IpcService] 停止已有 Python sidecar 超时";
        return;
    }

    const QString output = QString::fromUtf8(killer.readAll()).trimmed();
    if (killer.exitCode() != 0) {
        qWarning() << "[IpcService] 停止已有 Python sidecar 失败:" << output;
        return;
    }
    qInfo() << "[IpcService] 已停止已有 Python sidecar 进程数:" << output;
#endif
}

bool IpcService::ensureServiceAvailable(QString* errorOut)
{
    for (int i = 0; i < 10; ++i) {
        HealthCheckResponse health = checkHealth();
        if (health.status == ResponseStatus::Success && health.healthy) {
            m_serviceAvailable = true;
            emit serviceStatusChanged(true);
            startCommandWebSocketClient();
            return true;
        }

        QEventLoop loop;
        QTimer::singleShot(200, &loop, &QEventLoop::quit);
        loop.exec();
    }

    const QString error = QStringLiteral("Python AI 服务未就绪");
    if (errorOut)
        *errorOut = error;
    m_serviceAvailable = false;
    emit serviceStatusChanged(false);
    return false;
}

QString IpcService::requestAiSuggestion(const AiSuggestionRequest& request)
{
    QUrl url(m_endpoint + QStringLiteral("/api/ai/suggestion"));
    QNetworkRequest netRequest(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload = buildAiSuggestionPayload(request);
    QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = m_network->post(netRequest, data);

    PendingRequest pending;
    pending.requestId = request.requestId;
    pending.type = RequestType::AiSuggestion;
    pending.reply = reply;
    pending.startedAt = QDateTime::currentDateTime();

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setProperty("requestId", request.requestId);
    connect(timer, &QTimer::timeout, this, &IpcService::onRequestTimeout);
    timer->start(m_defaultTimeoutMs);
    pending.timer = timer;

    m_pendingRequests[request.requestId] = pending;
    reply->setProperty("requestId", request.requestId);

    qDebug() << "[IpcService] 发送 AI 建议请求:" << request.requestId
             << "conversationId=" << request.conversationId;

    return request.requestId;
}

void IpcService::cancelRequest(const QString& requestId)
{
    auto it = m_pendingRequests.find(requestId);
    if (it == m_pendingRequests.end()) return;

    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }
    if (it->reply) {
        it->reply->abort();
    }

    m_pendingRequests.erase(it);
    qDebug() << "[IpcService] 已取消请求:" << requestId;
}

HealthCheckResponse IpcService::checkHealth()
{
    HealthCheckResponse response;
    response.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    response.respondedAt = QDateTime::currentDateTime();

    QUrl url(m_endpoint + QStringLiteral("/api/health"));
    QNetworkRequest request(url);
    request.setTransferTimeout(1000);

    QNetworkReply* reply = m_network->get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            response.healthy = obj.value(QStringLiteral("healthy")).toBool(false);
            response.version = obj.value(QStringLiteral("version")).toString();
            response.status = ResponseStatus::Success;
        }
    } else {
        response.status = ResponseStatus::Error;
        response.errorMessage = reply->errorString();
        response.healthy = false;
    }

    reply->deleteLater();
    return response;
}

RpaCommandResponse IpcService::sendRpaCommand(const RpaCommandRequest& request, int timeoutMs)
{
    RpaCommandResponse response;
    response.requestId = request.requestId;
    response.respondedAt = QDateTime::currentDateTime();

    ResponseStatus status = ResponseStatus::Error;
    QString error;
    const QJsonObject json = performJsonPost(
        QUrl(m_endpoint + QStringLiteral("/api/rpa/command")),
        buildRpaCommandPayload(request),
        timeoutMs,
        &status,
        &error);

    response.status = status;
    response.errorMessage = error;
    if (status != ResponseStatus::Success)
        return response;

    response.status = responseStatusFromString(json.value(QStringLiteral("status")).toString(QStringLiteral("success")));
    response.errorMessage = json.value(QStringLiteral("error")).toString();
    response.result = json.value(QStringLiteral("result")).toObject();
    return response;
}

RpaEventBatch IpcService::fetchRpaEvents(const QString& platformType,
                                         const QString& cursor,
                                         int limit,
                                         int timeoutMs)
{
    RpaEventBatch batch;
    batch.respondedAt = QDateTime::currentDateTime();
    batch.cursor = cursor;

    QUrl url(m_endpoint + QStringLiteral("/api/rpa/events"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("platform"), platformType);
    query.addQueryItem(QStringLiteral("cursor"), cursor.isEmpty() ? QStringLiteral("0") : cursor);
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    url.setQuery(query);

    ResponseStatus status = ResponseStatus::Error;
    QString error;
    const QJsonObject json = performJsonGet(url, timeoutMs, &status, &error);
    batch.status = status;
    batch.errorMessage = error;
    if (status != ResponseStatus::Success)
        return batch;

    batch.status = responseStatusFromString(json.value(QStringLiteral("status")).toString(QStringLiteral("success")));
    batch.errorMessage = json.value(QStringLiteral("error")).toString();
    batch.cursor = json.value(QStringLiteral("cursor")).toString(cursor);
    batch.latestCursor = json.value(QStringLiteral("latest_cursor")).toString(batch.cursor);
    const QJsonArray events = json.value(QStringLiteral("events")).toArray();
    for (const QJsonValue& value : events) {
        if (value.isObject())
            batch.events.append(value.toObject());
    }
    return batch;
}

RpaCommandResponse IpcService::checkRpaHealth(const QString& platformType, int timeoutMs)
{
    RpaCommandResponse response;
    response.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    response.respondedAt = QDateTime::currentDateTime();

    QUrl url(m_endpoint + QStringLiteral("/api/rpa/health"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("platform"), platformType);
    url.setQuery(query);

    ResponseStatus status = ResponseStatus::Error;
    QString error;
    const QJsonObject json = performJsonGet(url, timeoutMs, &status, &error);
    response.status = status;
    response.errorMessage = error;
    if (status != ResponseStatus::Success)
        return response;

    response.status = responseStatusFromString(json.value(QStringLiteral("status")).toString(QStringLiteral("success")));
    response.errorMessage = json.value(QStringLiteral("error")).toString();
    response.result = json;
    return response;
}

void IpcService::appendServiceLog(const QByteArray& chunk)
{
    if (chunk.isEmpty())
        return;
    qInfo().noquote() << "[IpcService][Python]" << QString::fromUtf8(chunk).trimmed();
}

void IpcService::onRequestFinished(QNetworkReply* reply)
{
    QString requestId = reply->property("requestId").toString();
    auto it = m_pendingRequests.find(requestId);
    if (it == m_pendingRequests.end()) {
        reply->deleteLater();
        return;
    }

    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }

    RequestType type = it->type;
    m_pendingRequests.erase(it);

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[IpcService] 请求失败:" << requestId << reply->errorString();
        emit requestFailed(requestId, reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject()) {
        emit requestFailed(requestId, QStringLiteral("无效的响应格式"));
        reply->deleteLater();
        return;
    }

    QJsonObject json = doc.object();

    switch (type) {
    case RequestType::AiSuggestion: {
        AiSuggestionResponse response = parseAiSuggestionResponse(json, requestId);
        emit aiSuggestionReceived(response);
        break;
    }
    default:
        qWarning() << "[IpcService] 未知请求类型:" << static_cast<int>(type);
        break;
    }

    reply->deleteLater();
}

void IpcService::onRequestTimeout()
{
    QTimer* timer = qobject_cast<QTimer*>(sender());
    if (!timer) return;

    QString requestId = timer->property("requestId").toString();
    auto it = m_pendingRequests.find(requestId);
    if (it == m_pendingRequests.end()) return;

    if (it->reply) {
        it->reply->abort();
    }

    m_pendingRequests.erase(it);
    timer->deleteLater();

    qWarning() << "[IpcService] 请求超时:" << requestId;
    emit requestFailed(requestId, QStringLiteral("请求超时"));
}

QJsonObject IpcService::buildAiSuggestionPayload(const AiSuggestionRequest& request) const
{
    QJsonObject payload;
    payload[QStringLiteral("request_id")] = request.requestId;
    payload[QStringLiteral("conversation_id")] = request.conversationId;
    payload[QStringLiteral("platform_type")] = request.platformType;
    payload[QStringLiteral("max_suggestions")] = request.maxSuggestions;
    payload[QStringLiteral("customer_context")] = request.customerContext;

    QJsonArray messages;
    for (const auto& msg : request.recentMessages) {
        QJsonObject msgObj;
        msgObj[QStringLiteral("role")] = msg.first;
        msgObj[QStringLiteral("content")] = msg.second;
        messages.append(msgObj);
    }
    payload[QStringLiteral("messages")] = messages;

    if (!request.metadata.isEmpty()) {
        payload[QStringLiteral("metadata")] = request.metadata;
    }

    return payload;
}

QJsonObject IpcService::buildRpaCommandPayload(const RpaCommandRequest& request) const
{
    QJsonObject payload;
    payload[QStringLiteral("request_id")] = request.requestId;
    payload[QStringLiteral("client_message_id")] = request.taskId;
    payload[QStringLiteral("command")] = request.commandType;
    payload[QStringLiteral("command_type")] = request.commandType;
    payload[QStringLiteral("platform")] = request.platformType;
    payload[QStringLiteral("platform_type")] = request.platformType;
    payload[QStringLiteral("account_id")] = request.accountId;
    payload[QStringLiteral("task_id")] = request.taskId;
    payload[QStringLiteral("target_window")] = request.targetWindow;
    payload[QStringLiteral("parameters")] = request.parameters;
    return payload;
}

QJsonObject IpcService::performJsonGet(const QUrl& url, int timeoutMs,
                                       ResponseStatus* statusOut,
                                       QString* errorOut)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = m_network->get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    QJsonObject obj;
    if (!reply->isFinished()) {
        reply->abort();
        if (statusOut) *statusOut = ResponseStatus::Timeout;
        if (errorOut) *errorOut = QStringLiteral("request_timeout");
        reply->deleteLater();
        return obj;
    }

    if (reply->error() != QNetworkReply::NoError) {
        if (statusOut) *statusOut = ResponseStatus::Error;
        if (errorOut) *errorOut = reply->errorString();
        reply->deleteLater();
        return obj;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        if (statusOut) *statusOut = ResponseStatus::Error;
        if (errorOut) *errorOut = QStringLiteral("invalid_json_response");
        reply->deleteLater();
        return obj;
    }

    if (statusOut) *statusOut = ResponseStatus::Success;
    if (errorOut) errorOut->clear();
    obj = doc.object();
    reply->deleteLater();
    return obj;
}

QJsonObject IpcService::performJsonPost(const QUrl& url,
                                        const QJsonObject& payload,
                                        int timeoutMs,
                                        ResponseStatus* statusOut,
                                        QString* errorOut)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    QJsonObject obj;
    if (!reply->isFinished()) {
        reply->abort();
        if (statusOut) *statusOut = ResponseStatus::Timeout;
        if (errorOut) *errorOut = QStringLiteral("request_timeout");
        reply->deleteLater();
        return obj;
    }

    if (reply->error() != QNetworkReply::NoError) {
        if (statusOut) *statusOut = ResponseStatus::Error;
        if (errorOut) *errorOut = reply->errorString();
        reply->deleteLater();
        return obj;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        if (statusOut) *statusOut = ResponseStatus::Error;
        if (errorOut) *errorOut = QStringLiteral("invalid_json_response");
        reply->deleteLater();
        return obj;
    }

    if (statusOut) *statusOut = ResponseStatus::Success;
    if (errorOut) errorOut->clear();
    obj = doc.object();
    reply->deleteLater();
    return obj;
}

AiSuggestionResponse IpcService::parseAiSuggestionResponse(const QJsonObject& json,
                                                            const QString& requestId) const
{
    AiSuggestionResponse response;
    response.requestId = requestId;
    response.respondedAt = QDateTime::currentDateTime();

    QString status = json.value(QStringLiteral("status")).toString(QStringLiteral("success"));
    response.status = responseStatusFromString(status);
    response.errorMessage = json.value(QStringLiteral("error")).toString();

    QJsonArray suggestions = json.value(QStringLiteral("suggestions")).toArray();
    for (const QJsonValue& v : suggestions) {
        if (v.isString()) {
            response.suggestions.append(v.toString());
            response.confidences.append(80);
        } else if (v.isObject()) {
            QJsonObject obj = v.toObject();
            response.suggestions.append(obj.value(QStringLiteral("content")).toString());
            response.confidences.append(obj.value(QStringLiteral("confidence")).toInt(80));
        }
    }

    if (json.contains(QStringLiteral("metadata"))) {
        response.metadata = json.value(QStringLiteral("metadata")).toObject();
    }

    return response;
}

} // namespace Ipc
