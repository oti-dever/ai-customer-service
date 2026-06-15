#include "aichatappservice.h"

#include "../../data/messagedao.h"
#include "../ai/aiservicefacade.h"
#include "../ai/aistreamingsession.h"

#include <QFileInfo>
#include <QNetworkAccessManager>

namespace {

constexpr int kAggregateRecentHistoryLimit = 12;
constexpr int kAggregateMessageTextLimit = 1600;

QString boundedText(QString text, int maxChars = kAggregateMessageTextLimit)
{
    text = text.trimmed();
    if (maxChars <= 0 || text.size() <= maxChars)
        return text;
    return text.left(maxChars).trimmed() + QStringLiteral("...");
}

QString roleForAggregateMessage(const MessageRecord& msg)
{
    if (msg.direction == QLatin1String("out"))
        return QStringLiteral("assistant");
    if (msg.direction == QLatin1String("system"))
        return QStringLiteral("system");
    return QStringLiteral("user");
}

QList<AiConversationTurn> buildAggregateHistoryTurns(const QVector<MessageRecord>& messages)
{
    QList<AiConversationTurn> turns;
    const int start = qMax(0, messages.size() - kAggregateRecentHistoryLimit);
    for (int i = start; i < messages.size(); ++i) {
        const QString text = boundedText(messages.at(i).content);
        if (text.isEmpty())
            continue;
        turns.append(makeAiTextTurn(roleForAggregateMessage(messages.at(i)), text));
    }
    return turns;
}

QString aggregateAiMvpSystemPrompt()
{
    return QStringLiteral(
        "你是电商客服场景的辅助起草助手。请根据最近聊天记录、客户最新入站消息（可能附带聊天区截图）和下列店铺知识，起草一条可直接发送给客户的回复正文。\n"
        "要求：语气专业、友好；不要编造未在上下文或知识中出现的承诺；不要加「客服：」等前缀或引号；若有截图，请结合画面理解客户意图。\n"
        "篇幅：默认简短。订单、物流、商品规格、售后等常见询问用几句话说明要点即可（约 80～200 字量级），不要长篇铺垫、不要展开无关背景；仅在客户问题本身很复杂或明确要求详细说明时再适当增加。\n\n"
        "【店铺知识·MVP 占位，可随版本替换】\n"
        "礼貌问候；明确订单、物流、售后的查询途径；避免无法兑现的承诺。具体规则以公司内部文档为准。");
}

QString aggregateCustomerProfileSystemPrompt()
{
    return QStringLiteral(
        "你是电商客服工作台的客户信息整理助手。请只根据最近聊天记录提炼对后续接待有用的客户信息。"
        "不要编造未出现的信息，不要推断敏感身份、购买力、年龄、性别等隐私标签。"
        "内容必须简短，适合显示在客服右侧栏。\n"
        "请只输出合法 JSON，不要 Markdown，不要解释。字段固定为：\n"
        "{"
        "\"summary\":\"一句话概括当前客户情况\","
        "\"concerns\":[\"关注点，最多3条\"],"
        "\"preferences\":[\"沟通偏好或明确要求，最多3条\"],"
        "\"risks\":[\"需要客服注意的风险或禁忌，最多3条\"],"
        "\"current_need\":\"当前最直接诉求\""
        "}"
        "没有明确内容的数组返回 []，字符串返回空字符串。");
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
    const auto snap = dao.latestCachedInboundSnapshot(conversationId);
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
    built.request.turns = buildAggregateHistoryTurns(dao.listCachedMessages(conversationId, 40));

    AiConversationTurn userTurn;
    userTurn.role = QStringLiteral("user");
    if (hasUsableImage)
        userTurn.parts.append(makeAiImageFilePart(imgPath));
    userTurn.parts.append(makeAiTextPart(
        hasUsableImage
            ? QStringLiteral("请结合上面的最近聊天记录、下方客户最新入站内容和这张聊天区截图，生成本条客服回复。\n\n【客户最新入站】\n%1")
                  .arg(textInbound.isEmpty()
                           ? QStringLiteral("（无 OCR 文本，请根据截图理解客户意图）")
                           : textInbound)
            : QStringLiteral("请结合上面的最近聊天记录和下方客户最新入站内容，生成本条客服回复。\n\n【客户最新入站】\n%1").arg(textInbound)));
    built.request.turns.append(userTurn);
    return built;
}

IAiStreamingSession* AiChatAppService::createSession(const AiProviderConfig& config,
                                                     const AiRequest& request,
                                                     QObject* parent) const
{
    return m_facade->createSession(config, request, parent);
}

AggregateAiBuiltRequest AiChatAppService::buildAggregateCustomerProfileRequest(int conversationId,
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
    const QVector<MessageRecord> messages = dao.listCachedMessages(conversationId, 40);
    const QList<AiConversationTurn> turns = buildAggregateHistoryTurns(messages);
    if (turns.isEmpty()) {
        built.failure = AggregateAiBuildFailure::EmptyInbound;
        built.failureDetail = QStringLiteral("暂无可整理的聊天记录");
        return built;
    }

    built.request.systemPrompt = aggregateCustomerProfileSystemPrompt();
    built.request.turns = turns;
    built.request.turns.append(makeAiTextTurn(
        QStringLiteral("user"),
        QStringLiteral("请根据上面的最近聊天记录，整理这位客户的信息。只输出指定 JSON。")));
    return built;
}
