#include <QtTest>

#include "core/messagerouter.h"
#include "data/conversationdao.h"
#include "data/database.h"
#include "data/messagedao.h"
#include "services/platforms/iplatformadapter.h"
#include "testdatabase.h"

#include <QDir>
#include <QSqlQuery>
#include <QTemporaryFile>

class FakePlatformAdapter : public IPlatformAdapter
{
    Q_OBJECT

public:
    explicit FakePlatformAdapter(const QString& name,
                                 bool autoAck = false,
                                 QObject* parent = nullptr)
        : IPlatformAdapter(parent)
        , m_name(name)
        , m_autoAck(autoAck)
    {
    }

    QString platformName() const override { return m_name; }
    void connectPlatform() override { m_connected = true; }
    void disconnectPlatform() override { m_connected = false; }
    void startListening() override { }
    void stopListening() override { }
    void sendMessage(const QString& conversationId, const QString& text, const QString& clientMessageId) override
    {
        lastConversationId = conversationId;
        lastText = text;
        lastClientMessageId = clientMessageId;
        if (m_autoAck)
            emit messageSent(conversationId, text, clientMessageId);
    }
    void sendMessagePart(const QString& conversationId,
                         const OutgoingMessagePart& part,
                         const QString& clientMessageId) override
    {
        lastPart = part;
        if (part.type == OutgoingPartType::Text) {
            sendMessage(conversationId, part.text, clientMessageId);
            return;
        }
        lastConversationId = conversationId;
        lastClientMessageId = clientMessageId;
        if (m_autoAck)
            emit messageSent(conversationId, part.fileName, clientMessageId);
    }
    bool isConnected() const override { return m_connected; }

    void emitIncoming(const PlatformMessage& msg) { emit incomingMessage(msg); }
    void emitSendFailed(const QString& conversationId, const QString& reason, const QString& clientMessageId = QString()) { emit sendFailed(conversationId, reason, clientMessageId); }

    QString lastConversationId;
    QString lastText;
    QString lastClientMessageId;
    OutgoingMessagePart lastPart;

private:
    QString m_name;
    bool m_autoAck = false;
    bool m_connected = false;
};

class TestMessageRouter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void incomingMessage_createsConversationAndPersistsMessage();
    void incomingWechatMessage_upgradesLegacyShortConversationKey();
    void incomingQianniuMessage_upgradesLegacyShortConversationKey();
    void sendMessage_routesToAdapterAndStoresPendingMessage();
    void sendMedia_routesToAdapterAndStoresPlatformExtension();
    void sendMessage_autoAck_marksMessageAsSent();
    void sendFailed_mapsBackToConversationIdByAdapterPlatform();
    void sendFailed_marksLatestPendingMessageAsFailed();
};

void TestMessageRouter::initTestCase()
{
    qRegisterMetaType<ConversationInfo>("ConversationInfo");
    qRegisterMetaType<MessageRecord>("MessageRecord");
    qRegisterMetaType<Models::Message>("Models::Message");
    qRegisterMetaType<Models::MessageStatus>("Models::MessageStatus");
}

