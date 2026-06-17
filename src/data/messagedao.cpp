#include "messagedao.h"
#include "database.h"
#include "qianniuconversationdao.h"
#include "wechatmessagedao.h"
#include <QDebug>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>

namespace {

QString messageCacheScope()
{
    return QStringLiteral("local_cache");
}

QString messageCacheOrigin(const Models::Message& message)
{
    if (message.status == Models::MessageStatus::Pending
        || message.status == Models::MessageStatus::Sent
        || message.status == Models::MessageStatus::Failed) {
        return QStringLiteral("manual_outbound_cache");
    }

    switch (message.sourceType) {
    case Models::SourceType::Mock:
        return QStringLiteral("simulator_runtime");
    case Models::SourceType::ManualConfirmed:
        return QStringLiteral("manual_runtime");
    case Models::SourceType::DomObserved:
    case Models::SourceType::UiObserved:
    case Models::SourceType::OcrExtracted:
    case Models::SourceType::NotificationObserved:
        return QStringLiteral("platform_observed_cache");
    case Models::SourceType::Experimental:
        return QStringLiteral("experimental_cache");
    }
    return QStringLiteral("legacy_runtime");
}

QString jsonString(const QJsonObject& object, const QString& key)
{
    return object.value(key).toString().trimmed();
}

int jsonInt(const QJsonObject& object, const QString& key, int fallback = 0)
{
    const auto value = object.value(key);
    if (value.isDouble())
        return value.toInt();
    bool ok = false;
    const int parsed = value.toString().toInt(&ok);
    return ok ? parsed : fallback;
}

QString jsonStringAny(const QJsonObject& object, const QString& primaryKey, const QString& fallbackKey)
{
    QString value = jsonString(object, primaryKey);
    if (value.isEmpty())
        value = jsonString(object, fallbackKey);
    return value;
}

QString statusFromSyncStatus(int syncStatus)
{
    switch (syncStatus) {
    case 10:
        return QStringLiteral("pending");
    case 11:
        return QStringLiteral("sent");
    case 12:
        return QStringLiteral("failed");
    case 1:
    default:
        return QStringLiteral("observed");
    }
}

int syncStatusFromStatus(const QString& status)
{
    return Models::legacySyncStatusFromMessageStatus(Models::messageStatusFromString(status));
}

QVariant rowValue(const QSqlQuery& q, const QString& name)
{
    const int idx = q.record().indexOf(name);
    if (idx < 0)
        return QVariant();
    return q.value(idx);
}

QString rowString(const QSqlQuery& q, const QString& name, const QString& fallback = QString())
{
    const QVariant value = rowValue(q, name);
    return value.isValid() && !value.isNull() ? value.toString() : fallback;
}

int rowInt(const QSqlQuery& q, const QString& name, int fallback = 0)
{
    const QVariant value = rowValue(q, name);
    return value.isValid() && !value.isNull() ? value.toInt() : fallback;
}

QDateTime rowDateTime(const QSqlQuery& q, const QString& name)
{
    return rowValue(q, name).toDateTime();
}

int existingSnapshotMessageId(int conversationId,
                              const QString& platformMsgId,
                              const QString& clientMessageId)
{
    if (conversationId <= 0)
        return 0;

    QSqlQuery q(Database::getInstance().connection());
    if (!platformMsgId.isEmpty()) {
        q.prepare(QStringLiteral(
            "SELECT id FROM messages WHERE conversation_id = :cid "
            "AND platform_message_id = :pmid ORDER BY id DESC LIMIT 1"));
        q.bindValue(QStringLiteral(":cid"), conversationId);
        q.bindValue(QStringLiteral(":pmid"), platformMsgId);
    } else if (!clientMessageId.isEmpty()) {
        q.prepare(QStringLiteral(
            "SELECT id FROM messages WHERE conversation_id = :cid "
            "AND client_message_id = :cmid ORDER BY id DESC LIMIT 1"));
        q.bindValue(QStringLiteral(":cid"), conversationId);
        q.bindValue(QStringLiteral(":cmid"), clientMessageId);
    } else {
        return 0;
    }

    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toInt();
}

QString placeholders(const QString& prefix, int count)
{
    QStringList items;
    items.reserve(count);
    for (int i = 0; i < count; ++i)
        items.append(QStringLiteral(":%1%2").arg(prefix).arg(i));
    return items.join(QStringLiteral(", "));
}

QString messageSelectProjection()
{
    return QStringLiteral(
        "SELECT m.*, "
        "COALESCE(wm.original_timestamp, qm.original_timestamp, '') AS original_timestamp, "
        "COALESCE(NULLIF(wm.content_image_path, ''), NULLIF(qm.content_image_path, ''), "
        "NULLIF(wm.evidence_ref, ''), NULLIF(qm.evidence_ref, ''), '') AS content_image_path, "
        "COALESCE(wm.source_type, qm.source_type, '') AS source_type, "
        "COALESCE(wm.confidence, qm.confidence) AS confidence, "
        "COALESCE(wm.verification_status, qm.verification_status, '') AS verification_status "
        "FROM messages m "
        "LEFT JOIN wechat_messages wm ON wm.message_id = m.id "
        "LEFT JOIN qianniu_messages qm ON qm.message_id = m.id ");
}

bool tableExists(const QString& tableName)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = :name LIMIT 1"));
    q.bindValue(QStringLiteral(":name"), tableName);
    return q.exec() && q.next();
}

} // namespace

