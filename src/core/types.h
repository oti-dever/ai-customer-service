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
    QString senderName;        // OCR 识别的发送者名称（如 "店铺:昵称"）
    QString originalTimestamp; // 入站展示用时间（如微信 RPA 为入库/解析时刻；千牛等可为 OCR 时间）
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
    QString senderName; // OCR 识别的发送者名称（如千牛店铺:昵称；微信通常为空）
    QDateTime createdAt;
    QString platformMsgId;
    int syncStatus = 1;      // 1=normal, 10=pending_send, 11=sent_ok, 12=sent_failed
    QString errorReason;
    QString originalTimestamp; // 对方消息在聚合侧的展示时间（来源依平台：入库时刻或 OCR）
};

Q_DECLARE_METATYPE(PlatformMessage)
Q_DECLARE_METATYPE(ConversationInfo)
Q_DECLARE_METATYPE(MessageRecord)

#endif // TYPES_H
