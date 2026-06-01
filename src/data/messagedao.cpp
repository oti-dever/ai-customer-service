#include "messagedao.h"
#include "database.h"
#include "wechatmessagedao.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

/** SQLite DEFAULT CURRENT_TIMESTAMP 为 UTC；Qt 对无时区 DATETIME 常按 LocalTime 解析，会少时区偏移。 */
static QDateTime messageRowCreatedAtToLocal(const QVariant& v)
{
    const QDateTime parsed = v.toDateTime();
    if (!parsed.isValid())
        return parsed;
    return QDateTime(parsed.date(), parsed.time(), Qt::UTC).toLocalTime();
}

static MessageRecord messageRecordFromQuery(QSqlQuery& q)
{
    MessageRecord m;
    m.id = q.value(QStringLiteral("id")).toInt();
    m.conversationId = q.value(QStringLiteral("conversation_id")).toInt();
    m.direction = q.value(QStringLiteral("direction")).toString();
    m.content = q.value(QStringLiteral("content")).toString();
    m.sender = q.value(QStringLiteral("sender")).toString();
    m.senderName = q.value(QStringLiteral("sender_name")).toString();
    m.createdAt = messageRowCreatedAtToLocal(q.value(QStringLiteral("created_at")));
    m.platformMsgId = q.value(QStringLiteral("platform_msg_id")).toString();
    m.syncStatus = q.value(QStringLiteral("sync_status")).isNull() ? 1 : q.value(QStringLiteral("sync_status")).toInt();
    m.errorReason = q.value(QStringLiteral("error_reason")).toString();
    m.originalTimestamp = q.value(QStringLiteral("original_timestamp")).toString();
    m.contentImagePath = q.value(QStringLiteral("content_image_path")).toString();
    m.clientMessageId = q.value(QStringLiteral("client_message_id")).toString();
    m.sourceType = q.value(QStringLiteral("source_type")).toString();
    m.confidence = q.value(QStringLiteral("confidence")).isNull()
        ? Models::defaultConfidence(Models::sourceTypeFromString(m.sourceType))
        : q.value(QStringLiteral("confidence")).toInt();
    m.verificationStatus = q.value(QStringLiteral("verification_status")).toString();
    m.contentType = q.value(QStringLiteral("content_type")).toString();
    m.observedAt = messageRowCreatedAtToLocal(q.value(QStringLiteral("observed_at")));
    m.status = Models::toString(Models::messageStatusFromLegacySyncStatus(m.syncStatus));
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
            "original_timestamp = :ots, client_message_id = :cmid WHERE id = :id"));
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
    q.prepare("INSERT INTO messages (conversation_id, direction, content, sender, sender_name, "
              "platform_msg_id, sync_status, error_reason, original_timestamp, content_image_path, "
              "source_type, confidence, verification_status, content_type, observed_at, client_message_id) "
              "VALUES (:cid, :dir, :content, :sender, :sname, :pmid, :status, :reason, :ots, :cimg, "
              ":source, :confidence, :verification, :ctype, :observed, :cmid)");
    const QString legacyDirection = Models::legacyDirectionFromMessageDirection(message.direction);
    q.bindValue(":cid", message.conversationId);
    q.bindValue(":dir", legacyDirection);
    q.bindValue(":content", message.content);
    q.bindValue(":sender", message.direction == Models::MessageDirection::Outbound
                    ? QStringLiteral("agent")
                    : (message.direction == Models::MessageDirection::System
                           ? QStringLiteral("system")
                           : QStringLiteral("customer")));
    q.bindValue(":sname", message.metadata.value(QStringLiteral("senderName")).toString());
    q.bindValue(":pmid", message.platformMessageId.isEmpty() ? QVariant() : message.platformMessageId);
    q.bindValue(":status", Models::legacySyncStatusFromMessageStatus(message.status));
    q.bindValue(":reason", message.metadata.value(QStringLiteral("errorReason")).toString());
    q.bindValue(":ots", message.metadata.value(QStringLiteral("originalTimestamp")).toString());
    q.bindValue(":cimg", message.evidenceRef.isEmpty() ? QVariant() : message.evidenceRef);
    q.bindValue(":source", Models::toString(message.sourceType));
    q.bindValue(":confidence", message.confidence);
    q.bindValue(":verification", Models::toString(message.verificationStatus));
    q.bindValue(":ctype", Models::toString(message.contentType));
    q.bindValue(":observed", message.observedAt.isValid()
                    ? message.observedAt
                    : QDateTime::currentDateTime());
    const QString clientMessageId = message.clientMessageId.isEmpty()
        ? message.metadata.value(QStringLiteral("client_message_id")).toString()
        : message.clientMessageId;
    q.bindValue(":cmid", clientMessageId);

    if (!q.exec()) {
        qWarning() << "MessageDao::create 失败:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

std::optional<MessageRecord> MessageDao::findById(int messageId) const
{
    if (messageId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("SELECT * FROM messages WHERE id = :id"));
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
        "SELECT * FROM messages WHERE conversation_id = :cid "
        "AND direction = 'out' AND sync_status = 10 AND client_message_id = :cmid "
        "ORDER BY id DESC LIMIT 1"));
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

