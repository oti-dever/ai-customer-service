#include "messagerouter.h"
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
#include "../data/wechatmessagedao.h"
#include "../data/database.h"
#include "../services/platforms/iplatformadapter.h"
#include "types.h"
#include <QSqlError>
#include <QSqlQuery>

MessageRouter::MessageRouter(QObject* parent)
    : QObject(parent)
{
}

void MessageRouter::dispatchEvent(const Models::ConversationEvent& event)
{
    qDebug() << "[MessageRouter] dispatch event:" << event.type
             << "conversationId=" << event.conversationId;
    emit conversationEventDispatched(event);
}

Models::Message MessageRouter::createUnifiedMessage(const PlatformMessage& msg, int conversationId) const
{
    Models::Message unified;
    unified.conversationId = conversationId;
    unified.platformMessageId = msg.platformMsgId;
    unified.direction = Models::messageDirectionFromLegacy(msg.direction, msg.sender);
    unified.contentType = Models::messageContentTypeFromString(msg.contentType);
    if (unified.contentType == Models::MessageContentType::Unknown) {
        unified.contentType = msg.contentImagePath.isEmpty()
            ? Models::MessageContentType::Text
            : Models::MessageContentType::Image;
    }
    unified.content = msg.content;
    unified.status = Models::MessageStatus::Observed;
    unified.sourceType = Models::sourceTypeFromString(msg.sourceType);
    unified.confidence = msg.confidence;
    unified.verificationStatus = Models::verificationStatusFromString(msg.verificationStatus);
    unified.observedAt = msg.createdAt.isValid() ? msg.createdAt : QDateTime::currentDateTime();
    unified.platformDisplayedAt = msg.createdAt;
    unified.evidenceRef = msg.contentImagePath;
    unified.metadata.insert(QStringLiteral("senderName"), msg.senderName);
    unified.metadata.insert(QStringLiteral("originalTimestamp"), msg.originalTimestamp);
    return unified;
}

Models::Conversation MessageRouter::fetchUnifiedConversation(int conversationId) const
{
    ConversationDao dao;
    auto legacy = dao.findById(conversationId);
    if (legacy) {
        return LegacyModelCompat::toUnifiedConversation(*legacy);
    }
    return Models::Conversation();
}

void MessageRouter::registerAdapter(IPlatformAdapter* adapter)
{
    if (!adapter) return;
    const QString name = adapter->platformName();
    m_adapters[name] = adapter;

    connect(adapter, &IPlatformAdapter::incomingMessage,
            this, &MessageRouter::onIncomingMessage);
    connect(adapter, &IPlatformAdapter::conversationObserved,
            this, &MessageRouter::onConversationObserved);
    connect(adapter, &IPlatformAdapter::messageSent,
            this, &MessageRouter::onMessageSent);
    connect(adapter, &IPlatformAdapter::sendFailed,
            this, &MessageRouter::onSendFailed);

    qInfo() << "[MessageRouter] adapter registered" << name;
}

void MessageRouter::unregisterAdapter(const QString& platformName)
{
    if (auto* a = m_adapters.take(platformName)) {
        disconnect(a, nullptr, this, nullptr);
        qInfo() << "[MessageRouter] adapter unregistered" << platformName;
    }
}

IPlatformAdapter* MessageRouter::adapter(const QString& platformName) const
{
    return m_adapters.value(platformName, nullptr);
}

void MessageRouter::sendMessage(int conversationId, const QString& text, const QString& clientMessageId)
{
    ConversationDao convDao;
    auto conv = convDao.findById(conversationId);
    if (!conv) {
        qWarning() << "[MessageRouter] conversation not found" << conversationId;
        emit messageSendFailed(conversationId, QStringLiteral("conversation not found"));
        return;
    }

    auto* a = m_adapters.value(conv->platform, nullptr);
    if (!a) {
        qWarning() << "[MessageRouter] platform adapter not found:" << conv->platform;
        emit messageSendFailed(conversationId, QStringLiteral("platform not connected"));
        return;
    }

    MessageDao msgDao;
    QDateTime now = QDateTime::currentDateTime();
    Models::Message pendingMessage;
    const QString normalizedClientMessageId = clientMessageId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : clientMessageId;
    pendingMessage.conversationId = conversationId;
    pendingMessage.direction = Models::MessageDirection::Outbound;
    pendingMessage.contentType = Models::MessageContentType::Text;
    pendingMessage.content = text;
    pendingMessage.status = Models::MessageStatus::Pending;
    pendingMessage.sourceType = Models::SourceType::ManualConfirmed;
    pendingMessage.confidence = Models::defaultConfidence(pendingMessage.sourceType);
    pendingMessage.verificationStatus = Models::VerificationStatus::ManualVerified;
    pendingMessage.observedAt = now;
    pendingMessage.metadata.insert(QStringLiteral("client_message_id"), normalizedClientMessageId);
    pendingMessage.clientMessageId = normalizedClientMessageId;
    int msgId = msgDao.create(pendingMessage);

    if (msgId <= 0) {
        emit messageSendFailed(conversationId, QStringLiteral("message store failed"));
        return;
    }

    convDao.updateLastMessage(conversationId, text, now);

    const auto persisted = msgDao.findById(msgId);
    if (persisted) {
        emit unifiedMessageReceived(conversationId, LegacyModelCompat::toUnifiedMessage(*persisted));
    }

    auto updatedConv = convDao.findById(conversationId);
    if (updatedConv) {
        emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*updatedConv));
        emit conversationUpdated(*updatedConv);
    }

    a->sendMessage(conv->platformConversationId, text, normalizedClientMessageId);

    qDebug() << "[MessageRouter] message sent convId=" << conversationId << "text=" << text.left(30);
}

