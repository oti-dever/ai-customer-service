#include "conversationdao.h"
#include "wechatmessagedao.h"
#include "qianniuconversationdao.h"
#include "database.h"
#include <QDebug>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QVariant>

static ConversationInfo recordFromQuery(QSqlQuery& q);

namespace {

QString conversationCacheScope()
{
    return QStringLiteral("local_cache");
}

QString conversationCacheOriginForSourceType(Models::SourceType sourceType)
{
    switch (sourceType) {
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

QString snapshotCursorKey(const QString& platform)
{
    const QString suffix = platform.trimmed().toLower().isEmpty()
        ? QStringLiteral("all")
        : platform.trimmed().toLower();
    return QStringLiteral("cache_snapshot_cursor/%1").arg(suffix);
}

QString rpaReplayCursorKey(const QString& platform)
{
    const QString suffix = platform.trimmed().toLower().isEmpty()
        ? QStringLiteral("all")
        : platform.trimmed().toLower();
    return QStringLiteral("rpa_replay_cursor/%1").arg(suffix);
}

QString displayKeyFromCanonical(const QString& platformConversationId)
{
    const int lastSep = platformConversationId.lastIndexOf(QLatin1Char(':'));
    if (lastSep < 0 || lastSep + 1 >= platformConversationId.size())
        return QString();
    return platformConversationId.mid(lastSep + 1).trimmed();
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

std::optional<ConversationInfo> findLegacyShortConversation(const QString& platform,
                                                            const QString& platformConversationId)
{
    if (!platformConversationId.startsWith(platform + QLatin1Char(':')))
        return std::nullopt;
    const QString displayKey = displayKeyFromCanonical(platformConversationId);
    if (displayKey.isEmpty() || displayKey == platformConversationId)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("SELECT * FROM conversations WHERE platform = :p AND platform_conversation_id = :pcid"));
    q.bindValue(QStringLiteral(":p"), platform);
    q.bindValue(QStringLiteral(":pcid"), displayKey);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return recordFromQuery(q);
}

QString placeholders(const QString& prefix, int count)
{
    QStringList items;
    items.reserve(count);
    for (int i = 0; i < count; ++i)
        items.append(QStringLiteral(":%1%2").arg(prefix).arg(i));
    return items.join(QStringLiteral(", "));
}

bool tableHasColumn(const QString& tableName, const QString& columnName)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("PRAGMA table_info(%1)").arg(tableName));
    if (!q.exec())
        return false;
    while (q.next()) {
        if (q.value(1).toString() == columnName)
            return true;
    }
    return false;
}

} // namespace

static ConversationInfo recordFromQuery(QSqlQuery& q)
{
    ConversationInfo c;
    c.id = rowInt(q, QStringLiteral("id"));
    c.platform = rowString(q, QStringLiteral("platform"));
    c.platformConversationId = rowString(q, QStringLiteral("platform_conversation_id"));
    c.customerName = rowString(q, QStringLiteral("customer_name"));
    c.lastMessage = rowString(q, QStringLiteral("last_message"));
    c.lastTime = rowDateTime(q, QStringLiteral("last_time"));
    c.unreadCount = rowInt(q, QStringLiteral("unread_count"));
    c.status = rowString(q, QStringLiteral("status"));
    c.createdAt = rowDateTime(q, QStringLiteral("created_at"));
    c.accountId = rowString(q, QStringLiteral("account_id"));
    c.sourceType = QStringLiteral("ui_observed");
    c.confidence = 100;
    c.updatedAt = rowDateTime(q, QStringLiteral("updated_at"));
    c.cacheScope = rowString(q, QStringLiteral("cache_scope"));
    if (c.cacheScope.isEmpty())
        c.cacheScope = QStringLiteral("service_db");
    c.cacheOrigin = rowString(q, QStringLiteral("cache_origin"));
    if (c.cacheOrigin.isEmpty())
        c.cacheOrigin = QStringLiteral("python_service_db");
    return c;
}

int ConversationDao::create(const QString& platform, const QString& platformConvId, const QString& customerName)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("INSERT INTO conversations (platform, platform_conversation_id, account_id, customer_name, "
              "status, cache_scope, cache_origin, last_time, updated_at) "
              "VALUES (:platform, :pcid, :account, :name, :status, :cache_scope, :cache_origin, "
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
    q.bindValue(":cache_scope", conversationCacheScope());
    q.bindValue(":cache_origin", conversationCacheOriginForSourceType(sourceType));

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
              "status, cache_scope, cache_origin, last_time, updated_at) "
              "VALUES (:platform, :pcid, :account, :name, :status, :cache_scope, :cache_origin, "
              "datetime('now','localtime'), datetime('now','localtime'))");
    q.bindValue(":platform", Models::toString(conversation.platformType));
    q.bindValue(":pcid", conversation.platformConversationId);
    q.bindValue(":account", conversation.accountId);
    q.bindValue(":name", conversation.title);
    q.bindValue(":status", Models::toString(conversation.status));
    q.bindValue(":cache_scope", conversationCacheScope());
    q.bindValue(":cache_origin", conversationCacheOriginForSourceType(conversation.sourceType));

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

