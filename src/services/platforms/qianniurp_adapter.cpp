#include "qianniurp_adapter.h"
#include "../../data/database.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

QianniuRPAAdapter::QianniuRPAAdapter(QObject* parent)
    : IPlatformAdapter(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(800);
    connect(m_pollTimer, &QTimer::timeout, this, &QianniuRPAAdapter::pollInboxOnce);
}

void QianniuRPAAdapter::connectPlatform()
{
    m_connected = true;
    qInfo() << "[QianniuRPAAdapter] 千牛 RPA 适配器已就绪（inbox 轮询）";
    emit connectionStateChanged(true);
}

void QianniuRPAAdapter::disconnectPlatform()
{
    m_connected = false;
    stopListening();
    qInfo() << "[QianniuRPAAdapter] 千牛 RPA 适配器已断开";
    emit connectionStateChanged(false);
}

void QianniuRPAAdapter::startListening()
{
    if (!m_connected) {
        connectPlatform();
    }
    if (!m_pollTimer->isActive())
        m_pollTimer->start();
    qInfo() << "[QianniuRPAAdapter] startListening（轮询 rpa_inbox_messages）";
}

void QianniuRPAAdapter::stopListening()
{
    if (m_pollTimer && m_pollTimer->isActive())
        m_pollTimer->stop();
    qInfo() << "[QianniuRPAAdapter] stopListening";
}

void QianniuRPAAdapter::sendMessage(const QString& conversationId, const QString& text)
{
    Q_UNUSED(conversationId)
    Q_UNUSED(text)
    // 真正的发送逻辑由 Python Writer 负责，这里只起到“占位”作用。
    qInfo() << "[QianniuRPAAdapter] sendMessage 被调用 — 当前实现不直接操作千牛窗口，消息会通过 SQLite 交给 Python 处理";
}

void QianniuRPAAdapter::pollInboxOnce()
{
    if (!m_connected)
        return;

    QSqlDatabase db = Database::getInstance().connection();
    if (!db.isOpen())
        return;

    QSqlQuery q(db);
    q.prepare(
        "SELECT id, platform_conversation_id, customer_name, content, created_at, platform_msg_id "
        "FROM rpa_inbox_messages "
        "WHERE platform = :platform "
        "  AND consume_status = 0 "
        "  AND id > :lastId "
        "ORDER BY id ASC LIMIT 50");
    q.bindValue(":platform", platformName());
    q.bindValue(":lastId", m_lastInboxId);

    if (!q.exec()) {
        qWarning() << "[QianniuRPAAdapter] inbox 查询失败:" << q.lastError().text();
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

        PlatformMessage msg;
        msg.platform = platformName();
        msg.platformConversationId = platformConvId;
        msg.customerName = customerName;
        msg.content = content;
        msg.direction = QStringLiteral("in");
        msg.sender = QStringLiteral("customer");
        msg.createdAt = createdAt.isValid() ? createdAt : QDateTime::currentDateTime();
        msg.platformMsgId = platformMsgId;

        emit incomingMessage(msg);

        consumedIds.append(inboxId);
        if (inboxId > m_lastInboxId)
            m_lastInboxId = inboxId;
    }

    if (consumedIds.isEmpty())
        return;

    // 标记已消费，避免重复
    QSqlQuery u(db);
    u.prepare("UPDATE rpa_inbox_messages SET consume_status = 1 WHERE id = :id");
    db.transaction();
    bool ok = true;
    for (qint64 id : consumedIds) {
        u.bindValue(":id", id);
        if (!u.exec()) {
            ok = false;
            qWarning() << "[QianniuRPAAdapter] inbox 更新 consume_status 失败:" << u.lastError().text();
        }
    }
    ok ? db.commit() : db.rollback();
}

