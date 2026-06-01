#ifndef CONVERSATIONDAO_H
#define CONVERSATIONDAO_H

#include "../core/types.h"
#include <QVector>
#include <optional>

class ConversationDao
{
public:
    ConversationDao() = default;

    int create(const QString& platform, const QString& platformConvId, const QString& customerName);
    int create(const Models::Conversation& conversation);
    std::optional<ConversationInfo> findById(int id);
    std::optional<ConversationInfo> findByPlatformId(const QString& platform, const QString& platformConvId);
    QVector<ConversationInfo> listByStatus(const QString& status, int limit = 100, int offset = 0);
    QVector<ConversationInfo> listAll(int limit = 100, int offset = 0);
    bool updateLastMessage(int id, const QString& lastMessage, const QDateTime& lastTime);
    bool incrementUnread(int id);
    bool clearUnread(int id);
    bool setStatus(int id, const QString& status);
    QString draftForConversation(int id) const;
    bool saveDraft(int id, const QString& content);
    bool clearDraft(int id);
    int lastSelectedConversationId() const;
    bool setLastSelectedConversationId(int id);
    bool remove(int id);
};

#endif // CONVERSATIONDAO_H
