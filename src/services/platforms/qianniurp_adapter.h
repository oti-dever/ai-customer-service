#ifndef QIANNIURP_ADAPTER_H
#define QIANNIURP_ADAPTER_H

#include "iplatformadapter.h"
#include <QJsonObject>
#include <QSet>

// 千牛 sidecar 适配器。
// C++ 侧保留平台展示名与会话兼容键，真实自动化由 Python service + qianniu sidecar 承担。
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
    void sendMessage(const QString& conversationId, const QString& text, const QString& clientMessageId = QString()) override;
    bool isConnected() const override { return m_connected; }

private:
    PlatformMessage platformMessageFromEvent(const QJsonObject& event) const;
    QString normalizeConversationKey(const QString& conversationKey) const;
    void handleRpaEvent(const QJsonObject& event);
    void emitConversationObserved(const QJsonObject& event);

    bool m_connected = false;
    QString m_eventCursor = QStringLiteral("0");
    bool m_commandInFlight = false;
    bool m_eventSocketConnected = false;
    QSet<QString> m_seenSeqs;
};

#endif // QIANNIURP_ADAPTER_H

