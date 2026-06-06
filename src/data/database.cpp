#include "database.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDebug>
#include <QStringList>

namespace {

QString defaultDatabasePath()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDir.isEmpty())
        dataDir = QDir::homePath() + QStringLiteral("/.yy-ai-customer-service");
    QDir().mkpath(dataDir);
    return dataDir + QDir::separator() + QStringLiteral("client_cache.db");
}

bool copyDatabaseWithSidecars(const QString& sourcePath, const QString& targetPath)
{
    if (!QFileInfo::exists(sourcePath))
        return false;

    const QFileInfo targetInfo(targetPath);
    QDir().mkpath(targetInfo.absolutePath());
    if (!QFile::copy(sourcePath, targetPath))
        return false;

    const QStringList suffixes = {
        QStringLiteral("-wal"),
        QStringLiteral("-shm"),
        QStringLiteral("-journal")
    };
    for (const QString& suffix : suffixes) {
        const QString sourceSidecar = sourcePath + suffix;
        const QString targetSidecar = targetPath + suffix;
        if (QFileInfo::exists(sourceSidecar) && !QFileInfo::exists(targetSidecar))
            QFile::copy(sourceSidecar, targetSidecar);
    }
    return true;
}

void migrateLegacyDatabaseIfNeeded(const QString& targetPath)
{
    const QFileInfo targetInfo(targetPath);
    if (targetInfo.exists())
        return;

    const QString appDataLegacyPath =
        targetInfo.absolutePath() + QDir::separator() + QStringLiteral("app.db");
    if (copyDatabaseWithSidecars(appDataLegacyPath, targetPath)) {
        qInfo() << "[Database] migrated legacy AppData cache database:"
                << appDataLegacyPath << "->" << targetPath;
        return;
    }

    const QString legacyDir = QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/database");
    const QString legacyPath = legacyDir + QDir::separator() + QStringLiteral("app.db");
    if (!QFileInfo::exists(legacyPath))
        return;

    QDir().mkpath(targetInfo.absolutePath());
    if (QFile::copy(legacyPath, targetPath))
        qInfo() << "已迁移旧数据库到应用数据目录:" << targetPath;

    const QStringList sidecars = {
        QStringLiteral(".db-wal"),
        QStringLiteral(".db-shm"),
        QStringLiteral(".db-journal")
    };
    for (const QString& suffix : sidecars) {
        const QString legacySidecar = legacyPath + suffix.mid(3);
        const QString targetSidecar = targetPath + suffix.mid(3);
        if (!QFileInfo::exists(legacySidecar) || QFileInfo::exists(targetSidecar))
            continue;
        QFile::copy(legacySidecar, targetSidecar);
    }
}

} // namespace

