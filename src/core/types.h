#ifndef TYPES_H
#define TYPES_H

#include <QDateTime>
#include <QMetaType>
#include <QString>

struct PlatformMessage {
    QString platform;
    QString platformConversationId;
    QString customerName;
    QString content;
    QString direction; // "in" or "out"
    QString sender;    // "customer", "agent", "system"
    QDateTime createdAt;
    QString platformMsgId;
};

struct ConversationInfo {
    int id = 0;
    QString platform;
    QString platformConversationId;
    QString customerName;
    QString lastMessage;
    QDateTime lastTime;
    int unreadCount = 0;
    QString status; // "open" or "closed"
    QDateTime createdAt;
};

struct MessageRecord {
    int id = 0;
    int conversationId = 0;
    QString direction; // "in" or "out"
    QString content;
    QString sender; // "customer", "agent", "system"
    QDateTime createdAt;
    QString platformMsgId;
};

Q_DECLARE_METATYPE(PlatformMessage)
Q_DECLARE_METATYPE(ConversationInfo)
Q_DECLARE_METATYPE(MessageRecord)

#endif // TYPES_H