void TestMessageRouter::incomingMessage_createsConversationAndPersistsMessage()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    MessageRouter router;
    FakePlatformAdapter adapter(QStringLiteral("wechat"));
    router.registerAdapter(&adapter);

    QSignalSpy createdSpy(&router, &MessageRouter::conversationCreated);
    QSignalSpy receivedSpy(&router, &MessageRouter::messageReceived);

    PlatformMessage msg;
    msg.platform = QStringLiteral("wechat");
    msg.platformConversationId = QStringLiteral("wx-conv-1");
    msg.customerName = QStringLiteral("王五");
    msg.content = QStringLiteral("你好");
    msg.direction = QStringLiteral("in");
    msg.sender = QStringLiteral("customer");
    msg.platformMsgId = QStringLiteral("wx-msg-1");
    msg.sourceType = QStringLiteral("ui_observed");
    msg.confidence = 72;

    adapter.emitIncoming(msg);

    QCOMPARE(createdSpy.count(), 1);
    QCOMPARE(receivedSpy.count(), 1);

    ConversationDao convDao;
    auto conv = convDao.findByPlatformId(QStringLiteral("wechat"), QStringLiteral("wx-conv-1"));
    QVERIFY(conv.has_value());
    QCOMPARE(conv->customerName, QStringLiteral("王五"));
    QCOMPARE(conv->unreadCount, 1);
    QCOMPARE(conv->cacheScope, QStringLiteral("local_cache"));
    QCOMPARE(conv->cacheOrigin, QStringLiteral("platform_observed_cache"));

    MessageDao msgDao;
    const auto messages = msgDao.listByConversation(conv->id);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().content, QStringLiteral("你好"));
    QCOMPARE(messages.first().platformMsgId, QStringLiteral("wx-msg-1"));
    QCOMPARE(messages.first().sourceType, QStringLiteral("ui_observed"));
    QCOMPARE(messages.first().confidence, 72);
    QCOMPARE(messages.first().cacheScope, QStringLiteral("local_cache"));
    QCOMPARE(messages.first().cacheOrigin, QStringLiteral("platform_observed_cache"));
}

void TestMessageRouter::incomingWechatMessage_upgradesLegacyShortConversationKey()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("wechat"),
                                      QStringLiteral("legacy-user"),
                                      QStringLiteral("legacy-user"));
    QVERIFY(convId > 0);

    MessageRouter router;
    FakePlatformAdapter adapter(QStringLiteral("wechat"));
    router.registerAdapter(&adapter);

    PlatformMessage msg;
    msg.platform = QStringLiteral("wechat");
    msg.platformConversationId = QStringLiteral("wechat:wechat:legacy-user");
    msg.customerName = QStringLiteral("legacy-user");
    msg.content = QStringLiteral("hello from full key");
    msg.direction = QStringLiteral("in");
    msg.sender = QStringLiteral("customer");
    msg.platformMsgId = QStringLiteral("wechat-full-key-msg-1");
    msg.sourceType = QStringLiteral("ui_observed");
    msg.confidence = 76;
    msg.metadata.insert(QStringLiteral("_event_account_id"), QStringLiteral("wechat"));
    msg.metadata.insert(QStringLiteral("_event_conversation_key"), msg.platformConversationId);

    adapter.emitIncoming(msg);

    auto oldLookup = convDao.findByPlatformId(QStringLiteral("wechat"), QStringLiteral("legacy-user"));
    QVERIFY(!oldLookup.has_value());

    auto upgraded = convDao.findByPlatformId(QStringLiteral("wechat"), QStringLiteral("wechat:wechat:legacy-user"));
    QVERIFY(upgraded.has_value());
    QCOMPARE(upgraded->id, convId);
    QCOMPARE(upgraded->platformConversationId, QStringLiteral("wechat:wechat:legacy-user"));
    QCOMPARE(upgraded->unreadCount, 1);

    MessageDao msgDao;
    const auto messages = msgDao.listByConversation(convId);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().platformMsgId, QStringLiteral("wechat-full-key-msg-1"));

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT wechat_conversation_key FROM wechat_messages WHERE message_id = :mid"));
    q.bindValue(QStringLiteral(":mid"), messages.first().id);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("wechat:wechat:legacy-user"));
}

