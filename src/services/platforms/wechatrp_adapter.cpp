#include "wechatrp_adapter.h"
#include "../../data/database.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

namespace {
// 空闲时略长于原 500ms，有流量时缩短以尽快 drain（含单批 LIMIT 50 后的连续批次）。
constexpr int kInboxPollIdleMs = 600;
constexpr int kInboxPollActiveMs = 150;
} // namespace

WechatRPAAdapter::WechatRPAAdapter(QObject* parent)
    : IPlatformAdapter(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kInboxPollIdleMs);
    connect(m_pollTimer, &QTimer::timeout, this, &WechatRPAAdapter::pollInboxOnce);
}

void WechatRPAAdapter::applyInboxPollCadence(bool hadInboundWork)
{
    if (!m_pollTimer)
        return;
    const int ms = hadInboundWork ? kInboxPollActiveMs : kInboxPollIdleMs;
    if (m_pollTimer->interval() != ms)
        m_pollTimer->setInterval(ms);
}

void WechatRPAAdapter::connectPlatform()
{
    m_connected = true;
    qInfo() << "[WechatRPAAdapter] 微信 RPA 适配器已就绪（inbox 轮询）";
    emit connectionStateChanged(true);
}

void WechatRPAAdapter::disconnectPlatform()
{
    m_connected = false;
    stopListening();
    qInfo() << "[WechatRPAAdapter] 微信 RPA 适配器已断开";
    emit connectionStateChanged(false);
}

void WechatRPAAdapter::startListening()
{
    if (!m_connected) {
        connectPlatform();
    }
    applyInboxPollCadence(false);
    if (!m_pollTimer->isActive())
        m_pollTimer->start();
    qInfo() << "[WechatRPAAdapter] startListening（动态轮询 rpa_inbox_messages，空闲"
             << kInboxPollIdleMs << "ms / 有流量" << kInboxPollActiveMs << "ms）";
}

void WechatRPAAdapter::stopListening()
{
    if (m_pollTimer && m_pollTimer->isActive())
        m_pollTimer->stop();
    qInfo() << "[WechatRPAAdapter] stopListening";
}

void WechatRPAAdapter::sendMessage(const QString& conversationId, const QString& text)
{
    Q_UNUSED(conversationId)
    Q_UNUSED(text)
    qInfo() << "[WechatRPAAdapter] sendMessage 被调用 — 当前只读验证版不发送微信消息";
}

void WechatRPAAdapter::pollInboxOnce()
{
    if (!m_connected)
        return;

    QSqlDatabase db = Database::getInstance().connection();
    if (!db.isOpen()) {
        applyInboxPollCadence(false);
        return;
    }

    QSqlQuery q(db);
    q.prepare(
        "SELECT id, platform_conversation_id, customer_name, content, created_at, platform_msg_id, "
        "       sender_name, original_timestamp "
        "FROM rpa_inbox_messages "
        "WHERE platform = :platform "
        "  AND consume_status = 0 "
        "  AND id > :lastId "
        "ORDER BY id ASC LIMIT 50");
    q.bindValue(":platform", platformName());
    q.bindValue(":lastId", m_lastInboxId);

    if (!q.exec()) {
        qWarning() << "[WechatRPAAdapter] inbox 查询失败:" << q.lastError().text();
        applyInboxPollCadence(false);
        return;
    }

    QList<qint64> consumedIds;
    while (q.next()) {
        const qint64 inboxId = q.value(0).toLongLong();
        const QString platformConvId = q.value(1).toString();
        const QString customerName = q.value(2).toString();
        const QString content = q.value(3).toString();
        const QDateTime createdAt = q.value(4).toDateTime();
        const QString platformMsgId = q.value(5).toString();
        const QString senderName = q.value(6).toString();
        const QString originalTimestamp = q.value(7).toString();

        PlatformMessage msg;
        msg.platform = platformName();
        msg.platformConversationId = platformConvId;
        msg.customerName = customerName;
        msg.content = content;
        msg.direction = QStringLiteral("in");
        msg.sender = QStringLiteral("customer");
        msg.createdAt = createdAt.isValid() ? createdAt : QDateTime::currentDateTime();
        msg.platformMsgId = platformMsgId;
        msg.senderName = senderName;
        msg.originalTimestamp = originalTimestamp;

        emit incomingMessage(msg);

        consumedIds.append(inboxId);
        if (inboxId > m_lastInboxId)
            m_lastInboxId = inboxId;
    }

    if (consumedIds.isEmpty()) {
        applyInboxPollCadence(false);
        return;
    }

    QSqlQuery u(db);
    u.prepare("UPDATE rpa_inbox_messages SET consume_status = 1 WHERE id = :id");
    db.transaction();
    bool ok = true;
    for (qint64 id : consumedIds) {
        u.bindValue(":id", id);
        if (!u.exec()) {
            ok = false;
            qWarning() << "[WechatRPAAdapter] inbox 更新 consume_status 失败:" << u.lastError().text();
        }
    }
    ok ? db.commit() : db.rollback();
    applyInboxPollCadence(ok);
}