static MessageRecord messageRecordFromQuery(QSqlQuery& q)
{
    MessageRecord m;
    m.id = q.value(QStringLiteral("id")).toInt();
    m.conversationId = q.value(QStringLiteral("conversation_id")).toInt();
    m.direction = q.value(QStringLiteral("direction")).toString();
    m.content = q.value(QStringLiteral("content")).toString();
    m.sender = q.value(QStringLiteral("sender")).toString();
    m.senderName = rowString(q, QStringLiteral("sender_name"));
    m.createdAt = rowDateTime(q, QStringLiteral("created_at"));
    m.platformMsgId = rowString(q, QStringLiteral("platform_message_id"));
    if (m.platformMsgId.isEmpty())
        m.platformMsgId = rowString(q, QStringLiteral("platform_msg_id"));
    m.status = rowString(q, QStringLiteral("status"));
    if (m.status.isEmpty()) {
        m.syncStatus = rowInt(q, QStringLiteral("sync_status"), 1);
        m.status = Models::toString(Models::messageStatusFromLegacySyncStatus(m.syncStatus));
    } else {
        m.syncStatus = syncStatusFromStatus(m.status);
    }
    m.errorReason = rowString(q, QStringLiteral("error_reason"));
    m.originalTimestamp = rowString(q, QStringLiteral("original_timestamp"));
    m.contentImagePath = rowString(q, QStringLiteral("content_image_path"));
    m.clientMessageId = rowString(q, QStringLiteral("client_message_id"));
    m.sourceType = rowString(q, QStringLiteral("source_type"));
    m.confidence = rowValue(q, QStringLiteral("confidence")).isNull()
        ? Models::defaultConfidence(Models::sourceTypeFromString(m.sourceType))
        : rowInt(q, QStringLiteral("confidence"));
    m.verificationStatus = rowString(q, QStringLiteral("verification_status"));
    m.contentType = rowString(q, QStringLiteral("content_type"), QStringLiteral("text"));
    m.observedAt = rowDateTime(q, QStringLiteral("message_time"));
    if (!m.observedAt.isValid())
        m.observedAt = rowDateTime(q, QStringLiteral("observed_at"));
    m.cacheScope = rowString(q, QStringLiteral("cache_scope"));
    if (m.cacheScope.isEmpty())
        m.cacheScope = QStringLiteral("local_cache");
    m.cacheOrigin = rowString(q, QStringLiteral("cache_origin"));
    if (m.cacheOrigin.isEmpty())
        m.cacheOrigin = QStringLiteral("legacy_runtime");
    return m;
}

