#ifndef CONVERSATIONDAO_H
#define CONVERSATIONDAO_H

#include "../core/types.h"
#include <QSet>
#include <QVector>
#include <optional>

class QJsonObject;

class ConversationDao
{
public:
    ConversationDao() = default;

    int create(const QString& platform, const QString& platformConvId, const QString& customerName);
    int create(const Models::Conversation& conversation);
    int upsertObservedCacheConversation(const Models::Conversation& conversation);
    int upsertSnapshotCacheConversation(const QJsonObject& conversation);
    int deleteMissingSnapshotCacheConversations(const QString& platform,
                                                const QSet<QString>& keepPlatformConversationIds);
    std::optional<ConversationInfo> findById(int id);
    std::optional<ConversationInfo> findByPlatformId(const QString& platform, const QString& platformConvId);
    QVector<ConversationInfo> listByStatus(const QString& status, int limit = 100, int offset = 0);
    QVector<ConversationInfo> listAll(int limit = 100, int offset = 0);
    /** 读取客户端本地会话缓存；用于 UI 恢复/展示，不代表服务端真相源。 */
    QVector<ConversationInfo> listCachedConversations(int limit = 100, int offset = 0);
    bool updateDisplayName(int id, const QString& customerName);
    bool updateLastMessage(int id, const QString& lastMessage, const QDateTime& lastTime);
    bool incrementUnread(int id);
    bool clearUnread(int id);
    bool setStatus(int id, const QString& status);
    QString draftForConversation(int id) const;
    bool saveDraft(int id, const QString& content);
    bool clearDraft(int id);
    int lastSelectedConversationId() const;
    bool setLastSelectedConversationId(int id);
    /** 客户端本地草稿缓存；用于 UI 恢复，不代表服务端真相源。 */
    QString cachedDraftForConversation(int id) const;
    bool saveCachedDraft(int id, const QString& content);
    bool clearCachedDraft(int id);
    /** 客户端本地最近会话游标；用于 UI 恢复。 */
    int lastSelectedCachedConversationId() const;
    bool setLastSelectedCachedConversationId(int id);
    QString snapshotCursor(const QString& platform) const;
    bool setSnapshotCursor(const QString& platform, const QString& cursor);
    QString rpaReplayCursor(const QString& platform) const;
    bool setRpaReplayCursor(const QString& platform, const QString& cursor);
    bool remove(int id);
};

#endif // CONVERSATIONDAO_H
