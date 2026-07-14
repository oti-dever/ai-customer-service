#ifndef AICHATAPPSERVICE_H
#define AICHATAPPSERVICE_H

#include "../../core/types.h"
#include "../ai/aiprovidercatalog.h"
#include "../ai/aitypes.h"
#include <QObject>
#include <QStringList>

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

struct KnowledgeSnippetContext {
    QString chunkId;
    QString sourceTitle;
    QString titlePath;
    QString snippet;
    QString matchType;
    double score = 0.0;
    double keywordScore = 0.0;
    double vectorScore = 0.0;
};

struct AggregateReplyStrategy {
    QString replyTone;
    QString commonAddressTerms;
    bool allowAutoSendImages = false;
    bool allowAutoSendMultiMessages = false;
    int maxAutoSendMessages = 1;
};

struct ReplyRuntimeConfig {
    QString platform;
    QString source;
    QString statusText;
    QString sessionModelKey;
    QString boundRobotId;
    QString robotName;
    QStringList knowledgeBaseIds;
    QStringList knowledgeBaseNames;
    AggregateReplyStrategy strategy;
    bool usingRobot = false;
    bool robotFound = false;
    bool robotEnabled = false;
};

struct ReplyKnowledgeTrace {
    QString latestInbound;
    QString platform;
    QString shopId;
    QString scene;
    QStringList boundBaseIds;
    QString bindingStatus;
    QString bindingError;
    QString searchQuery;
    QString statusText;
    QString errorText;
    QString failureStage;
    QString responseStatus;
    int healthMs = 0;
    int bindingHttpMs = 0;
    int searchHttpMs = 0;
    int totalMs = 0;
    int serverLatencyMs = -1;
    bool searched = false;
    bool skipped = false;
    QList<KnowledgeSnippetContext> snippets;
};

struct ReplyImageCandidate {
    QString assetId;
    QString sourceTitle;
    QString originalFilename;
    QString filePath;
    QString assetType;
    QString summary;
    QString tags;
    QString scenarios;
    QString riskTags;
    QString suggestedReply;
    QString recommendationReason;
    QString riskNotice;
    QString matchType;
    double score = 0.0;
    bool shouldAttach = false;
    bool requiresHumanConfirm = true;
};

struct ReplyImageTrace {
    QString latestInbound;
    QString platform;
    QStringList boundBaseIds;
    QString bindingStatus;
    QString bindingError;
    QString searchQuery;
    QString resolvedProductFocus;
    QString resolutionSource;
    QString statusText;
    QString errorText;
    QString failureStage;
    QString responseStatus;
    int bindingHttpMs = 0;
    int searchHttpMs = 0;
    int totalMs = 0;
    int serverLatencyMs = -1;
    int rawCandidateCount = 0;
    int filteredCandidateCount = 0;
    bool searched = false;
    bool skipped = false;
    QList<ReplyImageCandidate> candidates;
};

struct ReplyIntentDecision {
    QString intent = QStringLiteral("normal_question");
    QString workflow = QStringLiteral("normal_text_reply");
    QString source = QStringLiteral("heuristic");
    QString reason;
    QString rawJson;
    QString errorText;
    QString imageQuery;
    QString replyGoal;
    QString email;
    QString templateId;
    QString nextAction;
    QString businessObjectName;
    QStringList missingSlots;
    QStringList riskFlags;
    double confidence = 0.0;
    bool modelRouted = false;
    bool needCustomerReply = true;
    bool needDocSearch = true;
    bool needImageSearch = false;
    bool needEmail = false;
};

struct ReplyActionPlan {
    QString workflow = QStringLiteral("normal_text_reply");
    QString replyMode = QStringLiteral("text");
    QString imageQuery;
    QString replyInstruction;
    QStringList requiredActions;
    QStringList blockedActions;
    bool generateReply = true;
    bool needDocSearch = true;
    bool needImageSearch = false;
    bool allowImageAttachments = false;
    bool hasImageAttachment = false;
    bool needEmail = false;
    bool askForEmail = false;
    bool askForTemplate = false;
    bool emailServiceRequired = false;
    bool directExternalLinkBlocked = false;
    bool requiresHumanReview = false;
};

struct ReplyContextInput {
    enum class Source {
        AggregateConversation,
        RobotSandbox,
    };

    Source source = Source::AggregateConversation;
    int conversationId = 0;
    QString latestUserText;
    QList<AiConversationTurn> recentTurns;
    ReplyRuntimeConfig runtimeConfig;
};

struct ReplyContextResult {
    QString knowledgeStatus;
    QString imageStatus;
    QString linkedImageName;
    ReplyIntentDecision intent;
    ReplyActionPlan actionPlan;
    ReplyKnowledgeTrace knowledgeTrace;
    ReplyImageTrace imageTrace;
    QList<KnowledgeSnippetContext> knowledgeSnippets;
    QList<ReplyImageCandidate> imageCandidates;
    QVector<OutgoingMessagePart> imageAttachments;
    AggregateAiBuiltRequest built;
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
    AggregateAiBuiltRequest buildAggregateReplyRequest(
        int conversationId,
        const QString& sessionModelKey,
        const QList<KnowledgeSnippetContext>& knowledgeSnippets = {},
        const AggregateReplyStrategy& strategy = {},
        const ReplyIntentDecision& intent = {},
        const ReplyActionPlan& actionPlan = {}) const;
    AggregateAiBuiltRequest buildRobotSandboxReplyRequest(
        const QString& sessionModelKey,
        const QList<AiConversationTurn>& recentTurns,
        const QString& latestUserText,
        const QList<KnowledgeSnippetContext>& knowledgeSnippets = {},
        const AggregateReplyStrategy& strategy = {},
        const ReplyIntentDecision& intent = {},
        const ReplyActionPlan& actionPlan = {}) const;
    ReplyContextResult buildReplyContext(const ReplyContextInput& input) const;
    AggregateAiBuiltRequest buildAggregateCustomerProfileRequest(int conversationId,
                                                                 const QString& sessionModelKey) const;
    IAiStreamingSession* createSession(const AiProviderConfig& config,
                                       const AiRequest& request,
                                       QObject* parent) const;

private:
    ReplyIntentDecision classifyReplyIntent(const ReplyContextInput& input,
                                            const QString& latestText,
                                            const QString& latestImagePath,
                                            const QList<AiConversationTurn>& recentTurns,
                                            const QString& platform,
                                            bool latestIsImageOnly) const;
    QNetworkAccessManager* m_network = nullptr;
    AiServiceFacade* m_facade = nullptr;
};

#endif // AICHATAPPSERVICE_H
