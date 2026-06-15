#include "airequesteventdao.h"
#include "database.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

bool updateEventStatus(qint64 eventId,
                       const QString& status,
                       int durationMs,
                       int firstTokenMs,
                       int outputChars,
                       const QString& errorReason)
{
    if (eventId <= 0)
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "UPDATE ai_request_events "
        "SET status = :status, completed_at = datetime('now','localtime'), "
        "    duration_ms = :duration, first_token_ms = :firstToken, "
        "    output_chars = :outputChars, error_reason = :error "
        "WHERE id = :id"));
    q.bindValue(QStringLiteral(":status"), status);
    q.bindValue(QStringLiteral(":duration"), qMax(0, durationMs));
    q.bindValue(QStringLiteral(":firstToken"), qMax(0, firstTokenMs));
    q.bindValue(QStringLiteral(":outputChars"), qMax(0, outputChars));
    q.bindValue(QStringLiteral(":error"), errorReason.left(500));
    q.bindValue(QStringLiteral(":id"), eventId);
    if (!q.exec()) {
        qWarning() << "AiRequestEventDao update failed:" << q.lastError().text();
        return false;
    }
    return true;
}

} // namespace

qint64 AiRequestEventDao::beginEvent(const QString& source,
                                     int conversationId,
                                     const QString& sessionModelKey,
                                     const QString& model,
                                     const QString& triggerTag)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "INSERT INTO ai_request_events "
        "(source, conversation_id, session_model_key, model, status, trigger_tag, started_at, created_at) "
        "VALUES (:source, :conversationId, :sessionModelKey, :model, 'started', :triggerTag, "
        "        datetime('now','localtime'), datetime('now','localtime'))"));
    q.bindValue(QStringLiteral(":source"), source);
    if (conversationId > 0)
        q.bindValue(QStringLiteral(":conversationId"), conversationId);
    else
        q.bindValue(QStringLiteral(":conversationId"), QVariant());
    q.bindValue(QStringLiteral(":sessionModelKey"), sessionModelKey);
    q.bindValue(QStringLiteral(":model"), model);
    q.bindValue(QStringLiteral(":triggerTag"), triggerTag.left(120));
    if (!q.exec()) {
        qWarning() << "AiRequestEventDao::beginEvent failed:" << q.lastError().text();
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool AiRequestEventDao::completeEvent(qint64 eventId, int durationMs, int firstTokenMs, int outputChars)
{
    return updateEventStatus(eventId, QStringLiteral("completed"), durationMs, firstTokenMs, outputChars, QString());
}

bool AiRequestEventDao::failEvent(qint64 eventId, int durationMs, const QString& errorReason)
{
    return updateEventStatus(eventId, QStringLiteral("failed"), durationMs, 0, 0, errorReason);
}

bool AiRequestEventDao::cancelEvent(qint64 eventId, int durationMs)
{
    return updateEventStatus(eventId, QStringLiteral("canceled"), durationMs, 0, 0, QStringLiteral("canceled"));
}

bool AiRequestEventDao::appendStage(qint64 requestEventId,
                                    int conversationId,
                                    const QString& stage,
                                    const QString& detail)
{
    if (requestEventId <= 0 || conversationId <= 0 || stage.trimmed().isEmpty())
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "INSERT INTO ai_request_stage_events "
        "(request_event_id, conversation_id, stage, detail, created_at) "
        "VALUES (:requestEventId, :conversationId, :stage, :detail, datetime('now','localtime'))"));
    q.bindValue(QStringLiteral(":requestEventId"), requestEventId);
    q.bindValue(QStringLiteral(":conversationId"), conversationId);
    q.bindValue(QStringLiteral(":stage"), stage);
    q.bindValue(QStringLiteral(":detail"), detail.left(500));
    if (!q.exec()) {
        qWarning() << "AiRequestEventDao::appendStage failed:" << q.lastError().text();
        return false;
    }
    return true;
}