QVector<MessageRecord> MessageDao::listByConversation(int conversationId, int limit, int offset)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("SELECT * FROM messages WHERE conversation_id = :cid "
              "ORDER BY created_at ASC LIMIT :lim OFFSET :off");
    q.bindValue(":cid", conversationId);
    q.bindValue(":lim", limit);
    q.bindValue(":off", offset);
    q.exec();

    QVector<MessageRecord> result;
    while (q.next())
        result.append(messageRecordFromQuery(q));
    return result;
}

std::optional<MessageRecord> MessageDao::lastMessageForConversation(int conversationId) const
{
    if (conversationId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("SELECT * FROM messages WHERE conversation_id = :cid ORDER BY id DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (!q.exec()) {
        qWarning() << "MessageDao::lastMessageForConversation 失败:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;

    return messageRecordFromQuery(q);
}

std::optional<MessageRecord> MessageDao::latestPendingOutbound(int conversationId, const QString& content) const
{
    if (conversationId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    QString sql = QStringLiteral(
        "SELECT * FROM messages WHERE conversation_id = :cid "
        "AND direction = 'out' AND sync_status = 10");
    if (!content.isEmpty())
        sql += QStringLiteral(" AND content = :content");
    sql += QStringLiteral(" ORDER BY id DESC LIMIT 1");

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

bool MessageDao::updateDeliveryState(int messageId,
                                     int syncStatus,
                                     const QString& errorReason,
                                     const QString& platformMsgId)
{
    if (messageId <= 0)
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "UPDATE messages SET sync_status = :status, error_reason = :reason, "
        "platform_msg_id = COALESCE(NULLIF(:pmid, ''), platform_msg_id) "
        "WHERE id = :id"));
    q.bindValue(QStringLiteral(":status"), syncStatus);
    q.bindValue(QStringLiteral(":reason"), errorReason);
    q.bindValue(QStringLiteral(":pmid"), platformMsgId);
    q.bindValue(QStringLiteral(":id"), messageId);
    if (!q.exec()) {
        qWarning() << "MessageDao::updateDeliveryState 失败:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

std::optional<LatestInboundSnapshot> MessageDao::latestInboundSnapshot(int conversationId) const
{
    if (conversationId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT content, coalesce(content_image_path, '') FROM messages "
        "WHERE conversation_id = :cid AND direction = 'in' AND "
        "(length(trim(coalesce(content, ''))) > 0 OR length(trim(coalesce(content_image_path, ''))) > 0) "
        "ORDER BY id DESC LIMIT 1"));
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

QHash<int, QString> MessageDao::lastDirectionsByConversation() const
{
    QHash<int, QString> out;
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

void MessageDao::notifyReaderIncrementalStatePurge(const QString& platform,
                                                   const QString& platformConversationId)
{
    if (platformConversationId.isEmpty())
        return;
    QString fileName;
    if (platform == QLatin1String("wechat_pc"))
        fileName = QStringLiteral("reader_incremental_purge_wechat_pc.txt");
    else if (platform == QLatin1String("qianniu"))
        fileName = QStringLiteral("reader_incremental_purge_qianniu.txt");
    else if (platform == QLatin1String("pdd_web"))
        fileName = QStringLiteral("reader_incremental_purge_pdd_web.txt");
    else
        return;

    const QString path = QStringLiteral(PROJECT_ROOT_DIR)
        + QStringLiteral("/python/rpa/_state/") + fileName;
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "notifyReaderIncrementalStatePurge: 无法写入" << path;
        return;
    }
    QTextStream out(&f);
    out << platformConversationId << QLatin1Char('\n');
}

bool MessageDao::existsByPlatformMsgId(const QString& platformMsgId)
{
    if (platformMsgId.isEmpty())
        return false;
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("SELECT 1 FROM messages WHERE platform_msg_id = :pmid LIMIT 1");
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

    QString platformForPurge;
    QString pcidForPurge;
    q.prepare(QStringLiteral(
        "SELECT platform, platform_conversation_id FROM conversations WHERE id = :cid"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (q.exec() && q.next()) {
        platformForPurge = q.value(0).toString();
        pcidForPurge = q.value(1).toString();
        q.prepare(
            QStringLiteral("DELETE FROM rpa_inbox_messages WHERE platform = :p "
                           "AND platform_conversation_id = :pcid"));
        q.bindValue(QStringLiteral(":p"), platformForPurge);
        q.bindValue(QStringLiteral(":pcid"), pcidForPurge);
        if (!q.exec()) {
            qWarning() << "MessageDao::clearAllForConversation 删除 rpa_inbox_messages 失败:"
                       << q.lastError().text();
            db.rollback();
            return false;
        }
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
    notifyReaderIncrementalStatePurge(platformForPurge, pcidForPurge);
    if (platformForPurge == QLatin1String("wechat_pc")) {
        WechatMessageDao wechatDao;
        wechatDao.deleteForConversation(conversationId);
    }
    return true;
}
