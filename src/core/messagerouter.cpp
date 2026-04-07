#include "messagerouter.h"
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
#include "../services/platforms/iplatformadapter.h"

MessageRouter::MessageRouter(QObject* parent)
    : QObject(parent)
{
}

void MessageRouter::registerAdapter(IPlatformAdapter* adapter)
{
    if (!adapter) return;
    const QString name = adapter->platformName();
    m_adapters[name] = adapter;

    connect(adapter, &IPlatformAdapter::incomingMessage,
            this, &MessageRouter::onIncomingMessage);
    connect(adapter, &IPlatformAdapter::messageSent,
            this, &MessageRouter::onMessageSent);
    connect(adapter, &IPlatformAdapter::sendFailed,
            this, &MessageRouter::onSendFailed);

    qInfo() << "[MessageRouter] 注册适配器:" << name;
}

void MessageRouter::unregisterAdapter(const QString& platformName)
{
    if (auto* a = m_adapters.take(platformName)) {
        disconnect(a, nullptr, this, nullptr);
        qInfo() << "[MessageRouter] 注销适配器:" << platformName;
    }
}

IPlatformAdapter* MessageRouter::adapter(const QString& platformName) const
{
    return m_adapters.value(platformName, nullptr);
}

void MessageRouter::sendMessage(int conversationId, const QString& text)
{
    ConversationDao convDao;
    auto conv = convDao.findById(conversationId);
    if (!conv) {
        qWarning() << "[MessageRouter] 会话不存在:" << conversationId;
        emit messageSendFailed(conversationId, QStringLiteral("会话不存在"));
        return;
    }

    auto* a = m_adapters.value(conv->platform, nullptr);
    if (!a) {
        qWarning() << "[MessageRouter] 平台适配器不存在:" << conv->platform;
        emit messageSendFailed(conversationId, QStringLiteral("平台未连接"));
        return;
    }

    a->sendMessage(conv->platformConversationId, text);

    MessageDao msgDao;
    QDateTime now = QDateTime::currentDateTime();
    // 标记为待发送，等待 Python RPA 组件实际送达平台
    int msgId = msgDao.create(conversationId,
                              QStringLiteral("out"),
                              text,
                              QStringLiteral("agent"),
                              QString(),  // platformMsgId 由 Python 侧补充
                              10,         // sync_status = 10 (pending_send)
                              QString(),  // errorReason
                              QString(),  // senderName（自己发的消息为空）
                              QString()); // originalTimestamp（自己发的消息为空）

    convDao.updateLastMessage(conversationId, text, now);

    if (msgId > 0) {
        MessageRecord rec;
        rec.id = msgId;
        rec.conversationId = conversationId;
        rec.direction = QStringLiteral("out");
        rec.content = text;
        rec.sender = QStringLiteral("agent");
        rec.senderName = QString();
        rec.createdAt = now;
        rec.syncStatus = 10;
        emit messageSentOk(conversationId, rec);
    }

    auto updatedConv = convDao.findById(conversationId);
    if (updatedConv)
        emit conversationUpdated(*updatedConv);

    qDebug() << "[MessageRouter] 消息已发送 convId=" << conversationId << "text=" << text.left(30);
}

void MessageRouter::onIncomingMessage(const PlatformMessage& msg)
{
    MessageDao msgDao;
    if (!msg.platformMsgId.isEmpty() && msgDao.existsByPlatformMsgId(msg.platformMsgId)) {
        qDebug() << "[MessageRouter] 消息去重，跳过:" << msg.platformMsgId;
        return;
    }

    int convId = ensureConversation(msg);
    if (convId <= 0) return;

    QDateTime now = msg.createdAt.isValid() ? msg.createdAt : QDateTime::currentDateTime();
    int msgId = msgDao.create(convId,
                              msg.direction,
                              msg.content,
                              msg.sender,
                              msg.platformMsgId,
                              1,  // 正常消息
                              QString(),  // errorReason
                              msg.senderName,
                              msg.originalTimestamp);  // 原始时间戳

    ConversationDao convDao;
    convDao.updateLastMessage(convId, msg.content, now);
    if (msg.direction == QLatin1String("in"))
        convDao.incrementUnread(convId);

    if (msgId > 0) {
        MessageRecord rec;
        rec.id = msgId;
        rec.conversationId = convId;
        rec.direction = msg.direction;
        rec.content = msg.content;
        rec.sender = msg.sender;
        rec.senderName = msg.senderName;
        rec.createdAt = now;
        rec.platformMsgId = msg.platformMsgId;
        rec.originalTimestamp = msg.originalTimestamp;
        emit messageReceived(convId, rec);
    }

    auto conv = convDao.findById(convId);
    if (conv)
        emit conversationUpdated(*conv);

    qDebug() << "[MessageRouter] 收到消息 convId=" << convId
             << "from=" << msg.customerName << "content=" << msg.content.left(30);
}

void MessageRouter::onMessageSent(const QString& conversationId, const QString& text)
{
    Q_UNUSED(conversationId)
    Q_UNUSED(text)
}

void MessageRouter::onSendFailed(const QString& conversationId, const QString& reason)
{
    ConversationDao dao;
    auto conv = dao.findByPlatformId(QStringLiteral("simulator"), conversationId);
    int cid = conv ? conv->id : 0;
    qWarning() << "[MessageRouter] 发送失败 conv=" << conversationId << "reason=" << reason;
    emit messageSendFailed(cid, reason);
}

int MessageRouter::ensureConversation(const PlatformMessage& msg)
{
    ConversationDao dao;
    auto existing = dao.findByPlatformId(msg.platform, msg.platformConversationId);
    if (existing) {
        if (existing->status == QLatin1String("closed")) {
            dao.setStatus(existing->id, QStringLiteral("open"));
            auto reopened = dao.findById(existing->id);
            if (reopened)
                emit conversationCreated(*reopened);
        }
        return existing->id;
    }

    int id = dao.create(msg.platform, msg.platformConversationId, msg.customerName);
    if (id > 0) {
        auto conv = dao.findById(id);
        if (conv)
            emit conversationCreated(*conv);
    }
    return id;
}