void MessageRouter::onConversationObserved(const ConversationInfo& conv)
{
    ConversationDao dao;
    auto existing = dao.findByPlatformId(conv.platform, conv.platformConversationId);
    if (existing) {
        if (!conv.customerName.isEmpty() && existing->customerName != conv.customerName) {
            QSqlQuery q(Database::getInstance().connection());
            q.prepare(QStringLiteral(
                "UPDATE conversations SET customer_name = :name, updated_at = datetime('now','localtime') "
                "WHERE id = :id"));
            q.bindValue(QStringLiteral(":name"), conv.customerName);
            q.bindValue(QStringLiteral(":id"), existing->id);
            if (!q.exec())
                qWarning() << "[MessageRouter] failed to update conversation name" << q.lastError().text();
        }
        auto updated = dao.findById(existing->id);
        if (updated) {
            emit conversationUpdated(*updated);
            emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*updated));
        }
        qInfo() << "[MessageRouter] conversation observed updated existing"
                << "platform=" << conv.platform
                << "conversation=" << conv.platformConversationId
                << "conversationId=" << existing->id;
        return;
    }

    Models::Conversation unified;
    unified.platformType = Models::platformTypeFromString(conv.platform);
    unified.platformConversationId = conv.platformConversationId;
    unified.accountId = conv.accountId;
    unified.title = conv.customerName;
    unified.status = Models::ConversationStatus::Active;
    unified.sourceType = Models::sourceTypeFromString(conv.sourceType);
    unified.confidence = conv.confidence;
    unified.createdAt = conv.createdAt.isValid() ? conv.createdAt : QDateTime::currentDateTime();
    unified.updatedAt = conv.updatedAt.isValid() ? conv.updatedAt : unified.createdAt;
    unified.lastMessageAt = conv.updatedAt.isValid() ? conv.updatedAt : unified.createdAt;

    int id = dao.create(unified);
    if (id > 0) {
        auto created = dao.findById(id);
        if (created) {
            emit conversationCreated(*created);
            emit conversationUpdated(*created);
            emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*created));
        }
        qInfo() << "[MessageRouter] conversation observed created"
                << "platform=" << conv.platform
                << "conversation=" << conv.platformConversationId
                << "conversationId=" << id;
    }
}

void MessageRouter::onIncomingMessage(const PlatformMessage& msg)
{
    MessageDao msgDao;
    if (!msg.platformMsgId.isEmpty() && msgDao.existsByPlatformMsgId(msg.platformMsgId)) {
        qDebug() << "[MessageRouter] duplicate message skipped" << msg.platformMsgId;
        return;
    }

    int convId = ensureConversation(msg);
    if (convId <= 0) return;

    QDateTime now = msg.createdAt.isValid() ? msg.createdAt : QDateTime::currentDateTime();

    Models::Message unifiedMessage = createUnifiedMessage(msg, convId);
    unifiedMessage.observedAt = now;
    const QString uiSide = unifiedMessage.direction == Models::MessageDirection::Outbound
        ? QStringLiteral("right")
        : (unifiedMessage.direction == Models::MessageDirection::System
               ? QStringLiteral("system")
               : QStringLiteral("left"));
    qInfo() << "[MessageRouter] incoming message normalized"
            << "conversationId=" << convId
            << "platform=" << msg.platform
            << "rawDirection=" << msg.direction
            << "rawSender=" << msg.sender
            << "uiSide=" << uiSide
            << "unifiedDirection=" << Models::toString(unifiedMessage.direction)
            << "platformMsgId=" << msg.platformMsgId
            << "content=" << msg.content.left(30);
    int msgId = msgDao.create(unifiedMessage);

    ConversationDao convDao;
    if (msg.platform == QLatin1String("wechat_pc")) {
        WechatMessageDao wechatDao;
        wechatDao.upsertConversation(
            convId,
            QString(),
            msg.platformConversationId,
            msg.customerName,
            msg.metadata);
    }
    convDao.updateLastMessage(convId, msg.content, now);
    if (msg.direction == QLatin1String("in"))
        convDao.incrementUnread(convId);

    if (msgId > 0) {
        unifiedMessage.id = msgId;
        emit unifiedMessageReceived(convId, unifiedMessage);

        if (msg.platform == QLatin1String("wechat_pc")) {
            WechatMessageDao wechatDao;
            wechatDao.createMessageExtension(
                msgId,
                convId,
                QString(),
                msg.platformConversationId,
                msg.customerName,
                msg.platformMsgId,
                msg.metadata);
        }

        MessageRecord rec;
        rec.id = msgId;
        rec.conversationId = convId;
        rec.direction = msg.direction;
        rec.content = msg.content;
        rec.sender = msg.sender;
        rec.senderName = msg.senderName;
        rec.createdAt = now;
        rec.platformMsgId = msg.platformMsgId;
        rec.originalTimestamp = msg.originalTimestamp;
        rec.contentImagePath = msg.contentImagePath;
        rec.sourceType = msg.sourceType;
        rec.confidence = msg.confidence;
        rec.verificationStatus = msg.verificationStatus;
        rec.contentType = msg.contentType.isEmpty()
            ? (msg.contentImagePath.isEmpty() ? QStringLiteral("text") : QStringLiteral("image"))
            : msg.contentType;
        rec.status = QStringLiteral("observed");
        rec.observedAt = now;
        qInfo() << "[MessageRouter] message record emitted"
                << "conversationId=" << convId
                << "messageId=" << msgId
                << "direction=" << rec.direction
                << "sender=" << rec.sender
                << "uiSide=" << (rec.direction == QLatin1String("out") ? "right" : (rec.direction == QLatin1String("system") ? "system" : "left"))
                << "platformMsgId=" << rec.platformMsgId
                << "content=" << rec.content.left(30);
        emit messageReceived(convId, rec);
    }

    auto conv = convDao.findById(convId);
    if (conv) {
        emit conversationUpdated(*conv);
        emit unifiedConversationUpdated(fetchUnifiedConversation(convId));
    }

    qDebug() << "[MessageRouter] incoming message convId=" << convId
             << "from=" << msg.customerName << "content=" << msg.content.left(30);
}

