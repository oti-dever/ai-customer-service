#ifndef AISTREAMINGSESSION_H
#define AISTREAMINGSESSION_H

#include "aitypes.h"

#include <QObject>

class QNetworkAccessManager;
class OpenAiCompatClient;
class VolcengineArkFileChatService;

class IAiStreamingSession : public QObject
{
    Q_OBJECT
public:
    explicit IAiStreamingSession(QObject* parent = nullptr) : QObject(parent) {}
    ~IAiStreamingSession() override = default;

    virtual void start() = 0;
    virtual void abort() = 0;

signals:
    void delta(const QString& text);
    void completed();
    void failed(const QString& reason);
};

class ImmediateFailAiSession : public IAiStreamingSession
{
    Q_OBJECT
public:
    explicit ImmediateFailAiSession(const QString& reason, QObject* parent = nullptr);

    void start() override;
    void abort() override;

private:
    QString m_reason;
    bool m_done = false;
};

class OpenAiChatSession : public IAiStreamingSession
{
    Q_OBJECT
public:
    OpenAiChatSession(QNetworkAccessManager* nam,
                      const AiProviderConfig& config,
                      const AiRequest& request,
                      QObject* parent = nullptr);
    ~OpenAiChatSession() override;

    void start() override;
    void abort() override;

private:
    OpenAiCompatClient* m_client = nullptr;
    AiProviderConfig m_config;
    AiRequest m_request;
};

class ArkFileSession : public IAiStreamingSession
{
    Q_OBJECT
public:
    ArkFileSession(QNetworkAccessManager* nam,
                   const AiProviderConfig& config,
                   const AiRequest& request,
                   QObject* parent = nullptr);
    ~ArkFileSession() override;

    void start() override;
    void abort() override;

private:
    VolcengineArkFileChatService* m_service = nullptr;
    AiProviderConfig m_config;
    AiRequest m_request;
};

#endif // AISTREAMINGSESSION_H
