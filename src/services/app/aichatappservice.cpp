#include "aichatappservice.h"

#include "../../data/conversationdao.h"
#include "../../data/messagedao.h"
#include "../../ipc/ipcservice.h"
#include "../ai/airequestassembler.h"
#include "../ai/aiservicefacade.h"
#include "../ai/aistreamingsession.h"
#include "../ai/openaicompatclient.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

namespace {

constexpr int kAggregateRecentHistoryLimit = 10;
constexpr int kAggregateMessageTextLimit = 1600;

QString boundedText(QString text, int maxChars = kAggregateMessageTextLimit)
{
    text = text.trimmed();
    if (maxChars <= 0 || text.size() <= maxChars)
        return text;
    return text.left(maxChars).trimmed() + QStringLiteral("...");
}

bool isImagePlaceholderText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    return normalized == QLatin1String("[image]")
        || normalized == QLatin1String("image")
        || normalized == QStringLiteral("[图片]")
        || normalized == QStringLiteral("图片");
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

QString aggregateAiMvpSystemPrompt(const AggregateReplyStrategy& strategy)
{
    QString prompt = QStringLiteral(
        "你是电商客服场景的辅助起草助手。请起草一条可直接发送给客户的回复正文。\n"
        "任务优先级：必须优先回应【客户最新入站】中的最后一条消息；最近 10 条聊天记录只作为理解上下文、称呼、订单背景、客户偏好和避免重复追问的参考，不要喧宾夺主。\n"
        "重复控制：不要把历史里已经回答过的问题重新回答一遍；如果客户最新消息是追问，只回答追问点；如果客户最新消息换了话题，不要把旧话题再次展开。\n"
        "场景处理：如果客户最新消息只是商品链接、商品卡片或商品 ID，且没有明确咨询问题，请按新客户/浏览商品场景接待，亲切欢迎并询问客户想了解规格、价格、发货、售后还是其他问题；可以简短介绍店铺主营键盘、键帽和相关配件；不要编造该商品的具体规格、库存、价格或活动。\n"
        "明确咨询：如果客户明确咨询商品、物流、售后、价格、活动、发票等问题，请直接回答最新问题，必要时结合聊天记录和店铺知识库片段；知识库没有覆盖时，不要编造承诺，可说明需要进一步核实。\n"
        "短确认处理：如果客户最新消息只是“可以”“好”“嗯”“行”“谢谢”等短确认，请结合最近聊天记录判断语境；如果上一轮已经完整回答，就自然收口，例如提示有其他问题随时联系；如果上一轮是在询问客户是否需要推荐、补充说明或下一步操作，才继续推进对应事项，不要把上一轮内容同义重复一遍。\n"
        "信息不足：只问一个最关键的补充问题，不要连续追问多个问题。遇到情绪、催促或投诉时，先安抚，再给出当前能做的处理或需要补充的信息。\n"
        "语气：亲切、温和、不失热情，可自然使用“亲”“宝子”等称呼，但不要堆砌；避免生硬、冷冰冰或过度营销。\n"
        "输出格式：只输出可直接发送给客户的纯文本正文；不要 Markdown；不要项目符号列表；不要表格；不要代码块；不要标题；不要加「客服：」等前缀；不要使用引号包裹整段回复；不要带 emoji 表情；不要输出任何外部链接、网址、URL 或跳转口令。\n"
        "篇幅：默认简短，通常 1 到 3 句话即可；仅在客户问题本身很复杂或明确要求详细说明时再适当增加。\n"
        "若有客户发来的图片：图片只用于辅助理解最新入站中无法从文本识别的信息；如果最新文本已经明确，请不要引用图片或历史中无关的旧商品、旧问题。\n"
        "真人口吻：不要在回复客户时说“截图”“页面”“屏幕”“画面中”等机器感表达；需要引用视觉信息时，优先说“从图片看”“图片里看”。");
    const QString tone = strategy.replyTone.trimmed();
    const QString addresses = strategy.commonAddressTerms.trimmed();
    if (!tone.isEmpty() || !addresses.isEmpty()
        || strategy.allowAutoSendImages || strategy.allowAutoSendMultiMessages) {
        QStringList lines;
        lines << QStringLiteral("【当前机器人回复策略】");
        if (!tone.isEmpty())
            lines << QStringLiteral("回复语气：%1。").arg(tone);
        if (!addresses.isEmpty())
            lines << QStringLiteral("常用称呼：可自然使用 %1，但不要每句话都强行添加，也不要堆砌。").arg(addresses);
        const int maxMessages = strategy.allowAutoSendMultiMessages
            ? qBound(1, strategy.maxAutoSendMessages, 3)
            : 1;
        lines << (maxMessages > 1
                      ? QStringLiteral("消息条数：默认仍生成 1 条；只有确有必要补充不同信息时，才最多生成 %1 条。多条消息必须用单独一行 ---MSG--- 分隔。每条消息必须承担不同作用，严禁同义重复。").arg(maxMessages)
                      : QStringLiteral("消息条数：只生成一条可直接发送的消息，不要用分隔符模拟多条消息。"));
        lines << (strategy.allowAutoSendImages
                      ? QStringLiteral("附图策略：如果系统另有建议附图，文字回复可以自然承接，不要承诺不存在的图片。")
                      : QStringLiteral("附图策略：自动回复默认只发送文字，不要在回复里说“已附图”或“见附图”。"));
        prompt += QStringLiteral("\n") + lines.join(QLatin1Char('\n'));
    }
    return prompt;
}

QString formatKnowledgePromptBlock(const QList<KnowledgeSnippetContext>& snippets)
{
    if (snippets.isEmpty())
        return {};

    QStringList lines;
    lines << QStringLiteral("【店铺知识库检索结果】");
    lines << QStringLiteral("以下内容是从店铺知识库检索到的原文片段。涉及商品、售后、物流、价格、活动、发票等事实问题时，必须优先依据这些片段回答。没有覆盖的信息不要编造承诺。");
    lines << QString();

    int count = 0;
    int totalChars = 0;
    for (const KnowledgeSnippetContext& item : snippets) {
        QString snippet = item.snippet.trimmed();
        if (snippet.isEmpty())
            continue;
        if (snippet.size() > 500)
            snippet = snippet.left(500).trimmed() + QStringLiteral("...");
        if (totalChars + snippet.size() > 2000)
            break;

        ++count;
        totalChars += snippet.size();
        const QString source = item.sourceTitle.trimmed().isEmpty()
            ? QStringLiteral("未命名文档")
            : item.sourceTitle.trimmed();
        const QString titlePath = item.titlePath.trimmed();
        lines << QStringLiteral("%1. 来源：%2%3")
                     .arg(count)
                     .arg(source)
                     .arg(titlePath.isEmpty() ? QString() : QStringLiteral(" / %1").arg(titlePath));
        lines << QStringLiteral("原文：%1").arg(snippet);
        lines << QString();
        if (count >= 5)
            break;
    }
    if (count <= 0)
        return {};

    lines << QStringLiteral("要求：");
    lines << QStringLiteral("- 回复客户时不要暴露 chunk_id、score、base_id 等内部字段。");
    lines << QStringLiteral("- 如果知识库片段不足以确认结论，请引导客户补充信息或说明需要进一步核实。");
    lines << QStringLiteral("- 如果知识库片段明确出现“是否有图=是”或“图片名=...”，说明系统已有对应图片素材；客户索要图片时不要回答“没有图片/暂无实拍图”，可自然说明“给您看下实拍图”。");
    lines << QStringLiteral("- 不要输出“根据知识库”这类生硬表述。");
    lines << QStringLiteral("- 即使原文片段包含链接、网址、URL、图片空间链接或第三方跳转信息，回复客户时也不要原样输出这些链接，避免外部链接带来账号风险。");
    lines << QStringLiteral("- 不要照搬原文的 Markdown、编号、表格或标题格式，请转写成自然客服口吻的纯文本回复。");
    return lines.join(QLatin1Char('\n')).trimmed();
}

QString jsonStringArrayLabel(const QJsonArray& values)
{
    QStringList out;
    out.reserve(values.size());
    for (const QJsonValue& value : values) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    out.removeDuplicates();
    return out.join(QStringLiteral(", "));
}

bool riskTagsEmpty(const QString& text)
{
    const QString value = text.trimmed();
    return value.isEmpty()
        || value.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("no_risk"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("无"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("无风险"), Qt::CaseInsensitive) == 0;
}

bool looksLikeImageFilename(const QString& query)
{
    const QString text = query.trimmed().toLower();
    return text.endsWith(QStringLiteral(".png"))
        || text.endsWith(QStringLiteral(".jpg"))
        || text.endsWith(QStringLiteral(".jpeg"))
        || text.endsWith(QStringLiteral(".webp"))
        || text.endsWith(QStringLiteral(".gif"))
        || text.endsWith(QStringLiteral(".bmp"));
}

bool queryRequestsImageAttachment(const QString& query)
{
    const QString text = query.trimmed().toLower();
    if (text.isEmpty())
        return false;
    if (looksLikeImageFilename(text))
        return true;
    const QStringList linkAddressTerms = {
        QStringLiteral("看图地址"),
        QStringLiteral("看圖地址"),
        QStringLiteral("店铺地址"),
        QStringLiteral("店鋪地址"),
        QStringLiteral("粉丝群"),
        QStringLiteral("粉絲群"),
        QStringLiteral("链接"),
        QStringLiteral("連結"),
        QStringLiteral("网址"),
        QStringLiteral("網址"),
        QStringLiteral("url"),
    };
    const QStringList explicitImageTerms = {
        QStringLiteral("实拍"),
        QStringLiteral("图片"),
        QStringLiteral("照片"),
        QStringLiteral("商品图"),
        QStringLiteral("效果图"),
    };
    bool looksLikeAddressRequest = false;
    for (const QString& term : linkAddressTerms) {
        if (text.contains(term)) {
            looksLikeAddressRequest = true;
            break;
        }
    }
    bool explicitlyRequestsImage = false;
    for (const QString& term : explicitImageTerms) {
        if (text.contains(term)) {
            explicitlyRequestsImage = true;
            break;
        }
    }
    if (looksLikeAddressRequest && !explicitlyRequestsImage)
        return false;
    const QStringList keywords = {
        QStringLiteral("实拍"),
        QStringLiteral("外观图"),
        QStringLiteral("外观"),
        QStringLiteral("细节图"),
        QStringLiteral("细节"),
        QStringLiteral("商品图"),
        QStringLiteral("图片"),
        QStringLiteral("照片"),
        QStringLiteral("配色图"),
        QStringLiteral("效果图"),
        QStringLiteral("发图"),
        QStringLiteral("看图"),
        QStringLiteral("有图"),
        QStringLiteral("图看看"),
        QStringLiteral("发我看"),
        QStringLiteral("看一下外观"),
        QStringLiteral("长什么样"),
        QStringLiteral("什么样子"),
        QStringLiteral("photo"),
        QStringLiteral("picture"),
        QStringLiteral("image"),
    };
    for (const QString& keyword : keywords) {
        if (text.contains(keyword))
            return true;
    }
    return false;
}

QString normalizedImageText(QString text)
{
    text = text.toLower();
    QString out;
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber())
            out.append(ch);
    }
    return out;
}

