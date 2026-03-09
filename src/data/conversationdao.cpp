#include "conversationdao.h"
#include "database.h"
#include <QSqlError>
#include <QSqlQuery>

static ConversationInfo recordFromQuery(QSqlQuery& q)
{
    ConversationInfo c;
    c.id = q.value("id").toInt();
    c.platform = q.value("platform").toString();
    c.platformConversationId = q.value("platform_conversation_id").toString();
    c.customerName = q.value("customer_name").toString();
    c.lastMessage = q.value("last_message").toString();
    c.lastTime = q.value("last_time").toDateTime();
    c.unreadCount = q.value("unread_count").toInt();
    c.status = q.value("status").toString();
    c.createdAt = q.value("created_at").toDateTime();
    return c;
}

int ConversationDao::create(const QString& platform, const QString& platformConvId, const QString& customerName)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("INSERT INTO conversations (platform, platform_conversation_id, customer_name, last_time) "
              "VALUES (:platform, :pcid, :name, datetime('now','localtime'))");
    q.bindValue(":platform", platform);
    q.bindValue(":pcid", platformConvId);
    q.bindValue(":name", customerName);

    if (!q.exec()) {
        qWarning() << "ConversationDao::create 失败:" << q.lastError().text();
        return -1;
    }
    int newId = q.lastInsertId().toInt();
    qDebug() << "创建会话 id=" << newId << "platform=" << platform << "customer=" << customerName;
    return newId;
}

std::optional<ConversationInfo> ConversationDao::findById(int id)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("SELECT * FROM conversations WHERE id = :id");
    q.bindValue(":id", id);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return recordFromQuery(q);
}

std::optional<ConversationInfo> ConversationDao::findByPlatformId(const QString& platform, const QString& platformConvId)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("SELECT * FROM conversations WHERE platform = :p AND platform_conversation_id = :pcid");
    q.bindValue(":p", platform);
    q.bindValue(":pcid", platformConvId);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return recordFromQuery(q);
}

QVector<ConversationInfo> ConversationDao::listByStatus(const QString& status, int limit, int offset)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("SELECT * FROM conversations WHERE status = :s ORDER BY last_time DESC LIMIT :lim OFFSET :off");
    q.bindValue(":s", status);
    q.bindValue(":lim", limit);
    q.bindValue(":off", offset);
    q.exec();

    QVector<ConversationInfo> result;
    while (q.next())
        result.append(recordFromQuery(q));
    return result;
}

QVector<ConversationInfo> ConversationDao::listAll(int limit, int offset)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("SELECT * FROM conversations ORDER BY last_time DESC LIMIT :lim OFFSET :off");
    q.bindValue(":lim", limit);
    q.bindValue(":off", offset);
    q.exec();

    QVector<ConversationInfo> result;
    while (q.next())
        result.append(recordFromQuery(q));
    return result;
}

bool ConversationDao::updateLastMessage(int id, const QString& lastMessage, const QDateTime& lastTime)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("UPDATE conversations SET last_message = :msg, last_time = :t WHERE id = :id");
    q.bindValue(":msg", lastMessage);
    q.bindValue(":t", lastTime);
    q.bindValue(":id", id);
    if (!q.exec()) {
        qWarning() << "ConversationDao::updateLastMessage 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

bool ConversationDao::incrementUnread(int id)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("UPDATE conversations SET unread_count = unread_count + 1 WHERE id = :id");
    q.bindValue(":id", id);
    return q.exec();
}

bool ConversationDao::clearUnread(int id)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("UPDATE conversations SET unread_count = 0 WHERE id = :id");
    q.bindValue(":id", id);
    return q.exec();
}

bool ConversationDao::setStatus(int id, const QString& status)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("UPDATE conversations SET status = :s WHERE id = :id");
    q.bindValue(":s", status);
    q.bindValue(":id", id);
    if (!q.exec()) {
        qWarning() << "ConversationDao::setStatus 失败:" << q.lastError().text();
        return false;
    }
    qDebug() << "会话" << id << "状态更新为" << status;
    return true;
}

bool ConversationDao::remove(int id)
{
    QSqlDatabase db = Database::getInstance().connection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM messages WHERE conversation_id = :id");
    q.bindValue(":id", id);
    q.exec();
    q.prepare("DELETE FROM conversations WHERE id = :id");
    q.bindValue(":id", id);
    return q.exec();
}
