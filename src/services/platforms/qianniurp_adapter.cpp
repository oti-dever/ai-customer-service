#include "qianniurp_adapter.h"
#include "../../ipc/ipcservice.h"
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>
#include <QUuid>

namespace {
const QString kQianniuSidecarPlatform = QStringLiteral("qianniu");

QString normalizedDirection(const QString& direction, const QString& senderRole, const QJsonObject& payload)
{
    auto norm = [](const QString& value) {
        return value.trimmed().toLower();
    };
    const QString d = norm(direction);
    const QString s = norm(senderRole);
    const QString metaDirection = norm(payload.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("direction")).toString());
    const QString metaSenderRole = norm(payload.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("sender_role")).toString());

    const QStringList rolePriority = { s, metaSenderRole };
    for (const QString& role : rolePriority) {
        if (role == QLatin1String("agent"))
            return QStringLiteral("out");
        if (role == QLatin1String("customer"))
            return QStringLiteral("in");
        if (role == QLatin1String("system"))
            return QStringLiteral("system");
    }

    const QStringList directionPriority = { d, metaDirection };
    for (const QString& value : directionPriority) {
        if (value == QLatin1String("outbound") || value == QLatin1String("out"))
            return QStringLiteral("out");
        if (value == QLatin1String("inbound") || value == QLatin1String("in"))
            return QStringLiteral("in");
        if (value == QLatin1String("system"))
            return QStringLiteral("system");
    }

    return QStringLiteral("in");
}
} // namespace

QianniuRPAAdapter::QianniuRPAAdapter(QObject* parent)
    : IPlatformAdapter(parent)
{
    connect(&Ipc::IpcService::instance(), &Ipc::IpcService::platformEventReceived,
            this, &QianniuRPAAdapter::handleRpaEvent);
    connect(&Ipc::IpcService::instance(), &Ipc::IpcService::platformEventBridgeStateChanged,
            this, [this](bool connected) {
        m_eventSocketConnected = connected;
        qInfo() << "[QianniuRPAAdapter] realtime event bridge"
                << (connected ? "connected" : "disconnected")
                << "cursor=" << m_eventCursor;
    });
}

void QianniuRPAAdapter::connectPlatform()
{
    m_connected = true;
    qInfo() << "[QianniuRPAAdapter] Qianniu adapter connected";
    emit connectionStateChanged(true);
}

void QianniuRPAAdapter::disconnectPlatform()
{
    m_connected = false;
    stopListening();
    qInfo() << "[QianniuRPAAdapter] Qianniu adapter disconnected";
    emit connectionStateChanged(false);
}

void QianniuRPAAdapter::startListening()
{
    QElapsedTimer timer;
    timer.start();
    QString serviceError;
    if (!Ipc::IpcService::instance().connectToConfiguredService(&serviceError)) {
        qWarning() << "[QianniuRPAAdapter] Python service unavailable:"
                   << serviceError << "elapsedMs=" << timer.elapsed();
        return;
    }

    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("connect");
    request.platform = kQianniuSidecarPlatform;
    request.accountId = accountId();
    request.parameters.insert(QStringLiteral("mode"), QStringLiteral("listen"));
    request.parameters.insert(QStringLiteral("emit_initial_snapshot"), false);
    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 3000);
    if (response.status != Ipc::ResponseStatus::Success) {
        qWarning() << "[QianniuRPAAdapter] connect command failed:"
                   << response.errorMessage << "elapsedMs=" << timer.elapsed();
        return;
    }

    if (!m_connected)
        connectPlatform();
    qInfo() << "[QianniuRPAAdapter] startListening with WebSocket command/event bridge"
            << "elapsedMs=" << timer.elapsed();
}

void QianniuRPAAdapter::stopListening()
{
    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("disconnect");
    request.platform = kQianniuSidecarPlatform;
    request.accountId = accountId();
    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 3000);
    if (response.status != Ipc::ResponseStatus::Success)
        qWarning() << "[QianniuRPAAdapter] disconnect command failed:" << response.errorMessage;
    qInfo() << "[QianniuRPAAdapter] stopListening";
}

