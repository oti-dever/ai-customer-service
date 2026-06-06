#ifndef MESSAGEDAO_H
#define MESSAGEDAO_H

#include "../core/types.h"
#include <QHash>
#include <QSet>
#include <QVector>
#include <optional>
#include "../models/unifiedmodels.h"

class QJsonObject;

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
    int createObservedCacheMessage(const Models::Message& message);
    /** 写入客户端本地出站消息缓存；用于 UI 乐观展示和 sidecar 回执关联。 */
    int createOutboundCacheMessage(const Models::Message& message);
    int upsertSnapshotCacheMessage(int conversationId, const QJsonObject& message);
    int deleteMissingSnapshotCacheMessages(int conversationId,
                                           const QSet<QString>& keepPlatformMessageIds,
                                           const QSet<QString>& keepClientMessageIds);
    std::optional<MessageRecord> findById(int messageId) const;
    QVector<MessageRecord> listByConversation(int conversationId, int limit = 200, int offset = 0);
    /** 读取客户端本地消息缓存；用于 UI 恢复/展示，不代表服务端真相源。 */
    QVector<MessageRecord> listCachedMessages(int conversationId, int limit = 200, int offset = 0);
    /** 按 `messages.id` 最大的一条（当前会话时间线上的最后一条），无消息则 `nullopt`。 */
    std::optional<MessageRecord> lastMessageForConversation(int conversationId) const;
    /** 读取客户端本地缓存中的最后一条消息；用于 UI/应用服务判断，不代表服务端真相源。 */
    std::optional<MessageRecord> lastCachedMessageForConversation(int conversationId) const;
    /** 当前会话最后一条待发送出站消息；若提供文本则优先按内容匹配。 */
    std::optional<MessageRecord> latestPendingOutbound(int conversationId,
                                                       const QString& content = QString()) const;
    std::optional<MessageRecord> latestPendingOutboundByClientMessageId(int conversationId,
                                                                        const QString& clientMessageId) const;
    std::optional<MessageRecord> latestOutboundByClientMessageId(int conversationId,
                                                                 const QString& clientMessageId) const;
    /** 读取客户端本地待确认出站缓存；用于 sidecar 回执关联。 */
    std::optional<MessageRecord> latestPendingOutboundCache(int conversationId,
                                                           const QString& content = QString()) const;
    std::optional<MessageRecord> latestPendingOutboundCacheByClientMessageId(int conversationId,
                                                                             const QString& clientMessageId) const;
    std::optional<MessageRecord> latestOutboundCacheByClientMessageId(int conversationId,
                                                                      const QString& clientMessageId) const;
    bool updateDeliveryState(int messageId,
                             int syncStatus,
                             const QString& errorReason = QString(),
                             const QString& platformMsgId = QString());
    /** 更新客户端本地出站缓存投递状态；服务端真相回灌接管前的 UI 状态缓存。 */
    bool updateOutboundCacheDeliveryState(int messageId,
                                          int syncStatus,
                                          const QString& errorReason = QString(),
                                          const QString& platformMsgId = QString());
    /** 当前会话时间序上最后一条非空入站文本（direction=`in`），无则 `nullopt`。 */
    std::optional<QString> latestInboundContent(int conversationId) const;
    std::optional<QString> latestCachedInboundContent(int conversationId) const;
    /** 最后一条入站：文本与/或聊天区截图路径（用于聚合 AI 多模态）。 */
    std::optional<LatestInboundSnapshot> latestInboundSnapshot(int conversationId) const;
    std::optional<LatestInboundSnapshot> latestCachedInboundSnapshot(int conversationId) const;
    /** 各会话当前最后一条消息的 direction（按 messages.id 最大）；无消息则无键。 */
    QHash<int, QString> lastDirectionsByConversation() const;
    /** 各会话本地缓存最后一条消息的 direction；用于会话列表恢复/分栏。 */
    QHash<int, QString> lastCachedDirectionsByConversation() const;
    bool existsByPlatformMsgId(const QString& platformMsgId);
    /** 删除该会话全部消息和 message_send_events（事务内执行）。 */
    bool clearAllForConversation(int conversationId);
};

#endif // MESSAGEDAO_H
