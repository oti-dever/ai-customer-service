#include "ipcservice.h"
#include "../utils/appsettings.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QTimer>
#include <QSettings>
#include <QUrlQuery>
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

void IpcService::markServiceAvailable()
{
    if (!m_serviceAvailable) {
        m_serviceAvailable = true;
        emit serviceStatusChanged(true);
    }
    startCommandWebSocketClient();
}

void IpcService::markServiceUnavailable()
{
    stopCommandWebSocketClient();
    if (!m_serviceAvailable)
        return;
    m_serviceAvailable = false;
    emit serviceStatusChanged(false);
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

void IpcService::loadConnectionSettings()
{
    QSettings settings = AppSettings::create();
    const QVariant savedEndpoint = settings.value(
        QStringLiteral("pythonService/endpoint"),
        settings.value(QStringLiteral("rpa/serviceEndpoint"), m_endpoint));
    m_endpoint = normalizedEndpoint(savedEndpoint.toString());
}

void IpcService::saveConnectionSettings() const
{
    QSettings settings = AppSettings::create();
    settings.setValue(QStringLiteral("pythonService/endpoint"), m_endpoint);
    settings.remove(QStringLiteral("rpa/serviceEndpoint"));
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

QString IpcService::eventWebSocketUrl() const
{
    return QStringLiteral("ws://127.0.0.1:%1").arg(m_eventPort);
}

PlatformCommandResponse IpcService::sendPlatformCommandViaWebSocket(const PlatformCommandRequest& request, int timeoutMs)
{
    PlatformCommandResponse response;
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

    const QJsonObject payload = buildPlatformCommandPayload(request);
    qInfo() << "[IpcService] platform command WebSocket send"
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
    qInfo() << "[IpcService] platform command WebSocket response"
            << "requestId=" << request.requestId
            << "status=" << json.value(QStringLiteral("status")).toString()
            << "error=" << response.errorMessage;
    return response;
}

RpaCommandResponse IpcService::sendRpaCommandViaWebSocket(const RpaCommandRequest& request, int timeoutMs)
{
    return sendPlatformCommandViaWebSocket(request, timeoutMs);
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
            emit platformEventBridgeStateChanged(true);
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
    QElapsedTimer timer;
    timer.start();
    const QString type = payload.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("rpa_event")) {
        const QJsonObject event = payload.value(QStringLiteral("event")).toObject();
        if (!event.isEmpty()) {
            emit platformEventReceived(event);
            emit rpaEventReceived(event);
            qInfo() << "[IpcService] platform event dispatch timing"
                    << "eventId=" << event.value(QStringLiteral("event_id")).toString()
                    << "platform=" << event.value(QStringLiteral("platform")).toString()
                    << "eventType=" << event.value(QStringLiteral("event_type")).toString()
                    << "cursor=" << event.value(QStringLiteral("cursor")).toString()
                    << "elapsedMs=" << timer.elapsed();
        }
        return;
    }
    if (type == QLatin1String("hello")) {
        qInfo() << "[IpcService] event WebSocket hello"
                << payload.value(QStringLiteral("platform")).toString()
                << payload.value(QStringLiteral("account_id")).toString();
        emit platformEventBridgeStateChanged(true);
        emit rpaEventBridgeStateChanged(true);
        return;
    }
    qInfo() << "[IpcService] event WebSocket message type=" << type;
}

void IpcService::onEventSocketTextMessageReceived(const QString& message)
{
    QElapsedTimer timer;
    timer.start();
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        qWarning() << "[IpcService] event WebSocket invalid JSON";
        return;
    }
    handleEventSocketPayload(doc.object());
    qInfo() << "[IpcService] event WebSocket parse timing"
            << "bytes=" << message.toUtf8().size()
            << "elapsedMs=" << timer.elapsed();
}

void IpcService::onEventSocketDisconnected()
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket)
        return;
    m_eventSockets.remove(socket);
    qInfo() << "[IpcService] event WebSocket disconnected count=" << m_eventSockets.size();
    emit platformEventBridgeStateChanged(!m_eventSockets.isEmpty());
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

