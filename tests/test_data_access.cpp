#include <QtTest>

#include "data/conversationdao.h"
#include "data/appdatauistatedao.h"
#include "data/database.h"
#include "data/messagedao.h"
#include "data/wechatmessagedao.h"
#include "testdatabase.h"

#include <QJsonObject>
#include <QScopeGuard>
#include <QSet>
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
    void conversation_fullKeyFindsAndUpgradesLegacyShortKey();
    void message_latestInboundSnapshotAndClear();
    void message_mediaPathFallsBackToEvidenceRef();
    void snapshot_upsertWritesLocalCache();
    void appDataUiState_conversationDraftRoundtrip();
    void database_runMigrations_upgradesLegacySchema();
};

void TestDataAccess::conversation_roundtripAndUnread()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("wechat"),
                                      QStringLiteral("conv-001"),
                                      QStringLiteral("张三"));
    QVERIFY(convId > 0);

    auto conv = convDao.findByPlatformId(QStringLiteral("wechat"), QStringLiteral("conv-001"));
    QVERIFY(conv.has_value());
    QCOMPARE(conv->customerName, QStringLiteral("张三"));
    QCOMPARE(conv->status, QStringLiteral("new"));
    QCOMPARE(conv->cacheScope, QStringLiteral("local_cache"));
    const auto cachedConversations = convDao.listCachedConversations();
    QCOMPARE(cachedConversations.size(), 1);
    QCOMPARE(cachedConversations.first().id, convId);
    QVERIFY(convDao.saveCachedDraft(convId, QStringLiteral("本地草稿")));
    QCOMPARE(convDao.cachedDraftForConversation(convId), QStringLiteral("本地草稿"));
    QVERIFY(convDao.clearCachedDraft(convId));
    QCOMPARE(convDao.cachedDraftForConversation(convId), QString());
    QVERIFY(convDao.setLastSelectedCachedConversationId(convId));
    QCOMPARE(convDao.lastSelectedCachedConversationId(), convId);

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

