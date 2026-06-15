#include "wechatrp_adapter.h"
#include "../../ipc/ipcservice.h"
#include <QDebug>
#include <QMetaObject>
#include <QDateTime>
#include <QStringList>
#include <QTimer>
#include <QUuid>

namespace {
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

QString displayNameFromConversationKey(const QString& conversationKey)
{
    const QString trimmed = conversationKey.trimmed();
    const int lastSep = trimmed.lastIndexOf(QLatin1Char(':'));
    if (lastSep >= 0 && lastSep + 1 < trimmed.size())
        return trimmed.mid(lastSep + 1).trimmed();
    return trimmed;
}

QString outgoingContentType(const OutgoingMessagePart& part)
{
    switch (part.type) {
    case OutgoingPartType::Image: return QStringLiteral("image");
    case OutgoingPartType::Video: return QStringLiteral("video");
    case OutgoingPartType::File: return QStringLiteral("file");
    case OutgoingPartType::Text:
    default: return QStringLiteral("text");
    }
}

QString outgoingContent(const OutgoingMessagePart& part)
{
    if (part.type == OutgoingPartType::Text)
        return part.text;
    if (part.type == OutgoingPartType::Image)
        return QStringLiteral("[图片]");
    if (part.type == OutgoingPartType::Video)
        return QStringLiteral("[视频] %1").arg(part.fileName);
    return QStringLiteral("[文件] %1").arg(part.fileName);
}
} // namespace

WechatRPAAdapter::WechatRPAAdapter(QObject* parent)
    : IPlatformAdapter(parent)
{
    connect(&Ipc::IpcService::instance(), &Ipc::IpcService::platformEventReceived,
            this, &WechatRPAAdapter::handleRpaEvent);
    connect(&Ipc::IpcService::instance(), &Ipc::IpcService::platformEventBridgeStateChanged,
            this, [this](bool connected) {
        m_eventSocketConnected = connected;
        qInfo() << "[WechatRPAAdapter] realtime event bridge"
                << (connected ? "connected" : "disconnected")
                << "cursor=" << m_eventCursor;
    });
}

void WechatRPAAdapter::connectPlatform()
{
    m_connected = true;
    qInfo() << "[WechatRPAAdapter] WeChat adapter connected";
    emit connectionStateChanged(true);
}

void WechatRPAAdapter::disconnectPlatform()
{
    m_connected = false;
    stopListening();
    qInfo() << "[WechatRPAAdapter] WeChat adapter disconnected";
    emit connectionStateChanged(false);
}

void WechatRPAAdapter::startListening()
{
    QString serviceError;
    if (!Ipc::IpcService::instance().connectToConfiguredService(&serviceError)) {
        qWarning() << "[WechatRPAAdapter] Python service unavailable:" << serviceError;
        return;
    }
    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("connect");
    request.platform = platformName();
    request.accountId = accountId();
    request.parameters.insert(QStringLiteral("mode"), QStringLiteral("listen"));
    request.parameters.insert(QStringLiteral("emit_initial_snapshot"), false);
    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 3000);
    if (response.status != Ipc::ResponseStatus::Success) {
        qWarning() << "[WechatRPAAdapter] connect command failed:" << response.errorMessage;
        return;
    }
    if (!m_connected)
        connectPlatform();
    qInfo() << "[WechatRPAAdapter] startListening with WebSocket command/event bridge";
}

void WechatRPAAdapter::stopListening()
{
    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("disconnect");
    request.platform = platformName();
    request.accountId = accountId();
    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 3000);
    if (response.status != Ipc::ResponseStatus::Success)
        qWarning() << "[WechatRPAAdapter] disconnect command failed:" << response.errorMessage;
    qInfo() << "[WechatRPAAdapter] stopListening";
}

void WechatRPAAdapter::sendMessage(const QString& conversationId, const QString& text, const QString& clientMessageId)
{
    OutgoingMessagePart part;
    part.type = OutgoingPartType::Text;
    part.text = text;
    sendMessagePart(conversationId, part, clientMessageId);
}

