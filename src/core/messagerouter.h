#ifndef MESSAGEROUTER_H
#define MESSAGEROUTER_H

#include <QMap>
#include <QObject>
#include "types.h"

class IPlatformAdapter;

class MessageRouter : public QObject
{
    Q_OBJECT
public:
    explicit MessageRouter(QObject* parent = nullptr);

    void registerAdapter(IPlatformAdapter* adapter);
    void unregisterAdapter(const QString& platformName);
    IPlatformAdapter* adapter(const QString& platformName) const;

    void sendMessage(int conversationId, const QString& text);

signals:
    void messageReceived(int conversationId, const MessageRecord& record);
    void messageSentOk(int conversationId, const MessageRecord& record);
    void messageSendFailed(int conversationId, const QString& reason);
    void conversationCreated(const ConversationInfo& conv);
    void conversationUpdated(const ConversationInfo& conv);

private slots:
    void onIncomingMessage(const PlatformMessage& msg);
    void onMessageSent(const QString& conversationId, const QString& text);
    void onSendFailed(const QString& conversationId, const QString& reason);

private:
    int ensureConversation(const PlatformMessage& msg);

    QMap<QString, IPlatformAdapter*> m_adapters;
};

#endif // MESSAGEROUTER_H
