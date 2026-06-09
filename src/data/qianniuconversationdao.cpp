#include "qianniuconversationdao.h"
#include "database.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

namespace {

QString jsonCompact(const QJsonObject& obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString effectiveDisplayName(const QString& displayName, const QJsonObject& payload)
{
    const QString explicitName = payload.value(QStringLiteral("display_name")).toString().trimmed();
    if (!explicitName.isEmpty())
        return explicitName;
    const QString senderName = payload.value(QStringLiteral("sender_name")).toString().trimmed();
    if (!senderName.isEmpty())
        return senderName;
    return displayName.trimmed();
}

} // namespace

bool QianniuConversationDao::upsertConversation(int conversationId,
                                               const QString& accountId,
                                               const QString& conversationKey,
                                               const QString& displayName,
                                               const QJsonObject& payload)
{
    if (conversationId <= 0)
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "INSERT INTO qianniu_conversations ("
        "conversation_id, qianniu_account_id, qianniu_conversation_key, display_name, "
        "last_unread_badge, last_observed_at, last_health_status, raw_payload_json"
        ") VALUES ("
        ":cid, :account, :ckey, :display, :unread, datetime('now','localtime'), :health, :raw"
        ") "
        "ON CONFLICT(conversation_id) DO UPDATE SET "
        "qianniu_account_id = excluded.qianniu_account_id, "
        "qianniu_conversation_key = excluded.qianniu_conversation_key, "
        "display_name = excluded.display_name, "
        "last_unread_badge = excluded.last_unread_badge, "
        "last_observed_at = excluded.last_observed_at, "
        "last_health_status = excluded.last_health_status, "
        "raw_payload_json = excluded.raw_payload_json, "
        "updated_at = datetime('now','localtime')"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    q.bindValue(QStringLiteral(":account"),
                accountId.isEmpty() ? payload.value(QStringLiteral("_event_account_id")).toString() : accountId);
    q.bindValue(QStringLiteral(":ckey"),
                conversationKey.isEmpty()
                    ? payload.value(QStringLiteral("_event_conversation_key")).toString()
                    : conversationKey);
    q.bindValue(QStringLiteral(":display"), effectiveDisplayName(displayName, payload));
    q.bindValue(QStringLiteral(":unread"), payload.value(QStringLiteral("unread_count")).toInt(0));
    q.bindValue(QStringLiteral(":health"), payload.value(QStringLiteral("status")).toString());
    q.bindValue(QStringLiteral(":raw"), jsonCompact(payload));
    if (!q.exec()) {
        qWarning() << "QianniuConversationDao::upsertConversation failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool QianniuConversationDao::createMessageExtension(int messageId,
                                                   int conversationId,
                                                   const QString& accountId,
                                                   const QString& conversationKey,
                                                   const QString& displayName,
                                                   const QString& platformMsgId,
                                                   const QJsonObject& payload)
{
    if (messageId <= 0 || conversationId <= 0)
        return false;

    const QJsonObject meta = payload.value(QStringLiteral("metadata")).toObject();
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "INSERT INTO qianniu_messages ("
        "message_id, conversation_id, qianniu_account_id, qianniu_conversation_key, "
        "qianniu_display_name, platform_message_id, direction, sender_role, raw_sender, "
        "raw_timestamp_text, parser_source, source_type, confidence, verification_status, "
        "original_timestamp, content_image_path, role_method, role_confidence, bubble_rect, "
        "message_list_rect, evidence_ref, raw_payload_json"
        ") VALUES ("
        ":mid, :cid, :account, :ckey, :display, :pmid, :direction, :sender_role, :raw_sender, "
        ":raw_ts, :parser, :source, :confidence, :verification, :original_timestamp, "
        ":content_image_path, :role_method, :role_confidence, :bubble_rect, :message_list_rect, "
        ":evidence, :raw"
        ") "
        "ON CONFLICT(message_id) DO UPDATE SET "
        "conversation_id = excluded.conversation_id, "
        "qianniu_account_id = excluded.qianniu_account_id, "
        "qianniu_conversation_key = excluded.qianniu_conversation_key, "
        "qianniu_display_name = excluded.qianniu_display_name, "
        "platform_message_id = excluded.platform_message_id, "
        "direction = excluded.direction, "
        "sender_role = excluded.sender_role, "
        "raw_sender = excluded.raw_sender, "
        "raw_timestamp_text = excluded.raw_timestamp_text, "
        "parser_source = excluded.parser_source, "
        "source_type = excluded.source_type, "
        "confidence = excluded.confidence, "
        "verification_status = excluded.verification_status, "
        "original_timestamp = excluded.original_timestamp, "
        "content_image_path = excluded.content_image_path, "
        "role_method = excluded.role_method, "
        "role_confidence = excluded.role_confidence, "
        "bubble_rect = excluded.bubble_rect, "
        "message_list_rect = excluded.message_list_rect, "
        "evidence_ref = excluded.evidence_ref, "
        "raw_payload_json = excluded.raw_payload_json"));
    q.bindValue(QStringLiteral(":mid"), messageId);
    q.bindValue(QStringLiteral(":cid"), conversationId);
    q.bindValue(QStringLiteral(":account"),
                accountId.isEmpty() ? payload.value(QStringLiteral("_event_account_id")).toString() : accountId);
    q.bindValue(QStringLiteral(":ckey"),
                conversationKey.isEmpty()
                    ? payload.value(QStringLiteral("_event_conversation_key")).toString()
                    : conversationKey);
    q.bindValue(QStringLiteral(":display"), effectiveDisplayName(displayName, payload));
    q.bindValue(QStringLiteral(":pmid"), platformMsgId);
    q.bindValue(QStringLiteral(":direction"), payload.value(QStringLiteral("direction")).toString());
    q.bindValue(QStringLiteral(":sender_role"), payload.value(QStringLiteral("sender_role")).toString());
    q.bindValue(QStringLiteral(":raw_sender"), payload.value(QStringLiteral("sender_name")).toString());
    q.bindValue(QStringLiteral(":raw_ts"), payload.value(QStringLiteral("original_timestamp")).toString());
    q.bindValue(QStringLiteral(":parser"),
                meta.value(QStringLiteral("parser_source")).toString(
                    meta.value(QStringLiteral("observation_method")).toString()));
    q.bindValue(QStringLiteral(":source"), payload.value(QStringLiteral("source_type")).toString(QStringLiteral("ui_observed")));
    q.bindValue(QStringLiteral(":confidence"), payload.value(QStringLiteral("confidence")).toInt(70));
    q.bindValue(QStringLiteral(":verification"), payload.value(QStringLiteral("verification_status")).toString(QStringLiteral("unverified")));
    q.bindValue(QStringLiteral(":original_timestamp"), payload.value(QStringLiteral("original_timestamp")).toString());
    q.bindValue(QStringLiteral(":content_image_path"), payload.value(QStringLiteral("content_image_path")).toString());
    q.bindValue(QStringLiteral(":role_method"), meta.value(QStringLiteral("direction_method")).toString());
    q.bindValue(QStringLiteral(":role_confidence"), meta.value(QStringLiteral("role_confidence")).toDouble());
    q.bindValue(QStringLiteral(":bubble_rect"), meta.value(QStringLiteral("rect")).toString());
    q.bindValue(QStringLiteral(":message_list_rect"),
                QString::fromUtf8(QJsonDocument(meta.value(QStringLiteral("chat_context")).toObject())
                                      .toJson(QJsonDocument::Compact)));
    q.bindValue(QStringLiteral(":evidence"), payload.value(QStringLiteral("evidence_ref")).toString(
                    payload.value(QStringLiteral("content_image_path")).toString()));
    q.bindValue(QStringLiteral(":raw"), jsonCompact(payload));
    if (!q.exec()) {
        qWarning() << "QianniuConversationDao::createMessageExtension failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool QianniuConversationDao::deleteForConversation(int conversationId)
{
    if (conversationId <= 0)
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("DELETE FROM qianniu_messages WHERE conversation_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (!q.exec()) {
        qWarning() << "QianniuConversationDao::deleteForConversation messages failed:" << q.lastError().text();
        return false;
    }
    q.prepare(QStringLiteral("DELETE FROM qianniu_conversations WHERE conversation_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), conversationId);
    if (!q.exec()) {
        qWarning() << "QianniuConversationDao::deleteForConversation failed:" << q.lastError().text();
        return false;
    }
    return true;
}
