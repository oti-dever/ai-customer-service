#include "qianniurp_adapter.h"
#include <QDebug>

QianniuRPAAdapter::QianniuRPAAdapter(QObject* parent)
    : IPlatformAdapter(parent)
{
}

void QianniuRPAAdapter::connectPlatform()
{
    m_connected = true;
    qInfo() << "[QianniuRPAAdapter] 千牛 RPA 适配器已就绪（占位实现，仅日志）";
    emit connectionStateChanged(true);
}

void QianniuRPAAdapter::disconnectPlatform()
{
    m_connected = false;
    qInfo() << "[QianniuRPAAdapter] 千牛 RPA 适配器已断开";
    emit connectionStateChanged(false);
}

void QianniuRPAAdapter::startListening()
{
    qInfo() << "[QianniuRPAAdapter] startListening（后续将通过 SQLite 与 Python Reader 集成）";
}

void QianniuRPAAdapter::stopListening()
{
    qInfo() << "[QianniuRPAAdapter] stopListening";
}

void QianniuRPAAdapter::sendMessage(const QString& conversationId, const QString& text)
{
    Q_UNUSED(conversationId)
    Q_UNUSED(text)
    // 真正的发送逻辑由 Python Writer 负责，这里只起到“占位”作用。
    qInfo() << "[QianniuRPAAdapter] sendMessage 被调用 — 当前实现不直接操作千牛窗口，消息会通过 SQLite 交给 Python 处理";
}

