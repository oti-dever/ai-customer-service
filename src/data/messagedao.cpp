#include "messagedao.h"
#include "database.h"
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

int MessageDao::create(int conversationId, const QString& direction,
                       const QString& content, const QString& sender,
                       const QString& platformMsgId,
                       int syncStatus,
                       const QString& errorReason,
                       const QString& senderName,
                       const QString& originalTimestamp)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("INSERT INTO messages (conversation_id, direction, content, sender, sender_name, platform_msg_id, sync_status, error_reason, original_timestamp) "
              "VALUES (:cid, :dir, :content, :sender, :sname, :pmid, :status, :reason, :ots)");
    q.bindValue(":cid", conversationId);
    q.bindValue(":dir", direction);
    q.bindValue(":content", content);
    q.bindValue(":sender", sender);
    q.bindValue(":sname", senderName);
    q.bindValue(":pmid", platformMsgId.isEmpty() ? QVariant() : platformMsgId);
    q.bindValue(":status", syncStatus);
    q.bindValue(":reason", errorReason);
    q.bindValue(":ots", originalTimestamp);

    if (!q.exec()) {
        qWarning() << "MessageDao::create 失败:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
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
    while (q.next()) {
        MessageRecord m;
        m.id = q.value("id").toInt();
        m.conversationId = q.value("conversation_id").toInt();
        m.direction = q.value("direction").toString();
        m.content = q.value("content").toString();
        m.sender = q.value("sender").toString();
        m.senderName = q.value("sender_name").toString();
        m.createdAt = messageRowCreatedAtToLocal(q.value("created_at"));
        m.platformMsgId = q.value("platform_msg_id").toString();
        m.syncStatus = q.value("sync_status").isNull() ? 1 : q.value("sync_status").toInt();
        m.errorReason = q.value("error_reason").toString();
        m.originalTimestamp = q.value("original_timestamp").toString();
        result.append(m);
    }
    return result;
}

std::optional<QString> MessageDao::latestInboundContent(int conversationId) const
{
    if (conversationId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT content FROM messages WHERE conversation_id = :cid AND direction = 'in' "
        "AND length(trim(coalesce(content, ''))) > 0 ORDER BY id DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (!q.exec()) {
        qWarning() << "MessageDao::latestInboundContent failed:" << q.lastError().text();
        return std::nullopt;
    }
    if (q.next())
        return q.value(0).toString();
    return std::nullopt;
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
    return true;
}
