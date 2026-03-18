#ifndef QIANNIURP_ADAPTER_H
#define QIANNIURP_ADAPTER_H

#include "iplatformadapter.h"
#include <QTimer>

// 基于共享 SQLite 的千牛 RPA 适配器骨架。
// 通过轮询 rpa_inbox_messages 队列消费入站消息（Python Reader -> Qt）。
class QianniuRPAAdapter : public IPlatformAdapter
{
    Q_OBJECT
public:
    explicit QianniuRPAAdapter(QObject* parent = nullptr);

    QString platformName() const override { return QStringLiteral("qianniu"); }
    void connectPlatform() override;
    void disconnectPlatform() override;
    void startListening() override;
    void stopListening() override;
    void sendMessage(const QString& conversationId, const QString& text) override;
    bool isConnected() const override { return m_connected; }

private:
    void pollInboxOnce();

    bool m_connected = false;
    QTimer* m_pollTimer = nullptr;
    qint64 m_lastInboxId = 0;
};

#endif // QIANNIURP_ADAPTER_H