QJsonObject IpcService::fetchPlatformStatuses(int timeoutMs,
                                              ResponseStatus* statusOut,
                                              QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/platforms"));
    QJsonObject statuses = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] platform statuses fetched"
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "platforms=" << statuses.value(QStringLiteral("platforms")).toArray().size()
            << "error=" << (errorOut ? *errorOut : QString());
    return statuses;
}

QJsonObject IpcService::fetchCacheSnapshot(const QString& platform,
                                           int conversationLimit,
                                           int messageLimit,
                                           const QString& cursor,
                                           int timeoutMs,
                                           ResponseStatus* statusOut,
                                           QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/cache/snapshot"));
    QUrlQuery query;
    if (!platform.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("platform"), platform.trimmed().toLower());
    if (!cursor.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("cursor"), cursor.trimmed());
    query.addQueryItem(QStringLiteral("conversation_limit"), QString::number(qMax(1, conversationLimit)));
    query.addQueryItem(QStringLiteral("message_limit"), QString::number(qMax(1, messageLimit)));
    url.setQuery(query);

    QJsonObject snapshot = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] cache snapshot fetched"
            << "platform=" << platform
            << "cursor=" << cursor
            << "sourceRole=" << snapshot.value(QStringLiteral("source_role")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "conversations=" << snapshot.value(QStringLiteral("conversation_count")).toInt()
            << "messages=" << snapshot.value(QStringLiteral("message_count")).toInt();
    return snapshot;
}

