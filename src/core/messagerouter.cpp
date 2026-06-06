#include "messagerouter.h"
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
#include "../data/wechatmessagedao.h"
#include "../services/platforms/iplatformadapter.h"
#include "types.h"
#include <QElapsedTimer>

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
    connect(adapter, &IPlatformAdapter::conversationMessagesCleared,
            this, &MessageRouter::onConversationMessagesCleared);
    connect(adapter, &IPlatformAdapter::conversationDeleted,
            this, &MessageRouter::onConversationDeleted);
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
    QElapsedTimer totalTimer;
    totalTimer.start();
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
    QElapsedTimer cacheTimer;
    cacheTimer.start();
    int msgId = msgDao.createOutboundCacheMessage(pendingMessage);
    const qint64 createPendingElapsedMs = cacheTimer.elapsed();

    if (msgId <= 0) {
        qWarning() << "[MessageRouter] send timing"
                   << "conversationId=" << conversationId
                   << "clientMessageId=" << normalizedClientMessageId
                   << "stage=create_pending"
                   << "elapsedMs=" << totalTimer.elapsed()
                   << "createPendingElapsedMs=" << createPendingElapsedMs;
        emit messageSendFailed(conversationId, QStringLiteral("message store failed"));
        return;
    }

    cacheTimer.restart();
    convDao.updateLastMessage(conversationId, text, now);
    const qint64 updateConversationElapsedMs = cacheTimer.elapsed();

    QElapsedTimer emitTimer;
    emitTimer.start();
    const auto persisted = msgDao.findById(msgId);
    if (persisted) {
        emit unifiedMessageReceived(conversationId, LegacyModelCompat::toUnifiedMessage(*persisted));
    }

    auto updatedConv = convDao.findById(conversationId);
    if (updatedConv) {
        emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*updatedConv));
        emit conversationUpdated(*updatedConv);
    }
    const qint64 emitPendingElapsedMs = emitTimer.elapsed();

    QElapsedTimer adapterTimer;
    adapterTimer.start();
    a->sendMessage(conv->platformConversationId, text, normalizedClientMessageId);
    const qint64 adapterElapsedMs = adapterTimer.elapsed();

    qInfo() << "[MessageRouter] send timing"
            << "conversationId=" << conversationId
            << "platform=" << conv->platform
            << "platformConversationId=" << conv->platformConversationId
            << "clientMessageId=" << normalizedClientMessageId
            << "createPendingElapsedMs=" << createPendingElapsedMs
            << "updateConversationElapsedMs=" << updateConversationElapsedMs
            << "emitPendingElapsedMs=" << emitPendingElapsedMs
            << "adapterElapsedMs=" << adapterElapsedMs
            << "totalElapsedMs=" << totalTimer.elapsed()
            << "text=" << text.left(30);
}

void MessageRouter::onConversationObserved(const ConversationInfo& conv)
{
    QElapsedTimer timer;
    timer.start();
    ConversationDao dao;
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

    const auto existing = dao.findByPlatformId(conv.platform, conv.platformConversationId);
    const int id = dao.upsertObservedCacheConversation(unified);
    if (id > 0) {
        auto persisted = dao.findById(id);
        if (persisted) {
            if (!existing) {
                emit conversationCreated(*persisted);
            }
            emit conversationUpdated(*persisted);
            emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*persisted));
        }
        qInfo() << "[MessageRouter] conversation observed cached"
                << "platform=" << conv.platform
                << "conversation=" << conv.platformConversationId
                << "conversationId=" << id
                << "elapsedMs=" << timer.elapsed();
    }
}

