#ifndef MESSAGEROUTER_H
#define MESSAGEROUTER_H

#include <QMap>
#include <QObject>
#include "types.h"
#include "../models/unifiedmodels.h"

class IPlatformAdapter;

class MessageRouter : public QObject
{
    Q_OBJECT
public:
    explicit MessageRouter(QObject* parent = nullptr);

    void registerAdapter(IPlatformAdapter* adapter);
    void unregisterAdapter(const QString& platformName);
    IPlatformAdapter* adapter(const QString& platformName) const;

    void sendMessage(int conversationId, const QString& text, const QString& clientMessageId = QString());

    void dispatchEvent(const Models::ConversationEvent& event);

signals:
    void messageReceived(int conversationId, const MessageRecord& record);
    void messageSentOk(int conversationId, const MessageRecord& record);
    void messageSendFailed(int conversationId, const QString& reason);
    void conversationCreated(const ConversationInfo& conv);
    void conversationUpdated(const ConversationInfo& conv);

    void unifiedMessageReceived(int conversationId, const Models::Message& message);
    void unifiedConversationUpdated(const Models::Conversation& conversation);
    void conversationEventDispatched(const Models::ConversationEvent& event);

    void messageStatusChanged(int conversationId, int messageId, Models::MessageStatus newStatus, const QString& errorReason);

private slots:
    void onConversationObserved(const ConversationInfo& conv);
    void onIncomingMessage(const PlatformMessage& msg);
    void onMessageSent(const QString& conversationId, const QString& text, const QString& clientMessageId = QString());
    void onSendFailed(const QString& conversationId, const QString& reason, const QString& clientMessageId = QString());

private:
    int ensureConversation(const PlatformMessage& msg);
    Models::Message createUnifiedMessage(const PlatformMessage& msg, int conversationId) const;
    Models::Conversation fetchUnifiedConversation(int conversationId) const;

    QMap<QString, IPlatformAdapter*> m_adapters;
};

#endif // MESSAGEROUTER_H