int ConversationDao::upsertObservedCacheConversation(const Models::Conversation& conversation)
{
    const QString platform = Models::toString(conversation.platformType);
    if (platform.isEmpty() || conversation.platformConversationId.isEmpty())
        return -1;

    auto existing = findByPlatformId(platform, conversation.platformConversationId);
    if (!existing)
        existing = findLegacyShortConversation(platform, conversation.platformConversationId);
    if (!existing)
        return create(conversation);

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "UPDATE conversations SET "
        "platform_conversation_id = :pcid, "
        "account_id = COALESCE(NULLIF(:account, ''), account_id), "
        "customer_name = CASE "
        "  WHEN length(trim(coalesce(:name, ''))) > 0 THEN :name "
        "  ELSE customer_name END, "
        "status = CASE "
        "  WHEN status = 'closed' THEN 'active' "
        "  ELSE status END, "
        "cache_scope = :cache_scope, "
        "cache_origin = :cache_origin, "
        "updated_at = datetime('now','localtime') "
        "WHERE id = :id"));
    q.bindValue(QStringLiteral(":pcid"), conversation.platformConversationId);
    q.bindValue(QStringLiteral(":account"), conversation.accountId);
    q.bindValue(QStringLiteral(":name"), conversation.title);
    q.bindValue(QStringLiteral(":cache_scope"), conversationCacheScope());
    q.bindValue(QStringLiteral(":cache_origin"), QStringLiteral("platform_observed_cache"));
    q.bindValue(QStringLiteral(":id"), existing->id);
    if (!q.exec()) {
        qWarning() << "ConversationDao::upsertObservedCacheConversation 失败:"
                   << q.lastError().text();
        return -1;
    }
    return existing->id;
}

int ConversationDao::upsertSnapshotCacheConversation(const QJsonObject& conversation)
{
    const QString platform = jsonString(conversation, QStringLiteral("platform")).toLower();
    const QString platformConversationId = jsonString(conversation, QStringLiteral("platform_conversation_id"));
    if (platform.isEmpty() || platformConversationId.isEmpty())
        return -1;

    const QString customerName = jsonString(conversation, QStringLiteral("customer_name"));
    const QString accountId = jsonString(conversation, QStringLiteral("account_id"));
    const QString lastMessage = conversation.value(QStringLiteral("last_message")).toString();
    const QString status = jsonString(conversation, QStringLiteral("status")).isEmpty()
        ? QStringLiteral("active")
        : jsonString(conversation, QStringLiteral("status"));
    const int unreadCount = jsonInt(conversation, QStringLiteral("unread_count"), 0);
    const QString lastTime = jsonString(conversation, QStringLiteral("last_time"));
    const QString createdAt = jsonString(conversation, QStringLiteral("created_at"));
    const QString updatedAt = jsonString(conversation, QStringLiteral("updated_at"));
    const QString deletedAt = jsonString(conversation, QStringLiteral("deleted_at"));
    auto existing = findByPlatformId(platform, platformConversationId);
    if (!existing)
        existing = findLegacyShortConversation(platform, platformConversationId);

    QSqlQuery q(Database::getInstance().connection());
    if (existing) {
        q.prepare(QStringLiteral(
            "UPDATE conversations SET "
            "platform_conversation_id = :pcid, "
                "account_id = :account, "
            "customer_name = :name, "
            "last_message = :last_message, "
            "unread_count = :unread, "
            "status = :status, "
            "cache_scope = 'local_cache', "
            "cache_origin = 'server_snapshot_cache', "
            "last_time = COALESCE(NULLIF(:last_time, ''), last_time), "
            "created_at = COALESCE(NULLIF(:created_at, ''), created_at), "
            "updated_at = COALESCE(NULLIF(:updated_at, ''), datetime('now','localtime')), "
            "deleted_at = NULLIF(:deleted_at, '') "
            "WHERE id = :id"));
        q.bindValue(QStringLiteral(":id"), existing->id);
        q.bindValue(QStringLiteral(":pcid"), platformConversationId);
    } else {
        q.prepare(QStringLiteral(
            "INSERT INTO conversations (platform, platform_conversation_id, account_id, customer_name, "
            "last_message, unread_count, status, cache_scope, cache_origin, "
            "last_time, created_at, updated_at, deleted_at) "
            "VALUES (:platform, :pcid, :account, :name, :last_message, :unread, :status, "
            "'local_cache', 'server_snapshot_cache', "
            "COALESCE(NULLIF(:last_time, ''), datetime('now','localtime')), "
            "COALESCE(NULLIF(:created_at, ''), datetime('now','localtime')), "
            "COALESCE(NULLIF(:updated_at, ''), datetime('now','localtime')), "
            "NULLIF(:deleted_at, ''))"));
        q.bindValue(QStringLiteral(":platform"), platform);
        q.bindValue(QStringLiteral(":pcid"), platformConversationId);
    }
    q.bindValue(QStringLiteral(":account"), accountId.isEmpty() ? platform : accountId);
    q.bindValue(QStringLiteral(":name"), customerName.isEmpty() ? platformConversationId : customerName);
    q.bindValue(QStringLiteral(":last_message"), lastMessage);
    q.bindValue(QStringLiteral(":unread"), unreadCount);
    q.bindValue(QStringLiteral(":status"), status);
    q.bindValue(QStringLiteral(":last_time"), lastTime);
    q.bindValue(QStringLiteral(":created_at"), createdAt);
    q.bindValue(QStringLiteral(":updated_at"), updatedAt);
    q.bindValue(QStringLiteral(":deleted_at"), deletedAt);
    if (!q.exec()) {
        qWarning() << "ConversationDao::upsertSnapshotCacheConversation 失败:"
                   << q.lastError().text();
        return -1;
    }
    if (existing)
        return existing->id;
    return q.lastInsertId().toInt();
}