void MessageRouter::onConversationMessagesCleared(const QString& conversationId)
{
    const auto* adapter = qobject_cast<IPlatformAdapter*>(sender());
    const QString platformName = adapter ? adapter->platformName() : QString();
    ConversationDao convDao;
    const auto conv = platformName.isEmpty()
        ? std::nullopt
        : convDao.findByPlatformId(platformName, conversationId);
    if (!conv)
        return;

    MessageDao msgDao;
    if (!msgDao.clearAllForConversation(conv->id))
        return;
    convDao.updateLastMessage(conv->id, QString(), QDateTime::currentDateTime());
    convDao.clearUnread(conv->id);
    const auto updated = convDao.findById(conv->id);
    if (updated) {
        emit conversationUpdated(*updated);
        emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*updated));
    }
    emit conversationMessagesCleared(conv->id);
    qInfo() << "[MessageRouter] conversation messages cleared"
            << "platform=" << platformName
            << "conversation=" << conversationId
            << "conversationId=" << conv->id;
}

void MessageRouter::onConversationDeleted(const QString& conversationId)
{
    const auto* adapter = qobject_cast<IPlatformAdapter*>(sender());
    const QString platformName = adapter ? adapter->platformName() : QString();
    ConversationDao convDao;
    const auto conv = platformName.isEmpty()
        ? std::nullopt
        : convDao.findByPlatformId(platformName, conversationId);
    if (!conv)
        return;

    const int localId = conv->id;
    if (!convDao.remove(localId))
        return;
    emit conversationDeleted(localId);
    qInfo() << "[MessageRouter] conversation deleted"
            << "platform=" << platformName
            << "conversation=" << conversationId
            << "conversationId=" << localId;
}

