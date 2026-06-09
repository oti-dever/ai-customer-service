#ifndef UNIFIEDMODELS_H
#define UNIFIEDMODELS_H

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace Models {

enum class PlatformType {
    Mock,
    PddWeb,
    QianniuPc,
    WechatPc,
    GenericWeb,
    GenericPc,
    Unknown,
};

enum class ConversationStatus {
    New,
    Active,
    WaitingCustomer,
    WaitingAgent,
    Closed,
    Degraded,
    Unknown,
};

enum class MessageDirection {
    Inbound,
    Outbound,
    System,
};

enum class MessageContentType {
    Text,
    Image,
    Emoji,
    File,
    Video,
    Link,
    OrderCard,
    ProductCard,
    System,
    Unknown,
};

enum class MessageStatus {
    Draft,
    Observed,
    Pending,
    Sent,
    Failed,
    Unknown,
    Recalled,
};

enum class SourceType {
    Mock,
    DomObserved,
    UiObserved,
    OcrExtracted,
    NotificationObserved,
    ManualConfirmed,
    Experimental,
};

enum class VerificationStatus {
    Unverified,
    AutoVerified,
    ManualVerified,
    Conflict,
};

enum class PlatformAccountStatus {
    Offline,
    Starting,
    Online,
    Degraded,
    Error,
    Unknown,
};

QString toString(PlatformType value);
QString toString(ConversationStatus value);
QString toString(MessageDirection value);
QString toString(MessageContentType value);
QString toString(MessageStatus value);
QString toString(SourceType value);
QString toString(VerificationStatus value);
QString toString(PlatformAccountStatus value);

PlatformType platformTypeFromString(const QString& value);
ConversationStatus conversationStatusFromString(const QString& value);
MessageDirection messageDirectionFromString(const QString& value);
MessageContentType messageContentTypeFromString(const QString& value);
MessageStatus messageStatusFromString(const QString& value);
SourceType sourceTypeFromString(const QString& value);
VerificationStatus verificationStatusFromString(const QString& value);
PlatformAccountStatus platformAccountStatusFromString(const QString& value);

int defaultConfidence(SourceType sourceType);
MessageStatus messageStatusFromLegacySyncStatus(int syncStatus);
int legacySyncStatusFromMessageStatus(MessageStatus status);
QString legacyDirectionFromMessageDirection(MessageDirection direction);
MessageDirection messageDirectionFromLegacy(const QString& direction, const QString& sender = QString());

struct Conversation {
    int id = 0;
    PlatformType platformType = PlatformType::Unknown;
    QString platformConversationId;
    QString accountId;
    QString title;
    ConversationStatus status = ConversationStatus::Unknown;
    QString lastMessage;
    QDateTime lastMessageAt;
    int unreadCount = 0;
    SourceType sourceType = SourceType::Mock;
    int confidence = defaultConfidence(SourceType::Mock);
    QJsonObject metadata;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct Message {
    int id = 0;
    int conversationId = 0;
    QString platformMessageId;
    QString clientMessageId;
    MessageDirection direction = MessageDirection::Inbound;
    MessageContentType contentType = MessageContentType::Text;
    QString content;
    MessageStatus status = MessageStatus::Observed;
    SourceType sourceType = SourceType::Mock;
    int confidence = defaultConfidence(SourceType::Mock);
    VerificationStatus verificationStatus = VerificationStatus::Unverified;
    QDateTime observedAt;
    QDateTime platformDisplayedAt;
    QString evidenceRef;
    QJsonObject metadata;
};

struct PlatformAccount {
    QString id;
    PlatformType platformType = PlatformType::Unknown;
    QString displayName;
    PlatformAccountStatus status = PlatformAccountStatus::Unknown;
    QString adapterStatus;
    QDateTime lastActiveAt;
    QString lastError;
    QJsonObject metadata;
};

struct ConversationEvent {
    QString id;
    QString type;
    PlatformType platformType = PlatformType::Unknown;
    QString accountId;
    int conversationId = 0;
    int messageId = 0;
    SourceType sourceType = SourceType::Mock;
    int confidence = defaultConfidence(SourceType::Mock);
    QJsonObject payload;
    QDateTime createdAt;
};

struct SendMessageCommand {
    QString id;
    int conversationId = 0;
    QString content;
    QString mode;
    MessageStatus status = MessageStatus::Draft;
    QString createdBy;
    QDateTime createdAt;
    QDateTime confirmedAt;
    QJsonObject metadata;
};

struct AISuggestion {
    QString id;
    int conversationId = 0;
    QString content;
    int confidence = 0;
    bool accepted = false;
    QDateTime createdAt;
    QJsonObject metadata;
};

} // namespace Models

Q_DECLARE_METATYPE(Models::Conversation)
Q_DECLARE_METATYPE(Models::Message)
Q_DECLARE_METATYPE(Models::PlatformAccount)
Q_DECLARE_METATYPE(Models::ConversationEvent)
Q_DECLARE_METATYPE(Models::SendMessageCommand)
Q_DECLARE_METATYPE(Models::AISuggestion)

#endif // UNIFIEDMODELS_H