int ConversationDao::deleteMissingSnapshotCacheConversations(
    const QString& platform,
    const QSet<QString>& keepPlatformConversationIds)
{
    const QString normalizedPlatform = platform.trimmed().toLower();
    if (normalizedPlatform.isEmpty())
        return 0;

    QSqlDatabase db = Database::getInstance().connection();
    const QString keepClause = keepPlatformConversationIds.isEmpty()
        ? QString()
        : QStringLiteral(" AND platform_conversation_id NOT IN (%1)")
              .arg(placeholders(QStringLiteral("pcid"), keepPlatformConversationIds.size()));
    const QString targetWhere = QStringLiteral(
        "platform = :platform "
        "AND cache_origin = 'server_snapshot_cache' "
        "%1 "
        "AND id NOT IN ("
        "  SELECT DISTINCT conversation_id FROM messages "
        "  WHERE conversation_id IS NOT NULL "
        "    AND coalesce(cache_origin, '') <> 'server_snapshot_cache'"
        ")").arg(keepClause);

    if (!db.transaction()) {
        qWarning() << "ConversationDao::deleteMissingSnapshotCacheConversations 无法开启事务";
        return 0;
    }

    auto bindKeepIds = [&keepPlatformConversationIds](QSqlQuery& query) {
        int i = 0;
        for (const QString& id : keepPlatformConversationIds) {
            query.bindValue(QStringLiteral(":pcid%1").arg(i), id);
            ++i;
        }
    };

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM messages WHERE conversation_id IN ("
        "  SELECT id FROM conversations WHERE %1"
        ")").arg(targetWhere));
    q.bindValue(QStringLiteral(":platform"), normalizedPlatform);
    bindKeepIds(q);
    if (!q.exec()) {
        qWarning() << "ConversationDao::deleteMissingSnapshotCacheConversations 删除消息失败:"
                   << q.lastError().text();
        db.rollback();
        return 0;
    }

    q.prepare(QStringLiteral("DELETE FROM conversations WHERE %1").arg(targetWhere));
    q.bindValue(QStringLiteral(":platform"), normalizedPlatform);
    bindKeepIds(q);
    if (!q.exec()) {
        qWarning() << "ConversationDao::deleteMissingSnapshotCacheConversations 删除会话失败:"
                   << q.lastError().text();
        db.rollback();
        return 0;
    }
    const int removed = q.numRowsAffected();
    db.commit();
    return removed;
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
    if (!q.exec())
        return std::nullopt;
    if (!q.next())
        return findLegacyShortConversation(platform, platformConvId);
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
    const QString deletedFilter = tableHasColumn(QStringLiteral("conversations"), QStringLiteral("deleted_at"))
        ? QStringLiteral("WHERE deleted_at IS NULL OR deleted_at = ''")
        : QString();
    q.prepare(QStringLiteral(
        "SELECT * FROM conversations %1 "
        "ORDER BY last_time DESC, id DESC LIMIT :lim OFFSET :off").arg(deletedFilter));
    q.bindValue(":lim", limit);
    q.bindValue(":off", offset);
    q.exec();

    QVector<ConversationInfo> result;
    while (q.next())
        result.append(recordFromQuery(q));
    return result;
}