AiRequestEventMetrics AiRequestEventDao::aggregateMetrics(const QString& sessionModelKey) const
{
    AiRequestEventMetrics metrics;

    {
        QSqlQuery q(Database::getInstance().connection());
        q.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM ai_request_events "
            "WHERE source LIKE 'aggregate_%' AND date(started_at) = date('now','localtime')"));
        if (q.exec() && q.next())
            metrics.todayRequestCount = q.value(0).toInt();
        else
            qWarning() << "AiRequestEventDao today count failed:" << q.lastError().text();
    }

    {
        QSqlQuery q(Database::getInstance().connection());
        q.prepare(QStringLiteral(
            "SELECT "
            "  SUM(CASE WHEN status = 'completed' THEN 1 ELSE 0 END), "
            "  COUNT(*) "
            "FROM ai_request_events "
            "WHERE source LIKE 'aggregate_%' "
            "  AND status IN ('completed','failed','canceled') "
            "  AND started_at >= datetime('now','localtime','-1 day')"));
        if (q.exec() && q.next()) {
            const int completed = q.value(0).toInt();
            const int total = q.value(1).toInt();
            if (total > 0) {
                metrics.hasSuccessRate = true;
                metrics.successRatePercent = qRound(completed * 100.0 / total);
            }
        } else {
            qWarning() << "AiRequestEventDao success rate failed:" << q.lastError().text();
        }
    }

    {
        QSqlQuery q(Database::getInstance().connection());
        q.prepare(QStringLiteral(
            "SELECT AVG(duration_ms) FROM ("
            "  SELECT duration_ms FROM ai_request_events "
            "  WHERE source LIKE 'aggregate_%' AND status = 'completed' AND duration_ms > 0 "
            "  ORDER BY id DESC LIMIT 20"
            ")"));
        if (q.exec() && q.next() && !q.value(0).isNull()) {
            metrics.hasAverageDuration = true;
            metrics.averageDurationMs = qRound(q.value(0).toDouble());
        } else if (q.lastError().isValid()) {
            qWarning() << "AiRequestEventDao avg duration failed:" << q.lastError().text();
        }
    }

    if (!sessionModelKey.trimmed().isEmpty()) {
        QSqlQuery q(Database::getInstance().connection());
        q.prepare(QStringLiteral(
            "SELECT status, error_reason FROM ai_request_events "
            "WHERE session_model_key = :sessionModelKey AND status <> 'started' "
            "ORDER BY id DESC LIMIT 1"));
        q.bindValue(QStringLiteral(":sessionModelKey"), sessionModelKey);
        if (q.exec() && q.next()) {
            metrics.latestStatus = q.value(0).toString();
            metrics.latestError = q.value(1).toString();
        } else if (q.lastError().isValid()) {
            qWarning() << "AiRequestEventDao latest status failed:" << q.lastError().text();
        }
    }

    return metrics;
}

qint64 AiRequestEventDao::globalStageMaxId() const
{
    QSqlQuery q(Database::getInstance().connection());
    if (!q.exec(QStringLiteral("SELECT COALESCE(MAX(id), 0) FROM ai_request_stage_events")) || !q.next())
        return 0;
    return q.value(0).toLongLong();
}

QVector<AiRequestStageEventRecord> AiRequestEventDao::listStagesSince(int conversationId,
                                                                     qint64 afterId,
                                                                     int limit) const
{
    QVector<AiRequestStageEventRecord> out;
    if (conversationId <= 0)
        return out;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT id, request_event_id, conversation_id, stage, detail, created_at "
        "FROM ai_request_stage_events "
        "WHERE conversation_id = :conversationId AND id > :afterId "
        "ORDER BY id ASC LIMIT :limit"));
    q.bindValue(QStringLiteral(":conversationId"), conversationId);
    q.bindValue(QStringLiteral(":afterId"), afterId);
    q.bindValue(QStringLiteral(":limit"), limit);
    if (!q.exec()) {
        qWarning() << "AiRequestEventDao::listStagesSince failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        AiRequestStageEventRecord record;
        record.id = q.value(0).toLongLong();
        record.requestEventId = q.value(1).toLongLong();
        record.conversationId = q.value(2).toInt();
        record.stage = q.value(3).toString();
        record.detail = q.value(4).toString();
        record.createdAt = q.value(5).toDateTime();
        out.push_back(record);
    }
    return out;
}
