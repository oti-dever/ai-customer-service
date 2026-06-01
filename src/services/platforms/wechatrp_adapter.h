#ifndef WECHATRPA_ADAPTER_H
#define WECHATRPA_ADAPTER_H

#include "iplatformadapter.h"
#include <QJsonObject>
#include <QSet>
#include <QTimer>

// WeChat PC adapter. The primary path consumes Python sidecar RPA events;
// rpa_inbox_messages remains as a short-term compatibility fallback.
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
    void sendMessage(const QString& conversationId, const QString& text, const QString& clientMessageId = QString()) override;
    bool isConnected() const override { return m_connected; }

private:
    void pollOnce();
    bool pollRpaEventsOnce();
    bool pollInboxOnce();
    void applyPollCadence(bool hadWork);
    PlatformMessage platformMessageFromEvent(const QJsonObject& event) const;
    QString normalizeConversationKey(const QString& conversationKey) const;
    void handleRpaEvent(const QJsonObject& event);
    void emitConversationObserved(const QJsonObject& event);

    bool m_connected = false;
    QTimer* m_pollTimer = nullptr;
    QString m_eventCursor = QStringLiteral("0");
    qint64 m_lastInboxId = 0;
    int m_idleFetchTicks = 0;
    bool m_commandInFlight = false;
    bool m_eventSocketConnected = false;
    QSet<QString> m_seenSeqs;
};

#endif // WECHATRPA_ADAPTER_H
