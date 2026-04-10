#include "aiassistantdao.h"
#include "database.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>

QString AiAssistantDao::modelKeyFromBaseUrlAndModel(const QString& baseUrl, const QString& model)
{
    const QString m = model.trimmed().isEmpty() ? QStringLiteral("deepseek-chat") : model.trimmed();
    QUrl u(baseUrl.trimmed());
    QString host = u.isValid() && !u.host().isEmpty() ? u.host() : baseUrl.trimmed();
    if (host.isEmpty())
        host = QStringLiteral("default");
    return QStringLiteral("compat:%1:%2").arg(host, m);
}

int AiAssistantDao::ensureSession(int userId, const QString& modelKey)
{
    if (userId <= 0 || modelKey.isEmpty())
        return -1;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("SELECT id FROM ai_assistant_sessions WHERE user_id = :uid AND model_key = :mk"));
    q.bindValue(QStringLiteral(":uid"), userId);
    q.bindValue(QStringLiteral(":mk"), modelKey);
    if (!q.exec()) {
        qWarning() << "AiAssistantDao::ensureSession SELECT failed:" << q.lastError().text();
        return -1;
    }
    if (q.next())
        return q.value(0).toInt();

    q.prepare(QStringLiteral(
        "INSERT INTO ai_assistant_sessions (user_id, model_key) VALUES (:uid, :mk)"));
    q.bindValue(QStringLiteral(":uid"), userId);
    q.bindValue(QStringLiteral(":mk"), modelKey);
    if (!q.exec()) {
        qWarning() << "AiAssistantDao::ensureSession INSERT failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

QVector<AiAssistantChatTurn> AiAssistantDao::listMessages(int sessionId) const
{
    QVector<AiAssistantChatTurn> out;
    if (sessionId <= 0)
        return out;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT role, content FROM ai_assistant_messages WHERE session_id = :sid ORDER BY id ASC"));
    q.bindValue(QStringLiteral(":sid"), sessionId);
    if (!q.exec()) {
        qWarning() << "AiAssistantDao::listMessages failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        out.push_back({q.value(0).toString(), q.value(1).toString()});
    }
    return out;
}

bool AiAssistantDao::appendMessage(int sessionId, const QString& role, const QString& content)
{
    if (sessionId <= 0 || (role != QLatin1String("user") && role != QLatin1String("assistant")))
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "INSERT INTO ai_assistant_messages (session_id, role, content) VALUES (:sid, :role, :content)"));
    q.bindValue(QStringLiteral(":sid"), sessionId);
    q.bindValue(QStringLiteral(":role"), role);
    q.bindValue(QStringLiteral(":content"), content);
    if (!q.exec()) {
        qWarning() << "AiAssistantDao::appendMessage failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool AiAssistantDao::clearMessages(int sessionId)
{
    if (sessionId <= 0)
        return false;

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral("DELETE FROM ai_assistant_messages WHERE session_id = :sid"));
    q.bindValue(QStringLiteral(":sid"), sessionId);
    if (!q.exec()) {
        qWarning() << "AiAssistantDao::clearMessages failed:" << q.lastError().text();
        return false;
    }
    return true;
}
