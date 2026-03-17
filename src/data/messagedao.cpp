#include "messagedao.h"
#include "database.h"
#include <QSqlError>
#include <QSqlQuery>

int MessageDao::create(int conversationId, const QString& direction,
                       const QString& content, const QString& sender,
                       const QString& platformMsgId,
                       int syncStatus,
                       const QString& errorReason)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("INSERT INTO messages (conversation_id, direction, content, sender, platform_msg_id, sync_status, error_reason) "
              "VALUES (:cid, :dir, :content, :sender, :pmid, :status, :reason)");
    q.bindValue(":cid", conversationId);
    q.bindValue(":dir", direction);
    q.bindValue(":content", content);
    q.bindValue(":sender", sender);
    q.bindValue(":pmid", platformMsgId.isEmpty() ? QVariant() : platformMsgId);
    q.bindValue(":status", syncStatus);
    q.bindValue(":reason", errorReason);

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
        m.createdAt = q.value("created_at").toDateTime();
        m.platformMsgId = q.value("platform_msg_id").toString();
        m.syncStatus = q.value("sync_status").isNull() ? 1 : q.value("sync_status").toInt();
        m.errorReason = q.value("error_reason").toString();
        result.append(m);
    }
    return result;
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
