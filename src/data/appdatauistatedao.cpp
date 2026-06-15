#include "appdatauistatedao.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QDebug>

namespace {

QString cleanEnvValue(const char* name)
{
    return QProcessEnvironment::systemEnvironment()
        .value(QString::fromLatin1(name))
        .trimmed();
}

QString normalizedPlatform(QString platform)
{
    return platform.trimmed().toLower();
}

QString lastSelectedConversationStateKey()
{
    return QStringLiteral("aggregate/last_selected_conversation");
}

bool openAppDataConnection(QSqlDatabase& db, QString& connectionName)
{
    connectionName = QStringLiteral("app_data_ui_state_%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(AppDataUiStateDao::resolvedAppDataDbPath());
    if (!db.open()) {
        qWarning() << "AppDataUiStateDao open failed:" << db.lastError().text()
                   << "path=" << db.databaseName();
        return false;
    }

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
    return true;
}

bool ensureUiStateTable(QSqlDatabase db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS ui_state ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT DEFAULT '',"
            "  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ")"))) {
        qWarning() << "AppDataUiStateDao ensure ui_state failed:"
                   << q.lastError().text();
        return false;
    }
    return true;
}

void closeAppDataConnection(QSqlDatabase& db, const QString& connectionName)
{
    if (db.isValid())
        db.close();
    db = QSqlDatabase();
    if (!connectionName.isEmpty())
        QSqlDatabase::removeDatabase(connectionName);
}

bool ensureUiDraftTable(QSqlDatabase db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS ui_conversation_drafts ("
            "  platform TEXT NOT NULL,"
            "  conversation_key TEXT NOT NULL,"
            "  content TEXT NOT NULL DEFAULT '',"
            "  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "  PRIMARY KEY(platform, conversation_key)"
            ")"))) {
        qWarning() << "AppDataUiStateDao ensure ui_conversation_drafts failed:"
                   << q.lastError().text();
        return false;
    }
    return true;
}

} // namespace

QString AppDataUiStateDao::resolvedAppDataDbPath()
{
    const QString appDb = cleanEnvValue("AI_CUSTOMER_SERVICE_APP_DB");
    if (!appDb.isEmpty())
        return QDir::fromNativeSeparators(appDb);

    const QString serverDb = cleanEnvValue("AI_CUSTOMER_SERVICE_SERVER_DB");
    if (!serverDb.isEmpty())
        return QDir::fromNativeSeparators(serverDb);

    const QString snapshotDb = cleanEnvValue("AI_CUSTOMER_SERVICE_SNAPSHOT_DB");
    if (!snapshotDb.isEmpty())
        return QDir::fromNativeSeparators(snapshotDb);

    const QString legacyDb = cleanEnvValue("AI_CUSTOMER_SERVICE_DB");
    if (!legacyDb.isEmpty())
        return QDir::fromNativeSeparators(legacyDb);

    return QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/database/app_data.db");
}

QString AppDataUiStateDao::draftForConversation(const QString& platform, const QString& conversationKey) const
{
    const QString normalized = normalizedPlatform(platform);
    const QString key = conversationKey.trimmed();
    if (normalized.isEmpty() || key.isEmpty())
        return {};

    QSqlDatabase db;
    QString connectionName;
    if (!openAppDataConnection(db, connectionName))
        return {};

    QString result;
    {
        if (!ensureUiDraftTable(db)) {
            closeAppDataConnection(db, connectionName);
            return {};
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT content FROM ui_conversation_drafts "
            "WHERE platform = :platform AND conversation_key = :conversation_key "
            "LIMIT 1"));
        q.bindValue(QStringLiteral(":platform"), normalized);
        q.bindValue(QStringLiteral(":conversation_key"), key);
        if (!q.exec()) {
            qWarning() << "AppDataUiStateDao::draftForConversation failed:" << q.lastError().text();
        } else if (q.next()) {
            result = q.value(0).toString();
        }
    }
    closeAppDataConnection(db, connectionName);
    return result;
}

bool AppDataUiStateDao::saveDraft(const QString& platform,
                                  const QString& conversationKey,
                                  const QString& content) const
{
    const QString normalized = normalizedPlatform(platform);
    const QString key = conversationKey.trimmed();
    if (normalized.isEmpty() || key.isEmpty())
        return false;
    if (content.isEmpty())
        return clearDraft(normalized, key);

    QSqlDatabase db;
    QString connectionName;
    if (!openAppDataConnection(db, connectionName))
        return false;

    bool ok = false;
    {
        if (!ensureUiDraftTable(db)) {
            closeAppDataConnection(db, connectionName);
            return false;
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO ui_conversation_drafts "
            "(platform, conversation_key, content, updated_at) "
            "VALUES (:platform, :conversation_key, :content, datetime('now','localtime'))"));
        q.bindValue(QStringLiteral(":platform"), normalized);
        q.bindValue(QStringLiteral(":conversation_key"), key);
        q.bindValue(QStringLiteral(":content"), content);
        ok = q.exec();
        if (!ok)
            qWarning() << "AppDataUiStateDao::saveDraft failed:" << q.lastError().text();
    }
    closeAppDataConnection(db, connectionName);
    return ok;
}

