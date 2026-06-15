#include "conversationmanager.h"
#include "messagerouter.h"
#include "../data/appdatauistatedao.h"
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
#include "../services/platforms/iplatformadapter.h"
#include "../utils/runtimemode.h"
#include <QDateTime>
#include <QDebug>

ConversationManager& ConversationManager::instance()
{
    static ConversationManager mgr;
    return mgr;
}

ConversationManager::ConversationManager(QObject* parent)
    : QObject(parent)
{
}

void ConversationManager::initialize(MessageRouter* router)
{
    if (!router) {
        qWarning() << "[ConversationManager] initialize failed: MessageRouter cannot be null";
        return;
    }
    if (m_router) {
        qWarning() << "[ConversationManager] already initialized, skip duplicate setup";
        return;
    }

    m_router = router;

    connect(m_router, &MessageRouter::conversationCreated, this, [this](const ConversationInfo& conv) {
        qDebug() << "[ConversationManager] new conversation" << conv.customerName;
        emit conversationListChanged();
        emit conversationUpdated(conv);
    });

    connect(m_router, &MessageRouter::conversationUpdated, this, [this](const ConversationInfo& conv) {
        emit conversationUpdated(conv);
        emit conversationListChanged();
    });

    connect(m_router, &MessageRouter::unifiedConversationUpdated,
            this, [this](const Models::Conversation& conv) {
        emit unifiedConversationUpdated(conv);
        emit conversationListChanged();
    });

    connect(m_router, &MessageRouter::messageReceived, this, [this](int convId, const MessageRecord& rec) {
        if (rec.direction == QLatin1String("in")) {
            ConversationDao dao;
            dao.setStatus(convId, QStringLiteral("waiting_agent"));
        }
        emit newMessageReceived(convId, rec);
    });

    connect(m_router, &MessageRouter::unifiedMessageReceived,
            this, [this](int convId, const Models::Message& message) {
        if (message.direction == Models::MessageDirection::Inbound) {
            ConversationDao dao;
            dao.setStatus(convId, QStringLiteral("waiting_agent"));
        }
        emit unifiedMessageReceived(convId, message);
    });

    connect(m_router, &MessageRouter::messageSentOk, this, [this](int convId, const MessageRecord& rec) {
        if (rec.direction == QLatin1String("out")) {
            ConversationDao dao;
            dao.setStatus(convId, QStringLiteral("waiting_customer"));
            const auto conv = dao.findById(convId);
            if (conv) {
                emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*conv));
                emit conversationUpdated(*conv);
            }
        }
        emit messageSentOk(convId, rec);
    });

    connect(m_router, &MessageRouter::messageSendFailed, this, [this](int convId, const QString& reason) {
        emit messageSendFailed(convId, reason);
    });

    connect(m_router, &MessageRouter::messageStatusChanged, this,
            [this](int convId, int msgId, Models::MessageStatus status, const QString& reason) {
        qDebug() << "[ConversationManager] message status changed convId=" << convId
                 << "msgId=" << msgId << "status=" << Models::toString(status);
        if (status == Models::MessageStatus::Sent) {
            ConversationDao dao;
            dao.setStatus(convId, QStringLiteral("waiting_customer"));
            const auto conv = dao.findById(convId);
            if (conv) {
                emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*conv));
                emit conversationUpdated(*conv);
            }
        }
        emit messageStatusChanged(convId, msgId, status, reason);
    });

    connect(m_router, &MessageRouter::conversationMessagesCleared, this, [this](int convId) {
        if (m_currentConvId == convId)
            emit currentConversationChanged(convId);
        emit conversationMessagesCleared(convId);
        emit conversationListChanged();
    });

    connect(m_router, &MessageRouter::conversationDeleted, this, [this](int convId) {
        if (m_currentConvId == convId) {
            m_currentConvId = -1;
            ConversationDao().setLastSelectedCachedConversationId(-1);
            emit currentConversationChanged(-1);
        }
        emit conversationDeleted(convId);
        emit conversationListChanged();
    });

    qInfo() << "[ConversationManager] initialization complete";
}

QVector<ConversationInfo> ConversationManager::allConversations() const
{
    ConversationDao dao;
    return dao.listCachedConversations();
}

QVector<MessageRecord> ConversationManager::messages(int conversationId) const
{
    MessageDao dao;
    return dao.listCachedMessages(conversationId);
}

void ConversationManager::reloadFromLocalCache()
{
    ConversationDao dao;

    if (m_currentConvId > 0) {
        const auto current = dao.findById(m_currentConvId);
        if (current) {
            emit conversationUpdated(*current);
            emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*current));
        } else {
            qInfo() << "[ConversationManager] current conversation missing after local cache reload:"
                    << m_currentConvId;
            m_currentConvId = -1;
            dao.setLastSelectedCachedConversationId(-1);
            emit currentConversationChanged(-1);
        }
    }

    qInfo() << "[ConversationManager] reloaded conversations from local cache";
    emit conversationListChanged();
}

void ConversationManager::reloadFromDatabase()
{
    reloadFromLocalCache();
}