void WechatRPAAdapter::sendMessagePart(const QString& conversationId,
                                       const OutgoingMessagePart& part,
                                       const QString& clientMessageId)
{
    const QString content = outgoingContent(part);
    if (m_commandInFlight) {
        qInfo() << "[WechatRPAAdapter] sendMessage delayed: command already in flight";
        QTimer::singleShot(200, this, [this, conversationId, part, clientMessageId]() {
            sendMessagePart(conversationId, part, clientMessageId);
        });
        return;
    }

    m_commandInFlight = true;
    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("send_message");
    request.platform = platformName();
    request.accountId = accountId();
    request.taskId = clientMessageId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : clientMessageId;
    const QString displayName = displayNameFromConversationKey(conversationId);
    request.parameters.insert(QStringLiteral("client_message_id"), request.taskId);
    request.parameters.insert(QStringLiteral("conversation_key"), conversationId);
    request.parameters.insert(QStringLiteral("display_name"), displayName);
    request.parameters.insert(QStringLiteral("content_type"), outgoingContentType(part));
    request.parameters.insert(QStringLiteral("text"), part.text);
    request.parameters.insert(QStringLiteral("file_path"), part.localPath);
    request.parameters.insert(QStringLiteral("file_name"), part.fileName);
    request.parameters.insert(QStringLiteral("mime_type"), part.mimeType);
    request.parameters.insert(QStringLiteral("size_bytes"), double(part.sizeBytes));
    request.parameters.insert(QStringLiteral("require_target_verification"), true);
    request.parameters.insert(QStringLiteral("prefer_background"), true);
    request.parameters.insert(QStringLiteral("allow_foreground_fallback"), true);
    request.parameters.insert(QStringLiteral("strict_background"), false);
    request.parameters.insert(QStringLiteral("confirm_token"), QStringLiteral("manual_confirmed_by_agent"));

    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 4000);
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
        qInfo() << "[WechatRPAAdapter] sendMessage result"
                << "conversation=" << conversationId
                << "clientMessageId=" << request.taskId
                << "accepted=" << accepted
                << "draftMethod=" << result.value(QStringLiteral("draft_method")).toString()
                << "strictBackgroundWriteSuccess=" << result.value(QStringLiteral("strict_background_write_success")).toBool()
                << "strictBackgroundWriteMethod=" << result.value(QStringLiteral("strict_background_write_method")).toString()
                << "strictBackgroundSupported=" << result.value(QStringLiteral("strict_background_supported")).toBool()
                << "strictBackgroundReason=" << result.value(QStringLiteral("strict_background_reason")).toString()
                << "sendMethod=" << result.value(QStringLiteral("send_method")).toString()
                << "foreground=" << result.value(QStringLiteral("foreground")).toBool()
                << "sent=" << sent;
        if (accepted && !sent) {
            scheduleConfirmTimeout();
            return;
        }
        QTimer::singleShot(800, this, [this, conversationId, content, clientMessageId = request.taskId]() {
            emit messageSent(conversationId, content, clientMessageId);
        });
        return;
    }

    if (response.status == Ipc::ResponseStatus::Timeout
        && response.errorMessage == QLatin1String("request_timeout")) {
        qWarning() << "[WechatRPAAdapter] sendMessage command timed out; waiting for result event"
                   << "conversation=" << conversationId
                   << "clientMessageId=" << request.taskId
                   << "contentType=" << outgoingContentType(part);
        scheduleConfirmTimeout();
        return;
    }

    qWarning() << "[WechatRPAAdapter] sendMessage failed:" << response.errorMessage;
    emit sendFailed(conversationId, response.errorMessage.isEmpty()
                                    ? QStringLiteral("wechat_rpa_command_failed")
                                    : response.errorMessage,
                    request.taskId);
}

