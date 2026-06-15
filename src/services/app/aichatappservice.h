#ifndef AICHATAPPSERVICE_H
#define AICHATAPPSERVICE_H

#include "../ai/aiprovidercatalog.h"
#include "../ai/aitypes.h"
#include <QObject>

class AiServiceFacade;
class IAiStreamingSession;
class QNetworkAccessManager;

enum class AggregateAiBuildFailure {
    None,
    MissingApiKey,
    IncompleteModelConfig,
    MissingInboundSnapshot,
    MissingInboundImage,
    EmptyInbound,
    VisionUnsupported,
};

struct AggregateAiBuiltRequest {
    AggregateAiBuildFailure failure = AggregateAiBuildFailure::None;
    QString failureDetail;
    AiProviderConfig config;
    AiRequest request;

    bool ok() const { return failure == AggregateAiBuildFailure::None; }
};

class AiChatAppService : public QObject
{
    Q_OBJECT
public:
    explicit AiChatAppService(QObject* parent = nullptr);

    AiProviderConfig resolveProviderConfig(const QString& sessionModelKey,
                                           const QString& baseUrlOverride = QString(),
                                           const QString& modelOverride = QString(),
                                           const QString& apiKeyOverride = QString(),
                                           const AiConfigLoadOptions& options = {}) const;
    AggregateAiBuiltRequest buildAggregateReplyRequest(int conversationId,
                                                       const QString& sessionModelKey) const;
    AggregateAiBuiltRequest buildAggregateCustomerProfileRequest(int conversationId,
                                                                 const QString& sessionModelKey) const;
    IAiStreamingSession* createSession(const AiProviderConfig& config,
                                       const AiRequest& request,
                                       QObject* parent) const;

private:
    QNetworkAccessManager* m_network = nullptr;
    AiServiceFacade* m_facade = nullptr;
};

#endif // AICHATAPPSERVICE_H
