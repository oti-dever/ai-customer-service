#include "database.h"
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

bool Database::open(const QString& path)
{
    if (m_path.isEmpty()) {
        if (path.isEmpty()) {
            QString dataDir = QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/database");
            QDir().mkpath(dataDir);
            m_path = dataDir + QDir::separator() + "app.db";
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
    qDebug() << "数据库打开成功";

    // Multi-process read/write (Qt + Python RPA) recommended defaults
    {
        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        pragma.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
        pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    }
    return runMigrations();
}

void Database::close()
{
    QSqlDatabase::database().close();
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

    // 必须成功的迁移（CREATE TABLE/INDEX IF NOT EXISTS）
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
        "  customer_name TEXT NOT NULL,"
        "  last_message TEXT DEFAULT '',"
        "  last_time DATETIME,"
        "  unread_count INTEGER DEFAULT 0,"
        "  status TEXT DEFAULT 'open',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE(platform, platform_conversation_id)"
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
        "  FOREIGN KEY(conversation_id) REFERENCES conversations(id)"
        ")",

        "CREATE TABLE IF NOT EXISTS rpa_inbox_messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  platform TEXT NOT NULL,"
        "  platform_conversation_id TEXT NOT NULL,"
        "  customer_name TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  created_at DATETIME,"
        "  platform_msg_id TEXT NOT NULL,"
        "  consume_status INTEGER NOT NULL DEFAULT 0,"
        "  error_reason TEXT DEFAULT '',"
        "  sender_name TEXT DEFAULT '',"
        "  original_timestamp TEXT DEFAULT ''"
        ")",

        "CREATE INDEX IF NOT EXISTS idx_messages_conv_id ON messages(conversation_id)",
        "CREATE INDEX IF NOT EXISTS idx_conversations_status ON conversations(status)",

        "CREATE UNIQUE INDEX IF NOT EXISTS idx_inbox_platform_msg_id "
        "  ON rpa_inbox_messages(platform, platform_msg_id)",
        "CREATE INDEX IF NOT EXISTS idx_inbox_consume_status "
        "  ON rpa_inbox_messages(platform, consume_status, id)",

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
        "ALTER TABLE rpa_inbox_messages ADD COLUMN sender_name TEXT DEFAULT ''",
        "ALTER TABLE rpa_inbox_messages ADD COLUMN original_timestamp TEXT DEFAULT ''",
        "ALTER TABLE users ADD COLUMN display_name TEXT DEFAULT ''",
        "ALTER TABLE users ADD COLUMN bio TEXT DEFAULT ''",
        "ALTER TABLE users ADD COLUMN avatar_path TEXT DEFAULT ''",
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