void MessageRouter::onMessageSent(const QString& conversationId, const QString& text, const QString& clientMessageId)
{
    const auto* sentAdapter = qobject_cast<IPlatformAdapter*>(sender());
    if (!sentAdapter)
        return;

    ConversationDao convDao;
    const auto conv = convDao.findByPlatformId(sentAdapter->platformName(), conversationId);
    if (!conv)
        return;

    MessageDao msgDao;
    std::optional<MessageRecord> pending = msgDao.latestPendingOutboundByClientMessageId(conv->id, clientMessageId);
    if (!pending)
        pending = msgDao.latestPendingOutbound(conv->id, text);
    if (!pending) {
        qWarning() << "[MessageRouter] no pending outbound found platform="
                   << sentAdapter->platformName() << "conv=" << conversationId;
        return;
    }
    if (msgDao.updateDeliveryState(pending->id, 11)) {
        qInfo() << "[MessageRouter] message send confirmed convId=" << conv->id << "msgId=" << pending->id;
        emit messageStatusChanged(conv->id, pending->id, Models::MessageStatus::Sent, QString());
        const auto updatedConv = convDao.findById(conv->id);
        if (updatedConv) {
            emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*updatedConv));
            emit conversationUpdated(*updatedConv);
        }
    }
}

void MessageRouter::onSendFailed(const QString& conversationId, const QString& reason, const QString& clientMessageId)
{
    ConversationDao dao;
    const auto* failedAdapter = qobject_cast<IPlatformAdapter*>(sender());
    const QString platformName = failedAdapter ? failedAdapter->platformName() : QString();
    auto conv = platformName.isEmpty() ? std::nullopt : dao.findByPlatformId(platformName, conversationId);
    int cid = conv ? conv->id : 0;

    if (conv) {
        MessageDao msgDao;
        std::optional<MessageRecord> pending = msgDao.latestPendingOutboundByClientMessageId(conv->id, clientMessageId);
        if (!pending)
            pending = msgDao.latestPendingOutbound(conv->id);
        if (pending) {
            msgDao.updateDeliveryState(pending->id, 12, reason);
            emit messageStatusChanged(conv->id, pending->id, Models::MessageStatus::Failed, reason);
        }
        const auto updatedConv = dao.findById(conv->id);
        if (updatedConv) {
            emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*updatedConv));
            emit conversationUpdated(*updatedConv);
        }
    }

    qWarning() << "[MessageRouter] send failed platform=" << platformName
               << "conv=" << conversationId << "reason=" << reason;
    emit messageSendFailed(cid, reason);
}

int MessageRouter::ensureConversation(const PlatformMessage& msg)
{
    ConversationDao dao;
    auto existing = dao.findByPlatformId(msg.platform, msg.platformConversationId);
    if (existing) {
        if (existing->status == QLatin1String("closed")) {
            dao.setStatus(existing->id, QStringLiteral("active"));
            auto reopened = dao.findById(existing->id);
            if (reopened)
                emit conversationCreated(*reopened);
        }
        return existing->id;
    }

    int id = dao.create(msg.platform, msg.platformConversationId, msg.customerName);
    if (id > 0) {
        if (msg.platform == QLatin1String("wechat_pc")) {
            WechatMessageDao wechatDao;
            wechatDao.upsertConversation(
                id,
                msg.platform,
                msg.platformConversationId,
                msg.customerName,
                msg.metadata);
        }
        auto conv = dao.findById(id);
        if (conv)
            emit conversationCreated(*conv);
    }
    return id;
}
