#ifndef MESSAGEDAO_H
#define MESSAGEDAO_H

#include "../core/types.h"
#include <QVector>

class MessageDao
{
public:
    MessageDao() = default;

    int create(int conversationId, const QString& direction,
               const QString& content, const QString& sender,
               const QString& platformMsgId = QString(),
               int syncStatus = 1,
               const QString& errorReason = QString());
    QVector<MessageRecord> listByConversation(int conversationId, int limit = 200, int offset = 0);
    bool existsByPlatformMsgId(const QString& platformMsgId);
};

#endif // MESSAGEDAO_H
