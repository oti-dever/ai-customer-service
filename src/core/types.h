#ifndef TYPES_H
#define TYPES_H

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>
#include "../models/unifiedmodels.h"

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
    QString contentImagePath;  // 千牛聊天区截图等；空表示纯文本入站
    QString sourceType = QStringLiteral("mock");
    int confidence = 100;
    QString verificationStatus = QStringLiteral("unverified");
    QString contentType = QStringLiteral("text");
    QJsonObject metadata;
};

struct ConversationInfo {
    int id = 0;
    QString platform;
    QString platformConversationId;
    QString customerName;
    QString lastMessage;
    QDateTime lastTime;
    int unreadCount = 0;
    QString status; // "new", "active", "waiting_agent", "waiting_customer", "closed"
    QDateTime createdAt;
    QString accountId;
    QString sourceType = QStringLiteral("mock");
    int confidence = 100;
    QDateTime updatedAt;
    QString cacheScope = QStringLiteral("local_cache");
    QString cacheOrigin = QStringLiteral("legacy_runtime");
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
    QString contentImagePath;  // 千牛等：聊天区截图本地路径；空表示纯文本
    QString clientMessageId;   // C++ 发起发送命令时生成的本地消息关联 ID
    QString status = QStringLiteral("observed");
    QString sourceType = QStringLiteral("mock");
    int confidence = 100;
    QString verificationStatus = QStringLiteral("unverified");
    QString contentType = QStringLiteral("text");
    QDateTime observedAt;
    QString cacheScope = QStringLiteral("local_cache");
    QString cacheOrigin = QStringLiteral("legacy_runtime");
};

enum class OutgoingPartType {
    Text,
    Image,
    Video,
    File,
};

struct OutgoingMessagePart {
    OutgoingPartType type = OutgoingPartType::Text;
    QString text;
    QString localPath;
    QString fileName;
    qint64 sizeBytes = 0;
    QString mimeType;
};

struct OutgoingMessagePayload {
    QVector<OutgoingMessagePart> parts;
};

/** 聚合「生成本条回复」：最后一条有效入站文本与可选聊天区截图路径。 */
struct LatestInboundSnapshot {
    QString content;
    QString contentImagePath;
};

namespace LegacyModelCompat {

Models::Conversation toUnifiedConversation(const ConversationInfo& value);
Models::Message toUnifiedMessage(const MessageRecord& value);
Models::ConversationEvent toMessageObservedEvent(const PlatformMessage& value);

} // namespace LegacyModelCompat

Q_DECLARE_METATYPE(PlatformMessage)
Q_DECLARE_METATYPE(ConversationInfo)
Q_DECLARE_METATYPE(MessageRecord)
Q_DECLARE_METATYPE(OutgoingMessagePart)
Q_DECLARE_METATYPE(OutgoingMessagePayload)

#endif // TYPES_H
