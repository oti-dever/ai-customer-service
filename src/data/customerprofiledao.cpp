#include "customerprofiledao.h"
#include "database.h"

#include <QDebug>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

std::optional<CustomerProfileRecord> CustomerProfileDao::findByConversationId(int conversationId) const
{
    if (conversationId <= 0)
        return std::nullopt;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT conversation_id, profile_json, source_model_key, source_request_event_id, updated_at "
        "FROM conversation_customer_profiles WHERE conversation_id = :conversationId"));
    q.bindValue(QStringLiteral(":conversationId"), conversationId);
    if (!q.exec()) {
        qWarning() << "CustomerProfileDao::findByConversationId failed:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;

    CustomerProfileRecord record;
    record.conversationId = q.value(0).toInt();
    const QJsonDocument doc = QJsonDocument::fromJson(q.value(1).toString().toUtf8());
    if (doc.isObject())
        record.profile = doc.object();
    record.sourceModelKey = q.value(2).toString();
    record.sourceRequestEventId = q.value(3).toLongLong();
    record.updatedAt = q.value(4).toDateTime();
    return record;
}

bool CustomerProfileDao::upsert(int conversationId,
                                const QJsonObject& profile,
                                const QString& sourceModelKey,
                                qint64 sourceRequestEventId)
{
    if (conversationId <= 0 || profile.isEmpty())
        return false;

    const QString json = QString::fromUtf8(QJsonDocument(profile).toJson(QJsonDocument::Compact));
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "INSERT INTO conversation_customer_profiles "
        "(conversation_id, profile_json, source_model_key, source_request_event_id, created_at, updated_at) "
        "VALUES (:conversationId, :profileJson, :sourceModelKey, :sourceRequestEventId, "
        "        datetime('now','localtime'), datetime('now','localtime')) "
        "ON CONFLICT(conversation_id) DO UPDATE SET "
        "  profile_json = excluded.profile_json, "
        "  source_model_key = excluded.source_model_key, "
        "  source_request_event_id = excluded.source_request_event_id, "
        "  updated_at = datetime('now','localtime')"));
    q.bindValue(QStringLiteral(":conversationId"), conversationId);
    q.bindValue(QStringLiteral(":profileJson"), json);
    q.bindValue(QStringLiteral(":sourceModelKey"), sourceModelKey);
    if (sourceRequestEventId > 0)
        q.bindValue(QStringLiteral(":sourceRequestEventId"), sourceRequestEventId);
    else
        q.bindValue(QStringLiteral(":sourceRequestEventId"), QVariant());
    if (!q.exec()) {
        qWarning() << "CustomerProfileDao::upsert failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool CustomerProfileDao::remove(int conversationId)
{
    if (conversationId <= 0)
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("DELETE FROM conversation_customer_profiles WHERE conversation_id = :conversationId"));
    q.bindValue(QStringLiteral(":conversationId"), conversationId);
    if (!q.exec()) {
        qWarning() << "CustomerProfileDao::remove failed:" << q.lastError().text();
        return false;
    }
    return true;
}