int MessageDao::create(int conversationId, const QString& direction,
                       const QString& content, const QString& sender,
                       const QString& platformMsgId,
                       int syncStatus,
                       const QString& errorReason,
                       const QString& senderName,
                       const QString& originalTimestamp,
                       const QString& contentImagePath,
                       const QString& clientMessageId)
{
    Models::Message message;
    message.conversationId = conversationId;
    message.direction = Models::messageDirectionFromLegacy(direction, sender);
    message.contentType = contentImagePath.isEmpty()
        ? Models::MessageContentType::Text
        : Models::MessageContentType::Image;
    message.content = content;
    message.status = Models::messageStatusFromLegacySyncStatus(syncStatus);
    message.platformMessageId = platformMsgId;
    message.sourceType = Models::SourceType::Mock;
    message.confidence = Models::defaultConfidence(message.sourceType);
    message.verificationStatus = Models::VerificationStatus::Unverified;
    message.observedAt = QDateTime::currentDateTime();
    message.evidenceRef = contentImagePath;
    message.metadata.insert(QStringLiteral("client_message_id"), clientMessageId);
    message.clientMessageId = clientMessageId;

    const int id = create(message);
    if (id <= 0)
        return id;

    if (!errorReason.isEmpty() || !senderName.isEmpty() || !originalTimestamp.isEmpty() || !clientMessageId.isEmpty()) {
        QSqlQuery q(Database::getInstance().connection());
        q.prepare(QStringLiteral(
            "UPDATE messages SET error_reason = :reason, sender_name = :sname, "
            "message_time = COALESCE(NULLIF(:ots, ''), message_time), "
            "client_message_id = :cmid, updated_at = datetime('now','localtime') WHERE id = :id"));
        q.bindValue(QStringLiteral(":reason"), errorReason);
        q.bindValue(QStringLiteral(":sname"), senderName);
        q.bindValue(QStringLiteral(":ots"), originalTimestamp);
        q.bindValue(QStringLiteral(":cmid"), clientMessageId);
        q.bindValue(QStringLiteral(":id"), id);
        if (!q.exec())
            qWarning() << "MessageDao::create legacy metadata update 失败:" << q.lastError().text();
    }
    return id;
}