QJsonObject IpcService::fetchConversationList(const QString& platform,
                                              int conversationLimit,
                                              int timeoutMs,
                                              ResponseStatus* statusOut,
                                              QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/conversations/list"));
    QUrlQuery query;
    if (!platform.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("platform"), platform.trimmed().toLower());
    query.addQueryItem(QStringLiteral("conversation_limit"), QString::number(qMax(1, conversationLimit)));
    url.setQuery(query);

    QJsonObject response = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] conversation list fetched"
            << "platform=" << platform
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "conversations=" << response.value(QStringLiteral("conversation_count")).toInt()
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::fetchConversationMessages(const QString& platform,
                                                  const QString& conversationKey,
                                                  int messageLimit,
                                                  int timeoutMs,
                                                  ResponseStatus* statusOut,
                                                  QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/conversations/messages"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("platform"), platform.trimmed().toLower());
    query.addQueryItem(QStringLiteral("conversation_key"), conversationKey.trimmed());
    query.addQueryItem(QStringLiteral("message_limit"), QString::number(qMax(1, messageLimit)));
    url.setQuery(query);

    QJsonObject response = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] conversation messages fetched"
            << "platform=" << platform
            << "conversationKey=" << conversationKey
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "messages=" << response.value(QStringLiteral("message_count")).toInt()
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::fetchKnowledgeBases(int timeoutMs, ResponseStatus* statusOut, QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/bases"));
    QJsonObject response = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge bases fetched"
            << "bases=" << response.value(QStringLiteral("bases")).toArray().size()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::createKnowledgeBase(const QJsonObject& payload,
                                            int timeoutMs,
                                            ResponseStatus* statusOut,
                                            QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/bases"));
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge base create"
            << "name=" << payload.value(QStringLiteral("name")).toString()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::updateKnowledgeBase(const QJsonObject& payload,
                                            int timeoutMs,
                                            ResponseStatus* statusOut,
                                            QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/bases/update"));
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge base update"
            << "baseId=" << payload.value(QStringLiteral("id")).toString()
            << "name=" << payload.value(QStringLiteral("name")).toString()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::setKnowledgeBaseEnabled(const QString& baseId,
                                                bool enabled,
                                                int timeoutMs,
                                                ResponseStatus* statusOut,
                                                QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/bases/set_enabled"));
    QJsonObject payload;
    payload.insert(QStringLiteral("id"), baseId.trimmed());
    payload.insert(QStringLiteral("enabled"), enabled);
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge base set enabled"
            << "baseId=" << baseId
            << "enabled=" << enabled
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::deleteKnowledgeBase(const QString& baseId,
                                            int timeoutMs,
                                            ResponseStatus* statusOut,
                                            QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/bases/delete"));
    QJsonObject payload;
    payload.insert(QStringLiteral("id"), baseId.trimmed());
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge base delete"
            << "baseId=" << baseId
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::fetchKnowledgePlatformBindings(const QString& platform,
                                                       int timeoutMs,
                                                       ResponseStatus* statusOut,
                                                       QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/platform_bindings"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("platform"), platform.trimmed().toLower());
    url.setQuery(query);

    QJsonObject response = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge platform bindings fetched"
            << "platform=" << platform
            << "baseIds=" << response.value(QStringLiteral("base_ids")).toArray().size()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::saveKnowledgePlatformBindings(const QString& platform,
                                                      const QStringList& baseIds,
                                                      int timeoutMs,
                                                      ResponseStatus* statusOut,
                                                      QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/platform_bindings"));
    QJsonArray baseIdArray;
    for (const QString& baseId : baseIds) {
        const QString trimmed = baseId.trimmed();
        if (!trimmed.isEmpty())
            baseIdArray.append(trimmed);
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("platform"), platform.trimmed().toLower());
    payload.insert(QStringLiteral("base_ids"), baseIdArray);

    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge platform bindings saved"
            << "platform=" << platform
            << "baseIds=" << baseIdArray.size()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::importKnowledgeDirectory(const QString& directory,
                                                 const QString& baseName,
                                                 const QString& shopId,
                                                 const QString& scene,
                                                 int timeoutMs,
                                                 bool asyncTask,
                                                 ResponseStatus* statusOut,
                                                 QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/import_directory"));
    QJsonObject payload;
    payload.insert(QStringLiteral("directory"), directory.trimmed());
    payload.insert(QStringLiteral("base_name"), baseName.trimmed().isEmpty()
                       ? QStringLiteral("键盘键帽客服知识库PoC")
                       : baseName.trimmed());
    payload.insert(QStringLiteral("shop_id"), shopId.trimmed());
    payload.insert(QStringLiteral("scene"), scene.trimmed().isEmpty() ? QStringLiteral("reply_draft") : scene.trimmed());
    payload.insert(QStringLiteral("async"), asyncTask);

    QElapsedTimer timer;
    timer.start();
    qInfo() << "[IpcService] knowledge directory import request"
            << "url=" << url.toString()
            << "directory=" << directory
            << "baseName=" << payload.value(QStringLiteral("base_name")).toString()
            << "shopId=" << shopId
            << "scene=" << payload.value(QStringLiteral("scene")).toString()
            << "async=" << asyncTask
            << "timeoutMs=" << timeoutMs;
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge directory import"
            << "elapsedMs=" << timer.elapsed()
            << "directory=" << directory
            << "documents=" << response.value(QStringLiteral("documents")).toArray().size()
            << "images=" << response.value(QStringLiteral("images")).toArray().size()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "detail=" << response.value(QStringLiteral("detail")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::importKnowledgeDirectoryForBase(const QString& directory,
                                                        const QString& baseId,
                                                        const QString& baseName,
                                                        const QString& applicableShops,
                                                        const QString& scene,
                                                        int timeoutMs,
                                                        bool asyncTask,
                                                        ResponseStatus* statusOut,
                                                        QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/import_directory"));
    QJsonObject payload;
    payload.insert(QStringLiteral("directory"), directory.trimmed());
    payload.insert(QStringLiteral("base_id"), baseId.trimmed());
    payload.insert(QStringLiteral("base_name"), baseName.trimmed().isEmpty()
                       ? QStringLiteral("键盘键帽客服知识库PoC")
                       : baseName.trimmed());
    payload.insert(QStringLiteral("applicable_shops"), applicableShops.trimmed());
    payload.insert(QStringLiteral("scene"), scene.trimmed().isEmpty() ? QStringLiteral("reply_draft") : scene.trimmed());
    payload.insert(QStringLiteral("async"), asyncTask);

    QElapsedTimer timer;
    timer.start();
    qInfo() << "[IpcService] knowledge directory import for base request"
            << "url=" << url.toString()
            << "directory=" << directory
            << "baseId=" << baseId
            << "baseName=" << payload.value(QStringLiteral("base_name")).toString()
            << "async=" << asyncTask
            << "timeoutMs=" << timeoutMs;
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge directory import for base"
            << "elapsedMs=" << timer.elapsed()
            << "baseId=" << baseId
            << "documents=" << response.value(QStringLiteral("documents")).toArray().size()
            << "images=" << response.value(QStringLiteral("images")).toArray().size()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "detail=" << response.value(QStringLiteral("detail")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::fetchKnowledgeImportTask(const QString& taskId,
                                                 int timeoutMs,
                                                 ResponseStatus* statusOut,
                                                 QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/import_task"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("task_id"), taskId.trimmed());
    url.setQuery(query);

    QJsonObject response = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge import task fetched"
            << "taskId=" << taskId
            << "taskStatus=" << response.value(QStringLiteral("status")).toString()
            << "importStatus=" << response.value(QStringLiteral("task_status")).toString()
            << "documents=" << response.value(QStringLiteral("documents")).toArray().size()
            << "images=" << response.value(QStringLiteral("images")).toArray().size()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::fetchKnowledgeDocuments(const QString& baseId,
                                                int timeoutMs,
                                                ResponseStatus* statusOut,
                                                QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/documents"));
    QUrlQuery query;
    if (!baseId.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("base_id"), baseId.trimmed());
    url.setQuery(query);

    QJsonObject response = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge documents fetched"
            << "documents=" << response.value(QStringLiteral("documents")).toArray().size()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::fetchKnowledgeImages(const QString& baseId,
                                             int timeoutMs,
                                             ResponseStatus* statusOut,
                                             QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/images"));
    QUrlQuery query;
    if (!baseId.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("base_id"), baseId.trimmed());
    url.setQuery(query);

    QJsonObject response = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge images fetched"
            << "images=" << response.value(QStringLiteral("images")).toArray().size()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::searchKnowledgeImages(const QString& queryText,
                                              const QString& platform,
                                              const QString& shopId,
                                              const QString& scene,
                                              int topK,
                                              int timeoutMs,
                                              ResponseStatus* statusOut,
                                              QString* errorOut,
                                              const QStringList& baseIds)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/images/search"));
    QJsonObject payload;
    payload.insert(QStringLiteral("query"), queryText.trimmed());
    payload.insert(QStringLiteral("platform"), platform.trimmed().toLower());
    payload.insert(QStringLiteral("shop_id"), shopId.trimmed());
    payload.insert(QStringLiteral("scene"), scene.trimmed().isEmpty() ? QStringLiteral("reply_draft") : scene.trimmed());
    payload.insert(QStringLiteral("top_k"), qMax(1, topK));
    QJsonArray baseIdArray;
    for (const QString& baseId : baseIds) {
        const QString trimmed = baseId.trimmed();
        if (!trimmed.isEmpty())
            baseIdArray.append(trimmed);
    }
    payload.insert(QStringLiteral("base_ids"), baseIdArray);

    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge image search"
            << "queryChars=" << queryText.size()
            << "baseIds=" << baseIdArray.size()
            << "results=" << response.value(QStringLiteral("results")).toArray().size()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::searchKnowledge(const QString& queryText,
                                        const QString& platform,
                                        const QString& shopId,
                                        const QString& scene,
                                        int topK,
                                        int timeoutMs,
                                        ResponseStatus* statusOut,
                                        QString* errorOut,
                                        const QStringList& baseIds)
{
    QUrl url(m_endpoint + QStringLiteral("/api/knowledge/search"));
    QJsonObject payload;
    payload.insert(QStringLiteral("query"), queryText.trimmed());
    payload.insert(QStringLiteral("platform"), platform.trimmed().toLower());
    payload.insert(QStringLiteral("shop_id"), shopId.trimmed());
    payload.insert(QStringLiteral("scene"), scene.trimmed().isEmpty() ? QStringLiteral("reply_draft") : scene.trimmed());
    payload.insert(QStringLiteral("top_k"), qMax(1, topK));
    QJsonArray baseIdArray;
    for (const QString& baseId : baseIds) {
        const QString trimmed = baseId.trimmed();
        if (!trimmed.isEmpty())
            baseIdArray.append(trimmed);
    }
    payload.insert(QStringLiteral("base_ids"), baseIdArray);

    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] knowledge search"
            << "queryChars=" << queryText.size()
            << "baseIds=" << baseIdArray.size()
            << "results=" << response.value(QStringLiteral("results")).toArray().size()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::fetchEmailConfig(int timeoutMs, ResponseStatus* statusOut, QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/email/config"));
    QJsonObject response = performJsonGet(url, timeoutMs, statusOut, errorOut);
    const QJsonObject config = response.value(QStringLiteral("config")).toObject();
    qInfo() << "[IpcService] email config fetched"
            << "provider=" << config.value(QStringLiteral("provider")).toString()
            << "sender=" << config.value(QStringLiteral("sender_email_masked")).toString()
            << "enabled=" << config.value(QStringLiteral("enabled")).toBool()
            << "authCodeSaved=" << config.value(QStringLiteral("auth_code_saved")).toBool()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::saveEmailConfig(const QJsonObject& payload,
                                        int timeoutMs,
                                        ResponseStatus* statusOut,
                                        QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/email/config"));
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] email config saved"
            << "provider=" << payload.value(QStringLiteral("provider")).toString()
            << "senderChars=" << payload.value(QStringLiteral("sender_email")).toString().size()
            << "smtpHost=" << payload.value(QStringLiteral("smtp_host")).toString()
            << "smtpPort=" << payload.value(QStringLiteral("smtp_port")).toInt()
            << "security=" << payload.value(QStringLiteral("security")).toString()
            << "enabled=" << payload.value(QStringLiteral("enabled")).toBool()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::sendTestEmail(const QString& toEmail,
                                      int timeoutMs,
                                      ResponseStatus* statusOut,
                                      QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/email/test"));
    QJsonObject payload;
    payload.insert(QStringLiteral("to"), toEmail.trimmed());
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] email test sent"
            << "toChars=" << toEmail.trimmed().size()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "errorCode=" << response.value(QStringLiteral("error")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::sendEmail(const QString& toEmail,
                                  const QString& scene,
                                  int conversationId,
                                  const QString& traceId,
                                  const QString& templateId,
                                  int timeoutMs,
                                  ResponseStatus* statusOut,
                                  QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/email/send"));
    QJsonObject payload;
    payload.insert(QStringLiteral("to"), toEmail.trimmed());
    payload.insert(QStringLiteral("scene"),
                   scene.trimmed().isEmpty() ? QStringLiteral("link_request") : scene.trimmed());
    if (conversationId > 0)
        payload.insert(QStringLiteral("conversation_id"), conversationId);
    if (!traceId.trimmed().isEmpty())
        payload.insert(QStringLiteral("trace_id"), traceId.trimmed());
    if (!templateId.trimmed().isEmpty())
        payload.insert(QStringLiteral("template_id"), templateId.trimmed());

    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] workflow email sent"
            << "toChars=" << toEmail.trimmed().size()
            << "scene=" << payload.value(QStringLiteral("scene")).toString()
            << "templateId=" << templateId.trimmed()
            << "conversationId=" << conversationId
            << "traceId=" << traceId
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "errorCode=" << response.value(QStringLiteral("error")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::fetchEmailTemplates(bool includeBody,
                                            int timeoutMs,
                                            ResponseStatus* statusOut,
                                            QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/email/templates"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("include_body"), includeBody ? QStringLiteral("1") : QStringLiteral("0"));
    url.setQuery(query);
    QJsonObject response = performJsonGet(url, timeoutMs, statusOut, errorOut);
    const int count = response.value(QStringLiteral("templates")).toArray().size();
    qInfo() << "[IpcService] email templates fetched"
            << "includeBody=" << includeBody
            << "count=" << count
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::saveEmailTemplate(const QJsonObject& payload,
                                          int timeoutMs,
                                          ResponseStatus* statusOut,
                                          QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/email/templates"));
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] email template saved"
            << "templateId=" << payload.value(QStringLiteral("template_id")).toString()
            << "scene=" << payload.value(QStringLiteral("scene")).toString()
            << "subjectChars=" << payload.value(QStringLiteral("subject")).toString().size()
            << "bodyChars=" << payload.value(QStringLiteral("body")).toString().size()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::deleteEmailTemplate(const QString& templateId,
                                            int timeoutMs,
                                            ResponseStatus* statusOut,
                                            QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/email/templates/delete"));
    QJsonObject payload;
    payload.insert(QStringLiteral("template_id"), templateId.trimmed());
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] email template deleted"
            << "templateId=" << templateId.trimmed()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::importEmailTemplates(const QString& text,
                                             int timeoutMs,
                                             ResponseStatus* statusOut,
                                             QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/email/templates/import"));
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), text);
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] email templates imported"
            << "textChars=" << text.size()
            << "count=" << response.value(QStringLiteral("count")).toInt()
            << "responseStatus=" << response.value(QStringLiteral("status")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::fetchPlatformReplay(const QString& platform,
                                            const QString& cursor,
                                            int limit,
                                            int timeoutMs,
                                            ResponseStatus* statusOut,
                                            QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/platform/replay"));
    QUrlQuery query;
    if (!platform.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("platform"), platform.trimmed().toLower());
    if (!cursor.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("cursor"), cursor.trimmed());
    query.addQueryItem(QStringLiteral("limit"), QString::number(qMax(1, limit)));
    url.setQuery(query);

    QJsonObject replay = performJsonGet(url, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] platform replay fetched"
            << "platform=" << platform
            << "cursor=" << cursor
            << "sourceRole=" << replay.value(QStringLiteral("source_role")).toString()
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "events=" << replay.value(QStringLiteral("event_count")).toInt()
            << "nextCursor=" << replay.value(QStringLiteral("cursor")).toString();
    return replay;
}

QJsonObject IpcService::fetchRpaReplay(const QString& platform,
                                       const QString& cursor,
                                       int limit,
                                       int timeoutMs,
                                       ResponseStatus* statusOut,
                                       QString* errorOut)
{
    return fetchPlatformReplay(platform, cursor, limit, timeoutMs, statusOut, errorOut);
}

QJsonObject IpcService::clearConversationMessages(const QString& platform,
                                                  const QString& accountId,
                                                  const QString& conversationKey,
                                                  int timeoutMs,
                                                  ResponseStatus* statusOut,
                                                  QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/conversations/clear_messages"));
    QJsonObject payload;
    payload.insert(QStringLiteral("platform"), platform.trimmed().toLower());
    payload.insert(QStringLiteral("account_id"), accountId.trimmed());
    payload.insert(QStringLiteral("conversation_key"), conversationKey.trimmed());
    payload.insert(QStringLiteral("operator"), QStringLiteral("cpp_client"));
    payload.insert(QStringLiteral("reason"), QStringLiteral("aggregate_context_menu"));
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] clear conversation messages"
            << "platform=" << platform
            << "conversationKey=" << conversationKey
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

QJsonObject IpcService::deleteConversationOnService(const QString& platform,
                                                    const QString& accountId,
                                                    const QString& conversationKey,
                                                    int timeoutMs,
                                                    ResponseStatus* statusOut,
                                                    QString* errorOut)
{
    QUrl url(m_endpoint + QStringLiteral("/api/conversations/delete"));
    QJsonObject payload;
    payload.insert(QStringLiteral("platform"), platform.trimmed().toLower());
    payload.insert(QStringLiteral("account_id"), accountId.trimmed());
    payload.insert(QStringLiteral("conversation_key"), conversationKey.trimmed());
    payload.insert(QStringLiteral("operator"), QStringLiteral("cpp_client"));
    payload.insert(QStringLiteral("reason"), QStringLiteral("aggregate_context_menu"));
    QJsonObject response = performJsonPost(url, payload, timeoutMs, statusOut, errorOut);
    qInfo() << "[IpcService] delete conversation"
            << "platform=" << platform
            << "conversationKey=" << conversationKey
            << "status=" << (statusOut ? Ipc::toString(*statusOut) : QStringLiteral("unknown"))
            << "error=" << (errorOut ? *errorOut : QString());
    return response;
}

bool IpcService::dispatchPlatformEvent(const QJsonObject& event, bool replayed)
{
    if (event.isEmpty())
        return false;

    QJsonObject dispatched = event;
    if (replayed)
        dispatched.insert(QStringLiteral("replayed"), true);
    emit platformEventReceived(dispatched);
    emit rpaEventReceived(dispatched);
    qInfo() << "[IpcService] platform event dispatched"
            << "eventId=" << dispatched.value(QStringLiteral("event_id")).toString()
            << "platform=" << dispatched.value(QStringLiteral("platform")).toString()
            << "eventType=" << dispatched.value(QStringLiteral("event_type")).toString()
            << "replayed=" << replayed;
    return true;
}

int IpcService::dispatchPlatformReplayEvents(const QJsonObject& replay)
{
    if (replay.value(QStringLiteral("status")).toString() != QLatin1String("success"))
        return 0;

    int dispatched = 0;
    const QJsonArray events = replay.value(QStringLiteral("events")).toArray();
    for (const QJsonValue& value : events) {
        if (dispatchPlatformEvent(value.toObject(), true))
            ++dispatched;
    }
    qInfo() << "[IpcService] platform replay dispatched"
            << "platform=" << replay.value(QStringLiteral("platform")).toString()
            << "events=" << dispatched
            << "cursor=" << replay.value(QStringLiteral("cursor")).toString();
    return dispatched;
}

int IpcService::dispatchRpaReplayEvents(const QJsonObject& replay)
{
    return dispatchPlatformReplayEvents(replay);
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
    payload[QStringLiteral("platform")] = request.platform;
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

QJsonObject IpcService::buildPlatformCommandPayload(const PlatformCommandRequest& request) const
{
    QJsonObject payload;
    payload[QStringLiteral("request_id")] = request.requestId;
    payload[QStringLiteral("client_message_id")] = request.taskId;
    payload[QStringLiteral("command")] = request.commandType;
    payload[QStringLiteral("platform")] = request.platform;
    payload[QStringLiteral("account_id")] = request.accountId;
    payload[QStringLiteral("task_id")] = request.taskId;
    payload[QStringLiteral("target_window")] = request.targetWindow;
    payload[QStringLiteral("parameters")] = request.parameters;
    return payload;
}

QJsonObject IpcService::buildRpaCommandPayload(const RpaCommandRequest& request) const
{
    return buildPlatformCommandPayload(request);
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
        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject())
            obj = doc.object();
        if (statusOut) *statusOut = ResponseStatus::Error;
        if (errorOut) {
            const QString detail = obj.value(QStringLiteral("detail")).toString(
                obj.value(QStringLiteral("error")).toString());
            *errorOut = detail.isEmpty() ? reply->errorString() : detail;
        }
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
    QElapsedTimer timer;
    timer.start();
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
        qWarning() << "[IpcService] HTTP POST timeout"
                   << "url=" << url.toString()
                   << "elapsedMs=" << timer.elapsed()
                   << "timeoutMs=" << timeoutMs
                   << "payloadKeys=" << payload.keys();
        reply->deleteLater();
        return obj;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject())
            obj = doc.object();
        if (statusOut) *statusOut = ResponseStatus::Error;
        if (errorOut) {
            const QString detail = obj.value(QStringLiteral("detail")).toString(
                obj.value(QStringLiteral("error")).toString());
            *errorOut = detail.isEmpty() ? reply->errorString() : detail;
        }
        qWarning() << "[IpcService] HTTP POST failed"
                   << "url=" << url.toString()
                   << "elapsedMs=" << timer.elapsed()
                   << "networkError=" << reply->error()
                   << "httpStatus=" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                   << "error=" << (errorOut ? *errorOut : reply->errorString())
                   << "bodyBytes=" << body.size();
        reply->deleteLater();
        return obj;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        if (statusOut) *statusOut = ResponseStatus::Error;
        if (errorOut) *errorOut = QStringLiteral("invalid_json_response");
        qWarning() << "[IpcService] HTTP POST invalid JSON"
                   << "url=" << url.toString()
                   << "elapsedMs=" << timer.elapsed()
                   << "bodyBytes=" << body.size();
        reply->deleteLater();
        return obj;
    }

    if (statusOut) *statusOut = ResponseStatus::Success;
    if (errorOut) errorOut->clear();
    obj = doc.object();
    qInfo() << "[IpcService] HTTP POST success"
            << "url=" << url.toString()
            << "elapsedMs=" << timer.elapsed()
            << "httpStatus=" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            << "bodyBytes=" << body.size();
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