void TestMessageRouter::incomingQianniuMessage_upgradesLegacyShortConversationKey()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("qianniu"),
                                      QStringLiteral("tb4947894539"),
                                      QStringLiteral("tb4947894539"));
    QVERIFY(convId > 0);

    MessageRouter router;
    FakePlatformAdapter adapter(QStringLiteral("qianniu"));
    router.registerAdapter(&adapter);

    PlatformMessage msg;
    msg.platform = QStringLiteral("qianniu");
    msg.platformConversationId = QStringLiteral("qianniu:local_qianniu:tb4947894539");
    msg.customerName = QStringLiteral("tb4947894539");
    msg.content = QStringLiteral("hello from qianniu full key");
    msg.direction = QStringLiteral("in");
    msg.sender = QStringLiteral("customer");
    msg.senderName = QStringLiteral("tb4947894539");
    msg.platformMsgId = QStringLiteral("qianniu-full-key-msg-1");
    msg.sourceType = QStringLiteral("ui_observed");
    msg.confidence = 70;
    msg.metadata.insert(QStringLiteral("display_name"), QStringLiteral("tb4947894539"));
    msg.metadata.insert(QStringLiteral("sender_name"), QStringLiteral("tb4947894539"));
    msg.metadata.insert(QStringLiteral("direction"), QStringLiteral("inbound"));
    msg.metadata.insert(QStringLiteral("sender_role"), QStringLiteral("customer"));
    msg.metadata.insert(QStringLiteral("_event_account_id"), QStringLiteral("local_qianniu"));
    msg.metadata.insert(QStringLiteral("_event_conversation_key"), msg.platformConversationId);

    adapter.emitIncoming(msg);

    auto oldLookup = convDao.findByPlatformId(QStringLiteral("qianniu"), QStringLiteral("tb4947894539"));
    QVERIFY(!oldLookup.has_value());

    auto upgraded = convDao.findByPlatformId(QStringLiteral("qianniu"), QStringLiteral("qianniu:local_qianniu:tb4947894539"));
    QVERIFY(upgraded.has_value());
    QCOMPARE(upgraded->id, convId);
    QCOMPARE(upgraded->platformConversationId, QStringLiteral("qianniu:local_qianniu:tb4947894539"));
    QCOMPARE(upgraded->customerName, QStringLiteral("tb4947894539"));
    QCOMPARE(upgraded->unreadCount, 1);

    MessageDao msgDao;
    const auto messages = msgDao.listByConversation(convId);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().platformMsgId, QStringLiteral("qianniu-full-key-msg-1"));
    QCOMPARE(messages.first().senderName, QStringLiteral("tb4947894539"));

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT qianniu_conversation_key, qianniu_display_name FROM qianniu_messages WHERE message_id = :mid"));
    q.bindValue(QStringLiteral(":mid"), messages.first().id);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("qianniu:local_qianniu:tb4947894539"));
    QCOMPARE(q.value(1).toString(), QStringLiteral("tb4947894539"));
}

void TestMessageRouter::sendMessage_routesToAdapterAndStoresPendingMessage()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("qianniu"),
                                      QStringLiteral("qn-conv-1"),
                                      QStringLiteral("赵六"));
    QVERIFY(convId > 0);

    MessageRouter router;
    FakePlatformAdapter adapter(QStringLiteral("qianniu"));
    router.registerAdapter(&adapter);

    QSignalSpy pendingSpy(&router, &MessageRouter::unifiedMessageReceived);

    router.sendMessage(convId, QStringLiteral("已收到，请稍等"));

    QCOMPARE(adapter.lastConversationId, QStringLiteral("qn-conv-1"));
    QCOMPARE(adapter.lastText, QStringLiteral("已收到，请稍等"));
    QCOMPARE(pendingSpy.count(), 1);

    MessageDao msgDao;
    const auto messages = msgDao.listByConversation(convId);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().direction, QStringLiteral("out"));
    QCOMPARE(messages.first().syncStatus, 10);
    QCOMPARE(messages.first().content, QStringLiteral("已收到，请稍等"));
    QCOMPARE(messages.first().cacheScope, QStringLiteral("local_cache"));
    QCOMPARE(messages.first().cacheOrigin, QStringLiteral("manual_outbound_cache"));
}

