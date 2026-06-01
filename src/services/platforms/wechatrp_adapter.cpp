#include "wechatrp_adapter.h"
#include "../../data/database.h"
#include "../../ipc/ipcservice.h"
#include <QDebug>
#include <QMetaObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QDateTime>
#include <QStringList>
#include <QUuid>

namespace {
constexpr int kRpaEventBatchLimit = 50;
constexpr int kCompensationPollTicks = 60;
constexpr int kCompensationPollMs = 15000;

QString normalizedDirection(const QString& direction, const QString& senderRole, const QJsonObject& payload)
{
    auto norm = [](const QString& value) {
        return value.trimmed().toLower();
    };
    const QString d = norm(direction);
    const QString s = norm(senderRole);
    const QString metaDirection = norm(payload.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("direction")).toString());
    const QString metaSenderRole = norm(payload.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("sender_role")).toString());
    const QString metaSide = norm(payload.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("side")).toString());

    const QStringList outbound = {
        QStringLiteral("out"),
        QStringLiteral("outbound"),
        QStringLiteral("outgoing"),
        QStringLiteral("sent"),
        QStringLiteral("send"),
        QStringLiteral("agent"),
        QStringLiteral("assistant"),
        QStringLiteral("me"),
        QStringLiteral("self"),
    };
    const QStringList inbound = {
        QStringLiteral("in"),
        QStringLiteral("inbound"),
        QStringLiteral("incoming"),
        QStringLiteral("received"),
        QStringLiteral("receive"),
        QStringLiteral("customer"),
        QStringLiteral("user"),
    };

    const auto isOutbound = [&](const QString& v) { return outbound.contains(v); };
    const auto isInbound = [&](const QString& v) { return inbound.contains(v); };

    if (isOutbound(d) || isOutbound(s) || isOutbound(metaDirection) || isOutbound(metaSenderRole) || isOutbound(metaSide))
        return QStringLiteral("out");
    if (isInbound(d) || isInbound(s) || isInbound(metaDirection) || isInbound(metaSenderRole) || isInbound(metaSide))
        return QStringLiteral("in");
    if (d == QLatin1String("system") || s == QLatin1String("system") || metaDirection == QLatin1String("system"))
        return QStringLiteral("system");
    return QStringLiteral("in");
}
} // namespace

WechatRPAAdapter::WechatRPAAdapter(QObject* parent)
    : IPlatformAdapter(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kCompensationPollMs);
    connect(m_pollTimer, &QTimer::timeout, this, &WechatRPAAdapter::pollOnce);
    connect(&Ipc::IpcService::instance(), &Ipc::IpcService::rpaEventReceived,
            this, &WechatRPAAdapter::handleRpaEvent);
    connect(&Ipc::IpcService::instance(), &Ipc::IpcService::rpaEventBridgeStateChanged,
            this, [this](bool connected) {
        m_eventSocketConnected = connected;
        qInfo() << "[WechatRPAAdapter] realtime event bridge"
                << (connected ? "connected" : "disconnected")
                << "cursor=" << m_eventCursor;
    });
}

void WechatRPAAdapter::applyPollCadence(bool hadWork)
{
    if (!m_pollTimer)
        return;
    Q_UNUSED(hadWork)
    if (m_pollTimer->interval() != kCompensationPollMs)
        m_pollTimer->setInterval(kCompensationPollMs);
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
    if (!m_connected)
        connectPlatform();
    QString serviceError;
    if (!Ipc::IpcService::instance().connectToConfiguredService(&serviceError)) {
        qWarning() << "[WechatRPAAdapter] Python service unavailable:" << serviceError;
    }
    Ipc::RpaCommandRequest request;
    request.commandType = QStringLiteral("connect");
    request.platformType = platformName();
    request.accountId = accountId();
    request.parameters.insert(QStringLiteral("mode"), QStringLiteral("listen"));
    request.parameters.insert(QStringLiteral("emit_initial_snapshot"), false);
    const auto response = Ipc::IpcService::instance().sendRpaCommand(request, 3000);
    if (response.status != Ipc::ResponseStatus::Success) {
        qWarning() << "[WechatRPAAdapter] connect command failed:" << response.errorMessage;
    }
    if (!m_pollTimer->isActive())
        m_pollTimer->start();
    qInfo() << "[WechatRPAAdapter] startListening with WebSocket event bridge + low-frequency compensation";
}

