#include "messagesendeventdao.h"
#include "database.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

qint64 MessageSendEventDao::globalMaxId() const
{
    QSqlQuery q(Database::getInstance().connection());
    if (!q.exec(QStringLiteral("SELECT COALESCE(MAX(id), 0) FROM message_send_events")) || !q.next())
        return 0;
    return q.value(0).toLongLong();
}

QVector<MessageSendEventRecord> MessageSendEventDao::listSince(int conversationId, qint64 afterId, int limit) const
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(
        QStringLiteral("SELECT id, message_id, conversation_id, phase, detail, created_at "
                       "FROM message_send_events "
                       "WHERE conversation_id = :cid AND id > :after "
                       "ORDER BY id ASC LIMIT :lim"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    q.bindValue(QStringLiteral(":after"), afterId);
    q.bindValue(QStringLiteral(":lim"), limit);
    QVector<MessageSendEventRecord> out;
    if (!q.exec()) {
        qWarning() << "MessageSendEventDao::listSince failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        MessageSendEventRecord r;
        r.id = q.value(0).toLongLong();
        r.messageId = q.value(1).toInt();
        r.conversationId = q.value(2).toInt();
        r.phase = q.value(3).toString();
        r.detail = q.value(4).toString();
        r.createdAt = q.value(5).toDateTime();
        out.append(r);
    }
    return out;
}