void MessageRouter::onIncomingMessage(const PlatformMessage& msg)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    MessageDao msgDao;
    if (!msg.platformMsgId.isEmpty() && msgDao.existsByPlatformMsgId(msg.platformMsgId)) {
        qDebug() << "[MessageRouter] duplicate message skipped" << msg.platformMsgId;
        return;
    }

    QElapsedTimer stageTimer;
    stageTimer.start();
    int convId = ensureConversation(msg);
    const qint64 ensureConversationElapsedMs = stageTimer.elapsed();
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
    stageTimer.restart();
    int msgId = msgDao.createObservedCacheMessage(unifiedMessage);
    const qint64 createMessageElapsedMs = stageTimer.elapsed();

    ConversationDao convDao;
    if (msg.platform == QLatin1String("wechat")) {
        WechatMessageDao wechatDao;
        wechatDao.upsertConversation(
            convId,
            QString(),
            msg.platformConversationId,
            msg.customerName,
            msg.metadata);
    }
    stageTimer.restart();
    convDao.updateLastMessage(convId, msg.content, now);
    if (msg.direction == QLatin1String("in"))
        convDao.incrementUnread(convId);
    const qint64 updateConversationElapsedMs = stageTimer.elapsed();

    if (msgId > 0) {
        unifiedMessage.id = msgId;
        stageTimer.restart();
        emit unifiedMessageReceived(convId, unifiedMessage);
        const qint64 emitUnifiedElapsedMs = stageTimer.elapsed();

        if (msg.platform == QLatin1String("wechat")) {
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
        const auto persisted = msgDao.findById(msgId);
        if (persisted) {
            rec = *persisted;
        } else {
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
            rec.cacheScope = QStringLiteral("local_cache");
            rec.cacheOrigin = QStringLiteral("platform_observed_cache");
        }
        qInfo() << "[MessageRouter] message record emitted"
                << "conversationId=" << convId
                << "messageId=" << msgId
                << "direction=" << rec.direction
                << "sender=" << rec.sender
                << "uiSide=" << (rec.direction == QLatin1String("out") ? "right" : (rec.direction == QLatin1String("system") ? "system" : "left"))
                << "platformMsgId=" << rec.platformMsgId
                << "ensureConversationElapsedMs=" << ensureConversationElapsedMs
                << "createMessageElapsedMs=" << createMessageElapsedMs
                << "updateConversationElapsedMs=" << updateConversationElapsedMs
                << "emitUnifiedElapsedMs=" << emitUnifiedElapsedMs
                << "totalElapsedMs=" << totalTimer.elapsed()
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
    QElapsedTimer totalTimer;
    totalTimer.start();
    const auto* sentAdapter = qobject_cast<IPlatformAdapter*>(sender());
    if (!sentAdapter)
        return;

    ConversationDao convDao;
    const auto conv = convDao.findByPlatformId(sentAdapter->platformName(), conversationId);
    if (!conv)
        return;

    MessageDao msgDao;
    QElapsedTimer stageTimer;
    stageTimer.start();
    std::optional<MessageRecord> pending = msgDao.latestOutboundCacheByClientMessageId(conv->id, clientMessageId);
    if (!pending)
        pending = msgDao.latestPendingOutboundCache(conv->id, text);
    const qint64 findPendingElapsedMs = stageTimer.elapsed();
    if (!pending) {
        qWarning() << "[MessageRouter] no pending outbound found platform="
                   << sentAdapter->platformName() << "conv=" << conversationId
                   << "clientMessageId=" << clientMessageId
                   << "findPendingElapsedMs=" << findPendingElapsedMs
                   << "totalElapsedMs=" << totalTimer.elapsed();
        return;
    }
    if (pending->syncStatus == 11) {
        qInfo() << "[MessageRouter] send confirm ignored: already sent"
                << "platform=" << sentAdapter->platformName()
                << "convId=" << conv->id
                << "msgId=" << pending->id
                << "clientMessageId=" << clientMessageId;
        return;
    }
    stageTimer.restart();
    if (msgDao.updateOutboundCacheDeliveryState(pending->id, 11)) {
        const qint64 updateStateElapsedMs = stageTimer.elapsed();
        qInfo() << "[MessageRouter] send confirm timing"
                << "platform=" << sentAdapter->platformName()
                << "convId=" << conv->id
                << "msgId=" << pending->id
                << "clientMessageId=" << clientMessageId
                << "findPendingElapsedMs=" << findPendingElapsedMs
                << "updateStateElapsedMs=" << updateStateElapsedMs
                << "totalElapsedMs=" << totalTimer.elapsed();
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
    QElapsedTimer totalTimer;
    totalTimer.start();
    ConversationDao dao;
    const auto* failedAdapter = qobject_cast<IPlatformAdapter*>(sender());
    const QString platformName = failedAdapter ? failedAdapter->platformName() : QString();
    auto conv = platformName.isEmpty() ? std::nullopt : dao.findByPlatformId(platformName, conversationId);

    if (conv) {
        MessageDao msgDao;
        std::optional<MessageRecord> pending = msgDao.latestPendingOutboundCacheByClientMessageId(conv->id, clientMessageId);
        if (!pending)
            pending = msgDao.latestPendingOutboundCache(conv->id);
        if (pending) {
            msgDao.updateOutboundCacheDeliveryState(pending->id, 12, reason);
            emit messageStatusChanged(conv->id, pending->id, Models::MessageStatus::Failed, reason);
        }
        const auto updatedConv = dao.findById(conv->id);
        if (updatedConv) {
            emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*updatedConv));
            emit conversationUpdated(*updatedConv);
        }
    }

    qWarning() << "[MessageRouter] send failed platform=" << platformName
               << "conv=" << conversationId
               << "clientMessageId=" << clientMessageId
               << "elapsedMs=" << totalTimer.elapsed()
               << "reason=" << reason;
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

    Models::Conversation observedConversation;
    observedConversation.platformType = Models::platformTypeFromString(msg.platform);
    observedConversation.platformConversationId = msg.platformConversationId;
    observedConversation.accountId = msg.platform;
    observedConversation.title = msg.customerName;
    observedConversation.status = Models::ConversationStatus::Active;
    observedConversation.sourceType = Models::sourceTypeFromString(msg.sourceType);
    observedConversation.confidence = msg.confidence;
    observedConversation.createdAt = msg.createdAt.isValid() ? msg.createdAt : QDateTime::currentDateTime();
    observedConversation.updatedAt = observedConversation.createdAt;

    int id = dao.upsertObservedCacheConversation(observedConversation);
    if (id > 0) {
        if (msg.platform == QLatin1String("wechat")) {
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