QString imageObjectFocus(QString query)
{
    query = query.toLower().trimmed();
    const QStringList generic = {
        QStringLiteral("看下"),
        QStringLiteral("看看"),
        QStringLiteral("看一下"),
        QStringLiteral("发下"),
        QStringLiteral("发一个"),
        QStringLiteral("发个"),
        QStringLiteral("有"),
        QStringLiteral("没有"),
        QStringLiteral("实拍图"),
        QStringLiteral("实拍"),
        QStringLiteral("图片"),
        QStringLiteral("照片"),
        QStringLiteral("图"),
        QStringLiteral("的"),
        QStringLiteral("吗"),
        QStringLiteral("呢"),
        QStringLiteral("么"),
        QStringLiteral("呗"),
        QStringLiteral("可以"),
        QStringLiteral("给我"),
        QStringLiteral("给"),
        QStringLiteral("了"),
    };
    for (const QString& word : generic)
        query.replace(word, QString());
    query = normalizedImageText(query);
    return query.size() >= 2 ? query : QString();
}

QStringList productModelsInText(const QString& text)
{
    QSet<QString> models;
    static const QRegularExpression modelRegex(
        QStringLiteral("K\\s*(68|87|98)\\s*(?:Pro|Max|Lite)?"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = modelRegex.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString number = match.captured(1).trimmed();
        if (!number.isEmpty())
            models.insert(QStringLiteral("K%1").arg(number));
    }
    QStringList out = models.values();
    out.sort();
    return out;
}

bool candidateMatchesProductFocus(const ReplyImageCandidate& candidate, const QString& productFocus)
{
    const QString focus = productFocus.trimmed().toUpper();
    if (focus.isEmpty())
        return true;
    const QString haystack = QStringLiteral("%1\n%2\n%3\n%4\n%5\n%6")
                                 .arg(candidate.sourceTitle,
                                      candidate.originalFilename,
                                      candidate.filePath,
                                      candidate.summary,
                                      candidate.tags,
                                      candidate.scenarios)
                                 .toUpper();
    return haystack.contains(focus);
}

bool isProductCardWithoutExplicitQuestion(const QString& text)
{
    QString normalized = text.trimmed();
    if (normalized.isEmpty())
        return false;
    normalized.remove(QRegularExpression(QStringLiteral("\\s+")));

    const bool looksLikeProductCard =
        normalized.contains(QStringLiteral("商品ID"), Qt::CaseInsensitive)
        || normalized.contains(QStringLiteral("商品链接"))
        || normalized.contains(QStringLiteral("商品卡片"))
        || normalized.contains(QStringLiteral("查看商品规格"));
    if (!looksLikeProductCard)
        return false;

    QString questionText = normalized;
    questionText.replace(QStringLiteral("查看商品规格"), QString());
    questionText.replace(QStringLiteral("复制"), QString());

    const QStringList questionSignals = {
        QStringLiteral("吗"), QStringLiteral("呢"), QStringLiteral("?"), QStringLiteral("？"),
        QStringLiteral("怎么"), QStringLiteral("多少"), QStringLiteral("几"),
        QStringLiteral("有没有"), QStringLiteral("能不能"), QStringLiteral("可以"),
        QStringLiteral("是不是"), QStringLiteral("发货"), QStringLiteral("退"),
        QStringLiteral("换"), QStringLiteral("保修"), QStringLiteral("售后"),
        QStringLiteral("价格"), QStringLiteral("优惠"), QStringLiteral("活动"),
        QStringLiteral("规格"), QStringLiteral("尺寸"), QStringLiteral("材质"),
        QStringLiteral("库存"),
    };
    for (const QString& signal : questionSignals) {
        if (questionText.contains(signal, Qt::CaseInsensitive))
            return false;
    }
    return normalized.size() <= 80;
}

bool shouldSupplementKnowledgeQueryWithContext(const QString& latestText)
{
    QString normalized = latestText.trimmed();
    if (normalized.isEmpty() || normalized.size() > 80)
        return false;
    normalized.remove(QRegularExpression(QStringLiteral("\\s+")));

    const QStringList pronounSignals = {
        QStringLiteral("这个"), QStringLiteral("这款"), QStringLiteral("这个商品"),
        QStringLiteral("这款商品"), QStringLiteral("它"), QStringLiteral("那个"),
        QStringLiteral("上面"), QStringLiteral("刚才"),
    };
    for (const QString& signal : pronounSignals) {
        if (normalized.contains(signal))
            return true;
    }

    const QStringList shortQuestionSignals = {
        QStringLiteral("可以吗"), QStringLiteral("能吗"), QStringLiteral("能不能"),
        QStringLiteral("行吗"), QStringLiteral("怎么处理"), QStringLiteral("怎么弄"),
        QStringLiteral("多少钱"), QStringLiteral("有货吗"), QStringLiteral("能退吗"),
        QStringLiteral("能换吗"), QStringLiteral("保修吗"), QStringLiteral("发货吗"),
    };
    for (const QString& signal : shortQuestionSignals) {
        if (normalized.contains(signal))
            return true;
    }
    return false;
}

bool isShortCustomerAcknowledgement(const QString& text)
{
    QString normalized = text.trimmed().toLower();
    normalized.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (normalized.isEmpty() || normalized.size() > 8)
        return false;
    const QSet<QString> exact = {
        QStringLiteral("好"),
        QStringLiteral("好的"),
        QStringLiteral("可以"),
        QStringLiteral("行"),
        QStringLiteral("嗯"),
        QStringLiteral("嗯嗯"),
        QStringLiteral("哦"),
        QStringLiteral("谢谢"),
        QStringLiteral("行吧"),
        QStringLiteral("可以的"),
        QStringLiteral("ok"),
        QStringLiteral("OK").toLower(),
    };
    return exact.contains(normalized);
}

bool previousReplyExpectsContinuation(const QString& text)
{
    const QString normalized = text.trimmed();
    if (normalized.isEmpty())
        return false;
    const QStringList continuationSignals = {
        QStringLiteral("需要的话"),
        QStringLiteral("要的话"),
        QStringLiteral("可以帮您"),
        QStringLiteral("给您推荐"),
        QStringLiteral("发图"),
        QStringLiteral("实拍图"),
        QStringLiteral("继续处理"),
        QStringLiteral("帮您确认"),
    };
    for (const QString& signal : continuationSignals) {
        if (normalized.contains(signal))
            return true;
    }
    return false;
}

bool isImageOnlyKnowledgeQuery(const QString& latestText, const QString& imagePath)
{
    if (imagePath.trimmed().isEmpty())
        return false;
    return isImagePlaceholderText(latestText) || latestText.trimmed().isEmpty();
}

QString firstTextPart(const AiConversationTurn& turn)
{
    for (const AiMessagePart& part : turn.parts) {
        if (part.kind == AiMessagePartKind::Text && !part.text.trimmed().isEmpty())
            return part.text.trimmed();
    }
    return {};
}

QString roleLabelForKnowledge(const QString& role)
{
    const QString value = role.trimmed().toLower();
    if (value == QLatin1String("assistant") || value == QLatin1String("out"))
        return QStringLiteral("我方");
    if (value == QLatin1String("system"))
        return QStringLiteral("系统");
    return QStringLiteral("客户");
}

QString buildKnowledgeSearchQueryFromTurns(const QString& latestText,
                                           const QList<AiConversationTurn>& recentTurns)
{
    const QString latest = latestText.trimmed().left(800);
    if (isShortCustomerAcknowledgement(latest)) {
        for (int i = recentTurns.size() - 1; i >= 0; --i) {
            const AiConversationTurn& turn = recentTurns.at(i);
            if (turn.role != QLatin1String("assistant"))
                continue;
            const QString previousReply = firstTextPart(turn).left(500);
            if (previousReplyExpectsContinuation(previousReply)) {
                return QStringLiteral("客户已确认继续上一项服务。\n上一条客服消息：%1")
                    .arg(previousReply)
                    .left(800);
            }
            return {};
        }
        return {};
    }
    if (!shouldSupplementKnowledgeQueryWithContext(latest))
        return latest;

    QStringList contextLines;
    for (int i = recentTurns.size() - 1; i >= 0 && contextLines.size() < 2; --i) {
        const AiConversationTurn& turn = recentTurns.at(i);
        QString content = firstTextPart(turn);
        if (content.isEmpty() || content == latestText.trimmed())
            continue;
        content = content.left(160);
        contextLines.prepend(QStringLiteral("%1：%2").arg(roleLabelForKnowledge(turn.role), content));
    }
    if (contextLines.isEmpty())
        return latest;

    return QStringLiteral("客户最新咨询：%1\n最近上下文（仅用于补全指代）：%2")
        .arg(latest, contextLines.join(QStringLiteral(" / ")))
        .left(1000);
}

QString buildKnowledgeSearchQueryFromMessages(const QString& latestText,
                                              const QVector<MessageRecord>& recentMessages)
{
    QList<AiConversationTurn> turns;
    turns.reserve(recentMessages.size());
    for (const MessageRecord& message : recentMessages)
        turns.append(makeAiTextTurn(roleForAggregateMessage(message), message.content));
    return buildKnowledgeSearchQueryFromTurns(latestText, turns);
}

bool containsExternalLinkRequest(const QString& text)
{
    const QString value = text.trimmed().toLower();
    if (value.isEmpty())
        return false;
    const QStringList keywords = {
        QStringLiteral("链接"),
        QStringLiteral("連結"),
        QStringLiteral("网址"),
        QStringLiteral("網址"),
        QStringLiteral("url"),
        QStringLiteral("http"),
        QStringLiteral("邮箱"),
        QStringLiteral("郵箱"),
        QStringLiteral("邮件"),
        QStringLiteral("郵件"),
        QStringLiteral("email"),
        QStringLiteral("发我地址"),
        QStringLiteral("發我地址"),
        QStringLiteral("下载地址"),
        QStringLiteral("下載地址"),
        QStringLiteral("看图地址"),
        QStringLiteral("看圖地址"),
        QStringLiteral("店铺地址"),
        QStringLiteral("店鋪地址"),
        QStringLiteral("粉丝群"),
        QStringLiteral("粉絲群"),
        QStringLiteral("群链接"),
        QStringLiteral("群連結"),
    };
    for (const QString& keyword : keywords) {
        if (value.contains(keyword))
            return true;
    }
    return false;
}

QString extractEmailAddress(const QString& text)
{
    static const QRegularExpression emailRegex(
        QStringLiteral(R"(([A-Z0-9._%+\-]+@[A-Z0-9.\-]+\.[A-Z]{2,}))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = emailRegex.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString maskEmailAddressForPrompt(const QString& email)
{
    const QString trimmed = email.trimmed();
    const int at = trimmed.indexOf(QLatin1Char('@'));
    if (at <= 0)
        return trimmed.isEmpty() ? QString() : QStringLiteral("[email_provided]");
    const QString local = trimmed.left(at);
    const QString domain = trimmed.mid(at + 1);
    const QString visible = local.left(qMin(2, local.size()));
    return QStringLiteral("%1***@%2").arg(visible, domain);
}

bool recentContextSuggestsEmailDeliveryWorkflow(const QList<AiConversationTurn>& recentTurns)
{
    const int start = qMax(0, recentTurns.size() - 6);
    for (int i = start; i < recentTurns.size(); ++i) {
        const QString text = firstTextPart(recentTurns.at(i)).trimmed();
        if (text.isEmpty())
            continue;
        if (containsExternalLinkRequest(text))
            return true;
    }
    return false;
}

struct EmailTemplateCatalogItem
{
    QString templateId;
    QString name;
    QString scene;
    QStringList aliases;
    bool enabled = true;
};

QStringList jsonStringListValue(const QJsonValue& value)
{
    QStringList out;
    const QJsonArray arr = value.toArray();
    out.reserve(arr.size());
    for (const QJsonValue& item : arr) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    out.removeDuplicates();
    return out;
}

QStringList jsonStringListFromArray(const QJsonArray& arr)
{
    QStringList out;
    out.reserve(arr.size());
    for (const QJsonValue& value : arr) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    out.removeDuplicates();
    return out;
}

QList<EmailTemplateCatalogItem> parseEmailTemplateCatalog(const QJsonObject& response)
{
    QList<EmailTemplateCatalogItem> out;
    const QJsonArray templates = response.value(QStringLiteral("templates")).toArray();
    out.reserve(templates.size());
    for (const QJsonValue& value : templates) {
        const QJsonObject object = value.toObject();
        EmailTemplateCatalogItem item;
        item.templateId = object.value(QStringLiteral("template_id")).toString().trimmed();
        item.name = object.value(QStringLiteral("name")).toString().trimmed();
        item.scene = object.value(QStringLiteral("scene")).toString(QStringLiteral("store_view_link")).trimmed();
        item.aliases = jsonStringListFromArray(object.value(QStringLiteral("aliases")).toArray());
        item.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        if (!item.templateId.isEmpty() && !item.name.isEmpty() && item.enabled)
            out.append(item);
    }
    return out;
}

QList<EmailTemplateCatalogItem> fetchEmailTemplateCatalogForRouter()
{
    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    QString error;
    const QJsonObject response =
        Ipc::IpcService::instance().fetchEmailTemplates(false, 1500, &status, &error);
    if (status != Ipc::ResponseStatus::Success
        || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
        qInfo() << "[AiChatAppService] email template catalog unavailable for intent router"
                << "status=" << Ipc::toString(status)
                << "error=" << error.left(120);
        return {};
    }
    return parseEmailTemplateCatalog(response);
}

QString normalizeTemplateMatchText(QString text)
{
    text = text.trimmed().toLower();
    text.remove(QRegularExpression(QStringLiteral(R"(\s+)")));
    return text;
}

QString matchEmailTemplateIdFromText(const QString& text,
                                     const QList<EmailTemplateCatalogItem>& templates,
                                     QString* matchedNameOut = nullptr)
{
    const QString normalized = normalizeTemplateMatchText(text);
    if (normalized.isEmpty())
        return {};
    for (const EmailTemplateCatalogItem& item : templates) {
        QStringList keys = item.aliases;
        keys << item.name;
        for (const QString& key : keys) {
            const QString normalizedKey = normalizeTemplateMatchText(key);
            if (normalizedKey.size() >= 2 && normalized.contains(normalizedKey)) {
                if (matchedNameOut)
                    *matchedNameOut = key;
                return item.templateId;
            }
        }
    }
    return {};
}

QString matchEmailTemplateIdFromContext(const QString& latestText,
                                        const QList<AiConversationTurn>& recentTurns,
                                        const QList<EmailTemplateCatalogItem>& templates,
                                        QString* matchedNameOut = nullptr)
{
    const QString latestMatch = matchEmailTemplateIdFromText(latestText, templates, matchedNameOut);
    if (!latestMatch.isEmpty())
        return latestMatch;
    const int start = qMax(0, recentTurns.size() - 8);
    for (int i = recentTurns.size() - 1; i >= start; --i) {
        const QString text = firstTextPart(recentTurns.at(i));
        const QString match = matchEmailTemplateIdFromText(text, templates, matchedNameOut);
        if (!match.isEmpty())
            return match;
    }
    return {};
}

QString formatEmailTemplateCatalogForPrompt(const QList<EmailTemplateCatalogItem>& templates)
{
    if (templates.isEmpty())
        return QStringLiteral("(empty)");
    QStringList lines;
    const int count = qMin(templates.size(), 50);
    for (int i = 0; i < count; ++i) {
        const EmailTemplateCatalogItem& item = templates.at(i);
        lines << QStringLiteral("- template_id=%1; name=%2; scene=%3; aliases=%4")
                     .arg(item.templateId,
                          boundedText(item.name, 80),
                          item.scene,
                          item.aliases.join(QStringLiteral("|")));
    }
    if (templates.size() > count)
        lines << QStringLiteral("- ... %1 more templates omitted").arg(templates.size() - count);
    return lines.join(QLatin1Char('\n'));
}

bool jsonBoolValue(const QJsonObject& object, const QString& key, bool defaultValue)
{
    const QJsonValue value = object.value(key);
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return !qFuzzyIsNull(value.toDouble());
    const QString text = value.toString().trimmed().toLower();
    if (text == QLatin1String("true") || text == QLatin1String("yes") || text == QLatin1String("1"))
        return true;
    if (text == QLatin1String("false") || text == QLatin1String("no") || text == QLatin1String("0"))
        return false;
    return defaultValue;
}

QJsonObject parseJsonObjectFromModelText(QString text, QString* errorOut = nullptr)
{
    if (errorOut)
        errorOut->clear();
    text = text.trimmed();
    text.remove(QStringLiteral("```json"), Qt::CaseInsensitive);
    text.remove(QStringLiteral("```"));
    const int firstBrace = text.indexOf(QLatin1Char('{'));
    const int lastBrace = text.lastIndexOf(QLatin1Char('}'));
    if (firstBrace >= 0 && lastBrace > firstBrace)
        text = text.mid(firstBrace, lastBrace - firstBrace + 1);

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject())
        return doc.object();
    if (errorOut)
        *errorOut = err.errorString();
    return {};
}

ReplyIntentDecision heuristicReplyIntent(const QString& latestText,
                                         const QString& latestImagePath,
                                         const QList<AiConversationTurn>& recentTurns,
                                         bool latestIsImageOnly,
                                         const QList<EmailTemplateCatalogItem>& emailTemplates)
{
    Q_UNUSED(latestImagePath)
    ReplyIntentDecision intent;
    intent.source = QStringLiteral("heuristic");
    intent.confidence = 0.65;

    const QString latest = latestText.trimmed();
    const QString email = extractEmailAddress(latest);
    const bool wantsImage = queryRequestsImageAttachment(latest) || looksLikeImageFilename(latest);
    const bool wantsLink = containsExternalLinkRequest(latest);
    const bool emailCompletesLinkWorkflow =
        !email.isEmpty() && recentContextSuggestsEmailDeliveryWorkflow(recentTurns);
    QString matchedTemplateName;
    const QString matchedTemplateId =
        matchEmailTemplateIdFromContext(latest, recentTurns, emailTemplates, &matchedTemplateName);
    const bool templateCompletesLinkWorkflow =
        !matchedTemplateId.isEmpty() && recentContextSuggestsEmailDeliveryWorkflow(recentTurns);

    if (latestIsImageOnly) {
        intent.intent = QStringLiteral("normal_question");
        intent.workflow = QStringLiteral("vision_text_reply");
        intent.reason = QStringLiteral("latest_inbound_is_image_only");
        intent.needDocSearch = false;
        intent.needImageSearch = false;
        intent.replyGoal = QStringLiteral("reply based on the inbound image and recent context");
        return intent;
    }

    if (latest.isEmpty()) {
        intent.intent = QStringLiteral("unknown");
        intent.workflow = QStringLiteral("no_usable_input");
        intent.reason = QStringLiteral("latest_text_empty");
        intent.needCustomerReply = false;
        intent.needDocSearch = false;
        intent.confidence = 0.4;
        return intent;
    }

    if (wantsLink || emailCompletesLinkWorkflow || templateCompletesLinkWorkflow) {
        intent.intent = QStringLiteral("link_request");
        intent.templateId = matchedTemplateId;
        intent.businessObjectName = matchedTemplateName;
        const bool missingTemplate = matchedTemplateId.isEmpty();
        intent.workflow = missingTemplate
            ? QStringLiteral("collect_template_for_link")
            : (email.isEmpty()
                   ? QStringLiteral("collect_email_for_link")
                   : QStringLiteral("email_service_required"));
        intent.nextAction = missingTemplate
            ? QStringLiteral("ask_template")
            : (email.isEmpty() ? QStringLiteral("ask_email") : QStringLiteral("send_email"));
        intent.reason = emailCompletesLinkWorkflow && !wantsLink
            ? QStringLiteral("email_provided_after_link_collection_context")
            : (templateCompletesLinkWorkflow && !wantsLink
                   ? QStringLiteral("template_provided_after_link_collection_context")
                   : QStringLiteral("link_or_email_keyword_detected"));
        intent.needDocSearch = false;
        intent.needImageSearch = false;
        intent.needEmail = true;
        intent.email = email;
        if (missingTemplate)
            intent.missingSlots << QStringLiteral("template");
        if (email.isEmpty() && !missingTemplate)
            intent.missingSlots << QStringLiteral("email");
        intent.riskFlags << QStringLiteral("direct_external_link_blocked");
        intent.replyGoal = missingTemplate
            ? QStringLiteral("ask the customer which store/material/template they need; do not output any URL")
            : (email.isEmpty()
            ? QStringLiteral("ask the customer to provide an email address because direct external links are blocked")
            : QStringLiteral("acknowledge the email but do not claim an email has already been sent"));
        intent.confidence = 0.78;
        return intent;
    }

    if (wantsImage) {
        intent.intent = QStringLiteral("image_request");
        intent.workflow = QStringLiteral("send_product_image");
        intent.reason = QStringLiteral("image_intent_keyword_detected");
        intent.needDocSearch = true;
        intent.needImageSearch = true;
        intent.imageQuery = latest;
        intent.replyGoal = QStringLiteral("answer briefly and attach a matched product image when available");
        intent.confidence = 0.82;
        return intent;
    }

    if (isProductCardWithoutExplicitQuestion(latest)) {
        intent.intent = QStringLiteral("normal_question");
        intent.workflow = QStringLiteral("product_card_greeting");
        intent.reason = QStringLiteral("product_card_without_explicit_question");
        intent.needDocSearch = false;
        intent.needImageSearch = false;
        intent.replyGoal = QStringLiteral("greet the customer and ask what they want to know");
        return intent;
    }

    if (isShortCustomerAcknowledgement(latest)) {
        intent.workflow = QStringLiteral("contextual_short_ack_reply");
        intent.reason = QStringLiteral("short_acknowledgement");
        intent.needDocSearch = !buildKnowledgeSearchQueryFromTurns(latest, recentTurns).trimmed().isEmpty();
        intent.needImageSearch = false;
        intent.replyGoal = QStringLiteral("respond naturally according to recent context");
        return intent;
    }

    intent.intent = QStringLiteral("normal_question");
    intent.workflow = QStringLiteral("normal_text_reply");
    intent.reason = QStringLiteral("default_text_question");
    intent.needDocSearch = true;
    intent.needImageSearch = false;
    intent.replyGoal = QStringLiteral("answer the customer's latest question with knowledge when available");
    return intent;
}

QString canonicalIntentName(QString intent)
{
    intent = intent.trimmed().toLower();
    intent.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (intent == QLatin1String("image") || intent == QLatin1String("photo_request")
        || intent == QLatin1String("picture_request"))
        return QStringLiteral("image_request");
    if (intent == QLatin1String("link") || intent == QLatin1String("url_request"))
        return QStringLiteral("link_request");
    if (intent == QLatin1String("email") || intent == QLatin1String("email_provided"))
        return QStringLiteral("link_request");
    if (intent == QLatin1String("normal") || intent == QLatin1String("question"))
        return QStringLiteral("normal_question");
    if (intent == QLatin1String("handoff") || intent == QLatin1String("human"))
        return QStringLiteral("human_handoff");
    if (intent == QLatin1String("no_reply") || intent == QLatin1String("no_response"))
        return QStringLiteral("no_reply_needed");
    if (intent == QLatin1String("normal_question")
        || intent == QLatin1String("image_request")
        || intent == QLatin1String("link_request")
        || intent == QLatin1String("human_handoff")
        || intent == QLatin1String("no_reply_needed")
        || intent == QLatin1String("unknown"))
        return intent;
    return QStringLiteral("unknown");
}

ReplyIntentDecision parseReplyIntentDecision(const QString& modelOutput,
                                             const ReplyIntentDecision& fallback,
                                             const QString& latestText)
{
    QString parseError;
    const QJsonObject object = parseJsonObjectFromModelText(modelOutput, &parseError);
    if (object.isEmpty()) {
        ReplyIntentDecision out = fallback;
        out.source = QStringLiteral("heuristic_fallback");
        out.modelRouted = false;
        out.rawJson = modelOutput.trimmed().left(2000);
        out.errorText = QStringLiteral("intent_json_parse_failed: %1").arg(parseError);
        return out;
    }

    ReplyIntentDecision out = fallback;
    out.source = QStringLiteral("ai_router");
    out.modelRouted = true;
    out.rawJson = modelOutput.trimmed().left(4000);
    out.errorText.clear();
    out.intent = canonicalIntentName(object.value(QStringLiteral("intent")).toString(fallback.intent));
    out.workflow = object.value(QStringLiteral("workflow")).toString(fallback.workflow).trimmed();
    out.reason = object.value(QStringLiteral("reason")).toString(fallback.reason).trimmed();
    out.imageQuery = object.value(QStringLiteral("image_query")).toString(fallback.imageQuery).trimmed();
    out.replyGoal = object.value(QStringLiteral("reply_goal")).toString(fallback.replyGoal).trimmed();
    out.email = object.value(QStringLiteral("email")).toString(fallback.email).trimmed();
    out.templateId = object.value(QStringLiteral("template_id")).toString(fallback.templateId).trimmed();
    out.nextAction = object.value(QStringLiteral("next_action")).toString(fallback.nextAction).trimmed();
    const QJsonObject businessObject = object.value(QStringLiteral("business_object")).toObject();
    out.businessObjectName = businessObject.value(QStringLiteral("name")).toString(fallback.businessObjectName).trimmed();
    if (out.email.isEmpty())
        out.email = extractEmailAddress(latestText);
    out.missingSlots = jsonStringListValue(object.value(QStringLiteral("missing_slots")));
    out.riskFlags = jsonStringListValue(object.value(QStringLiteral("risk_flags")));
    out.confidence = qBound(0.0, object.value(QStringLiteral("confidence")).toDouble(fallback.confidence), 1.0);

    const bool heuristicImage = queryRequestsImageAttachment(latestText) || looksLikeImageFilename(latestText);
    const bool heuristicLink = containsExternalLinkRequest(latestText);
    const bool fallbackLink = fallback.intent == QLatin1String("link_request");
    const bool confidentImageIntent = out.intent == QLatin1String("image_request") && out.confidence >= 0.60;
    const bool confidentLinkIntent = out.intent == QLatin1String("link_request") && out.confidence >= 0.55;

    if (heuristicLink || fallbackLink || confidentLinkIntent) {
        out.intent = QStringLiteral("link_request");
        out.needEmail = true;
        out.needImageSearch = false;
        out.needDocSearch = jsonBoolValue(object, QStringLiteral("need_doc_search"), false);
        if (out.templateId.isEmpty() && !out.missingSlots.contains(QStringLiteral("template")))
            out.missingSlots << QStringLiteral("template");
        if (!out.templateId.isEmpty()
            && out.email.isEmpty()
            && !out.missingSlots.contains(QStringLiteral("email")))
            out.missingSlots << QStringLiteral("email");
        if (!out.riskFlags.contains(QStringLiteral("direct_external_link_blocked")))
            out.riskFlags << QStringLiteral("direct_external_link_blocked");
        if (out.workflow.isEmpty() || out.workflow == QLatin1String("normal_text_reply"))
            out.workflow = out.templateId.isEmpty()
                ? QStringLiteral("collect_template_for_link")
                : (out.email.isEmpty()
                       ? QStringLiteral("collect_email_for_link")
                       : QStringLiteral("email_service_required"));
        if (out.nextAction.isEmpty())
            out.nextAction = out.templateId.isEmpty()
                ? QStringLiteral("ask_template")
                : (out.email.isEmpty()
                       ? QStringLiteral("ask_email")
                       : QStringLiteral("send_email"));
    } else if (heuristicImage || confidentImageIntent) {
        out.intent = QStringLiteral("image_request");
        out.needDocSearch = jsonBoolValue(object, QStringLiteral("need_doc_search"), true);
        out.needImageSearch = jsonBoolValue(object, QStringLiteral("need_image_search"), true);
        out.needEmail = false;
        if (out.imageQuery.isEmpty())
            out.imageQuery = latestText.trimmed();
        if (out.workflow.isEmpty() || out.workflow == QLatin1String("normal_text_reply"))
            out.workflow = QStringLiteral("send_product_image");
    } else if (out.intent == QLatin1String("human_handoff")) {
        out.needCustomerReply = jsonBoolValue(object, QStringLiteral("need_customer_reply"), true);
        out.needDocSearch = jsonBoolValue(object, QStringLiteral("need_doc_search"), false);
        out.needImageSearch = false;
        if (out.workflow.isEmpty())
            out.workflow = QStringLiteral("human_review");
    } else if (out.intent == QLatin1String("no_reply_needed")) {
        out.needCustomerReply = false;
        out.needDocSearch = false;
        out.needImageSearch = false;
        if (out.workflow.isEmpty())
            out.workflow = QStringLiteral("no_reply");
    } else {
        out.intent = QStringLiteral("normal_question");
        out.needCustomerReply = jsonBoolValue(object, QStringLiteral("need_customer_reply"), true);
        out.needDocSearch = jsonBoolValue(object, QStringLiteral("need_doc_search"), fallback.needDocSearch);
        out.needImageSearch = false;
        out.needEmail = false;
        if (out.workflow.isEmpty())
            out.workflow = QStringLiteral("normal_text_reply");
    }

    return out;
}

ReplyActionPlan buildReplyActionPlan(const ReplyIntentDecision& intent,
                                     const ReplyRuntimeConfig& runtime)
{
    ReplyActionPlan plan;
    plan.workflow = intent.workflow.trimmed().isEmpty()
        ? QStringLiteral("normal_text_reply")
        : intent.workflow.trimmed();
    plan.generateReply = intent.needCustomerReply;
    plan.needDocSearch = intent.needDocSearch;
    plan.needImageSearch = intent.needImageSearch && intent.intent == QLatin1String("image_request");
    plan.needEmail = intent.needEmail || intent.intent == QLatin1String("link_request");
    plan.askForTemplate = plan.needEmail
        && intent.templateId.trimmed().isEmpty()
        && (intent.missingSlots.contains(QStringLiteral("template"))
            || intent.intent == QLatin1String("link_request"));
    plan.askForEmail = plan.needEmail && !plan.askForTemplate && intent.email.trimmed().isEmpty();
    plan.emailServiceRequired = plan.needEmail
        && !plan.askForTemplate
        && !intent.email.trimmed().isEmpty()
        && !intent.templateId.trimmed().isEmpty();
    plan.directExternalLinkBlocked = intent.intent == QLatin1String("link_request")
        || intent.riskFlags.contains(QStringLiteral("direct_external_link_blocked"));
    plan.requiresHumanReview = intent.intent == QLatin1String("human_handoff");
    plan.imageQuery = intent.imageQuery.trimmed();

    if (intent.intent == QLatin1String("image_request")) {
        plan.replyMode = QStringLiteral("text_with_image");
        plan.allowImageAttachments = runtime.strategy.allowAutoSendImages && plan.needImageSearch;
        plan.requiredActions << QStringLiteral("doc_search") << QStringLiteral("image_search");
        if (!plan.allowImageAttachments)
            plan.blockedActions << QStringLiteral("auto_send_image_disabled");
        plan.replyInstruction = QStringLiteral(
            "The customer is asking to see an image/photo. Generate a short text reply. "
            "If the system has an image attachment, you may naturally say it is being sent. "
            "If no image attachment is available, do not claim an image is attached.");
    } else if (intent.intent == QLatin1String("link_request")) {
        plan.replyMode = plan.askForTemplate
            ? QStringLiteral("collect_template")
            : (plan.askForEmail ? QStringLiteral("collect_email") : QStringLiteral("email_service_pending"));
        plan.needDocSearch = false;
        plan.needImageSearch = false;
        plan.allowImageAttachments = false;
        plan.requiredActions << (plan.askForTemplate
                                      ? QStringLiteral("collect_template")
                                      : (plan.askForEmail ? QStringLiteral("collect_email")
                                                          : QStringLiteral("email_service")));
        plan.blockedActions << QStringLiteral("send_external_link_in_chat");
        plan.replyInstruction = plan.askForTemplate
            ? QStringLiteral("The customer wants an external link/address but the exact store/material template is unclear. Do not output any URL. Ask which store/material they need.")
            : (plan.askForEmail
                   ? QStringLiteral("The customer wants an external link/address. Do not output any URL. Ask for an email address because the platform may block external links.")
                   : QStringLiteral("The customer provided an email for link delivery and a template is selected. Do not output any URL. Do not claim an email has already been sent unless a programmatic email action result says success. If no action result is provided, only acknowledge that the email address has been received and will be processed."));
    } else if (intent.intent == QLatin1String("human_handoff")) {
        plan.replyMode = QStringLiteral("human_review");
        plan.needDocSearch = false;
        plan.needImageSearch = false;
        plan.allowImageAttachments = false;
        plan.requiredActions << QStringLiteral("human_review");
        plan.replyInstruction = QStringLiteral("The request may require human review. Reply briefly and avoid making unsupported promises.");
    } else if (intent.intent == QLatin1String("no_reply_needed")) {
        plan.replyMode = QStringLiteral("no_reply");
        plan.generateReply = false;
        plan.needDocSearch = false;
        plan.needImageSearch = false;
        plan.allowImageAttachments = false;
        plan.replyInstruction = QStringLiteral("No customer-facing reply is required.");
    } else {
        plan.replyMode = QStringLiteral("text");
        plan.needImageSearch = false;
        plan.allowImageAttachments = false;
        plan.requiredActions << QStringLiteral("doc_search_optional");
        plan.replyInstruction = QStringLiteral("Generate a normal concise text reply.");
    }
    return plan;
}

QString formatIntentRouterHistory(const QList<AiConversationTurn>& recentTurns,
                                  const QString& latestText)
{
    QStringList lines;
    const int start = qMax(0, recentTurns.size() - kAggregateRecentHistoryLimit);
    for (int i = start; i < recentTurns.size(); ++i) {
        const AiConversationTurn& turn = recentTurns.at(i);
        QString text = firstTextPart(turn);
        if (text.trimmed().isEmpty())
            continue;
        text = boundedText(text, 360);
        lines << QStringLiteral("%1: %2").arg(turn.role, text);
    }
    if (!latestText.trimmed().isEmpty())
        lines << QStringLiteral("latest_user: %1").arg(boundedText(latestText, 600));
    return lines.join(QLatin1Char('\n')).trimmed();
}

QString intentRouterSystemPrompt()
{
    return QStringLiteral(
        "You are an ecommerce customer-service intent router. "
        "Do not generate the customer reply. Classify what the customer wants and return only one JSON object. "
        "Use recent chat only as context; prioritize the latest user message. "
        "Supported intents: normal_question, image_request, link_request, human_handoff, no_reply_needed, unknown. "
        "Rules: image_request means the customer explicitly wants a photo, product image, real-shot, appearance/detail/effect picture, or asks to see what it looks like. "
        "Do not classify normal product questions as image_request just because product knowledge may contain images. "
        "link_request means the customer asks for an external link, URL, download address, website, store view address, fan group, or asks to send it by email. "
        "Chinese phrases like 看图地址, 店铺看图地址, 粉丝群, 链接, 网址 are link_request, not image_request. "
        "For link_request, choose template_id from the email template catalog when possible. If the requested store/material is unclear, set missing_slots to include template and next_action=ask_template. "
        "If template_id is known but email is missing, set missing_slots to include email and next_action=ask_email. If both template_id and email are present, next_action=send_email. "
        "External links should not be sent directly in platform chat; use email collection/delivery workflow. "
        "Return JSON keys exactly: intent, confidence, need_customer_reply, need_doc_search, need_image_search, need_email, workflow, image_query, reply_goal, missing_slots, risk_flags, email, template_id, next_action, business_object, reason. "
        "Use booleans for need_* fields, confidence from 0 to 1, arrays for missing_slots and risk_flags. "
        "No Markdown, no explanation outside JSON.");
}

QString formatReplyActionPlanPromptBlock(const ReplyIntentDecision& intent,
                                         const ReplyActionPlan& actionPlan)
{
    QStringList lines;
    lines << QStringLiteral("[workflow_action_plan]");
    lines << QStringLiteral("intent=%1").arg(intent.intent);
    lines << QStringLiteral("intent_confidence=%1").arg(intent.confidence, 0, 'f', 2);
    lines << QStringLiteral("intent_source=%1").arg(intent.source);
    lines << QStringLiteral("workflow=%1").arg(actionPlan.workflow);
    lines << QStringLiteral("reply_mode=%1").arg(actionPlan.replyMode);
    if (!intent.nextAction.trimmed().isEmpty())
        lines << QStringLiteral("next_action=%1").arg(intent.nextAction.trimmed());
    lines << QStringLiteral("reply_goal=%1").arg(intent.replyGoal.trimmed().isEmpty()
                                                    ? actionPlan.replyInstruction
                                                    : intent.replyGoal.trimmed());
    lines << QStringLiteral("need_doc_search=%1").arg(actionPlan.needDocSearch ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("need_image_search=%1").arg(actionPlan.needImageSearch ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("has_image_attachment=%1").arg(actionPlan.hasImageAttachment ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("need_email=%1").arg(actionPlan.needEmail ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("ask_for_template=%1").arg(actionPlan.askForTemplate ? QStringLiteral("true") : QStringLiteral("false"));
    lines << QStringLiteral("ask_for_email=%1").arg(actionPlan.askForEmail ? QStringLiteral("true") : QStringLiteral("false"));
    if (!actionPlan.imageQuery.trimmed().isEmpty())
        lines << QStringLiteral("image_query=%1").arg(actionPlan.imageQuery.trimmed());
    if (!intent.templateId.trimmed().isEmpty())
        lines << QStringLiteral("email_template_id=%1").arg(intent.templateId.trimmed());
    if (!intent.businessObjectName.trimmed().isEmpty())
        lines << QStringLiteral("business_object=%1").arg(intent.businessObjectName.trimmed());
    if (!intent.email.trimmed().isEmpty())
        lines << QStringLiteral("customer_email=%1").arg(maskEmailAddressForPrompt(intent.email));
    if (!intent.missingSlots.isEmpty())
        lines << QStringLiteral("missing_slots=%1").arg(intent.missingSlots.join(QStringLiteral(",")));
    if (!intent.riskFlags.isEmpty())
        lines << QStringLiteral("risk_flags=%1").arg(intent.riskFlags.join(QStringLiteral(",")));
    if (!actionPlan.requiredActions.isEmpty())
        lines << QStringLiteral("required_actions=%1").arg(actionPlan.requiredActions.join(QStringLiteral(",")));
    if (!actionPlan.blockedActions.isEmpty())
        lines << QStringLiteral("blocked_actions=%1").arg(actionPlan.blockedActions.join(QStringLiteral(",")));
    lines << QStringLiteral("instruction=%1").arg(actionPlan.replyInstruction);
    lines << QStringLiteral("Hard rules: never output external URLs or links in chat. Do not claim an image is attached unless has_image_attachment=true. Do not claim an email has been sent unless an email service execution result is provided.");
    return lines.join(QLatin1Char('\n')).trimmed();
}

struct ImageQueryResolution {
    QString query;
    QString productFocus;
    QString source;
    bool shouldSearch = false;
};

ImageQueryResolution resolveImageSearchQuery(const QString& latestText,
                                             const QList<AiConversationTurn>& recentTurns)
{
    ImageQueryResolution resolution;
    const QString latest = latestText.trimmed().left(800);
    resolution.query = latest;
    resolution.source = QStringLiteral("latest_text");

    const QStringList latestModels = productModelsInText(latest);
    if (latestModels.size() == 1) {
        resolution.productFocus = latestModels.first();
        if (queryRequestsImageAttachment(latest)) {
            resolution.query = QStringLiteral("%1 实拍图 外观图 图片").arg(resolution.productFocus);
            resolution.shouldSearch = true;
        }
        return resolution;
    }
    if (latestModels.size() > 1)
        return resolution;

    if (!queryRequestsImageAttachment(latest))
        return resolution;

    const QString latestObjectFocus = imageObjectFocus(latest);
    if (!latestObjectFocus.isEmpty()) {
        resolution.productFocus = latestObjectFocus;
        resolution.query = QStringLiteral("%1 实拍图 外观图 图片").arg(latestObjectFocus);
        resolution.source = QStringLiteral("latest_object_focus");
        resolution.shouldSearch = true;
        return resolution;
    }

    for (int i = recentTurns.size() - 1; i >= 0; --i) {
        const QString content = firstTextPart(recentTurns.at(i));
        if (content.isEmpty() || content == latest)
            continue;
        const QStringList models = productModelsInText(content);
        if (models.size() == 1) {
            resolution.productFocus = models.first();
            resolution.query = QStringLiteral("%1 实拍图 外观图 图片").arg(resolution.productFocus);
            resolution.source = QStringLiteral("recent_single_model_context");
            resolution.shouldSearch = true;
            return resolution;
        }
    }

    resolution.shouldSearch = false;
    resolution.source = QStringLiteral("ambiguous_image_request_no_product_focus");
    return resolution;
}

QString firstLinkedImageName(const QList<KnowledgeSnippetContext>& snippets)
{
    static const QRegularExpression imageNameRegex(
        QStringLiteral(R"((?:图片名|图片名称)\s*[=：:]\s*([^\s\r\n，,；;]+))"));
    for (const KnowledgeSnippetContext& snippet : snippets) {
        const QRegularExpressionMatch match = imageNameRegex.match(snippet.snippet);
        if (match.hasMatch())
            return match.captured(1).trimmed();
    }
    return {};
}

QList<KnowledgeSnippetContext> parseKnowledgeResults(const QJsonObject& response)
{
    QList<KnowledgeSnippetContext> snippets;
    const QJsonArray results = response.value(QStringLiteral("results")).toArray();
    snippets.reserve(results.size());
    for (const QJsonValue& value : results) {
        const QJsonObject item = value.toObject();
        KnowledgeSnippetContext snippet;
        snippet.chunkId = item.value(QStringLiteral("chunk_id")).toString();
        snippet.sourceTitle = item.value(QStringLiteral("source_title")).toString();
        snippet.titlePath = item.value(QStringLiteral("title_path")).toString();
        snippet.snippet = item.value(QStringLiteral("snippet")).toString();
        snippet.matchType = item.value(QStringLiteral("match_type")).toString();
        snippet.score = item.value(QStringLiteral("score")).toDouble();
        const QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
        snippet.keywordScore = item.value(QStringLiteral("keyword_score")).toDouble(
            metadata.value(QStringLiteral("keyword_score")).toDouble());
        snippet.vectorScore = item.value(QStringLiteral("vector_score")).toDouble(
            metadata.value(QStringLiteral("vector_score")).toDouble());
        if (!snippet.snippet.trimmed().isEmpty())
            snippets.append(snippet);
    }
    return snippets;
}

QList<ReplyImageCandidate> parseImageResults(const QJsonObject& response)
{
    QList<ReplyImageCandidate> candidates;
    const QJsonArray results = response.value(QStringLiteral("results")).toArray();
    candidates.reserve(results.size());
    for (const QJsonValue& value : results) {
        const QJsonObject item = value.toObject();
        ReplyImageCandidate candidate;
        candidate.assetId = item.value(QStringLiteral("asset_id")).toString(
            item.value(QStringLiteral("source_id")).toString());
        candidate.sourceTitle = item.value(QStringLiteral("source_title")).toString();
        candidate.originalFilename = item.value(QStringLiteral("original_filename")).toString();
        candidate.filePath = item.value(QStringLiteral("file_path")).toString();
        candidate.assetType = item.value(QStringLiteral("asset_type")).toString();
        candidate.summary = item.value(QStringLiteral("summary")).toString();
        candidate.tags = jsonStringArrayLabel(item.value(QStringLiteral("tags")).toArray());
        candidate.scenarios = jsonStringArrayLabel(item.value(QStringLiteral("scenarios")).toArray());
        candidate.riskTags = jsonStringArrayLabel(item.value(QStringLiteral("risk_tags")).toArray());
        candidate.suggestedReply = item.value(QStringLiteral("suggested_reply")).toString();
        candidate.score = item.value(QStringLiteral("score")).toDouble();
        candidate.matchType = item.value(QStringLiteral("match_type")).toString();
        const QJsonObject recommendation = item.value(QStringLiteral("recommendation")).toObject();
        candidate.shouldAttach = recommendation.value(QStringLiteral("should_attach")).toBool(false);
        candidate.requiresHumanConfirm = recommendation.value(QStringLiteral("requires_human_confirm")).toBool(true);
        candidate.recommendationReason = recommendation.value(QStringLiteral("reason")).toString();
        candidate.riskNotice = recommendation.value(QStringLiteral("risk_notice")).toString();
        if (!candidate.filePath.trimmed().isEmpty())
            candidates.append(candidate);
    }
    return candidates;
}

OutgoingMessagePart imageCandidateToOutgoingPart(const ReplyImageCandidate& candidate)
{
    OutgoingMessagePart part;
    part.type = OutgoingPartType::Image;
    const QFileInfo info(candidate.filePath);
    part.localPath = info.absoluteFilePath();
    part.fileName = info.fileName();
    part.sizeBytes = info.exists() ? info.size() : 0;
    part.mimeType = info.exists() ? QMimeDatabase().mimeTypeForFile(info).name() : QStringLiteral("image/png");
    return part;
}

QVector<OutgoingMessagePart> imageAttachmentsFromCandidates(const QList<ReplyImageCandidate>& candidates,
                                                            const QString& query,
                                                            bool imageWorkflow)
{
    QVector<OutgoingMessagePart> parts;
    if (!imageWorkflow && !queryRequestsImageAttachment(query))
        return parts;
    QSet<QString> seenPaths;
    for (const ReplyImageCandidate& candidate : candidates) {
        if (!candidate.shouldAttach)
            continue;
        if (!riskTagsEmpty(candidate.riskTags))
            continue;
        const QFileInfo info(candidate.filePath.trimmed());
        if (!info.exists() || !info.isFile())
            continue;
        const QString absolutePath = info.absoluteFilePath();
        if (seenPaths.contains(absolutePath))
            continue;
        seenPaths.insert(absolutePath);
        parts.push_back(imageCandidateToOutgoingPart(candidate));
        if (parts.size() >= 1)
            break;
    }
    return parts;
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

ReplyIntentDecision AiChatAppService::classifyReplyIntent(const ReplyContextInput& input,
                                                          const QString& latestText,
                                                          const QString& latestImagePath,
                                                          const QList<AiConversationTurn>& recentTurns,
                                                          const QString& platform,
                                                          bool latestIsImageOnly) const
{
    const QList<EmailTemplateCatalogItem> emailTemplates = fetchEmailTemplateCatalogForRouter();
    ReplyIntentDecision fallback =
        heuristicReplyIntent(latestText, latestImagePath, recentTurns, latestIsImageOnly, emailTemplates);
    auto logDecision = [&](const ReplyIntentDecision& decision, const QString& stage) {
        qInfo() << "[AiChatAppService] reply intent routed"
                << "stage=" << stage
                << "source=" << decision.source
                << "intent=" << decision.intent
                << "workflow=" << decision.workflow
                << "nextAction=" << decision.nextAction
                << "templateId=" << decision.templateId
                << "missingSlots=" << decision.missingSlots.join(QStringLiteral(","))
                << "emailPresent=" << !decision.email.trimmed().isEmpty()
                << "confidence=" << decision.confidence
                << "templateCatalogCount=" << emailTemplates.size()
                << "error=" << decision.errorText.left(160);
    };

    const QString sessionModelKey = input.runtimeConfig.sessionModelKey.trimmed();
    if (sessionModelKey.isEmpty()) {
        fallback.source = QStringLiteral("heuristic_no_model");
        fallback.errorText = QStringLiteral("missing_session_model_key");
        logDecision(fallback, QStringLiteral("missing_model_key"));
        return fallback;
    }

    AiConfigLoadOptions loadOptions;
    loadOptions.allowAggregateFallback = true;
    loadOptions.allowGeneralFallback = true;
    const AiProviderConfig config =
        resolveProviderConfig(sessionModelKey, QString(), QString(), QString(), loadOptions);
    if (!config.isValidForChat()) {
        fallback.source = QStringLiteral("heuristic_invalid_model_config");
        fallback.errorText = QStringLiteral("invalid_model_config");
        logDecision(fallback, QStringLiteral("invalid_model_config"));
        return fallback;
    }

    AiRequest request;
    request.stream = false;
    request.systemPrompt = intentRouterSystemPrompt();
    request.extraRootFields.insert(QStringLiteral("temperature"), 0);
    request.extraRootFields.insert(QStringLiteral("max_tokens"), 360);
    request.extraRootFields.insert(QStringLiteral("response_format"),
                                   QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}});

    QStringList state;
    state << QStringLiteral("platform=%1").arg(platform.trimmed().isEmpty()
                                                   ? QStringLiteral("(unknown)")
                                                   : platform.trimmed());
    state << QStringLiteral("source=%1").arg(input.source == ReplyContextInput::Source::RobotSandbox
                                                 ? QStringLiteral("robot_sandbox")
                                                 : QStringLiteral("aggregate_conversation"));
    state << QStringLiteral("robot_id=%1").arg(input.runtimeConfig.boundRobotId.trimmed().isEmpty()
                                                   ? QStringLiteral("(empty)")
                                                   : input.runtimeConfig.boundRobotId.trimmed());
    state << QStringLiteral("robot_name=%1").arg(input.runtimeConfig.robotName.trimmed().isEmpty()
                                                     ? QStringLiteral("(empty)")
                                                     : input.runtimeConfig.robotName.trimmed());
    state << QStringLiteral("allow_auto_send_images=%1")
                 .arg(input.runtimeConfig.strategy.allowAutoSendImages ? QStringLiteral("true")
                                                                        : QStringLiteral("false"));
    state << QStringLiteral("latest_is_image_only=%1").arg(latestIsImageOnly ? QStringLiteral("true")
                                                                              : QStringLiteral("false"));

    const QString history = formatIntentRouterHistory(recentTurns, latestText);
    const QString templateCatalog = formatEmailTemplateCatalogForPrompt(emailTemplates);
    request.turns.append(makeAiTextTurn(
        QStringLiteral("user"),
        QStringLiteral("Current state:\n%1\n\nEmail template catalog (metadata only, no email body):\n%2\n\nRecent chat:\n%3\n\nReturn the routing JSON now.")
            .arg(state.join(QLatin1Char('\n')),
                 templateCatalog,
                 history.isEmpty() ? QStringLiteral("(empty)") : history)));

    QString assembleError;
    const QJsonArray messages = buildChatCompletionsMessages(request, &assembleError);
    if (!assembleError.trimmed().isEmpty()) {
        fallback.source = QStringLiteral("heuristic_request_assemble_failed");
        fallback.errorText = assembleError;
        logDecision(fallback, QStringLiteral("request_assemble_failed"));
        return fallback;
    }

    QString modelOutput;
    QString failureReason;
    bool completed = false;
    bool failed = false;
    bool timedOut = false;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    OpenAiCompatClient client(m_network);
    QObject::connect(&client, &OpenAiCompatClient::streamDelta, &loop, [&modelOutput](const QString& delta) {
        modelOutput += delta;
    });
    QObject::connect(&client, &OpenAiCompatClient::completed, &loop, [&]() {
        completed = true;
        loop.quit();
    });
    QObject::connect(&client, &OpenAiCompatClient::failed, &loop, [&](const QString& reason) {
        failed = true;
        failureReason = reason;
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        client.abortActive();
        loop.quit();
    });

    timer.start(8000);
    client.requestChatCompletion(OpenAiCompatClient::buildCompletionsUrl(config.baseUrl),
                                 config.apiKey,
                                 config.model,
                                 messages,
                                 false,
                                 request.extraRootFields);
    if (!completed && !failed && !timedOut)
        loop.exec();
    timer.stop();

    if (timedOut || failed || !completed) {
        fallback.source = QStringLiteral("heuristic_intent_router_failed");
        fallback.errorText = timedOut ? QStringLiteral("intent_router_timeout")
                                      : (failureReason.trimmed().isEmpty()
                                             ? QStringLiteral("intent_router_incomplete")
                                             : failureReason);
        logDecision(fallback, QStringLiteral("router_failed"));
        return fallback;
    }

    ReplyIntentDecision routed = parseReplyIntentDecision(modelOutput, fallback, latestText);
    if (routed.intent == QLatin1String("unknown") && routed.confidence < 0.60) {
        fallback.source = QStringLiteral("heuristic_low_confidence_router");
        fallback.rawJson = modelOutput.trimmed().left(4000);
        fallback.errorText = QStringLiteral("router_low_confidence_unknown");
        logDecision(fallback, QStringLiteral("router_low_confidence"));
        return fallback;
    }
    logDecision(routed, QStringLiteral("router_completed"));
    return routed;
}

AggregateAiBuiltRequest AiChatAppService::buildAggregateReplyRequest(
    int conversationId,
    const QString& sessionModelKey,
    const QList<KnowledgeSnippetContext>& knowledgeSnippets,
    const AggregateReplyStrategy& strategy,
    const ReplyIntentDecision& intent,
    const ReplyActionPlan& actionPlan) const
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

    const QString rawTextInbound = snap->content.trimmed();
    const QString textInbound = isImagePlaceholderText(rawTextInbound) ? QString() : rawTextInbound;
    const QString imgPath = snap->contentImagePath.trimmed();
    const bool pathRecorded = !imgPath.isEmpty();
    const bool fileOk = pathRecorded && QFileInfo(imgPath).isFile();
    const bool shouldUseImage = textInbound.isEmpty() && fileOk;
    if (textInbound.isEmpty() && pathRecorded && !fileOk) {
        built.failure = AggregateAiBuildFailure::MissingInboundImage;
        built.failureDetail = QStringLiteral("聊天区截图文件不可用");
        return built;
    }

    if (textInbound.isEmpty() && !shouldUseImage) {
        built.failure = AggregateAiBuildFailure::EmptyInbound;
        built.failureDetail = QStringLiteral("入站文本与图片均空");
        return built;
    }

    if (shouldUseImage && !built.config.capabilities.supportsVisionDataUrl) {
        built.failure = AggregateAiBuildFailure::VisionUnsupported;
        built.failureDetail = QStringLiteral("多模态需支持视觉的模型");
        return built;
    }

    built.request.systemPrompt = aggregateAiMvpSystemPrompt(strategy);
    const QString actionPlanBlock = formatReplyActionPlanPromptBlock(intent, actionPlan);
    if (!actionPlanBlock.isEmpty())
        built.request.systemPrompt += QStringLiteral("\n\n") + actionPlanBlock;
    const QString knowledgeBlock = formatKnowledgePromptBlock(knowledgeSnippets);
    if (!knowledgeBlock.isEmpty())
        built.request.systemPrompt += QStringLiteral("\n\n") + knowledgeBlock;
    built.request.turns = buildAggregateHistoryTurns(
        dao.listRecentCachedMessages(conversationId, kAggregateRecentHistoryLimit));

    const int maxMessages = strategy.allowAutoSendMultiMessages
        ? qBound(1, strategy.maxAutoSendMessages, 3)
        : 1;
    const QString generationInstruction = maxMessages > 1
        ? QStringLiteral("请生成可直接发送给客户的纯文本客服回复。默认只生成一条；只有需要补充不同信息时，才最多生成 %1 条，并用单独一行 ---MSG--- 分隔。不要生成内容基本相同的多条消息。不要使用 Markdown，不要带 emoji 表情。")
              .arg(maxMessages)
        : QStringLiteral("请生成一条亲切温和、可直接发送给客户的纯文本客服回复，不要使用 Markdown，不要带 emoji 表情。");

    AiConversationTurn userTurn;
    userTurn.role = QStringLiteral("user");
    if (shouldUseImage)
        userTurn.parts.append(makeAiImageFilePart(imgPath));
    userTurn.parts.append(makeAiTextPart(
        shouldUseImage
            ? QStringLiteral("请重点回应下方【客户最新入站】中的最后一条消息；上面的最近聊天记录和客户发来的图片只作为辅助参考。图片只用于理解最新入站，不要引用图片或历史中无关的旧商品、旧问题。回复客户时不要说“截图”“页面”“屏幕”“画面中”等机器感表达；需要引用视觉信息时，优先说“从图片看”“图片里看”。%1\n\n【客户最新入站】\n%2")
                  .arg(generationInstruction,
                       textInbound.isEmpty()
                           ? QStringLiteral("（无 OCR 文本，请根据图片理解客户意图）")
                           : textInbound)
            : QStringLiteral("请重点回应下方【客户最新入站】中的最后一条消息；上面的最近聊天记录只作为辅助参考。不要引用历史中无关的旧商品或旧问题；除非客户最新消息明确询问商品现货/库存，否则不要主动追问是否现货。%1\n\n【客户最新入站】\n%2")
                  .arg(generationInstruction, textInbound)));
    built.request.turns.append(userTurn);
    return built;
}

AggregateAiBuiltRequest AiChatAppService::buildRobotSandboxReplyRequest(
    const QString& sessionModelKey,
    const QList<AiConversationTurn>& recentTurns,
    const QString& latestUserText,
    const QList<KnowledgeSnippetContext>& knowledgeSnippets,
    const AggregateReplyStrategy& strategy,
    const ReplyIntentDecision& intent,
    const ReplyActionPlan& actionPlan) const
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

    const QString latest = boundedText(latestUserText);
    if (latest.isEmpty()) {
        built.failure = AggregateAiBuildFailure::EmptyInbound;
        built.failureDetail = QStringLiteral("测试问题为空");
        return built;
    }

    built.request.systemPrompt = aggregateAiMvpSystemPrompt(strategy);
    built.request.systemPrompt += QStringLiteral(
        "\n\n【机器人沙盒】\n"
        "这是后台测试环境。请把测试用户当作真实客户回复，但不要声称已经执行真实平台操作、修改订单或联系了人工。");
    const QString actionPlanBlock = formatReplyActionPlanPromptBlock(intent, actionPlan);
    if (!actionPlanBlock.isEmpty())
        built.request.systemPrompt += QStringLiteral("\n\n") + actionPlanBlock;
    const QString knowledgeBlock = formatKnowledgePromptBlock(knowledgeSnippets);
    if (!knowledgeBlock.isEmpty())
        built.request.systemPrompt += QStringLiteral("\n\n") + knowledgeBlock;

    const int start = qMax(0, recentTurns.size() - kAggregateRecentHistoryLimit);
    for (int i = start; i < recentTurns.size(); ++i)
        built.request.turns.append(recentTurns.at(i));

    const int maxMessages = strategy.allowAutoSendMultiMessages
        ? qBound(1, strategy.maxAutoSendMessages, 3)
        : 1;
    const QString generationInstruction = maxMessages > 1
        ? QStringLiteral("默认只回复一条；只有确有必要补充不同信息时，才最多回复 %1 条，并用单独一行 ---MSG--- 分隔。严禁生成内容基本相同的多条消息。").arg(maxMessages)
        : QStringLiteral("只生成一条可直接发送的回复，不要用分隔符模拟多条消息。");

    built.request.turns.append(makeAiTextTurn(
        QStringLiteral("user"),
        QStringLiteral(
            "请重点回应下方【客户最新入站】。上面的沙盒聊天历史只作为辅助参考，不要重复回答已经处理过的问题。"
            "%1只输出纯文本客服回复，不要使用 Markdown，不要带 emoji 表情。\n\n"
            "【客户最新入站】\n%2")
            .arg(generationInstruction, latest)));
    return built;
}

ReplyContextResult AiChatAppService::buildReplyContext(const ReplyContextInput& input) const
{
    ReplyContextResult result;
    const ReplyRuntimeConfig runtime = input.runtimeConfig;
    QString platform = runtime.platform.trimmed();
    QString latestText = input.latestUserText.trimmed();
    QString latestImagePath;
    QList<AiConversationTurn> recentTurns = input.recentTurns;

    if (input.source == ReplyContextInput::Source::AggregateConversation) {
        MessageDao messageDao;
        const auto snap = messageDao.latestCachedInboundSnapshot(input.conversationId);
        latestText = snap ? snap->content.trimmed() : QString();
        latestImagePath = snap ? snap->contentImagePath.trimmed() : QString();
        recentTurns = buildAggregateHistoryTurns(
            messageDao.listRecentCachedMessages(input.conversationId, kAggregateRecentHistoryLimit));
        if (platform.isEmpty()) {
            if (const auto conv = ConversationDao().findById(input.conversationId))
                platform = conv->platform;
        }
    }

    const bool latestIsImageOnly = isImageOnlyKnowledgeQuery(latestText, latestImagePath);
    result.intent = classifyReplyIntent(input,
                                        latestText,
                                        latestImagePath,
                                        recentTurns,
                                        platform,
                                        latestIsImageOnly);
    result.actionPlan = buildReplyActionPlan(result.intent, runtime);

    result.knowledgeTrace.latestInbound = latestText;
    result.knowledgeTrace.platform = platform;
    result.knowledgeTrace.scene = QStringLiteral("reply_draft");
    result.knowledgeTrace.boundBaseIds = runtime.knowledgeBaseIds;
    result.knowledgeTrace.bindingStatus = runtime.statusText;
    result.knowledgeTrace.bindingError = runtime.source;

    QElapsedTimer knowledgeTotalTimer;
    knowledgeTotalTimer.start();
    if (!result.actionPlan.needDocSearch) {
        result.knowledgeStatus = QStringLiteral("知识库未检索：意图路由 workflow=%1，本轮不需要文档检索。")
                                     .arg(result.actionPlan.workflow);
        result.knowledgeTrace.skipped = true;
        result.knowledgeTrace.failureStage = QStringLiteral("intent_no_doc_search");
        result.knowledgeTrace.statusText = result.knowledgeStatus;
        result.knowledgeTrace.totalMs = int(knowledgeTotalTimer.elapsed());
    } else if (latestIsImageOnly) {
        result.knowledgeStatus = QStringLiteral("知识库未检索：最新入站仅为图片消息，将由多模态模型直接理解图片。");
        result.knowledgeTrace.skipped = true;
        result.knowledgeTrace.failureStage = QStringLiteral("image_only_no_query");
        result.knowledgeTrace.statusText = result.knowledgeStatus;
        result.knowledgeTrace.totalMs = int(knowledgeTotalTimer.elapsed());
    } else if (latestText.isEmpty()) {
        result.knowledgeStatus = QStringLiteral("知识库未检索：最新入站消息没有可用文本。");
        result.knowledgeTrace.skipped = true;
        result.knowledgeTrace.failureStage = QStringLiteral("no_query");
        result.knowledgeTrace.statusText = result.knowledgeStatus;
        result.knowledgeTrace.totalMs = int(knowledgeTotalTimer.elapsed());
    } else if (isProductCardWithoutExplicitQuestion(latestText)) {
        result.knowledgeStatus = QStringLiteral("知识库未检索：最新入站仅为商品卡片/商品ID，将按新客户接待场景生成。");
        result.knowledgeTrace.skipped = true;
        result.knowledgeTrace.failureStage = QStringLiteral("product_card_no_query");
        result.knowledgeTrace.statusText = result.knowledgeStatus;
        result.knowledgeTrace.totalMs = int(knowledgeTotalTimer.elapsed());
    } else {
        const QString knowledgeQuery = buildKnowledgeSearchQueryFromTurns(latestText, recentTurns);
        result.knowledgeTrace.searchQuery = knowledgeQuery;
        if (knowledgeQuery.trimmed().isEmpty()) {
            result.knowledgeStatus = QStringLiteral("知识库未检索：客户最新消息为短确认，将结合最近聊天语境判断。");
            result.knowledgeTrace.skipped = true;
            result.knowledgeTrace.failureStage = QStringLiteral("short_ack_context_only");
            result.knowledgeTrace.statusText = result.knowledgeStatus;
            result.knowledgeTrace.totalMs = int(knowledgeTotalTimer.elapsed());
        } else if (runtime.knowledgeBaseIds.isEmpty()) {
            result.knowledgeStatus = runtime.usingRobot
                ? QStringLiteral("知识库未检索：当前机器人未绑定知识库。")
                : QStringLiteral("知识库未检索：当前平台未绑定启用机器人，将使用输入框模型生成。");
            result.knowledgeTrace.skipped = true;
            result.knowledgeTrace.failureStage = runtime.usingRobot
                ? QStringLiteral("robot_no_knowledge_bases")
                : runtime.statusText;
            result.knowledgeTrace.statusText = result.knowledgeStatus;
            result.knowledgeTrace.totalMs = int(knowledgeTotalTimer.elapsed());
        } else {
            QString error;
            QElapsedTimer healthTimer;
            healthTimer.start();
            if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
                result.knowledgeStatus = QStringLiteral("知识库不可用，已按原链路生成：%1").arg(error.left(120));
                result.knowledgeTrace.skipped = true;
                result.knowledgeTrace.failureStage = QStringLiteral("health_failed");
                result.knowledgeTrace.healthMs = int(healthTimer.elapsed());
                result.knowledgeTrace.statusText = result.knowledgeStatus;
                result.knowledgeTrace.errorText = error;
                result.knowledgeTrace.totalMs = int(knowledgeTotalTimer.elapsed());
            } else {
                result.knowledgeTrace.healthMs = int(healthTimer.elapsed());
                result.knowledgeTrace.searched = true;
                Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
                QElapsedTimer searchTimer;
                searchTimer.start();
                const QJsonObject response = Ipc::IpcService::instance().searchKnowledge(
                    knowledgeQuery,
                    platform,
                    QString(),
                    QStringLiteral("reply_draft"),
                    3,
                    15000,
                    &status,
                    &error,
                    runtime.knowledgeBaseIds);
                result.knowledgeTrace.searchHttpMs = int(searchTimer.elapsed());
                result.knowledgeTrace.responseStatus = Ipc::toString(status);
                result.knowledgeTrace.serverLatencyMs = response.value(QStringLiteral("metadata"))
                                                            .toObject()
                                                            .value(QStringLiteral("latency_ms"))
                                                            .toInt(-1);
                if (status != Ipc::ResponseStatus::Success
                    || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
                    const QString detail = response.value(QStringLiteral("detail")).toString(
                        response.value(QStringLiteral("error")).toString(error));
                    result.knowledgeStatus = QStringLiteral("知识库检索失败，已按原链路生成：%1").arg(detail.left(120));
                    result.knowledgeTrace.failureStage = status == Ipc::ResponseStatus::Timeout
                        ? QStringLiteral("http_timeout")
                        : QStringLiteral("server_error");
                    result.knowledgeTrace.statusText = result.knowledgeStatus;
                    result.knowledgeTrace.errorText = detail;
                } else {
                    result.knowledgeSnippets = parseKnowledgeResults(response);
                    result.knowledgeTrace.snippets = result.knowledgeSnippets;
                    result.knowledgeTrace.failureStage = result.knowledgeSnippets.isEmpty()
                        ? QStringLiteral("empty_results")
                        : QStringLiteral("success");
                    result.knowledgeStatus = result.knowledgeSnippets.isEmpty()
                        ? QStringLiteral("知识库未命中，已按聊天上下文生成。")
                        : QStringLiteral("已检索到 %1 条知识库片段，将优先按原文生成。")
                              .arg(result.knowledgeSnippets.size());
                    result.knowledgeTrace.statusText = result.knowledgeStatus;
                }
                result.knowledgeTrace.totalMs = int(knowledgeTotalTimer.elapsed());
            }
        }
    }

    result.linkedImageName = firstLinkedImageName(result.knowledgeSnippets);

    QElapsedTimer imageTotalTimer;
    imageTotalTimer.start();
    result.imageTrace.latestInbound = latestText;
    result.imageTrace.platform = platform;
    result.imageTrace.boundBaseIds = runtime.knowledgeBaseIds;
    result.imageTrace.bindingStatus = runtime.statusText;
    result.imageTrace.bindingError = runtime.source;

    if (!result.actionPlan.needImageSearch) {
        result.imageStatus = QStringLiteral("图片素材未检索：意图路由 intent=%1 workflow=%2，本轮不需要发图。")
                                 .arg(result.intent.intent, result.actionPlan.workflow);
        result.imageTrace.skipped = true;
        result.imageTrace.failureStage = QStringLiteral("intent_no_image_workflow");
        result.imageTrace.statusText = result.imageStatus;
        result.imageTrace.totalMs = int(imageTotalTimer.elapsed());
    } else if (latestIsImageOnly) {
        result.imageStatus = QStringLiteral("图片素材未检索：最新入站仅为客户图片，暂无文字 query。");
        result.imageTrace.skipped = true;
        result.imageTrace.failureStage = QStringLiteral("image_only_no_query");
        result.imageTrace.statusText = result.imageStatus;
        result.imageTrace.totalMs = int(imageTotalTimer.elapsed());
    } else if (latestText.isEmpty()) {
        result.imageStatus = QStringLiteral("图片素材未检索：最新入站消息没有可用文本。");
        result.imageTrace.skipped = true;
        result.imageTrace.failureStage = QStringLiteral("no_query");
        result.imageTrace.statusText = result.imageStatus;
        result.imageTrace.totalMs = int(imageTotalTimer.elapsed());
    } else if (isProductCardWithoutExplicitQuestion(latestText)) {
        result.imageStatus = QStringLiteral("图片素材未检索：最新入站仅为商品卡片/商品ID。");
        result.imageTrace.skipped = true;
        result.imageTrace.failureStage = QStringLiteral("product_card_no_query");
        result.imageTrace.statusText = result.imageStatus;
        result.imageTrace.totalMs = int(imageTotalTimer.elapsed());
    } else if (!runtime.strategy.allowAutoSendImages) {
        result.imageStatus = QStringLiteral("机器人未开启允许自动发图，意图已识别为图片请求但未检索图片素材。");
        result.imageTrace.skipped = true;
        result.imageTrace.failureStage = QStringLiteral("auto_send_images_disabled");
        result.imageTrace.statusText = result.imageStatus;
        result.imageTrace.totalMs = int(imageTotalTimer.elapsed());
    } else {
        ImageQueryResolution imageResolution = resolveImageSearchQuery(latestText, recentTurns);
        if (!result.actionPlan.imageQuery.trimmed().isEmpty()) {
            imageResolution.query = result.actionPlan.imageQuery.trimmed();
            imageResolution.source = QStringLiteral("intent_image_query");
            imageResolution.shouldSearch = true;
        }
        const bool canSearchLinkedImage = !result.linkedImageName.trimmed().isEmpty();
        if (!imageResolution.shouldSearch && !canSearchLinkedImage) {
            result.imageStatus = QStringLiteral("图片素材未检索：客户在追问图片，但最近上下文无法确定唯一商品型号。");
            result.imageTrace.skipped = true;
            result.imageTrace.failureStage = imageResolution.source;
            result.imageTrace.searchQuery = imageResolution.query;
            result.imageTrace.resolvedProductFocus = imageResolution.productFocus;
            result.imageTrace.resolutionSource = imageResolution.source;
            result.imageTrace.statusText = result.imageStatus;
            result.imageTrace.totalMs = int(imageTotalTimer.elapsed());
        } else if (runtime.knowledgeBaseIds.isEmpty()) {
            result.imageStatus = runtime.usingRobot
                ? QStringLiteral("图片素材未检索：当前机器人未绑定知识库。")
                : QStringLiteral("图片素材未检索：当前平台未绑定启用机器人。");
            result.imageTrace.skipped = true;
            result.imageTrace.failureStage = runtime.usingRobot
                ? QStringLiteral("robot_no_knowledge_bases")
                : runtime.statusText;
            result.imageTrace.statusText = result.imageStatus;
            result.imageTrace.totalMs = int(imageTotalTimer.elapsed());
        } else {
            QString searchQuery = imageResolution.query;
            QString productFocus = imageResolution.productFocus;
            QString resolutionSource = imageResolution.source;
            if (canSearchLinkedImage) {
                searchQuery = result.linkedImageName.trimmed();
                resolutionSource = QStringLiteral("knowledge_linked_image_name");
            }
            result.imageTrace.searchQuery = searchQuery;
            result.imageTrace.resolvedProductFocus = productFocus;
            result.imageTrace.resolutionSource = resolutionSource;

            QString error;
            if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
                result.imageStatus = QStringLiteral("图片素材检索跳过：Python 服务不可用。");
                result.imageTrace.skipped = true;
                result.imageTrace.failureStage = QStringLiteral("health_failed");
                result.imageTrace.statusText = result.imageStatus;
                result.imageTrace.errorText = error;
                result.imageTrace.totalMs = int(imageTotalTimer.elapsed());
            } else {
                result.imageTrace.searched = true;
                Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
                QElapsedTimer searchTimer;
                searchTimer.start();
                const QJsonObject response = Ipc::IpcService::instance().searchKnowledgeImages(
                    searchQuery,
                    platform,
                    QString(),
                    QStringLiteral("reply_draft"),
                    3,
                    15000,
                    &status,
                    &error,
                    runtime.knowledgeBaseIds);
                result.imageTrace.searchHttpMs = int(searchTimer.elapsed());
                result.imageTrace.responseStatus = Ipc::toString(status);
                result.imageTrace.serverLatencyMs = response.value(QStringLiteral("metadata"))
                                                        .toObject()
                                                        .value(QStringLiteral("latency_ms"))
                                                        .toInt(-1);
                if (status != Ipc::ResponseStatus::Success
                    || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
                    const QString detail = response.value(QStringLiteral("detail")).toString(
                        response.value(QStringLiteral("error")).toString(error));
                    result.imageStatus = QStringLiteral("图片素材检索失败。");
                    result.imageTrace.failureStage = status == Ipc::ResponseStatus::Timeout
                        ? QStringLiteral("http_timeout")
                        : QStringLiteral("server_error");
                    result.imageTrace.statusText = result.imageStatus;
                    result.imageTrace.errorText = detail;
                } else {
                    QList<ReplyImageCandidate> candidates = parseImageResults(response);
                    const int rawCount = candidates.size();
                    if (!productFocus.trimmed().isEmpty()) {
                        QList<ReplyImageCandidate> filtered;
                        filtered.reserve(candidates.size());
                        for (const ReplyImageCandidate& candidate : std::as_const(candidates)) {
                            if (candidateMatchesProductFocus(candidate, productFocus))
                                filtered.append(candidate);
                        }
                        candidates = filtered;
                    }
                    result.imageCandidates = candidates;
                    result.imageTrace.candidates = candidates;
                    result.imageTrace.rawCandidateCount = rawCount;
                    result.imageTrace.filteredCandidateCount = candidates.size();
                    result.imageTrace.failureStage = candidates.isEmpty()
                        ? QStringLiteral("empty_results")
                        : QStringLiteral("success");
                    result.imageStatus = candidates.isEmpty()
                        ? QStringLiteral("图片素材未命中。")
                        : QStringLiteral("已检索到 %1 个候选图片素材。").arg(candidates.size());
                    result.imageTrace.statusText = result.imageStatus;
                }
                result.imageTrace.totalMs = int(imageTotalTimer.elapsed());
            }
        }
    }

    result.imageAttachments = runtime.strategy.allowAutoSendImages
        ? imageAttachmentsFromCandidates(result.imageCandidates,
                                         result.imageTrace.searchQuery,
                                         result.actionPlan.needImageSearch)
        : QVector<OutgoingMessagePart>();
    result.actionPlan.hasImageAttachment = !result.imageAttachments.isEmpty();
    result.actionPlan.allowImageAttachments = runtime.strategy.allowAutoSendImages
        && result.actionPlan.needImageSearch
        && result.actionPlan.hasImageAttachment;

    if (!result.linkedImageName.trimmed().isEmpty()) {
        if (!result.imageStatus.trimmed().isEmpty())
            result.imageStatus += QLatin1Char(' ');
        result.imageStatus += QStringLiteral("知识片段关联图片名：%1。").arg(result.linkedImageName);
    }

    if (input.source == ReplyContextInput::Source::AggregateConversation) {
        result.built = buildAggregateReplyRequest(input.conversationId,
                                                  runtime.sessionModelKey,
                                                  result.knowledgeSnippets,
                                                  runtime.strategy,
                                                  result.intent,
                                                  result.actionPlan);
    } else {
        result.built = buildRobotSandboxReplyRequest(runtime.sessionModelKey,
                                                     recentTurns,
                                                     latestText,
                                                     result.knowledgeSnippets,
                                                     runtime.strategy,
                                                     result.intent,
                                                     result.actionPlan);
    }
    return result;
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
    const QVector<MessageRecord> messages =
        dao.listRecentCachedMessages(conversationId, kAggregateRecentHistoryLimit);
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
