#include "conversationdao.h"
#include "messagedao.h"
#include "wechatmessagedao.h"
#include "database.h"
#include <QDebug>
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
    c.accountId = q.value("account_id").toString();
    c.sourceType = q.value("source_type").toString();
    c.confidence = q.value("confidence").isNull() ? 100 : q.value("confidence").toInt();
    c.updatedAt = q.value("updated_at").toDateTime();
    return c;
}

int ConversationDao::create(const QString& platform, const QString& platformConvId, const QString& customerName)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("INSERT INTO conversations (platform, platform_conversation_id, account_id, customer_name, "
              "status, source_type, confidence, last_time, updated_at) "
              "VALUES (:platform, :pcid, :account, :name, :status, :source, :confidence, "
              "datetime('now','localtime'), datetime('now','localtime'))");
    const Models::PlatformType platformType = Models::platformTypeFromString(platform);
    const Models::SourceType sourceType = (platformType == Models::PlatformType::Mock)
        ? Models::SourceType::Mock
        : Models::SourceType::UiObserved;
    q.bindValue(":platform", platform);
    q.bindValue(":pcid", platformConvId);
    q.bindValue(":account", platform);
    q.bindValue(":name", customerName);
    q.bindValue(":status", QStringLiteral("new"));
    q.bindValue(":source", Models::toString(sourceType));
    q.bindValue(":confidence", Models::defaultConfidence(sourceType));

    if (!q.exec()) {
        qWarning() << "ConversationDao::create 失败:" << q.lastError().text();
        return -1;
    }
    int newId = q.lastInsertId().toInt();
    qDebug() << "创建会话 id=" << newId << "platform=" << platform << "customer=" << customerName;
    return newId;
}

int ConversationDao::create(const Models::Conversation& conversation)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("INSERT INTO conversations (platform, platform_conversation_id, account_id, customer_name, "
              "status, source_type, confidence, last_time, updated_at) "
              "VALUES (:platform, :pcid, :account, :name, :status, :source, :confidence, "
              "datetime('now','localtime'), datetime('now','localtime'))");
    q.bindValue(":platform", Models::toString(conversation.platformType));
    q.bindValue(":pcid", conversation.platformConversationId);
    q.bindValue(":account", conversation.accountId);
    q.bindValue(":name", conversation.title);
    q.bindValue(":status", Models::toString(conversation.status));
    q.bindValue(":source", Models::toString(conversation.sourceType));
    q.bindValue(":confidence", conversation.confidence);

    if (!q.exec()) {
        qWarning() << "ConversationDao::create 失败:" << q.lastError().text();
        return -1;
    }
    int newId = q.lastInsertId().toInt();
    qDebug() << "创建会话 id=" << newId
             << "platform=" << Models::toString(conversation.platformType)
             << "customer=" << conversation.title;
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
    q.prepare("UPDATE conversations SET last_message = :msg, last_time = :t, "
              "updated_at = datetime('now','localtime') WHERE id = :id");
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
    q.prepare("UPDATE conversations SET unread_count = unread_count + 1, "
              "updated_at = datetime('now','localtime') WHERE id = :id");
    q.bindValue(":id", id);
    return q.exec();
}

bool ConversationDao::clearUnread(int id)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("UPDATE conversations SET unread_count = 0, updated_at = datetime('now','localtime') WHERE id = :id");
    q.bindValue(":id", id);
    return q.exec();
}

bool ConversationDao::setStatus(int id, const QString& status)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("UPDATE conversations SET status = :s, updated_at = datetime('now','localtime') WHERE id = :id");
    q.bindValue(":s", status);
    q.bindValue(":id", id);
    if (!q.exec()) {
        qWarning() << "ConversationDao::setStatus 失败:" << q.lastError().text();
        return false;
    }
    qDebug() << "会话" << id << "状态更新为" << status;
    return true;
}

QString ConversationDao::draftForConversation(int id) const
{
    if (id <= 0)
        return {};

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("SELECT content FROM conversation_drafts WHERE conversation_id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) {
        qWarning() << "ConversationDao::draftForConversation 失败:" << q.lastError().text();
        return {};
    }
    if (!q.next())
        return {};
    return q.value(0).toString();
}

bool ConversationDao::saveDraft(int id, const QString& content)
{
    if (id <= 0)
        return false;
    if (content.isEmpty())
        return clearDraft(id);

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO conversation_drafts (conversation_id, content, updated_at) "
        "VALUES (:id, :content, datetime('now','localtime'))"));
    q.bindValue(QStringLiteral(":id"), id);
    q.bindValue(QStringLiteral(":content"), content);
    if (!q.exec()) {
        qWarning() << "ConversationDao::saveDraft 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

bool ConversationDao::clearDraft(int id)
{
    if (id <= 0)
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("DELETE FROM conversation_drafts WHERE conversation_id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) {
        qWarning() << "ConversationDao::clearDraft 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

int ConversationDao::lastSelectedConversationId() const
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("SELECT value FROM app_state WHERE key = 'last_selected_conversation_id'"));
    if (!q.exec()) {
        qWarning() << "ConversationDao::lastSelectedConversationId 失败:" << q.lastError().text();
        return -1;
    }
    if (!q.next())
        return -1;
    return q.value(0).toInt();
}

bool ConversationDao::setLastSelectedConversationId(int id)
{
    QSqlQuery q(Database::getInstance().connection());
    if (id <= 0) {
        q.prepare(QStringLiteral("DELETE FROM app_state WHERE key = 'last_selected_conversation_id'"));
        if (!q.exec()) {
            qWarning() << "ConversationDao::setLastSelectedConversationId 清理失败:" << q.lastError().text();
            return false;
        }
        return true;
    }

    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO app_state (key, value, updated_at) "
        "VALUES ('last_selected_conversation_id', :value, datetime('now','localtime'))"));
    q.bindValue(QStringLiteral(":value"), QString::number(id));
    if (!q.exec()) {
        qWarning() << "ConversationDao::setLastSelectedConversationId 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

bool ConversationDao::remove(int id)
{
    QSqlDatabase db = Database::getInstance().connection();
    QSqlQuery q(db);

    const auto conv = findById(id);
    if (conv) {
        q.prepare(
            QStringLiteral("DELETE FROM rpa_inbox_messages WHERE platform = :p "
                           "AND platform_conversation_id = :pcid"));
        q.bindValue(QStringLiteral(":p"), conv->platform);
        q.bindValue(QStringLiteral(":pcid"), conv->platformConversationId);
        if (!q.exec())
            qWarning() << "ConversationDao::remove 清理 rpa_inbox_messages 失败:"
                       << q.lastError().text();
    }

    q.prepare(QStringLiteral("DELETE FROM message_send_events WHERE conversation_id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM messages WHERE conversation_id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM conversations WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    const bool ok = q.exec();
    if (ok && conv)
        MessageDao::notifyReaderIncrementalStatePurge(conv->platform, conv->platformConversationId);
    if (ok && conv && conv->platform == QLatin1String("wechat_pc")) {
        WechatMessageDao wechatDao;
        wechatDao.deleteForConversation(id);
    }
    return ok;
}
