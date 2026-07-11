#include "robotsandboxmessagedao.h"
#include "database.h"

#include <QDebug>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

RobotSandboxMessageRecord recordFromQuery(const QSqlQuery& query)
{
    RobotSandboxMessageRecord record;
    record.id = query.value(0).toLongLong();
    record.robotId = query.value(1).toString();
    record.role = query.value(2).toString();
    record.content = query.value(3).toString();
    record.contentType = query.value(4).toString();
    record.imagePath = query.value(5).toString();
    const QJsonDocument metadata = QJsonDocument::fromJson(query.value(6).toString().toUtf8());
    if (metadata.isObject())
        record.metadata = metadata.object();
    record.createdAt = query.value(7).toDateTime();
    return record;
}

bool appendMessage(QSqlQuery& query, const RobotSandboxMessageRecord& message)
{
    const QString robotId = message.robotId.trimmed();
    const QString role = message.role.trimmed().toLower();
    const QString contentType = message.contentType.trimmed().toLower();
    QString content = message.content;
    QString imagePath = message.imagePath;
    if (content.isNull())
        content = QStringLiteral("");
    if (imagePath.isNull())
        imagePath = QStringLiteral("");
    if (robotId.isEmpty()
        || (role != QLatin1String("user") && role != QLatin1String("assistant")
            && role != QLatin1String("system"))
        || (contentType != QLatin1String("text") && contentType != QLatin1String("image"))) {
        return false;
    }
    if (contentType == QLatin1String("text") && content.trimmed().isEmpty())
        return false;
    if (contentType == QLatin1String("image") && imagePath.trimmed().isEmpty())
        return false;
    if (contentType == QLatin1String("text"))
        imagePath = QStringLiteral("");
    if (contentType == QLatin1String("image"))
        content = QStringLiteral("");

    query.prepare(QStringLiteral(
        "INSERT INTO robot_sandbox_messages "
        "(robot_id, role, content, content_type, image_path, metadata, created_at) "
        "VALUES (:robotId, :role, :content, :contentType, :imagePath, :metadata, "
        "        COALESCE(:createdAt, datetime('now','localtime')))"));
    query.bindValue(QStringLiteral(":robotId"), robotId);
    query.bindValue(QStringLiteral(":role"), role);
    query.bindValue(QStringLiteral(":content"), content);
    query.bindValue(QStringLiteral(":contentType"), contentType);
    query.bindValue(QStringLiteral(":imagePath"), imagePath);
    query.bindValue(QStringLiteral(":metadata"),
                    QString::fromUtf8(QJsonDocument(message.metadata).toJson(QJsonDocument::Compact)));
    if (message.createdAt.isValid())
        query.bindValue(QStringLiteral(":createdAt"), message.createdAt.toString(Qt::ISODateWithMs));
    else
        query.bindValue(QStringLiteral(":createdAt"), QVariant());
    return query.exec();
}

} // namespace

QVector<RobotSandboxMessageRecord> RobotSandboxMessageDao::listForRobot(const QString& robotId,
                                                                        int limit) const
{
    QVector<RobotSandboxMessageRecord> records;
    const QString id = robotId.trimmed();
    if (id.isEmpty())
        return records;

    QSqlQuery query(Database::getInstance().connection());
    query.prepare(QStringLiteral(
        "SELECT id, robot_id, role, content, content_type, image_path, metadata, created_at "
        "FROM ("
        "  SELECT id, robot_id, role, content, content_type, image_path, metadata, created_at "
        "  FROM robot_sandbox_messages WHERE robot_id = :robotId "
        "  ORDER BY id DESC LIMIT :limit"
        ") ORDER BY id ASC"));
    query.bindValue(QStringLiteral(":robotId"), id);
    query.bindValue(QStringLiteral(":limit"), qBound(1, limit, 1000));
    if (!query.exec()) {
        qWarning() << "RobotSandboxMessageDao::listForRobot failed:" << query.lastError().text();
        return records;
    }
    while (query.next())
        records.append(recordFromQuery(query));
    return records;
}

QVector<RobotSandboxMessageRecord> RobotSandboxMessageDao::listRecent(const QString& robotId,
                                                                      int limit) const
{
    QVector<RobotSandboxMessageRecord> records;
    const QString id = robotId.trimmed();
    if (id.isEmpty())
        return records;

    QSqlQuery query(Database::getInstance().connection());
    query.prepare(QStringLiteral(
        "SELECT id, robot_id, role, content, content_type, image_path, metadata, created_at "
        "FROM ("
        "  SELECT id, robot_id, role, content, content_type, image_path, metadata, created_at "
        "  FROM robot_sandbox_messages WHERE robot_id = :robotId "
        "  ORDER BY id DESC LIMIT :limit"
        ") ORDER BY id ASC"));
    query.bindValue(QStringLiteral(":robotId"), id);
    query.bindValue(QStringLiteral(":limit"), qBound(1, limit, 100));
    if (!query.exec()) {
        qWarning() << "RobotSandboxMessageDao::listRecent failed:" << query.lastError().text();
        return records;
    }
    while (query.next())
        records.append(recordFromQuery(query));
    return records;
}

bool RobotSandboxMessageDao::append(const RobotSandboxMessageRecord& message)
{
    QSqlQuery query(Database::getInstance().connection());
    if (!appendMessage(query, message)) {
        if (query.lastError().isValid())
            qWarning() << "RobotSandboxMessageDao::append failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool RobotSandboxMessageDao::appendMany(const QVector<RobotSandboxMessageRecord>& messages)
{
    if (messages.isEmpty())
        return true;

    QSqlDatabase db = Database::getInstance().connection();
    if (!db.transaction())
        return false;
    QSqlQuery query(db);
    for (const RobotSandboxMessageRecord& message : messages) {
        if (!appendMessage(query, message)) {
            qWarning() << "RobotSandboxMessageDao::appendMany failed:" << query.lastError().text();
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

bool RobotSandboxMessageDao::clearForRobot(const QString& robotId)
{
    const QString id = robotId.trimmed();
    if (id.isEmpty())
        return false;
    QSqlQuery query(Database::getInstance().connection());
    query.prepare(QStringLiteral("DELETE FROM robot_sandbox_messages WHERE robot_id = :robotId"));
    query.bindValue(QStringLiteral(":robotId"), id);
    if (!query.exec()) {
        qWarning() << "RobotSandboxMessageDao::clearForRobot failed:" << query.lastError().text();
        return false;
    }
    return true;
}