void QianniuRPAAdapter::sendMessage(const QString& conversationId, const QString& text, const QString& clientMessageId)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    if (m_commandInFlight) {
        qInfo() << "[QianniuRPAAdapter] sendMessage delayed: command already in flight";
        QTimer::singleShot(200, this, [this, conversationId, text, clientMessageId]() {
            sendMessage(conversationId, text, clientMessageId);
        });
        return;
    }

    m_commandInFlight = true;
    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("send_message");
    request.platform = kQianniuSidecarPlatform;
    request.accountId = accountId();
    request.taskId = clientMessageId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : clientMessageId;
    request.parameters.insert(QStringLiteral("client_message_id"), request.taskId);
    request.parameters.insert(QStringLiteral("conversation_key"), conversationId);
    request.parameters.insert(QStringLiteral("display_name"), conversationId);
    request.parameters.insert(QStringLiteral("text"), text);
    request.parameters.insert(QStringLiteral("confirm_token"), QStringLiteral("manual_confirmed_by_agent"));

    QElapsedTimer commandTimer;
    commandTimer.start();
    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 4000);
    const qint64 commandElapsedMs = commandTimer.elapsed();
    m_commandInFlight = false;
    auto scheduleConfirmTimeout = [this, conversationId, clientMessageId = request.taskId]() {
        QTimer::singleShot(30000, this, [this, conversationId, clientMessageId]() {
            emit sendFailed(conversationId, QStringLiteral("send_confirm_timeout"), clientMessageId);
        });
    };
    if (response.status == Ipc::ResponseStatus::Success) {
        const QJsonObject result = response.result;
        const bool accepted = result.value(QStringLiteral("accepted")).toBool(false);
        const bool sent = result.value(QStringLiteral("sent")).toBool(!accepted);
        qInfo() << "[QianniuRPAAdapter] sendMessage timing"
                << "conversation=" << conversationId
                << "clientMessageId=" << request.taskId
                << "commandElapsedMs=" << commandElapsedMs
                << "totalElapsedMs=" << totalTimer.elapsed()
                << "accepted=" << accepted
                << "method=" << result.value(QStringLiteral("method")).toString()
                << "sent=" << sent;
        if (accepted && !sent) {
            scheduleConfirmTimeout();
            return;
        }
        if (!m_eventSocketConnected) {
            QTimer::singleShot(300, this, [this, conversationId, text, clientMessageId = request.taskId]() {
                emit messageSent(conversationId, text, clientMessageId);
            });
        }
        return;
    }

    if (response.status == Ipc::ResponseStatus::Timeout
        && response.errorMessage == QLatin1String("request_timeout")) {
        qWarning() << "[QianniuRPAAdapter] sendMessage command timed out; waiting for result event"
                   << "conversation=" << conversationId
                   << "clientMessageId=" << request.taskId
                   << "commandElapsedMs=" << commandElapsedMs
                   << "totalElapsedMs=" << totalTimer.elapsed();
        scheduleConfirmTimeout();
        return;
    }

    qWarning() << "[QianniuRPAAdapter] sendMessage failed:"
               << response.errorMessage
               << "commandElapsedMs=" << commandElapsedMs
               << "totalElapsedMs=" << totalTimer.elapsed();
    emit sendFailed(conversationId, response.errorMessage.isEmpty()
                                    ? QStringLiteral("qianniu_sidecar_command_failed")
                                    : response.errorMessage,
                    request.taskId);
}

