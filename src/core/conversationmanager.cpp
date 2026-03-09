#include "conversationmanager.h"
#include "messagerouter.h"
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
#include "../services/platforms/simplatformadapter.h"

ConversationManager& ConversationManager::instance()
{
    static ConversationManager mgr;
    return mgr;
}

ConversationManager::ConversationManager(QObject* parent)
    : QObject(parent)
{
}

void ConversationManager::initialize()
{
    m_router = new MessageRouter(this);

    m_simulator = new SimPlatformAdapter(this);
    m_router->registerAdapter(m_simulator);
    m_simulator->connectPlatform();
    m_simulator->startListening();

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

    qInfo() << "[ConversationManager] 初始化完成，模拟平台已就绪";
}

QVector<ConversationInfo> ConversationManager::conversations(bool pendingOnly) const
{
    ConversationDao dao;
    if (pendingOnly)
        return dao.listByStatus(QStringLiteral("open"));
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
    clearUnread(conversationId);
    qDebug() << "[ConversationManager] 选择会话:" << conversationId;
    emit currentConversationChanged(conversationId);
}

void ConversationManager::sendMessage(int conversationId, const QString& text)
{
    if (text.trimmed().isEmpty()) return;
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