void TestDataAccess::conversation_fullKeyFindsAndUpgradesLegacyShortKey()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("wechat"),
                                      QStringLiteral("legacy-user"),
                                      QStringLiteral("legacy-user"));
    QVERIFY(convId > 0);

    auto legacy = convDao.findByPlatformId(QStringLiteral("wechat"),
                                           QStringLiteral("wechat:wechat:legacy-user"));
    QVERIFY(legacy.has_value());
    QCOMPARE(legacy->id, convId);
    QCOMPARE(legacy->platformConversationId, QStringLiteral("legacy-user"));

    Models::Conversation observed;
    observed.platformType = Models::PlatformType::WechatPc;
    observed.platformConversationId = QStringLiteral("wechat:wechat:legacy-user");
    observed.accountId = QStringLiteral("wechat");
    observed.title = QStringLiteral("legacy-user");
    observed.status = Models::ConversationStatus::Active;
    observed.sourceType = Models::SourceType::UiObserved;
    observed.confidence = 80;
    observed.createdAt = QDateTime::currentDateTime();
    observed.updatedAt = observed.createdAt;

    QCOMPARE(convDao.upsertObservedCacheConversation(observed), convId);
    const auto upgraded = convDao.findById(convId);
    QVERIFY(upgraded.has_value());
    QCOMPARE(upgraded->platformConversationId, QStringLiteral("wechat:wechat:legacy-user"));
    QCOMPARE(upgraded->customerName, QStringLiteral("legacy-user"));
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

    Models::Message pendingOutbound;
    pendingOutbound.conversationId = convId;
    pendingOutbound.direction = Models::MessageDirection::Outbound;
    pendingOutbound.contentType = Models::MessageContentType::Text;
    pendingOutbound.content = QStringLiteral("待发送消息");
    pendingOutbound.status = Models::MessageStatus::Pending;
    pendingOutbound.sourceType = Models::SourceType::ManualConfirmed;
    pendingOutbound.confidence = Models::defaultConfidence(pendingOutbound.sourceType);
    pendingOutbound.verificationStatus = Models::VerificationStatus::ManualVerified;
    pendingOutbound.clientMessageId = QStringLiteral("local-msg-001");
    const int pendingId = msgDao.createOutboundCacheMessage(pendingOutbound);
    QVERIFY(pendingId > 0);
    const auto pending = msgDao.latestPendingOutboundCacheByClientMessageId(
        convId,
        QStringLiteral("local-msg-001"));
    QVERIFY(pending.has_value());
    QCOMPARE(pending->id, pendingId);
    QCOMPARE(pending->cacheOrigin, QStringLiteral("manual_outbound_cache"));
    QVERIFY(msgDao.updateOutboundCacheDeliveryState(pendingId, 11));
    const auto sent = msgDao.findById(pendingId);
    QVERIFY(sent.has_value());
    QCOMPARE(sent->syncStatus, 11);

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
    const auto cachedMessages = msgDao.listCachedMessages(convId);
    QCOMPARE(cachedMessages.size(), 4);
    const auto lastCached = msgDao.lastCachedMessageForConversation(convId);
    QVERIFY(lastCached.has_value());
    QCOMPARE(lastCached->content, QStringLiteral("最后一条客户消息"));
    const auto lastDirections = msgDao.lastCachedDirectionsByConversation();
    QCOMPARE(lastDirections.value(convId), QStringLiteral("in"));

    const auto snapshot = msgDao.latestInboundSnapshot(convId);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->content, QStringLiteral("最后一条客户消息"));
    QCOMPARE(snapshot->contentImagePath, QString());
    const auto cachedSnapshot = msgDao.latestCachedInboundSnapshot(convId);
    QVERIFY(cachedSnapshot.has_value());
    QCOMPARE(cachedSnapshot->content, QStringLiteral("最后一条客户消息"));

    const auto lastInbound = msgDao.latestInboundContent(convId);
    QVERIFY(lastInbound.has_value());
    QCOMPARE(*lastInbound, QStringLiteral("最后一条客户消息"));
    const auto cachedLastInbound = msgDao.latestCachedInboundContent(convId);
    QVERIFY(cachedLastInbound.has_value());
    QCOMPARE(*cachedLastInbound, QStringLiteral("最后一条客户消息"));

    QVERIFY(msgDao.clearAllForConversation(convId));
    QCOMPARE(msgDao.listByConversation(convId).size(), 0);
    QVERIFY(!msgDao.latestInboundSnapshot(convId).has_value());
}

void TestDataAccess::message_mediaPathFallsBackToEvidenceRef()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    MessageDao msgDao;
    WechatMessageDao wechatDao;

    const int convId = convDao.create(QStringLiteral("wechat"),
                                      QStringLiteral("wechat-media-001"),
                                      QStringLiteral("media-user"));
    QVERIFY(convId > 0);

    Models::Message message;
    message.conversationId = convId;
    message.platformMessageId = QStringLiteral("wechat-media-msg-001");
    message.direction = Models::MessageDirection::Inbound;
    message.contentType = Models::MessageContentType::File;
    message.content = QStringLiteral("文件 demo.pdf 12K 微信电脑版");
    message.status = Models::MessageStatus::Observed;
    message.sourceType = Models::SourceType::UiObserved;
    message.confidence = 90;
    const int messageId = msgDao.createObservedCacheMessage(message);
    QVERIFY(messageId > 0);

    QJsonObject payload;
    payload.insert(QStringLiteral("direction"), QStringLiteral("in"));
    payload.insert(QStringLiteral("content"), message.content);
    payload.insert(QStringLiteral("content_type"), QStringLiteral("file"));
    payload.insert(QStringLiteral("content_image_path"), QString());
    payload.insert(QStringLiteral("evidence_ref"), QStringLiteral("D:/media/demo.pdf"));
    QVERIFY(wechatDao.createMessageExtension(
        messageId,
        convId,
        QStringLiteral("wechat"),
        QStringLiteral("wechat-media-001"),
        QStringLiteral("media-user"),
        QStringLiteral("wechat-media-msg-001"),
        payload));

    const auto persisted = msgDao.findById(messageId);
    QVERIFY(persisted.has_value());
    QCOMPARE(persisted->contentType, QStringLiteral("file"));
    QCOMPARE(persisted->contentImagePath, QStringLiteral("D:/media/demo.pdf"));

    payload.insert(QStringLiteral("evidence_ref"), QStringLiteral("D:/media/demo-updated.pdf"));
    QVERIFY(wechatDao.createMessageExtension(
        messageId,
        convId,
        QStringLiteral("wechat"),
        QStringLiteral("wechat-media-001"),
        QStringLiteral("media-user"),
        QStringLiteral("wechat-media-msg-001"),
        payload));
    const auto updated = msgDao.findById(messageId);
    QVERIFY(updated.has_value());
    QCOMPARE(updated->contentImagePath, QStringLiteral("D:/media/demo-updated.pdf"));
}