void QianniuRPAAdapter::handleRpaEvent(const QJsonObject& event)
{
    QElapsedTimer timer;
    timer.start();
    if (event.value(QStringLiteral("platform")).toString().trimmed().toLower() != kQianniuSidecarPlatform)
        return;

    const QString seq = QString::number(event.value(QStringLiteral("seq")).toInt());
    const QString eventId = event.value(QStringLiteral("event_id")).toString();
    const QString dedupeKey = !eventId.isEmpty() ? eventId : seq;
    if (!dedupeKey.isEmpty() && m_seenSeqs.contains(dedupeKey))
        return;
    if (!dedupeKey.isEmpty())
        m_seenSeqs.insert(dedupeKey);

    const QString cursor = event.value(QStringLiteral("cursor")).toString();
    if (!cursor.isEmpty())
        m_eventCursor = cursor;
    else if (event.value(QStringLiteral("seq")).isDouble())
        m_eventCursor = seq;

    const QString type = event.value(QStringLiteral("event_type")).toString();
    const QJsonObject payloadObject = event.value(QStringLiteral("payload")).toObject();
    const QString clientMessageId = event.value(QStringLiteral("client_message_id")).toString(
        payloadObject.value(QStringLiteral("client_message_id")).toString(
            event.value(QStringLiteral("task_id")).toString(
                payloadObject.value(QStringLiteral("task_id")).toString())));
    qInfo() << "[QianniuRPAAdapter] realtime event received"
            << "type=" << type
            << "eventId=" << eventId
            << "seq=" << seq
            << "cursor=" << m_eventCursor
            << "clientMessageId=" << clientMessageId;

    if (type == QLatin1String("conversation_observed")) {
        emitConversationObserved(event);
        qInfo() << "[QianniuRPAAdapter] event timing"
                << "type=" << type
                << "eventId=" << eventId
                << "elapsedMs=" << timer.elapsed();
        return;
    }

    if (type == QLatin1String("message_observed")) {
        QElapsedTimer convertTimer;
        convertTimer.start();
        const PlatformMessage msg = platformMessageFromEvent(event);
        const qint64 convertElapsedMs = convertTimer.elapsed();
        if (!msg.platformConversationId.isEmpty() && !msg.content.isEmpty())
            emit incomingMessage(msg);
        qInfo() << "[QianniuRPAAdapter] event timing"
                << "type=" << type
                << "eventId=" << eventId
                << "convertElapsedMs=" << convertElapsedMs
                << "elapsedMs=" << timer.elapsed()
                << "emitted=" << (!msg.platformConversationId.isEmpty() && !msg.content.isEmpty());
        return;
    }

    if (type == QLatin1String("conversation_messages_cleared")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        if (!conversation.isEmpty())
            emit conversationMessagesCleared(conversation);
        qInfo() << "[QianniuRPAAdapter] conversation cleared event"
                << "conversation=" << conversation
                << "eventId=" << eventId
                << "elapsedMs=" << timer.elapsed();
        return;
    }

    if (type == QLatin1String("conversation_deleted")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        if (!conversation.isEmpty())
            emit conversationDeleted(conversation);
        qInfo() << "[QianniuRPAAdapter] conversation deleted event"
                << "conversation=" << conversation
                << "eventId=" << eventId
                << "elapsedMs=" << timer.elapsed();
        return;
    }

    if (type == QLatin1String("message_sent")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        if (!conversation.isEmpty()) {
            qInfo() << "[QianniuRPAAdapter] message_sent event"
                    << "conversation=" << conversation
                    << "clientMessageId=" << clientMessageId;
            emit messageSent(conversation, QString(), clientMessageId);
        }
        qInfo() << "[QianniuRPAAdapter] event timing"
                << "type=" << type
                << "eventId=" << eventId
                << "elapsedMs=" << timer.elapsed();
        return;
    }

    if (type == QLatin1String("send_failed")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        QString reason = payloadObject.value(QStringLiteral("error_message")).toString();
        if (reason.isEmpty())
            reason = payloadObject.value(QStringLiteral("error")).toString();
        if (reason.isEmpty())
            reason = payloadObject.value(QStringLiteral("status")).toString();
        if (!conversation.isEmpty()) {
            qInfo() << "[QianniuRPAAdapter] send_failed event"
                    << "conversation=" << conversation
                    << "clientMessageId=" << clientMessageId
                    << "reason=" << reason;
            emit sendFailed(conversation,
                            reason.isEmpty() ? QStringLiteral("qianniu_sidecar_send_failed") : reason,
                            clientMessageId);
        }
        qInfo() << "[QianniuRPAAdapter] event timing"
                << "type=" << type
                << "eventId=" << eventId
                << "elapsedMs=" << timer.elapsed();
        return;
    }

    if (type == QLatin1String("account_health_changed")) {
        const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
        qInfo() << "[QianniuRPAAdapter] account health changed"
                << "healthy=" << payload.value(QStringLiteral("healthy")).toBool(false)
                << "status=" << payload.value(QStringLiteral("status")).toString()
                << "message=" << payload.value(QStringLiteral("message")).toString();
        qInfo() << "[QianniuRPAAdapter] event timing"
                << "type=" << type
                << "eventId=" << eventId
                << "elapsedMs=" << timer.elapsed();
        return;
    }

    qInfo() << "[QianniuRPAAdapter] unhandled realtime event type=" << type;
}

