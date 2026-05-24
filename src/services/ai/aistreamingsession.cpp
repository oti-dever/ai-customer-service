#include "aistreamingsession.h"

#include "airequestassembler.h"
#include "arkfilesresponses.h"
#include "openaicompatclient.h"

#include <QNetworkAccessManager>
#include <QTimer>

ImmediateFailAiSession::ImmediateFailAiSession(const QString& reason, QObject* parent)
    : IAiStreamingSession(parent)
    , m_reason(reason)
{
}

void ImmediateFailAiSession::start()
{
    if (m_done)
        return;
    m_done = true;
    QTimer::singleShot(0, this, [this]() { emit failed(m_reason); });
}

void ImmediateFailAiSession::abort()
{
    m_done = true;
}

OpenAiChatSession::OpenAiChatSession(QNetworkAccessManager* nam,
                                     const AiProviderConfig& config,
                                     const AiRequest& request,
                                     QObject* parent)
    : IAiStreamingSession(parent)
    , m_client(new OpenAiCompatClient(nam, this))
    , m_config(config)
    , m_request(request)
{
    connect(m_client, &OpenAiCompatClient::streamDelta, this, &IAiStreamingSession::delta);
    connect(m_client, &OpenAiCompatClient::completed, this, &IAiStreamingSession::completed);
    connect(m_client, &OpenAiCompatClient::failed, this, &IAiStreamingSession::failed);
}

OpenAiChatSession::~OpenAiChatSession()
{
    abort();
}

void OpenAiChatSession::start()
{
    QString err;
    const QJsonArray messages = buildChatCompletionsMessages(m_request, &err);
    if (!err.isEmpty()) {
        emit failed(err);
        return;
    }
    m_client->requestChatCompletion(OpenAiCompatClient::buildCompletionsUrl(m_config.baseUrl),
                                    m_config.apiKey,
                                    m_config.model,
                                    messages,
                                    m_request.stream,
                                    m_request.extraRootFields);
}

void OpenAiChatSession::abort()
{
    if (m_client)
        m_client->abortActive();
}

ArkFileSession::ArkFileSession(QNetworkAccessManager* nam,
                               const AiProviderConfig& config,
                               const AiRequest& request,
                               QObject* parent)
    : IAiStreamingSession(parent)
    , m_service(new VolcengineArkFileChatService(nam, this))
    , m_config(config)
    , m_request(request)
{
    connect(m_service, &VolcengineArkFileChatService::textDelta, this, &IAiStreamingSession::delta);
    connect(m_service, &VolcengineArkFileChatService::completed, this, &IAiStreamingSession::completed);
    connect(m_service, &VolcengineArkFileChatService::failed, this, &IAiStreamingSession::failed);
}

ArkFileSession::~ArkFileSession()
{
    abort();
}

void ArkFileSession::start()
{
    AiArkFileRequestData data;
    QString err;
    if (!buildArkFileRequestData(m_request, &data, &err)) {
        emit failed(err.isEmpty() ? QStringLiteral("文件请求组装失败") : err);
        return;
    }
    m_service->start(m_config.baseUrl,
                     m_config.apiKey,
                     m_config.model,
                     data.localFilePath,
                     data.userText,
                     data.instructions,
                     data.historyPlainText);
}

void ArkFileSession::abort()
{
    if (m_service)
        m_service->abort();
}
