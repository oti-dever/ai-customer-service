#ifndef QQRPA_ADAPTER_H
#define QQRPA_ADAPTER_H

#include "iplatformadapter.h"
#include <QJsonObject>
#include <QSet>

class QQRPAAdapter : public IPlatformAdapter
{
    Q_OBJECT
public:
    explicit QQRPAAdapter(QObject* parent = nullptr);

    QString platformName() const override { return QStringLiteral("qq"); }
    void connectPlatform() override;
    void disconnectPlatform() override;
    void startListening() override;
    void stopListening() override;
    void sendMessage(const QString& conversationId, const QString& text, const QString& clientMessageId = QString()) override;
    void sendMessagePart(const QString& conversationId,
                         const OutgoingMessagePart& part,
                         const QString& clientMessageId = QString()) override;
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

#endif // QQRPA_ADAPTER_H
