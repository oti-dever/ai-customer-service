#include "types.h"

namespace {

Models::SourceType sourceTypeOrDefault(const QString& value, const QString& platform)
{
    if (!value.trimmed().isEmpty())
        return Models::sourceTypeFromString(value);
    if (platform == QLatin1String("simulator") || platform == QLatin1String("mock"))
        return Models::SourceType::Mock;
    return Models::SourceType::UiObserved;
}

int confidenceOrDefault(int confidence, Models::SourceType sourceType)
{
    if (confidence >= 0 && confidence <= 100)
        return confidence;
    return Models::defaultConfidence(sourceType);
}

} // namespace

namespace LegacyModelCompat {

Models::Conversation toUnifiedConversation(const ConversationInfo& value)
{
    Models::Conversation out;
    out.id = value.id;
    out.platformType = Models::platformTypeFromString(value.platform);
    out.platformConversationId = value.platformConversationId;
    out.accountId = value.accountId.isEmpty() ? value.platform : value.accountId;
    out.title = value.customerName;
    out.status = Models::conversationStatusFromString(value.status);
    out.lastMessage = value.lastMessage;
    out.lastMessageAt = value.lastTime;
    out.unreadCount = value.unreadCount;
    out.sourceType = sourceTypeOrDefault(value.sourceType, value.platform);
    out.confidence = confidenceOrDefault(value.confidence, out.sourceType);
    out.createdAt = value.createdAt;
    out.updatedAt = value.updatedAt.isValid() ? value.updatedAt : value.lastTime;
    return out;
}

Models::Message toUnifiedMessage(const MessageRecord& value)
{
    Models::Message out;
    out.id = value.id;
    out.conversationId = value.conversationId;
    out.platformMessageId = value.platformMsgId;
    out.clientMessageId = value.clientMessageId;
    out.direction = Models::messageDirectionFromLegacy(value.direction, value.sender);
    out.contentType = Models::messageContentTypeFromString(value.contentType);
    if (out.contentType == Models::MessageContentType::Unknown)
        out.contentType = value.contentImagePath.isEmpty()
            ? Models::MessageContentType::Text
            : Models::MessageContentType::Image;
    out.content = value.content;
    out.status = value.status.isEmpty()
        ? Models::messageStatusFromLegacySyncStatus(value.syncStatus)
        : Models::messageStatusFromString(value.status);
    out.sourceType = sourceTypeOrDefault(value.sourceType, QString());
    out.confidence = confidenceOrDefault(value.confidence, out.sourceType);
    out.verificationStatus = Models::verificationStatusFromString(value.verificationStatus);
    out.observedAt = value.observedAt.isValid() ? value.observedAt : value.createdAt;
    out.platformDisplayedAt = value.createdAt;
    out.evidenceRef = value.contentImagePath;
    out.metadata.insert(QStringLiteral("clientMessageId"), value.clientMessageId);
    return out;
}

Models::ConversationEvent toMessageObservedEvent(const PlatformMessage& value)
{
    Models::ConversationEvent out;
    out.type = QStringLiteral("message_observed");
    out.platformType = Models::platformTypeFromString(value.platform);
    out.accountId = value.platform;
    out.sourceType = sourceTypeOrDefault(value.sourceType, value.platform);
    out.confidence = confidenceOrDefault(value.confidence, out.sourceType);
    out.createdAt = value.createdAt.isValid() ? value.createdAt : QDateTime::currentDateTime();
    out.payload.insert(QStringLiteral("platformConversationId"), value.platformConversationId);
    out.payload.insert(QStringLiteral("customerName"), value.customerName);
    out.payload.insert(QStringLiteral("content"), value.content);
    out.payload.insert(QStringLiteral("direction"), value.direction);
    out.payload.insert(QStringLiteral("sender"), value.sender);
    out.payload.insert(QStringLiteral("platformMessageId"), value.platformMsgId);
    out.payload.insert(QStringLiteral("verificationStatus"), value.verificationStatus);
    return out;
}

} // namespace LegacyModelCompat