bool AppDataUiStateDao::clearDraft(const QString& platform, const QString& conversationKey) const
{
    const QString normalized = normalizedPlatform(platform);
    const QString key = conversationKey.trimmed();
    if (normalized.isEmpty() || key.isEmpty())
        return false;

    QSqlDatabase db;
    QString connectionName;
    if (!openAppDataConnection(db, connectionName))
        return false;

    bool ok = false;
    {
        if (!ensureUiDraftTable(db)) {
            closeAppDataConnection(db, connectionName);
            return false;
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "DELETE FROM ui_conversation_drafts "
            "WHERE platform = :platform AND conversation_key = :conversation_key"));
        q.bindValue(QStringLiteral(":platform"), normalized);
        q.bindValue(QStringLiteral(":conversation_key"), key);
        ok = q.exec();
        if (!ok)
            qWarning() << "AppDataUiStateDao::clearDraft failed:" << q.lastError().text();
    }
    closeAppDataConnection(db, connectionName);
    return ok;
}

bool AppDataUiStateDao::lastSelectedConversation(QString* platform, QString* conversationKey) const
{
    if (platform)
        platform->clear();
    if (conversationKey)
        conversationKey->clear();

    QSqlDatabase db;
    QString connectionName;
    if (!openAppDataConnection(db, connectionName))
        return false;

    bool found = false;
    {
        if (!ensureUiStateTable(db)) {
            closeAppDataConnection(db, connectionName);
            return false;
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT value FROM ui_state WHERE key = :key LIMIT 1"));
        q.bindValue(QStringLiteral(":key"), lastSelectedConversationStateKey());
        if (!q.exec()) {
            qWarning() << "AppDataUiStateDao::lastSelectedConversation failed:" << q.lastError().text();
        } else if (q.next()) {
            const QJsonDocument doc = QJsonDocument::fromJson(q.value(0).toString().toUtf8());
            const QJsonObject obj = doc.object();
            const QString parsedPlatform = normalizedPlatform(obj.value(QStringLiteral("platform")).toString());
            const QString parsedKey = obj.value(QStringLiteral("conversation_key")).toString().trimmed();
            if (!parsedPlatform.isEmpty() && !parsedKey.isEmpty()) {
                if (platform)
                    *platform = parsedPlatform;
                if (conversationKey)
                    *conversationKey = parsedKey;
                found = true;
            }
        }
    }
    closeAppDataConnection(db, connectionName);
    return found;
}

bool AppDataUiStateDao::saveLastSelectedConversation(const QString& platform,
                                                     const QString& conversationKey) const
{
    const QString normalized = normalizedPlatform(platform);
    const QString key = conversationKey.trimmed();
    if (normalized.isEmpty() || key.isEmpty())
        return clearLastSelectedConversation();

    QJsonObject obj;
    obj.insert(QStringLiteral("platform"), normalized);
    obj.insert(QStringLiteral("conversation_key"), key);
    const QString value = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));

    QSqlDatabase db;
    QString connectionName;
    if (!openAppDataConnection(db, connectionName))
        return false;

    bool ok = false;
    {
        if (!ensureUiStateTable(db)) {
            closeAppDataConnection(db, connectionName);
            return false;
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO ui_state (key, value, updated_at) "
            "VALUES (:key, :value, datetime('now','localtime'))"));
        q.bindValue(QStringLiteral(":key"), lastSelectedConversationStateKey());
        q.bindValue(QStringLiteral(":value"), value);
        ok = q.exec();
        if (!ok)
            qWarning() << "AppDataUiStateDao::saveLastSelectedConversation failed:" << q.lastError().text();
    }
    closeAppDataConnection(db, connectionName);
    return ok;
}

bool AppDataUiStateDao::clearLastSelectedConversation() const
{
    QSqlDatabase db;
    QString connectionName;
    if (!openAppDataConnection(db, connectionName))
        return false;

    bool ok = false;
    {
        if (!ensureUiStateTable(db)) {
            closeAppDataConnection(db, connectionName);
            return false;
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM ui_state WHERE key = :key"));
        q.bindValue(QStringLiteral(":key"), lastSelectedConversationStateKey());
        ok = q.exec();
        if (!ok)
            qWarning() << "AppDataUiStateDao::clearLastSelectedConversation failed:" << q.lastError().text();
    }
    closeAppDataConnection(db, connectionName);
    return ok;
}
