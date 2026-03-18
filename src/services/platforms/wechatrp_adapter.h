#ifndef WECHATRPA_ADAPTER_H
#define WECHATRPA_ADAPTER_H

#include "iplatformadapter.h"
#include <QTimer>

// WeChat (Windows desktop) inbox consumer adapter.
// Consumes rows from rpa_inbox_messages where platform='wechat_pc'.
class WechatRPAAdapter : public IPlatformAdapter
{
    Q_OBJECT
public:
    explicit WechatRPAAdapter(QObject* parent = nullptr);

    QString platformName() const override { return QStringLiteral("wechat_pc"); }
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

#endif // WECHATRPA_ADAPTER_H

