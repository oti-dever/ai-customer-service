#include <QtTest>

#include "core/messagerouter.h"
#include "data/conversationdao.h"
#include "data/messagedao.h"
#include "services/platforms/iplatformadapter.h"
#include "testdatabase.h"

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
    bool isConnected() const override { return m_connected; }

    void emitIncoming(const PlatformMessage& msg) { emit incomingMessage(msg); }
    void emitSendFailed(const QString& conversationId, const QString& reason, const QString& clientMessageId = QString()) { emit sendFailed(conversationId, reason, clientMessageId); }

    QString lastConversationId;
    QString lastText;
    QString lastClientMessageId;

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
    void sendMessage_routesToAdapterAndStoresPendingMessage();
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
    FakePlatformAdapter adapter(QStringLiteral("wechat_pc"));
    router.registerAdapter(&adapter);

    QSignalSpy createdSpy(&router, &MessageRouter::conversationCreated);
    QSignalSpy receivedSpy(&router, &MessageRouter::messageReceived);

    PlatformMessage msg;
    msg.platform = QStringLiteral("wechat_pc");
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
    auto conv = convDao.findByPlatformId(QStringLiteral("wechat_pc"), QStringLiteral("wx-conv-1"));
    QVERIFY(conv.has_value());
    QCOMPARE(conv->customerName, QStringLiteral("王五"));
    QCOMPARE(conv->unreadCount, 1);

    MessageDao msgDao;
    const auto messages = msgDao.listByConversation(conv->id);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().content, QStringLiteral("你好"));
    QCOMPARE(messages.first().platformMsgId, QStringLiteral("wx-msg-1"));
    QCOMPARE(messages.first().sourceType, QStringLiteral("ui_observed"));
    QCOMPARE(messages.first().confidence, 72);
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
