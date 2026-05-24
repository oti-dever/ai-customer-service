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
        qWarning() << "[ConversationManager] 初始化失败：MessageRouter 不能为空";
        return;
    }
    if (m_router) {
        qWarning() << "[ConversationManager] 已初始化，忽略重复装配";
        return;
    }

    m_router = router;

    connect(m_router, &MessageRouter::conversationCreated, this, [this](const ConversationInfo& conv) {
        qDebug() << "[ConversationManager] 新会话:" << conv.customerName;
        emit conversationListChanged();
        emit conversationUpdated(conv);
    });

    connect(m_router, &MessageRouter::conversationUpdated, this, [this](const ConversationInfo& conv) {
        emit conversationUpdated(conv);
        emit conversationListChanged();
    });

    connect(m_router, &MessageRouter::messageReceived, this, [this](int convId, const MessageRecord& rec) {
        emit newMessageReceived(convId, rec);
    });

    connect(m_router, &MessageRouter::messageSentOk, this, [this](int convId, const MessageRecord& rec) {
        emit messageSentOk(convId, rec);
    });

    connect(m_router, &MessageRouter::messageSendFailed, this, [this](int convId, const QString& reason) {
        emit messageSendFailed(convId, reason);
    });

    qInfo() << "[ConversationManager] 初始化完成，消息路由已装配";
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

void ConversationManager::selectConversation(int conversationId)
{
    if (m_currentConvId == conversationId)
        return;
    m_currentConvId = conversationId;
    if (conversationId > 0)
        clearUnread(conversationId);
    qDebug() << "[ConversationManager] 选择会话:" << conversationId;
    emit currentConversationChanged(conversationId);
}

void ConversationManager::sendMessage(int conversationId, const QString& text)
{
    if (text.trimmed().isEmpty()) return;
    if (!m_router) {
        qWarning() << "[ConversationManager] 消息路由未初始化，无法发送";
        emit messageSendFailed(conversationId, QStringLiteral("消息路由未初始化"));
        return;
    }
    qDebug() << "[ConversationManager] 发送消息 convId=" << conversationId;
    m_router->sendMessage(conversationId, text);
}

void ConversationManager::closeConversation(int conversationId)
{
    ConversationDao dao;
    dao.setStatus(conversationId, QStringLiteral("closed"));
    qInfo() << "[ConversationManager] 关闭会话:" << conversationId;
    emit conversationListChanged();
}

void ConversationManager::deleteConversation(int conversationId)
{
    ConversationDao dao;
    dao.remove(conversationId);
    if (m_currentConvId == conversationId) {
        m_currentConvId = -1;
        emit currentConversationChanged(-1);
    }
    qInfo() << "[ConversationManager] 删除会话:" << conversationId;
    emit conversationListChanged();
}

void ConversationManager::clearUnread(int conversationId)
{
    ConversationDao dao;
    dao.clearUnread(conversationId);
}
