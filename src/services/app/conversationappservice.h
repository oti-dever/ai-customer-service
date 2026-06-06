#ifndef CONVERSATIONAPPSERVICE_H
#define CONVERSATIONAPPSERVICE_H

#include "../../core/types.h"
#include <QVector>
#include <optional>

struct LocalUserProfile
{
    QString username;
    QString displayName;
    QString avatarAbsolutePath;
};

class ConversationAppService
{
public:
    /** 客户端本地会话缓存，用于聚合 UI 展示。 */
    QVector<ConversationInfo> allConversations() const;
    /** 客户端本地消息缓存，用于聚合 UI 展示。 */
    QVector<MessageRecord> messages(int conversationId) const;
    std::optional<ConversationInfo> conversationById(int conversationId) const;
    bool isAggregateAutoReplyCandidate(int conversationId) const;
    LocalUserProfile loadLocalUserProfile(const QString& username) const;
};

#endif // CONVERSATIONAPPSERVICE_H
