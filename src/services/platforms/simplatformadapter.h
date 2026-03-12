#ifndef SIMPLATFORMADAPTER_H
#define SIMPLATFORMADAPTER_H

#include "iplatformadapter.h"
#include <QStringList>

class SimPlatformAdapter : public IPlatformAdapter
{
    Q_OBJECT
public:
    explicit SimPlatformAdapter(QObject* parent = nullptr);

    QString platformName() const override { return QStringLiteral("simulator"); }
    void connectPlatform() override;
    void disconnectPlatform() override;
    void startListening() override;
    void stopListening() override;
    void sendMessage(const QString& conversationId, const QString& text) override;
    bool isConnected() const override { return m_connected; }

    void simulateIncomingMessage(const QString& buyerName, const QString& text);

private:
    bool m_connected = false;
    int m_nextBuyerId = 1;
    QStringList m_sampleMessages;
    QStringList m_sampleNames;
};

#endif // SIMPLATFORMADAPTER_H