void TestDataAccess::snapshot_upsertWritesLocalCache()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    MessageDao msgDao;

    QJsonObject conversation;
    conversation.insert(QStringLiteral("platform"), QStringLiteral("wechat"));
    conversation.insert(QStringLiteral("platform_conversation_id"), QStringLiteral("wechat-snapshot-001"));
    conversation.insert(QStringLiteral("account_id"), QStringLiteral("wechat"));
    conversation.insert(QStringLiteral("customer_name"), QStringLiteral("王五"));
    conversation.insert(QStringLiteral("last_message"), QStringLiteral("服务端快照消息"));
    conversation.insert(QStringLiteral("unread_count"), 2);
    conversation.insert(QStringLiteral("status"), QStringLiteral("active"));
    conversation.insert(QStringLiteral("source_type"), QStringLiteral("ui_observed"));
    conversation.insert(QStringLiteral("confidence"), 90);
    conversation.insert(QStringLiteral("last_time"), QStringLiteral("2026-06-03 12:00:00"));
    conversation.insert(QStringLiteral("created_at"), QStringLiteral("2026-06-03 11:59:59"));
    conversation.insert(QStringLiteral("updated_at"), QStringLiteral("2026-06-03 12:00:01"));
    conversation.insert(QStringLiteral("deleted_at"), QString());

    const int convId = convDao.upsertSnapshotCacheConversation(conversation);
    QVERIFY(convId > 0);
    auto cached = convDao.findByPlatformId(QStringLiteral("wechat"), QStringLiteral("wechat-snapshot-001"));
    QVERIFY(cached.has_value());
    QCOMPARE(cached->customerName, QStringLiteral("王五"));
    QCOMPARE(cached->lastMessage, QStringLiteral("服务端快照消息"));
    QCOMPARE(cached->unreadCount, 2);
    QCOMPARE(cached->cacheOrigin, QStringLiteral("server_snapshot_cache"));
    QCOMPARE(cached->createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
             QStringLiteral("2026-06-03 11:59:59"));

    QJsonObject message;
    message.insert(QStringLiteral("direction"), QStringLiteral("in"));
    message.insert(QStringLiteral("content"), QStringLiteral("服务端快照消息"));
    message.insert(QStringLiteral("sender"), QStringLiteral("customer"));
    message.insert(QStringLiteral("sender_name"), QStringLiteral("王五"));
    message.insert(QStringLiteral("platform_msg_id"), QStringLiteral("server-msg-001"));
    message.insert(QStringLiteral("sync_status"), 1);
    message.insert(QStringLiteral("source_type"), QStringLiteral("ui_observed"));
    message.insert(QStringLiteral("confidence"), 90);
    message.insert(QStringLiteral("verification_status"), QStringLiteral("auto_verified"));
    message.insert(QStringLiteral("content_type"), QStringLiteral("text"));
    message.insert(QStringLiteral("observed_at"), QStringLiteral("2026-06-03 12:00:00"));
    message.insert(QStringLiteral("created_at"), QStringLiteral("2026-06-03 12:00:00"));

    const int firstMessageId = msgDao.upsertSnapshotCacheMessage(convId, message);
    QVERIFY(firstMessageId > 0);
    QCOMPARE(msgDao.listCachedMessages(convId).size(), 1);

    message.insert(QStringLiteral("content"), QStringLiteral("服务端快照消息-更新"));
    const int secondMessageId = msgDao.upsertSnapshotCacheMessage(convId, message);
    QCOMPARE(secondMessageId, firstMessageId);
    const auto messages = msgDao.listCachedMessages(convId);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().content, QStringLiteral("服务端快照消息-更新"));
    QCOMPARE(messages.first().createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
             QStringLiteral("2026-06-03 12:00:00"));
    QCOMPARE(messages.first().observedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
             QStringLiteral("2026-06-03 12:00:00"));
    QCOMPARE(messages.first().cacheOrigin, QStringLiteral("server_snapshot_cache"));

    QJsonObject secondMessage = message;
    secondMessage.insert(QStringLiteral("platform_msg_id"), QStringLiteral("server-msg-002"));
    secondMessage.insert(QStringLiteral("content"), QStringLiteral("会被服务端快照清理"));
    QVERIFY(msgDao.upsertSnapshotCacheMessage(convId, secondMessage) > 0);
    QCOMPARE(msgDao.listCachedMessages(convId).size(), 2);

    QSet<QString> keepPlatformMessageIds;
    keepPlatformMessageIds.insert(QStringLiteral("server-msg-001"));
    const int removedMessages = msgDao.deleteMissingSnapshotCacheMessages(convId, keepPlatformMessageIds, {});
    QCOMPARE(removedMessages, 1);
    QCOMPARE(msgDao.listCachedMessages(convId).size(), 1);

    QJsonObject staleConversation = conversation;
    staleConversation.insert(QStringLiteral("platform_conversation_id"), QStringLiteral("wechat-stale-001"));
    staleConversation.insert(QStringLiteral("customer_name"), QStringLiteral("旧会话"));
    const int staleConvId = convDao.upsertSnapshotCacheConversation(staleConversation);
    QVERIFY(staleConvId > 0);
    QSet<QString> keepConversationIds;
    keepConversationIds.insert(QStringLiteral("wechat-snapshot-001"));
    const int removedConversations = convDao.deleteMissingSnapshotCacheConversations(
        QStringLiteral("wechat"),
        keepConversationIds);
    QCOMPARE(removedConversations, 1);
    QVERIFY(!convDao.findByPlatformId(QStringLiteral("wechat"), QStringLiteral("wechat-stale-001")).has_value());

    QVERIFY(convDao.setSnapshotCursor(QStringLiteral("wechat"), QStringLiteral("2026-06-03 12:05:00")));
    QCOMPARE(convDao.snapshotCursor(QStringLiteral("wechat")), QStringLiteral("2026-06-03 12:05:00"));
    QVERIFY(convDao.setRpaReplayCursor(QStringLiteral("wechat"), QStringLiteral("42")));
    QCOMPARE(convDao.rpaReplayCursor(QStringLiteral("wechat")), QStringLiteral("42"));
}