void TestMessageRouter::sendMedia_routesToAdapterAndStoresPlatformExtension()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    QTemporaryFile mediaFile(QDir::tempPath() + QStringLiteral("/router-media-XXXXXX.png"));
    QVERIFY(mediaFile.open());
    QVERIFY(mediaFile.write("fake-png") > 0);
    mediaFile.flush();

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("wechat"),
                                      QStringLiteral("wechat:wechat:media-user"),
                                      QStringLiteral("media-user"));
    QVERIFY(convId > 0);

    MessageRouter router;
    FakePlatformAdapter adapter(QStringLiteral("wechat"));
    router.registerAdapter(&adapter);

    OutgoingMessagePart part;
    part.type = OutgoingPartType::Image;
    part.localPath = mediaFile.fileName();
    part.fileName = QStringLiteral("demo.png");
    part.mimeType = QStringLiteral("image/png");
    part.sizeBytes = mediaFile.size();
    router.sendMessage(convId, part);

    QCOMPARE(adapter.lastConversationId, QStringLiteral("wechat:wechat:media-user"));
    QCOMPARE(int(adapter.lastPart.type), int(OutgoingPartType::Image));
    QCOMPARE(adapter.lastPart.localPath, mediaFile.fileName());

    MessageDao msgDao;
    const auto messages = msgDao.listByConversation(convId);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().contentType, QStringLiteral("image"));
    QCOMPARE(messages.first().content, QStringLiteral("[图片]"));
    QCOMPARE(messages.first().contentImagePath, mediaFile.fileName());
    QCOMPARE(messages.first().syncStatus, 10);

    QSqlQuery q(Database::getInstance().connection());
    q.prepare(QStringLiteral(
        "SELECT wechat_conversation_key, content_image_path, evidence_ref "
        "FROM wechat_messages WHERE message_id = :mid"));
    q.bindValue(QStringLiteral(":mid"), messages.first().id);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("wechat:wechat:media-user"));
    QCOMPARE(q.value(1).toString(), mediaFile.fileName());
    QCOMPARE(q.value(2).toString(), mediaFile.fileName());
}

void TestMessageRouter::sendFailed_mapsBackToConversationIdByAdapterPlatform()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("pdd_web"),
                                      QStringLiteral("pdd-conv-1"),
                                      QStringLiteral("钱七"));
    QVERIFY(convId > 0);

    MessageRouter router;
    FakePlatformAdapter adapter(QStringLiteral("pdd_web"));
    router.registerAdapter(&adapter);

    QSignalSpy failedSpy(&router, &MessageRouter::messageSendFailed);
    adapter.emitSendFailed(QStringLiteral("pdd-conv-1"), QStringLiteral("writer timeout"));

    QCOMPARE(failedSpy.count(), 1);
    const QList<QVariant> args = failedSpy.takeFirst();
    QCOMPARE(args.at(0).toInt(), convId);
    QCOMPARE(args.at(1).toString(), QStringLiteral("writer timeout"));
}

void TestMessageRouter::sendMessage_autoAck_marksMessageAsSent()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("simulator"),
                                      QStringLiteral("sim-conv-1"),
                                      QStringLiteral("测试买家"));
    QVERIFY(convId > 0);

    MessageRouter router;
    FakePlatformAdapter adapter(QStringLiteral("simulator"), true);
    router.registerAdapter(&adapter);

    QSignalSpy statusSpy(&router, &MessageRouter::messageStatusChanged);

    router.sendMessage(convId, QStringLiteral("自动确认发送"));

    QCOMPARE(statusSpy.count(), 1);

    MessageDao msgDao;
    const auto messages = msgDao.listByConversation(convId);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().syncStatus, 11);
    QCOMPARE(messages.first().errorReason, QString());
}

void TestMessageRouter::sendFailed_marksLatestPendingMessageAsFailed()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    ConversationDao convDao;
    const int convId = convDao.create(QStringLiteral("qianniu"),
                                      QStringLiteral("qn-conv-2"),
                                      QStringLiteral("客户乙"));
    QVERIFY(convId > 0);

    MessageRouter router;
    FakePlatformAdapter adapter(QStringLiteral("qianniu"));
    router.registerAdapter(&adapter);

    router.sendMessage(convId, QStringLiteral("待发送消息"));
    adapter.emitSendFailed(QStringLiteral("qn-conv-2"), QStringLiteral("writer timeout"));

    MessageDao msgDao;
    const auto messages = msgDao.listByConversation(convId);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().syncStatus, 12);
    QCOMPARE(messages.first().errorReason, QStringLiteral("writer timeout"));
}

QTEST_MAIN(TestMessageRouter)
#include "test_message_router.moc"
