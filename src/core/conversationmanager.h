#ifndef CONVERSATIONMANAGER_H
#define CONVERSATIONMANAGER_H

#include <QObject>
#include <QVector>
#include "types.h"
#include "../models/unifiedmodels.h"

class MessageRouter;

class ConversationManager : public QObject
{
    Q_OBJECT
public:
    static ConversationManager& instance();

    void initialize(MessageRouter* router);

    MessageRouter* router() const { return m_router; }

    /** 客户端本地会话缓存（聚合左侧列表在界面层按最后一条 direction 再分栏）。 */
    QVector<ConversationInfo> allConversations() const;
    /** 客户端本地消息缓存，用于当前 UI 恢复/展示。 */
    QVector<MessageRecord> messages(int conversationId) const;
    int currentConversationId() const { return m_currentConvId; }
    void reloadFromLocalCache();
    /** 兼容旧命名；主路径请使用 reloadFromLocalCache()。 */
    void reloadFromDatabase();

    void selectConversation(int conversationId);
    void sendMessage(int conversationId, const QString& text, const QString& clientMessageId = QString());
    void closeConversation(int conversationId);
    bool clearConversationMessages(int conversationId);
    void deleteConversation(int conversationId);
    void clearUnread(int conversationId);
    bool startPlatformListening(const QString& platform);
    bool stopPlatformListening(const QString& platform);
    bool isPlatformListening(const QString& platform) const;

signals:
    void conversationListChanged();
    void conversationUpdated(const ConversationInfo& conv);
    void conversationMessagesCleared(int conversationId);
    void conversationDeleted(int conversationId);
    void newMessageReceived(int conversationId, const MessageRecord& msg);
    void messageSentOk(int conversationId, const MessageRecord& msg);
    void messageSendFailed(int conversationId, const QString& reason);
    void currentConversationChanged(int conversationId);
    void messageStatusChanged(int conversationId, int messageId, Models::MessageStatus newStatus, const QString& errorReason);
    void unifiedMessageReceived(int conversationId, const Models::Message& message);
    void unifiedConversationUpdated(const Models::Conversation& conversation);

private:
    explicit ConversationManager(QObject* parent = nullptr);

    MessageRouter* m_router = nullptr;
    int m_currentConvId = -1;
};

#endif // CONVERSATIONMANAGER_H
