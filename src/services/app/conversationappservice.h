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
    QVector<ConversationInfo> allConversations() const;
    QVector<MessageRecord> messages(int conversationId) const;
    std::optional<ConversationInfo> conversationById(int conversationId) const;
    bool isAggregateAutoReplyCandidate(int conversationId) const;
    LocalUserProfile loadLocalUserProfile(const QString& username) const;
};

#endif // CONVERSATIONAPPSERVICE_H