void TestDataAccess::appDataUiState_conversationDraftRoundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("app_data.db"));

    const QByteArray envName("AI_CUSTOMER_SERVICE_APP_DB");
    const QByteArray oldValue = qgetenv(envName.constData());
    const bool hadOldValue = qEnvironmentVariableIsSet(envName.constData());
    qputenv(envName.constData(), dbPath.toUtf8());
    auto restoreEnv = qScopeGuard([&]() {
        if (hadOldValue)
            qputenv(envName.constData(), oldValue);
        else
            qunsetenv(envName.constData());
    });
    Q_UNUSED(restoreEnv);

    AppDataUiStateDao dao;
    QCOMPARE(AppDataUiStateDao::resolvedAppDataDbPath(), dbPath);
    QCOMPARE(dao.draftForConversation(QStringLiteral("wechat"),
                                      QStringLiteral("wechat:张三")),
             QString());
    QVERIFY(dao.saveDraft(QStringLiteral("wechat"),
                          QStringLiteral("wechat:张三"),
                          QStringLiteral("统一库草稿")));
    QCOMPARE(dao.draftForConversation(QStringLiteral("WECHAT"),
                                      QStringLiteral("wechat:张三")),
             QStringLiteral("统一库草稿"));
    QVERIFY(dao.clearDraft(QStringLiteral("wechat"),
                           QStringLiteral("wechat:张三")));
    QCOMPARE(dao.draftForConversation(QStringLiteral("wechat"),
                                      QStringLiteral("wechat:张三")),
             QString());

    QString platform;
    QString conversationKey;
    QVERIFY(!dao.lastSelectedConversation(&platform, &conversationKey));
    QVERIFY(dao.saveLastSelectedConversation(QStringLiteral("WECHAT"),
                                             QStringLiteral("wechat:张三")));
    QVERIFY(dao.lastSelectedConversation(&platform, &conversationKey));
    QCOMPARE(platform, QStringLiteral("wechat"));
    QCOMPARE(conversationKey, QStringLiteral("wechat:张三"));
    QVERIFY(dao.clearLastSelectedConversation());
    QVERIFY(!dao.lastSelectedConversation(&platform, &conversationKey));
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
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("platform_message_id")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("client_message_id")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("status")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("content_type")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("message_time")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("updated_at")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("deleted_at")));
    QVERIFY(!tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("platform_msg_id")));
    QVERIFY(!tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("sync_status")));
    QVERIFY(!tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("original_timestamp")));
    QVERIFY(!tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("content_image_path")));
    QVERIFY(!tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("source_type")));
    QVERIFY(!tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("confidence")));
    QVERIFY(!tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("verification_status")));
    QVERIFY(!tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("observed_at")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("cache_scope")));
    QVERIFY(tableColumns(db, QStringLiteral("messages")).contains(QStringLiteral("cache_origin")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_conversations")).contains(QStringLiteral("wechat_account_id")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_conversations")).contains(QStringLiteral("wechat_conversation_key")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_conversations")).contains(QStringLiteral("raw_payload_json")));
    QVERIFY(tableColumns(db, QStringLiteral("qianniu_conversations")).contains(QStringLiteral("qianniu_account_id")));
    QVERIFY(tableColumns(db, QStringLiteral("qianniu_conversations")).contains(QStringLiteral("qianniu_conversation_key")));
    QVERIFY(tableColumns(db, QStringLiteral("qianniu_conversations")).contains(QStringLiteral("raw_payload_json")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_messages")).contains(QStringLiteral("wechat_account_id")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_messages")).contains(QStringLiteral("wechat_conversation_key")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_messages")).contains(QStringLiteral("platform_message_id")));
    QVERIFY(tableColumns(db, QStringLiteral("wechat_messages")).contains(QStringLiteral("raw_payload_json")));
    QVERIFY(tableColumns(db, QStringLiteral("qianniu_messages")).contains(QStringLiteral("qianniu_account_id")));
    QVERIFY(tableColumns(db, QStringLiteral("qianniu_messages")).contains(QStringLiteral("qianniu_conversation_key")));
    QVERIFY(tableColumns(db, QStringLiteral("qianniu_messages")).contains(QStringLiteral("platform_message_id")));
    QVERIFY(tableColumns(db, QStringLiteral("qianniu_messages")).contains(QStringLiteral("raw_payload_json")));
    QVERIFY(tableColumns(db, QStringLiteral("rpa_inbox_messages")).isEmpty());
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("account_id")));
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("cache_scope")));
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("cache_origin")));
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("created_at")));
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("updated_at")));
    QVERIFY(tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("deleted_at")));
    QVERIFY(!tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("source_type")));
    QVERIFY(!tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("confidence")));
    QVERIFY(!tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("canonical_conversation_id")));
    QVERIFY(!tableColumns(db, QStringLiteral("conversations")).contains(QStringLiteral("display_name")));
    Database::getInstance().close();
}

QTEST_MAIN(TestDataAccess)
#include "test_data_access.moc"
