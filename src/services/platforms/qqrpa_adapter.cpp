#include "qqrpa_adapter.h"
#include "../../ipc/ipcservice.h"
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>
#include <QUuid>

namespace {
const QString kQQSidecarPlatform = QStringLiteral("qq");

QString normalizedDirection(const QString& direction, const QString& senderRole, const QJsonObject& payload)
{
    auto norm = [](const QString& value) {
        return value.trimmed().toLower();
    };
    const QString d = norm(direction);
    const QString s = norm(senderRole);
    const QJsonObject metadata = payload.value(QStringLiteral("metadata")).toObject();
    const QString metaDirection = norm(metadata.value(QStringLiteral("direction")).toString());
    const QString metaSenderRole = norm(metadata.value(QStringLiteral("sender_role")).toString());

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

QString displayNameFromConversationKey(const QString& conversationKey)
{
    const QString trimmed = conversationKey.trimmed();
    if (trimmed.startsWith(QStringLiteral("qq:"))) {
        const int lastSep = trimmed.lastIndexOf(QLatin1Char(':'));
        if (lastSep >= 0 && lastSep + 1 < trimmed.size())
            return trimmed.mid(lastSep + 1).trimmed();
    }
    return trimmed;
}

QString firstNonEmpty(const QStringList& values)
{
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return QString();
}

QString outgoingContentType(const OutgoingMessagePart& part)
{
    switch (part.type) {
    case OutgoingPartType::Text:
        return QStringLiteral("text");
    case OutgoingPartType::Image:
        return QStringLiteral("image");
    case OutgoingPartType::Video:
        return QStringLiteral("video");
    case OutgoingPartType::File:
        return QStringLiteral("file");
    }
    return QStringLiteral("text");
}
} // namespace

QQRPAAdapter::QQRPAAdapter(QObject* parent)
    : IPlatformAdapter(parent)
{
    connect(&Ipc::IpcService::instance(), &Ipc::IpcService::platformEventReceived,
            this, &QQRPAAdapter::handleRpaEvent);
    connect(&Ipc::IpcService::instance(), &Ipc::IpcService::platformEventBridgeStateChanged,
            this, [this](bool connected) {
        m_eventSocketConnected = connected;
        qInfo() << "[QQRPAAdapter] realtime event bridge"
                << (connected ? "connected" : "disconnected")
                << "cursor=" << m_eventCursor;
    });
}

void QQRPAAdapter::connectPlatform()
{
    m_connected = true;
    qInfo() << "[QQRPAAdapter] QQ adapter connected";
    emit connectionStateChanged(true);
}

void QQRPAAdapter::disconnectPlatform()
{
    m_connected = false;
    stopListening();
    qInfo() << "[QQRPAAdapter] QQ adapter disconnected";
    emit connectionStateChanged(false);
}

void QQRPAAdapter::startListening()
{
    QElapsedTimer timer;
    timer.start();
    QString serviceError;
    if (!Ipc::IpcService::instance().connectToConfiguredService(&serviceError)) {
        qWarning() << "[QQRPAAdapter] Python service unavailable:"
                   << serviceError << "elapsedMs=" << timer.elapsed();
        return;
    }

    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("connect");
    request.platform = kQQSidecarPlatform;
    request.accountId = accountId();
    request.parameters.insert(QStringLiteral("mode"), QStringLiteral("listen"));
    request.parameters.insert(QStringLiteral("emit_initial_snapshot"), false);
    request.parameters.insert(QStringLiteral("quick_connect"), true);
    request.parameters.insert(QStringLiteral("message_limit"), 40);
    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 10000);
    if (response.status != Ipc::ResponseStatus::Success) {
        if (response.status == Ipc::ResponseStatus::Timeout
            && response.errorMessage == QLatin1String("request_timeout")) {
            qWarning() << "[QQRPAAdapter] connect command timed out; treat as pending and wait for service status"
                       << "elapsedMs=" << timer.elapsed();
            if (!m_connected)
                connectPlatform();
            return;
        }
        qWarning() << "[QQRPAAdapter] connect command failed:"
                   << response.errorMessage << "elapsedMs=" << timer.elapsed();
        return;
    }

    if (!m_connected)
        connectPlatform();
    qInfo() << "[QQRPAAdapter] startListening with WebSocket command/event bridge"
            << "elapsedMs=" << timer.elapsed();
}