bool Database::open(const QString& path)
{
    if (m_path.isEmpty()) {
        if (path.isEmpty()) {
            m_path = defaultDatabasePath();
            migrateLegacyDatabaseIfNeeded(m_path);
        } else {
            m_path = path;
        }
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(m_path);

    if (!db.open()) {
        qWarning() << "数据库打开失败:" << db.lastError().text();
        return false;
    }
    qInfo() << "[Database] SQLite path:" << m_path << "(client local cache)";


    // Client cache uses SQLite WAL for local recovery.
    {
        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        pragma.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
        pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    }
    if (!runMigrations())
        return false;
    return normalizePlatformConversationKeys();
}

void Database::close()
{
    const QString connectionName = QSqlDatabase::defaultConnection;
    if (QSqlDatabase::contains(connectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(connectionName, false);
            if (db.isValid())
                db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }
    m_path.clear();
}

bool Database::isOpen() const
{
    return QSqlDatabase::database().isOpen();
}

QSqlDatabase Database::connection() const
{
    return QSqlDatabase::database();
}

bool Database::runMigrations()
{
    QSqlQuery q(connection());

    // Required migrations must succeed.
    const char* requiredMigrations[] = {
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT UNIQUE NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  salt TEXT NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")",

        "CREATE TABLE IF NOT EXISTS conversations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  platform TEXT NOT NULL,"
        "  platform_conversation_id TEXT,"
        "  account_id TEXT DEFAULT '',"
        "  customer_name TEXT NOT NULL,"
        "  last_message TEXT DEFAULT '',"
        "  last_time DATETIME,"
        "  unread_count INTEGER DEFAULT 0,"
        "  status TEXT DEFAULT 'new',"
        "  source_type TEXT NOT NULL DEFAULT 'mock',"
        "  confidence INTEGER NOT NULL DEFAULT 100,"
        "  cache_scope TEXT NOT NULL DEFAULT 'local_cache',"
        "  cache_origin TEXT NOT NULL DEFAULT 'legacy_runtime',"
        "  updated_at DATETIME,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE(platform, platform_conversation_id)"
        ")",

        "CREATE TABLE IF NOT EXISTS conversation_drafts ("
        "  conversation_id INTEGER PRIMARY KEY,"
        "  content TEXT NOT NULL DEFAULT '',"
        "  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE"
        ")",

        "CREATE TABLE IF NOT EXISTS app_state ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT DEFAULT '',"
        "  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")",

        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  conversation_id INTEGER NOT NULL,"
        "  direction TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  sender TEXT NOT NULL,"
        "  sender_name TEXT DEFAULT '',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  platform_msg_id TEXT,"
        "  sync_status INTEGER NOT NULL DEFAULT 1,"
        "  error_reason TEXT DEFAULT '',"
        "  original_timestamp TEXT DEFAULT '',"
        "  source_type TEXT NOT NULL DEFAULT 'mock',"
        "  confidence INTEGER NOT NULL DEFAULT 100,"
        "  verification_status TEXT NOT NULL DEFAULT 'unverified',"
        "  content_type TEXT NOT NULL DEFAULT 'text',"
        "  observed_at DATETIME,"
        "  client_message_id TEXT DEFAULT '',"
        "  cache_scope TEXT NOT NULL DEFAULT 'local_cache',"
        "  cache_origin TEXT NOT NULL DEFAULT 'legacy_runtime',"
        "  FOREIGN KEY(conversation_id) REFERENCES conversations(id)"
        ")",

        "CREATE INDEX IF NOT EXISTS idx_messages_conv_id ON messages(conversation_id)",
        "CREATE INDEX IF NOT EXISTS idx_conversations_status ON conversations(status)",

        "CREATE TABLE IF NOT EXISTS message_send_events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  message_id INTEGER NOT NULL,"
        "  conversation_id INTEGER NOT NULL,"
        "  phase TEXT NOT NULL,"
        "  detail TEXT DEFAULT '',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY(message_id) REFERENCES messages(id)"
        ")",
        "CREATE INDEX IF NOT EXISTS idx_send_events_conv_id ON message_send_events(conversation_id)",
        "CREATE INDEX IF NOT EXISTS idx_send_events_message_id ON message_send_events(message_id)",

        "CREATE TABLE IF NOT EXISTS wechat_conversations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  conversation_id INTEGER NOT NULL UNIQUE,"
        "  wechat_account_id TEXT DEFAULT '',"
        "  wechat_conversation_key TEXT DEFAULT '',"
        "  display_name TEXT DEFAULT '',"
        "  session_control_hash TEXT DEFAULT '',"
        "  last_unread_badge INTEGER DEFAULT 0,"
        "  last_observed_at DATETIME,"
        "  last_health_status TEXT DEFAULT '',"
        "  raw_payload_json TEXT DEFAULT '',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE"
        ")",
        "CREATE INDEX IF NOT EXISTS idx_wechat_conversations_key "
        "  ON wechat_conversations(wechat_account_id, wechat_conversation_key)",

        "CREATE TABLE IF NOT EXISTS wechat_messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  message_id INTEGER NOT NULL UNIQUE,"
        "  conversation_id INTEGER NOT NULL,"
        "  wechat_account_id TEXT DEFAULT '',"
        "  wechat_conversation_key TEXT DEFAULT '',"
        "  wechat_display_name TEXT DEFAULT '',"
        "  platform_msg_id TEXT DEFAULT '',"
        "  direction TEXT DEFAULT '',"
        "  sender_role TEXT DEFAULT '',"
        "  raw_control_name TEXT DEFAULT '',"
        "  raw_control_type TEXT DEFAULT '',"
        "  role_method TEXT DEFAULT '',"
        "  role_confidence REAL DEFAULT 0,"
        "  bubble_rect TEXT DEFAULT '',"
        "  message_list_rect TEXT DEFAULT '',"
        "  observation_method TEXT DEFAULT '',"
        "  evidence_ref TEXT DEFAULT '',"
        "  raw_payload_json TEXT DEFAULT '',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE,"
        "  FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE"
        ")",
        "CREATE INDEX IF NOT EXISTS idx_wechat_messages_conv_id ON wechat_messages(conversation_id)",
        "CREATE INDEX IF NOT EXISTS idx_wechat_messages_platform_msg_id ON wechat_messages(platform_msg_id)",

        "CREATE TABLE IF NOT EXISTS ai_assistant_sessions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL,"
        "  model_key TEXT NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,"
        "  UNIQUE(user_id, model_key)"
        ")",

        "CREATE TABLE IF NOT EXISTS ai_assistant_messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id INTEGER NOT NULL,"
        "  role TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY(session_id) REFERENCES ai_assistant_sessions(id) ON DELETE CASCADE"
        ")",

        "CREATE INDEX IF NOT EXISTS idx_ai_assistant_messages_session_id "
        "  ON ai_assistant_messages(session_id, id)",
    };

    // 可选迁移（ALTER TABLE ADD COLUMN，列已存在时会失败，忽略错误）
    const char* optionalMigrations[] = {
        "ALTER TABLE messages ADD COLUMN sender_name TEXT DEFAULT ''",
        "ALTER TABLE messages ADD COLUMN original_timestamp TEXT DEFAULT ''",
        "ALTER TABLE users ADD COLUMN display_name TEXT DEFAULT ''",
        "ALTER TABLE users ADD COLUMN bio TEXT DEFAULT ''",
        "ALTER TABLE users ADD COLUMN avatar_path TEXT DEFAULT ''",
        "ALTER TABLE messages ADD COLUMN content_image_path TEXT DEFAULT ''",
        "ALTER TABLE rpa_inbox_messages ADD COLUMN sender_name TEXT DEFAULT ''",
        "ALTER TABLE rpa_inbox_messages ADD COLUMN original_timestamp TEXT DEFAULT ''",
        "ALTER TABLE rpa_inbox_messages ADD COLUMN content_image_path TEXT DEFAULT ''",
        "ALTER TABLE rpa_inbox_messages ADD COLUMN source_type TEXT NOT NULL DEFAULT 'mock'",
        "ALTER TABLE rpa_inbox_messages ADD COLUMN confidence INTEGER NOT NULL DEFAULT 100",
        "ALTER TABLE rpa_inbox_messages ADD COLUMN verification_status TEXT NOT NULL DEFAULT 'unverified'",
        "ALTER TABLE rpa_inbox_messages ADD COLUMN content_type TEXT NOT NULL DEFAULT 'text'",
        "ALTER TABLE conversations ADD COLUMN account_id TEXT DEFAULT ''",
        "ALTER TABLE conversations ADD COLUMN source_type TEXT NOT NULL DEFAULT 'mock'",
        "ALTER TABLE conversations ADD COLUMN confidence INTEGER NOT NULL DEFAULT 100",
        "ALTER TABLE conversations ADD COLUMN cache_scope TEXT NOT NULL DEFAULT 'local_cache'",
        "ALTER TABLE conversations ADD COLUMN cache_origin TEXT NOT NULL DEFAULT 'legacy_runtime'",
        "ALTER TABLE conversations ADD COLUMN updated_at DATETIME",
        "ALTER TABLE messages ADD COLUMN source_type TEXT NOT NULL DEFAULT 'mock'",
        "ALTER TABLE messages ADD COLUMN confidence INTEGER NOT NULL DEFAULT 100",
        "ALTER TABLE messages ADD COLUMN verification_status TEXT NOT NULL DEFAULT 'unverified'",
        "ALTER TABLE messages ADD COLUMN content_type TEXT NOT NULL DEFAULT 'text'",
        "ALTER TABLE messages ADD COLUMN observed_at DATETIME",
        "ALTER TABLE messages ADD COLUMN client_message_id TEXT DEFAULT ''",
        "ALTER TABLE messages ADD COLUMN cache_scope TEXT NOT NULL DEFAULT 'local_cache'",
        "ALTER TABLE messages ADD COLUMN cache_origin TEXT NOT NULL DEFAULT 'legacy_runtime'",
        "ALTER TABLE wechat_conversations ADD COLUMN session_control_hash TEXT DEFAULT ''",
        "ALTER TABLE wechat_conversations ADD COLUMN last_unread_badge INTEGER DEFAULT 0",
        "ALTER TABLE wechat_conversations ADD COLUMN last_observed_at DATETIME",
        "ALTER TABLE wechat_conversations ADD COLUMN last_health_status TEXT DEFAULT ''",
        "ALTER TABLE wechat_conversations ADD COLUMN raw_payload_json TEXT DEFAULT ''",
        "ALTER TABLE wechat_conversations ADD COLUMN created_at DATETIME DEFAULT CURRENT_TIMESTAMP",
        "ALTER TABLE wechat_conversations ADD COLUMN updated_at DATETIME DEFAULT CURRENT_TIMESTAMP",
        "ALTER TABLE wechat_messages ADD COLUMN wechat_account_id TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN wechat_conversation_key TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN wechat_display_name TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN direction TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN sender_role TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN raw_control_name TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN raw_control_type TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN role_method TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN role_confidence REAL DEFAULT 0",
        "ALTER TABLE wechat_messages ADD COLUMN bubble_rect TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN message_list_rect TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN observation_method TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN evidence_ref TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN raw_payload_json TEXT DEFAULT ''",
        "ALTER TABLE wechat_messages ADD COLUMN created_at DATETIME DEFAULT CURRENT_TIMESTAMP",
    };

    for (const char* sql : requiredMigrations) {
        if (!q.exec(sql)) {
            qWarning() << "数据库迁移失败:" << q.lastError().text() << "\nSQL:" << sql;
            return false;
        }
    }

    for (const char* sql : optionalMigrations) {
        if (!q.exec(sql)) {
            // ALTER TABLE ADD COLUMN 失败通常是因为列已存在，忽略即可
            qDebug() << "可选迁移跳过（列可能已存在）:" << sql;
        }
    }

    qInfo() << "数据库迁移完成";
    return true;
}

bool Database::normalizePlatformConversationKeys()
{
    QSqlDatabase db = connection();
    if (!db.transaction()) {
        qWarning() << "[Database] normalize conversation keys transaction failed";
        return false;
    }

    const QStringList platforms = {
        QStringLiteral("wechat"),
        QStringLiteral("qianniu"),
    };

    for (const QString& platform : platforms) {
        QSqlQuery canonicalQuery(db);
        canonicalQuery.prepare(QStringLiteral(
            "SELECT id, platform_conversation_id FROM conversations "
            "WHERE platform = :platform AND platform_conversation_id LIKE :prefix"));
        canonicalQuery.bindValue(QStringLiteral(":platform"), platform);
        canonicalQuery.bindValue(QStringLiteral(":prefix"), platform + QStringLiteral(":%"));
        if (!canonicalQuery.exec()) {
            qWarning() << "[Database] query canonical conversations failed:" << canonicalQuery.lastError().text();
            db.rollback();
            return false;
        }

        while (canonicalQuery.next()) {
            const int canonicalId = canonicalQuery.value(0).toInt();
            const QString canonicalKey = canonicalQuery.value(1).toString();
            const int lastSep = canonicalKey.lastIndexOf(QLatin1Char(':'));
            if (lastSep < 0 || lastSep + 1 >= canonicalKey.size())
                continue;
            const QString displayKey = canonicalKey.mid(lastSep + 1);
            if (displayKey.isEmpty() || displayKey == canonicalKey)
                continue;

            QSqlQuery shortQuery(db);
            shortQuery.prepare(QStringLiteral(
                "SELECT id FROM conversations "
                "WHERE platform = :platform AND platform_conversation_id = :shortKey "
                "LIMIT 1"));
            shortQuery.bindValue(QStringLiteral(":platform"), platform);
            shortQuery.bindValue(QStringLiteral(":shortKey"), displayKey);
            if (!shortQuery.exec()) {
                qWarning() << "[Database] query short conversation failed:" << shortQuery.lastError().text();
                db.rollback();
                return false;
            }
            if (!shortQuery.next())
                continue;

            const int shortId = shortQuery.value(0).toInt();
            if (shortId == canonicalId)
                continue;

            QSqlQuery moveMessages(db);
            moveMessages.prepare(QStringLiteral(
                "UPDATE messages SET conversation_id = :canonicalId WHERE conversation_id = :shortId"));
            moveMessages.bindValue(QStringLiteral(":canonicalId"), canonicalId);
            moveMessages.bindValue(QStringLiteral(":shortId"), shortId);
            if (!moveMessages.exec()) {
                qWarning() << "[Database] move messages to canonical conversation failed:" << moveMessages.lastError().text();
                db.rollback();
                return false;
            }

            QSqlQuery moveDraft(db);
            moveDraft.prepare(QStringLiteral(
                "UPDATE OR IGNORE conversation_drafts SET conversation_id = :canonicalId WHERE conversation_id = :shortId"));
            moveDraft.bindValue(QStringLiteral(":canonicalId"), canonicalId);
            moveDraft.bindValue(QStringLiteral(":shortId"), shortId);
            if (!moveDraft.exec()) {
                qWarning() << "[Database] move conversation draft failed:" << moveDraft.lastError().text();
                db.rollback();
                return false;
            }

            QSqlQuery deleteShortDraft(db);
            deleteShortDraft.prepare(QStringLiteral("DELETE FROM conversation_drafts WHERE conversation_id = :shortId"));
            deleteShortDraft.bindValue(QStringLiteral(":shortId"), shortId);
            if (!deleteShortDraft.exec()) {
                qWarning() << "[Database] delete duplicate conversation draft failed:" << deleteShortDraft.lastError().text();
                db.rollback();
                return false;
            }

            QSqlQuery deleteShort(db);
            deleteShort.prepare(QStringLiteral("DELETE FROM conversations WHERE id = :shortId"));
            deleteShort.bindValue(QStringLiteral(":shortId"), shortId);
            if (!deleteShort.exec()) {
                qWarning() << "[Database] delete short conversation failed:" << deleteShort.lastError().text();
                db.rollback();
                return false;
            }
        }
    }

    if (!db.commit()) {
        qWarning() << "[Database] normalize conversation keys commit failed:" << db.lastError().text();
        return false;
    }
    return true;
}