void QianniuRPAAdapter::emitConversationObserved(const QJsonObject& event)
{
    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    const QString conversationKey = event.value(QStringLiteral("conversation_key")).toString();
    const QString normalizedConversation = normalizeConversationKey(conversationKey);
    if (normalizedConversation.isEmpty())
        return;

    ConversationInfo info;
    info.platform = platformName();
    info.platformConversationId = normalizedConversation;
    info.customerName = payload.value(QStringLiteral("display_name")).toString(normalizedConversation);
    info.status = QStringLiteral("active");
    info.accountId = event.value(QStringLiteral("account_id")).toString();
    info.sourceType = payload.value(QStringLiteral("source_type")).toString(QStringLiteral("ui_observed"));
    info.confidence = payload.value(QStringLiteral("confidence")).toInt(70);
    info.updatedAt = QDateTime::fromString(event.value(QStringLiteral("occurred_at")).toString(), Qt::ISODateWithMs);
    if (!info.updatedAt.isValid())
        info.updatedAt = QDateTime::currentDateTime();
    info.createdAt = info.updatedAt;

    emit conversationObserved(info);
}

PlatformMessage QianniuRPAAdapter::platformMessageFromEvent(const QJsonObject& event) const
{
    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    const QString conversationKey = event.value(QStringLiteral("conversation_key")).toString();
    const QString normalizedConversation = normalizeConversationKey(conversationKey);
    const QString displayName = payload.value(QStringLiteral("sender_name")).toString(normalizedConversation);
    const QString content = payload.value(QStringLiteral("content")).toString();
    const QString rawDirection = payload.value(QStringLiteral("direction")).toString();
    const QString rawSenderRole = payload.value(QStringLiteral("sender_role")).toString();
    const QString direction = normalizedDirection(rawDirection, rawSenderRole, payload);

    PlatformMessage msg;
    msg.platform = platformName();
    msg.platformConversationId = normalizedConversation;
    msg.customerName = normalizedConversation;
    msg.content = content;
    msg.direction = direction;
    msg.sender = msg.direction == QLatin1String("in")
        ? QStringLiteral("customer")
        : (msg.direction == QLatin1String("system") ? QStringLiteral("system") : QStringLiteral("agent"));
    msg.createdAt = QDateTime::fromString(event.value(QStringLiteral("occurred_at")).toString(), Qt::ISODateWithMs);
    if (!msg.createdAt.isValid())
        msg.createdAt = QDateTime::currentDateTime();
    msg.platformMsgId = payload.value(QStringLiteral("platform_msg_id")).toString();
    msg.senderName = displayName;
    msg.originalTimestamp = payload.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("timestamp")).toString();
    msg.contentImagePath = payload.value(QStringLiteral("evidence_ref")).toString();
    msg.sourceType = payload.value(QStringLiteral("source_type")).toString(QStringLiteral("ui_observed"));
    msg.confidence = payload.value(QStringLiteral("confidence")).toInt(70);
    msg.verificationStatus = payload.value(QStringLiteral("verification_status")).toString(QStringLiteral("unverified"));
    msg.contentType = payload.value(QStringLiteral("content_type")).toString(QStringLiteral("text"));
    msg.metadata = payload;
    msg.metadata.insert(QStringLiteral("_event_account_id"), event.value(QStringLiteral("account_id")).toString());
    msg.metadata.insert(QStringLiteral("_event_conversation_key"), conversationKey);
    msg.metadata.insert(QStringLiteral("raw_direction"), rawDirection);
    msg.metadata.insert(QStringLiteral("raw_sender_role"), rawSenderRole);
    msg.metadata.insert(QStringLiteral("normalized_direction"), direction);
    return msg;
}

QString QianniuRPAAdapter::normalizeConversationKey(const QString& conversationKey) const
{
    if (conversationKey.startsWith(QStringLiteral("qianniu:"))) {
        const int lastSep = conversationKey.lastIndexOf(QLatin1Char(':'));
        if (lastSep >= 0 && lastSep + 1 < conversationKey.size())
            return conversationKey.mid(lastSep + 1);
    }
    return conversationKey;
}