void QQRPAAdapter::stopListening()
{
    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("disconnect");
    request.platform = kQQSidecarPlatform;
    request.accountId = accountId();
    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 3000);
    if (response.status != Ipc::ResponseStatus::Success)
        qWarning() << "[QQRPAAdapter] disconnect command failed:" << response.errorMessage;
    qInfo() << "[QQRPAAdapter] stopListening";
}

void QQRPAAdapter::sendMessage(const QString& conversationId, const QString& text, const QString& clientMessageId)
{
    OutgoingMessagePart part;
    part.type = OutgoingPartType::Text;
    part.text = text;
    sendMessagePart(conversationId, part, clientMessageId);
}

void QQRPAAdapter::sendMessagePart(const QString& conversationId,
                                   const OutgoingMessagePart& part,
                                   const QString& clientMessageId)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    if (part.type != OutgoingPartType::Text) {
        emit sendFailed(conversationId, QStringLiteral("qq_send_only_supports_text"), clientMessageId);
        return;
    }
    if (m_commandInFlight) {
        qInfo() << "[QQRPAAdapter] sendMessage delayed: command already in flight";
        QTimer::singleShot(200, this, [this, conversationId, part, clientMessageId]() {
            sendMessagePart(conversationId, part, clientMessageId);
        });
        return;
    }

    m_commandInFlight = true;
    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("send_message");
    request.platform = kQQSidecarPlatform;
    request.accountId = accountId();
    request.taskId = clientMessageId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : clientMessageId;
    request.parameters.insert(QStringLiteral("client_message_id"), request.taskId);
    request.parameters.insert(QStringLiteral("conversation_key"), conversationId);
    request.parameters.insert(QStringLiteral("display_name"), displayNameFromConversationKey(conversationId));
    request.parameters.insert(QStringLiteral("content_type"), outgoingContentType(part));
    request.parameters.insert(QStringLiteral("text"), part.text);
    request.parameters.insert(QStringLiteral("confirm_token"), QStringLiteral("manual_confirmed_by_agent"));
    request.parameters.insert(QStringLiteral("verify"), false);

    QElapsedTimer commandTimer;
    commandTimer.start();
    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 12000);
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
        qInfo() << "[QQRPAAdapter] sendMessage timing"
                << "conversation=" << conversationId
                << "clientMessageId=" << request.taskId
                << "commandElapsedMs=" << commandElapsedMs
                << "totalElapsedMs=" << totalTimer.elapsed()
                << "accepted=" << accepted
                << "method=" << result.value(QStringLiteral("method")).toString()
                << "verified=" << result.value(QStringLiteral("verified")).toBool()
                << "sent=" << sent;
        if (accepted && !sent) {
            scheduleConfirmTimeout();
            return;
        }
        if (!m_eventSocketConnected) {
            QTimer::singleShot(300, this, [this, conversationId, text = part.text, clientMessageId = request.taskId]() {
                emit messageSent(conversationId, text, clientMessageId);
            });
        }
        return;
    }

    if (response.status == Ipc::ResponseStatus::Timeout
        && response.errorMessage == QLatin1String("request_timeout")) {
        qWarning() << "[QQRPAAdapter] sendMessage command timed out; waiting for result event"
                   << "conversation=" << conversationId
                   << "clientMessageId=" << request.taskId
                   << "commandElapsedMs=" << commandElapsedMs
                   << "totalElapsedMs=" << totalTimer.elapsed();
        scheduleConfirmTimeout();
        return;
    }

    qWarning() << "[QQRPAAdapter] sendMessage failed:"
               << response.errorMessage
               << "commandElapsedMs=" << commandElapsedMs
               << "totalElapsedMs=" << totalTimer.elapsed();
    emit sendFailed(conversationId, response.errorMessage.isEmpty()
                                    ? QStringLiteral("qq_sidecar_command_failed")
                                    : response.errorMessage,
                    request.taskId);
}