void WechatRPAAdapter::stopListening()
{
    if (m_pollTimer && m_pollTimer->isActive())
        m_pollTimer->stop();
    qInfo() << "[WechatRPAAdapter] stopListening";
}

void WechatRPAAdapter::sendMessage(const QString& conversationId, const QString& text, const QString& clientMessageId)
{
    if (m_commandInFlight) {
        qInfo() << "[WechatRPAAdapter] sendMessage delayed: command already in flight";
        QTimer::singleShot(200, this, [this, conversationId, text, clientMessageId]() {
            sendMessage(conversationId, text, clientMessageId);
        });
        return;
    }

    m_commandInFlight = true;
    Ipc::RpaCommandRequest request;
    request.commandType = QStringLiteral("send_message");
    request.platformType = platformName();
    request.accountId = accountId();
    request.taskId = clientMessageId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : clientMessageId;
    request.parameters.insert(QStringLiteral("client_message_id"), request.taskId);
    request.parameters.insert(QStringLiteral("conversation_key"), conversationId);
    request.parameters.insert(QStringLiteral("display_name"), conversationId);
    request.parameters.insert(QStringLiteral("text"), text);
    request.parameters.insert(QStringLiteral("require_target_verification"), true);
    request.parameters.insert(QStringLiteral("prefer_background"), true);
    request.parameters.insert(QStringLiteral("allow_foreground_fallback"), true);
    request.parameters.insert(QStringLiteral("strict_background"), false);
    request.parameters.insert(QStringLiteral("confirm_token"), QStringLiteral("manual_confirmed_by_agent"));

    auto response = Ipc::IpcService::instance().sendRpaCommandViaWebSocket(request, 4000);
    if (response.status != Ipc::ResponseStatus::Success
        && response.errorMessage == QLatin1String("command_websocket_not_connected")) {
        qWarning() << "[WechatRPAAdapter] sendMessage WebSocket unavailable, fallback to HTTP";
        response = Ipc::IpcService::instance().sendRpaCommand(request, 4000);
    }
    m_commandInFlight = false;
    if (response.status == Ipc::ResponseStatus::Success) {
        const QJsonObject result = response.result;
        qInfo() << "[WechatRPAAdapter] sendMessage result"
                << "conversation=" << conversationId
                << "clientMessageId=" << request.taskId
                << "draftMethod=" << result.value(QStringLiteral("draft_method")).toString()
                << "strictBackgroundWriteSuccess=" << result.value(QStringLiteral("strict_background_write_success")).toBool()
                << "strictBackgroundWriteMethod=" << result.value(QStringLiteral("strict_background_write_method")).toString()
                << "strictBackgroundSupported=" << result.value(QStringLiteral("strict_background_supported")).toBool()
                << "strictBackgroundReason=" << result.value(QStringLiteral("strict_background_reason")).toString()
                << "sendMethod=" << result.value(QStringLiteral("send_method")).toString()
                << "foreground=" << result.value(QStringLiteral("foreground")).toBool()
                << "sent=" << result.value(QStringLiteral("sent")).toBool();
        QTimer::singleShot(800, this, [this, conversationId, text, clientMessageId = request.taskId]() {
            emit messageSent(conversationId, text, clientMessageId);
        });
        return;
    }

    qWarning() << "[WechatRPAAdapter] sendMessage failed:" << response.errorMessage;
    emit sendFailed(conversationId, response.errorMessage.isEmpty()
                                    ? QStringLiteral("wechat_rpa_command_failed")
                                    : response.errorMessage,
                    request.taskId);
}

