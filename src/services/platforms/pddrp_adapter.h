#ifndef PDDRP_ADAPTER_H
#define PDDRP_ADAPTER_H

#include "iplatformadapter.h"
#include <QTimer>

// 拼多多网页（商家后台）RPA inbox consumer adapter.
// Consumes rows from rpa_inbox_messages where platform='pdd_web'.
class PddRPAAdapter : public IPlatformAdapter
{
    Q_OBJECT
public:
    explicit PddRPAAdapter(QObject* parent = nullptr);

    QString platformName() const override { return QStringLiteral("pdd_web"); }
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

#endif // PDDRP_ADAPTER_H

