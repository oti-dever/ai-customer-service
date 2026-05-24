#include "aichatappservice.h"

#include "../../data/messagedao.h"
#include "../ai/aiservicefacade.h"
#include "../ai/aistreamingsession.h"

#include <QFileInfo>
#include <QNetworkAccessManager>

namespace {

QString aggregateAiMvpSystemPrompt()
{
    return QStringLiteral(
        "你是电商客服场景的辅助起草助手。请根据用户给出的「客户最后一条入站消息」（可能附带聊天区截图）和下列店铺知识，起草一条可直接发送给客户的回复正文。\n"
        "要求：语气专业、友好；不要编造未在知识中出现的承诺；不要加「客服：」等前缀或引号；若有截图，请结合画面理解客户意图。\n"
        "篇幅：默认简短。订单、物流、商品规格等常见询问用几句话说明要点即可（约 80～200 字量级），不要长篇铺垫、不要展开无关背景；仅在客户问题本身很复杂或明确要求详细说明时再适当增加。\n\n"
        "【店铺知识·MVP 占位，可随版本替换】\n"
        "礼貌问候；明确订单、物流、售后的查询途径；避免无法兑现的承诺。具体规则以公司内部文档为准。");
}

} // namespace

AiChatAppService::AiChatAppService(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_facade(new AiServiceFacade(m_network, this))
{
}

AiProviderConfig AiChatAppService::resolveProviderConfig(const QString& sessionModelKey,
                                                         const QString& baseUrlOverride,
                                                         const QString& modelOverride,
                                                         const QString& apiKeyOverride,
                                                         const AiConfigLoadOptions& options) const
{
    AiProviderConfig config = loadAiProviderConfig(sessionModelKey, options);
    config.sessionModelKey = sessionModelKey;
    if (!baseUrlOverride.trimmed().isEmpty())
        config.baseUrl = baseUrlOverride.trimmed();
    if (!modelOverride.trimmed().isEmpty())
        config.model = modelOverride.trimmed();
    if (!apiKeyOverride.trimmed().isEmpty())
        config.apiKey = apiKeyOverride.trimmed();
    if (config.model.isEmpty())
        config.model = aiPresetDefinition(sessionModelKey).defaultModel;
    return config;
}

AggregateAiBuiltRequest AiChatAppService::buildAggregateReplyRequest(int conversationId,
                                                                     const QString& sessionModelKey) const
{
    AggregateAiBuiltRequest built;

    AiConfigLoadOptions loadOptions;
    loadOptions.allowAggregateFallback = true;
    loadOptions.allowGeneralFallback = true;
    built.config = resolveProviderConfig(sessionModelKey, QString(), QString(), QString(), loadOptions);

    if (built.config.apiKey.trimmed().isEmpty()) {
        built.failure = AggregateAiBuildFailure::MissingApiKey;
        built.failureDetail = QStringLiteral("缺少 API Key");
        return built;
    }
    if (built.config.baseUrl.trimmed().isEmpty() || built.config.model.trimmed().isEmpty()) {
        built.failure = AggregateAiBuildFailure::IncompleteModelConfig;
        built.failureDetail = QStringLiteral("模型配置不完整");
        return built;
    }

    MessageDao dao;
    const auto snap = dao.latestInboundSnapshot(conversationId);
    if (!snap) {
        built.failure = AggregateAiBuildFailure::MissingInboundSnapshot;
        built.failureDetail = QStringLiteral("无入站快照");
        return built;
    }

    const QString imgPath = snap->contentImagePath.trimmed();
    const bool pathRecorded = !imgPath.isEmpty();
    const bool fileOk = pathRecorded && QFileInfo(imgPath).isFile();
    if (pathRecorded && !fileOk) {
        built.failure = AggregateAiBuildFailure::MissingInboundImage;
        built.failureDetail = QStringLiteral("聊天区截图文件不可用");
        return built;
    }

    const QString textInbound = snap->content.trimmed();
    const bool hasUsableImage = fileOk;
    if (textInbound.isEmpty() && !hasUsableImage) {
        built.failure = AggregateAiBuildFailure::EmptyInbound;
        built.failureDetail = QStringLiteral("入站文本与图片均空");
        return built;
    }

    if (hasUsableImage && !built.config.capabilities.supportsVisionDataUrl) {
        built.failure = AggregateAiBuildFailure::VisionUnsupported;
        built.failureDetail = QStringLiteral("多模态需支持视觉的模型");
        return built;
    }

    built.request.systemPrompt = aggregateAiMvpSystemPrompt();
    AiConversationTurn userTurn;
    userTurn.role = QStringLiteral("user");
    if (hasUsableImage)
        userTurn.parts.append(makeAiImageFilePart(imgPath));
    userTurn.parts.append(makeAiTextPart(
        hasUsableImage
            ? QStringLiteral("【客户最后一条入站】\n%1")
                  .arg(textInbound.isEmpty()
                           ? QStringLiteral("（无 OCR 文本，请根据上图理解客户意图）")
                           : textInbound)
            : QStringLiteral("【客户最后一条入站消息】\n%1").arg(textInbound)));
    built.request.turns = {userTurn};
    return built;
}

IAiStreamingSession* AiChatAppService::createSession(const AiProviderConfig& config,
                                                     const AiRequest& request,
                                                     QObject* parent) const
{
    return m_facade->createSession(config, request, parent);
}