void WechatRPAAdapter::pollOnce()
{
    if (!m_connected)
        return;
    if (m_commandInFlight)
        return;

    m_commandInFlight = true;
    bool hadWork = false;
    if (!m_eventSocketConnected)
        hadWork = pollRpaEventsOnce() || hadWork;

    if (++m_idleFetchTicks >= kCompensationPollTicks) {
        m_idleFetchTicks = 0;
        hadWork = pollRpaEventsOnce() || hadWork;
        hadWork = pollInboxOnce() || hadWork;
        Ipc::RpaCommandRequest request;
        request.commandType = QStringLiteral("scan_unread_and_fetch");
        request.platformType = platformName();
        request.accountId = accountId();
        request.parameters.insert(QStringLiteral("session_limit"), 3);
        request.parameters.insert(QStringLiteral("message_limit"), 20);
        request.parameters.insert(QStringLiteral("allow_foreground"), false);
        request.parameters.insert(QStringLiteral("settle_ms"), 300);
        const auto response = Ipc::IpcService::instance().sendRpaCommand(request, 1200);
        const bool scanHadWork = response.status == Ipc::ResponseStatus::Success
            && response.result.value(QStringLiteral("message_count")).toInt() > 0;
        hadWork = scanHadWork || hadWork;
        qInfo() << "[WechatRPAAdapter] low-frequency compensation scan"
                << "eventBridgeConnected=" << m_eventSocketConnected
                << "cursor=" << m_eventCursor
                << "status=" << Ipc::toString(response.status)
                << "messages=" << response.result.value(QStringLiteral("message_count")).toInt()
                << "error=" << response.errorMessage;
    }
    m_commandInFlight = false;
    applyPollCadence(hadWork);
}

bool WechatRPAAdapter::pollRpaEventsOnce()
{
    const auto batch = Ipc::IpcService::instance().fetchRpaEvents(
        platformName(),
        m_eventCursor,
        kRpaEventBatchLimit,
        2000);

    if (batch.status != Ipc::ResponseStatus::Success) {
        qWarning() << "[WechatRPAAdapter] RPA event fetch failed:" << batch.errorMessage;
        return false;
    }

    bool hadWork = false;
    for (const QJsonObject& event : batch.events) {
        handleRpaEvent(event);
        hadWork = true;
    }

    if (!batch.cursor.isEmpty())
        m_eventCursor = batch.cursor;
    if (!batch.latestCursor.isEmpty())
        m_eventCursor = batch.latestCursor;
    return hadWork;
}

