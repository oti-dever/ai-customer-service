#include "conversationmanager.h"
#include "messagerouter.h"
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
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

    qInfo() << "[ConversationManager] initialization complete";
}

QVector<ConversationInfo> ConversationManager::allConversations() const
{
    ConversationDao dao;
    return dao.listAll();
}

QVector<MessageRecord> ConversationManager::messages(int conversationId) const
{
    MessageDao dao;
    return dao.listByConversation(conversationId);
}

void ConversationManager::reloadFromDatabase()
{
    ConversationDao dao;

    if (m_currentConvId > 0) {
        const auto current = dao.findById(m_currentConvId);
        if (current) {
            emit conversationUpdated(*current);
            emit unifiedConversationUpdated(LegacyModelCompat::toUnifiedConversation(*current));
        } else {
            qInfo() << "[ConversationManager] current conversation missing after database reload:"
                    << m_currentConvId;
            m_currentConvId = -1;
            dao.setLastSelectedConversationId(-1);
            emit currentConversationChanged(-1);
        }
    }

    qInfo() << "[ConversationManager] reloaded conversations from database";
    emit conversationListChanged();
}

void ConversationManager::selectConversation(int conversationId)
{
    const bool changed = m_currentConvId != conversationId;
    m_currentConvId = conversationId;
    ConversationDao dao;
    dao.setLastSelectedConversationId(conversationId);
    if (conversationId > 0) {
        clearUnread(conversationId);
        dao.setStatus(conversationId, QStringLiteral("active"));
        const auto conv = dao.findById(conversationId);
        if (conv) {
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

void ConversationManager::closeConversation(int conversationId)
{
    ConversationDao dao;
    dao.setStatus(conversationId, QStringLiteral("closed"));
    if (m_currentConvId == conversationId) {
        m_currentConvId = -1;
        dao.setLastSelectedConversationId(-1);
        emit currentConversationChanged(-1);
    }
    qInfo() << "[ConversationManager] close conversation:" << conversationId;
    emit conversationListChanged();
}

void ConversationManager::deleteConversation(int conversationId)
{
    ConversationDao dao;
    dao.remove(conversationId);
    if (m_currentConvId == conversationId) {
        m_currentConvId = -1;
        dao.setLastSelectedConversationId(-1);
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