void QQRPAAdapter::handleRpaEvent(const QJsonObject& event)
{
    QElapsedTimer timer;
    timer.start();
    if (event.value(QStringLiteral("platform")).toString().trimmed().toLower() != kQQSidecarPlatform)
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
    qInfo() << "[QQRPAAdapter] realtime event received"
            << "type=" << type
            << "eventId=" << eventId
            << "seq=" << seq
            << "cursor=" << m_eventCursor
            << "clientMessageId=" << clientMessageId;

    if (type == QLatin1String("conversation_observed")) {
        emitConversationObserved(event);
        qInfo() << "[QQRPAAdapter] event timing"
                << "type=" << type
                << "eventId=" << eventId
                << "elapsedMs=" << timer.elapsed();
        return;
    }

    if (type == QLatin1String("message_observed")) {
        const PlatformMessage msg = platformMessageFromEvent(event);
        const bool emitted = !msg.platformConversationId.isEmpty() && !msg.content.isEmpty();
        if (emitted)
            emit incomingMessage(msg);
        qInfo() << "[QQRPAAdapter] event timing"
                << "type=" << type
                << "eventId=" << eventId
                << "elapsedMs=" << timer.elapsed()
                << "emitted=" << emitted;
        return;
    }

    if (type == QLatin1String("message_sent")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        if (!conversation.isEmpty())
            emit messageSent(conversation, payloadObject.value(QStringLiteral("content")).toString(), clientMessageId);
        return;
    }

    if (type == QLatin1String("send_failed")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        QString reason = payloadObject.value(QStringLiteral("error_message")).toString();
        if (reason.isEmpty())
            reason = payloadObject.value(QStringLiteral("error")).toString();
        if (reason.isEmpty())
            reason = payloadObject.value(QStringLiteral("status")).toString();
        if (!conversation.isEmpty())
            emit sendFailed(conversation,
                            reason.isEmpty() ? QStringLiteral("qq_send_failed") : reason,
                            clientMessageId);
        return;
    }

    if (type == QLatin1String("account_health_changed")) {
        const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
        qInfo() << "[QQRPAAdapter] account health changed"
                << "healthy=" << payload.value(QStringLiteral("healthy")).toBool(false)
                << "status=" << payload.value(QStringLiteral("status")).toString()
                << "message=" << payload.value(QStringLiteral("message")).toString();
        return;
    }

    qInfo() << "[QQRPAAdapter] unhandled realtime event type=" << type;
}

void QQRPAAdapter::emitConversationObserved(const QJsonObject& event)
{
    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    const QString conversationKey = event.value(QStringLiteral("conversation_key")).toString();
    const QString normalizedConversation = normalizeConversationKey(conversationKey);
    if (normalizedConversation.isEmpty())
        return;
    const QString displayName = firstNonEmpty({
        payload.value(QStringLiteral("display_name")).toString(),
        payload.value(QStringLiteral("sender_name")).toString(),
        displayNameFromConversationKey(normalizedConversation),
    });

    ConversationInfo info;
    info.platform = platformName();
    info.platformConversationId = normalizedConversation;
    info.customerName = displayName;
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

PlatformMessage QQRPAAdapter::platformMessageFromEvent(const QJsonObject& event) const
{
    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    const QString conversationKey = event.value(QStringLiteral("conversation_key")).toString();
    const QString normalizedConversation = normalizeConversationKey(conversationKey);
    const QString conversationDisplayName = firstNonEmpty({
        payload.value(QStringLiteral("display_name")).toString(),
        payload.value(QStringLiteral("sender_name")).toString(),
        displayNameFromConversationKey(normalizedConversation),
    });
    const QString senderName = firstNonEmpty({
        payload.value(QStringLiteral("sender_name")).toString(),
        conversationDisplayName,
    });
    const QString content = payload.value(QStringLiteral("content")).toString();
    const QString rawDirection = payload.value(QStringLiteral("direction")).toString();
    const QString rawSenderRole = payload.value(QStringLiteral("sender_role")).toString();
    const QString direction = normalizedDirection(rawDirection, rawSenderRole, payload);
    const QJsonObject metadata = payload.value(QStringLiteral("metadata")).toObject();

    PlatformMessage msg;
    msg.platform = platformName();
    msg.platformConversationId = normalizedConversation;
    msg.customerName = conversationDisplayName;
    msg.content = content;
    msg.direction = direction;
    msg.sender = msg.direction == QLatin1String("in")
        ? QStringLiteral("customer")
        : (msg.direction == QLatin1String("system") ? QStringLiteral("system") : QStringLiteral("agent"));
    msg.createdAt = QDateTime::fromString(event.value(QStringLiteral("occurred_at")).toString(), Qt::ISODateWithMs);
    if (!msg.createdAt.isValid())
        msg.createdAt = QDateTime::currentDateTime();
    msg.platformMsgId = payload.value(QStringLiteral("platform_msg_id")).toString();
    msg.senderName = senderName;
    msg.originalTimestamp = firstNonEmpty({
        payload.value(QStringLiteral("original_timestamp")).toString(),
        metadata.value(QStringLiteral("time_text")).toString(),
    });
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

QString QQRPAAdapter::normalizeConversationKey(const QString& conversationKey) const
{
    return conversationKey.trimmed();
}
