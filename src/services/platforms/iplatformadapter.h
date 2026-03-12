#ifndef IPLATFORMADAPTER_H
#define IPLATFORMADAPTER_H

#include <QObject>
#include <QString>
#include "../../core/types.h"

class IPlatformAdapter : public QObject
{
    Q_OBJECT
public:
    explicit IPlatformAdapter(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IPlatformAdapter() = default;

    virtual QString platformName() const = 0;
    virtual void connectPlatform() = 0;
    virtual void disconnectPlatform() = 0;
    virtual void startListening() = 0;
    virtual void stopListening() = 0;
    virtual void sendMessage(const QString& conversationId, const QString& text) = 0;
    virtual bool isConnected() const = 0;

signals:
    void incomingMessage(const PlatformMessage& msg);
    void messageSent(const QString& conversationId, const QString& text);
    void sendFailed(const QString& conversationId, const QString& reason);
    void connectionStateChanged(bool connected);
};

#endif // IPLATFORMADAPTER_H