int MessageDao::create(const Models::Message& message)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("INSERT INTO messages (conversation_id, platform_message_id, client_message_id, "
              "direction, sender, sender_name, content_type, content, status, error_reason, "
              "message_time, cache_scope, cache_origin, created_at, updated_at) "
              "VALUES (:cid, :pmid, :cmid, :dir, :sender, :sname, :ctype, :content, :status, "
              ":reason, :message_time, :cache_scope, :cache_origin, "
              "COALESCE(:message_time, datetime('now','localtime')), "
              "COALESCE(:message_time, datetime('now','localtime')))");
    const QString legacyDirection = Models::legacyDirectionFromMessageDirection(message.direction);
    const QString clientMessageId = message.clientMessageId.isEmpty()
        ? message.metadata.value(QStringLiteral("client_message_id")).toString()
        : message.clientMessageId;
    const QDateTime messageTime = message.observedAt.isValid()
        ? message.observedAt
        : QDateTime::currentDateTime();
    q.bindValue(":cid", message.conversationId);
    q.bindValue(":pmid", message.platformMessageId.isEmpty() ? QVariant() : message.platformMessageId);
    q.bindValue(":cmid", clientMessageId);
    q.bindValue(":dir", legacyDirection);
    q.bindValue(":sender", message.direction == Models::MessageDirection::Outbound
                    ? QStringLiteral("agent")
                    : (message.direction == Models::MessageDirection::System
                           ? QStringLiteral("system")
                           : QStringLiteral("customer")));
    q.bindValue(":sname", message.metadata.value(QStringLiteral("senderName")).toString());
    q.bindValue(":ctype", Models::toString(message.contentType));
    q.bindValue(":content", message.content);
    q.bindValue(":status", Models::toString(message.status));
    q.bindValue(":reason", message.metadata.value(QStringLiteral("errorReason")).toString());
    q.bindValue(":message_time", messageTime);
    q.bindValue(":cache_scope", messageCacheScope());
    q.bindValue(":cache_origin", messageCacheOrigin(message));

    if (!q.exec()) {
        qWarning() << "MessageDao::create 失败:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

int MessageDao::createObservedCacheMessage(const Models::Message& message)
{
    Models::Message cacheMessage = message;
    cacheMessage.status = Models::MessageStatus::Observed;
    if (!cacheMessage.observedAt.isValid())
        cacheMessage.observedAt = QDateTime::currentDateTime();
    return create(cacheMessage);
}

int MessageDao::createOutboundCacheMessage(const Models::Message& message)
{
    Models::Message cacheMessage = message;
    cacheMessage.direction = Models::MessageDirection::Outbound;
    if (cacheMessage.status != Models::MessageStatus::Sent
        && cacheMessage.status != Models::MessageStatus::Failed) {
        cacheMessage.status = Models::MessageStatus::Pending;
    }
    if (!cacheMessage.observedAt.isValid())
        cacheMessage.observedAt = QDateTime::currentDateTime();
    return create(cacheMessage);
}

int MessageDao::upsertSnapshotCacheMessage(int conversationId, const QJsonObject& message)
{
    if (conversationId <= 0)
        return -1;

    const QString platformMsgId = jsonStringAny(
        message,
        QStringLiteral("platform_message_id"),
        QStringLiteral("platform_msg_id"));
    const QString clientMessageId = jsonString(message, QStringLiteral("client_message_id"));
    if (platformMsgId.isEmpty() && clientMessageId.isEmpty())
        return -1;

    const int existingId = existingSnapshotMessageId(conversationId, platformMsgId, clientMessageId);
    const QString direction = jsonString(message, QStringLiteral("direction")).isEmpty()
        ? QStringLiteral("in")
        : jsonString(message, QStringLiteral("direction"));
    const QString sender = jsonString(message, QStringLiteral("sender")).isEmpty()
        ? (direction == QLatin1String("out") ? QStringLiteral("agent") : QStringLiteral("customer"))
        : jsonString(message, QStringLiteral("sender"));
    const QString content = message.value(QStringLiteral("content")).toString();
    const QString senderName = jsonString(message, QStringLiteral("sender_name"));
    QString status = jsonString(message, QStringLiteral("status"));
    if (status.isEmpty())
        status = statusFromSyncStatus(jsonInt(message, QStringLiteral("sync_status"), 1));
    const QString errorReason = message.value(QStringLiteral("error_reason")).toString();
    const QString contentType = jsonString(message, QStringLiteral("content_type")).isEmpty()
        ? QStringLiteral("text")
        : jsonString(message, QStringLiteral("content_type"));
    const QString messageTime = jsonStringAny(
        message,
        QStringLiteral("message_time"),
        QStringLiteral("observed_at"));
    const QString createdAt = jsonString(message, QStringLiteral("created_at"));
    const QString updatedAt = jsonString(message, QStringLiteral("updated_at"));

    QSqlQuery q(Database::getInstance().connection());
    if (existingId > 0) {
        q.prepare(QStringLiteral(
            "UPDATE messages SET "
            "direction = :direction, "
            "content = :content, "
            "sender = :sender, "
            "sender_name = :sender_name, "
            "platform_message_id = COALESCE(NULLIF(:pmid, ''), platform_message_id), "
            "status = :status, "
            "error_reason = :error_reason, "
            "content_type = :content_type, "
            "message_time = COALESCE(NULLIF(:message_time, ''), message_time), "
            "client_message_id = COALESCE(NULLIF(:cmid, ''), client_message_id), "
            "cache_scope = 'local_cache', "
            "cache_origin = 'server_snapshot_cache', "
            "updated_at = COALESCE(NULLIF(:updated_at, ''), datetime('now','localtime')) "
            "WHERE id = :id"));
        q.bindValue(QStringLiteral(":id"), existingId);
    } else {
        q.prepare(QStringLiteral(
            "INSERT INTO messages (conversation_id, platform_message_id, client_message_id, "
            "direction, sender, sender_name, content_type, content, status, error_reason, "
            "message_time, cache_scope, cache_origin, created_at, updated_at) "
            "VALUES (:cid, :pmid, :cmid, :direction, :sender, :sender_name, :content_type, "
            ":content, :status, :error_reason, "
            "COALESCE(NULLIF(:message_time, ''), datetime('now','localtime')), "
            "'local_cache', 'server_snapshot_cache', "
            "COALESCE(NULLIF(:created_at, ''), datetime('now','localtime')), "
            "COALESCE(NULLIF(:updated_at, ''), COALESCE(NULLIF(:created_at, ''), datetime('now','localtime'))))"));
        q.bindValue(QStringLiteral(":cid"), conversationId);
        q.bindValue(QStringLiteral(":created_at"), createdAt);
    }

    q.bindValue(QStringLiteral(":direction"), direction);
    q.bindValue(QStringLiteral(":content"), content);
    q.bindValue(QStringLiteral(":sender"), sender);
    q.bindValue(QStringLiteral(":sender_name"), senderName);
    q.bindValue(QStringLiteral(":pmid"), platformMsgId);
    q.bindValue(QStringLiteral(":status"), status);
    q.bindValue(QStringLiteral(":error_reason"), errorReason);
    q.bindValue(QStringLiteral(":content_type"), contentType);
    q.bindValue(QStringLiteral(":message_time"), messageTime);
    q.bindValue(QStringLiteral(":cmid"), clientMessageId);
    q.bindValue(QStringLiteral(":updated_at"), updatedAt);

    if (!q.exec()) {
        qWarning() << "MessageDao::upsertSnapshotCacheMessage 失败:" << q.lastError().text();
        return -1;
    }
    return existingId > 0 ? existingId : q.lastInsertId().toInt();
}

int MessageDao::deleteMissingSnapshotCacheMessages(
    int conversationId,
    const QSet<QString>& keepPlatformMessageIds,
    const QSet<QString>& keepClientMessageIds)
{
    if (conversationId <= 0)
        return 0;

    QString keepPredicate;
    if (!keepPlatformMessageIds.isEmpty())
        keepPredicate += QStringLiteral("platform_message_id IN (%1)")
                             .arg(placeholders(QStringLiteral("pmid"), keepPlatformMessageIds.size()));
    if (!keepClientMessageIds.isEmpty()) {
        if (!keepPredicate.isEmpty())
            keepPredicate += QStringLiteral(" OR ");
        keepPredicate += QStringLiteral("client_message_id IN (%1)")
                             .arg(placeholders(QStringLiteral("cmid"), keepClientMessageIds.size()));
    }

    QString sql = QStringLiteral(
        "DELETE FROM messages WHERE conversation_id = :cid "
        "AND cache_origin = 'server_snapshot_cache'");
    if (!keepPredicate.isEmpty())
        sql += QStringLiteral(" AND NOT (%1)").arg(keepPredicate);

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(sql);
    q.bindValue(QStringLiteral(":cid"), conversationId);
    int i = 0;
    for (const QString& id : keepPlatformMessageIds) {
        q.bindValue(QStringLiteral(":pmid%1").arg(i), id);
        ++i;
    }
    i = 0;
    for (const QString& id : keepClientMessageIds) {
        q.bindValue(QStringLiteral(":cmid%1").arg(i), id);
        ++i;
    }
    if (!q.exec()) {
        qWarning() << "MessageDao::deleteMissingSnapshotCacheMessages 失败:"
                   << q.lastError().text();
        return 0;
    }
    return q.numRowsAffected();
}

std::optional<MessageRecord> MessageDao::findById(int messageId) const
{
    if (messageId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(messageSelectProjection() + QStringLiteral("WHERE m.id = :id"));
    q.bindValue(QStringLiteral(":id"), messageId);
    if (!q.exec()) {
        qWarning() << "MessageDao::findById 失败:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return messageRecordFromQuery(q);
}

std::optional<MessageRecord> MessageDao::latestPendingOutboundByClientMessageId(int conversationId,
                                                                                const QString& clientMessageId) const
{
    if (conversationId <= 0 || clientMessageId.isEmpty())
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "%1WHERE m.conversation_id = :cid "
        "AND m.direction = 'out' AND m.status = 'pending' AND m.client_message_id = :cmid "
        "ORDER BY m.id DESC LIMIT 1").arg(messageSelectProjection()));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    q.bindValue(QStringLiteral(":cmid"), clientMessageId);
    if (!q.exec()) {
        qWarning() << "MessageDao::latestPendingOutboundByClientMessageId 失败:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return messageRecordFromQuery(q);
}

std::optional<MessageRecord> MessageDao::latestOutboundByClientMessageId(int conversationId,
                                                                         const QString& clientMessageId) const
{
    if (conversationId <= 0 || clientMessageId.isEmpty())
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "%1WHERE m.conversation_id = :cid "
        "AND m.direction = 'out' AND m.client_message_id = :cmid "
        "ORDER BY m.id DESC LIMIT 1").arg(messageSelectProjection()));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    q.bindValue(QStringLiteral(":cmid"), clientMessageId);
    if (!q.exec()) {
        qWarning() << "MessageDao::latestOutboundByClientMessageId failed:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return messageRecordFromQuery(q);
}

QVector<MessageRecord> MessageDao::listByConversation(int conversationId, int limit, int offset)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(messageSelectProjection()
              + QStringLiteral("WHERE m.conversation_id = :cid "
                               "ORDER BY m.created_at ASC LIMIT :lim OFFSET :off"));
    q.bindValue(":cid", conversationId);
    q.bindValue(":lim", limit);
    q.bindValue(":off", offset);
    q.exec();

    QVector<MessageRecord> result;
    while (q.next())
        result.append(messageRecordFromQuery(q));
    return result;
}