QVector<ConversationInfo> ConversationDao::listCachedConversations(int limit, int offset)
{
    return listAll(limit, offset);
}

bool ConversationDao::updateDisplayName(int id, const QString& customerName)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("UPDATE conversations SET customer_name = :name, updated_at = datetime('now','localtime') WHERE id = :id");
    q.bindValue(":name", customerName);
    q.bindValue(":id", id);
    if (!q.exec()) {
        qWarning() << "ConversationDao::updateDisplayName 失败:" << q.lastError().text();
        return false;
    }
    return true;
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

QString ConversationDao::snapshotCursor(const QString& platform) const
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("SELECT value FROM app_state WHERE key = :key"));
    q.bindValue(QStringLiteral(":key"), snapshotCursorKey(platform));
    if (!q.exec()) {
        qWarning() << "ConversationDao::snapshotCursor 失败:" << q.lastError().text();
        return {};
    }
    if (!q.next())
        return {};
    return q.value(0).toString();
}

bool ConversationDao::setSnapshotCursor(const QString& platform, const QString& cursor)
{
    const QString cleanCursor = cursor.trimmed();
    QSqlQuery q(Database::getInstance().connection());
    if (cleanCursor.isEmpty()) {
        q.prepare(QStringLiteral("DELETE FROM app_state WHERE key = :key"));
        q.bindValue(QStringLiteral(":key"), snapshotCursorKey(platform));
        return q.exec();
    }

    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO app_state (key, value, updated_at) "
        "VALUES (:key, :value, datetime('now','localtime'))"));
    q.bindValue(QStringLiteral(":key"), snapshotCursorKey(platform));
    q.bindValue(QStringLiteral(":value"), cleanCursor);
    if (!q.exec()) {
        qWarning() << "ConversationDao::setSnapshotCursor 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

QString ConversationDao::rpaReplayCursor(const QString& platform) const
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("SELECT value FROM app_state WHERE key = :key"));
    q.bindValue(QStringLiteral(":key"), rpaReplayCursorKey(platform));
    if (!q.exec()) {
        qWarning() << "ConversationDao::rpaReplayCursor 失败:" << q.lastError().text();
        return {};
    }
    if (!q.next())
        return {};
    return q.value(0).toString();
}

bool ConversationDao::setRpaReplayCursor(const QString& platform, const QString& cursor)
{
    const QString cleanCursor = cursor.trimmed();
    QSqlQuery q(Database::getInstance().connection());
    if (cleanCursor.isEmpty()) {
        q.prepare(QStringLiteral("DELETE FROM app_state WHERE key = :key"));
        q.bindValue(QStringLiteral(":key"), rpaReplayCursorKey(platform));
        return q.exec();
    }

    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO app_state (key, value, updated_at) "
        "VALUES (:key, :value, datetime('now','localtime'))"));
    q.bindValue(QStringLiteral(":key"), rpaReplayCursorKey(platform));
    q.bindValue(QStringLiteral(":value"), cleanCursor);
    if (!q.exec()) {
        qWarning() << "ConversationDao::setRpaReplayCursor 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

QString ConversationDao::cachedDraftForConversation(int id) const
{
    return draftForConversation(id);
}

bool ConversationDao::saveCachedDraft(int id, const QString& content)
{
    return saveDraft(id, content);
}

bool ConversationDao::clearCachedDraft(int id)
{
    return clearDraft(id);
}

int ConversationDao::lastSelectedCachedConversationId() const
{
    return lastSelectedConversationId();
}

bool ConversationDao::setLastSelectedCachedConversationId(int id)
{
    return setLastSelectedConversationId(id);
}

bool ConversationDao::remove(int id)
{
    QSqlDatabase db = Database::getInstance().connection();
    QSqlQuery q(db);

    const auto conv = findById(id);

    q.prepare(QStringLiteral("DELETE FROM message_send_events WHERE conversation_id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM messages WHERE conversation_id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM conversations WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    const bool ok = q.exec();
    if (ok && conv && conv->platform == QLatin1String("wechat")) {
        WechatMessageDao wechatDao;
        wechatDao.deleteForConversation(id);
    } else if (ok && conv && conv->platform == QLatin1String("qianniu")) {
        QianniuConversationDao qianniuDao;
        qianniuDao.deleteForConversation(id);
    }
    return ok;
}
