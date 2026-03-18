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

    const char* migrations[] = {
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
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  platform_msg_id TEXT,"
        "  sync_status INTEGER NOT NULL DEFAULT 1,"
        "  error_reason TEXT DEFAULT '',"
        "  FOREIGN KEY(conversation_id) REFERENCES conversations(id)"
        ")",

        // Python Reader -> Qt consumer inbox queue for inbound messages
        "CREATE TABLE IF NOT EXISTS rpa_inbox_messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  platform TEXT NOT NULL,"
        "  platform_conversation_id TEXT NOT NULL,"
        "  customer_name TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  created_at DATETIME,"
        "  platform_msg_id TEXT NOT NULL,"
        "  consume_status INTEGER NOT NULL DEFAULT 0,"
        "  error_reason TEXT DEFAULT ''"
        ")",

        "CREATE INDEX IF NOT EXISTS idx_messages_conv_id ON messages(conversation_id)",
        "CREATE INDEX IF NOT EXISTS idx_conversations_status ON conversations(status)",

        "CREATE UNIQUE INDEX IF NOT EXISTS idx_inbox_platform_msg_id "
        "  ON rpa_inbox_messages(platform, platform_msg_id)",
        "CREATE INDEX IF NOT EXISTS idx_inbox_consume_status "
        "  ON rpa_inbox_messages(platform, consume_status, id)",
    };

    for (const char* sql : migrations) {
        if (!q.exec(sql)) {
            qWarning() << "数据库迁移失败:" << q.lastError().text() << "\nSQL:" << sql;
            return false;
        }
    }
    qInfo() << "数据库迁移完成（共" << std::size(migrations) << "条）";

    return true;
}