QVector<MessageRecord> MessageDao::listCachedMessages(int conversationId, int limit, int offset)
{
    return listByConversation(conversationId, limit, offset);
}

std::optional<MessageRecord> MessageDao::lastMessageForConversation(int conversationId) const
{
    if (conversationId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(messageSelectProjection()
              + QStringLiteral("WHERE m.conversation_id = :cid ORDER BY m.id DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (!q.exec()) {
        qWarning() << "MessageDao::lastMessageForConversation 失败:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;

    return messageRecordFromQuery(q);
}

std::optional<MessageRecord> MessageDao::lastCachedMessageForConversation(int conversationId) const
{
    return lastMessageForConversation(conversationId);
}

std::optional<MessageRecord> MessageDao::latestPendingOutbound(int conversationId, const QString& content) const
{
    if (conversationId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    QString sql = messageSelectProjection()
        + QStringLiteral("WHERE m.conversation_id = :cid "
                         "AND m.direction = 'out' AND m.status = 'pending'");
    if (!content.isEmpty())
        sql += QStringLiteral(" AND m.content = :content");
    sql += QStringLiteral(" ORDER BY m.id DESC LIMIT 1");

    q.prepare(sql);
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (!content.isEmpty())
        q.bindValue(QStringLiteral(":content"), content);
    if (!q.exec()) {
        qWarning() << "MessageDao::latestPendingOutbound 失败:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return messageRecordFromQuery(q);
}

std::optional<MessageRecord> MessageDao::latestPendingOutboundCache(int conversationId,
                                                                    const QString& content) const
{
    return latestPendingOutbound(conversationId, content);
}

std::optional<MessageRecord> MessageDao::latestPendingOutboundCacheByClientMessageId(
    int conversationId,
    const QString& clientMessageId) const
{
    return latestPendingOutboundByClientMessageId(conversationId, clientMessageId);
}

std::optional<MessageRecord> MessageDao::latestOutboundCacheByClientMessageId(
    int conversationId,
    const QString& clientMessageId) const
{
    return latestOutboundByClientMessageId(conversationId, clientMessageId);
}

bool MessageDao::updateDeliveryState(int messageId,
                                     int syncStatus,
                                     const QString& errorReason,
                                     const QString& platformMsgId)
{
    if (messageId <= 0)
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "UPDATE messages SET status = :status, error_reason = :reason, "
        "platform_message_id = COALESCE(NULLIF(:pmid, ''), platform_message_id), "
        "updated_at = datetime('now','localtime') "
        "WHERE id = :id"));
    q.bindValue(QStringLiteral(":status"), statusFromSyncStatus(syncStatus));
    q.bindValue(QStringLiteral(":reason"), errorReason);
    q.bindValue(QStringLiteral(":pmid"), platformMsgId);
    q.bindValue(QStringLiteral(":id"), messageId);
    if (!q.exec()) {
        qWarning() << "MessageDao::updateDeliveryState 失败:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool MessageDao::updateOutboundCacheDeliveryState(int messageId,
                                                  int syncStatus,
                                                  const QString& errorReason,
                                                  const QString& platformMsgId)
{
    return updateDeliveryState(messageId, syncStatus, errorReason, platformMsgId);
}

std::optional<LatestInboundSnapshot> MessageDao::latestInboundSnapshot(int conversationId) const
{
    if (conversationId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT m.content, coalesce(wm.evidence_ref, qm.evidence_ref, '') "
        "FROM messages m "
        "LEFT JOIN wechat_messages wm ON wm.message_id = m.id "
        "LEFT JOIN qianniu_messages qm ON qm.message_id = m.id "
        "WHERE m.conversation_id = :cid AND m.direction = 'in' AND "
        "(length(trim(coalesce(m.content, ''))) > 0 "
        " OR length(trim(coalesce(wm.evidence_ref, qm.evidence_ref, ''))) > 0) "
        "ORDER BY m.id DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (!q.exec()) {
        qWarning() << "MessageDao::latestInboundSnapshot failed:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    LatestInboundSnapshot s;
    s.content = q.value(0).toString();
    s.contentImagePath = q.value(1).toString().trimmed();
    return s;
}

std::optional<QString> MessageDao::latestInboundContent(int conversationId) const
{
    const auto s = latestInboundSnapshot(conversationId);
    if (!s)
        return std::nullopt;
    if (s->content.trimmed().isEmpty())
        return std::nullopt;
    return s->content;
}

std::optional<QString> MessageDao::latestCachedInboundContent(int conversationId) const
{
    return latestInboundContent(conversationId);
}

std::optional<LatestInboundSnapshot> MessageDao::latestCachedInboundSnapshot(int conversationId) const
{
    return latestInboundSnapshot(conversationId);
}

QHash<int, QString> MessageDao::lastDirectionsByConversation() const
{
    QHash<int, QString> out;
    if (!tableExists(QStringLiteral("messages")))
        return out;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT conversation_id, direction FROM messages WHERE id IN "
        "(SELECT MAX(id) FROM messages GROUP BY conversation_id)"));
    if (!q.exec()) {
        qWarning() << "MessageDao::lastDirectionsByConversation 失败:" << q.lastError().text();
        return out;
    }
    while (q.next())
        out.insert(q.value(0).toInt(), q.value(1).toString());
    return out;
}

QHash<int, QString> MessageDao::lastCachedDirectionsByConversation() const
{
    return lastDirectionsByConversation();
}

bool MessageDao::existsByPlatformMsgId(const QString& platformMsgId)
{
    if (platformMsgId.isEmpty())
        return false;
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("SELECT 1 FROM messages WHERE platform_message_id = :pmid LIMIT 1");
    q.bindValue(":pmid", platformMsgId);
    return q.exec() && q.next();
}

bool MessageDao::clearAllForConversation(int conversationId)
{
    if (conversationId <= 0)
        return false;
    QSqlDatabase db = Database::getInstance().connection();
    if (!db.transaction()) {
        qWarning() << "MessageDao::clearAllForConversation: 无法开启事务";
        return false;
    }
    QSqlQuery q(db);

    QString platform;
    q.prepare(QStringLiteral(
        "SELECT platform FROM conversations WHERE id = :cid"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (q.exec() && q.next()) {
        platform = q.value(0).toString();
    }

    q.prepare(QStringLiteral("DELETE FROM message_send_events WHERE conversation_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (!q.exec()) {
        qWarning() << "MessageDao::clearAllForConversation 删除 send_events 失败:"
                   << q.lastError().text();
        db.rollback();
        return false;
    }
    q.prepare(QStringLiteral("DELETE FROM messages WHERE conversation_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (!q.exec()) {
        qWarning() << "MessageDao::clearAllForConversation 删除 messages 失败:"
                   << q.lastError().text();
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        qWarning() << "MessageDao::clearAllForConversation: commit 失败";
        db.rollback();
        return false;
    }
    if (platform == QLatin1String("wechat")) {
        WechatMessageDao wechatDao;
        wechatDao.deleteForConversation(conversationId);
    } else if (platform == QLatin1String("qianniu")) {
        QianniuConversationDao qianniuDao;
        qianniuDao.deleteForConversation(conversationId);
    }
    return true;
}