bool WechatRPAAdapter::pollInboxOnce()
{
    QSqlDatabase db = Database::getInstance().connection();
    if (!db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(
        "SELECT id, platform_conversation_id, customer_name, content, created_at, platform_msg_id, "
        "       direction, sender_role, sender_name, original_timestamp, content_image_path "
        "FROM rpa_inbox_messages "
        "WHERE platform = :platform "
        "  AND consume_status = 0 "
        "  AND id > :lastId "
        "ORDER BY id ASC LIMIT 50");
    q.bindValue(":platform", platformName());
    q.bindValue(":lastId", m_lastInboxId);

    if (!q.exec()) {
        qWarning() << "[WechatRPAAdapter] inbox query failed:" << q.lastError().text();
        return false;
    }

    QList<qint64> consumedIds;
    bool hadWork = false;
    while (q.next()) {
        const qint64 inboxId = q.value(0).toLongLong();
        const QString platformConvId = q.value(1).toString();
        const QString customerName = q.value(2).toString();
        const QString content = q.value(3).toString();
        const QDateTime createdAt = q.value(4).toDateTime();
        const QString platformMsgId = q.value(5).toString();
        const QString direction = q.value(6).toString();
        const QString senderRole = q.value(7).toString();
        const QString senderName = q.value(8).toString();
        const QString originalTimestamp = q.value(9).toString();
        const QString contentImagePath = q.value(10).toString();

        PlatformMessage msg;
        msg.platform = platformName();
        msg.platformConversationId = platformConvId;
        msg.customerName = customerName;
        msg.content = content;
        msg.direction = normalizedDirection(direction, senderRole, QJsonObject());
        msg.sender = msg.direction == QLatin1String("in")
            ? QStringLiteral("customer")
            : (msg.direction == QLatin1String("system") ? QStringLiteral("system") : QStringLiteral("agent"));
        msg.createdAt = createdAt.isValid() ? createdAt : QDateTime::currentDateTime();
        msg.platformMsgId = platformMsgId;
        msg.senderName = senderName;
        msg.originalTimestamp = originalTimestamp;
        msg.contentImagePath = contentImagePath;
        msg.sourceType = QStringLiteral("ui_observed");
        msg.confidence = contentImagePath.isEmpty() ? 70 : 55;
        msg.verificationStatus = QStringLiteral("unverified");
        msg.contentType = contentImagePath.isEmpty() ? QStringLiteral("text") : QStringLiteral("image");
        msg.metadata.insert(QStringLiteral("platform_msg_id"), platformMsgId);
        msg.metadata.insert(QStringLiteral("content"), content);
        msg.metadata.insert(QStringLiteral("direction"), direction);
        msg.metadata.insert(QStringLiteral("sender_role"), senderRole);
        msg.metadata.insert(QStringLiteral("source_type"), msg.sourceType);
        msg.metadata.insert(QStringLiteral("confidence"), msg.confidence);
        msg.metadata.insert(QStringLiteral("verification_status"), msg.verificationStatus);
        msg.metadata.insert(QStringLiteral("content_type"), msg.contentType);
        msg.metadata.insert(QStringLiteral("evidence_ref"), contentImagePath);
        msg.metadata.insert(QStringLiteral("_event_account_id"), accountId());
        msg.metadata.insert(QStringLiteral("_event_conversation_key"), platformConvId);
        msg.metadata.insert(QStringLiteral("metadata"), QJsonObject{
            { QStringLiteral("observation_method"), QStringLiteral("rpa_inbox_fallback") },
            { QStringLiteral("direction"), direction },
            { QStringLiteral("sender_role"), senderRole },
            { QStringLiteral("normalized_direction"), msg.direction }
        });

        emit incomingMessage(msg);
        hadWork = true;
        consumedIds.append(inboxId);
        if (inboxId > m_lastInboxId)
            m_lastInboxId = inboxId;
    }

    if (consumedIds.isEmpty())
        return hadWork;

    QSqlQuery u(db);
    u.prepare("UPDATE rpa_inbox_messages SET consume_status = 1 WHERE id = :id");
    db.transaction();
    bool ok = true;
    for (qint64 id : consumedIds) {
        u.bindValue(":id", id);
        if (!u.exec()) {
            ok = false;
            qWarning() << "[WechatRPAAdapter] inbox update consume_status failed:" << u.lastError().text();
        }
    }
    ok ? db.commit() : db.rollback();
    return hadWork;
}

void WechatRPAAdapter::handleRpaEvent(const QJsonObject& event)
{
    if (event.value(QStringLiteral("platform")).toString() != platformName())
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
    const QString clientMessageId = event.value(QStringLiteral("client_message_id")).toString(
        event.value(QStringLiteral("task_id")).toString());
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

    if (type == QLatin1String("message_sent")) {
        const QString conversation = normalizeConversationKey(event.value(QStringLiteral("conversation_key")).toString());
        const QString text = event.value(QStringLiteral("payload")).toObject().value(QStringLiteral("content")).toString();
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
        const QString reason = event.value(QStringLiteral("payload")).toObject().value(QStringLiteral("error_message")).toString();
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

    ConversationInfo info;
    info.platform = platformName();
    info.platformConversationId = normalizedConversation;
    info.customerName = payload.value(QStringLiteral("display_name")).toString(normalizedConversation);
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
    const QString displayName = payload.value(QStringLiteral("sender_name")).toString(normalizedConversation);
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
    if (conversationKey.startsWith(QStringLiteral("wechat:"))) {
        const int lastSep = conversationKey.lastIndexOf(QLatin1Char(':'));
        if (lastSep >= 0 && lastSep + 1 < conversationKey.size())
            return conversationKey.mid(lastSep + 1);
    }
    return conversationKey;
}
