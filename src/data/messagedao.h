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
               const QString& errorReason = QString(),
               const QString& senderName = QString(),
               const QString& originalTimestamp = QString());
    QVector<MessageRecord> listByConversation(int conversationId, int limit = 200, int offset = 0);
    bool existsByPlatformMsgId(const QString& platformMsgId);
    /** 删除该会话全部消息、message_send_events、rpa_inbox_messages 中同平台同会话行（事务内执行）。 */
    bool clearAllForConversation(int conversationId);

    /** 通知 Python Reader 丢弃该会话的增量去重状态（与 clearAll/删会话配合）。 */
    static void notifyReaderIncrementalStatePurge(const QString& platform,
                                                  const QString& platformConversationId);
};

#endif // MESSAGEDAO_H