void WechatRPAAdapter::handleRpaEvent(const QJsonObject& event)
{
    if (event.value(QStringLiteral("platform")).toString().trimmed().toLower() != platformName())
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
    qInfo() << "[WechatRPAAdapter] realtime event received"
            << "type=" << type
            << "eventId=" << eventId
            << "seq=" << seq
            << "cursor=" << m_eventCursor
            << "clientMessageId=" << clientMessageId;

    if (type == QLatin1String("conversation_observed")) {
        emitConversationObserved(event);
        return;
    }

    if (type == QLatin1String("message_observed")) {
        const PlatformMessage msg = platformMessageFromEvent(event);
        if (!msg.platformConversationId.isEmpty() && !msg.content.isEmpty())
            emit incomingMessage(msg);
        return;
    }

    if (type == QLatin1String("conversation_messages_cleared")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        if (!conversation.isEmpty())
            emit conversationMessagesCleared(conversation);
        qInfo() << "[WechatRPAAdapter] conversation cleared event"
                << "conversation=" << conversation
                << "eventId=" << eventId;
        return;
    }

    if (type == QLatin1String("conversation_deleted")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        if (!conversation.isEmpty())
            emit conversationDeleted(conversation);
        qInfo() << "[WechatRPAAdapter] conversation deleted event"
                << "conversation=" << conversation
                << "eventId=" << eventId;
        return;
    }

    if (type == QLatin1String("message_sent")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        const QString text = payloadObject.value(QStringLiteral("content")).toString();
        if (!conversation.isEmpty()) {
            qInfo() << "[WechatRPAAdapter] message_sent event"
                    << "conversation=" << conversation
                    << "clientMessageId=" << clientMessageId;
            emit messageSent(conversation, text, clientMessageId);
        }
        return;
    }

    if (type == QLatin1String("send_failed")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        const QString reason = payloadObject.value(QStringLiteral("error_message")).toString();
        if (!conversation.isEmpty()) {
            qInfo() << "[WechatRPAAdapter] send_failed event"
                    << "conversation=" << conversation
                    << "clientMessageId=" << clientMessageId
                    << "reason=" << reason;
            emit sendFailed(conversation, reason.isEmpty() ? QStringLiteral("wechat_rpa_send_failed") : reason, clientMessageId);
        }
        return;
    }

    if (type == QLatin1String("account_health_changed")) {
        const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
        const bool healthy = payload.value(QStringLiteral("healthy")).toBool(false);
        const QString status = payload.value(QStringLiteral("status")).toString();
        qInfo() << "[WechatRPAAdapter] account health changed"
                << "healthy=" << healthy
                << "status=" << status
                << "message=" << payload.value(QStringLiteral("message")).toString();
        return;
    }

    qInfo() << "[WechatRPAAdapter] unhandled realtime event type=" << type;
}

void WechatRPAAdapter::emitConversationObserved(const QJsonObject& event)
{
    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    const QString conversationKey = event.value(QStringLiteral("conversation_key")).toString();
    const QString normalizedConversation = normalizeConversationKey(conversationKey);
    if (normalizedConversation.isEmpty())
        return;
    const QString displayName = payload.value(QStringLiteral("display_name")).toString(
        displayNameFromConversationKey(conversationKey));

    ConversationInfo info;
    info.platform = platformName();
    info.platformConversationId = normalizedConversation;
    info.customerName = displayName.isEmpty() ? normalizedConversation : displayName;
    info.status = QStringLiteral("active");
    info.accountId = event.value(QStringLiteral("account_id")).toString();
    info.sourceType = payload.value(QStringLiteral("source_type")).toString(QStringLiteral("ui_observed"));
    info.confidence = payload.value(QStringLiteral("confidence")).toInt(75);
    info.updatedAt = QDateTime::fromString(event.value(QStringLiteral("occurred_at")).toString(), Qt::ISODateWithMs);
    if (!info.updatedAt.isValid())
        info.updatedAt = QDateTime::currentDateTime();
    info.createdAt = info.updatedAt;

    emit conversationObserved(info);
}

PlatformMessage WechatRPAAdapter::platformMessageFromEvent(const QJsonObject& event) const
{
    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    const QString conversationKey = event.value(QStringLiteral("conversation_key")).toString();
    const QString normalizedConversation = normalizeConversationKey(conversationKey);
    QString displayName = payload.value(QStringLiteral("display_name")).toString().trimmed();
    if (displayName.isEmpty())
        displayName = payload.value(QStringLiteral("sender_name")).toString().trimmed();
    if (displayName.isEmpty())
        displayName = displayNameFromConversationKey(conversationKey);
    if (displayName.isEmpty())
        displayName = normalizedConversation;
    const QString content = payload.value(QStringLiteral("content")).toString();
    const QString rawDirection = payload.value(QStringLiteral("direction")).toString();
    const QString rawSenderRole = payload.value(QStringLiteral("sender_role")).toString();
    const QString direction = normalizedDirection(rawDirection, rawSenderRole, payload);

    PlatformMessage msg;
    msg.platform = platformName();
    msg.platformConversationId = normalizedConversation;
    msg.customerName = displayName;
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
    msg.originalTimestamp = event.value(QStringLiteral("occurred_at")).toString();
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
    qInfo() << "[WechatRPAAdapter] message direction normalized"
            << "rawDirection=" << rawDirection
            << "rawSenderRole=" << rawSenderRole
            << "normalizedDirection=" << direction
            << "conversation=" << normalizedConversation
            << "content=" << content.left(30);
    return msg;
}

QString WechatRPAAdapter::normalizeConversationKey(const QString& conversationKey) const
{
    return conversationKey.trimmed();
}