void ConversationManager::selectConversation(int conversationId)
{
    const bool changed = m_currentConvId != conversationId;
    m_currentConvId = conversationId;
    ConversationDao dao;
    dao.setLastSelectedCachedConversationId(conversationId);
    if (RuntimeMode::isSingleHostServiceDb()) {
        if (conversationId <= 0) {
            AppDataUiStateDao().clearLastSelectedConversation();
        }
    }
    if (conversationId > 0) {
        clearUnread(conversationId);
        dao.setStatus(conversationId, QStringLiteral("active"));
        const auto conv = dao.findById(conversationId);
        if (conv) {
            if (RuntimeMode::isSingleHostServiceDb()) {
                AppDataUiStateDao().saveLastSelectedConversation(
                    conv->platform,
                    conv->platformConversationId);
            }
            emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*conv));
            emit conversationUpdated(*conv);
        }
    }
    qDebug() << "[ConversationManager] select conversation:" << conversationId;
    if (changed)
        emit currentConversationChanged(conversationId);
}

void ConversationManager::sendMessage(int conversationId, const QString& text, const QString& clientMessageId)
{
    if (text.trimmed().isEmpty()) return;
    if (!m_router) {
        qWarning() << "[ConversationManager] message router not initialized" << conversationId;
        emit messageSendFailed(conversationId, QStringLiteral("message router not initialized"));
        return;
    }
    qDebug() << "[ConversationManager] send message convId=" << conversationId;
    m_router->sendMessage(conversationId, text, clientMessageId);
}

void ConversationManager::sendMessage(int conversationId,
                                      const OutgoingMessagePart& part,
                                      const QString& clientMessageId)
{
    if (!m_router) {
        qWarning() << "[ConversationManager] message router not initialized" << conversationId;
        emit messageSendFailed(conversationId, QStringLiteral("message router not initialized"));
        return;
    }
    m_router->sendMessage(conversationId, part, clientMessageId);
}

void ConversationManager::sendPayload(int conversationId, const OutgoingMessagePayload& payload)
{
    if (!m_router) {
        emit messageSendFailed(conversationId, QStringLiteral("message router not initialized"));
        return;
    }
    m_router->sendPayload(conversationId, payload);
}

bool ConversationManager::startPlatformListening(const QString& platform)
{
    const QString normalized = platform.trimmed().toLower();
    if (!m_router || normalized.isEmpty()) {
        qWarning() << "[ConversationManager] start platform failed: router/platform missing" << platform;
        return false;
    }
    auto* adapter = m_router->adapter(normalized);
    if (!adapter) {
        qWarning() << "[ConversationManager] start platform failed: adapter not found" << normalized;
        return false;
    }
    adapter->startListening();
    const bool listening = adapter->isConnected();
    qInfo() << "[ConversationManager] platform listening requested" << normalized << "listening=" << listening;
    return listening;
}

bool ConversationManager::stopPlatformListening(const QString& platform)
{
    const QString normalized = platform.trimmed().toLower();
    if (!m_router || normalized.isEmpty()) {
        qWarning() << "[ConversationManager] stop platform failed: router/platform missing" << platform;
        return false;
    }
    auto* adapter = m_router->adapter(normalized);
    if (!adapter) {
        qWarning() << "[ConversationManager] stop platform failed: adapter not found" << normalized;
        return false;
    }
    adapter->disconnectPlatform();
    const bool stopped = !adapter->isConnected();
    qInfo() << "[ConversationManager] platform stop requested" << normalized << "stopped=" << stopped;
    return stopped;
}

bool ConversationManager::isPlatformListening(const QString& platform) const
{
    const QString normalized = platform.trimmed().toLower();
    if (!m_router || normalized.isEmpty())
        return false;
    auto* adapter = m_router->adapter(normalized);
    return adapter && adapter->isConnected();
}

void ConversationManager::closeConversation(int conversationId)
{
    ConversationDao dao;
    dao.setStatus(conversationId, QStringLiteral("closed"));
    if (m_currentConvId == conversationId) {
        m_currentConvId = -1;
        dao.setLastSelectedCachedConversationId(-1);
        emit currentConversationChanged(-1);
    }
    qInfo() << "[ConversationManager] close conversation:" << conversationId;
    emit conversationListChanged();
}

bool ConversationManager::clearConversationMessages(int conversationId)
{
    MessageDao msgDao;
    if (!msgDao.clearAllForConversation(conversationId))
        return false;

    ConversationDao convDao;
    convDao.updateLastMessage(conversationId, QString(), QDateTime::currentDateTime());
    convDao.clearUnread(conversationId);
    const auto conv = convDao.findById(conversationId);
    if (conv) {
        emit conversationUpdated(*conv);
        emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*conv));
    }
    emit conversationMessagesCleared(conversationId);
    emit conversationListChanged();
    qInfo() << "[ConversationManager] clear conversation messages:" << conversationId;
    return true;
}

void ConversationManager::deleteConversation(int conversationId)
{
    ConversationDao dao;
    dao.remove(conversationId);
    if (m_currentConvId == conversationId) {
        m_currentConvId = -1;
        dao.setLastSelectedCachedConversationId(-1);
        emit currentConversationChanged(-1);
    }
    qInfo() << "[ConversationManager] delete conversation:" << conversationId;
    emit conversationListChanged();
}

void ConversationManager::clearUnread(int conversationId)
{
    ConversationDao dao;
    dao.clearUnread(conversationId);
}
