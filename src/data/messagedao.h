#ifndef MESSAGEDAO_H
#define MESSAGEDAO_H

#include "../core/types.h"
#include <QHash>
#include <QVector>
#include <optional>
#include "../models/unifiedmodels.h"

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
               const QString& originalTimestamp = QString(),
               const QString& contentImagePath = QString(),
               const QString& clientMessageId = QString());
    int create(const Models::Message& message);
    std::optional<MessageRecord> findById(int messageId) const;
    QVector<MessageRecord> listByConversation(int conversationId, int limit = 200, int offset = 0);
    /** 按 `messages.id` 最大的一条（当前会话时间线上的最后一条），无消息则 `nullopt`。 */
    std::optional<MessageRecord> lastMessageForConversation(int conversationId) const;
    /** 当前会话最后一条待发送出站消息；若提供文本则优先按内容匹配。 */
    std::optional<MessageRecord> latestPendingOutbound(int conversationId,
                                                       const QString& content = QString()) const;
    std::optional<MessageRecord> latestPendingOutboundByClientMessageId(int conversationId,
                                                                        const QString& clientMessageId) const;
    bool updateDeliveryState(int messageId,
                             int syncStatus,
                             const QString& errorReason = QString(),
                             const QString& platformMsgId = QString());
    /** 当前会话时间序上最后一条非空入站文本（direction=`in`），无则 `nullopt`。 */
    std::optional<QString> latestInboundContent(int conversationId) const;
    /** 最后一条入站：文本与/或聊天区截图路径（用于聚合 AI 多模态）。 */
    std::optional<LatestInboundSnapshot> latestInboundSnapshot(int conversationId) const;
    /** 各会话当前最后一条消息的 direction（按 messages.id 最大）；无消息则无键。 */
    QHash<int, QString> lastDirectionsByConversation() const;
    bool existsByPlatformMsgId(const QString& platformMsgId);
    /** 删除该会话全部消息、message_send_events、rpa_inbox_messages 中同平台同会话行（事务内执行）。 */
    bool clearAllForConversation(int conversationId);

    /** 通知 Python Reader 丢弃该会话的增量去重状态（与 clearAll/删会话配合）。 */
    static void notifyReaderIncrementalStatePurge(const QString& platform,
                                                  const QString& platformConversationId);
};

#endif // MESSAGEDAO_H
