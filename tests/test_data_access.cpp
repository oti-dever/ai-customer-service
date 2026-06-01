#include <QtTest>

#include "data/conversationdao.h"
#include "data/database.h"
#include "data/messagedao.h"
#include "testdatabase.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

namespace {

QStringList tableColumns(QSqlDatabase db, const QString& tableName)
{
    QStringList columns;
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName));
    while (q.next())
        columns.append(q.value(1).toString());
    return columns;
}

} // namespace

class TestDataAccess : public QObject
{
    Q_OBJECT

private slots:
    void conversation_roundtripAndUnread();
    void message_latestInboundSnapshotAndClear();
    void database_runMigrations_upgradesLegacySchema();
};

void TestDataAccess::conversation_roundtripAndUnread()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("wechat_pc"),
                                      QStringLiteral("conv-001"),
                                      QStringLiteral("张三"));
    QVERIFY(convId > 0);

    auto conv = convDao.findByPlatformId(QStringLiteral("wechat_pc"), QStringLiteral("conv-001"));
    QVERIFY(conv.has_value());
    QCOMPARE(conv->customerName, QStringLiteral("张三"));
    QCOMPARE(conv->status, QStringLiteral("open"));

    QVERIFY(convDao.incrementUnread(convId));
    QVERIFY(convDao.incrementUnread(convId));
    QVERIFY(convDao.setStatus(convId, QStringLiteral("closed")));

    conv = convDao.findById(convId);
    QVERIFY(conv.has_value());
    QCOMPARE(conv->unreadCount, 2);
    QCOMPARE(conv->status, QStringLiteral("closed"));

    QVERIFY(convDao.clearUnread(convId));
    conv = convDao.findById(convId);
    QVERIFY(conv.has_value());
    QCOMPARE(conv->unreadCount, 0);
}

void TestDataAccess::message_latestInboundSnapshotAndClear()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = dir.filePath(QStringLiteral("chat.png"));

    ConversationDao convDao;
    MessageDao msgDao;
    const int convId = convDao.create(QStringLiteral("test_platform"),
                                      QStringLiteral("conv-002"),
                                      QStringLiteral("李四"));
    QVERIFY(convId > 0);

    QVERIFY(msgDao.create(convId,
                          QStringLiteral("out"),
                          QStringLiteral("客服回复"),
                          QStringLiteral("agent")) > 0);
    QVERIFY(msgDao.create(convId,
                          QStringLiteral("in"),
                          QStringLiteral(""),
                          QStringLiteral("customer"),
                          QStringLiteral("msg-img"),
                          1,
                          QString(),
                          QStringLiteral("客户"),
                          QString(),
                          imagePath) > 0);
    QVERIFY(msgDao.create(convId,
                          QStringLiteral("in"),
                          QStringLiteral("最后一条客户消息"),
                          QStringLiteral("customer"),
                          QStringLiteral("msg-text")) > 0);

    const auto snapshot = msgDao.latestInboundSnapshot(convId);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->content, QStringLiteral("最后一条客户消息"));
    QCOMPARE(snapshot->contentImagePath, QString());

    const auto lastInbound = msgDao.latestInboundContent(convId);
    QVERIFY(lastInbound.has_value());
    QCOMPARE(*lastInbound, QStringLiteral("最后一条客户消息"));

    QVERIFY(msgDao.clearAllForConversation(convId));
    QCOMPARE(msgDao.listByConversation(convId).size(), 0);
    QVERIFY(!msgDao.latestInboundSnapshot(convId).has_value());
}

void TestDataAccess::database_runMigrations_upgradesLegacySchema()
{
    Database::getInstance().close();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("legacy.db"));

    {
        const QString connName = QStringLiteral("legacy_schema_builder");
        {
            QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            legacy.setDatabaseName(dbPath);
            QVERIFY(legacy.open());

            QSqlQuery q(legacy);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE users ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "username TEXT UNIQUE NOT NULL,"
                "password_hash TEXT NOT NULL,"
                "salt TEXT NOT NULL,"
                "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE messages ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "conversation_id INTEGER NOT NULL,"
                "direction TEXT NOT NULL,"
                "content TEXT NOT NULL,"
                "sender TEXT NOT NULL,"
                "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                "platform_msg_id TEXT,"
                "sync_status INTEGER NOT NULL DEFAULT 1,"
                "error_reason TEXT DEFAULT '')")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE rpa_inbox_messages ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "platform TEXT NOT NULL,"
                "platform_conversation_id TEXT NOT NULL,"
                "customer_name TEXT NOT NULL,"
                "content TEXT NOT NULL,"
                "created_at DATETIME,"
                "platform_msg_id TEXT NOT NULL,"
                "consume_status INTEGER NOT NULL DEFAULT 0,"
                "error_reason TEXT DEFAULT '')")));
            legacy.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    QVERIFY(Database::getInstance().open(dbPath));
    const QSqlDatabase db = Database::getInstance().connection();
    QVERIFY(tableColumns(db, QStringLiteral("users")).contains(QStringLiteral("display_name")));
    QVERIFY(tableColumns(db, QStringLiteral("users")).contains(QStringLiteral("bio")));
    QVERIFY(tableColumns(db, QStringLiteral("users")).contains(QStringLiteral("avatar_path")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("sender_name")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("original_timestamp")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("content_image_path")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("source_type")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("confidence")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("verification_status")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("content_type")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("observed_at")));
    QVERIFY(tableColumns(db, QStringLiteral("rpa_inbox_messages")).contains(QStringLiteral("sender_name")));
    QVERIFY(tableColumns(db, QStringLiteral("rpa_inbox_messages")).contains(QStringLiteral("original_timestamp")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_conversations")).contains(QStringLiteral("wechat_account_id")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_conversations")).contains(QStringLiteral("wechat_conversation_key")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_conversations")).contains(QStringLiteral("raw_payload_json")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_messages")).contains(QStringLiteral("wechat_account_id")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_messages")).contains(QStringLiteral("wechat_conversation_key")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_messages")).contains(QStringLiteral("raw_payload_json")));
    QVERIFY(tableColumns(db, QStringLiteral("rpa_inbox_messages")).contains(QStringLiteral("content_image_path")));
    QVERIFY(tableColumns(db, QStringLiteral("rpa_inbox_messages")).contains(QStringLiteral("source_type")));
    QVERIFY(tableColumns(db, QStringLiteral("rpa_inbox_messages")).contains(QStringLiteral("confidence")));
    QVERIFY(tableColumns(db, QStringLiteral("rpa_inbox_messages")).contains(QStringLiteral("verification_status")));
    QVERIFY(tableColumns(db, QStringLiteral("rpa_inbox_messages")).contains(QStringLiteral("content_type")));
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("account_id")));
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("source_type")));
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("confidence")));
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("updated_at")));
    Database::getInstance().close();
}

QTEST_MAIN(TestDataAccess)
#include "test_data_access.moc"
