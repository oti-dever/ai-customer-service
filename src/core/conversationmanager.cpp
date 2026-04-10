#include "conversationmanager.h"
#include "messagerouter.h"
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
#include "../services/platforms/simplatformadapter.h"
#include "../services/platforms/qianniurp_adapter.h"
#include "../services/platforms/wechatrp_adapter.h"
#include "../services/platforms/pddrp_adapter.h"

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

    m_qianniu = new QianniuRPAAdapter(this);
    m_router->registerAdapter(m_qianniu);
    m_qianniu->connectPlatform();
    m_qianniu->startListening();

    m_wechat = new WechatRPAAdapter(this);
    m_router->registerAdapter(m_wechat);
    m_wechat->connectPlatform();
    m_wechat->startListening();

    m_pdd = new PddRPAAdapter(this);
    m_router->registerAdapter(m_pdd);
    m_pdd->connectPlatform();
    m_pdd->startListening();

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

    qInfo() << "[ConversationManager] 初始化完成，模拟平台/千牛RPA/微信RPA/PDD RPA 适配器已就绪";
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
