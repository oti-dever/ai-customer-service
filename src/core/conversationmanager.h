#ifndef CONVERSATIONMANAGER_H
#define CONVERSATIONMANAGER_H

#include <QObject>
#include <QVector>
#include "types.h"

class MessageRouter;
class SimPlatformAdapter;
class QianniuRPAAdapter;
class WechatRPAAdapter;
class PddRPAAdapter;

class ConversationManager : public QObject
{
    Q_OBJECT
public:
    static ConversationManager& instance();

    void initialize();

    MessageRouter* router() const { return m_router; }
    SimPlatformAdapter* simulator() const { return m_simulator; }
    QianniuRPAAdapter* qianniu() const { return m_qianniu; }
    WechatRPAAdapter* wechat() const { return m_wechat; }
    PddRPAAdapter* pdd() const { return m_pdd; }

    QVector<ConversationInfo> conversations(bool pendingOnly) const;
    QVector<MessageRecord> messages(int conversationId) const;
    int currentConversationId() const { return m_currentConvId; }

    void selectConversation(int conversationId);
    void sendMessage(int conversationId, const QString& text);
    void closeConversation(int conversationId);
    void deleteConversation(int conversationId);
    void clearUnread(int conversationId);

signals:
    void conversationListChanged();
    void conversationUpdated(const ConversationInfo& conv);
    void newMessageReceived(int conversationId, const MessageRecord& msg);
    void messageSentOk(int conversationId, const MessageRecord& msg);
    void messageSendFailed(int conversationId, const QString& reason);
    void currentConversationChanged(int conversationId);

private:
    explicit ConversationManager(QObject* parent = nullptr);

    MessageRouter* m_router = nullptr;
    SimPlatformAdapter* m_simulator = nullptr;
    QianniuRPAAdapter* m_qianniu = nullptr;
    WechatRPAAdapter* m_wechat = nullptr;
    PddRPAAdapter* m_pdd = nullptr;
    int m_currentConvId = -1;
};

#endif // CONVERSATIONMANAGER_H
