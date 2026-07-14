#include "aggregatechatform.h"
#include "../core/conversationmanager.h"
#include "../core/messagerouter.h"
#include "../ipc/ipcservice.h"
#include "conversationlistmodel.h"
#include "messagelistmodel.h"
#include <QButtonGroup>
#include "../data/appdatauistatedao.h"
#include "../data/conversationdao.h"
#include "../data/airequesteventdao.h"
#include "../data/customerprofiledao.h"
#include "../data/messagedao.h"
#include "../data/messagesendeventdao.h"
#include "../data/qianniuconversationdao.h"
#include "../data/qqmessagedao.h"
#include "../data/wechatmessagedao.h"
#include "../services/app/aichatappservice.h"
#include "../services/app/conversationappservice.h"
#include "../services/app/pythonservicecontroller.h"
#include "../services/ai/aiprovidercatalog.h"
#include "../services/ai/aistreamingsession.h"
#include "../services/platforms/simplatformadapter.h"
#include "../services/ai/aiprovidercatalog.h"
#include "../utils/appsettings.h"
#include "../utils/applystyle.h"
#include "../utils/runtimemode.h"
#include "../utils/svgresourcepixmap.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAudioOutput>
#include <QEventLoop>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QSet>
#include <QStringConverter>
#include <QStringList>
#include <algorithm>
#include <utility>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHash>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QMetaObject>
#include <QAction>
#include <QAbstractButton>
#include <QMenu>
#include <QToolButton>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPixmapCache>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStringList>
#include <QStyle>
#include <QSvgRenderer>
#include <QTextDocument>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QShowEvent>
#include <QSlider>
#include <QSizePolicy>
#include <QColor>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMutex>
#include <QMutexLocker>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <functional>

namespace {

/** 右栏「展示模型」图标区，与布局中 model 块一致。 */
constexpr int kAggregateRightBarModelIconBoxSide = 58;
constexpr int kAggregateRightBarModelIconDrawSide = 56;
constexpr int kAggregateConversationPanelDefaultWidth = 248;
constexpr int kAggregateConversationPanelMinWidth = 208;
constexpr int kAggregateConversationPanelMaxWidth = 320;
constexpr int kAggregateConversationPanelHideBreakpoint = 740;
constexpr int kAggregateConversationPanelShowBreakpoint = 860;
constexpr int kAggregateConversationPanelCollapseSlop = 2;
constexpr int kAggregateRightPanelMinWidth = 208;
constexpr int kAggregateRightPanelPreferredMinWidth = 240;
constexpr int kAggregateRightPanelMaxWidth = 288;
constexpr int kAggregateChatWithRightMinWidth = 360;
constexpr int kAggregateRightPanelAutoHideBreakpoint = 700;
constexpr int kAggregateMetricSingleColumnBreakpoint = 252;

QString listenPlatformDisplayName(const QString& platform)
{
    const QString value = platform.trimmed().toLower();
    if (value == QLatin1String("wechat"))
        return QStringLiteral("微信");
    if (value == QLatin1String("qianniu"))
        return QStringLiteral("千牛");
    if (value == QLatin1String("pdd_web") || value == QLatin1String("pdd"))
        return QStringLiteral("拼多多");
    if (value == QLatin1String("douyin") || value == QLatin1String("doudian"))
        return QStringLiteral("抖店");
    if (value == QLatin1String("qq"))
        return QStringLiteral("QQ");
    return platform;
}

QString aggregatePlatformIdForButtonId(int id)
{
    switch (static_cast<AggregatePlatformFilter>(id)) {
    case AggregatePlatformFilter::Qianniu:
        return QStringLiteral("qianniu");
    case AggregatePlatformFilter::Pdd:
        return QStringLiteral("pdd_web");
    case AggregatePlatformFilter::Doudian:
        return QStringLiteral("douyin");
    case AggregatePlatformFilter::Wechat:
        return QStringLiteral("wechat");
    case AggregatePlatformFilter::QQ:
        return QStringLiteral("qq");
    case AggregatePlatformFilter::All:
    default:
        return QString();
    }
}

QStringList aggregateJsonStringArray(const QJsonArray& values)
{
    QStringList out;
    out.reserve(values.size());
    for (const QJsonValue& value : values) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    out.removeDuplicates();
    return out;
}

QString aggregateFirstLinkedImageName(const QList<KnowledgeSnippetContext>& snippets)
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

static constexpr char kAggregateRobotsSettingsKey[] = "ai/robots/list";
static constexpr char kAggregatePlatformRobotBindingsGroup[] = "ai/platformRobotBindings";

QJsonArray loadAggregateRobotConfigs()
{
    QSettings settings = AppSettings::create();
    const QString raw = settings.value(QString::fromLatin1(kAggregateRobotsSettingsKey)).toString();
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isArray())
        return {};
    return doc.array();
}

QString platformRobotBindingSettingsKey(const QString& platform)
{
    return QStringLiteral("%1/%2").arg(QString::fromLatin1(kAggregatePlatformRobotBindingsGroup),
                                       platform.trimmed().toLower());
}

QString boundRobotIdForAggregatePlatform(const QString& platform)
{
    const QString normalized = platform.trimmed().toLower();
    if (normalized.isEmpty())
        return {};
    QSettings settings = AppSettings::create();
    return settings.value(platformRobotBindingSettingsKey(normalized)).toString().trimmed();
}

void saveAggregatePlatformRobotBinding(const QString& platform, const QString& robotId)
{
    const QString normalized = platform.trimmed().toLower();
    if (normalized.isEmpty())
        return;
    QSettings settings = AppSettings::create();
    const QString key = platformRobotBindingSettingsKey(normalized);
    const QString trimmedRobotId = robotId.trimmed();
    if (trimmedRobotId.isEmpty())
        settings.remove(key);
    else
        settings.setValue(key, trimmedRobotId);
}

bool aggregateRobotEnabled(const QJsonObject& robot)
{
    const QJsonValue enabled = robot.value(QStringLiteral("enabled"));
    if (enabled.isBool())
        return enabled.toBool();
    return enabled.toInt(1) != 0;
}

QJsonObject aggregateRobotById(const QString& robotId)
{
    const QString wanted = robotId.trimmed();
    if (wanted.isEmpty())
        return {};
    const QJsonArray robots = loadAggregateRobotConfigs();
    for (const QJsonValue& value : robots) {
        const QJsonObject robot = value.toObject();
        if (robot.value(QStringLiteral("robot_id")).toString() == wanted)
            return robot;
    }
    return {};
}

QString aggregateRobotDisplayName(const QJsonObject& robot)
{
    const QString name = robot.value(QStringLiteral("robot_name")).toString().trimmed();
    const QString id = robot.value(QStringLiteral("robot_id")).toString().trimmed();
    if (!name.isEmpty() && !id.isEmpty())
        return QStringLiteral("%1 / %2").arg(name, id);
    if (!name.isEmpty())
        return name;
    return id.isEmpty() ? QStringLiteral("未命名机器人") : id;
}

QString aggregateRobotKnowledgeLabel(const QJsonObject& robot)
{
    QStringList names = aggregateJsonStringArray(robot.value(QStringLiteral("knowledge_base_names")).toArray());
    if (names.isEmpty())
        names = aggregateJsonStringArray(robot.value(QStringLiteral("knowledge_base_ids")).toArray());
    if (names.isEmpty())
        return QStringLiteral("未绑定知识库");
    if (names.size() <= 2)
        return names.join(QStringLiteral("、"));
    return QStringLiteral("%1 等 %2 个知识库").arg(names.first()).arg(names.size());
}

QString aggregateRobotModelLabel(const QJsonObject& robot)
{
    const QString modelKey = robot.value(QStringLiteral("model_config_id")).toString().trimmed();
    if (modelKey.isEmpty())
        return QStringLiteral("未配置模型");
    const QString label = aiPresetLabel(modelKey);
    return label.trimmed().isEmpty() ? modelKey : label;
}

QString aggregatePlatformRobotTooltip(const QString& baseTip, const QString& platform)
{
    const QString robotId = boundRobotIdForAggregatePlatform(platform);
    if (robotId.isEmpty())
        return QStringLiteral("%1\n右键：绑定机器人\n当前机器人：未绑定").arg(baseTip);
    const QJsonObject robot = aggregateRobotById(robotId);
    if (robot.isEmpty())
        return QStringLiteral("%1\n右键：绑定机器人\n当前机器人：已失效（%2）").arg(baseTip, robotId);
    return QStringLiteral("%1\n右键：绑定机器人\n当前机器人：%2%3")
        .arg(baseTip,
             robot.value(QStringLiteral("robot_name")).toString(robotId),
             aggregateRobotEnabled(robot) ? QString() : QStringLiteral("（停用）"));
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
        QStringLiteral("吗"),
        QStringLiteral("呢"),
        QStringLiteral("?"),
        QStringLiteral("？"),
        QStringLiteral("怎么"),
        QStringLiteral("多少"),
        QStringLiteral("几"),
        QStringLiteral("有没有"),
        QStringLiteral("能不能"),
        QStringLiteral("可以"),
        QStringLiteral("是不是"),
        QStringLiteral("发货"),
        QStringLiteral("退"),
        QStringLiteral("换"),
        QStringLiteral("保修"),
        QStringLiteral("售后"),
        QStringLiteral("价格"),
        QStringLiteral("优惠"),
        QStringLiteral("活动"),
        QStringLiteral("规格"),
        QStringLiteral("尺寸"),
        QStringLiteral("材质"),
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
        QStringLiteral("这个"),
        QStringLiteral("这款"),
        QStringLiteral("这个商品"),
        QStringLiteral("这款商品"),
        QStringLiteral("它"),
        QStringLiteral("那个"),
        QStringLiteral("上面"),
        QStringLiteral("刚才"),
    };
    for (const QString& signal : pronounSignals) {
        if (normalized.contains(signal))
            return true;
    }

    const QStringList shortQuestionSignals = {
        QStringLiteral("可以吗"),
        QStringLiteral("能吗"),
        QStringLiteral("能不能"),
        QStringLiteral("行吗"),
        QStringLiteral("怎么处理"),
        QStringLiteral("怎么弄"),
        QStringLiteral("多少钱"),
        QStringLiteral("有货吗"),
        QStringLiteral("能退吗"),
        QStringLiteral("能换吗"),
        QStringLiteral("保修吗"),
        QStringLiteral("发货吗"),
    };
    for (const QString& signal : shortQuestionSignals) {
        if (normalized.contains(signal))
            return true;
    }
    return false;
}

bool isImageOnlyKnowledgeQuery(const QString& latestText, const QString& imagePath)
{
    if (imagePath.trimmed().isEmpty())
        return false;

    const QString normalized = latestText.trimmed().toLower();
    return normalized.isEmpty()
        || normalized == QLatin1String("[image]")
        || normalized == QLatin1String("image")
        || normalized == QStringLiteral("[图片]")
        || normalized == QStringLiteral("图片");
}

QString knowledgeContextRoleLabel(const MessageRecord& message)
{
    if (message.direction == QLatin1String("out"))
        return QStringLiteral("我方");
    if (message.direction == QLatin1String("system"))
        return QStringLiteral("系统");
    return QStringLiteral("客户");
}

struct AggregateKnowledgeTrace {
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

struct AggregateImageCandidate {
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

struct AggregateImageTrace {
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
    QList<AggregateImageCandidate> candidates;
};

struct AggregateReplyRuntimeConfig {
    QString platform;
    QString source;
    QString statusText;
    QString sessionModelKey;
    QString boundRobotId;
    QString robotName;
    QString replyTone;
    QString commonAddressTerms;
    QStringList knowledgeBaseIds;
    QStringList knowledgeBaseNames;
    bool robotFound = false;
    bool robotEnabled = false;
    bool usingRobot = false;
    bool allowAutoSendImages = false;
    bool allowAutoSendMultiMessages = false;
    int maxAutoSendMessages = 1;
};

AggregateReplyStrategy aggregateReplyStrategyFromRuntimeConfig(const AggregateReplyRuntimeConfig& config)
{
    AggregateReplyStrategy strategy;
    strategy.replyTone = config.replyTone;
    strategy.commonAddressTerms = config.commonAddressTerms;
    strategy.allowAutoSendImages = config.allowAutoSendImages;
    strategy.allowAutoSendMultiMessages = config.allowAutoSendMultiMessages;
    strategy.maxAutoSendMessages = config.maxAutoSendMessages;
    return strategy;
}

AggregateReplyRuntimeConfig resolveAggregateReplyRuntimeConfig(int conversationId,
                                                               const QString& fallbackSessionModelKey)
{
    AggregateReplyRuntimeConfig config;
    config.source = QStringLiteral("manual_model");
    config.sessionModelKey = fallbackSessionModelKey.trimmed();
    config.statusText = QStringLiteral("platform_not_bound_to_robot");

    if (const auto conv = ConversationDao().findById(conversationId))
        config.platform = conv->platform.trimmed().toLower();

    const QString robotId = boundRobotIdForAggregatePlatform(config.platform);
    config.boundRobotId = robotId;
    if (robotId.isEmpty())
        return config;

    const QJsonObject robot = aggregateRobotById(robotId);
    if (robot.isEmpty()) {
        config.source = QStringLiteral("manual_model");
        config.statusText = QStringLiteral("robot_deleted");
        return config;
    }

    config.robotFound = true;
    config.robotEnabled = aggregateRobotEnabled(robot);
    config.robotName = robot.value(QStringLiteral("robot_name")).toString(robotId);
    if (!config.robotEnabled) {
        config.source = QStringLiteral("manual_model");
        config.statusText = QStringLiteral("robot_disabled");
        return config;
    }

    const QString modelKey = robot.value(QStringLiteral("model_config_id")).toString().trimmed();
    if (modelKey.isEmpty()) {
        config.source = QStringLiteral("manual_model");
        config.statusText = QStringLiteral("model_config_missing");
        return config;
    }

    config.source = QStringLiteral("robot");
    config.statusText = QStringLiteral("robot_bound");
    config.usingRobot = true;
    config.sessionModelKey = modelKey;
    config.knowledgeBaseIds = aggregateJsonStringArray(robot.value(QStringLiteral("knowledge_base_ids")).toArray());
    config.knowledgeBaseNames = aggregateJsonStringArray(robot.value(QStringLiteral("knowledge_base_names")).toArray());
    config.replyTone = robot.value(QStringLiteral("reply_tone"))
                           .toString(QStringLiteral("亲切温和、不失热情"))
                           .trimmed();
    config.commonAddressTerms = robot.value(QStringLiteral("common_address_terms"))
                                    .toString(QStringLiteral("亲、宝子"))
                                    .trimmed();
    config.allowAutoSendImages = robot.value(QStringLiteral("allow_auto_send_images")).toBool(false);
    config.allowAutoSendMultiMessages =
        robot.value(QStringLiteral("allow_auto_send_multi_messages")).toBool(false);
    config.maxAutoSendMessages = config.allowAutoSendMultiMessages
        ? qBound(1, robot.value(QStringLiteral("max_auto_send_messages")).toInt(2), 3)
        : 1;
    return config;
}

QStringList resolveKnowledgeBaseIdsForPlatform(const QString& platform,
                                               QString* statusTextOut = nullptr,
                                               QString* errorTextOut = nullptr,
                                               int* elapsedMsOut = nullptr)
{
    if (statusTextOut)
        statusTextOut->clear();
    if (errorTextOut)
        errorTextOut->clear();
    if (elapsedMsOut)
        *elapsedMsOut = 0;

    const QString normalizedPlatform = platform.trimmed().toLower();
    if (normalizedPlatform.isEmpty()) {
        if (statusTextOut)
            *statusTextOut = QStringLiteral("no_platform");
        return {};
    }

    QString error;
    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    QElapsedTimer timer;
    timer.start();
    const QJsonObject response = Ipc::IpcService::instance().fetchKnowledgePlatformBindings(
        normalizedPlatform,
        1500,
        &status,
        &error);
    if (elapsedMsOut)
        *elapsedMsOut = int(timer.elapsed());

    const QString responseStatus = response.value(QStringLiteral("status")).toString(QStringLiteral("success"));
    if (status != Ipc::ResponseStatus::Success || responseStatus == QLatin1String("error")) {
        const QString detail = response.value(QStringLiteral("detail")).toString(
            response.value(QStringLiteral("error")).toString(error));
        if (statusTextOut)
            *statusTextOut = status == Ipc::ResponseStatus::Timeout
                ? QStringLiteral("binding_http_timeout")
                : QStringLiteral("binding_error");
        if (errorTextOut)
            *errorTextOut = detail;
        return {};
    }

    const QStringList baseIds = aggregateJsonStringArray(response.value(QStringLiteral("base_ids")).toArray());
    if (statusTextOut)
        *statusTextOut = baseIds.isEmpty() ? QStringLiteral("unbound_all_enabled") : QStringLiteral("bound");
    return baseIds;
}

bool aggregateImageRiskIsEmpty(const QString& riskTags)
{
    const QString text = riskTags.trimmed();
    return text.isEmpty()
        || text == QLatin1String("(none)")
        || text.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("no_risk"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("无"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("无风险"), Qt::CaseInsensitive) == 0;
}

QString aggregateImageCandidateTitle(const AggregateImageCandidate& candidate)
{
    QString title = candidate.sourceTitle.trimmed();
    if (title.isEmpty())
        title = QFileInfo(candidate.filePath).fileName();
    if (title.isEmpty())
        title = QStringLiteral("建议附图");
    return title;
}

SuggestedImageAttachment aggregateSuggestedImageFromCandidate(const AggregateImageCandidate& candidate)
{
    SuggestedImageAttachment item;
    item.assetId = candidate.assetId;
    item.title = aggregateImageCandidateTitle(candidate);
    item.filePath = candidate.filePath;
    item.summary = candidate.summary;
    item.tags = candidate.tags;
    item.scenarios = candidate.scenarios;
    item.riskTags = candidate.riskTags;
    item.reason = candidate.recommendationReason;
    item.riskNotice = candidate.riskNotice;
    item.score = candidate.score;
    return item;
}

bool aggregateLooksLikeImageFilename(const QString& query)
{
    const QString text = query.trimmed().toLower();
    return text.endsWith(QStringLiteral(".png"))
        || text.endsWith(QStringLiteral(".jpg"))
        || text.endsWith(QStringLiteral(".jpeg"))
        || text.endsWith(QStringLiteral(".webp"))
        || text.endsWith(QStringLiteral(".gif"))
        || text.endsWith(QStringLiteral(".bmp"));
}

bool aggregateQueryRequestsImageAttachment(const QString& query)
{
    const QString text = query.trimmed().toLower();
    if (text.isEmpty())
        return false;
    if (aggregateLooksLikeImageFilename(text))
        return true;
    const QStringList keywords = {
        QStringLiteral("实拍"),
        QStringLiteral("外观图"),
        QStringLiteral("外观"),
        QStringLiteral("细节图"),
        QStringLiteral("细节"),
        QStringLiteral("商品图"),
        QStringLiteral("商品"),
        QStringLiteral("图片"),
        QStringLiteral("照片"),
        QStringLiteral("商品图"),
        QStringLiteral("外观图"),
        QStringLiteral("细节图"),
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

QString aggregateNormalizedImageText(QString text)
{
    text = text.toLower();
    QString out;
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber())
            out.append(ch);
    }
    return out;
}

QString aggregateImageObjectFocus(QString query)
{
    query = query.toLower().trimmed();
    const QStringList generic = {
        QStringLiteral("看下"),
        QStringLiteral("看看"),
        QStringLiteral("看一下"),
        QStringLiteral("发下"),
        QStringLiteral("发一下"),
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
        QStringLiteral("嘛"),
        QStringLiteral("么"),
        QStringLiteral("呢"),
        QStringLiteral("可以"),
        QStringLiteral("给我"),
        QStringLiteral("给"),
        QStringLiteral("亲"),
    };
    for (const QString& word : generic)
        query.replace(word, QString());
    query = aggregateNormalizedImageText(query);
    return query.size() >= 2 ? query : QString();
}

QStringList aggregateProductModelsInText(const QString& text)
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

bool aggregateCandidateMatchesProductFocus(const AggregateImageCandidate& candidate, const QString& productFocus)
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

bool aggregateCandidateMatchesImageObjectFocus(const AggregateImageCandidate& candidate, const QString& focusText)
{
    const QString focus = aggregateNormalizedImageText(focusText);
    if (focus.isEmpty())
        return true;
    const QString haystack = aggregateNormalizedImageText(
        QStringLiteral("%1\n%2\n%3\n%4\n%5\n%6\n%7")
            .arg(candidate.sourceTitle,
                 candidate.originalFilename,
                 candidate.filePath,
                 candidate.summary,
                 candidate.tags,
                 candidate.scenarios,
                 candidate.suggestedReply));
    return haystack.contains(focus);
}

struct AggregateImageQueryResolution {
    QString query;
    QString productFocus;
    QString source;
    bool shouldSearch = true;
};

AggregateImageQueryResolution resolveAggregateImageSearchQuery(const QString& latestText,
                                                               const QVector<MessageRecord>& recentMessages)
{
    AggregateImageQueryResolution resolution;
    const QString latest = latestText.trimmed().left(800);
    resolution.query = latest;
    resolution.source = QStringLiteral("latest_text");

    const QStringList latestModels = aggregateProductModelsInText(latest);
    if (latestModels.size() == 1) {
        resolution.productFocus = latestModels.first();
        if (aggregateQueryRequestsImageAttachment(latest))
            resolution.query = QStringLiteral("%1 实拍图 外观图 图片").arg(resolution.productFocus);
        return resolution;
    }
    if (latestModels.size() > 1)
        return resolution;

    if (!aggregateQueryRequestsImageAttachment(latest))
        return resolution;

    const QString latestObjectFocus = aggregateImageObjectFocus(latest);
    if (!latestObjectFocus.isEmpty()) {
        resolution.productFocus = latestObjectFocus;
        resolution.query = QStringLiteral("%1 实拍图 外观图 图片").arg(latestObjectFocus);
        resolution.source = QStringLiteral("latest_object_focus");
        return resolution;
    }

    for (int i = recentMessages.size() - 1; i >= 0; --i) {
        const MessageRecord& message = recentMessages.at(i);
        const QString content = message.content.trimmed();
        if (content.isEmpty() || content == latest)
            continue;
        const QStringList models = aggregateProductModelsInText(content);
        if (models.size() == 1) {
            resolution.productFocus = models.first();
            resolution.query = QStringLiteral("%1 实拍图 外观图 图片").arg(resolution.productFocus);
            resolution.source = QStringLiteral("recent_single_model_context");
            return resolution;
        }
    }

    resolution.shouldSearch = false;
    resolution.source = QStringLiteral("ambiguous_image_request_no_product_focus");
    return resolution;
}

OutgoingMessagePart aggregateImageCandidateToOutgoingPart(const AggregateImageCandidate& candidate)
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

QVector<OutgoingMessagePart> aggregateAutoReplyImagePartsFromCandidates(const QList<AggregateImageCandidate>& candidates,
                                                                        const QString& query)
{
    QVector<OutgoingMessagePart> parts;
    if (!aggregateQueryRequestsImageAttachment(query))
        return parts;
    QSet<QString> seenPaths;
    for (const AggregateImageCandidate& candidate : candidates) {
        if (!candidate.shouldAttach)
            continue;
        const QString path = candidate.filePath.trimmed();
        if (path.isEmpty())
            continue;
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile())
            continue;
        if (!aggregateImageRiskIsEmpty(candidate.riskTags))
            continue;
        const QString absolutePath = info.absoluteFilePath();
        if (seenPaths.contains(absolutePath))
            continue;
        seenPaths.insert(absolutePath);
        parts.push_back(aggregateImageCandidateToOutgoingPart(candidate));
        if (parts.size() >= 1)
            break;
    }
    return parts;
}

QString normalizedAutoReplyMessage(QString text)
{
    text = text.toLower().trimmed();
    QString normalized;
    normalized.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber())
            normalized.append(ch);
    }
    return normalized;
}

QSet<QString> autoReplyMessageBigrams(const QString& text)
{
    QSet<QString> grams;
    if (text.size() < 2) {
        if (!text.isEmpty())
            grams.insert(text);
        return grams;
    }
    for (int i = 0; i + 1 < text.size(); ++i)
        grams.insert(text.mid(i, 2));
    return grams;
}

bool autoReplyMessagesAreSimilar(const QString& left, const QString& right)
{
    const QString a = normalizedAutoReplyMessage(left);
    const QString b = normalizedAutoReplyMessage(right);
    if (a.isEmpty() || b.isEmpty())
        return false;
    if (a == b)
        return true;
    if (qMin(a.size(), b.size()) >= 12 && (a.contains(b) || b.contains(a)))
        return true;

    const QSet<QString> gramsA = autoReplyMessageBigrams(a);
    const QSet<QString> gramsB = autoReplyMessageBigrams(b);
    if (gramsA.isEmpty() || gramsB.isEmpty())
        return false;
    int intersection = 0;
    for (const QString& gram : gramsA) {
        if (gramsB.contains(gram))
            ++intersection;
    }
    const double dice = (2.0 * intersection) / double(gramsA.size() + gramsB.size());
    return dice >= 0.72;
}

bool aggregateReplyDeniesImageAvailability(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized.isEmpty())
        return false;
    const QStringList denySignals = {
        QStringLiteral("没有图片"),
        QStringLiteral("暂无图片"),
        QStringLiteral("没有实拍"),
        QStringLiteral("暂无实拍"),
        QStringLiteral("不能发图"),
        QStringLiteral("无法发图"),
        QStringLiteral("图片不可用"),
        QStringLiteral("没有对应图片"),
        QStringLiteral("no image"),
        QStringLiteral("not available"),
    };
    for (const QString& signal : denySignals) {
        if (normalized.contains(signal))
            return true;
    }
    return false;
}

QStringList aggregateAutoReplyTextMessages(const QString& generatedText,
                                           bool allowMultiple,
                                           int maxMessages,
                                           int* rawPartCountOut = nullptr)
{
    static const QRegularExpression separator(
        QStringLiteral(R"((?m)^\s*-{3,}\s*MSG\s*-{3,}\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList rawParts;
    if (allowMultiple && maxMessages > 1) {
        rawParts = generatedText.split(separator, Qt::SkipEmptyParts);
    } else {
        QString single = generatedText;
        single.replace(separator, QStringLiteral("\n"));
        rawParts.append(single);
    }
    if (rawPartCountOut)
        *rawPartCountOut = rawParts.size();

    QStringList messages;
    const int limit = allowMultiple ? qBound(1, maxMessages, 3) : 1;
    for (QString part : rawParts) {
        part = part.trimmed();
        if (part.isEmpty())
            continue;
        bool duplicate = false;
        for (const QString& existing : std::as_const(messages)) {
            if (autoReplyMessagesAreSimilar(existing, part)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        messages.append(part);
        if (messages.size() >= limit)
            break;
    }
    if (messages.isEmpty() && !generatedText.trimmed().isEmpty())
        messages.append(generatedText.trimmed());
    return messages;
}

QString aggregateJsonStringArrayLabel(const QJsonArray& values)
{
    QStringList out;
    out.reserve(values.size());
    for (const QJsonValue& value : values) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    return out.isEmpty() ? QStringLiteral("(none)") : out.join(QStringLiteral(", "));
}

bool isShortCustomerAcknowledgement(const QString& text)
{
    QString normalized = text.trimmed().toLower();
    normalized.remove(QRegularExpression(QStringLiteral("[\\s，。！？,.!?~～]+")));
    static const QSet<QString> acknowledgements = {
        QStringLiteral("好"),
        QStringLiteral("好的"),
        QStringLiteral("可以"),
        QStringLiteral("行"),
        QStringLiteral("嗯"),
        QStringLiteral("恩"),
        QStringLiteral("知道了"),
        QStringLiteral("明白了"),
        QStringLiteral("谢谢"),
        QStringLiteral("感谢"),
        QStringLiteral("ok"),
        QStringLiteral("okay"),
    };
    return acknowledgements.contains(normalized);
}

bool previousReplyExpectsContinuation(const QString& text)
{
    const QString normalized = text.trimmed();
    if (normalized.isEmpty())
        return false;
    const QStringList continuationSignals = {
        QStringLiteral("需要我"),
        QStringLiteral("要不要"),
        QStringLiteral("是否需要"),
        QStringLiteral("可以帮您"),
        QStringLiteral("可以给您"),
        QStringLiteral("帮您推荐"),
        QStringLiteral("给您推荐"),
        QStringLiteral("详细介绍"),
        QStringLiteral("补充说明"),
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

QString buildKnowledgeSearchQuery(const QString& latestText, const QVector<MessageRecord>& recentMessages)
{
    const QString latest = latestText.trimmed().left(800);
    if (isShortCustomerAcknowledgement(latest)) {
        for (int i = recentMessages.size() - 1; i >= 0; --i) {
            const MessageRecord& message = recentMessages.at(i);
            if (message.direction != QLatin1String("out"))
                continue;
            const QString previousReply = message.content.trimmed().left(500);
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
    for (int i = recentMessages.size() - 1; i >= 0 && contextLines.size() < 2; --i) {
        const MessageRecord& message = recentMessages.at(i);
        QString content = message.content.trimmed();
        if (content.isEmpty() || content == latestText.trimmed())
            continue;
        content = content.left(160);
        contextLines.prepend(QStringLiteral("%1：%2").arg(knowledgeContextRoleLabel(message), content));
    }
    if (contextLines.isEmpty())
        return latest;

    return QStringLiteral("客户最新咨询：%1\n最近上下文（仅用于补全指代）：%2")
        .arg(latest, contextLines.join(QStringLiteral(" / ")))
        .left(1000);
}

QList<KnowledgeSnippetContext> retrieveAggregateKnowledgeSnippets(int conversationId,
                                                                  QString* statusOut,
                                                                  AggregateKnowledgeTrace* traceOut = nullptr,
                                                                  const AggregateReplyRuntimeConfig& runtimeConfig = {})
{
    if (statusOut)
        statusOut->clear();
    if (conversationId <= 0)
        return {};

    QElapsedTimer totalTimer;
    totalTimer.start();
    MessageDao messageDao;
    const auto snap = messageDao.latestCachedInboundSnapshot(conversationId);
    const QString queryText = snap ? snap->content.trimmed() : QString();
    const QString imagePath = snap ? snap->contentImagePath.trimmed() : QString();
    if (traceOut) {
        *traceOut = AggregateKnowledgeTrace();
        traceOut->latestInbound = queryText;
        traceOut->shopId = QString();
        traceOut->scene = QStringLiteral("reply_draft");
    }
    if (isImageOnlyKnowledgeQuery(queryText, imagePath)) {
        if (statusOut)
            *statusOut = QStringLiteral("知识库未检索：最新入站仅为图片消息，将由多模态模型直接理解图片。");
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = QStringLiteral("image_only_no_query");
            traceOut->statusText = statusOut ? *statusOut : QString();
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }
    if (queryText.isEmpty()) {
        if (statusOut)
            *statusOut = QStringLiteral("知识库未检索：最新入站消息没有可用文本。");
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = QStringLiteral("no_query");
            traceOut->statusText = statusOut ? *statusOut : QString();
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }
    if (isProductCardWithoutExplicitQuestion(queryText)) {
        if (statusOut)
            *statusOut = QStringLiteral("知识库未检索：最新入站仅为商品卡片/商品ID，将按新客户接待场景生成。");
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = QStringLiteral("product_card_no_query");
            traceOut->statusText = statusOut ? *statusOut : QString();
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }
    const QVector<MessageRecord> recentMessages = messageDao.listRecentCachedMessages(conversationId, 10);
    const QString searchQuery = buildKnowledgeSearchQuery(queryText, recentMessages);
    if (traceOut)
        traceOut->searchQuery = searchQuery;
    if (searchQuery.trimmed().isEmpty()) {
        if (statusOut)
            *statusOut = QStringLiteral("知识库未检索：客户最新消息为短确认，将结合最近聊天语境判断是继续处理还是自然收口。");
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = QStringLiteral("short_ack_context_only");
            traceOut->statusText = statusOut ? *statusOut : QString();
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }

    QString platform;
    if (const auto conv = ConversationDao().findById(conversationId))
        platform = conv->platform;
    if (traceOut)
        traceOut->platform = platform;

    const QStringList scopedBaseIds = runtimeConfig.knowledgeBaseIds;
    if (traceOut) {
        traceOut->boundBaseIds = scopedBaseIds;
        traceOut->bindingStatus = runtimeConfig.statusText;
        traceOut->bindingError = runtimeConfig.source;
        traceOut->bindingHttpMs = 0;
    }
    if (scopedBaseIds.isEmpty()) {
        if (statusOut) {
            if (runtimeConfig.usingRobot)
                *statusOut = QStringLiteral("知识库未检索：当前机器人未绑定知识库。");
            else
                *statusOut = QStringLiteral("知识库未检索：当前平台未绑定启用机器人，将使用输入框模型生成。");
        }
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = runtimeConfig.usingRobot
                ? QStringLiteral("robot_no_knowledge_bases")
                : runtimeConfig.statusText;
            traceOut->statusText = statusOut ? *statusOut : QString();
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }

    QString error;
    QElapsedTimer healthTimer;
    healthTimer.start();
    if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
        if (statusOut)
            *statusOut = QStringLiteral("知识库不可用，已按原链路生成：%1").arg(error.left(120));
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = QStringLiteral("health_failed");
            traceOut->healthMs = int(healthTimer.elapsed());
            traceOut->statusText = statusOut ? *statusOut : QString();
            traceOut->errorText = error;
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }
    if (traceOut)
        traceOut->healthMs = int(healthTimer.elapsed());

    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    if (traceOut)
        traceOut->searched = true;
    QElapsedTimer searchTimer;
    searchTimer.start();
    const QJsonObject response = Ipc::IpcService::instance().searchKnowledge(
        searchQuery,
        platform,
        QString(),
        QStringLiteral("reply_draft"),
        3,
        15000,
        &status,
        &error,
        scopedBaseIds);
    if (traceOut) {
        traceOut->searchHttpMs = int(searchTimer.elapsed());
        traceOut->responseStatus = Ipc::toString(status);
        traceOut->serverLatencyMs = response.value(QStringLiteral("metadata"))
                                        .toObject()
                                        .value(QStringLiteral("latency_ms"))
                                        .toInt(-1);
    }
    if (status != Ipc::ResponseStatus::Success
        || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
        const QString detail = response.value(QStringLiteral("detail")).toString(
            response.value(QStringLiteral("error")).toString(error));
        if (statusOut)
            *statusOut = QStringLiteral("知识库检索失败，已按原链路生成：%1").arg(detail.left(120));
        if (traceOut) {
            traceOut->failureStage = status == Ipc::ResponseStatus::Timeout
                ? QStringLiteral("http_timeout")
                : QStringLiteral("server_error");
            traceOut->statusText = statusOut ? *statusOut : QString();
            traceOut->errorText = detail;
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }

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

    if (statusOut) {
        if (snippets.isEmpty())
            *statusOut = QStringLiteral("知识库未命中，已按聊天上下文生成。");
        else
            *statusOut = QStringLiteral("已检索到 %1 条知识库片段，将优先按原文生成。").arg(snippets.size());
    }
    if (traceOut) {
        traceOut->failureStage = snippets.isEmpty() ? QStringLiteral("empty_results") : QStringLiteral("success");
        traceOut->statusText = statusOut ? *statusOut : QString();
        traceOut->snippets = snippets;
        traceOut->totalMs = int(totalTimer.elapsed());
    }
    return snippets;
}

QList<AggregateImageCandidate> retrieveAggregateImageCandidates(int conversationId,
                                                                AggregateImageTrace* traceOut = nullptr,
                                                                const AggregateReplyRuntimeConfig& runtimeConfig = {},
                                                                const QList<KnowledgeSnippetContext>& knowledgeSnippets = {})
{
    if (conversationId <= 0)
        return {};

    QElapsedTimer totalTimer;
    totalTimer.start();
    MessageDao messageDao;
    const auto snap = messageDao.latestCachedInboundSnapshot(conversationId);
    const QString queryText = snap ? snap->content.trimmed() : QString();
    const QString imagePath = snap ? snap->contentImagePath.trimmed() : QString();
    if (traceOut) {
        *traceOut = AggregateImageTrace();
        traceOut->latestInbound = queryText;
    }

    if (isImageOnlyKnowledgeQuery(queryText, imagePath)) {
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = QStringLiteral("image_only_no_query");
            traceOut->statusText = QStringLiteral("图片素材未检索：最新入站仅为客户图片，暂无文字 query。");
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }
    if (queryText.isEmpty()) {
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = QStringLiteral("no_query");
            traceOut->statusText = QStringLiteral("图片素材未检索：最新入站消息没有可用文本。");
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }
    if (isProductCardWithoutExplicitQuestion(queryText)) {
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = QStringLiteral("product_card_no_query");
            traceOut->statusText = QStringLiteral("图片素材未检索：最新入站仅为商品卡片/商品ID。");
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }

    const QVector<MessageRecord> recentMessages = messageDao.listRecentCachedMessages(conversationId, 10);
    const AggregateImageQueryResolution queryResolution = resolveAggregateImageSearchQuery(queryText, recentMessages);
    const QString linkedImageName = aggregateFirstLinkedImageName(knowledgeSnippets);
    const bool canSearchLinkedImage = aggregateQueryRequestsImageAttachment(queryText)
        && !linkedImageName.trimmed().isEmpty();
    if (!queryResolution.shouldSearch && !canSearchLinkedImage) {
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = queryResolution.source;
            traceOut->searchQuery = queryResolution.query;
            traceOut->resolvedProductFocus = queryResolution.productFocus;
            traceOut->resolutionSource = queryResolution.source;
            traceOut->statusText = QStringLiteral("图片素材未检索：客户在追问图片，但最近上下文无法确定唯一商品型号。");
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }
    QString searchQuery = queryResolution.query;
    QString productFocus = queryResolution.productFocus;
    QString resolutionSource = queryResolution.source;
    if (canSearchLinkedImage) {
        searchQuery = linkedImageName.trimmed();
        resolutionSource = QStringLiteral("knowledge_linked_image_name");
    }
    if (traceOut) {
        traceOut->searchQuery = searchQuery;
        traceOut->resolvedProductFocus = productFocus;
        traceOut->resolutionSource = resolutionSource;
    }

    QString platform;
    if (const auto conv = ConversationDao().findById(conversationId))
        platform = conv->platform;
    if (traceOut)
        traceOut->platform = platform;

    const QStringList scopedBaseIds = runtimeConfig.knowledgeBaseIds;
    if (traceOut) {
        traceOut->boundBaseIds = scopedBaseIds;
        traceOut->bindingStatus = runtimeConfig.statusText;
        traceOut->bindingError = runtimeConfig.source;
        traceOut->bindingHttpMs = 0;
    }
    if (scopedBaseIds.isEmpty()) {
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = runtimeConfig.usingRobot
                ? QStringLiteral("robot_no_knowledge_bases")
                : runtimeConfig.statusText;
            traceOut->statusText = runtimeConfig.usingRobot
                ? QStringLiteral("图片素材未检索：当前机器人未绑定知识库。")
                : QStringLiteral("图片素材未检索：当前平台未绑定启用机器人。");
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }

    QString error;
    if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
        if (traceOut) {
            traceOut->skipped = true;
            traceOut->failureStage = QStringLiteral("health_failed");
            traceOut->statusText = QStringLiteral("图片素材检索跳过：Python 服务不可用。");
            traceOut->errorText = error;
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }

    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    if (traceOut)
        traceOut->searched = true;
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
        scopedBaseIds);
    if (traceOut) {
        traceOut->searchHttpMs = int(searchTimer.elapsed());
        traceOut->responseStatus = Ipc::toString(status);
        traceOut->serverLatencyMs = response.value(QStringLiteral("metadata"))
                                        .toObject()
                                        .value(QStringLiteral("latency_ms"))
                                        .toInt(-1);
    }
    if (status != Ipc::ResponseStatus::Success
        || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
        const QString detail = response.value(QStringLiteral("detail")).toString(
            response.value(QStringLiteral("error")).toString(error));
        if (traceOut) {
            traceOut->failureStage = status == Ipc::ResponseStatus::Timeout
                ? QStringLiteral("http_timeout")
                : QStringLiteral("server_error");
            traceOut->statusText = QStringLiteral("图片素材检索失败。");
            traceOut->errorText = detail;
            traceOut->totalMs = int(totalTimer.elapsed());
        }
        return {};
    }

    QList<AggregateImageCandidate> candidates;
    const QJsonArray results = response.value(QStringLiteral("results")).toArray();
    candidates.reserve(results.size());
    for (const QJsonValue& value : results) {
        const QJsonObject item = value.toObject();
        AggregateImageCandidate candidate;
        candidate.assetId = item.value(QStringLiteral("asset_id")).toString(
            item.value(QStringLiteral("source_id")).toString());
        candidate.sourceTitle = item.value(QStringLiteral("source_title")).toString();
        candidate.originalFilename = item.value(QStringLiteral("original_filename")).toString();
        candidate.filePath = item.value(QStringLiteral("file_path")).toString();
        candidate.assetType = item.value(QStringLiteral("asset_type")).toString();
        candidate.summary = item.value(QStringLiteral("summary")).toString();
        candidate.tags = aggregateJsonStringArrayLabel(item.value(QStringLiteral("tags")).toArray());
        candidate.scenarios = aggregateJsonStringArrayLabel(item.value(QStringLiteral("scenarios")).toArray());
        candidate.riskTags = aggregateJsonStringArrayLabel(item.value(QStringLiteral("risk_tags")).toArray());
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
    const int rawCandidateCount = candidates.size();
    if (!productFocus.trimmed().isEmpty()) {
        QList<AggregateImageCandidate> filtered;
        filtered.reserve(candidates.size());
        for (const AggregateImageCandidate& candidate : std::as_const(candidates)) {
            if (aggregateCandidateMatchesProductFocus(candidate, productFocus))
                filtered.append(candidate);
        }
        candidates = filtered;
    }
    if (traceOut) {
        traceOut->failureStage = candidates.isEmpty() ? QStringLiteral("empty_results") : QStringLiteral("success");
        traceOut->statusText = candidates.isEmpty()
            ? QStringLiteral("图片素材未命中。")
            : QStringLiteral("已检索到 %1 个候选图片素材。").arg(candidates.size());
        traceOut->rawCandidateCount = rawCandidateCount;
        traceOut->filteredCandidateCount = candidates.size();
        traceOut->candidates = candidates;
        traceOut->totalMs = int(totalTimer.elapsed());
    }
    return candidates;
}

QString aggregateTraceRoleLabel(const QString& roleOrDirection)
{
    const QString value = roleOrDirection.trimmed().toLower();
    if (value == QLatin1String("out") || value == QLatin1String("assistant"))
        return QStringLiteral("assistant/我方");
    if (value == QLatin1String("system"))
        return QStringLiteral("system/系统");
    return QStringLiteral("user/客户");
}

QString aggregateTracePartLabel(AiMessagePartKind kind)
{
    switch (kind) {
    case AiMessagePartKind::Text:
        return QStringLiteral("text");
    case AiMessagePartKind::ImageFile:
        return QStringLiteral("image_file");
    case AiMessagePartKind::LocalFile:
        return QStringLiteral("local_file");
    }
    return QStringLiteral("unknown");
}

QString aggregateListLabel(const QStringList& values)
{
    return values.isEmpty() ? QStringLiteral("(none)") : values.join(QStringLiteral(", "));
}

QString formatAggregateTraceHistory(int conversationId)
{
    const QVector<MessageRecord> messages = MessageDao().listRecentCachedMessages(conversationId, 10);
    if (messages.isEmpty())
        return QStringLiteral("(empty)");

    QStringList lines;
    for (int i = 0; i < messages.size(); ++i) {
        const MessageRecord& message = messages.at(i);
        QString content = message.content.trimmed();
        if (content.isEmpty() && !message.contentImagePath.trimmed().isEmpty())
            content = QStringLiteral("[image] %1").arg(message.contentImagePath.trimmed());
        if (content.isEmpty())
            content = QStringLiteral("(empty)");
        lines << QStringLiteral("%1. %2: %3")
                     .arg(i + 1)
                     .arg(aggregateTraceRoleLabel(message.direction), content);
    }
    return lines.join(QLatin1Char('\n'));
}

QString formatAggregateTraceKnowledge(const AggregateKnowledgeTrace& trace)
{
    QStringList lines;
    lines << QStringLiteral("searched: %1").arg(trace.searched ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("skipped: %1").arg(trace.skipped ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("failure_stage: %1").arg(trace.failureStage);
    lines << QStringLiteral("response_status: %1").arg(trace.responseStatus);
    lines << QStringLiteral("health_ms: %1").arg(trace.healthMs);
    lines << QStringLiteral("binding_http_ms: %1").arg(trace.bindingHttpMs);
    lines << QStringLiteral("search_http_ms: %1").arg(trace.searchHttpMs);
    lines << QStringLiteral("server_latency_ms: %1").arg(trace.serverLatencyMs);
    lines << QStringLiteral("total_ms: %1").arg(trace.totalMs);
    lines << QStringLiteral("platform: %1").arg(trace.platform);
    lines << QStringLiteral("shop_id: %1").arg(trace.shopId);
    lines << QStringLiteral("scene: %1").arg(trace.scene);
    lines << QStringLiteral("binding_status: %1").arg(trace.bindingStatus);
    lines << QStringLiteral("bound_base_ids: %1").arg(aggregateListLabel(trace.boundBaseIds));
    if (!trace.bindingError.trimmed().isEmpty())
        lines << QStringLiteral("binding_error: %1").arg(trace.bindingError.trimmed());
    lines << QStringLiteral("status: %1").arg(trace.statusText);
    if (!trace.errorText.trimmed().isEmpty())
        lines << QStringLiteral("error: %1").arg(trace.errorText.trimmed());
    lines << QStringLiteral("query:\n%1").arg(trace.searchQuery.trimmed().isEmpty()
                                                 ? QStringLiteral("(empty)")
                                                 : trace.searchQuery.trimmed());
    lines << QString();
    lines << QStringLiteral("results: %1").arg(trace.snippets.size());
    for (int i = 0; i < trace.snippets.size(); ++i) {
        const KnowledgeSnippetContext& item = trace.snippets.at(i);
        lines << QStringLiteral("--- result %1 ---").arg(i + 1);
        lines << QStringLiteral("chunk_id: %1").arg(item.chunkId);
        lines << QStringLiteral("source_title: %1").arg(item.sourceTitle);
        lines << QStringLiteral("title_path: %1").arg(item.titlePath);
        lines << QStringLiteral("score: %1").arg(item.score, 0, 'f', 4);
        lines << QStringLiteral("match_type: %1").arg(item.matchType);
        lines << QStringLiteral("keyword_score: %1").arg(item.keywordScore, 0, 'f', 4);
        lines << QStringLiteral("vector_score: %1").arg(item.vectorScore, 0, 'f', 4);
        lines << QStringLiteral("snippet:\n%1").arg(item.snippet.trimmed());
    }
    return lines.join(QLatin1Char('\n'));
}

QString formatAggregateTraceImages(const AggregateImageTrace& trace)
{
    QStringList lines;
    lines << QStringLiteral("searched: %1").arg(trace.searched ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("skipped: %1").arg(trace.skipped ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("failure_stage: %1").arg(trace.failureStage);
    lines << QStringLiteral("response_status: %1").arg(trace.responseStatus);
    lines << QStringLiteral("binding_http_ms: %1").arg(trace.bindingHttpMs);
    lines << QStringLiteral("search_http_ms: %1").arg(trace.searchHttpMs);
    lines << QStringLiteral("server_latency_ms: %1").arg(trace.serverLatencyMs);
    lines << QStringLiteral("total_ms: %1").arg(trace.totalMs);
    lines << QStringLiteral("platform: %1").arg(trace.platform);
    lines << QStringLiteral("binding_status: %1").arg(trace.bindingStatus);
    lines << QStringLiteral("bound_base_ids: %1").arg(aggregateListLabel(trace.boundBaseIds));
    if (!trace.bindingError.trimmed().isEmpty())
        lines << QStringLiteral("binding_error: %1").arg(trace.bindingError.trimmed());
    lines << QStringLiteral("status: %1").arg(trace.statusText);
    if (!trace.errorText.trimmed().isEmpty())
        lines << QStringLiteral("error: %1").arg(trace.errorText.trimmed());
    lines << QStringLiteral("latest_inbound: %1").arg(trace.latestInbound.trimmed().isEmpty()
                                                       ? QStringLiteral("(empty)")
                                                       : trace.latestInbound.trimmed());
    lines << QStringLiteral("resolved_product_focus: %1").arg(trace.resolvedProductFocus.trimmed().isEmpty()
                                                               ? QStringLiteral("(none)")
                                                               : trace.resolvedProductFocus.trimmed());
    lines << QStringLiteral("resolution_source: %1").arg(trace.resolutionSource.trimmed().isEmpty()
                                                          ? QStringLiteral("(none)")
                                                          : trace.resolutionSource.trimmed());
    lines << QStringLiteral("query:\n%1").arg(trace.searchQuery.trimmed().isEmpty()
                                                 ? QStringLiteral("(empty)")
                                                 : trace.searchQuery.trimmed());
    lines << QString();
    lines << QStringLiteral("raw_candidates: %1").arg(trace.rawCandidateCount);
    lines << QStringLiteral("filtered_candidates: %1").arg(trace.filteredCandidateCount);
    lines << QStringLiteral("candidates: %1").arg(trace.candidates.size());
    for (int i = 0; i < trace.candidates.size(); ++i) {
        const AggregateImageCandidate& item = trace.candidates.at(i);
        lines << QStringLiteral("--- candidate %1 ---").arg(i + 1);
        lines << QStringLiteral("asset_id: %1").arg(item.assetId);
        lines << QStringLiteral("source_title: %1").arg(item.sourceTitle);
        lines << QStringLiteral("original_filename: %1").arg(item.originalFilename);
        lines << QStringLiteral("file_path: %1").arg(item.filePath);
        lines << QStringLiteral("asset_type: %1").arg(item.assetType);
        lines << QStringLiteral("score: %1").arg(item.score, 0, 'f', 4);
        lines << QStringLiteral("match_type: %1").arg(item.matchType);
        lines << QStringLiteral("tags: %1").arg(item.tags);
        lines << QStringLiteral("scenarios: %1").arg(item.scenarios);
        lines << QStringLiteral("risk_tags: %1").arg(item.riskTags);
        lines << QStringLiteral("should_attach: %1").arg(item.shouldAttach ? QStringLiteral("yes") : QStringLiteral("no"));
        lines << QStringLiteral("requires_human_confirm: %1").arg(item.requiresHumanConfirm ? QStringLiteral("yes") : QStringLiteral("no"));
        lines << QStringLiteral("recommendation_reason: %1").arg(item.recommendationReason);
        lines << QStringLiteral("risk_notice: %1").arg(item.riskNotice);
        lines << QStringLiteral("suggested_reply: %1").arg(item.suggestedReply);
        lines << QStringLiteral("summary:\n%1").arg(item.summary.trimmed());
    }
    return lines.join(QLatin1Char('\n'));
}

QString formatAggregateTraceAiRequest(const AiRequest& request)
{
    QStringList lines;
    lines << QStringLiteral("system:\n%1").arg(request.systemPrompt.trimmed());
    lines << QString();
    lines << QStringLiteral("turn_count: %1").arg(request.turns.size());
    for (int i = 0; i < request.turns.size(); ++i) {
        const AiConversationTurn& turn = request.turns.at(i);
        lines << QStringLiteral("--- turn %1 role=%2 ---").arg(i + 1).arg(turn.role);
        for (int j = 0; j < turn.parts.size(); ++j) {
            const AiMessagePart& part = turn.parts.at(j);
            lines << QStringLiteral("[part %1 kind=%2]").arg(j + 1).arg(aggregateTracePartLabel(part.kind));
            if (part.kind == AiMessagePartKind::Text) {
                lines << part.text;
            } else {
                lines << QStringLiteral("file_path: %1").arg(part.filePath);
                if (!part.displayName.trimmed().isEmpty())
                    lines << QStringLiteral("display_name: %1").arg(part.displayName.trimmed());
            }
        }
    }
    lines << QString();
    const QJsonDocument extraDoc(request.extraRootFields);
    lines << QStringLiteral("extra_root_fields:\n%1")
                 .arg(QString::fromUtf8(extraDoc.toJson(QJsonDocument::Indented)).trimmed());
    lines << QStringLiteral("stream: %1").arg(request.stream ? QStringLiteral("true") : QStringLiteral("false"));
    return lines.join(QLatin1Char('\n'));
}

QString aggregateAiTraceLogPath()
{
    const QString logDir = QDir(QStringLiteral(PROJECT_ROOT_DIR))
                               .filePath(QStringLiteral("python/rpa/logs/pdd_web"));
    QDir().mkpath(logDir);
    return QDir(logDir).filePath(
        QStringLiteral("aggregate_ai_trace_%1.log")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"))));
}

QString aggregateBoolLabel(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString aggregateLogPreview(QString text, int maxLen = 160)
{
    text = text.simplified();
    if (text.size() <= maxLen)
        return text;
    return text.left(maxLen) + QStringLiteral("...");
}

QString aggregateEmailResponseDetail(const QJsonObject& response, const QString& transportError)
{
    return response.value(QStringLiteral("detail")).toString(
        response.value(QStringLiteral("error")).toString(transportError)).trimmed();
}

QString aggregateWorkflowEmailSuccessMessage()
{
    return QStringLiteral("亲，资料已发送到您的邮箱，请注意查收。");
}

QString aggregateWorkflowEmailFailureMessage()
{
    return QStringLiteral("亲，邮件发送暂时异常，我这边帮您进一步处理。");
}

struct AggregateWorkflowEmailResult
{
    int conversationId = 0;
    qint64 requestEventId = 0;
    QString traceId;
    QJsonObject response;
    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    QString error;
    int elapsedMs = 0;
};

QJsonObject aggregateBlockingJsonPostOnWorker(const QUrl& url,
                                              const QJsonObject& payload,
                                              int timeoutMs,
                                              Ipc::ResponseStatus* statusOut,
                                              QString* errorOut)
{
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    QJsonObject object;
    if (!reply->isFinished()) {
        reply->abort();
        if (statusOut)
            *statusOut = Ipc::ResponseStatus::Timeout;
        if (errorOut)
            *errorOut = QStringLiteral("request_timeout");
        reply->deleteLater();
        return object;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject())
        object = document.object();

    if (reply->error() != QNetworkReply::NoError) {
        if (statusOut)
            *statusOut = Ipc::ResponseStatus::Error;
        if (errorOut) {
            const QString detail = object.value(QStringLiteral("detail")).toString(
                object.value(QStringLiteral("error")).toString());
            *errorOut = detail.isEmpty() ? reply->errorString() : detail;
        }
        reply->deleteLater();
        return object;
    }

    if (!document.isObject()) {
        if (statusOut)
            *statusOut = Ipc::ResponseStatus::Error;
        if (errorOut)
            *errorOut = QStringLiteral("invalid_json_response");
        reply->deleteLater();
        return object;
    }

    if (statusOut)
        *statusOut = Ipc::ResponseStatus::Success;
    if (errorOut)
        errorOut->clear();
    reply->deleteLater();
    return object;
}

AggregateWorkflowEmailResult sendAggregateWorkflowEmailOnWorker(const QString& endpoint,
                                                                int conversationId,
                                                                qint64 requestEventId,
                                                                const QString& traceId,
                                                                const QString& recipient,
                                                                const QString& scene,
                                                                const QString& templateId)
{
    AggregateWorkflowEmailResult result;
    result.conversationId = conversationId;
    result.requestEventId = requestEventId;
    result.traceId = traceId;

    QElapsedTimer timer;
    timer.start();
    QJsonObject payload;
    payload.insert(QStringLiteral("to"), recipient.trimmed());
    payload.insert(QStringLiteral("scene"), scene.trimmed().isEmpty()
                                        ? QStringLiteral("link_request")
                                        : scene.trimmed());
    payload.insert(QStringLiteral("conversation_id"), conversationId);
    payload.insert(QStringLiteral("trace_id"), traceId);
    if (!templateId.trimmed().isEmpty())
        payload.insert(QStringLiteral("template_id"), templateId.trimmed());
    result.response = aggregateBlockingJsonPostOnWorker(
        QUrl(endpoint + QStringLiteral("/api/email/send")),
        payload,
        30000,
        &result.status,
        &result.error);
    result.elapsedMs = int(timer.elapsed());
    return result;
}

QStringList sortedAggregatePlatformSet(const QSet<QString>& values)
{
    QStringList list;
    list.reserve(values.size());
    for (const QString& value : values) {
        const QString normalized = value.trimmed().toLower();
        if (!normalized.isEmpty())
            list.append(normalized);
    }
    list.removeDuplicates();
    list.sort(Qt::CaseInsensitive);
    return list;
}

QStringList aggregateManagerListeningPlatforms()
{
    const QStringList knownPlatforms = {
        QStringLiteral("pdd_web"),
        QStringLiteral("qianniu"),
        QStringLiteral("wechat"),
        QStringLiteral("qq"),
    };
    QStringList listening;
    for (const QString& platform : knownPlatforms) {
        if (ConversationManager::instance().isPlatformListening(platform))
            listening.append(platform);
    }
    listening.sort(Qt::CaseInsensitive);
    return listening;
}

QString aggregateAutoReplyLogPath()
{
    const QString logDir = QDir(QStringLiteral(PROJECT_ROOT_DIR))
                               .filePath(QStringLiteral("python/rpa/logs/pdd_web"));
    QDir().mkpath(logDir);
    return QDir(logDir).filePath(
        QStringLiteral("aggregate_auto_reply_%1.log")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"))));
}

QString aggregateLogField(const QString& key, const QString& value)
{
    return QStringLiteral("%1: %2").arg(key, value.trimmed().isEmpty() ? QStringLiteral("(empty)") : value);
}

void appendAggregateAutoReplyLog(const QString& event, const QStringList& fields = {})
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    QFile file(aggregateAutoReplyLogPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[AggregateAutoReplyTrace] failed to open log" << file.fileName() << file.errorString();
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "\n-------------------- Aggregate Auto Reply --------------------\n";
    stream << aggregateLogField(QStringLiteral("time"),
                                QDateTime::currentDateTime().toString(Qt::ISODateWithMs)) << '\n';
    stream << aggregateLogField(QStringLiteral("event"), event) << '\n';
    for (const QString& field : fields) {
        if (!field.trimmed().isEmpty())
            stream << field << '\n';
    }
    stream << "--------------------------------------------------------------\n";
    stream.flush();
}

void appendAggregateAiTraceLog(const QString& text)
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    QFile file(aggregateAiTraceLogPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[AggregateAITrace] failed to open log" << file.fileName() << file.errorString();
        return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << text;
    if (!text.endsWith(QLatin1Char('\n')))
        stream << '\n';
    stream.flush();
}

void appendAggregateAiTraceStart(const QString& traceId,
                                 const QString& entry,
                                 int conversationId,
                                 const QString& sessionModelKey,
                                 const AiProviderConfig& config,
                                 const AiRequest& request,
                                 const ReplyIntentDecision& intent,
                                 const ReplyActionPlan& actionPlan,
                                 const AggregateKnowledgeTrace& knowledgeTrace,
                                 const AggregateImageTrace& imageTrace,
                                 const AggregateReplyRuntimeConfig& runtimeConfig)
{
    QString platform;
    QString platformConversationId;
    QString customerName;
    if (const auto conv = ConversationDao().findById(conversationId)) {
        platform = conv->platform;
        platformConversationId = conv->platformConversationId;
        customerName = conv->customerName;
    }

    QStringList lines;
    lines << QStringLiteral("\n==================== Aggregate AI Reply Trace ====================");
    lines << QStringLiteral("trace_id: %1").arg(traceId);
    lines << QStringLiteral("phase: start");
    lines << QStringLiteral("time: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    lines << QStringLiteral("entry: %1").arg(entry);
    lines << QStringLiteral("conversation_id: %1").arg(conversationId);
    lines << QStringLiteral("platform: %1").arg(platform);
    lines << QStringLiteral("platform_conversation_id: %1").arg(platformConversationId);
    lines << QStringLiteral("customer_name: %1").arg(customerName);
    lines << QStringLiteral("session_model_key: %1").arg(sessionModelKey);
    lines << QStringLiteral("model: %1").arg(config.model);
    lines << QStringLiteral("base_url: %1").arg(config.baseUrl);
    lines << QStringLiteral("api_key: [redacted]");
    lines << QString();
    lines << QStringLiteral("[robot runtime config]");
    lines << QStringLiteral("config_source: %1").arg(runtimeConfig.source);
    lines << QStringLiteral("config_status: %1").arg(runtimeConfig.statusText);
    lines << QStringLiteral("runtime_platform: %1").arg(runtimeConfig.platform);
    lines << QStringLiteral("bound_robot_id: %1").arg(runtimeConfig.boundRobotId.trimmed().isEmpty()
                                                         ? QStringLiteral("(empty)")
                                                         : runtimeConfig.boundRobotId);
    lines << QStringLiteral("robot_name: %1").arg(runtimeConfig.robotName.trimmed().isEmpty()
                                                     ? QStringLiteral("(empty)")
                                                     : runtimeConfig.robotName);
    lines << QStringLiteral("robot_found: %1").arg(aggregateBoolLabel(runtimeConfig.robotFound));
    lines << QStringLiteral("robot_enabled: %1").arg(aggregateBoolLabel(runtimeConfig.robotEnabled));
    lines << QStringLiteral("using_robot: %1").arg(aggregateBoolLabel(runtimeConfig.usingRobot));
    lines << QStringLiteral("knowledge_base_ids: %1").arg(runtimeConfig.knowledgeBaseIds.isEmpty()
                                                             ? QStringLiteral("(empty)")
                                                             : runtimeConfig.knowledgeBaseIds.join(QStringLiteral(",")));
    lines << QStringLiteral("knowledge_base_names: %1").arg(runtimeConfig.knowledgeBaseNames.isEmpty()
                                                               ? QStringLiteral("(empty)")
                                                               : runtimeConfig.knowledgeBaseNames.join(QStringLiteral("、")));
    lines << QStringLiteral("reply_tone: %1").arg(runtimeConfig.replyTone.trimmed().isEmpty()
                                                      ? QStringLiteral("(empty)")
                                                      : runtimeConfig.replyTone);
    lines << QStringLiteral("common_address_terms: %1").arg(runtimeConfig.commonAddressTerms.trimmed().isEmpty()
                                                               ? QStringLiteral("(empty)")
                                                               : runtimeConfig.commonAddressTerms);
    lines << QStringLiteral("allow_auto_send_images: %1").arg(aggregateBoolLabel(runtimeConfig.allowAutoSendImages));
    lines << QStringLiteral("allow_auto_send_multi_messages: %1")
                 .arg(aggregateBoolLabel(runtimeConfig.allowAutoSendMultiMessages));
    lines << QStringLiteral("max_auto_send_messages: %1").arg(runtimeConfig.maxAutoSendMessages);
    lines << QString();
    lines << QStringLiteral("[latest inbound]");
    lines << (knowledgeTrace.latestInbound.trimmed().isEmpty()
                  ? QStringLiteral("(empty)")
                  : knowledgeTrace.latestInbound.trimmed());
    lines << QString();
    lines << QStringLiteral("[recent chat history - last 10]");
    lines << formatAggregateTraceHistory(conversationId);
    lines << QString();
    lines << QStringLiteral("[intent router / action plan]");
    lines << QStringLiteral("intent: %1").arg(intent.intent);
    lines << QStringLiteral("workflow: %1").arg(intent.workflow);
    lines << QStringLiteral("next_action: %1").arg(intent.nextAction.trimmed().isEmpty()
                                                     ? QStringLiteral("(empty)")
                                                     : intent.nextAction.trimmed());
    lines << QStringLiteral("intent_source: %1").arg(intent.source);
    lines << QStringLiteral("confidence: %1").arg(intent.confidence, 0, 'f', 2);
    lines << QStringLiteral("model_routed: %1").arg(aggregateBoolLabel(intent.modelRouted));
    lines << QStringLiteral("reason: %1").arg(intent.reason.trimmed().isEmpty()
                                                ? QStringLiteral("(empty)")
                                                : intent.reason.trimmed());
    lines << QStringLiteral("need_customer_reply: %1").arg(aggregateBoolLabel(intent.needCustomerReply));
    lines << QStringLiteral("need_doc_search: %1").arg(aggregateBoolLabel(actionPlan.needDocSearch));
    lines << QStringLiteral("need_image_search: %1").arg(aggregateBoolLabel(actionPlan.needImageSearch));
    lines << QStringLiteral("image_query: %1").arg(actionPlan.imageQuery.trimmed().isEmpty()
                                                    ? QStringLiteral("(empty)")
                                                    : actionPlan.imageQuery.trimmed());
    lines << QStringLiteral("need_email: %1").arg(aggregateBoolLabel(actionPlan.needEmail));
    lines << QStringLiteral("ask_for_template: %1").arg(aggregateBoolLabel(actionPlan.askForTemplate));
    lines << QStringLiteral("ask_for_email: %1").arg(aggregateBoolLabel(actionPlan.askForEmail));
    lines << QStringLiteral("email_service_required: %1").arg(aggregateBoolLabel(actionPlan.emailServiceRequired));
    lines << QStringLiteral("email_template_id: %1").arg(intent.templateId.trimmed().isEmpty()
                                                            ? QStringLiteral("(empty)")
                                                            : intent.templateId.trimmed());
    lines << QStringLiteral("business_object: %1").arg(intent.businessObjectName.trimmed().isEmpty()
                                                          ? QStringLiteral("(empty)")
                                                          : intent.businessObjectName.trimmed());
    lines << QStringLiteral("direct_external_link_blocked: %1").arg(aggregateBoolLabel(actionPlan.directExternalLinkBlocked));
    lines << QStringLiteral("missing_slots: %1").arg(intent.missingSlots.isEmpty()
                                                       ? QStringLiteral("(empty)")
                                                       : intent.missingSlots.join(QStringLiteral(",")));
    lines << QStringLiteral("risk_flags: %1").arg(intent.riskFlags.isEmpty()
                                                    ? QStringLiteral("(empty)")
                                                    : intent.riskFlags.join(QStringLiteral(",")));
    if (!intent.errorText.trimmed().isEmpty())
        lines << QStringLiteral("router_error: %1").arg(intent.errorText.left(300));
    if (!intent.rawJson.trimmed().isEmpty()) {
        lines << QStringLiteral("[intent raw json]");
        lines << intent.rawJson.trimmed().left(2000);
    }
    lines << QString();
    lines << QStringLiteral("[knowledge retrieval]");
    lines << formatAggregateTraceKnowledge(knowledgeTrace);
    lines << QString();
    lines << QStringLiteral("[image asset retrieval]");
    lines << formatAggregateTraceImages(imageTrace);
    lines << QString();
    lines << QStringLiteral("[full request sent to model]");
    lines << formatAggregateTraceAiRequest(request);
    lines << QStringLiteral("==================== Awaiting Model Response ====================");
    appendAggregateAiTraceLog(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

struct AggregateReplyBuildContext {
    QString traceId;
    AggregateReplyRuntimeConfig runtimeConfig;
    QString knowledgeStatus;
    ReplyIntentDecision intent;
    ReplyActionPlan actionPlan;
    AggregateKnowledgeTrace knowledgeTrace;
    AggregateImageTrace imageTrace;
    QList<KnowledgeSnippetContext> knowledgeSnippets;
    QList<AggregateImageCandidate> imageCandidates;
    QVector<OutgoingMessagePart> imageAttachments;
    AggregateAiBuiltRequest built;
};

ReplyRuntimeConfig toSharedReplyRuntimeConfig(const AggregateReplyRuntimeConfig& config)
{
    ReplyRuntimeConfig runtime;
    runtime.platform = config.platform;
    runtime.source = config.source;
    runtime.statusText = config.statusText;
    runtime.sessionModelKey = config.sessionModelKey;
    runtime.boundRobotId = config.boundRobotId;
    runtime.robotName = config.robotName;
    runtime.knowledgeBaseIds = config.knowledgeBaseIds;
    runtime.knowledgeBaseNames = config.knowledgeBaseNames;
    runtime.usingRobot = config.usingRobot;
    runtime.robotFound = config.robotFound;
    runtime.robotEnabled = config.robotEnabled;
    runtime.strategy = aggregateReplyStrategyFromRuntimeConfig(config);
    return runtime;
}

AggregateKnowledgeTrace toAggregateKnowledgeTrace(const ReplyKnowledgeTrace& trace)
{
    AggregateKnowledgeTrace out;
    out.latestInbound = trace.latestInbound;
    out.platform = trace.platform;
    out.shopId = trace.shopId;
    out.scene = trace.scene;
    out.boundBaseIds = trace.boundBaseIds;
    out.bindingStatus = trace.bindingStatus;
    out.bindingError = trace.bindingError;
    out.searchQuery = trace.searchQuery;
    out.statusText = trace.statusText;
    out.errorText = trace.errorText;
    out.failureStage = trace.failureStage;
    out.responseStatus = trace.responseStatus;
    out.healthMs = trace.healthMs;
    out.bindingHttpMs = trace.bindingHttpMs;
    out.searchHttpMs = trace.searchHttpMs;
    out.totalMs = trace.totalMs;
    out.serverLatencyMs = trace.serverLatencyMs;
    out.searched = trace.searched;
    out.skipped = trace.skipped;
    out.snippets = trace.snippets;
    return out;
}

AggregateImageCandidate toAggregateImageCandidate(const ReplyImageCandidate& candidate)
{
    AggregateImageCandidate out;
    out.assetId = candidate.assetId;
    out.sourceTitle = candidate.sourceTitle;
    out.originalFilename = candidate.originalFilename;
    out.filePath = candidate.filePath;
    out.assetType = candidate.assetType;
    out.summary = candidate.summary;
    out.tags = candidate.tags;
    out.scenarios = candidate.scenarios;
    out.riskTags = candidate.riskTags;
    out.suggestedReply = candidate.suggestedReply;
    out.recommendationReason = candidate.recommendationReason;
    out.riskNotice = candidate.riskNotice;
    out.matchType = candidate.matchType;
    out.score = candidate.score;
    out.shouldAttach = candidate.shouldAttach;
    out.requiresHumanConfirm = candidate.requiresHumanConfirm;
    return out;
}

QList<AggregateImageCandidate> toAggregateImageCandidates(const QList<ReplyImageCandidate>& candidates)
{
    QList<AggregateImageCandidate> out;
    out.reserve(candidates.size());
    for (const ReplyImageCandidate& candidate : candidates)
        out.append(toAggregateImageCandidate(candidate));
    return out;
}

AggregateImageTrace toAggregateImageTrace(const ReplyImageTrace& trace)
{
    AggregateImageTrace out;
    out.latestInbound = trace.latestInbound;
    out.platform = trace.platform;
    out.boundBaseIds = trace.boundBaseIds;
    out.bindingStatus = trace.bindingStatus;
    out.bindingError = trace.bindingError;
    out.searchQuery = trace.searchQuery;
    out.resolvedProductFocus = trace.resolvedProductFocus;
    out.resolutionSource = trace.resolutionSource;
    out.statusText = trace.statusText;
    out.errorText = trace.errorText;
    out.failureStage = trace.failureStage;
    out.responseStatus = trace.responseStatus;
    out.bindingHttpMs = trace.bindingHttpMs;
    out.searchHttpMs = trace.searchHttpMs;
    out.totalMs = trace.totalMs;
    out.serverLatencyMs = trace.serverLatencyMs;
    out.rawCandidateCount = trace.rawCandidateCount;
    out.filteredCandidateCount = trace.filteredCandidateCount;
    out.searched = trace.searched;
    out.skipped = trace.skipped;
    out.candidates = toAggregateImageCandidates(trace.candidates);
    return out;
}

AggregateReplyBuildContext buildAggregateReplyContext(AiChatAppService* service,
                                                      int conversationId,
                                                      const QString& sessionModelKey,
                                                      const QString& traceEntry)
{
    AggregateReplyBuildContext context;
    context.traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!service)
        return context;

    context.runtimeConfig = resolveAggregateReplyRuntimeConfig(conversationId, sessionModelKey);
    ReplyContextInput input;
    input.source = ReplyContextInput::Source::AggregateConversation;
    input.conversationId = conversationId;
    input.runtimeConfig = toSharedReplyRuntimeConfig(context.runtimeConfig);
    const ReplyContextResult shared = service->buildReplyContext(input);
    context.knowledgeStatus = shared.knowledgeStatus;
    context.intent = shared.intent;
    context.actionPlan = shared.actionPlan;
    context.knowledgeTrace = toAggregateKnowledgeTrace(shared.knowledgeTrace);
    context.imageTrace = toAggregateImageTrace(shared.imageTrace);
    context.knowledgeSnippets = shared.knowledgeSnippets;
    context.imageCandidates = toAggregateImageCandidates(shared.imageCandidates);
    context.imageAttachments = shared.imageAttachments;
    context.built = shared.built;
    if (context.built.ok())
        context.built.request.extraRootFields.insert(QStringLiteral("max_tokens"), 512);
    appendAggregateAiTraceStart(context.traceId,
                                traceEntry,
                                conversationId,
                                context.runtimeConfig.sessionModelKey,
                                context.built.config,
                                context.built.request,
                                context.intent,
                                context.actionPlan,
                                context.knowledgeTrace,
                                context.imageTrace,
                                context.runtimeConfig);
    return context;
}

void appendAggregateAiTraceFinish(const QString& traceId,
                                  int conversationId,
                                  const QString& phase,
                                  const QString& modelOutput,
                                  int durationMs,
                                  int firstTokenMs,
                                  const QString& errorText = QString())
{
    QStringList lines;
    lines << QStringLiteral("\n-------------------- Aggregate AI Reply Result --------------------");
    lines << QStringLiteral("trace_id: %1").arg(traceId);
    lines << QStringLiteral("phase: %1").arg(phase);
    lines << QStringLiteral("time: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    lines << QStringLiteral("conversation_id: %1").arg(conversationId);
    lines << QStringLiteral("duration_ms: %1").arg(durationMs);
    lines << QStringLiteral("first_token_ms: %1").arg(firstTokenMs);
    if (!errorText.trimmed().isEmpty()) {
        lines << QStringLiteral("[error]");
        lines << errorText.trimmed();
    }
    lines << QStringLiteral("[model raw output]");
    lines << (modelOutput.trimmed().isEmpty() ? QStringLiteral("(empty)") : modelOutput.trimmed());
    lines << QStringLiteral("==================================================================");
    appendAggregateAiTraceLog(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

class ComposeTextEdit final : public QPlainTextEdit
{
public:
    explicit ComposeTextEdit(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
    {
    }

    std::function<bool(const QMimeData*)> mimeHandler;

protected:
    void insertFromMimeData(const QMimeData* source) override
    {
        if (mimeHandler && mimeHandler(source))
            return;
        QPlainTextEdit::insertFromMimeData(source);
    }
};

void applySolidBackground(QWidget* widget, const QColor& color)
{
    if (!widget)
        return;
    QPalette pal = widget->palette();
    pal.setColor(QPalette::Window, color);
    pal.setColor(QPalette::Base, color);
    widget->setPalette(pal);
    widget->setAutoFillBackground(true);
}

QString formatProcessingTime(const QDateTime& createdAt)
{
    return createdAt.isValid()
        ? createdAt.toString(QStringLiteral("HH:mm:ss"))
        : QStringLiteral("--:--:--");
}

QString formatSendEventLine(const MessageSendEventRecord& e)
{
    QString text;
    if (e.phase == QLatin1String("send_attempt")) {
        text = QStringLiteral("正在向客户发送回复");
    } else if (e.phase == QLatin1String("success")) {
        text = QStringLiteral("客户平台已确认发送成功");
    } else if (e.phase == QLatin1String("failed")
               || e.phase == QLatin1String("lock_timeout")) {
        text = QStringLiteral("回复发送失败，请检查客户平台");
    } else if (e.phase == QLatin1String("receipt_result")) {
        text = e.detail.contains(QStringLiteral("success"), Qt::CaseInsensitive)
            ? QStringLiteral("客户平台已确认发送成功")
            : QStringLiteral("正在等待客户平台确认发送结果");
    }
    if (text.isEmpty())
        return QString();
    return QStringLiteral("%1  %2").arg(formatProcessingTime(e.createdAt), text);
}

QString formatAiStageEventLine(const AiRequestStageEventRecord& e)
{
    QString text;
    const QString detail = e.detail.trimmed();
    if (e.stage == QLatin1String("manual_started"))
        text = QStringLiteral("开始生成回复草稿");
    else if (e.stage == QLatin1String("auto_started"))
        text = QStringLiteral("开始生成自动回复");
    else if (e.stage == QLatin1String("profile_started"))
        text = QStringLiteral("开始整理客户信息");
    else if (e.stage == QLatin1String("context_ready"))
        text = QStringLiteral("已整理当前会话的最近聊天记录");
    else if (e.stage == QLatin1String("request_sent"))
        text = detail.isEmpty() ? QStringLiteral("正在请求 AI 模型")
                                : QStringLiteral("正在请求 %1").arg(detail);
    else if (e.stage == QLatin1String("first_token"))
        text = detail.isEmpty() ? QStringLiteral("模型开始返回回复内容")
                                : QStringLiteral("模型开始返回内容，等待 %1").arg(detail);
    else if (e.stage == QLatin1String("completed"))
        text = detail.isEmpty() ? QStringLiteral("回复内容已生成")
                                : QStringLiteral("回复内容已生成，%1").arg(detail);
    else if (e.stage == QLatin1String("profile_completed"))
        text = detail.isEmpty() ? QStringLiteral("客户信息已整理完成")
                                : QStringLiteral("客户信息已整理完成，%1").arg(detail);
    else if (e.stage == QLatin1String("failed"))
        text = detail.isEmpty() ? QStringLiteral("未能生成回复")
                                : QStringLiteral("未能生成回复：%1").arg(detail);
    else if (e.stage == QLatin1String("canceled"))
        text = QStringLiteral("AI 生成已由用户停止");
    else if (e.stage == QLatin1String("send_submitted"))
        text = QStringLiteral("自动回复已提交发送");
    else if (e.stage == QLatin1String("profile_saved"))
        text = QStringLiteral("客户信息已更新");
    if (text.isEmpty())
        return QString();
    return QStringLiteral("%1  %2").arg(formatProcessingTime(e.createdAt), text);
}

MessageRecord messageRecordFromUnified(const Models::Message& msg)
{
    MessageRecord record;
    record.id = msg.id;
    record.conversationId = msg.conversationId;
    record.direction = Models::legacyDirectionFromMessageDirection(msg.direction);
    record.content = msg.content;
    record.sender = msg.direction == Models::MessageDirection::Outbound
        ? QStringLiteral("agent")
        : (msg.direction == Models::MessageDirection::System
               ? QStringLiteral("system")
               : QStringLiteral("customer"));
    record.senderName = msg.metadata.value(QStringLiteral("senderName")).toString();
    record.createdAt = msg.platformDisplayedAt.isValid() ? msg.platformDisplayedAt : msg.observedAt;
    record.platformMsgId = msg.platformMessageId;
    record.syncStatus = Models::legacySyncStatusFromMessageStatus(msg.status);
    record.errorReason = msg.metadata.value(QStringLiteral("errorReason")).toString();
    record.originalTimestamp = msg.metadata.value(QStringLiteral("originalTimestamp")).toString();
    record.contentImagePath = msg.evidenceRef;
    record.status = Models::toString(msg.status);
    record.sourceType = Models::toString(msg.sourceType);
    record.confidence = msg.confidence;
    record.verificationStatus = Models::toString(msg.verificationStatus);
    record.contentType = Models::toString(msg.contentType);
    record.observedAt = msg.observedAt;
    return record;
}

ConversationInfo conversationInfoFromUnified(const Models::Conversation& conv)
{
    ConversationInfo info;
    info.id = conv.id;
    info.platform = Models::toString(conv.platformType);
    info.platformConversationId = conv.platformConversationId;
    info.customerName = conv.title;
    info.lastMessage = conv.lastMessage;
    info.lastTime = conv.lastMessageAt;
    info.unreadCount = conv.unreadCount;
    info.status = Models::toString(conv.status);
    info.createdAt = conv.createdAt;
    info.accountId = conv.accountId;
    info.sourceType = Models::toString(conv.sourceType);
    info.confidence = conv.confidence;
    info.updatedAt = conv.updatedAt;
    return info;
}

bool isHistorySyncMessage(const Models::Message& msg)
{
    const QJsonObject payload = msg.metadata.value(QStringLiteral("payload")).toObject();
    const QJsonObject meta = payload.value(QStringLiteral("metadata")).toObject();
    return meta.value(QStringLiteral("history_sync")).toBool(false)
        || meta.value(QStringLiteral("suppress_auto_reply")).toBool(false)
        || meta.value(QStringLiteral("preserve_conversation_last_message")).toBool(false);
}

QString conversationPlatformIconPath(const QString& platform)
{
    const QString value = platform.trimmed().toLower();
    if (value == QLatin1String("qianniu"))
        return QStringLiteral(":/aggregate_reception_icons/qianniu_logo_selected_icon.svg");
    if (value == QLatin1String("pdd_web") || value == QLatin1String("pdd"))
        return QStringLiteral(":/aggregate_reception_icons/pdd_logo_selected_icon.svg");
    if (value == QLatin1String("douyin") || value == QLatin1String("doudian"))
        return QStringLiteral(":/aggregate_reception_icons/doudian_logo_selected_icon.svg");
    if (value == QLatin1String("wechat"))
        return QStringLiteral(":/aggregate_reception_icons/wechat_logo_selected_icon.svg");
    if (value == QLatin1String("qq"))
        return QStringLiteral(":/aggregate_reception_icons/qq_logo_icon.svg");
    return {};
}

QColor conversationPlatformAvatarBackground(const QString& platform)
{
    const QString value = platform.trimmed().toLower();
    if (value == QLatin1String("wechat"))
        return QColor(QStringLiteral("#DDF7E7"));
    if (value == QLatin1String("pdd_web") || value == QLatin1String("pdd"))
        return QColor(QStringLiteral("#FFE8E8"));
    if (value == QLatin1String("douyin") || value == QLatin1String("doudian"))
        return QColor(QStringLiteral("#FFE8EF"));
    if (value == QLatin1String("qq"))
        return QColor(QStringLiteral("#E5F0FF"));
    return QColor(QStringLiteral("#DFF2FC"));
}

/** 右栏性能指标卡：高度随列宽变化，与宽度一致，接近正方形。 */
class AggregateRightMetricCardFrame final : public QFrame
{
public:
    explicit AggregateRightMetricCardFrame(QWidget* parent = nullptr) : QFrame(parent)
    {
        setObjectName(QStringLiteral("aggregateRightBarMetricCard"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setAttribute(Qt::WA_StyledBackground, true);
        auto* sh = new QGraphicsDropShadowEffect(this);
        sh->setBlurRadius(18);
        sh->setOffset(0, 2);
        sh->setColor(QColor(15, 23, 42, 36));
        setGraphicsEffect(sh);
    }
protected:
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int w) const override
    {
        return qMax(1, w);
    }
    QSize minimumSizeHint() const override
    {
        if (const QLayout* lay = layout()) {
            const QSize s = lay->minimumSize();
            const int side = qMax(76, qMax(s.width(), s.height()));
            return { side, side };
        }
        return { 80, 80 };
    }
};

class ConversationItemDelegate final : public QStyledItemDelegate
{
public:
    explicit ConversationItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(option)
        Q_UNUSED(index)
        return { 240, 76 };
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const ConversationInfo conv = index.data(ConversationListModel::ConversationRole).value<ConversationInfo>();
        const QString lastDirection = index.data(ConversationListModel::LastDirectionRole).toString();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QRect r = option.rect.adjusted(4, 3, -4, -3);
        QColor fill = Qt::transparent;
        if (selected)
            fill = QColor(QStringLiteral("#DFF2FC"));
        else if (hovered)
            fill = QColor(QStringLiteral("#F5FBFE"));
        if (fill.alpha() > 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(r, 6, 6);
        }

        const int avatarSide = 44;
        const QRect avatarRect(r.left() + 8, r.top() + (r.height() - avatarSide) / 2, avatarSide, avatarSide);
        painter->setBrush(conversationPlatformAvatarBackground(conv.platform));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(avatarRect);
        painter->setBrush(QColor(QStringLiteral("#FFFFFF")));
        painter->drawEllipse(avatarRect.adjusted(4, 4, -4, -4));

        const QString platformIcon = conversationPlatformIconPath(conv.platform);
        const int iconSide = 24;
        const QRect iconRect(avatarRect.center().x() - iconSide / 2,
                             avatarRect.center().y() - iconSide / 2,
                             iconSide,
                             iconSide);
        const QPixmap platformPixmap = platformIcon.isEmpty()
                                           ? QPixmap()
                                           : QIcon(platformIcon).pixmap(iconSide, iconSide);
        if (!platformPixmap.isNull()) {
            painter->drawPixmap(iconRect, platformPixmap);
        } else {
            painter->setBrush(QColor(QStringLiteral("#20B8E8")));
            painter->drawEllipse(avatarRect.adjusted(4, 4, -4, -4));
            painter->setPen(Qt::white);
            QFont avatarFont(option.font);
            avatarFont.setBold(true);
            painter->setFont(avatarFont);
            const QString avatarText = conv.customerName.trimmed().isEmpty()
                                            ? QStringLiteral("?")
                                            : conv.customerName.left(1);
            painter->drawText(avatarRect, Qt::AlignCenter, avatarText);
        }

        const QRect textRect(avatarRect.right() + 8, r.top() + 8,
                             qMax(1, r.right() - avatarRect.right() - 14), r.height() - 16);
        QFont titleFont(option.font);
        titleFont.setBold(true);
        const QFontMetrics titleFm(titleFont);
        QString preview = conv.lastMessage;
        if (!lastDirection.isEmpty())
            preview = preview.isEmpty() ? lastDirection : preview;
        const bool hasPreview = !preview.trimmed().isEmpty();
        const int titleY = hasPreview
                               ? textRect.top()
                               : textRect.top() + qMax(0, (textRect.height() - titleFm.height()) / 2);
        QString timeStr;
        if (conv.lastTime.isValid()) {
            timeStr = conv.lastTime.date() == QDate::currentDate()
                          ? conv.lastTime.toString(QStringLiteral("HH:mm"))
                          : conv.lastTime.toString(QStringLiteral("MM/dd"));
        }
        QFont smallFont(option.font);
        constexpr int kTitleTimeGap = 8;
        constexpr int kTimeColumnWidth = 48;
        const int timeWidth = timeStr.isEmpty() ? 0 : kTimeColumnWidth;
        const QRect timeRect(textRect.right() - timeWidth + 1, titleY, timeWidth, titleFm.height());
        const int titleRight = timeWidth > 0 ? timeRect.left() - kTitleTimeGap : textRect.right();
        const int titleMaxWidth = qMax(1, titleRight - textRect.left() + 1);

        painter->setFont(titleFont);
        painter->setPen(QColor(QStringLiteral("#111827")));
        const QString title = titleFm.elidedText(conv.customerName, Qt::ElideRight, titleMaxWidth);
        painter->drawText(QRect(textRect.left(), titleY, titleMaxWidth, titleFm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, title);

        painter->setFont(smallFont);
        painter->setPen(QColor(QStringLiteral("#9CA3AF")));
        if (timeWidth > 0) {
            painter->drawText(timeRect,
                              Qt::AlignRight | Qt::AlignVCenter, timeStr);
        }

        QFont previewFont(option.font);
        if (previewFont.pointSizeF() > 0)
            previewFont.setPointSizeF(previewFont.pointSizeF() + 0.5);
        else if (previewFont.pixelSize() > 0)
            previewFont.setPixelSize(previewFont.pixelSize() + 1);
        const QFontMetrics bodyFm(previewFont);
        if (hasPreview) {
            painter->setFont(previewFont);
            painter->setPen(QColor(QStringLiteral("#4B5563")));
            const int previewWidth = qMax(1, textRect.width() - (conv.unreadCount > 0 ? 26 : 0));
            preview = bodyFm.elidedText(preview, Qt::ElideRight, previewWidth);
            painter->drawText(QRect(textRect.left(), titleY + titleFm.height() + 8,
                                    previewWidth, bodyFm.height() + 4),
                              Qt::AlignLeft | Qt::AlignTop, preview);
        }

        if (conv.unreadCount > 0) {
            const QString badge = conv.unreadCount > 99 ? QStringLiteral("99+") : QString::number(conv.unreadCount);
            const QSize badgeSize(qMax(18, bodyFm.horizontalAdvance(badge) + 10), 18);
            const int badgeY = hasPreview
                                   ? titleY + titleFm.height() + 7
                                   : textRect.top() + qMax(0, (textRect.height() - badgeSize.height()) / 2);
            const QRect badgeRect(textRect.right() - badgeSize.width(), badgeY,
                                  badgeSize.width(), badgeSize.height());
            painter->setBrush(QColor(QStringLiteral("#20B8E8")));
            painter->drawRoundedRect(badgeRect, 9, 9);
            painter->setPen(Qt::white);
            painter->drawText(badgeRect, Qt::AlignCenter, badge);
        }

        painter->restore();
    }
};

class MessageItemDelegate final : public QStyledItemDelegate
{
public:
    explicit MessageItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void setSelfProfile(const QString& displayName, const QPixmap& avatar)
    {
        m_selfDisplayName = displayName;
        m_selfAvatarPixmap = avatar;
    }

    void setCustomerAvatar(const QPixmap& avatar)
    {
        m_customerAvatarPixmap = avatar;
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool separator = index.data(MessageListModel::IsSeparatorRole).toBool();
        if (separator)
            return { qMax(320, option.rect.width()), 34 };

        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        const int avail = qMax(280, option.rect.width() > 0 ? option.rect.width() - 132 : 420);
        const int bubbleWidth = qMin(420, avail);
        const QString displayText = msg.content.trimmed().isEmpty() ? QStringLiteral(" ") : msg.content;
        QTextDocument doc;
        doc.setDefaultFont(option.font);
        doc.setTextWidth(qMax(1, bubbleWidth - 24));
        doc.setPlainText(displayText);
        const int textH = qMax(QFontMetrics(option.font).height(), qCeil(doc.size().height()));
        const int imageH = messageImageHeight(msg, bubbleWidth);
        const int metaH = messageMetaHeight(msg);
        int h = 2 + qMax(36, imageH > 0 ? imageH + textH + 32 : textH + 28) + metaH;
        if (msg.syncStatus == 12 && !msg.errorReason.isEmpty())
            h += 18;
        return { qMax(320, option.rect.width()), qMax(64, h) };
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const bool separator = index.data(MessageListModel::IsSeparatorRole).toBool();
        if (separator) {
            const QDate date = index.data(MessageListModel::SeparatorDateRole).toDate();
            const QRect r = option.rect.adjusted(16, 4, -16, -4);
            painter->setPen(QColor(QStringLiteral("#CBD5E1")));
            painter->drawLine(r.left(), r.center().y(), r.left() + r.width() / 2 - 28, r.center().y());
            painter->drawLine(r.left() + r.width() / 2 + 28, r.center().y(), r.right(), r.center().y());
            painter->setPen(QColor(QStringLiteral("#64748B")));
            const QString text = date == QDate::currentDate()
                                     ? QStringLiteral("今天")
                                     : date.toString(QStringLiteral("yyyy-MM-dd"));
            painter->drawText(r, Qt::AlignCenter, text);
            painter->restore();
            return;
        }

        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        const bool outgoing = msg.direction == QLatin1String("out");
        const QRect rowRect = option.rect.adjusted(16, 2, -16, -2);
        const int avatarSide = 36;
        const int gap = 8;
        const int bubbleMax = qMin(420, qMax(220, rowRect.width() - avatarSide - gap - 72));
        const QString displayText = msg.content.trimmed().isEmpty() ? QStringLiteral(" ") : msg.content;

        QTextDocument doc;
        doc.setDefaultFont(option.font);
        doc.setTextWidth(qMax(1, bubbleMax - 24));
        doc.setPlainText(displayText);
        const QSizeF docSize = doc.size();
        const int imageH = messageImageHeight(msg, bubbleMax);
        QFontMetrics fm(option.font);
        const QRect textBounds = fm.boundingRect(QRect(0, 0, bubbleMax - 24, 10000),
                                                 Qt::TextWordWrap,
                                                 displayText);
        const int naturalTextW = displayText.trimmed().isEmpty() ? 28 : qBound(28, textBounds.width() + 2, bubbleMax - 24);
        const int naturalImageW = imageH > 0 ? qMin(bubbleMax - 24, 360) : 0;
        const int minBubbleW = displayText.trimmed().isEmpty() && imageH == 0 ? 72 : 96;
        const int bubbleW = qMin(bubbleMax, qMax(minBubbleW, qMax(naturalTextW, naturalImageW) + 24));
        const int metaH = messageMetaHeight(msg);
        const int bubbleH = messageBubbleHeight(msg, bubbleW, option.font);
        const int totalBubbleH = bubbleH + metaH;
        const int avatarX = outgoing ? rowRect.right() - avatarSide : rowRect.left();
        const int bubbleX = outgoing ? avatarX - gap - bubbleW : avatarX + avatarSide + gap;
        const int topY = rowRect.top() + qMax(0, (rowRect.height() - totalBubbleH) / 2);

        const QRect avatarRect(avatarX, topY + metaH, avatarSide, avatarSide);
        const QPixmap& avatarPixmap = outgoing ? m_selfAvatarPixmap : m_customerAvatarPixmap;
        if (!avatarPixmap.isNull())
            painter->drawPixmap(avatarRect, avatarPixmap);

        if (outgoing) {
            const QRect metaRect(rowRect.left(), topY, bubbleX + bubbleW - rowRect.left(), metaH);
            if (metaH > 0) {
                const QString timeStr = msg.createdAt.isValid() ? msg.createdAt.toString(QStringLiteral("HH:mm")) : QString();
                const QString nick = m_selfDisplayName.trimmed().isEmpty() ? QStringLiteral("我") : m_selfDisplayName.trimmed();
                const int nickWidth = qMin(160, fm.horizontalAdvance(nick) + 4);
                const int timeWidth = timeStr.isEmpty() ? 0 : qMin(92, fm.horizontalAdvance(timeStr) + 4);
                if (!timeStr.isEmpty()) {
                    painter->setPen(QColor(QStringLiteral("#6B7280")));
                    painter->drawText(QRect(metaRect.right() - nickWidth - timeWidth - 8, metaRect.top(),
                                            timeWidth, metaRect.height()),
                                      Qt::AlignRight | Qt::AlignVCenter, timeStr);
                }
                painter->setPen(QColor(QStringLiteral("#374151")));
                painter->drawText(QRect(metaRect.right() - nickWidth, metaRect.top(), nickWidth, metaRect.height()),
                                  Qt::AlignRight | Qt::AlignVCenter, nick);
            }
        } else if (metaH > 0) {
            painter->setPen(QColor(QStringLiteral("#6B7280")));
            QString meta;
            if (!msg.senderName.isEmpty())
                meta = msg.senderName;
            if (msg.createdAt.isValid()) {
                const QString t = msg.createdAt.toString(QStringLiteral("HH:mm"));
                meta = meta.isEmpty() ? t : meta + QStringLiteral("  ") + t;
            } else if (!msg.originalTimestamp.isEmpty()) {
                meta = msg.senderName.isEmpty() ? msg.originalTimestamp : msg.senderName + QStringLiteral("  ") + msg.originalTimestamp;
            }
            painter->drawText(QRect(bubbleX, topY, bubbleW + 120, metaH),
                              Qt::AlignLeft | Qt::AlignVCenter, meta);
        }

        QRect bubbleRect(bubbleX, topY + metaH, bubbleW, bubbleH);
        QColor bubbleColor = outgoing ? QColor(QStringLiteral("#D9E9FF")) : QColor(QStringLiteral("#FFFFFF"));
        painter->setPen(QColor(QStringLiteral("#D1D5DB")));
        painter->setBrush(bubbleColor);
        painter->drawRoundedRect(bubbleRect, 12, 12);

        int contentY = bubbleRect.top() + 10;
        if (!msg.contentImagePath.isEmpty() && QFile::exists(msg.contentImagePath)) {
            QPixmap pm(msg.contentImagePath);
            if (!pm.isNull()) {
                pm = pm.scaledToWidth(qMin(bubbleW - 24, 360), Qt::SmoothTransformation);
                painter->drawPixmap(QRect(bubbleRect.left() + 12, contentY, pm.width(), pm.height()), pm);
                contentY += pm.height() + 8;
            }
        }

        QRect textRect(bubbleRect.left() + 12, contentY, bubbleW - 24, bubbleRect.bottom() - contentY - 20);
        painter->setPen(QColor(QStringLiteral("#111827")));
        doc.setTextWidth(textRect.width());
        painter->translate(textRect.topLeft());
        doc.drawContents(painter, QRectF(0, 0, textRect.width(), textRect.height()));
        painter->translate(-textRect.topLeft());

        QString statusText;
        if (outgoing) {
            if (msg.syncStatus == 10)
                statusText = QStringLiteral("待发送");
            else if (msg.syncStatus == 12)
                statusText = QStringLiteral("发送失败");
            else
                statusText = QStringLiteral("已发送");
            painter->setPen(QColor(QStringLiteral("#6B7280")));
            painter->drawText(QRect(bubbleRect.left() + 12, bubbleRect.bottom() - 16, bubbleW - 24, 14),
                              Qt::AlignLeft | Qt::AlignVCenter, statusText);
            if (msg.syncStatus == 12 && !msg.errorReason.isEmpty()) {
                painter->drawText(QRect(bubbleRect.left() + 12, bubbleRect.bottom() + 2, bubbleW - 24, 24),
                                  Qt::AlignLeft | Qt::AlignTop, msg.errorReason);
            }
        }

        painter->restore();
    }

private:
    static bool hasMessageImage(const MessageRecord& msg)
    {
        return !msg.contentImagePath.isEmpty() && QFile::exists(msg.contentImagePath);
    }

    static int messageMetaHeight(const MessageRecord& msg)
    {
        Q_UNUSED(msg)
        return 0;
    }

    static int messageImageHeight(const MessageRecord& msg, int bubbleWidth)
    {
        if (!hasMessageImage(msg))
            return 0;
        QPixmap pm(msg.contentImagePath);
        if (pm.isNull())
            return 0;
        const int maxW = qMin(360, qMax(1, bubbleWidth - 24));
        if (pm.width() <= maxW)
            return pm.height();
        return qMax(1, qRound(pm.height() * (qreal(maxW) / qreal(pm.width()))));
    }

    static int messageBubbleHeight(const MessageRecord& msg, int bubbleWidth, const QFont& font)
    {
        const QString displayText = msg.content.trimmed().isEmpty() ? QStringLiteral(" ") : msg.content;
        QTextDocument doc;
        doc.setDefaultFont(font);
        doc.setTextWidth(qMax(1, bubbleWidth - 24));
        doc.setPlainText(displayText);
        const int textH = qMax(QFontMetrics(font).height(), qCeil(doc.size().height()));
        int h = 8 + textH + 10;
        const int imageH = messageImageHeight(msg, bubbleWidth);
        if (imageH > 0)
            h += imageH + 6;
        h += 12;
        return qMax(36, h);
    }

    QString m_selfDisplayName;
    QPixmap m_selfAvatarPixmap;
    QPixmap m_customerAvatarPixmap;
};

class ModernMessageItemDelegate final : public QStyledItemDelegate
{
public:
    explicit ModernMessageItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
        if (QPixmapCache::cacheLimit() < 32768)
            QPixmapCache::setCacheLimit(32768);
    }

    void setSelfProfile(const QString& displayName, const QPixmap& avatar)
    {
        m_selfDisplayName = displayName;
        m_selfAvatarPixmap = avatar;
    }

    void setCustomerAvatar(const QPixmap& avatar)
    {
        m_customerAvatarPixmap = avatar;
    }

    void clearSizeHintCache()
    {
        m_sizeHintCache.clear();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (index.data(MessageListModel::IsSeparatorRole).toBool())
            return { qMax(1, option.rect.width()), 48 };

        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        const int rowWidth = qMax(1, option.rect.width());
        const QString cacheKey = sizeHintCacheKey(msg, rowWidth, option.font);
        if (const auto it = m_sizeHintCache.constFind(cacheKey); it != m_sizeHintCache.constEnd())
            return it.value();

        const QFont bodyFont = bodyTextFont(option.font);
        const QFont statusFont = statusTextFont(option.font);
        const MessageLayoutMetrics metrics = messageLayoutMetrics(QRect(0, 0, rowWidth, 1));
        const int bubbleMax = metrics.bubbleMax;
        const int bubbleWidth = messageBubbleWidth(msg, bubbleMax, bodyFont);
        const int h = messageMetaHeight(msg) + messageBubbleHeight(msg, bubbleWidth, bodyFont, statusFont) + 12;
        const QSize result(rowWidth, qMax(60, h));
        m_sizeHintCache.insert(cacheKey, result);
        if (m_sizeHintCache.size() > 2000)
            m_sizeHintCache.clear();
        return result;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        if (index.data(MessageListModel::IsSeparatorRole).toBool()) {
            paintDateSeparator(painter, option, index.data(MessageListModel::SeparatorTextRole).toString());
            painter->restore();
            return;
        }

        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        const bool outgoing = msg.direction == QLatin1String("out");
        const MessageLayoutMetrics metrics = messageLayoutMetrics(option.rect);
        const QRect rowRect = metrics.rowRect;
        const QRect laneRect = metrics.laneRect;
        const int avatarSide = metrics.avatarSide;
        const int gap = metrics.gap;
        const int bubbleMax = metrics.bubbleMax;
        const QFont bodyFont = bodyTextFont(option.font);
        const QFont metaFont = metaTextFont(option.font);
        const QFont statusFont = statusTextFont(option.font);
        const int bubbleW = messageBubbleWidth(msg, bubbleMax, bodyFont);
        const int metaH = messageMetaHeight(msg);
        const int bubbleH = messageBubbleHeight(msg, bubbleW, bodyFont, statusFont);
        const int totalH = metaH + bubbleH;
        const int avatarX = outgoing ? laneRect.right() - avatarSide + 1 : laneRect.left();
        const int bubbleX = outgoing ? avatarX - gap - bubbleW : avatarX + avatarSide + gap;
        const int topY = rowRect.top() + qMax(0, (rowRect.height() - totalH) / 2);

        paintAvatar(painter, QRect(avatarX, topY + metaH, avatarSide, avatarSide), outgoing, option.font);
        paintMeta(painter, QRect(bubbleX, topY, bubbleW, metaH), msg, outgoing, metaFont);

        const QRect bubbleRect(bubbleX, topY + metaH, bubbleW, bubbleH);
        painter->setPen(outgoing ? QColor(QStringLiteral("#20B8E8")) : QColor(QStringLiteral("#E5E7EB")));
        painter->setBrush(outgoing ? QColor(QStringLiteral("#20B8E8")) : QColor(QStringLiteral("#FFFFFF")));
        painter->drawRoundedRect(bubbleRect, 13, 13);

        paintMessageContent(painter, bubbleRect, bubbleW, msg, outgoing, bodyFont, statusFont);

        if (outgoing)
            paintSendStatus(painter, bubbleRect, bubbleW, msg, statusFont);

        painter->restore();
    }

private:
    struct MessageLayoutMetrics {
        QRect rowRect;
        QRect laneRect;
        int avatarSide = 40;
        int gap = 12;
        int bubbleMax = 1;
    };

    static MessageLayoutMetrics messageLayoutMetrics(const QRect& optionRect)
    {
        const int viewportW = qMax(1, optionRect.width());
        const int sidePad = viewportW < 340 ? 10 : (viewportW < 520 ? 16 : 24);
        QRect rowRect = optionRect.adjusted(sidePad, 3, -sidePad, -3);
        if (rowRect.width() < 1) {
            rowRect = QRect(optionRect.left() + sidePad,
                            optionRect.top() + 3,
                            qMax(1, viewportW - sidePad * 2),
                            qMax(1, optionRect.height() - 6));
        }

        const int laneMax = 760;
        const int laneW = qMax(1, qMin(rowRect.width(), laneMax));
        const int laneLeft = rowRect.left() + qMax(0, (rowRect.width() - laneW) / 2);
        QRect laneRect(laneLeft, rowRect.top(), laneW, rowRect.height());

        MessageLayoutMetrics metrics;
        metrics.rowRect = rowRect;
        metrics.laneRect = laneRect;
        metrics.avatarSide = laneW < 300 ? 32 : (laneW < 420 ? 36 : 40);
        metrics.gap = laneW < 300 ? 8 : 12;

        const int contentAvailable = qMax(1, laneW - metrics.avatarSide - metrics.gap);
        const qreal widthRatio = laneW < 360 ? 0.72 : 0.68;
        const int proportionalMax = qMax(1, qRound(laneW * widthRatio));
        const int hardMax = laneW < 420 ? 360 : 420;
        metrics.bubbleMax = qMax(1, qMin(qMin(hardMax, proportionalMax), contentAvailable));
        return metrics;
    }

    static QFont bodyTextFont(const QFont& base)
    {
        QFont f(base);
        if (f.pointSizeF() > 0)
            f.setPointSizeF(qMax<qreal>(10.5, f.pointSizeF() + 1.0));
        else
            f.setPixelSize(qMax(14, f.pixelSize() > 0 ? f.pixelSize() + 1 : 14));
        return f;
    }

    static QFont metaTextFont(const QFont& base)
    {
        QFont f(base);
        if (f.pointSizeF() > 0)
            f.setPointSizeF(qMax<qreal>(8.5, f.pointSizeF() - 0.5));
        else
            f.setPixelSize(qMax(11, f.pixelSize() > 0 ? f.pixelSize() - 1 : 11));
        return f;
    }

    static QFont statusTextFont(const QFont& base)
    {
        QFont f(base);
        if (f.pointSizeF() > 0)
            f.setPointSizeF(qMax<qreal>(8.5, f.pointSizeF() - 0.5));
        else
            f.setPixelSize(qMax(11, f.pixelSize() > 0 ? f.pixelSize() - 1 : 11));
        return f;
    }

    static int messageMetaHeight(const MessageRecord& msg)
    {
        Q_UNUSED(msg)
        return 0;
    }

    static QString normalizedContentType(const MessageRecord& msg)
    {
        return msg.contentType.trimmed().toLower();
    }

    static bool isImageLikeMessage(const MessageRecord& msg)
    {
        const QString type = normalizedContentType(msg);
        return type == QLatin1String("image") || type == QLatin1String("emoji");
    }

    static bool isEmojiMessage(const MessageRecord& msg)
    {
        return normalizedContentType(msg) == QLatin1String("emoji");
    }

    static bool isFileMessage(const MessageRecord& msg)
    {
        return normalizedContentType(msg) == QLatin1String("file");
    }

    static bool isVideoMessage(const MessageRecord& msg)
    {
        return normalizedContentType(msg) == QLatin1String("video");
    }

    static bool isMediaCardMessage(const MessageRecord& msg)
    {
        return isFileMessage(msg) || isVideoMessage(msg);
    }

    static QStringList contentParts(const MessageRecord& msg)
    {
        return msg.content.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    }

    static QString mediaTitle(const MessageRecord& msg)
    {
        const QStringList parts = contentParts(msg);
        if (isFileMessage(msg) && parts.size() >= 2)
            return parts.at(1);
        if (isVideoMessage(msg))
            return QStringLiteral("视频");
        const QFileInfo info(msg.contentImagePath);
        if (info.exists() && !info.fileName().isEmpty())
            return info.fileName();
        if (!msg.content.trimmed().isEmpty())
            return msg.content.trimmed();
        return isFileMessage(msg) ? QStringLiteral("文件") : QStringLiteral("视频");
    }

    static QString mediaDetail(const MessageRecord& msg)
    {
        const QStringList parts = contentParts(msg);
        if (isFileMessage(msg) && parts.size() >= 3) {
            QStringList detail = parts.mid(2);
            return detail.join(QStringLiteral(" "));
        }
        if (isVideoMessage(msg) && parts.size() >= 2)
            return parts.mid(1).join(QStringLiteral(" "));
        const QFileInfo info(msg.contentImagePath);
        if (info.exists() && info.isFile()) {
            const qint64 bytes = info.size();
            if (bytes >= 1024 * 1024)
                return QStringLiteral("%1 MB").arg(QString::number(bytes / 1024.0 / 1024.0, 'f', 1));
            if (bytes >= 1024)
                return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
        }
        return msg.content.trimmed();
    }

    static QString mediaCacheBaseKey(const MessageRecord& msg)
    {
        if (msg.contentImagePath.isEmpty())
            return {};
        const QFileInfo info(msg.contentImagePath);
        if (!info.exists() || !info.isFile())
            return {};
        return QStringLiteral("%1:%2:%3")
            .arg(info.absoluteFilePath())
            .arg(info.size())
            .arg(info.lastModified().toMSecsSinceEpoch());
    }

    static QSize originalPreviewSize(const MessageRecord& msg)
    {
        const QString key = mediaCacheBaseKey(msg);
        if (key.isEmpty())
            return {};

        static QHash<QString, QSize> sizeCache;
        if (const auto it = sizeCache.constFind(key); it != sizeCache.constEnd())
            return it.value();

        QImageReader reader(msg.contentImagePath);
        reader.setAutoTransform(true);
        QSize size = reader.size();
        if (!size.isValid()) {
            const QPixmap pm = loadPreviewPixmap(msg);
            size = pm.size();
        }
        if (!size.isEmpty()) {
            sizeCache.insert(key, size);
            if (sizeCache.size() > 2000)
                sizeCache.clear();
        }
        return size;
    }

    static QPixmap loadPreviewPixmap(const MessageRecord& msg)
    {
        const QString key = mediaCacheBaseKey(msg);
        if (key.isEmpty())
            return {};

        QPixmap pm;
        const QString cacheKey = QStringLiteral("aggregate.preview.original:%1").arg(key);
        if (QPixmapCache::find(cacheKey, &pm))
            return pm;

        QImageReader reader(msg.contentImagePath);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull())
            return {};

        pm = QPixmap::fromImage(image);
        QPixmapCache::insert(cacheKey, pm);
        return pm;
    }

    static QPixmap loadScaledPreviewPixmap(const MessageRecord& msg, const QSize& targetSize,
                                           Qt::AspectRatioMode aspectMode = Qt::KeepAspectRatio)
    {
        if (targetSize.isEmpty())
            return {};

        const QString key = mediaCacheBaseKey(msg);
        if (key.isEmpty())
            return {};

        const QString modeKey = aspectMode == Qt::KeepAspectRatioByExpanding
                                    ? QStringLiteral("expand")
                                    : QStringLiteral("fit");
        const QString cacheKey = QStringLiteral("aggregate.preview.scaled:%1:%2:%3x%4")
                                     .arg(modeKey, key)
                                     .arg(targetSize.width())
                                     .arg(targetSize.height());
        QPixmap pm;
        if (QPixmapCache::find(cacheKey, &pm))
            return pm;

        const QPixmap original = loadPreviewPixmap(msg);
        if (original.isNull())
            return {};

        pm = original.scaled(targetSize, aspectMode, Qt::SmoothTransformation);
        QPixmapCache::insert(cacheKey, pm);
        return pm;
    }

    static QSize messageImageSize(const MessageRecord& msg, int bubbleWidth)
    {
        if (!isImageLikeMessage(msg))
            return {};
        const QSize originalSize = originalPreviewSize(msg);
        if (originalSize.isEmpty())
            return {};
        const int maxW = isEmojiMessage(msg) ? qMin(180, qMax(1, bubbleWidth - 24))
                                             : qMin(336, qMax(1, bubbleWidth - 24));
        const int maxH = isEmojiMessage(msg) ? 180 : 260;
        const QSize bounded = originalSize.scaled(maxW, maxH, Qt::KeepAspectRatio);
        return { qMax(1, bounded.width()), qMax(1, bounded.height()) };
    }

    static QString messageDisplayText(const MessageRecord& msg)
    {
        if (isMediaCardMessage(msg))
            return {};
        if (isImageLikeMessage(msg) && !originalPreviewSize(msg).isEmpty())
            return {};
        if (!msg.content.trimmed().isEmpty())
            return msg.content;
        if (msg.contentImagePath.isEmpty())
            return QStringLiteral("暂未识别到文本");
        if (!QFile::exists(msg.contentImagePath))
            return QStringLiteral("图片文件不存在");
        QPixmap pm(msg.contentImagePath);
        if (pm.isNull())
            return QStringLiteral("图片加载失败");
        return QStringLiteral("图片内容待识别");
    }

    static bool messageDisplayTextIsHint(const MessageRecord& msg)
    {
        return !isMediaCardMessage(msg) && msg.content.trimmed().isEmpty();
    }

    static int textHeightForWidth(const QString& text, int width, const QFont& font)
    {
        QFontMetrics fm(font);
        const QRect bounds = fm.boundingRect(QRect(0, 0, qMax(1, width), 10000),
                                             Qt::TextWordWrap | Qt::TextExpandTabs,
                                             text);
        return qMax(fm.height(), bounds.height());
    }

    static int textNaturalWidth(const QString& text, int width, const QFont& font)
    {
        QFontMetrics fm(font);
        const QRect bounds = fm.boundingRect(QRect(0, 0, qMax(1, width), 10000),
                                             Qt::TextWordWrap | Qt::TextExpandTabs,
                                             text);
        return qBound(28, bounds.width() + 2, qMax(28, width));
    }

    static int messageBubbleWidth(const MessageRecord& msg, int bubbleMax, const QFont& bodyFont)
    {
        if (isFileMessage(msg))
            return qMin(bubbleMax, qMax(240, qMin(320, bubbleMax)));
        if (isVideoMessage(msg))
            return qMin(bubbleMax, qMax(220, qMin(300, bubbleMax)));

        const int innerMax = qMax(1, bubbleMax - 24);
        const QString displayText = messageDisplayText(msg);
        const QSize imageSize = messageImageSize(msg, bubbleMax);
        const int naturalTextW = textNaturalWidth(displayText, innerMax, bodyFont);
        const int minBubbleW = messageDisplayTextIsHint(msg) ? 112 : 84;
        return qMin(bubbleMax, qMax(minBubbleW, qMax(naturalTextW, imageSize.width()) + 24));
    }

    static int messageBubbleHeight(const MessageRecord& msg, int bubbleWidth,
                                   const QFont& bodyFont, const QFont& statusFont)
    {
        const QString displayText = messageDisplayText(msg);
        const QSize imageSize = messageImageSize(msg, bubbleWidth);
        if (isFileMessage(msg))
            return (msg.direction == QLatin1String("out") ? 84 : 76)
                   + messageStatusBlockHeight(msg, statusFont);
        if (isVideoMessage(msg)) {
            const int previewH = originalPreviewSize(msg).isEmpty() ? 112 : 150;
            return previewH + 44 + messageStatusBlockHeight(msg, statusFont);
        }

        const int textH = displayText.isEmpty() ? 0 : textHeightForWidth(displayText, bubbleWidth - 24, bodyFont);
        int h = 8;
        if (imageSize.height() > 0)
            h += imageSize.height() + (displayText.isEmpty() ? 0 : 6);
        h += textH;
        if (msg.direction == QLatin1String("out"))
            h += messageStatusBlockHeight(msg, statusFont);
        h += 7;
        return qMax(msg.direction == QLatin1String("out") ? 46 : 40, h);
    }

    static int messageStatusBlockHeight(const MessageRecord& msg, const QFont& statusFont)
    {
        if (msg.direction != QLatin1String("out"))
            return 0;
        const int lineH = QFontMetrics(statusFont).height();
        return lineH + 5 + ((msg.syncStatus == 12 && !msg.errorReason.isEmpty()) ? lineH + 3 : 0);
    }

    void paintDateSeparator(QPainter* painter, const QStyleOptionViewItem& option, const QString& text) const
    {
        const QString label = text.trimmed().isEmpty() ? QStringLiteral("--:--") : text.trimmed();
        QFontMetrics fm(option.font);
        const QSize textSize(fm.horizontalAdvance(label) + 24, 28);
        const QRect pill(QPoint(option.rect.center().x() - textSize.width() / 2,
                                option.rect.center().y() - textSize.height() / 2),
                         textSize);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#E5E7EB")));
        painter->drawRoundedRect(pill, 14, 14);
        painter->setPen(QColor(QStringLiteral("#6B7280")));
        painter->drawText(pill, Qt::AlignCenter, label);
    }

    void paintAvatar(QPainter* painter, const QRect& avatarRect, bool outgoing, const QFont& baseFont) const
    {
        Q_UNUSED(baseFont)
        const QPixmap& avatarPixmap = outgoing ? m_selfAvatarPixmap : m_customerAvatarPixmap;
        if (!avatarPixmap.isNull())
            painter->drawPixmap(avatarRect, avatarPixmap);
    }

    void paintMeta(QPainter* painter, const QRect& metaRect, const MessageRecord& msg,
                   bool outgoing, const QFont& metaFont) const
    {
        if (metaRect.height() <= 0)
            return;

        painter->setFont(metaFont);
        const QFontMetrics fm(metaFont);
        QString timeStr;
        if (msg.createdAt.isValid())
            timeStr = msg.createdAt.toString(QStringLiteral("HH:mm"));
        else if (!msg.originalTimestamp.isEmpty())
            timeStr = msg.originalTimestamp;

        QString name = outgoing ? m_selfDisplayName.trimmed() : msg.senderName.trimmed();
        if (name.isEmpty())
            name = outgoing ? QStringLiteral("我") : QStringLiteral("客户");

        painter->setPen(QColor(QStringLiteral("#6B7280")));
        if (outgoing) {
            const int nameW = qMin(160, fm.horizontalAdvance(name) + 4);
            const int timeW = timeStr.isEmpty() ? 0 : qMin(92, fm.horizontalAdvance(timeStr) + 4);
            if (!timeStr.isEmpty()) {
                painter->drawText(QRect(metaRect.right() - nameW - timeW - 8, metaRect.top(),
                                        timeW, metaRect.height()),
                                  Qt::AlignRight | Qt::AlignVCenter, timeStr);
            }
            painter->drawText(QRect(metaRect.right() - nameW, metaRect.top(), nameW, metaRect.height()),
                              Qt::AlignRight | Qt::AlignVCenter, name);
            return;
        }

        QString meta = name;
        if (!timeStr.isEmpty())
            meta += QStringLiteral("  ") + timeStr;
        painter->drawText(metaRect.adjusted(0, 0, 120, 0), Qt::AlignLeft | Qt::AlignVCenter, meta);
    }

    void paintMessageContent(QPainter* painter, const QRect& bubbleRect, int bubbleW,
                             const MessageRecord& msg, bool outgoing,
                             const QFont& bodyFont, const QFont& statusFont) const
    {
        if (isFileMessage(msg)) {
            paintFileCard(painter, bubbleRect, bubbleW, msg, outgoing, bodyFont, statusFont);
            return;
        }
        if (isVideoMessage(msg)) {
            paintVideoCard(painter, bubbleRect, bubbleW, msg, outgoing, bodyFont, statusFont);
            return;
        }

        int contentY = bubbleRect.top() + 8;
        const QSize imageSize = messageImageSize(msg, bubbleW);
        if (imageSize.height() > 0) {
            const QPixmap pm = loadScaledPreviewPixmap(msg, imageSize);
            if (!pm.isNull()) {
                painter->drawPixmap(QRect(bubbleRect.left() + 12, contentY, pm.width(), pm.height()), pm);
                contentY += pm.height() + 6;
            }
        }

        const int statusReserve = outgoing ? messageStatusBlockHeight(msg, statusFont) + 3 : 7;
        const QRect textRect(bubbleRect.left() + 12, contentY,
                             bubbleW - 24, qMax(1, bubbleRect.bottom() - contentY - statusReserve));
        painter->setFont(bodyFont);
        if (messageDisplayTextIsHint(msg))
            painter->setPen(outgoing ? QColor(QStringLiteral("#E0F2FE")) : QColor(QStringLiteral("#9CA3AF")));
        else
            painter->setPen(outgoing ? QColor(QStringLiteral("#FFFFFF")) : QColor(QStringLiteral("#111827")));
        painter->drawText(textRect,
                          Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextExpandTabs,
                          messageDisplayText(msg));
    }

    void paintFileCard(QPainter* painter, const QRect& bubbleRect, int bubbleW,
                       const MessageRecord& msg, bool outgoing,
                       const QFont& bodyFont, const QFont& statusFont) const
    {
        const QRect contentRect = bubbleRect.adjusted(12, 10, -12, -(outgoing ? messageStatusBlockHeight(msg, statusFont) + 7 : 10));
        const QRect iconRect(contentRect.left(), contentRect.top() + 4, 38, 44);

        painter->setPen(Qt::NoPen);
        painter->setBrush(outgoing ? QColor(QStringLiteral("#DFF7FF")) : QColor(QStringLiteral("#E0F2FE")));
        painter->drawRoundedRect(iconRect, 6, 6);
        painter->setBrush(outgoing ? QColor(QStringLiteral("#0EA5E9")) : QColor(QStringLiteral("#0284C7")));
        painter->drawRect(QRect(iconRect.left() + 8, iconRect.top() + 10, iconRect.width() - 16, 4));
        painter->drawRect(QRect(iconRect.left() + 8, iconRect.top() + 20, iconRect.width() - 16, 4));
        painter->drawRect(QRect(iconRect.left() + 8, iconRect.top() + 30, iconRect.width() - 22, 4));

        const QRect textRect(iconRect.right() + 12, contentRect.top(),
                             qMax(1, contentRect.right() - iconRect.right() - 12),
                             contentRect.height());
        QFont titleFont(bodyFont);
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(outgoing ? QColor(QStringLiteral("#FFFFFF")) : QColor(QStringLiteral("#111827")));
        const QFontMetrics titleFm(titleFont);
        painter->drawText(QRect(textRect.left(), textRect.top() + 2, textRect.width(), titleFm.height() + 2),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          titleFm.elidedText(mediaTitle(msg), Qt::ElideMiddle, textRect.width()));

        const QString detail = mediaDetail(msg);
        if (!detail.isEmpty()) {
            painter->setFont(statusFont);
            painter->setPen(outgoing ? QColor(QStringLiteral("#E0F2FE")) : QColor(QStringLiteral("#6B7280")));
            const QFontMetrics detailFm(statusFont);
            painter->drawText(QRect(textRect.left(), textRect.top() + titleFm.height() + 8,
                                    textRect.width(), detailFm.height() + 2),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              detailFm.elidedText(detail, Qt::ElideRight, textRect.width()));
        }
    }

    void paintVideoCard(QPainter* painter, const QRect& bubbleRect, int bubbleW,
                        const MessageRecord& msg, bool outgoing,
                        const QFont& bodyFont, const QFont& statusFont) const
    {
        const int statusReserve = outgoing ? messageStatusBlockHeight(msg, statusFont) : 0;
        const QRect previewRect = bubbleRect.adjusted(10, 10, -10, -(44 + statusReserve));
        QPixmap preview = loadScaledPreviewPixmap(
            msg,
            previewRect.size(),
            Qt::KeepAspectRatioByExpanding);

        painter->setPen(Qt::NoPen);
        if (!preview.isNull()) {
            const QRect src((preview.width() - previewRect.width()) / 2,
                            (preview.height() - previewRect.height()) / 2,
                            previewRect.width(),
                            previewRect.height());
            painter->drawPixmap(previewRect, preview, src);
            painter->fillRect(previewRect, QColor(17, 24, 39, 80));
        } else {
            painter->setBrush(outgoing ? QColor(QStringLiteral("#DFF7FF")) : QColor(QStringLiteral("#F3F4F6")));
            painter->drawRoundedRect(previewRect, 8, 8);
        }

        const QPoint center = previewRect.center();
        QPolygon play;
        play << QPoint(center.x() - 8, center.y() - 12)
             << QPoint(center.x() - 8, center.y() + 12)
             << QPoint(center.x() + 14, center.y());
        painter->setBrush(!preview.isNull() || outgoing ? QColor(QStringLiteral("#FFFFFF"))
                                                         : QColor(QStringLiteral("#0EA5E9")));
        painter->drawPolygon(play);

        const QRect textRect(bubbleRect.left() + 12, previewRect.bottom() + 6,
                             bubbleW - 24, 32);
        QFont titleFont(bodyFont);
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(outgoing ? QColor(QStringLiteral("#FFFFFF")) : QColor(QStringLiteral("#111827")));
        const QFontMetrics titleFm(titleFont);
        const QString detail = mediaDetail(msg);
        QString title = mediaTitle(msg);
        if (!detail.isEmpty() && detail != title)
            title += QStringLiteral("  ") + detail;
        painter->drawText(textRect,
                          Qt::AlignLeft | Qt::AlignVCenter,
                          titleFm.elidedText(title, Qt::ElideRight, textRect.width()));
    }

    void paintSendStatus(QPainter* painter, const QRect& bubbleRect, int bubbleW,
                         const MessageRecord& msg, const QFont& statusFont) const
    {
        QString statusText;
        QColor statusColor;

        switch (msg.syncStatus) {
        case 10:
            statusText = QStringLiteral("发送中...");
            statusColor = QColor(QStringLiteral("#FBBF24"));
            break;
        case 11:
            statusText = QStringLiteral("已发送");
            statusColor = QColor(QStringLiteral("#E0F2FE"));
            break;
        case 12:
            statusText = QStringLiteral("发送失败");
            statusColor = QColor(QStringLiteral("#FCA5A5"));
            break;
        default:
            statusText = QStringLiteral("已发送");
            statusColor = QColor(QStringLiteral("#E0F2FE"));
            break;
        }

        painter->setFont(statusFont);
        const int statusH = QFontMetrics(statusFont).height();
        const int blockH = messageStatusBlockHeight(msg, statusFont);
        painter->setPen(statusColor);
        const int statusTop = bubbleRect.bottom() - blockH + 4;
        painter->drawText(QRect(bubbleRect.left() + 12, statusTop, bubbleW - 24, statusH),
                          Qt::AlignLeft | Qt::AlignVCenter, statusText);
        if (msg.syncStatus == 12 && !msg.errorReason.isEmpty()) {
            painter->setPen(QColor(QStringLiteral("#DC2626")));
            painter->drawText(QRect(bubbleRect.left() + 12, statusTop + statusH + 2, bubbleW - 24, statusH),
                              Qt::AlignLeft | Qt::AlignTop, msg.errorReason);
        }
    }

    static QString sizeHintCacheKey(const MessageRecord& msg, int rowWidth, const QFont& font)
    {
        return QStringList{
            QString::number(rowWidth),
            font.toString(),
            QString::number(msg.id),
            QString::number(msg.syncStatus),
            msg.errorReason,
            msg.content,
            msg.contentImagePath,
            msg.contentType,
            msg.originalTimestamp,
            msg.direction,
        }.join(QChar(0x1f));
    }

    QString m_selfDisplayName;
    QPixmap m_selfAvatarPixmap;
    QPixmap m_customerAvatarPixmap;
    mutable QHash<QString, QSize> m_sizeHintCache;
};

class MessageListView final : public QListView
{
public:
    using QListView::QListView;

    void setBottomReserve(int px)
    {
        setViewportMargins(0, 0, 0, qMax(0, px));
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        const bool widthChanged = !event->oldSize().isValid()
                                  || event->oldSize().width() != event->size().width();
        if (widthChanged) {
            if (auto* delegate = dynamic_cast<ModernMessageItemDelegate*>(itemDelegate()))
                delegate->clearSizeHintCache();
        }
        QListView::resizeEvent(event);
        if (widthChanged) {
            doItemsLayout();
            if (QWidget* vp = viewport())
                vp->update();
        }
    }
};

/** 与父级 AggregateChatForm 样式隔离，避免 QLabel 继承深色字压在系统深色弹窗背景上。 */
QString aggregateMessageBoxContrastStyle()
{
    return ApplyStyle::messageBoxContrastStyle();
}

/** 与主窗口侧栏一致的圆角栅格化；cornerRadiusLogical 取边长一半即为圆形。 */
static QPixmap roundedAggregateAvatarPixmap(const QPixmap& source, int logicalSide, qreal dpr,
                                            int cornerRadiusLogical)
{
    if (source.isNull())
        return source;
    const int s = qMax(1, qRound(logicalSide * dpr));
    const int r = qBound(1, qRound(cornerRadiusLogical * dpr), s / 2);
    QPixmap square(s, s);
    square.fill(Qt::transparent);
    {
        QPainter pt(&square);
        pt.setRenderHint(QPainter::Antialiasing);
        QPixmap fill = source.scaled(s, s, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int ox = qMax(0, (fill.width() - s) / 2);
        const int oy = qMax(0, (fill.height() - s) / 2);
        pt.drawPixmap(0, 0, fill, ox, oy, s, s);
    }
    QPixmap out(s, s);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, s, s, qreal(r), qreal(r));
    p.setClipPath(path);
    p.drawPixmap(0, 0, square);
    p.end();
    out.setDevicePixelRatio(dpr);
    return out;
}

static QString aggregateModelMenuLabel(const QString& sessionModelKey)
{
    return aiPresetLabel(sessionModelKey);
}

QString aggregateMetricDurationLabel(int durationMs)
{
    if (durationMs <= 0)
        return QStringLiteral("—");
    if (durationMs < 1000)
        return QStringLiteral("%1ms").arg(durationMs);
    return QStringLiteral("%1s").arg(QString::number(durationMs / 1000.0, 'f', 1));
}

QStringList jsonArrayStrings(const QJsonObject& object, const QString& key, int maxItems = 3)
{
    QStringList out;
    const QJsonArray arr = object.value(key).toArray();
    for (const QJsonValue& value : arr) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
        if (out.size() >= maxItems)
            break;
    }
    return out;
}

QString customerProfileDisplayText(const QJsonObject& profile, const QDateTime& updatedAt)
{
    QStringList lines;
    const QString summary = profile.value(QStringLiteral("summary")).toString().trimmed();
    const QString currentNeed = profile.value(QStringLiteral("current_need")).toString().trimmed();
    if (!summary.isEmpty())
        lines.append(QStringLiteral("概况：%1").arg(summary));
    if (!currentNeed.isEmpty())
        lines.append(QStringLiteral("当前诉求：%1").arg(currentNeed));

    const auto appendArrayLine = [&lines, &profile](const QString& label, const QString& key) {
        const QStringList items = jsonArrayStrings(profile, key);
        if (!items.isEmpty())
            lines.append(QStringLiteral("%1：%2").arg(label, items.join(QStringLiteral("、"))));
    };
    appendArrayLine(QStringLiteral("关注点"), QStringLiteral("concerns"));
    appendArrayLine(QStringLiteral("偏好"), QStringLiteral("preferences"));
    appendArrayLine(QStringLiteral("注意"), QStringLiteral("risks"));

    if (updatedAt.isValid())
        lines.append(QStringLiteral("更新时间：%1").arg(updatedAt.toString(QStringLiteral("MM-dd HH:mm"))));
    if (lines.isEmpty())
        return QStringLiteral("暂无客户信息");
    return lines.join(QLatin1Char('\n'));
}

void showAggregateInfo(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
    box.setStyleSheet(aggregateMessageBoxContrastStyle());
    box.exec();
}

void showAggregateWarning(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    box.setStyleSheet(aggregateMessageBoxContrastStyle());
    box.exec();
}

bool pythonServiceStartupBackfillEnabled()
{
    QSettings settings = AppSettings::create();
    const bool defaultBackfill = RuntimeMode::isSingleHostServiceDb()
        ? false
        : RuntimeMode::ownsBusinessDatabase();
    return settings.value(QStringLiteral("pythonService/startupBackfillEnabled"),
                          defaultBackfill).toBool();
}

bool isImageFilePath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("png")
        || suffix == QLatin1String("jpg")
        || suffix == QLatin1String("jpeg")
        || suffix == QLatin1String("webp")
        || suffix == QLatin1String("bmp")
        || suffix == QLatin1String("gif");
}

QDateTime serviceDateTime(const QJsonObject& object, const QString& key)
{
    const QString raw = object.value(key).toString().trimmed();
    if (raw.isEmpty())
        return {};
    QDateTime dt = QDateTime::fromString(raw, Qt::ISODate);
    if (dt.isValid())
        return dt.toLocalTime();
    dt = QDateTime::fromString(raw, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (dt.isValid())
        return dt;
    return {};
}

int syncStatusFromServiceStatus(const QString& status)
{
    const QString normalized = status.trimmed().toLower();
    if (normalized == QLatin1String("pending"))
        return 10;
    if (normalized == QLatin1String("sent"))
        return 11;
    if (normalized == QLatin1String("failed"))
        return 12;
    return 1;
}

MessageRecord serviceMessageRecord(const QJsonObject& object, int localConversationId)
{
    MessageRecord record;
    record.id = object.value(QStringLiteral("id")).toInt();
    record.conversationId = localConversationId;
    record.direction = object.value(QStringLiteral("direction")).toString(QStringLiteral("in"));
    record.sender = object.value(QStringLiteral("sender")).toString(
        record.direction == QLatin1String("out") ? QStringLiteral("agent") : QStringLiteral("customer"));
    record.senderName = object.value(QStringLiteral("sender_name")).toString();
    record.content = object.value(QStringLiteral("content")).toString();
    record.platformMsgId = object.value(QStringLiteral("platform_message_id")).toString(
        object.value(QStringLiteral("platform_msg_id")).toString());
    record.clientMessageId = object.value(QStringLiteral("client_message_id")).toString();
    record.status = object.value(QStringLiteral("status")).toString(QStringLiteral("observed"));
    record.syncStatus = syncStatusFromServiceStatus(record.status);
    record.errorReason = object.value(QStringLiteral("error_reason")).toString();
    record.contentType = object.value(QStringLiteral("content_type")).toString(QStringLiteral("text"));
    record.contentImagePath = object.value(QStringLiteral("content_image_path")).toString(
        object.value(QStringLiteral("evidence_ref")).toString());
    record.originalTimestamp = object.value(QStringLiteral("original_timestamp")).toString();
    record.createdAt = serviceDateTime(object, QStringLiteral("message_time"));
    if (!record.createdAt.isValid())
        record.createdAt = serviceDateTime(object, QStringLiteral("created_at"));
    record.observedAt = record.createdAt;
    record.cacheScope = QStringLiteral("service_db");
    record.cacheOrigin = QStringLiteral("python_service_db");
    return record;
}

ConversationInfo serviceConversationInfo(const QJsonObject& object)
{
    ConversationInfo info;
    info.id = object.value(QStringLiteral("id")).toInt();
    info.platform = object.value(QStringLiteral("platform")).toString().trimmed().toLower();
    info.platformConversationId =
        object.value(QStringLiteral("platform_conversation_id")).toString().trimmed();
    info.accountId = object.value(QStringLiteral("account_id")).toString().trimmed();
    info.customerName = object.value(QStringLiteral("customer_name")).toString().trimmed();
    if (info.customerName.isEmpty())
        info.customerName = info.platformConversationId;
    info.lastMessage = object.value(QStringLiteral("last_message")).toString();
    info.lastTime = serviceDateTime(object, QStringLiteral("last_time"));
    info.unreadCount = object.value(QStringLiteral("unread_count")).toInt(0);
    info.status = object.value(QStringLiteral("status")).toString(QStringLiteral("active"));
    info.createdAt = serviceDateTime(object, QStringLiteral("created_at"));
    info.updatedAt = serviceDateTime(object, QStringLiteral("updated_at"));
    info.sourceType = QStringLiteral("python_service_truth");
    info.confidence = 100;
    info.cacheScope = QStringLiteral("service_db");
    info.cacheOrigin = QStringLiteral("python_service_db");
    return info;
}

bool isVideoFilePath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("mp4")
        || suffix == QLatin1String("mov")
        || suffix == QLatin1String("avi")
        || suffix == QLatin1String("mkv")
        || suffix == QLatin1String("wmv");
}

QString readableFileSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / 1024.0 / 1024.0, 'f', 1));
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    return QStringLiteral("%1 B").arg(qMax<qint64>(0, bytes));
}

QString saveComposeImageToCache(const QImage& image)
{
    if (image.isNull())
        return QString();
    QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (root.isEmpty())
        root = QDir::tempPath();
    const QString folder = root + QStringLiteral("/compose-media/")
        + QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
    QDir().mkpath(folder);
    const QString path = folder + QLatin1Char('/')
        + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QStringLiteral(".png");
    return image.save(path, "PNG") ? path : QString();
}

bool openLocalFileWithDefaultApp(QWidget* parent, const QString& path, const QString& failureTitle)
{
    if (path.trimmed().isEmpty() || !QFile::exists(path)) {
        showAggregateWarning(parent, failureTitle, QStringLiteral("文件不存在或已被移动。"));
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        showAggregateWarning(parent, failureTitle, QStringLiteral("无法打开该文件，请检查系统默认打开程序。"));
        return false;
    }
    return true;
}

class ImagePreviewDialog final : public QDialog
{
public:
    explicit ImagePreviewDialog(const QString& imagePath, QWidget* parent = nullptr)
        : QDialog(parent)
        , m_imagePath(imagePath)
    {
        const QString fileName = QFileInfo(imagePath).fileName();
        setWindowTitle(fileName.isEmpty() ? QStringLiteral("图片预览") : fileName);
        resize(860, 620);
        setMinimumSize(420, 320);

        m_original = QPixmap(imagePath);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(10);

        auto* toolbar = new QHBoxLayout();
        toolbar->setContentsMargins(0, 0, 0, 0);
        toolbar->setSpacing(8);

        auto* fitBtn = new QToolButton(this);
        fitBtn->setText(QStringLiteral("适配"));
        auto* actualBtn = new QToolButton(this);
        actualBtn->setText(QStringLiteral("1:1"));
        auto* zoomOutBtn = new QToolButton(this);
        zoomOutBtn->setText(QStringLiteral("-"));
        auto* zoomInBtn = new QToolButton(this);
        zoomInBtn->setText(QStringLiteral("+"));
        auto* openBtn = new QToolButton(this);
        openBtn->setText(QStringLiteral("系统打开"));
        auto* closeBtn = new QToolButton(this);
        closeBtn->setText(QStringLiteral("关闭"));

        toolbar->addWidget(fitBtn);
        toolbar->addWidget(actualBtn);
        toolbar->addWidget(zoomOutBtn);
        toolbar->addWidget(zoomInBtn);
        toolbar->addStretch(1);
        toolbar->addWidget(openBtn);
        toolbar->addWidget(closeBtn);
        root->addLayout(toolbar);

        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setWidgetResizable(false);
        m_scrollArea->setAlignment(Qt::AlignCenter);
        m_scrollArea->setFrameShape(QFrame::NoFrame);
        m_imageLabel = new QLabel(m_scrollArea);
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_scrollArea->setWidget(m_imageLabel);
        root->addWidget(m_scrollArea, 1);

        connect(fitBtn, &QToolButton::clicked, this, [this]() { fitToWindow(); });
        connect(actualBtn, &QToolButton::clicked, this, [this]() {
            m_fitMode = false;
            m_scale = 1.0;
            updatePixmap();
        });
        connect(zoomOutBtn, &QToolButton::clicked, this, [this]() {
            m_fitMode = false;
            m_scale = qMax<qreal>(0.1, m_scale * 0.8);
            updatePixmap();
        });
        connect(zoomInBtn, &QToolButton::clicked, this, [this]() {
            m_fitMode = false;
            m_scale = qMin<qreal>(8.0, m_scale * 1.25);
            updatePixmap();
        });
        connect(openBtn, &QToolButton::clicked, this, [this]() {
            openLocalFileWithDefaultApp(this, m_imagePath, QStringLiteral("打开图片失败"));
        });
        connect(closeBtn, &QToolButton::clicked, this, &QDialog::accept);

        if (m_original.isNull()) {
            m_imageLabel->setText(QStringLiteral("图片不可预览"));
            m_imageLabel->setMinimumSize(360, 220);
        } else {
            m_fitMode = true;
            QTimer::singleShot(0, this, [this]() { fitToWindow(); });
        }
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QDialog::resizeEvent(event);
        if (m_fitMode)
            fitToWindow();
    }

private:
    void fitToWindow()
    {
        if (m_original.isNull() || !m_scrollArea)
            return;
        m_fitMode = true;
        const QSize viewport = m_scrollArea->viewport()->size();
        const QSize fitted = m_original.size().scaled(qMax(1, viewport.width() - 8),
                                                      qMax(1, viewport.height() - 8),
                                                      Qt::KeepAspectRatio);
        m_scale = qMin(qreal(fitted.width()) / qreal(qMax(1, m_original.width())),
                       qreal(fitted.height()) / qreal(qMax(1, m_original.height())));
        updatePixmap();
    }

    void updatePixmap()
    {
        if (m_original.isNull() || !m_imageLabel)
            return;
        const QSize target(qMax(1, qRound(m_original.width() * m_scale)),
                           qMax(1, qRound(m_original.height() * m_scale)));
        m_imageLabel->setPixmap(m_original.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_imageLabel->resize(target);
    }

    QString m_imagePath;
    QPixmap m_original;
    QLabel* m_imageLabel = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    qreal m_scale = 1.0;
    bool m_fitMode = false;
};

QString mediaTimeText(qint64 ms)
{
    const qint64 totalSeconds = qMax<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

class VideoPreviewDialog final : public QDialog
{
public:
    explicit VideoPreviewDialog(const QString& videoPath, QWidget* parent = nullptr)
        : QDialog(parent)
        , m_videoPath(videoPath)
    {
        const QString fileName = QFileInfo(videoPath).fileName();
        setWindowTitle(fileName.isEmpty() ? QStringLiteral("视频预览") : fileName);
        resize(860, 620);
        setMinimumSize(420, 320);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(10);

        m_videoWidget = new QVideoWidget(this);
        m_videoWidget->setMinimumSize(360, 220);
        root->addWidget(m_videoWidget, 1);

        auto* controls = new QHBoxLayout();
        controls->setContentsMargins(0, 0, 0, 0);
        controls->setSpacing(8);

        m_playButton = new QToolButton(this);
        m_playButton->setText(QStringLiteral("播放"));
        m_positionSlider = new QSlider(Qt::Horizontal, this);
        m_positionSlider->setRange(0, 0);
        m_timeLabel = new QLabel(QStringLiteral("0:00 / 0:00"), this);
        auto* openButton = new QToolButton(this);
        openButton->setText(QStringLiteral("系统打开"));
        auto* closeButton = new QToolButton(this);
        closeButton->setText(QStringLiteral("关闭"));

        controls->addWidget(m_playButton);
        controls->addWidget(m_positionSlider, 1);
        controls->addWidget(m_timeLabel);
        controls->addWidget(openButton);
        controls->addWidget(closeButton);
        root->addLayout(controls);

        m_player = new QMediaPlayer(this);
        m_audioOutput = new QAudioOutput(this);
        m_audioOutput->setVolume(0.7);
        m_player->setAudioOutput(m_audioOutput);
        m_player->setVideoOutput(m_videoWidget);
        m_player->setSource(QUrl::fromLocalFile(videoPath));

        connect(m_playButton, &QToolButton::clicked, this, [this]() {
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                m_player->pause();
            else
                m_player->play();
        });
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
            m_playButton->setText(state == QMediaPlayer::PlayingState ? QStringLiteral("暂停") : QStringLiteral("播放"));
        });
        connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
            m_durationMs = qMax<qint64>(0, duration);
            m_positionSlider->setRange(0, qMax(0, int(m_durationMs / 1000)));
            updateTimeLabel(m_player->position());
        });
        connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
            if (!m_sliderPressed)
                m_positionSlider->setValue(qBound(0, int(position / 1000), m_positionSlider->maximum()));
            updateTimeLabel(position);
        });
        connect(m_positionSlider, &QSlider::sliderPressed, this, [this]() { m_sliderPressed = true; });
        connect(m_positionSlider, &QSlider::sliderReleased, this, [this]() {
            m_sliderPressed = false;
            m_player->setPosition(qint64(m_positionSlider->value()) * 1000);
        });
        connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value) {
            updateTimeLabel(qint64(value) * 1000);
        });
        connect(openButton, &QToolButton::clicked, this, [this]() {
            openLocalFileWithDefaultApp(this, m_videoPath, QStringLiteral("打开视频失败"));
        });
        connect(closeButton, &QToolButton::clicked, this, &QDialog::accept);
    }

    ~VideoPreviewDialog() override
    {
        if (m_player)
            m_player->stop();
    }

private:
    void updateTimeLabel(qint64 position)
    {
        if (m_timeLabel)
            m_timeLabel->setText(QStringLiteral("%1 / %2").arg(mediaTimeText(position), mediaTimeText(m_durationMs)));
    }

    QString m_videoPath;
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QVideoWidget* m_videoWidget = nullptr;
    QToolButton* m_playButton = nullptr;
    QSlider* m_positionSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    qint64 m_durationMs = 0;
    bool m_sliderPressed = false;
};

class PythonServiceStatusDialog final : public QDialog
{
public:
    explicit PythonServiceStatusDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Python 服务状态"));
        resize(620, 420);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(10);

        m_stateLabel = new QLabel(this);
        m_stateLabel->setWordWrap(true);
        layout->addWidget(m_stateLabel);

        m_logView = new QPlainTextEdit(this);
        m_logView->setReadOnly(true);
        m_logView->setPlaceholderText(QStringLiteral("暂无服务日志。"));
        layout->addWidget(m_logView, 1);

        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        m_startStopButton = new QPushButton(this);
        auto* refreshButton = new QPushButton(QStringLiteral("刷新状态"), this);
        auto* closeButton = new QPushButton(QStringLiteral("关闭"), this);
        row->addWidget(m_startStopButton);
        row->addWidget(refreshButton);
        row->addStretch(1);
        row->addWidget(closeButton);
        layout->addLayout(row);

        auto& controller = PythonServiceController::instance();
        connect(&controller, &PythonServiceController::stateChanged,
                this, [this]() { refresh(); });
        connect(&controller, &PythonServiceController::logAppended,
                this, [this](const QString& line) {
                    if (m_logView) {
                        m_logView->appendPlainText(line);
                        m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
                    }
                    refresh();
                });
        connect(m_startStopButton, &QPushButton::clicked, this, []() {
            auto& c = PythonServiceController::instance();
            if (c.isManagedServiceRunning() || c.state() == PythonServiceController::State::ExternalRunning)
                c.stopService();
            else
                c.startService();
        });
        connect(refreshButton, &QPushButton::clicked, this, []() {
            PythonServiceController::instance().refreshConnectionState();
        });
        connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

        setLogs(controller.humanLogs());
        refresh();
    }

private:
    void setLogs(const QStringList& logs)
    {
        if (!m_logView)
            return;
        m_logView->setPlainText(logs.join(QStringLiteral("\n")));
        m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
    }

    void refresh()
    {
        auto& controller = PythonServiceController::instance();
        if (m_stateLabel)
            m_stateLabel->setText(controller.stateText());
        if (!m_startStopButton)
            return;
        if (controller.isBusy()) {
            m_startStopButton->setEnabled(false);
            m_startStopButton->setText(QStringLiteral("处理中"));
            return;
        }
        m_startStopButton->setEnabled(true);
        m_startStopButton->setText(
            (controller.isManagedServiceRunning()
             || controller.state() == PythonServiceController::State::ExternalRunning)
                ? QStringLiteral("停止服务")
                : QStringLiteral("启动服务"));
    }

    QLabel* m_stateLabel = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    QPushButton* m_startStopButton = nullptr;
};

bool handleAggregateBuildFailure(QWidget* parent,
                                 const AggregateAiBuiltRequest& built,
                                 bool silent,
                                 QString* silentReason)
{
    if (silentReason)
        *silentReason = built.failureDetail;
    if (silent || built.ok())
        return built.ok();

    switch (built.failure) {
    case AggregateAiBuildFailure::MissingApiKey:
        showAggregateWarning(parent, QStringLiteral("API Key"),
                             QStringLiteral("请先在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中为该线路（豆包或 "
                                            "DeepSeek）填写并保存 API Key；"
                                            "亦可在 QSettings 的 aggregateAi/apiKey / ai/apiKey 中配置通用 Key。"));
        break;
    case AggregateAiBuildFailure::IncompleteModelConfig:
        showAggregateWarning(parent, QStringLiteral("模型配置"),
                             QStringLiteral("请先在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中补齐并保存 "
                                            "Base URL 与模型名称。"));
        break;
    case AggregateAiBuildFailure::MissingInboundSnapshot:
    case AggregateAiBuildFailure::EmptyInbound:
        showAggregateInfo(parent, QStringLiteral("AI 辅助"),
                          QStringLiteral("暂无可辅助的客户入站消息。请先收到客户消息后再试。"));
        break;
    case AggregateAiBuildFailure::MissingInboundImage:
        showAggregateWarning(parent, QStringLiteral("聊天区截图"),
                             QStringLiteral("本条入站关联的聊天区截图文件不存在或无法读取，可能已被清理。请重新收一条消息或检查路径。"));
        break;
    case AggregateAiBuildFailure::VisionUnsupported:
        showAggregateWarning(
            parent,
            QStringLiteral("多模态模型"),
            QStringLiteral("当前入站含聊天区截图，需要支持图片的模型。请点击下方「模型：…」切换到「豆包」，"
                           "并在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中完成该线路的 "
                           "Base URL、接入点 ID 与 API Key 并保存。"));
        break;
    case AggregateAiBuildFailure::None:
        break;
    }
    return false;
}

} // namespace

AggregateChatForm::AggregateChatForm(const QString& loginUsername, QWidget* parent)
    : QWidget(parent)
    , m_loginUsername(loginUsername)
{
    m_conversationListModel = new ConversationListModel(this);
    m_messageListModel = new MessageListModel(this);
    setupUI();
    m_conversationService = new ConversationAppService();
    m_aiChatService = new AiChatAppService(this);
    setupStyles();
    loadSelfBubbleIdentity();
    connectSignals();
    refreshConversationList();
    restoreLastSelectedConversation();
    QTimer::singleShot(0, this, [this]() {
        auto& ipc = Ipc::IpcService::instance();
        m_pythonServiceAvailable = ipc.isServiceAvailable();
        PythonServiceController::instance().refreshConnectionState();
        refreshPythonServiceButtonUi();
        refreshPlatformListenStateFromService();
        if (m_pythonServiceAvailable && pythonServiceStartupBackfillEnabled())
            backfillFromPythonService();
    });
    scheduleChatInputRelayout();
}

void AggregateChatForm::loadSelfBubbleIdentity()
{
    const LocalUserProfile profile = m_conversationService
                                         ? m_conversationService->loadLocalUserProfile(m_loginUsername)
                                         : LocalUserProfile{m_loginUsername, m_loginUsername, QString()};
    m_selfDisplayName = profile.displayName;

    constexpr int kAvatarLogical = 36;
    const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;

    QPixmap pm;
    if (!profile.avatarAbsolutePath.isEmpty()) {
        if (QFile::exists(profile.avatarAbsolutePath)) {
            const QImage img(profile.avatarAbsolutePath);
            if (!img.isNull()) {
                pm = QPixmap::fromImage(
                    img.scaled(QSize(kAvatarLogical, kAvatarLogical) * dpr, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation));
                pm.setDevicePixelRatio(dpr);
            }
        }
    }
    if (pm.isNull()) {
        QPixmap canvas(QSize(kAvatarLogical, kAvatarLogical) * dpr);
        canvas.setDevicePixelRatio(dpr);
        canvas.fill(Qt::transparent);
        QSvgRenderer renderer(QStringLiteral(":/default_avatar_icon.svg"));
        QPainter painter(&canvas);
        renderer.render(&painter, QRectF(0, 0, canvas.width(), canvas.height()));
        pm = canvas;
    }
    m_selfAvatarPixmap = roundedAggregateAvatarPixmap(pm, kAvatarLogical, dpr, kAvatarLogical / 2);
    if (auto* delegate = static_cast<ModernMessageItemDelegate*>(m_messageItemDelegate))
        delegate->setSelfProfile(m_selfDisplayName, m_selfAvatarPixmap);

    {
        const QString customerRes = QStringLiteral(
            ":/aggregate_reception_icons/customer_avatar_icon.svg");
        QPixmap customerPm;
        {
            QPixmap canvas(QSize(kAvatarLogical, kAvatarLogical) * dpr);
            canvas.setDevicePixelRatio(dpr);
            canvas.fill(Qt::transparent);
            QSvgRenderer renderer(customerRes);
            if (renderer.isValid()) {
                QPainter painter(&canvas);
                renderer.render(&painter, QRectF(0, 0, canvas.width(), canvas.height()));
                customerPm = canvas;
            }
        }
        if (customerPm.isNull()) {
            QPixmap canvas(QSize(kAvatarLogical, kAvatarLogical) * dpr);
            canvas.setDevicePixelRatio(dpr);
            canvas.fill(Qt::transparent);
            QSvgRenderer renderer(QStringLiteral(":/default_avatar_icon.svg"));
            QPainter painter(&canvas);
            renderer.render(&painter, QRectF(0, 0, canvas.width(), canvas.height()));
            customerPm = canvas;
        }
        m_customerDefaultAvatarPixmap =
            roundedAggregateAvatarPixmap(customerPm, kAvatarLogical, dpr, kAvatarLogical / 2);
    }
    if (auto* delegate = static_cast<ModernMessageItemDelegate*>(m_messageItemDelegate))
        delegate->setCustomerAvatar(m_customerDefaultAvatarPixmap);
}

void AggregateChatForm::refreshLocalUserProfile()
{
    loadSelfBubbleIdentity();
    if (m_currentConvId <= 0)
        return;
    const auto messages = ConversationManager::instance().messages(m_currentConvId);
    m_currentMessageSignature = buildMessageSignature(messages);
    renderConversationMessages(messages);
    scheduleScrollChatToBottom();
}

AggregateChatForm::~AggregateChatForm()
{
    shutdownTransientWork();
    delete m_conversationService;
    m_conversationService = nullptr;
}

void AggregateChatForm::setupUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto* body = new QWidget(this);
    auto* bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);

    m_leftToolBar = buildLeftToolBar();
    bodyLay->addWidget(m_leftToolBar, 0);

    m_hSplitter = new QSplitter(Qt::Horizontal, body);
    m_hSplitter->addWidget(buildLeftPanel());
    m_hSplitter->addWidget(buildCenterPanel());
    m_hSplitter->addWidget(buildRightPanel());
    m_hSplitter->setSizes({kAggregateConversationPanelDefaultWidth, 576, 296});
    m_hSplitter->setStretchFactor(0, 0);
    m_hSplitter->setStretchFactor(1, 1);
    m_hSplitter->setStretchFactor(2, 0);
    m_hSplitter->setCollapsible(0, false);
    m_hSplitter->setCollapsible(1, false);
    m_hSplitter->setCollapsible(2, true);
    m_hSplitter->setHandleWidth(1);
    bodyLay->addWidget(m_hSplitter, 1);
    if (m_hSplitter) {
        connect(m_hSplitter, &QSplitter::splitterMoved, this, [this]() {
            if (!m_rightBarHidden) {
                const QList<int> sizes = m_hSplitter->sizes();
                if (sizes.size() >= 3 && sizes[2] > 0)
                    m_lastRightBarWidth = sizes[2];
            }
            scheduleAdaptiveRelayout(true);
        });
    }

    outerLayout->addWidget(body, 1);

    m_messageRefreshTimer = new QTimer(this);
    m_messageRefreshTimer->setInterval(500);
    m_sendTimelineTimer = new QTimer(this);
    m_sendTimelineTimer->setInterval(900);
    m_pythonBackfillTimer = new QTimer(this);
    m_pythonBackfillTimer->setSingleShot(true);
    m_pythonBackfillTimer->setInterval(250);
    connect(m_pythonBackfillTimer, &QTimer::timeout,
            this, &AggregateChatForm::backfillFromPythonService);
    m_adaptiveRelayoutTimer = new QTimer(this);
    m_adaptiveRelayoutTimer->setSingleShot(true);
    m_adaptiveRelayoutTimer->setInterval(16);
    connect(m_adaptiveRelayoutTimer, &QTimer::timeout,
            this, &AggregateChatForm::runScheduledAdaptiveRelayout);
    m_chatInputRelayoutTimer = new QTimer(this);
    m_chatInputRelayoutTimer->setSingleShot(true);
    m_chatInputRelayoutTimer->setInterval(16);
    connect(m_chatInputRelayoutTimer, &QTimer::timeout,
            this, &AggregateChatForm::relayoutChatInputOverlay);
}

void AggregateChatForm::setupStyles()
{
    setStyleSheet(ApplyStyle::aggregateChatFormStyle());
    syncSolidBackgrounds();
}

void AggregateChatForm::backfillFromPythonService()
{
    if (RuntimeMode::isSingleHostServiceDb()) {
        qInfo() << "[AggregateChatForm] python service backfill skipped in single-host unified DB mode";
        return;
    }
    if (!pythonServiceStartupBackfillEnabled()) {
        qInfo() << "[AggregateChatForm] python service startup backfill disabled by settings";
        return;
    }
    if (m_pythonBackfillInProgress)
        return;

    m_pythonBackfillInProgress = true;
    ConversationDao conversationDao;
    const QStringList platforms = {
        QStringLiteral("wechat"),
        QStringLiteral("qianniu"),
        QStringLiteral("pdd_web"),
        QStringLiteral("qq"),
    };
    for (const QString& platform : platforms) {
        Ipc::ResponseStatus replayStatus = Ipc::ResponseStatus::Error;
        QString replayError;
        const QString replayCursor = conversationDao.rpaReplayCursor(platform);
        const QJsonObject replay = Ipc::IpcService::instance().fetchPlatformReplay(
            platform,
            replayCursor,
            200,
            5000,
            &replayStatus,
            &replayError);
        const int replayedEvents = replayStatus == Ipc::ResponseStatus::Success
            ? Ipc::IpcService::instance().dispatchPlatformReplayEvents(replay)
            : 0;
        const QString nextReplayCursor = replay.value(QStringLiteral("cursor")).toString().trimmed();
        if (replayStatus == Ipc::ResponseStatus::Success && !nextReplayCursor.isEmpty())
            conversationDao.setRpaReplayCursor(platform, nextReplayCursor);
        qInfo() << "[AggregateChatForm] platform replay backfill"
                << "platform=" << platform
                << "cursor=" << replayCursor
                << "status=" << Ipc::toString(replayStatus)
                << "error=" << replayError
                << "events=" << replay.value(QStringLiteral("event_count")).toInt()
                << "dispatched=" << replayedEvents
                << "nextCursor=" << nextReplayCursor;

        Ipc::ResponseStatus snapshotStatus = Ipc::ResponseStatus::Error;
        QString snapshotError;
        const QString cursor = conversationDao.snapshotCursor(platform);
        const QJsonObject snapshot = Ipc::IpcService::instance().fetchCacheSnapshot(
            platform,
            100,
            200,
            cursor,
            5000,
            &snapshotStatus,
            &snapshotError);
        qInfo() << "[AggregateChatForm] cache snapshot backfill"
                << "platform=" << platform
                << "cursor=" << cursor
                << "status=" << Ipc::toString(snapshotStatus)
                << "error=" << snapshotError
                << "conversations=" << snapshot.value(QStringLiteral("conversation_count")).toInt()
                << "messages=" << snapshot.value(QStringLiteral("message_count")).toInt();
        if (snapshotStatus == Ipc::ResponseStatus::Success)
            applyCacheSnapshotToLocalCache(snapshot);
    }
    ConversationManager::instance().reloadFromLocalCache();
    reloadFromLocalCache();
    m_pythonBackfillInProgress = false;
}

void AggregateChatForm::schedulePythonServiceBackfill(int delayMs)
{
    if (!RuntimeMode::ownsBusinessDatabase())
        return;
    if (RuntimeMode::isSingleHostServiceDb())
        return;
    if (!m_pythonServiceAvailable)
        return;
    if (!pythonServiceStartupBackfillEnabled())
        return;
    if (!m_pythonBackfillTimer)
        return;
    m_pythonBackfillTimer->start(qMax(0, delayMs));
}

void AggregateChatForm::applyTheme(ApplyStyle::MainWindowTheme theme)
{
    Q_UNUSED(theme)
    setStyleSheet(ApplyStyle::aggregateChatFormStyle());
    syncSolidBackgrounds();
    updateAggregateAiControlsVisibility();
    scheduleChatInputRelayout();
}

void AggregateChatForm::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    scheduleChatInputRelayout();
    scheduleAdaptiveRelayout(false);
}

void AggregateChatForm::closeEvent(QCloseEvent* event)
{
    shutdownTransientWork();
    QWidget::closeEvent(event);
}

void AggregateChatForm::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    scheduleAdaptiveRelayout(false);
}

bool AggregateChatForm::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_chatInputOverlayHost
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        scheduleChatInputRelayout();
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        scheduleAdaptiveRelayout(false);
    }
    return QWidget::eventFilter(watched, event);
}

void AggregateChatForm::setChatHeaderTitle(const QString& title)
{
    if (m_chatHeader)
        m_chatHeader->setText(title);
}

void AggregateChatForm::scheduleAdaptiveRelayout(bool fromUserSplitter)
{
    m_pendingAdaptiveRelayoutFromUserSplitter =
        m_pendingAdaptiveRelayoutFromUserSplitter || fromUserSplitter;
    if (m_adaptiveRelayoutTimer && !m_adaptiveRelayoutTimer->isActive())
        m_adaptiveRelayoutTimer->start();
}

void AggregateChatForm::runScheduledAdaptiveRelayout()
{
    const bool fromUserSplitter = m_pendingAdaptiveRelayoutFromUserSplitter;
    m_pendingAdaptiveRelayoutFromUserSplitter = false;
    updateAdaptiveConversationLayout(fromUserSplitter);
}

void AggregateChatForm::scheduleChatInputRelayout()
{
    if (m_chatInputRelayoutTimer && !m_chatInputRelayoutTimer->isActive())
        m_chatInputRelayoutTimer->start();
}

void AggregateChatForm::updateComposeInputHeight()
{
    if (!m_inputEdit)
        return;

    constexpr int kMinLines = 2;
    constexpr int kMaxLines = 8;
    constexpr int kVerticalPadding = 12;
    const QFontMetrics fm(m_inputEdit->font());
    const int minH = fm.lineSpacing() * kMinLines + kVerticalPadding;
    const int maxH = fm.lineSpacing() * kMaxLines + kVerticalPadding;

    int textWidth = m_inputEdit->viewport() ? m_inputEdit->viewport()->width() : m_inputEdit->width();
    if (textWidth <= 0)
        textWidth = m_inputEdit->width();
    if (textWidth > 0)
        m_inputEdit->document()->setTextWidth(textWidth);

    const int contentH = m_inputEdit->toPlainText().isEmpty()
                             ? fm.lineSpacing()
                             : int(m_inputEdit->document()->size().height() + 0.5);
    const int wantedH = contentH + kVerticalPadding;
    const int nextH = qBound(minH, wantedH, maxH);

    m_inputEdit->setVerticalScrollBarPolicy(wantedH > maxH ? Qt::ScrollBarAsNeeded
                                                           : Qt::ScrollBarAlwaysOff);
    if (m_inputEdit->height() != nextH) {
        m_inputEdit->setFixedHeight(nextH);
        m_inputEdit->updateGeometry();
    }
}

AggregateAdaptiveLayoutMode AggregateChatForm::desiredAdaptiveLayoutMode() const
{
    if (!m_hSplitter)
        return AggregateAdaptiveLayoutMode::Unknown;
    const int totalWidth = m_hSplitter->width();
    if (totalWidth >= kAggregateConversationPanelShowBreakpoint)
        return AggregateAdaptiveLayoutMode::Wide;
    if (totalWidth >= kAggregateRightPanelAutoHideBreakpoint)
        return AggregateAdaptiveLayoutMode::Medium;
    return AggregateAdaptiveLayoutMode::Compact;
}

void AggregateChatForm::updateAdaptiveConversationLayout(bool fromUserSplitter)
{
    if (!m_hSplitter || !m_leftPanel)
        return;

    const AggregateAdaptiveLayoutMode desiredMode = desiredAdaptiveLayoutMode();
    if (desiredMode == AggregateAdaptiveLayoutMode::Unknown)
        return;

    const bool modeChanged = desiredMode != m_adaptiveLayoutMode;
    m_adaptiveLayoutMode = desiredMode;

    updateRightBarAdaptiveLayout();

    Q_UNUSED(fromUserSplitter)
    Q_UNUSED(modeChanged)
    m_compactConversationListForced = false;
    if (m_centerPanel && !m_centerPanel->isVisible())
        m_centerPanel->show();
    if (m_leftPanelHidden)
        setConversationPanelHidden(false);
    updateCompactHeaderControls();
}

void AggregateChatForm::updateRightBarAdaptiveLayout()
{
    if (!m_hSplitter || !m_rightPanel)
        return;

    if (m_rightBarMetricsGrid) {
        const int panelWidth = m_rightPanel->width();
        const bool singleColumn = panelWidth > 0 && panelWidth < kAggregateMetricSingleColumnBreakpoint;
        const int columns = singleColumn ? 1 : 2;
        if (columns != m_rightBarMetricColumnCount) {
            m_rightBarMetricColumnCount = columns;
            for (int i = 0; i < 4; ++i) {
                if (!m_rightBarMetricCards[i])
                    continue;
                m_rightBarMetricsGrid->removeWidget(m_rightBarMetricCards[i]);
                m_rightBarMetricsGrid->addWidget(m_rightBarMetricCards[i], i / columns, i % columns);
            }
            m_rightBarMetricsGrid->setColumnStretch(0, 1);
            m_rightBarMetricsGrid->setColumnStretch(1, singleColumn ? 0 : 1);
            if (m_rightBarMetricsWrap)
                m_rightBarMetricsWrap->updateGeometry();
        }
    }

    const QList<int> sizes = m_hSplitter->sizes();
    if (sizes.size() < 3)
        return;

    if (!m_rightBarHidden) {
        const int centerWidth = sizes[1];
        const int rightWidth = sizes[2];
        if (rightWidth > 0
            && (m_hSplitter->width() < kAggregateRightPanelAutoHideBreakpoint
                || centerWidth < kAggregateChatWithRightMinWidth)) {
            m_rightBarAutoHidden = true;
            setRightBarHidden(true);
            return;
        }
    }

    if (m_rightBarAutoHidden
        && m_rightBarHidden
        && m_hSplitter->width() >= kAggregateConversationPanelShowBreakpoint + kAggregateRightPanelPreferredMinWidth) {
        m_rightBarAutoHidden = false;
        setRightBarHidden(false);
    }
}

void AggregateChatForm::setConversationPanelHidden(bool hidden)
{
    if (!m_hSplitter || !m_leftPanel || !m_centerPanel)
        return;
    if (m_leftPanelHidden == hidden) {
        updateCompactHeaderControls();
        return;
    }

    const QList<int> sizes = m_hSplitter->sizes();
    const int leftWidth = sizes.value(0, m_lastLeftPanelWidth);
    const int centerWidth = qMax(240, sizes.value(1, 360));
    int rightWidth = qMax(0, sizes.value(2, 0));

    if (hidden) {
        if (leftWidth > 24)
            m_lastLeftPanelWidth = qBound(kAggregateConversationPanelMinWidth,
                                          leftWidth,
                                          kAggregateConversationPanelMaxWidth);
        m_compactConversationListForced = false;
        m_leftPanelHidden = true;
        m_centerPanel->show();
        m_leftPanel->hide();
        m_hSplitter->setSizes({0, centerWidth + qMax(0, leftWidth), rightWidth});
    } else {
        if (m_centerPanel && !m_centerPanel->isVisible())
            m_centerPanel->show();
        const int totalWidth = qMax(m_hSplitter->width(), centerWidth + rightWidth + m_lastLeftPanelWidth);
        const int restoreLeft = qBound(kAggregateConversationPanelMinWidth,
                                       m_lastLeftPanelWidth,
                                       kAggregateConversationPanelMaxWidth);
        if (totalWidth - restoreLeft - rightWidth < 320)
            rightWidth = qMax(0, totalWidth - restoreLeft - 320);
        const int restoreCenter = qMax(240, totalWidth - restoreLeft - rightWidth);
        m_leftPanelHidden = false;
        m_leftPanel->show();
        m_hSplitter->setSizes({restoreLeft, restoreCenter, rightWidth});
    }

    updateCompactHeaderControls();
    scheduleChatInputRelayout();
}

void AggregateChatForm::updateCompactHeaderControls()
{
    if (!m_btnBackToConversationList)
        return;
    m_btnBackToConversationList->setVisible(false);
}

void AggregateChatForm::setRightBarHidden(bool hidden)
{
    if (!m_hSplitter)
        return;
    QList<int> sizes = m_hSplitter->sizes();
    if (sizes.size() < 3)
        return;

    const int leftWidth = qMax(0, sizes[0]);
    const int centerWidth = qMax(240, sizes[1]);
    const int rightWidth = qMax(0, sizes[2]);
    if (hidden) {
        if (rightWidth > 24)
            m_lastRightBarWidth = rightWidth;
        m_rightBarHidden = true;
        m_hSplitter->setSizes({leftWidth, centerWidth + rightWidth, 0});
    } else {
        const int totalWidth = qMax(leftWidth + centerWidth + rightWidth, m_hSplitter->width());
        const int availableForCenterAndRight = qMax(0, totalWidth - leftWidth);
        if (availableForCenterAndRight < kAggregateChatWithRightMinWidth + kAggregateRightPanelMinWidth) {
            m_rightBarHidden = true;
            m_rightBarAutoHidden = true;
            showStatusMessage(QStringLiteral("窗口宽度不足，右栏已保持隐藏"), 3000);
            scheduleChatInputRelayout();
            return;
        }
        int restoreRight = qBound(kAggregateRightPanelMinWidth,
                                  m_lastRightBarWidth,
                                  qMax(kAggregateRightPanelMinWidth,
                                       availableForCenterAndRight - kAggregateChatWithRightMinWidth));
        restoreRight = qMin(restoreRight, kAggregateRightPanelMaxWidth);
        const int restoreCenter = qMax(kAggregateChatWithRightMinWidth,
                                       availableForCenterAndRight - restoreRight);
        m_rightBarHidden = false;
        m_rightBarAutoHidden = false;
        m_hSplitter->setSizes({leftWidth, restoreCenter, restoreRight});
    }

    scheduleChatInputRelayout();
}

void AggregateChatForm::relayoutChatInputOverlay()
{
    if (!m_chatInputOverlayHost || !m_messageView || !m_chatInputPanel)
        return;
    const int w = m_chatInputOverlayHost->width();
    const int h = m_chatInputOverlayHost->height();
    if (w <= 0 || h <= 0)
        return;

    m_chatInputPanel->setFixedWidth(w);
    updateComposeInputHeight();
    m_chatInputPanel->adjustSize();
    int ih = m_chatInputPanel->sizeHint().height();
    ih = qBound(72, ih, qMax(72, h - 16));

    m_messageView->setGeometry(0, 0, w, h);
    m_chatInputPanel->setGeometry(0, h - ih, w, ih);
    m_chatInputPanel->raise();
    updateNewMessageHintGeometry();
    if (m_btnNewMessages)
        m_btnNewMessages->raise();
    updateMessageListBottomReserve(ih);
    m_chatInputOverlayHost->update();
}

void AggregateChatForm::updateMessageListBottomReserve(int overlayBottomPx)
{
    if (!m_messageView)
        return;
    const int extra = qMax(0, overlayBottomPx);
    static_cast<MessageListView*>(m_messageView)->setBottomReserve(extra);
}

void AggregateChatForm::connectSignals()
{
    auto& mgr = ConversationManager::instance();

    connect(&mgr, &ConversationManager::conversationListChanged,
            this, &AggregateChatForm::onConversationListChanged);
    connect(&mgr, &ConversationManager::unifiedMessageReceived,
            this, &AggregateChatForm::onUnifiedMessageReceived);
    connect(&mgr, &ConversationManager::unifiedConversationUpdated,
            this, &AggregateChatForm::onUnifiedConversationUpdated);
    connect(&mgr, &ConversationManager::conversationMessagesCleared,
            this, &AggregateChatForm::onConversationMessagesCleared);
    connect(&mgr, &ConversationManager::conversationDeleted,
            this, &AggregateChatForm::onConversationDeleted);
    connect(&mgr, &ConversationManager::messageSendFailed,
            this, [this](int convId, const QString& reason) {
                Q_UNUSED(convId)
                showStatusMessage(QStringLiteral("发送失败: %1").arg(reason), 5000);
            });

    connect(&mgr, &ConversationManager::messageStatusChanged,
            this, &AggregateChatForm::onMessageStatusChanged);

    auto& ipc = Ipc::IpcService::instance();
    connect(&ipc, &Ipc::IpcService::aiSuggestionReceived,
            this, &AggregateChatForm::onIpcAiSuggestionReceived);
    connect(&ipc, &Ipc::IpcService::requestFailed,
            this, &AggregateChatForm::onIpcRequestFailed);
    connect(&ipc, &Ipc::IpcService::serviceStatusChanged, this, [this](bool available) {
        m_pythonServiceAvailable = available;
        PythonServiceController::instance().refreshConnectionState();
        refreshPythonServiceButtonUi();
        refreshPlatformListenStateFromService();
        updateWechatHistorySyncButtonUi();
        if (available && !RuntimeMode::isSingleHostServiceDb() && pythonServiceStartupBackfillEnabled())
            backfillFromPythonService();
    });
    connect(&ipc, &Ipc::IpcService::platformEventReceived, this, [this](const QJsonObject& event) {
        const QString eventType = event.value(QStringLiteral("event_type")).toString();
        if (eventType == QLatin1String("history_sync_completed")) {
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            const int inserted = payload.value(QStringLiteral("inserted_count")).toInt();
            const int duplicates = payload.value(QStringLiteral("duplicate_count")).toInt();
            m_wechatHistorySyncInProgress = false;
            updateWechatHistorySyncButtonUi();
            showStatusMessage(QStringLiteral("历史同步完成：新增 %1 条，跳过 %2 条").arg(inserted).arg(duplicates), 5000);
            if (RuntimeMode::isSingleHostServiceDb())
                reloadFromLocalCache();
            else
                refreshVisibleConversationMessages();
            return;
        }
        const bool changesConversationData =
            eventType == QLatin1String("conversation_observed")
            || eventType == QLatin1String("message_observed")
            || eventType == QLatin1String("message_sent")
            || eventType == QLatin1String("send_failed")
            || eventType == QLatin1String("conversation_messages_cleared")
            || eventType == QLatin1String("conversation_deleted");
        if (RuntimeMode::isSingleHostServiceDb() && changesConversationData) {
            QTimer::singleShot(0, this, [this]() {
                reloadFromLocalCache();
            });
            return;
        }
        if (eventType == QLatin1String("message_observed")
            || eventType == QLatin1String("message_sent")
            || eventType == QLatin1String("send_failed")) {
            schedulePythonServiceBackfill(200);
        }
    });
    connect(&PythonServiceController::instance(), &PythonServiceController::stateChanged,
            this, [this]() {
                refreshPythonServiceButtonUi();
            });

    auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(shortcut, &QShortcut::activated, this, &AggregateChatForm::onSendClicked);
    connect(m_messageRefreshTimer, &QTimer::timeout,
            this, &AggregateChatForm::refreshVisibleConversationMessages);
    m_messageRefreshTimer->start();
    connect(m_btnClearSendTimeline, &QToolButton::clicked,
            this, &AggregateChatForm::onClearSendTimeline);
    if (m_btnModelPickerBack)
        connect(m_btnModelPickerBack, &QToolButton::clicked, this, &AggregateChatForm::onModelPickerBackClicked);
    if (m_modelPickerList)
        connect(m_modelPickerList, &QListWidget::itemClicked, this, &AggregateChatForm::onModelPickerListItem);
    connect(m_sendTimelineTimer, &QTimer::timeout,
            this, &AggregateChatForm::pollSendTimeline);
    m_sendTimelineTimer->start();

}

// ===================== Left tool bar =====================

QWidget* AggregateChatForm::buildLeftToolBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("aggregateLeftToolBar"));
    bar->setFixedWidth(52);
    bar->setAutoFillBackground(true);
    auto* lay = new QVBoxLayout(bar);
    lay->setContentsMargins(6, 14, 6, 14);
    lay->setSpacing(8);

    m_platformButtonGroup = new QButtonGroup(this);
    m_platformButtonGroup->setExclusive(true);

    const struct {
        const char* icon;
        const char* tip;
    } kItems[] = {
        { ":/aggregate_reception_icons/message_center_selected_icon.svg", "消息中心" },
        { ":/aggregate_reception_icons/qianniu_logo_selected_icon.svg", "千牛" },
        { ":/aggregate_reception_icons/pdd_logo_selected_icon.svg", "拼多多" },
        { ":/aggregate_reception_icons/doudian_logo_selected_icon.svg", "抖店" },
        { ":/aggregate_reception_icons/wechat_logo_selected_icon.svg", "微信" },
        { ":/aggregate_reception_icons/qq_logo_icon.svg", "QQ" },
    };
    const int itemCount = int(sizeof(kItems) / sizeof(kItems[0]));
    for (int i = 0; i < itemCount; ++i) {
        auto* btn = new QToolButton(bar);
        btn->setObjectName(QStringLiteral("aggregateToolBarButton"));
        btn->setCheckable(true);
        btn->setIcon(QIcon(QString::fromUtf8(kItems[i].icon)));
        btn->setIconSize(QSize(28, 28));
        btn->setFixedSize(40, 40);
        btn->setAutoRaise(true);
        const QString baseTip = QString::fromUtf8(kItems[i].tip);
        btn->setProperty("baseTip", baseTip);
        btn->setToolTip(baseTip);
        btn->setCursor(Qt::PointingHandCursor);
        const QString platformId = aggregatePlatformIdForButtonId(i);
        if (!platformId.isEmpty()) {
            btn->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(btn, &QToolButton::customContextMenuRequested, this,
                    [this, btn, platformId](const QPoint& pos) {
                showRobotBindingMenu(platformId, btn, pos);
            });
        }
        m_platformButtonGroup->addButton(btn, i);
        lay->addWidget(btn, 0, Qt::AlignHCenter);
    }
    if (QAbstractButton* first = m_platformButtonGroup->button(0))
        first->setChecked(true);
    m_platformFilter = AggregatePlatformFilter::All;
    connect(m_platformButtonGroup, &QButtonGroup::idClicked, this,
            &AggregateChatForm::onPlatformFilterButtonIdClicked);
    updatePlatformToolBarButtonIcons();

    m_btnPythonService = new QToolButton(bar);
    m_btnPythonService->setObjectName(QStringLiteral("aggregateToolBarButton"));
    m_btnPythonService->setCheckable(true);
    m_btnPythonService->setIcon(QIcon(QStringLiteral(":/aggregate_reception_icons/runtime_icon.svg")));
    m_btnPythonService->setIconSize(QSize(24, 24));
    m_btnPythonService->setFixedSize(40, 40);
    m_btnPythonService->setAutoRaise(true);
    m_btnPythonService->setCursor(Qt::PointingHandCursor);
    m_btnPythonService->setContextMenuPolicy(Qt::CustomContextMenu);
    m_btnPythonService->setAccessibleName(QStringLiteral("Python 服务"));
    connect(m_btnPythonService, &QToolButton::clicked,
            this, &AggregateChatForm::onPythonServiceButtonClicked);
    connect(m_btnPythonService, &QToolButton::customContextMenuRequested,
            this, &AggregateChatForm::onPythonServiceButtonContextMenu);
    lay->addWidget(m_btnPythonService, 0, Qt::AlignHCenter);

    m_btnSimulateMessage = new QToolButton(bar);
    m_btnSimulateMessage->setObjectName(QStringLiteral("aggregateToolBarButton"));
    m_btnSimulateMessage->setText(QStringLiteral("测"));
    m_btnSimulateMessage->setToolTip(QStringLiteral("模拟一条随机平台买家消息"));
    m_btnSimulateMessage->setAccessibleName(QStringLiteral("模拟买家消息"));
    m_btnSimulateMessage->setFixedSize(40, 40);
    m_btnSimulateMessage->setAutoRaise(true);
    m_btnSimulateMessage->setCursor(Qt::PointingHandCursor);
    connect(m_btnSimulateMessage, &QToolButton::clicked,
            this, &AggregateChatForm::onSimulateMessageClicked);
    lay->addWidget(m_btnSimulateMessage, 0, Qt::AlignHCenter);

    lay->addStretch(1);
    refreshPythonServiceButtonUi();
    return bar;
}

void AggregateChatForm::updatePlatformToolBarButtonIcons()
{
    if (!m_platformButtonGroup)
        return;
    static const char* kIcons[] = {
        ":/aggregate_reception_icons/message_center_selected_icon.svg",
        ":/aggregate_reception_icons/qianniu_logo_selected_icon.svg",
        ":/aggregate_reception_icons/pdd_logo_selected_icon.svg",
        ":/aggregate_reception_icons/doudian_logo_selected_icon.svg",
        ":/aggregate_reception_icons/wechat_logo_selected_icon.svg",
        ":/aggregate_reception_icons/qq_logo_icon.svg",
    };
    const int iconCount = int(sizeof(kIcons) / sizeof(kIcons[0]));
    for (int i = 0; i < iconCount; ++i) {
        auto* b = qobject_cast<QToolButton*>(m_platformButtonGroup->button(i));
        if (!b)
            continue;
        b->setIcon(QIcon(QString::fromUtf8(kIcons[i])));
        const QString platformId = aggregatePlatformIdForButtonId(i);
        const QString baseTip = b->property("baseTip").toString();
        if (!platformId.isEmpty() && !baseTip.isEmpty())
            b->setToolTip(aggregatePlatformRobotTooltip(baseTip, platformId));
    }
}

void AggregateChatForm::onPlatformFilterButtonIdClicked(int id)
{
    m_platformFilter = static_cast<AggregatePlatformFilter>(id);
    updatePlatformToolBarButtonIcons();
    updatePlatformSectionTitle();
    refreshConversationList();
}

void AggregateChatForm::showRobotBindingMenu(const QString& platform, QWidget* button, const QPoint& pos)
{
    if (platform.trimmed().isEmpty() || !button)
        return;

    QMenu menu(this);
    const QString displayName = listenPlatformDisplayName(platform);
    QAction* bindAction = menu.addAction(QStringLiteral("绑定机器人..."));
    QAction* viewAction = menu.addAction(QStringLiteral("查看当前机器人"));
    QAction* clearAction = menu.addAction(QStringLiteral("解绑机器人"));
    QAction* chosen = menu.exec(button->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == bindAction) {
        openRobotBindingDialog(platform);
        return;
    }
    if (chosen == clearAction) {
        clearRobotBindingForPlatform(platform);
        return;
    }
    if (chosen == viewAction) {
        const QString robotId = boundRobotIdForAggregatePlatform(platform);
        if (robotId.isEmpty()) {
            showStatusMessage(QStringLiteral("%1未绑定机器人。").arg(displayName), 3500);
            return;
        }
        const QJsonObject robot = aggregateRobotById(robotId);
        if (robot.isEmpty()) {
            showStatusMessage(QStringLiteral("%1绑定的机器人已失效：%2").arg(displayName, robotId), 5000);
            return;
        }
        showStatusMessage(QStringLiteral("%1当前机器人：%2；模型：%3；知识库：%4。")
                              .arg(displayName,
                                   aggregateRobotDisplayName(robot),
                                   aggregateRobotModelLabel(robot),
                                   aggregateRobotKnowledgeLabel(robot)),
                          7000);
    }
}

void AggregateChatForm::openRobotBindingDialog(const QString& platform)
{
    const QString normalizedPlatform = platform.trimmed().toLower();
    if (normalizedPlatform.isEmpty())
        return;

    const QJsonArray robots = loadAggregateRobotConfigs();
    const QString currentRobotId = boundRobotIdForAggregatePlatform(normalizedPlatform);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 - 绑定机器人").arg(listenPlatformDisplayName(normalizedPlatform)));
    dialog.resize(760, 520);
    dialog.setModal(true);
    auto* outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(10);

    auto* hint = new QLabel(QStringLiteral("一个平台同一时间只能绑定一个机器人。机器人包含模型、知识库和启用状态；阶段二只保存绑定关系，生成回复链路将在下一阶段接入。"), &dialog);
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setAlternatingRowColors(true);
    list->setWordWrap(true);
    outer->addWidget(list, 1);

    for (const QJsonValue& value : robots) {
        const QJsonObject robot = value.toObject();
        const QString robotId = robot.value(QStringLiteral("robot_id")).toString().trimmed();
        if (robotId.isEmpty())
            continue;
        const QString status = aggregateRobotEnabled(robot) ? QStringLiteral("启用") : QStringLiteral("停用");
        const QString text = QStringLiteral("%1\n模型：%2    知识库：%3    状态：%4")
                                 .arg(aggregateRobotDisplayName(robot),
                                      aggregateRobotModelLabel(robot),
                                      aggregateRobotKnowledgeLabel(robot),
                                      status);
        auto* item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, robotId);
        item->setToolTip(text);
        if (robotId == currentRobotId) {
            item->setSelected(true);
            list->setCurrentItem(item);
        }
    }
    if (list->count() == 0)
        hint->setText(QStringLiteral("暂无机器人。请先到 AI 客服后台的“店铺机器人配置”中新建机器人。"));

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    auto* clearBtn = new QPushButton(QStringLiteral("解绑机器人"), &dialog);
    auto* saveBtn = new QPushButton(QStringLiteral("保存"), &dialog);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), &dialog);
    buttonRow->addWidget(clearBtn);
    buttonRow->addWidget(saveBtn);
    buttonRow->addWidget(cancelBtn);
    outer->addLayout(buttonRow);

    auto saveBinding = [&](const QString& robotId) -> bool {
        saveAggregatePlatformRobotBinding(normalizedPlatform, robotId);
        updatePlatformToolBarButtonIcons();
        if (robotId.trimmed().isEmpty()) {
            showStatusMessage(QStringLiteral("%1已解绑机器人。").arg(listenPlatformDisplayName(normalizedPlatform)),
                              3500);
        } else {
            const QJsonObject robot = aggregateRobotById(robotId);
            showStatusMessage(QStringLiteral("%1已绑定机器人：%2。")
                                  .arg(listenPlatformDisplayName(normalizedPlatform),
                                       robot.isEmpty() ? robotId : aggregateRobotDisplayName(robot)),
                              4500);
        }
        return true;
    };

    connect(saveBtn, &QPushButton::clicked, &dialog, [&]() {
        QListWidgetItem* item = list->currentItem();
        if (!item) {
            QMessageBox::information(&dialog, QStringLiteral("绑定机器人"),
                                     QStringLiteral("请选择一个机器人，或点击“解绑机器人”。"));
            return;
        }
        if (saveBinding(item->data(Qt::UserRole).toString()))
            dialog.accept();
    });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem* item) {
        if (item && saveBinding(item->data(Qt::UserRole).toString()))
            dialog.accept();
    });
    connect(clearBtn, &QPushButton::clicked, &dialog, [&]() {
        if (saveBinding({}))
            dialog.accept();
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void AggregateChatForm::clearRobotBindingForPlatform(const QString& platform)
{
    const QString normalizedPlatform = platform.trimmed().toLower();
    if (normalizedPlatform.isEmpty())
        return;
    saveAggregatePlatformRobotBinding(normalizedPlatform, {});
    updatePlatformToolBarButtonIcons();
    showStatusMessage(QStringLiteral("%1已解绑机器人。").arg(listenPlatformDisplayName(normalizedPlatform)), 3500);
}

void AggregateChatForm::onSimulateMessageClicked()
{
    auto* router = ConversationManager::instance().router();
    if (!router) {
        showStatusMessage(QStringLiteral("模拟失败：消息路由未初始化"), 4000);
        return;
    }

    auto* adapter = qobject_cast<SimPlatformAdapter*>(router->adapter(QStringLiteral("simulator")));
    if (!adapter) {
        showStatusMessage(QStringLiteral("模拟失败：模拟平台未就绪"), 4000);
        return;
    }

    adapter->simulateRandomPlatformIncomingMessage();
    showStatusMessage(QStringLiteral("已模拟一条买家消息"), 2500);
}

void AggregateChatForm::refreshPythonServiceButtonUi()
{
    if (!m_btnPythonService)
        return;

    auto& controller = PythonServiceController::instance();
    const bool available = Ipc::IpcService::instance().isServiceAvailable();
    const bool active = available || controller.isManagedServiceRunning()
                        || controller.state() == PythonServiceController::State::ExternalRunning;
    m_btnPythonService->setChecked(active);
    m_btnPythonService->setEnabled(!controller.isBusy());

    QString tip = controller.stateText();
    if (controller.state() == PythonServiceController::State::ExternalRunning) {
        tip += QStringLiteral("\n左键：尝试停止（外部服务不会被强制关闭）\n右键：查看服务状态和运行日志");
    } else if (active) {
        tip += QStringLiteral("\n左键：停止由当前客户端启动的服务\n右键：查看服务状态和运行日志");
    } else {
        tip += QStringLiteral("\n左键：启动本机 Python 服务（debug 模式）\n右键：查看服务状态和运行日志");
    }
    m_btnPythonService->setToolTip(tip);
    updateWechatHistorySyncButtonUi();
}

bool AggregateChatForm::currentConversationIsWechat() const
{
    if (m_currentConvId <= 0)
        return false;
    ConversationDao dao;
    const auto conv = dao.findById(m_currentConvId);
    return conv && conv->platform == QLatin1String("wechat");
}

void AggregateChatForm::updateWechatHistorySyncButtonUi()
{
    if (!m_btnSyncWechatHistory)
        return;
    const bool visible = currentConversationIsWechat();
    const bool enabled = visible && m_pythonServiceAvailable && !m_wechatHistorySyncInProgress;
    m_btnSyncWechatHistory->setVisible(visible);
    m_btnSyncWechatHistory->setEnabled(enabled);
    if (!visible)
        m_btnSyncWechatHistory->setToolTip(QStringLiteral("同步当前会话最近历史"));
    else if (m_wechatHistorySyncInProgress)
        m_btnSyncWechatHistory->setToolTip(QStringLiteral("正在同步当前会话最近历史"));
    else if (!m_pythonServiceAvailable)
        m_btnSyncWechatHistory->setToolTip(QStringLiteral("Python 服务未连接，无法同步历史"));
    else
        m_btnSyncWechatHistory->setToolTip(QStringLiteral("同步当前会话最近历史"));
}

void AggregateChatForm::onSyncWechatHistoryClicked()
{
    if (m_currentConvId <= 0 || m_wechatHistorySyncInProgress)
        return;
    ConversationDao dao;
    const auto conv = dao.findById(m_currentConvId);
    if (!conv || conv->platform != QLatin1String("wechat")) {
        showStatusMessage(QStringLiteral("仅支持同步微信会话历史"), 3000);
        return;
    }
    QString serviceError;
    if (!Ipc::IpcService::instance().connectToConfiguredService(&serviceError)) {
        showStatusMessage(QStringLiteral("Python 服务未连接：%1").arg(serviceError), 5000);
        return;
    }

    bool ok = false;
    const int limit = QInputDialog::getInt(
        this,
        QStringLiteral("同步当前会话最近历史"),
        QStringLiteral("需要同步的最近消息条数（最多 50 条）："),
        20,
        1,
        50,
        1,
        &ok);
    if (!ok)
        return;

    m_wechatHistorySyncInProgress = true;
    updateWechatHistorySyncButtonUi();
    showStatusMessage(QStringLiteral("正在同步当前会话最近历史"), 3000);

    Ipc::PlatformCommandRequest request;
    request.commandType = QStringLiteral("sync_history_messages");
    request.platform = QStringLiteral("wechat");
    request.accountId = conv->accountId.isEmpty() ? QStringLiteral("wechat") : conv->accountId;
    request.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    request.parameters.insert(QStringLiteral("conversation_key"), conv->platformConversationId);
    request.parameters.insert(QStringLiteral("display_name"), conv->customerName);
    request.parameters.insert(QStringLiteral("limit"), limit);
    request.parameters.insert(QStringLiteral("max_scrolls"), 10);
    request.parameters.insert(QStringLiteral("settle_ms"), 500);
    request.parameters.insert(QStringLiteral("restore_bottom"), true);
    request.parameters.insert(QStringLiteral("allow_foreground"), true);

    const auto response = Ipc::IpcService::instance().sendPlatformCommandViaWebSocket(request, 35000);
    m_wechatHistorySyncInProgress = false;
    updateWechatHistorySyncButtonUi();
    if (response.status != Ipc::ResponseStatus::Success) {
        showStatusMessage(QStringLiteral("历史同步失败：%1").arg(response.errorMessage), 5000);
        return;
    }
    const QJsonObject result = response.result;
    const int inserted = result.value(QStringLiteral("inserted_count")).toInt();
    const int duplicates = result.value(QStringLiteral("duplicate_count")).toInt();
    showStatusMessage(QStringLiteral("历史同步完成：新增 %1 条，跳过 %2 条").arg(inserted).arg(duplicates), 5000);
    if (RuntimeMode::isSingleHostServiceDb())
        reloadFromLocalCache();
    else
        refreshVisibleConversationMessages();
}

void AggregateChatForm::onPythonServiceButtonClicked()
{
    auto& controller = PythonServiceController::instance();
    if (controller.isManagedServiceRunning()
        || controller.state() == PythonServiceController::State::ExternalRunning) {
        const bool external = controller.state() == PythonServiceController::State::ExternalRunning
                              && !controller.isManagedServiceRunning();
        QTimer::singleShot(0, this, []() {
            PythonServiceController::instance().stopService();
        });
        showStatusMessage(external
                              ? QStringLiteral("外部启动的 Python 服务不会被客户端强制关闭")
                              : QStringLiteral("已请求停止 Python 服务"),
                          3000);
    } else {
        QTimer::singleShot(0, this, []() {
            PythonServiceController::instance().startService();
        });
        showStatusMessage(QStringLiteral("正在启动 Python 服务"), 3000);
    }
    refreshPythonServiceButtonUi();
}

void AggregateChatForm::onPythonServiceButtonContextMenu(const QPoint& pos)
{
    Q_UNUSED(pos)
    PythonServiceStatusDialog dlg(this);
    dlg.exec();
}

QStringList AggregateChatForm::selectedPlatformListenTargets() const
{
    QStringList platforms;
    if (m_chkListenWechat && m_chkListenWechat->isChecked())
        platforms.append(QStringLiteral("wechat"));
    if (m_chkListenQianniu && m_chkListenQianniu->isChecked())
        platforms.append(QStringLiteral("qianniu"));
    if (m_chkListenPdd && m_chkListenPdd->isChecked())
        platforms.append(QStringLiteral("pdd_web"));
    if (m_chkListenQQ && m_chkListenQQ->isChecked())
        platforms.append(QStringLiteral("qq"));
    return platforms;
}

void AggregateChatForm::updatePlatformListenStatusLabel()
{
    if (!m_platformListenStatusLabel)
        return;

    if (!m_pythonServiceAvailable) {
        m_platformListenStatusLabel->setText(QStringLiteral("Python 服务未连接"));
        return;
    }

    QStringList listening;
    if (m_serviceListeningPlatforms.contains(QStringLiteral("wechat"))
        || ConversationManager::instance().isPlatformListening(QStringLiteral("wechat"))) {
        listening.append(listenPlatformDisplayName(QStringLiteral("wechat")));
    }
    if (m_serviceListeningPlatforms.contains(QStringLiteral("qianniu"))
        || ConversationManager::instance().isPlatformListening(QStringLiteral("qianniu"))) {
        listening.append(listenPlatformDisplayName(QStringLiteral("qianniu")));
    }
    if (m_serviceListeningPlatforms.contains(QStringLiteral("pdd_web"))
        || ConversationManager::instance().isPlatformListening(QStringLiteral("pdd_web"))) {
        listening.append(listenPlatformDisplayName(QStringLiteral("pdd_web")));
    }
    if (m_serviceListeningPlatforms.contains(QStringLiteral("qq"))
        || ConversationManager::instance().isPlatformListening(QStringLiteral("qq"))) {
        listening.append(listenPlatformDisplayName(QStringLiteral("qq")));
    }
    if (m_registeredListenPlatforms.isEmpty()) {
        m_platformListenStatusLabel->setText(QStringLiteral("暂无已注册平台"));
        return;
    }
    m_platformListenStatusLabel->setText(
        listening.isEmpty()
            ? QStringLiteral("未监听平台")
            : QStringLiteral("监听中：%1").arg(listening.join(QStringLiteral("、"))));
}

void AggregateChatForm::setPlatformListenControlsEnabled(bool enabled)
{
    if (m_chkListenWechat)
        m_chkListenWechat->setEnabled(enabled && m_registeredListenPlatforms.contains(QStringLiteral("wechat")));
    if (m_chkListenQianniu)
        m_chkListenQianniu->setEnabled(enabled && m_registeredListenPlatforms.contains(QStringLiteral("qianniu")));
    if (m_chkListenPdd)
        m_chkListenPdd->setEnabled(enabled && m_registeredListenPlatforms.contains(QStringLiteral("pdd_web")));
    if (m_chkListenQQ)
        m_chkListenQQ->setEnabled(enabled && m_registeredListenPlatforms.contains(QStringLiteral("qq")));
    if (m_btnStartPlatformListening)
        m_btnStartPlatformListening->setEnabled(enabled);
    if (m_btnStopPlatformListening)
        m_btnStopPlatformListening->setEnabled(enabled);
}

void AggregateChatForm::refreshPlatformListenStateFromService()
{
    auto& ipc = Ipc::IpcService::instance();
    m_pythonServiceAvailable = ipc.isServiceAvailable();
    if (!m_pythonServiceAvailable) {
        m_registeredListenPlatforms.clear();
        m_serviceListeningPlatforms.clear();
        setPlatformListenControlsEnabled(false);
        updatePlatformListenStatusLabel();
        return;
    }

    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    QString error;
    const QJsonObject response = ipc.fetchPlatformStatuses(3000, &status, &error);
    if (status == Ipc::ResponseStatus::Success
        && response.value(QStringLiteral("status")).toString() == QLatin1String("success")) {
        m_registeredListenPlatforms.clear();
        m_serviceListeningPlatforms.clear();
        const QJsonArray platforms = response.value(QStringLiteral("platforms")).toArray();
        for (const QJsonValue& value : platforms) {
            const QJsonObject item = value.toObject();
            const QString platform = item.value(QStringLiteral("platform")).toString().trimmed().toLower();
            if (platform.isEmpty())
                continue;
            if (item.value(QStringLiteral("registered")).toBool(false))
                m_registeredListenPlatforms.insert(platform);
            if (item.value(QStringLiteral("listening")).toBool(false))
                m_serviceListeningPlatforms.insert(platform);
        }
    } else {
        qWarning() << "[AggregateChatForm] fetch platform statuses failed"
                   << Ipc::toString(status) << error;
        m_registeredListenPlatforms = {
            QStringLiteral("wechat"),
            QStringLiteral("qianniu"),
            QStringLiteral("pdd_web"),
            QStringLiteral("qq"),
        };
        m_serviceListeningPlatforms.clear();
    }

    setPlatformListenControlsEnabled(true);
    updatePlatformListenStatusLabel();
}

void AggregateChatForm::onStartPlatformListeningClicked()
{
    const QStringList platforms = selectedPlatformListenTargets();
    if (platforms.isEmpty()) {
        showStatusMessage(QStringLiteral("请先选择要监听的平台"), 3000);
        return;
    }

    QStringList started;
    for (const QString& platform : platforms) {
        if (ConversationManager::instance().startPlatformListening(platform))
            started.append(listenPlatformDisplayName(platform));
    }
    refreshPlatformListenStateFromService();
    if (started.isEmpty()) {
        for (const QString& platform : platforms) {
            if (m_serviceListeningPlatforms.contains(platform)
                || ConversationManager::instance().isPlatformListening(platform)) {
                started.append(listenPlatformDisplayName(platform));
            }
        }
    }
    if (started.isEmpty()) {
        showStatusMessage(QStringLiteral("平台监听启动失败，请检查 Python 服务"), 5000);
        return;
    }
    showStatusMessage(QStringLiteral("已请求监听：%1").arg(started.join(QStringLiteral("、"))), 4000);
}

void AggregateChatForm::onStopPlatformListeningClicked()
{
    const QStringList platforms = selectedPlatformListenTargets();
    if (platforms.isEmpty()) {
        showStatusMessage(QStringLiteral("请先选择要停止监听的平台"), 3000);
        return;
    }

    QStringList stopped;
    for (const QString& platform : platforms) {
        if (ConversationManager::instance().stopPlatformListening(platform))
            stopped.append(listenPlatformDisplayName(platform));
    }
    refreshPlatformListenStateFromService();
    if (stopped.isEmpty()) {
        showStatusMessage(QStringLiteral("平台监听停止失败"), 5000);
        return;
    }
    showStatusMessage(QStringLiteral("已请求停止：%1").arg(stopped.join(QStringLiteral("、"))), 4000);
}

void AggregateChatForm::updatePlatformSectionTitle()
{
    if (!m_platformSectionTitle)
        return;
    QString t;
    switch (m_platformFilter) {
    case AggregatePlatformFilter::All:
        t = QStringLiteral("消息中心");
        break;
    case AggregatePlatformFilter::Qianniu:
        t = QStringLiteral("千牛");
        break;
    case AggregatePlatformFilter::Pdd:
        t = QStringLiteral("拼多多");
        break;
    case AggregatePlatformFilter::Doudian:
        t = QStringLiteral("抖店");
        break;
    case AggregatePlatformFilter::Wechat:
        t = QStringLiteral("微信");
        break;
    case AggregatePlatformFilter::QQ:
        t = QStringLiteral("QQ");
        break;
    }
    m_platformSectionTitle->setText(t);
}

// ===================== Left Panel =====================

QWidget* AggregateChatForm::buildLeftPanel()
{
    auto* panel = new QWidget(this);
    m_leftPanel = panel;
    panel->setObjectName("aggregateLeftPanel");
    panel->setAutoFillBackground(true);
    panel->setMinimumWidth(kAggregateConversationPanelMinWidth);
    panel->setMaximumWidth(kAggregateConversationPanelMaxWidth);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_platformSectionTitle = new QLabel(panel);
    m_platformSectionTitle->setObjectName(QStringLiteral("aggregatePlatformSectionTitle"));
    m_platformSectionTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    updatePlatformSectionTitle();
    layout->addWidget(m_platformSectionTitle);

    auto* listenBox = new QFrame(panel);
    listenBox->setObjectName(QStringLiteral("aggregatePlatformListenBox"));
    listenBox->setFrameShape(QFrame::NoFrame);
    auto* listenLayout = new QVBoxLayout(listenBox);
    listenLayout->setContentsMargins(10, 8, 10, 8);
    listenLayout->setSpacing(6);

    auto* listenTitle = new QLabel(QStringLiteral("平台监听"), listenBox);
    listenTitle->setObjectName(QStringLiteral("aggregateModeLabel"));
    listenLayout->addWidget(listenTitle);

    auto* listenCheckRow = new QWidget(listenBox);
    auto* listenCheckLayout = new QHBoxLayout(listenCheckRow);
    listenCheckLayout->setContentsMargins(0, 0, 0, 0);
    listenCheckLayout->setSpacing(10);
    m_chkListenWechat = new QCheckBox(QStringLiteral("微信"), listenCheckRow);
    m_chkListenWechat->setObjectName(QStringLiteral("aggregatePlatformListenCheck"));
    m_chkListenQianniu = new QCheckBox(QStringLiteral("千牛"), listenCheckRow);
    m_chkListenQianniu->setObjectName(QStringLiteral("aggregatePlatformListenCheck"));
    m_chkListenPdd = new QCheckBox(QStringLiteral("拼多多"), listenCheckRow);
    m_chkListenPdd->setObjectName(QStringLiteral("aggregatePlatformListenCheck"));
    m_chkListenQQ = new QCheckBox(QStringLiteral("QQ"), listenCheckRow);
    m_chkListenQQ->setObjectName(QStringLiteral("aggregatePlatformListenCheck"));
    listenCheckLayout->addWidget(m_chkListenWechat);
    listenCheckLayout->addWidget(m_chkListenQianniu);
    listenCheckLayout->addWidget(m_chkListenPdd);
    listenCheckLayout->addWidget(m_chkListenQQ);
    listenCheckLayout->addStretch(1);
    listenLayout->addWidget(listenCheckRow);

    auto* listenButtonRow = new QWidget(listenBox);
    auto* listenButtonLayout = new QHBoxLayout(listenButtonRow);
    listenButtonLayout->setContentsMargins(0, 0, 0, 0);
    listenButtonLayout->setSpacing(8);
    m_btnStartPlatformListening = new QPushButton(QStringLiteral("开始监听"), listenButtonRow);
    m_btnStartPlatformListening->setObjectName(QStringLiteral("aggregatePlatformListenStartButton"));
    m_btnStartPlatformListening->setCursor(Qt::PointingHandCursor);
    m_btnStopPlatformListening = new QPushButton(QStringLiteral("停止监听"), listenButtonRow);
    m_btnStopPlatformListening->setObjectName(QStringLiteral("aggregatePlatformListenStopButton"));
    m_btnStopPlatformListening->setCursor(Qt::PointingHandCursor);
    listenButtonLayout->addWidget(m_btnStartPlatformListening);
    listenButtonLayout->addWidget(m_btnStopPlatformListening);
    listenLayout->addWidget(listenButtonRow);

    m_platformListenStatusLabel = new QLabel(QStringLiteral("未监听平台"), listenBox);
    m_platformListenStatusLabel->setObjectName(QStringLiteral("aggregatePlatformListenStatus"));
    m_platformListenStatusLabel->setWordWrap(true);
    listenLayout->addWidget(m_platformListenStatusLabel);
    layout->addWidget(listenBox);

    connect(m_btnStartPlatformListening, &QPushButton::clicked,
            this, &AggregateChatForm::onStartPlatformListeningClicked);
    connect(m_btnStopPlatformListening, &QPushButton::clicked,
            this, &AggregateChatForm::onStopPlatformListeningClicked);
    setPlatformListenControlsEnabled(false);
    updatePlatformListenStatusLabel();

    // Tabs
    auto* tabRow = new QWidget(panel);
    auto* tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(8);
    m_btnAll = new QPushButton(QStringLiteral("全部"), tabRow);
    m_btnAll->setObjectName("aggregateTabAll");
    m_btnAll->setCheckable(true);
    m_btnAll->setChecked(false);
    m_btnAll->setCursor(Qt::PointingHandCursor);
    m_btnPending = new QPushButton(QStringLiteral("待处理"), tabRow);
    m_btnPending->setObjectName("aggregateTabPending");
    m_btnPending->setCheckable(true);
    m_btnPending->setChecked(true);
    m_btnPending->setCursor(Qt::PointingHandCursor);
    m_btnReplied = new QPushButton(QStringLiteral("已回复"), tabRow);
    m_btnReplied->setObjectName("aggregateTabReplied");
    m_btnReplied->setCheckable(true);
    m_btnReplied->setChecked(false);
    m_btnReplied->setCursor(Qt::PointingHandCursor);
    tabLayout->addWidget(m_btnAll);
    tabLayout->addWidget(m_btnPending);
    tabLayout->addWidget(m_btnReplied);
    tabLayout->addStretch(1);
    layout->addWidget(tabRow);
    connect(m_btnAll, &QPushButton::clicked, this, &AggregateChatForm::onTabAllClicked);
    connect(m_btnPending, &QPushButton::clicked, this, &AggregateChatForm::onTabPendingClicked);
    connect(m_btnReplied, &QPushButton::clicked, this, &AggregateChatForm::onTabRepliedClicked);

    // Search
    auto* searchRow = new QWidget(panel);
    auto* searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(0);
    m_searchEdit = new QLineEdit(searchRow);
    m_searchEdit->setObjectName("aggregateSearch");
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索会话或客户名"));
    m_searchEdit->addAction(QIcon(QStringLiteral(":/aggregate_reception_icons/search_icon.svg")),
                            QLineEdit::LeadingPosition);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { refreshConversationList(); });
    searchLayout->addWidget(m_searchEdit, 1);
    layout->addWidget(searchRow);

    // Conversation list
    m_leftStack = new QStackedWidget(panel);
    m_leftStack->setObjectName(QStringLiteral("aggregateLeftStack"));
    m_conversationList = new QListView(panel);
    m_conversationList->setObjectName("aggregateConversationList");
    m_conversationList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_conversationList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_conversationList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_conversationList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_conversationList->setUniformItemSizes(false);
    m_conversationList->setMouseTracking(true);
    m_conversationList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_conversationList->setModel(m_conversationListModel);
    m_conversationList->setItemDelegate(new ConversationItemDelegate(m_conversationList));
    connect(m_conversationList, &QListView::clicked,
            this, &AggregateChatForm::onConversationIndexClicked);
    connect(m_conversationList, &QListView::customContextMenuRequested,
            this, &AggregateChatForm::onConversationListContextMenu);
    m_leftStack->addWidget(m_conversationList);

    auto* listEmpty = new QWidget(panel);
    listEmpty->setObjectName("aggregateListEmpty");
    auto* listEmptyLayout = new QVBoxLayout(listEmpty);
    listEmptyLayout->setAlignment(Qt::AlignCenter);
    listEmptyLayout->setContentsMargins(12, 0, 12, 0);
    auto* emptyText = new QLabel(QStringLiteral("暂无会话"), listEmpty);
    emptyText->setObjectName("aggregateListEmptyText");
    emptyText->setAlignment(Qt::AlignCenter);
    listEmptyLayout->addWidget(emptyText);
    m_leftStack->addWidget(listEmpty);
    m_leftStack->setCurrentIndex(1);
    layout->addWidget(m_leftStack, 1);

    return panel;
}

// ===================== Center Panel =====================

QWidget* AggregateChatForm::buildCenterPanel()
{
    auto* panel = new QWidget(this);
    m_centerPanel = panel;
    panel->setObjectName("aggregateCenterPanel");
    panel->setAutoFillBackground(true);
    m_centerStack = new QStackedWidget(panel);
    m_centerStack->setObjectName(QStringLiteral("aggregateCenterStack"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_centerStack);

    // Empty state
    m_centerEmptyState = new QWidget(panel);
    m_centerEmptyState->setObjectName(QStringLiteral("aggregateCenterEmptyState"));
    auto* emptyLayout = new QVBoxLayout(m_centerEmptyState);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(8);
    emptyLayout->setContentsMargins(24, 0, 24, 0);
    auto* mainText = new QLabel(QStringLiteral("选择一个会话开始聊天"), m_centerEmptyState);
    mainText->setObjectName("aggregateEmptyMain");
    mainText->setAlignment(Qt::AlignHCenter);
    auto* subText = new QLabel(
        QStringLiteral("从左侧列表中选择会话开始接待"),
        m_centerEmptyState);
    subText->setObjectName("aggregateEmptySub");
    subText->setAlignment(Qt::AlignHCenter);
    subText->setWordWrap(true);
    emptyLayout->addWidget(mainText);
    emptyLayout->addWidget(subText);
    m_centerStack->addWidget(m_centerEmptyState);

    // Chat area
    m_chatArea = new QWidget(panel);
    m_chatArea->setObjectName("chatArea");
    m_chatArea->setAutoFillBackground(true);
    auto* chatLayout = new QVBoxLayout(m_chatArea);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    // Chat header
    m_chatHeaderBar = new QWidget(m_chatArea);
    m_chatHeaderBar->setObjectName("chatHeader");
    m_chatHeaderBar->setFixedHeight(48);
    auto* headerLayout = new QHBoxLayout(m_chatHeaderBar);
    headerLayout->setContentsMargins(8, 0, 16, 0);
    headerLayout->setSpacing(4);
    m_btnBackToConversationList = new QToolButton(m_chatHeaderBar);
    m_btnBackToConversationList->setObjectName(QStringLiteral("aggregateConversationBackButton"));
    m_btnBackToConversationList->setText(QStringLiteral("<"));
    m_btnBackToConversationList->setCursor(Qt::PointingHandCursor);
    m_btnBackToConversationList->setAutoRaise(true);
    m_btnBackToConversationList->setFixedSize(32, 32);
    m_btnBackToConversationList->setVisible(false);
    connect(m_btnBackToConversationList, &QToolButton::clicked, this, [this]() {
        m_compactConversationListForced = false;
        setConversationPanelHidden(false);
    });
    headerLayout->addWidget(m_btnBackToConversationList, 0, Qt::AlignVCenter);
    m_chatHeader = new QLabel(m_chatHeaderBar);
    m_chatHeader->setObjectName("chatHeaderTitle");
    m_chatHeader->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_chatHeader->setContentsMargins(4, 0, 0, 0);
    headerLayout->addWidget(m_chatHeader, 1);
    m_btnSyncWechatHistory = new QToolButton(m_chatHeaderBar);
    m_btnSyncWechatHistory->setObjectName(QStringLiteral("aggregateSyncWechatHistoryButton"));
    m_btnSyncWechatHistory->setIcon(QIcon(QStringLiteral(":/aggregate_reception_icons/sync_hitstory_messages_icon.svg")));
    m_btnSyncWechatHistory->setIconSize(QSize(20, 20));
    m_btnSyncWechatHistory->setToolTip(QStringLiteral("同步当前会话最近历史"));
    m_btnSyncWechatHistory->setAccessibleName(QStringLiteral("同步当前会话最近历史"));
    m_btnSyncWechatHistory->setCursor(Qt::PointingHandCursor);
    m_btnSyncWechatHistory->setAutoRaise(true);
    m_btnSyncWechatHistory->setFixedSize(32, 32);
    m_btnSyncWechatHistory->setVisible(false);
    connect(m_btnSyncWechatHistory, &QToolButton::clicked,
            this, &AggregateChatForm::onSyncWechatHistoryClicked);
    headerLayout->addWidget(m_btnSyncWechatHistory, 0, Qt::AlignVCenter);
    chatLayout->addWidget(m_chatHeaderBar);

    m_chatInputOverlayHost = new QWidget(m_chatArea);
    m_chatInputOverlayHost->setObjectName(QStringLiteral("aggregateChatInputOverlayHost"));
    // 须不透明绘制：若 WA_TranslucentBackground + 透明底，分割器改宽时子控件平移会擦不掉旧像素，出现输入框「重影」
    chatLayout->addWidget(m_chatInputOverlayHost, 1);

    // Message scroll：与底部输入条叠放，输入条盖在消息区下缘
    m_messageView = new MessageListView(m_chatInputOverlayHost);
    m_messageView->setObjectName("messageScroll");
    m_messageView->setFrameShape(QFrame::NoFrame);
    m_messageView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_messageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageView->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageView->setFocusPolicy(Qt::NoFocus);
    m_messageView->setUniformItemSizes(false);
    m_messageView->setResizeMode(QListView::Adjust);
    m_messageView->setMouseTracking(true);
    m_messageView->setModel(m_messageListModel);
    m_messageItemDelegate = new ModernMessageItemDelegate(m_messageView);
    if (auto* delegate = static_cast<ModernMessageItemDelegate*>(m_messageItemDelegate)) {
        delegate->setSelfProfile(m_selfDisplayName, m_selfAvatarPixmap);
        delegate->setCustomerAvatar(m_customerDefaultAvatarPixmap);
    }
    m_messageView->setItemDelegate(m_messageItemDelegate);
    if (QWidget* vp = m_messageView->viewport())
        vp->setObjectName(QStringLiteral("messageScrollViewport"));
    if (QWidget* vp = m_messageView->viewport())
        vp->setAutoFillBackground(false);
    connect(m_messageView, &QListView::clicked, this, [this](const QModelIndex& index) {
        const MessageRecord msg = index.data(MessageListModel::MessageRole).value<MessageRecord>();
        openMessageMedia(msg);
    });
    if (QScrollBar* sb = m_messageView->verticalScrollBar()) {
        connect(sb, &QScrollBar::valueChanged, this, [this]() {
            updateMessageScrollState();
        });
        connect(sb, &QScrollBar::rangeChanged, this, [this]() {
            updateMessageScrollState();
        });
    }

    m_btnNewMessages = new QToolButton(m_chatInputOverlayHost);
    m_btnNewMessages->setObjectName(QStringLiteral("aggregateNewMessageHint"));
    m_btnNewMessages->setText(QStringLiteral("新消息"));
    m_btnNewMessages->setCursor(Qt::PointingHandCursor);
    m_btnNewMessages->setAutoRaise(false);
    m_btnNewMessages->setVisible(false);
    m_btnNewMessages->setStyleSheet(QStringLiteral(
        "QToolButton#aggregateNewMessageHint{"
        "background:#FFFFFF;color:#0F172A;border:1px solid #CBD5E1;"
        "border-radius:14px;padding:4px 12px;font-size:12px;"
        "}"
        "QToolButton#aggregateNewMessageHint:hover{background:#F8FAFC;border-color:#38BDF8;}"
        "QToolButton#aggregateNewMessageHint:pressed{background:#E0F2FE;}"));
    connect(m_btnNewMessages, &QToolButton::clicked, this, [this]() {
        clearPendingNewMessageHint();
        scheduleScrollChatToBottom(true);
    });

    m_chatInputPanel = new QWidget(m_chatInputOverlayHost);
    m_chatInputPanel->setObjectName(QStringLiteral("aggregateChatInputPanel"));
    auto* inputOuter = new QVBoxLayout(m_chatInputPanel);
    inputOuter->setContentsMargins(0, 0, 0, 0);
    inputOuter->setSpacing(0);
    // 内层 16 控制输入/按钮与白色块边缘的留白；外层 0 使整块白底与中间栏（分割器）左右对齐
    auto* inputInner = new QWidget(m_chatInputPanel);
    auto* inputLayout = new QVBoxLayout(inputInner);
    inputLayout->setContentsMargins(16, 8, 16, 12);
    inputLayout->setSpacing(8);

    {
        QSettings st = AppSettings::create();
        m_aggregateAiSessionModelKey =
            st.value(QStringLiteral("aggregateAi/sessionModelKey"), QStringLiteral("deepseek:deepseek-chat"))
                .toString();
        if (m_aggregateAiSessionModelKey != QLatin1String("doubao:ark")
            && m_aggregateAiSessionModelKey != QLatin1String("deepseek:deepseek-chat"))
            m_aggregateAiSessionModelKey = QStringLiteral("deepseek:deepseek-chat");
    }

    m_btnAiModelPick = new QToolButton(inputInner);
    m_btnAiModelPick->setObjectName(QStringLiteral("aggregateAiModelButton"));
    m_btnAiModelPick->setCursor(Qt::PointingHandCursor);
    m_btnAiModelPick->setPopupMode(QToolButton::InstantPopup);
    m_btnAiModelPick->setAutoRaise(false);
    m_btnAiModelPick->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_aggregateAiModelMenu = new QMenu(m_btnAiModelPick);
    auto* actDoubao = m_aggregateAiModelMenu->addAction(QStringLiteral("豆包"));
    actDoubao->setData(QStringLiteral("doubao:ark"));
    actDoubao->setCheckable(true);
    auto* actDs = m_aggregateAiModelMenu->addAction(QStringLiteral("DeepSeek"));
    actDs->setData(QStringLiteral("deepseek:deepseek-chat"));
    actDs->setCheckable(true);
    m_btnAiModelPick->setMenu(m_aggregateAiModelMenu);
    m_btnAiModelPick->setToolTip(
        QStringLiteral("选择「生成本条回复」使用的线路；与左栏「管理后台」→「AI 客服后台」→"
                       "「API 配置/模型」中同一线路的配置一致。"));
    connect(m_aggregateAiModelMenu, &QMenu::triggered, this,
            &AggregateChatForm::onAggregateAiModelMenuTriggered);
    refreshAggregateAiModelButtonUi();

    m_composeAttachmentsScroll = new QScrollArea(inputInner);
    m_composeAttachmentsScroll->setObjectName(QStringLiteral("composeAttachmentsScroll"));
    m_composeAttachmentsScroll->setFrameShape(QFrame::NoFrame);
    m_composeAttachmentsScroll->setWidgetResizable(true);
    m_composeAttachmentsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_composeAttachmentsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_composeAttachmentsScroll->setMaximumHeight(86);
    m_composeAttachmentsScroll->setVisible(false);
    m_composeAttachmentsWidget = new QWidget(m_composeAttachmentsScroll);
    m_composeAttachmentsWidget->setObjectName(QStringLiteral("composeAttachmentsWidget"));
    m_composeAttachmentsWidget->setAutoFillBackground(true);
    m_composeAttachmentsLayout = new QHBoxLayout(m_composeAttachmentsWidget);
    m_composeAttachmentsLayout->setContentsMargins(0, 0, 0, 0);
    m_composeAttachmentsLayout->setSpacing(8);
    m_composeAttachmentsLayout->addStretch(1);
    m_composeAttachmentsScroll->setWidget(m_composeAttachmentsWidget);
    if (QWidget* vp = m_composeAttachmentsScroll->viewport()) {
        vp->setObjectName(QStringLiteral("composeAttachmentsViewport"));
        vp->setAutoFillBackground(true);
    }
    inputLayout->addWidget(m_composeAttachmentsScroll);

    m_suggestedImagePanel = new QWidget(inputInner);
    m_suggestedImagePanel->setObjectName(QStringLiteral("aggregateSuggestedImagePanel"));
    m_suggestedImagePanel->setVisible(false);
    m_suggestedImagePanel->setStyleSheet(QStringLiteral(
        "QWidget#aggregateSuggestedImagePanel{"
        "background:#F8FAFC;border:1px solid #D9E2EC;border-radius:6px;"
        "}"));
    auto* suggestedPanelLayout = new QVBoxLayout(m_suggestedImagePanel);
    suggestedPanelLayout->setContentsMargins(10, 8, 10, 8);
    suggestedPanelLayout->setSpacing(6);
    auto* suggestedHeader = new QHBoxLayout();
    suggestedHeader->setContentsMargins(0, 0, 0, 0);
    suggestedHeader->setSpacing(8);
    auto* suggestedTitle = new QLabel(QStringLiteral("建议附图"), m_suggestedImagePanel);
    suggestedTitle->setStyleSheet(QStringLiteral("color:#0F172A;font-size:12px;font-weight:600;"));
    auto* suggestedHint = new QLabel(QStringLiteral("需人工预览确认，当前不会自动发送图片"), m_suggestedImagePanel);
    suggestedHint->setText(QStringLiteral("人工预览确认后，可添加为本条回复附件一起发送"));
    suggestedHint->setStyleSheet(QStringLiteral("color:#64748B;font-size:11px;"));
    suggestedHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    suggestedHeader->addWidget(suggestedTitle, 0, Qt::AlignVCenter);
    suggestedHeader->addWidget(suggestedHint, 1, Qt::AlignVCenter);
    suggestedPanelLayout->addLayout(suggestedHeader);
    m_suggestedImageLayout = new QHBoxLayout();
    m_suggestedImageLayout->setContentsMargins(0, 0, 0, 0);
    m_suggestedImageLayout->setSpacing(8);
    m_suggestedImageLayout->addStretch(1);
    suggestedPanelLayout->addLayout(m_suggestedImageLayout);
    inputLayout->addWidget(m_suggestedImagePanel);

    auto* composeBox = new QWidget(inputInner);
    composeBox->setObjectName(QStringLiteral("aggregateComposeBox"));
    auto* composeLayout = new QVBoxLayout(composeBox);
    composeLayout->setContentsMargins(10, 8, 10, 8);
    composeLayout->setSpacing(6);

    auto* composeEdit = new ComposeTextEdit(composeBox);
    composeEdit->mimeHandler = [this](const QMimeData* mimeData) {
        return handleComposeMimeData(mimeData);
    };
    m_inputEdit = composeEdit;
    m_inputEdit->setObjectName("messageInput");
    m_inputEdit->setPlaceholderText(QString());
    m_inputEdit->setToolTip(QStringLiteral("输入消息，Ctrl+Enter 发送"));
    m_inputEdit->setFrameShape(QFrame::NoFrame);
    m_inputEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_inputEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inputEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inputEdit->document()->setDocumentMargin(0);
    composeLayout->addWidget(m_inputEdit);

    m_draftSaveTimer = new QTimer(this);
    m_draftSaveTimer->setSingleShot(true);
    m_draftSaveTimer->setInterval(600);
    connect(m_draftSaveTimer, &QTimer::timeout, this, &AggregateChatForm::persistCurrentDraft);

    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(8);
    btnRow->addWidget(m_btnAiModelPick, 0, Qt::AlignLeft | Qt::AlignVCenter);
    m_btnAutoReplyToggle = new QPushButton(composeBox);
    m_btnAutoReplyToggle->setObjectName(QStringLiteral("aggregateAutoReplyToggleButton"));
    m_btnAutoReplyToggle->setCursor(Qt::PointingHandCursor);
    m_btnAutoReplyToggle->setFlat(true);
    m_btnAutoReplyToggle->setFocusPolicy(Qt::NoFocus);
    m_btnAutoReplyToggle->setCheckable(true);
    m_btnAutoReplyToggle->setIconSize(QSize(20, 20));
    m_btnAutoReplyToggle->setFixedSize(38, 38);
    connect(m_btnAutoReplyToggle, &QPushButton::clicked, this, &AggregateChatForm::onAutoReplyToggleClicked);
    btnRow->addWidget(m_btnAutoReplyToggle, 0, Qt::AlignLeft | Qt::AlignVCenter);
    m_statusLabel = new QLabel(composeBox);
    m_statusLabel->setObjectName(QStringLiteral("aggregateInlineStatus"));
    m_statusLabel->setWordWrap(false);
    m_statusLabel->setMaximumWidth(320);
    m_statusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_statusLabel->setVisible(false);
    btnRow->addWidget(m_statusLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    btnRow->addStretch(1);
    m_btnAiGenerate = new QPushButton(composeBox);
    m_btnAiGenerate->setObjectName(QStringLiteral("simulateButton"));
    m_btnAiGenerate->setIcon(QIcon(QStringLiteral(":/aggregate_reception_icons/generate_reply_icon.svg")));
    m_btnAiGenerate->setIconSize(QSize(20, 20));
    m_btnAiGenerate->setToolTip(QStringLiteral("生成本条回复"));
    m_btnAiGenerate->setAccessibleName(QStringLiteral("生成本条回复"));
    m_btnAiGenerate->setCursor(Qt::PointingHandCursor);
    m_btnAiGenerate->setFixedSize(38, 38);
    connect(m_btnAiGenerate, &QPushButton::clicked, this, &AggregateChatForm::onGenerateAiDraftClicked);
    m_btnSend = new QPushButton(composeBox);
    m_btnSend->setObjectName("sendButton");
    m_btnSend->setIcon(QIcon(QStringLiteral(":/aggregate_reception_icons/send_icon.svg")));
    m_btnSend->setIconSize(QSize(20, 20));
    m_btnSend->setToolTip(QStringLiteral("发送"));
    m_btnSend->setAccessibleName(QStringLiteral("发送"));
    m_btnSend->setCursor(Qt::PointingHandCursor);
    m_btnSend->setFixedSize(38, 38);
    connect(m_btnSend, &QPushButton::clicked, this, &AggregateChatForm::onSendClicked);
    btnRow->addWidget(m_btnAiGenerate);
    btnRow->addWidget(m_btnSend);
    refreshAutoReplyToggleButtonUi();
    composeLayout->addLayout(btnRow);
    inputLayout->addWidget(composeBox);
    updateComposeInputHeight();

    inputOuter->addWidget(inputInner, 0);

    m_chatInputOverlayHost->installEventFilter(this);
    connect(m_inputEdit->document(), &QTextDocument::contentsChanged, this, [this]() {
        updateComposeInputHeight();
        scheduleChatInputRelayout();
        if (!m_restoringDraft && m_currentConvId > 0 && m_draftSaveTimer)
            m_draftSaveTimer->start();
    });

    m_centerStack->addWidget(m_chatArea);
    m_centerStack->setCurrentWidget(m_centerEmptyState);

    return panel;
}

// ===================== Right Panel =====================

QWidget* AggregateChatForm::buildRightPanel()
{
    auto* panel = new QWidget(this);
    m_rightPanel = panel;
    panel->setObjectName("aggregateRightPanel");
    panel->setAutoFillBackground(true);
    panel->setMinimumWidth(kAggregateRightPanelMinWidth);
    panel->setMaximumWidth(kAggregateRightPanelMaxWidth);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_rightStack = new QStackedWidget(panel);
    m_rightStack->setObjectName(QStringLiteral("aggregateRightStack"));
    auto* rightStackHost = new QWidget(panel);
    rightStackHost->setObjectName(QStringLiteral("aggregateRightStackHost"));
    auto* stackGrid = new QGridLayout(rightStackHost);
    stackGrid->setContentsMargins(16, 16, 16, 16);
    stackGrid->setRowStretch(0, 1);
    stackGrid->setColumnStretch(0, 1);
    stackGrid->addWidget(m_rightStack, 0, 0);
    m_modelPickerOverlay = new QWidget(rightStackHost);
    m_modelPickerOverlay->setObjectName(QStringLiteral("aggregateModelPickerOverlay"));
    m_modelPickerOverlay->hide();
    stackGrid->addWidget(m_modelPickerOverlay, 0, 0);
    layout->addWidget(rightStackHost, 1);

    auto* oLay = new QHBoxLayout(m_modelPickerOverlay);
    oLay->setContentsMargins(0, 0, 0, 0);
    oLay->setSpacing(0);
    oLay->addStretch(1);
    m_modelPickerSheet = new QWidget(m_modelPickerOverlay);
    m_modelPickerSheet->setObjectName(QStringLiteral("aggregateModelPickerSheet"));
    m_modelPickerSheet->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_modelPickerSheet->setFixedWidth(0);
    auto* sheetV = new QVBoxLayout(m_modelPickerSheet);
    sheetV->setContentsMargins(12, 8, 12, 8);
    sheetV->setSpacing(8);
    auto* topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);
    m_btnModelPickerBack = new QToolButton(m_modelPickerSheet);
    m_btnModelPickerBack->setObjectName(QStringLiteral("aggregateModelPickerBack"));
    m_btnModelPickerBack->setText(QStringLiteral("返回"));
    m_btnModelPickerBack->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_btnModelPickerBack->setAutoRaise(true);
    m_btnModelPickerBack->setFocusPolicy(Qt::NoFocus);
    m_btnModelPickerBack->setCursor(Qt::PointingHandCursor);
    auto* pickerTitle = new QLabel(QStringLiteral("选择展示模型"), m_modelPickerSheet);
    pickerTitle->setObjectName(QStringLiteral("aggregateModelPickerTitle"));
    topBar->addWidget(m_btnModelPickerBack, 0, Qt::AlignLeft | Qt::AlignVCenter);
    topBar->addWidget(pickerTitle, 1, Qt::AlignCenter);
    sheetV->addLayout(topBar);
    m_modelPickerList = new QListWidget(m_modelPickerSheet);
    m_modelPickerList->setObjectName(QStringLiteral("aggregateModelPickerList"));
    m_modelPickerList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_modelPickerList->setFrameShape(QFrame::NoFrame);
    for (const AiPresetDefinition& def : aiPresetDefinitions()) {
        auto* li = new QListWidgetItem(def.label, m_modelPickerList);
        QString tip = def.label;
        if (!def.defaultModel.isEmpty())
            tip += QLatin1String("\n") + QStringLiteral("默认模型：%1").arg(def.defaultModel);
        if (!def.defaultBaseUrl.isEmpty())
            tip += QLatin1String("\n") + QStringLiteral("Base：%1").arg(def.defaultBaseUrl);
        if (!def.available)
            tip += QStringLiteral("\n（尚未接入，仅作展示）");
        li->setToolTip(tip);
    }
    sheetV->addWidget(m_modelPickerList, 1);
    oLay->addWidget(m_modelPickerSheet, 0, Qt::AlignRight);

    // Customer detail：上模型 / 中性能指标 / 下发送状态（无会话时亦显示，不再单独整页占位）
    m_customerDetail = new QWidget(panel);
    auto* detailLayout = new QVBoxLayout(m_customerDetail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(0);
    detailLayout->setAlignment(Qt::AlignTop);

    m_rightBarScroll = new QScrollArea(m_customerDetail);
    m_rightBarScroll->setObjectName(QStringLiteral("aggregateRightBarScroll"));
    m_rightBarScroll->setFrameShape(QFrame::NoFrame);
    m_rightBarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_rightBarScroll->setWidgetResizable(true);
    if (QWidget* v = m_rightBarScroll->viewport()) {
        v->setObjectName(QStringLiteral("aggregateRightBarScrollViewport"));
        v->setAttribute(Qt::WA_StyledBackground, true);
    }
    m_rightBarScrollContent = new QWidget();
    m_rightBarScrollContent->setObjectName(QStringLiteral("aggregateRightBarScrollContent"));
    m_rightBarScrollContent->setAttribute(Qt::WA_StyledBackground, true);
    /* 避免 setWidgetResizable(true) 把内容区拉满视口高度，进而在中间/底部出现大块空白；仅按内容要高度。 */
    m_rightBarScrollContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_rightBarVLayout = new QVBoxLayout(m_rightBarScrollContent);
    m_rightBarVLayout->setContentsMargins(0, 0, 0, 0);
    m_rightBarVLayout->setSpacing(12);
    m_rightBarVLayout->setAlignment(Qt::AlignTop);
    m_rightBarScroll->setWidget(m_rightBarScrollContent);
    detailLayout->addWidget(m_rightBarScroll, 1);

    // —— 上：模型区（图标/标题文字居中，对照浅色参考图） ——
    auto* modelBlock = new QWidget(m_rightBarScrollContent);
    modelBlock->setObjectName(QStringLiteral("aggregateRightBarModelBlock"));
    modelBlock->setAttribute(Qt::WA_StyledBackground, true);
    modelBlock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* modelBlockLay = new QVBoxLayout(modelBlock);
    modelBlockLay->setContentsMargins(0, 0, 0, 0);
    modelBlockLay->setSpacing(8);

    auto* iconRow = new QHBoxLayout();
    iconRow->setContentsMargins(0, 0, 0, 0);
    iconRow->addStretch(1);
    auto* iconBox = new QFrame(modelBlock);
    iconBox->setObjectName(QStringLiteral("aggregateRightBarModelIcon"));
    iconBox->setFixedSize(kAggregateRightBarModelIconBoxSide, kAggregateRightBarModelIconBoxSide);
    auto* iconL = new QVBoxLayout(iconBox);
    iconL->setContentsMargins(0, 0, 0, 0);
    iconL->setSpacing(0);
    m_rightBarModelIconLabel = new QLabel(iconBox);
    m_rightBarModelIconLabel->setAlignment(Qt::AlignCenter);
    m_rightBarModelIconLabel->setFixedSize(kAggregateRightBarModelIconDrawSide,
                                           kAggregateRightBarModelIconDrawSide);
    iconL->addWidget(m_rightBarModelIconLabel, 0, Qt::AlignCenter);
    m_rightBarModelStatusDot = new QWidget(iconBox);
    m_rightBarModelStatusDot->setObjectName(QStringLiteral("aggregateRightBarModelStatusDot"));
    m_rightBarModelStatusDot->setFixedSize(8, 8);
    m_rightBarModelStatusDot->move(kAggregateRightBarModelIconBoxSide - 8 - 4, 4);
    m_rightBarModelStatusDot->raise();
    iconRow->addWidget(iconBox, 0, Qt::AlignHCenter);
    iconRow->addStretch(1);
    modelBlockLay->addLayout(iconRow);

    m_rightBarModelNameLabel = new QLabel(modelBlock);
    m_rightBarModelNameLabel->setObjectName(QStringLiteral("aggregateRightBarModelName"));
    m_rightBarModelNameLabel->setWordWrap(true);
    m_rightBarModelNameLabel->setAlignment(Qt::AlignHCenter);
    modelBlockLay->addWidget(m_rightBarModelNameLabel);
    m_rightBarVLayout->addWidget(modelBlock);

    auto* sep1 = new QFrame(m_rightBarScrollContent);
    sep1->setObjectName(QStringLiteral("aggregateRightBarBlockSep"));
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFixedHeight(1);
    sep1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_rightBarVLayout->addWidget(sep1);

    // —— 中：性能指标 2x2 占位 ——
    auto* metricsWrap = new QWidget(m_rightBarScrollContent);
    m_rightBarMetricsWrap = metricsWrap;
    auto* metLay = new QVBoxLayout(metricsWrap);
    metLay->setContentsMargins(0, 0, 0, 0);
    metLay->setSpacing(6);
    metricsWrap->setObjectName(QStringLiteral("aggregateRightBarMetricsWrap"));
    metricsWrap->setAttribute(Qt::WA_StyledBackground, true);
    metricsWrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* metTitle = new QHBoxLayout();
    auto* metTitleL = new QLabel(QStringLiteral("性能指标"), metricsWrap);
    metTitleL->setObjectName(QStringLiteral("aggregateRightBarMetricsTitle"));
    metTitle->addWidget(metTitleL);
    metTitle->addStretch(1);
    metLay->addLayout(metTitle);
    const QStringList metricCap = { QStringLiteral("AI请求"), QStringLiteral("成功率"), QStringLiteral("系统健康"),
                                    QStringLiteral("响应速度") };
    const QString kMetricResPaths[4] = { QStringLiteral(":/aggregate_reception_icons/request_handle_icon.svg"),
                                         QStringLiteral(":/aggregate_reception_icons/system_healthy_icon.svg"),
                                         QStringLiteral(":/aggregate_reception_icons/system_healthy_icon.svg"),
                                         QStringLiteral(":/aggregate_reception_icons/response_speed_icon.svg") };
    const QString kMetricKeyProp[4] = { QStringLiteral("request"), QStringLiteral("runtime"),
                                        QStringLiteral("system"), QStringLiteral("response") };
    auto* grid2 = new QGridLayout();
    m_rightBarMetricsGrid = grid2;
    grid2->setContentsMargins(1, 2, 1, 4);
    grid2->setHorizontalSpacing(8);
    grid2->setVerticalSpacing(8);
    grid2->setColumnStretch(0, 1);
    grid2->setColumnStretch(1, 1);
    for (int i = 0; i < 4; ++i) {
        auto* card = new AggregateRightMetricCardFrame(metricsWrap);
        m_rightBarMetricCards[i] = card;
        auto* cv = new QVBoxLayout(card);
        cv->setContentsMargins(14, 14, 14, 14);
        cv->setSpacing(6);

        auto* iconWrap = new QFrame(card);
        iconWrap->setObjectName(QStringLiteral("aggregateRightBarMetricIconWrap"));
        iconWrap->setFrameShape(QFrame::NoFrame);
        iconWrap->setAttribute(Qt::WA_StyledBackground, true);
        iconWrap->setFixedSize(40, 40);
        iconWrap->setProperty("metricKey", kMetricKeyProp[i]);
        auto* iconInLay = new QVBoxLayout(iconWrap);
        iconInLay->setContentsMargins(0, 0, 0, 0);
        iconInLay->setSpacing(0);
        iconInLay->addStretch(1);
        auto* icoL = new QLabel(iconWrap);
        icoL->setFixedSize(22, 22);
        icoL->setPixmap(
            QIcon(kMetricResPaths[i]).pixmap(22, 22, QIcon::Normal, QIcon::Off));
        icoL->setObjectName(QStringLiteral("aggregateRightBarMetricIcon"));
        iconInLay->addWidget(icoL, 0, Qt::AlignHCenter);
        iconInLay->addStretch(1);

        m_rightBarMetricValues[i] = new QLabel(QStringLiteral("—"), card);
        m_rightBarMetricValues[i]->setObjectName(QStringLiteral("aggregateRightBarMetricValue"));
        m_rightBarMetricValues[i]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* capL = new QLabel(metricCap[i], card);
        capL->setObjectName(QStringLiteral("aggregateRightBarMetricCaption"));
        capL->setWordWrap(true);
        cv->addWidget(iconWrap, 0, Qt::AlignLeft);
        cv->addWidget(m_rightBarMetricValues[i]);
        cv->addWidget(capL);
        grid2->addWidget(card, i / 2, i % 2);
    }
    metLay->addLayout(grid2);
    m_btnModelSwitchRow = new QPushButton(metricsWrap);
    m_btnModelSwitchRow->setObjectName(QStringLiteral("aggregateModelSwitchRowButton"));
    m_btnModelSwitchRow->setCursor(Qt::PointingHandCursor);
    m_btnModelSwitchRow->setFocusPolicy(Qt::NoFocus);
    m_btnModelSwitchRow->setContextMenuPolicy(Qt::CustomContextMenu);
    m_btnModelSwitchRow->setToolTip(QStringLiteral("右键切换展示模型"));
    connect(m_btnModelSwitchRow, &QPushButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_btnModelSwitchRow)
            return;
        const QList<AiPresetDefinition> presets = aiPresetDefinitions();
        if (presets.isEmpty())
            return;

        QMenu menu(m_btnModelSwitchRow);
        for (int i = 0; i < presets.size(); ++i) {
            QAction* action = menu.addAction(presets[i].label);
            action->setCheckable(true);
            action->setChecked(i == m_rightBarDisplayModelIndex);
            action->setData(i);
        }

        QAction* chosen = menu.exec(m_btnModelSwitchRow->mapToGlobal(pos));
        if (!chosen)
            return;
        const int row = chosen->data().toInt();
        if (row < 0 || row >= presets.size() || row == m_rightBarDisplayModelIndex)
            return;
        m_rightBarDisplayModelIndex = row;
        refreshRightBarModelDisplay();
    });
    metLay->addWidget(m_btnModelSwitchRow);
    m_rightBarVLayout->addWidget(metricsWrap);

    auto* sep2 = new QFrame(m_rightBarScrollContent);
    sep2->setObjectName(QStringLiteral("aggregateRightBarBlockSep"));
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFixedHeight(1);
    sep2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_rightBarVLayout->addWidget(sep2);

    m_customerProfileSection = new QWidget(m_rightBarScrollContent);
    m_customerProfileSection->setObjectName(QStringLiteral("aggregateRightBarCustomerProfileSection"));
    m_customerProfileSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* profileOuterLay = new QVBoxLayout(m_customerProfileSection);
    profileOuterLay->setContentsMargins(0, 0, 0, 0);
    profileOuterLay->setSpacing(4);

    auto* profileHeaderRow = new QWidget(m_customerProfileSection);
    auto* profileHeaderLay = new QHBoxLayout(profileHeaderRow);
    profileHeaderLay->setContentsMargins(0, 0, 0, 0);
    profileHeaderLay->setSpacing(4);
    m_customerProfileToggle = new QToolButton(profileHeaderRow);
    m_customerProfileToggle->setObjectName(QStringLiteral("aggregateCustomerProfileToggle"));
    m_customerProfileToggle->setText(QStringLiteral("客户信息"));
    m_customerProfileToggle->setCheckable(true);
    m_customerProfileToggle->setChecked(true);
    m_customerProfileToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_customerProfileToggle->setArrowType(Qt::DownArrow);
    m_customerProfileToggle->setAutoRaise(true);
    m_customerProfileToggle->setCursor(Qt::PointingHandCursor);
    m_customerProfileToggle->setFocusPolicy(Qt::NoFocus);
    m_customerProfileToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_btnOrganizeCustomerProfile = new QPushButton(QStringLiteral("整理"), profileHeaderRow);
    m_btnOrganizeCustomerProfile->setObjectName(QStringLiteral("aggregateCustomerProfileOrganizeButton"));
    m_btnOrganizeCustomerProfile->setCursor(Qt::PointingHandCursor);
    m_btnOrganizeCustomerProfile->setFixedSize(76, 28);
    connect(m_btnOrganizeCustomerProfile, &QPushButton::clicked,
            this, &AggregateChatForm::onOrganizeCustomerProfileClicked);
    profileHeaderLay->addWidget(m_customerProfileToggle, 1);
    profileHeaderLay->addWidget(m_btnOrganizeCustomerProfile, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_customerProfileBody = new QWidget(m_customerProfileSection);
    auto* profileBodyLay = new QVBoxLayout(m_customerProfileBody);
    profileBodyLay->setContentsMargins(0, 0, 0, 0);
    profileBodyLay->setSpacing(0);
    m_customerProfileText = new QLabel(QStringLiteral("暂无客户信息"), m_customerProfileBody);
    m_customerProfileText->setObjectName(QStringLiteral("aggregateCustomerProfileText"));
    m_customerProfileText->setWordWrap(true);
    m_customerProfileText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    profileBodyLay->addWidget(m_customerProfileText);
    connect(m_customerProfileToggle, &QToolButton::toggled, this, [this](bool expanded) {
        if (m_customerProfileBody)
            m_customerProfileBody->setVisible(expanded);
        if (m_customerProfileToggle)
            m_customerProfileToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        updateRightBarSendSectionStretch();
    });

    profileOuterLay->addWidget(profileHeaderRow, 0);
    profileOuterLay->addWidget(m_customerProfileBody, 0);
    m_rightBarVLayout->addWidget(m_customerProfileSection, 0);

    auto* sepProfile = new QFrame(m_rightBarScrollContent);
    sepProfile->setObjectName(QStringLiteral("aggregateRightBarBlockSep"));
    sepProfile->setFrameShape(QFrame::HLine);
    sepProfile->setFixedHeight(1);
    sepProfile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_rightBarVLayout->addWidget(sepProfile);

    m_rightBarSendStatusSection = new QWidget(m_rightBarScrollContent);
    m_rightBarSendStatusSection->setObjectName(QStringLiteral("aggregateRightBarSendStatusSection"));
    m_rightBarSendStatusSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* sendStatusOuterLay = new QVBoxLayout(m_rightBarSendStatusSection);
    sendStatusOuterLay->setContentsMargins(0, 0, 0, 0);
    sendStatusOuterLay->setSpacing(3);

    auto* sendTimelineHeaderRow = new QWidget(m_rightBarSendStatusSection);
    sendTimelineHeaderRow->setObjectName(QStringLiteral("aggregateSendTimelineHeaderRow"));
    sendTimelineHeaderRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* sendTimelineHeaderLay = new QHBoxLayout(sendTimelineHeaderRow);
    sendTimelineHeaderLay->setContentsMargins(0, 0, 0, 0);
    sendTimelineHeaderLay->setSpacing(4);

    m_sendTimelineToggle = new QToolButton(sendTimelineHeaderRow);
    m_sendTimelineToggle->setObjectName(QStringLiteral("aggregateSendTimelineToggle"));
    m_sendTimelineToggle->setText(QStringLiteral("处理动态"));
    m_sendTimelineToggle->setCheckable(true);
    m_sendTimelineToggle->setChecked(false);
    m_sendTimelineToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sendTimelineToggle->setArrowType(Qt::RightArrow);
    m_sendTimelineToggle->setAutoRaise(true);
    m_sendTimelineToggle->setCursor(Qt::PointingHandCursor);
    m_sendTimelineToggle->setFocusPolicy(Qt::NoFocus);
    m_sendTimelineToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_btnClearSendTimeline = new QToolButton(sendTimelineHeaderRow);
    m_btnClearSendTimeline->setObjectName(QStringLiteral("sendTimelineClearBtn"));
    m_btnClearSendTimeline->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_btnClearSendTimeline->setAutoRaise(true);
    m_btnClearSendTimeline->setCursor(Qt::PointingHandCursor);
    m_btnClearSendTimeline->setFocusPolicy(Qt::NoFocus);
    m_btnClearSendTimeline->setFixedSize(28, 28);
    m_btnClearSendTimeline->setIconSize(QSize(20, 20));
    m_btnClearSendTimeline->setToolTip(QStringLiteral("清空处理动态显示"));
    m_btnClearSendTimeline->setAccessibleName(QStringLiteral("清空显示"));
    {
        const QString n = QStringLiteral(":/aggregate_reception_icons/clear_output_normal_icon.svg");
        const QString s = QStringLiteral(":/aggregate_reception_icons/clear_output_selected_icon.svg");
        QIcon clearOutIcon;
        clearOutIcon.addFile(n, QSize(20, 20), QIcon::Normal, QIcon::Off);
        clearOutIcon.addFile(s, QSize(20, 20), QIcon::Active, QIcon::Off);
        clearOutIcon.addFile(s, QSize(20, 20), QIcon::Selected, QIcon::Off);
        m_btnClearSendTimeline->setIcon(clearOutIcon);
    }
    sendTimelineHeaderLay->addWidget(m_sendTimelineToggle, 1);
    sendTimelineHeaderLay->addWidget(m_btnClearSendTimeline, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_sendTimelineBody = new QWidget(m_rightBarSendStatusSection);
    m_sendTimelineBody->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* bodyLayout = new QVBoxLayout(m_sendTimelineBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    m_sendTimeline = new QPlainTextEdit(m_sendTimelineBody);
    m_sendTimeline->setObjectName(QStringLiteral("sendStatusTimeline"));
    m_sendTimeline->setReadOnly(true);
    m_sendTimeline->setMinimumHeight(120);
    bodyLayout->addWidget(m_sendTimeline, 1);

    connect(m_sendTimelineToggle, &QToolButton::toggled, this, [this](bool expanded) {
        if (m_sendTimelineBody)
            m_sendTimelineBody->setVisible(expanded);
        if (m_sendTimelineToggle)
            m_sendTimelineToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        updateRightBarSendSectionStretch();
        if (expanded)
            pollSendTimeline();
    });
    m_sendTimelineBody->setVisible(false);

    sendStatusOuterLay->addWidget(sendTimelineHeaderRow, 0);
    sendStatusOuterLay->addWidget(m_sendTimelineBody, 0);
    m_rightBarVLayout->addWidget(m_rightBarSendStatusSection, 0);
    m_rightBarBottomSpacer = new QWidget(m_rightBarScrollContent);
    m_rightBarBottomSpacer->setObjectName(QStringLiteral("aggregateRightBarBottomSpacer"));
    m_rightBarBottomSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rightBarVLayout->addWidget(m_rightBarBottomSpacer, 1);
    updateRightBarSendSectionStretch();
    m_rightStack->addWidget(m_customerDetail);

    m_rightStack->setCurrentWidget(m_customerDetail);
    refreshRightBarModelDisplay();
    refreshRightBarMetrics();
    refreshCustomerProfilePanel();
    return panel;
}

void AggregateChatForm::syncConversationItemVisualState()
{
    if (!m_conversationList)
        return;
    m_conversationList->viewport()->update();
}

void AggregateChatForm::syncSolidBackgrounds()
{
    const QColor leftBg(QStringLiteral("#EEF1F6"));
    const QColor centerBg(QStringLiteral("#F5F6F7"));
    const QColor rightBg(QStringLiteral("#F7F9FB"));
    const QColor inputBg(QStringLiteral("#FFFFFF"));

    if (m_leftToolBar)
        applySolidBackground(m_leftToolBar, leftBg);
    if (m_hSplitter)
        applySolidBackground(m_hSplitter, leftBg);
    applySolidBackground(m_leftPanel, leftBg);
    applySolidBackground(m_centerPanel, centerBg);
    applySolidBackground(m_rightPanel, rightBg);
    applySolidBackground(m_leftStack, leftBg);
    applySolidBackground(m_centerStack, centerBg);
    applySolidBackground(m_rightStack, rightBg);
    applySolidBackground(m_centerEmptyState, centerBg);
    applySolidBackground(m_chatArea, centerBg);
    applySolidBackground(m_chatInputOverlayHost, centerBg);
    applySolidBackground(m_chatInputPanel, inputBg);
    if (m_messageView) {
        applySolidBackground(m_messageView, centerBg);
        applySolidBackground(m_messageView->viewport(), centerBg);
    }
    if (m_conversationList) {
        applySolidBackground(m_conversationList, leftBg);
        applySolidBackground(m_conversationList->viewport(), leftBg);
    }
    applySolidBackground(m_customerDetail, rightBg);
    if (m_rightBarScroll) {
        applySolidBackground(m_rightBarScroll, rightBg);
        applySolidBackground(m_rightBarScroll->viewport(), rightBg);
    }
    if (m_rightBarScrollContent)
        applySolidBackground(m_rightBarScrollContent, rightBg);
    if (m_rightBarSendStatusSection)
        applySolidBackground(m_rightBarSendStatusSection, rightBg);
    if (m_rightBarBottomSpacer)
        applySolidBackground(m_rightBarBottomSpacer, rightBg);
    applySolidBackground(m_modelPickerSheet, rightBg);
    if (m_modelPickerList) {
        applySolidBackground(m_modelPickerList, rightBg);
        applySolidBackground(m_modelPickerList->viewport(), rightBg);
    }
}

void AggregateChatForm::updateRightBarSendSectionStretch()
{
    if (!m_rightBarVLayout || !m_rightBarSendStatusSection || !m_rightBarBottomSpacer)
        return;
    const bool expanded = m_sendTimelineBody && m_sendTimelineBody->isVisible();
    const int iSec = m_rightBarVLayout->indexOf(m_rightBarSendStatusSection);
    const int iSp = m_rightBarVLayout->indexOf(m_rightBarBottomSpacer);
    if (iSec >= 0)
        m_rightBarVLayout->setStretch(iSec, expanded ? 1 : 0);
    if (iSp >= 0)
        m_rightBarVLayout->setStretch(iSp, expanded ? 0 : 1);
}

void AggregateChatForm::refreshRightBarModelDisplay()
{
    if (!m_rightBarModelNameLabel)
        return;
    const QList<AiPresetDefinition> presets = aiPresetDefinitions();
    if (presets.isEmpty())
        return;
    m_rightBarDisplayModelIndex = qBound(0, m_rightBarDisplayModelIndex, presets.size() - 1);
    const AiPresetDefinition& d = presets[m_rightBarDisplayModelIndex];
    m_rightBarModelNameLabel->setText(d.label);
    if (m_btnModelSwitchRow)
        m_btnModelSwitchRow->setText(d.label);
    if (m_rightBarModelIconLabel) {
        QPixmap ap;
        if (d.assistantAvatarResource.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
            ap = svgResourcePixmapFittedInSquare(d.assistantAvatarResource,
                                                 kAggregateRightBarModelIconDrawSide);
        else
            ap = rasterResourcePixmapFittedInSquare(d.assistantAvatarResource,
                                                    kAggregateRightBarModelIconDrawSide);
        if (!ap.isNull())
            m_rightBarModelIconLabel->setPixmap(ap);
        else
            m_rightBarModelIconLabel->setPixmap(
                qApp->style()
                    ->standardIcon(QStyle::SP_FileDialogListView)
                    .pixmap(kAggregateRightBarModelIconDrawSide, kAggregateRightBarModelIconDrawSide));
    }
    if (m_modelPickerList)
        m_modelPickerList->setCurrentRow(m_rightBarDisplayModelIndex);
}

void AggregateChatForm::refreshRightBarMetrics()
{
    if (!m_rightBarMetricValues[0])
        return;

    const AiRequestEventMetrics metrics =
        AiRequestEventDao().aggregateMetrics(m_aggregateAiSessionModelKey);

    m_rightBarMetricValues[0]->setText(QStringLiteral("今日%1").arg(metrics.todayRequestCount));
    m_rightBarMetricValues[1]->setText(metrics.hasSuccessRate
                                           ? QStringLiteral("%1%").arg(metrics.successRatePercent)
                                           : QStringLiteral("—"));

    AiConfigLoadOptions loadOptions;
    loadOptions.allowAggregateFallback = true;
    loadOptions.allowGeneralFallback = true;
    const AiProviderConfig config = loadAiProviderConfig(m_aggregateAiSessionModelKey, loadOptions);
    QString health = QStringLiteral("未验证");
    if (config.apiKey.trimmed().isEmpty()
        || config.baseUrl.trimmed().isEmpty()
        || config.model.trimmed().isEmpty()) {
        health = QStringLiteral("未配置");
    } else if (metrics.latestStatus == QLatin1String("completed")) {
        health = QStringLiteral("可用");
    } else if (metrics.latestStatus == QLatin1String("failed")) {
        health = QStringLiteral("异常");
    }
    m_rightBarMetricValues[2]->setText(health);
    if (!metrics.latestError.trimmed().isEmpty())
        m_rightBarMetricValues[2]->setToolTip(metrics.latestError.left(200));
    else
        m_rightBarMetricValues[2]->setToolTip(QString());

    m_rightBarMetricValues[3]->setText(metrics.hasAverageDuration
                                           ? aggregateMetricDurationLabel(metrics.averageDurationMs)
                                           : QStringLiteral("—"));
}

void AggregateChatForm::refreshCustomerProfilePanel()
{
    if (!m_customerProfileText)
        return;

    const auto record = CustomerProfileDao().findByConversationId(m_currentConvId);
    if (record && !record->profile.isEmpty())
        m_customerProfileText->setText(customerProfileDisplayText(record->profile, record->updatedAt));
    else
        m_customerProfileText->setText(QStringLiteral("暂无客户信息"));

    if (m_btnOrganizeCustomerProfile) {
        m_btnOrganizeCustomerProfile->setText(m_customerProfileBusy
                                                  ? QStringLiteral("整理中")
                                                  : (record ? QStringLiteral("重新整理")
                                                            : QStringLiteral("整理")));
        m_btnOrganizeCustomerProfile->setEnabled(m_currentConvId > 0
                                                 && !m_customerProfileBusy
                                                 && !m_aggregateAiGenerating
                                                 && !m_autoReplyBusy);
    }
}

void AggregateChatForm::setCustomerProfileBusy(bool busy)
{
    m_customerProfileBusy = busy;
    if (m_btnOrganizeCustomerProfile) {
        m_btnOrganizeCustomerProfile->setText(busy ? QStringLiteral("整理中")
                                                   : QStringLiteral("整理"));
    }
    updateAggregateAiControlsVisibility();
    refreshCustomerProfilePanel();
}

QJsonObject AggregateChatForm::parseCustomerProfileJson(const QString& text) const
{
    QString raw = text.trimmed();
    raw.remove(QStringLiteral("```json"), Qt::CaseInsensitive);
    raw.remove(QStringLiteral("```"));
    const int firstBrace = raw.indexOf(QLatin1Char('{'));
    const int lastBrace = raw.lastIndexOf(QLatin1Char('}'));
    if (firstBrace >= 0 && lastBrace > firstBrace)
        raw = raw.mid(firstBrace, lastBrace - firstBrace + 1);

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject())
        return doc.object();

    QJsonObject fallback;
    fallback.insert(QStringLiteral("summary"), text.trimmed().left(300));
    fallback.insert(QStringLiteral("concerns"), QJsonArray());
    fallback.insert(QStringLiteral("preferences"), QJsonArray());
    fallback.insert(QStringLiteral("risks"), QJsonArray());
    fallback.insert(QStringLiteral("current_need"), QString());
    return fallback;
}

void AggregateChatForm::openModelPickerSheet()
{
    if (!m_modelPickerOverlay || !m_modelPickerSheet)
        return;
    m_modelPickerOverlay->show();
    m_modelPickerOverlay->raise();
    m_modelPickerSheet->setFixedWidth(0);
    if (m_modelSheetWidthAnim) {
        m_modelSheetWidthAnim->stop();
        m_modelSheetWidthAnim->deleteLater();
        m_modelSheetWidthAnim = nullptr;
    }
    if (m_modelPickerList) {
        m_modelPickerList->setCurrentRow(m_rightBarDisplayModelIndex);
    }
    QTimer::singleShot(0, this, [this]() {
        if (!m_modelPickerOverlay || !m_modelPickerSheet)
            return;
        int w = m_modelPickerOverlay->width();
        if (w < 1)
            w = 220;
        startModelSheetWidthAnimation(0, w, false);
    });
}

void AggregateChatForm::closeModelPickerSheet()
{
    if (!m_modelPickerSheet) {
        if (m_modelPickerOverlay)
            m_modelPickerOverlay->hide();
        return;
    }
    if (m_modelPickerSheet->width() <= 0) {
        if (m_modelPickerOverlay)
            m_modelPickerOverlay->hide();
        return;
    }
    startModelSheetWidthAnimation(m_modelPickerSheet->width(), 0, true);
}

void AggregateChatForm::startModelSheetWidthAnimation(int fromWidth, int toWidth, bool hideOverlayOnFinish)
{
    if (!m_modelPickerSheet)
        return;
    if (m_modelSheetWidthAnim) {
        m_modelSheetWidthAnim->stop();
        m_modelSheetWidthAnim->deleteLater();
        m_modelSheetWidthAnim = nullptr;
    }
    m_modelPickerSheet->setFixedWidth(fromWidth);
    auto* a = new QVariantAnimation(this);
    a->setDuration(220);
    a->setEasingCurve(QEasingCurve::OutCubic);
    a->setStartValue(fromWidth);
    a->setEndValue(toWidth);
    m_modelSheetWidthAnim = a;
    connect(
        a, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& v) {
            if (m_modelPickerSheet)
                m_modelPickerSheet->setFixedWidth(v.toInt());
        });
    connect(
        a, &QVariantAnimation::finished, this, [this, a, hideOverlayOnFinish]() {
            m_modelSheetWidthAnim = nullptr;
            if (hideOverlayOnFinish && m_modelPickerOverlay)
                m_modelPickerOverlay->hide();
            a->deleteLater();
        });
    a->start();
}

void AggregateChatForm::onModelPickerBackClicked()
{
    closeModelPickerSheet();
}

void AggregateChatForm::onModelPickerListItem(QListWidgetItem* item)
{
    if (!item || !m_modelPickerList)
        return;
    const int row = m_modelPickerList->row(item);
    if (row < 0 || row >= aiPresetDefinitions().size())
        return;
    m_rightBarDisplayModelIndex = row;
    refreshRightBarModelDisplay();
    closeModelPickerSheet();
}

void AggregateChatForm::onMessageStatusChanged(int conversationId, int messageId,
                                               Models::MessageStatus newStatus,
                                               const QString& errorReason)
{
    if (conversationId != m_currentConvId)
        return;

    if (!m_messageListModel)
        return;

    if (m_messageListModel->updateMessageStatus(messageId, newStatus, errorReason)) {
        m_currentMessageSignature = m_messageListModel->signature();
        qDebug() << "[AggregateChatForm] 消息状态已更新 msgId=" << messageId
                 << "status=" << Models::toString(newStatus);

        if (newStatus == Models::MessageStatus::Failed && !errorReason.isEmpty()) {
            showStatusMessage(QStringLiteral("发送失败: %1").arg(errorReason), 5000);
        }
    }
}

// ===================== Message Bubble =====================

QWidget* AggregateChatForm::createBubble(const MessageRecord& msg)
{
    const QString text = msg.content;
    const QString senderName = msg.senderName;
    const QDateTime time = msg.createdAt;
    const bool isOutgoing = (msg.direction == QLatin1String("out"));
    const QString originalTimestamp = msg.originalTimestamp;
    auto* row = new QWidget();
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(8);

    QString timeStr;
    if (!isOutgoing && !originalTimestamp.isEmpty()) {
        timeStr = originalTimestamp;
    } else if (isOutgoing && time.isValid()) {
        timeStr = time.toString(QStringLiteral("HH:mm"));
    }

    if (isOutgoing) {
        rowLayout->addStretch(1);

        auto* rightCol = new QWidget(row);
        auto* rightColLayout = new QVBoxLayout(rightCol);
        rightColLayout->setContentsMargins(0, 0, 0, 0);
        rightColLayout->setSpacing(4);

        auto* metaRow = new QWidget(rightCol);
        auto* metaRowLayout = new QHBoxLayout(metaRow);
        metaRowLayout->setContentsMargins(0, 0, 0, 0);
        metaRowLayout->setSpacing(8);
        metaRowLayout->addStretch(1);
        auto* timeAbove = new QLabel(timeStr, metaRow);
        timeAbove->setObjectName(QStringLiteral("bubbleOutSenderTime"));
        timeAbove->setVisible(!timeStr.isEmpty());
        metaRowLayout->addWidget(timeAbove, 0, Qt::AlignVCenter);
        auto* nickAbove = new QLabel(m_selfDisplayName, metaRow);
        nickAbove->setObjectName(QStringLiteral("bubbleOutSenderNick"));
        nickAbove->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        metaRowLayout->addWidget(nickAbove, 0, Qt::AlignVCenter);
        rightColLayout->addWidget(metaRow);

        auto* bubbleRow = new QWidget(rightCol);
        auto* bubbleRowLayout = new QHBoxLayout(bubbleRow);
        bubbleRowLayout->setContentsMargins(0, 0, 0, 0);
        bubbleRowLayout->setSpacing(0);
        bubbleRowLayout->addStretch(1);

        auto* bubble = new QFrame(bubbleRow);
        bubble->setObjectName(QStringLiteral("bubbleOut"));
        auto* bubbleLayout = new QVBoxLayout(bubble);
        bubbleLayout->setContentsMargins(12, 8, 12, 8);
        bubbleLayout->setSpacing(4);

        auto* contentLabel = new QLabel(text, bubble);
        contentLabel->setWordWrap(true);
        contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        contentLabel->setObjectName(QStringLiteral("bubbleTextOut"));
        bubbleLayout->addWidget(contentLabel);

        QString statusText = QStringLiteral("已发送");
        if (msg.syncStatus == 10)
            statusText = QStringLiteral("待发送");
        else if (msg.syncStatus == 12)
            statusText = QStringLiteral("发送失败");
        else if (msg.syncStatus == 11 || msg.syncStatus == 1)
            statusText = QStringLiteral("已发送");

        auto* statusLabel = new QLabel(statusText, bubble);
        statusLabel->setObjectName(QStringLiteral("bubbleMetaOut"));
        bubbleLayout->addWidget(statusLabel);

        if (msg.syncStatus == 12 && !msg.errorReason.isEmpty()) {
            auto* errorLabel = new QLabel(QStringLiteral("原因: %1").arg(msg.errorReason), bubble);
            errorLabel->setObjectName(QStringLiteral("bubbleMetaOut"));
            errorLabel->setWordWrap(true);
            bubbleLayout->addWidget(errorLabel);
        }

        bubble->setMaximumWidth(420);
        bubbleRowLayout->addWidget(bubble, 0, Qt::AlignTop);
        rightColLayout->addWidget(bubbleRow);

        rowLayout->addWidget(rightCol, 0, Qt::AlignTop);

        auto* avatarLabel = new QLabel(row);
        avatarLabel->setObjectName(QStringLiteral("bubbleOutAvatar"));
        avatarLabel->setFixedSize(36, 36);
        avatarLabel->setScaledContents(false);
        if (!m_selfAvatarPixmap.isNull())
            avatarLabel->setPixmap(m_selfAvatarPixmap);
        rowLayout->addWidget(avatarLabel, 0, Qt::AlignTop);

        return row;
    }

    // 对方消息：左侧通用头像，与右侧己方气泡布局对称；仅在存在 senderName / originalTimestamp 时展示元信息。
    // 微信 RPA：originalTimestamp 为入库/解析时刻；千牛常为 OCR 名称与时间。
    auto* avatarIn = new QLabel(row);
    avatarIn->setObjectName(QStringLiteral("bubbleInAvatar"));
    avatarIn->setFixedSize(36, 36);
    avatarIn->setScaledContents(false);
    if (!m_customerDefaultAvatarPixmap.isNull())
        avatarIn->setPixmap(m_customerDefaultAvatarPixmap);
    rowLayout->addWidget(avatarIn, 0, Qt::AlignTop);

    auto* col = new QWidget(row);
    auto* colLayout = new QVBoxLayout(col);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(4);

    QString metaAbove;
    if (!senderName.isEmpty() && !timeStr.isEmpty())
        metaAbove = senderName + QStringLiteral("  ") + timeStr;
    else if (!senderName.isEmpty())
        metaAbove = senderName;
    else if (!timeStr.isEmpty())
        metaAbove = timeStr;

    if (!metaAbove.isEmpty()) {
        auto* metaLabel = new QLabel(metaAbove, col);
        metaLabel->setObjectName(QStringLiteral("bubbleMetaIn"));
        metaLabel->setWordWrap(false);
        metaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        colLayout->addWidget(metaLabel);
    }

    auto* bubble = new QFrame(col);
    bubble->setObjectName(QStringLiteral("bubbleIn"));
    auto* bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 8, 12, 8);
    bubbleLayout->setSpacing(4);

    if (!msg.contentImagePath.isEmpty() && QFile::exists(msg.contentImagePath)) {
        QPixmap pm(msg.contentImagePath);
        if (!pm.isNull()) {
            const int maxW = 360;
            if (pm.width() > maxW)
                pm = pm.scaledToWidth(maxW, Qt::SmoothTransformation);
            auto* imgLabel = new QLabel(bubble);
            imgLabel->setPixmap(pm);
            imgLabel->setObjectName(QStringLiteral("bubbleImageIn"));
            bubbleLayout->addWidget(imgLabel);
        }
    }

    auto* contentLabel = new QLabel(text, bubble);
    contentLabel->setWordWrap(true);
    contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLabel->setObjectName(QStringLiteral("bubbleTextIn"));
    bubbleLayout->addWidget(contentLabel);

    bubble->setMaximumWidth(420);
    colLayout->addWidget(bubble);

    rowLayout->addWidget(col);
    rowLayout->addStretch(1);

    return row;
}

// ===================== Date Separator =====================

QWidget* AggregateChatForm::createDateSeparator(const QDate& date)
{
    auto* row = new QWidget();
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(40, 10, 40, 6);
    layout->setSpacing(10);

    auto* leftLine = new QFrame(row);
    leftLine->setFixedHeight(1);
    leftLine->setObjectName("dateSeparatorLine");

    QString dateText;
    if (date == QDate::currentDate())
        dateText = QStringLiteral("今天");
    else if (date == QDate::currentDate().addDays(-1))
        dateText = QStringLiteral("昨天");
    else
        dateText = date.toString(QStringLiteral("yyyy年M月d日"));

    auto* label = new QLabel(dateText, row);
    label->setObjectName("dateSeparatorText");
    label->setAlignment(Qt::AlignCenter);

    auto* rightLine = new QFrame(row);
    rightLine->setFixedHeight(1);
    rightLine->setObjectName("dateSeparatorLine");

    layout->addWidget(leftLine, 1);
    layout->addWidget(label);
    layout->addWidget(rightLine, 1);

    return row;
}

// ===================== Data Operations =====================

bool AggregateChatForm::handleComposeMimeData(const QMimeData* mimeData)
{
    if (!mimeData)
        return false;

    bool addedAttachment = false;
    if (mimeData->hasUrls()) {
        for (const QUrl& url : mimeData->urls()) {
            if (!url.isLocalFile())
                continue;
            addedAttachment = addComposeFileAttachment(url.toLocalFile()) || addedAttachment;
        }
    }

    if (!addedAttachment && mimeData->hasImage()) {
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (image.isNull()) {
            const QPixmap pixmap = qvariant_cast<QPixmap>(mimeData->imageData());
            if (!pixmap.isNull())
                image = pixmap.toImage();
        }
        const QString path = saveComposeImageToCache(image);
        if (!path.isEmpty())
            addedAttachment = addComposeFileAttachment(path) || addedAttachment;
    }

    if (!addedAttachment)
        return false;

    const QString text = mimeData->text();
    if (!text.trimmed().isEmpty() && m_inputEdit) {
        const bool looksLikeFileUrlList = text.trimmed().startsWith(QStringLiteral("file:/"));
        if (!looksLikeFileUrlList)
            m_inputEdit->textCursor().insertText(text);
    }

    if (m_draftSaveTimer && m_currentConvId > 0)
        m_draftSaveTimer->start();
    scheduleChatInputRelayout();
    return true;
}

bool AggregateChatForm::addComposeFileAttachment(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return false;

    OutgoingMessagePart part;
    if (isImageFilePath(path))
        part.type = OutgoingPartType::Image;
    else if (isVideoFilePath(path))
        part.type = OutgoingPartType::Video;
    else
        part.type = OutgoingPartType::File;
    part.localPath = info.absoluteFilePath();
    part.fileName = info.fileName();
    part.sizeBytes = info.size();
    part.mimeType = QMimeDatabase().mimeTypeForFile(info).name();

    for (const OutgoingMessagePart& existing : std::as_const(m_composeAttachments)) {
        if (QFileInfo(existing.localPath).absoluteFilePath() == part.localPath)
            return true;
    }

    m_composeAttachments.push_back(part);
    refreshComposeAttachments();
    return true;
}

void AggregateChatForm::refreshComposeAttachments()
{
    if (!m_composeAttachmentsLayout || !m_composeAttachmentsScroll)
        return;

    while (QLayoutItem* item = m_composeAttachmentsLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    for (int i = 0; i < m_composeAttachments.size(); ++i) {
        const OutgoingMessagePart part = m_composeAttachments.at(i);
        auto* card = new QFrame(m_composeAttachmentsWidget);
        card->setObjectName(QStringLiteral("composeAttachmentCard"));
        card->setFixedSize(190, 64);
        card->setStyleSheet(QStringLiteral(
            "QFrame#composeAttachmentCard{background:#F8FAFC;border:1px solid #D9E2EC;border-radius:6px;}"));
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(8, 6, 6, 6);
        row->setSpacing(8);

        auto* thumb = new QLabel(card);
        thumb->setFixedSize(48, 48);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setStyleSheet(QStringLiteral("background:#E5E7EB;border-radius:4px;color:#334155;font-size:12px;"));
        if (part.type == OutgoingPartType::Image) {
            QPixmap pixmap(part.localPath);
            if (!pixmap.isNull())
                thumb->setPixmap(pixmap.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            else
                thumb->setText(QStringLiteral("图片"));
        } else {
            thumb->setText(part.type == OutgoingPartType::Video ? QStringLiteral("视频") : QStringLiteral("文件"));
        }
        row->addWidget(thumb);

        auto* textBox = new QWidget(card);
        textBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* textLayout = new QVBoxLayout(textBox);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);
        auto* title = new QLabel(part.fileName.isEmpty() ? QFileInfo(part.localPath).fileName() : part.fileName, textBox);
        title->setStyleSheet(QStringLiteral("color:#0F172A;font-size:12px;"));
        title->setMinimumWidth(0);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        title->setTextFormat(Qt::PlainText);
        title->setWordWrap(false);
        title->setTextInteractionFlags(Qt::TextSelectableByMouse);
        title->setToolTip(part.localPath);
        auto* detail = new QLabel(readableFileSize(part.sizeBytes), textBox);
        detail->setStyleSheet(QStringLiteral("color:#64748B;font-size:11px;"));
        detail->setMinimumWidth(0);
        detail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        textLayout->addWidget(title);
        textLayout->addWidget(detail);
        row->addWidget(textBox, 1);

        auto* removeBtn = new QToolButton(card);
        removeBtn->setText(QStringLiteral("×"));
        removeBtn->setToolTip(QStringLiteral("删除附件"));
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setAutoRaise(false);
        removeBtn->setFixedSize(22, 22);
        removeBtn->setStyleSheet(QStringLiteral(
            "QToolButton{"
            "background:#E2E8F0;"
            "border:1px solid #CBD5E1;"
            "border-radius:11px;"
            "color:#334155;"
            "font-size:14px;"
            "font-weight:700;"
            "padding:0;"
            "}"
            "QToolButton:hover{background:#FEE2E2;border-color:#FCA5A5;color:#B91C1C;}"
            "QToolButton:pressed{background:#FECACA;}"));
        connect(removeBtn, &QToolButton::clicked, this, [this, i]() {
            if (i >= 0 && i < m_composeAttachments.size()) {
                m_composeAttachments.removeAt(i);
                refreshComposeAttachments();
                if (m_draftSaveTimer && m_currentConvId > 0)
                    m_draftSaveTimer->start();
                QTimer::singleShot(0, this, &AggregateChatForm::relayoutChatInputOverlay);
            }
        });
        row->addWidget(removeBtn, 0, Qt::AlignTop);

        m_composeAttachmentsLayout->addWidget(card);
    }
    m_composeAttachmentsLayout->addStretch(1);
    m_composeAttachmentsScroll->setVisible(!m_composeAttachments.isEmpty());
    scheduleChatInputRelayout();
}

void AggregateChatForm::clearComposeAttachments()
{
    m_composeAttachments.clear();
    refreshComposeAttachments();
}

void AggregateChatForm::setSuggestedImageAttachments(const QVector<SuggestedImageAttachment>& suggestions)
{
    m_suggestedImageAttachments = suggestions;
    m_selectedSuggestedImagePath.clear();
    refreshSuggestedImagePanel();
}

void AggregateChatForm::clearSuggestedImageAttachments()
{
    if (m_suggestedImageAttachments.isEmpty() && m_selectedSuggestedImagePath.isEmpty()) {
        if (m_suggestedImagePanel && m_suggestedImagePanel->isVisible()) {
            m_suggestedImagePanel->setVisible(false);
            scheduleChatInputRelayout();
        }
        return;
    }
    m_suggestedImageAttachments.clear();
    m_selectedSuggestedImagePath.clear();
    refreshSuggestedImagePanel();
}

void AggregateChatForm::refreshSuggestedImagePanel()
{
    if (!m_suggestedImagePanel || !m_suggestedImageLayout)
        return;

    while (QLayoutItem* item = m_suggestedImageLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    const int visibleCount = qMin(2, int(m_suggestedImageAttachments.size()));
    for (int i = 0; i < visibleCount; ++i) {
        const SuggestedImageAttachment item = m_suggestedImageAttachments.at(i);
        auto* card = new QFrame(m_suggestedImagePanel);
        card->setObjectName(QStringLiteral("aggregateSuggestedImageCard"));
        card->setMinimumWidth(0);
        card->setFixedHeight(86);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        card->setStyleSheet(QStringLiteral(
            "QFrame#aggregateSuggestedImageCard{"
            "background:#FFFFFF;border:1px solid #D9E2EC;border-radius:6px;"
            "}"));

        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(8, 6, 8, 6);
        row->setSpacing(8);

        auto* thumb = new QLabel(card);
        thumb->setFixedSize(52, 52);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setStyleSheet(QStringLiteral("background:#E5E7EB;border-radius:4px;color:#334155;font-size:11px;"));
        QPixmap pixmap(item.filePath);
        if (!pixmap.isNull())
            thumb->setPixmap(pixmap.scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else
            thumb->setText(QStringLiteral("图片"));
        row->addWidget(thumb, 0, Qt::AlignVCenter);

        auto* textBox = new QWidget(card);
        textBox->setMinimumWidth(0);
        textBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* textLayout = new QVBoxLayout(textBox);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);

        auto* title = new QLabel(item.title, textBox);
        title->setMinimumWidth(0);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        title->setTextFormat(Qt::PlainText);
        title->setWordWrap(false);
        title->setToolTip(item.filePath);
        title->setStyleSheet(QStringLiteral("color:#0F172A;font-size:12px;font-weight:600;"));
        textLayout->addWidget(title);

        QString reason = item.reason.trimmed();
        if (reason.isEmpty())
            reason = item.summary.trimmed();
        if (reason.isEmpty())
            reason = item.scenarios.trimmed();
        if (reason.isEmpty())
            reason = QStringLiteral("与当前咨询相关");
        auto* reasonLabel = new QLabel(reason.left(80), textBox);
        reasonLabel->setMinimumWidth(0);
        reasonLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        reasonLabel->setTextFormat(Qt::PlainText);
        reasonLabel->setWordWrap(false);
        reasonLabel->setToolTip(reason);
        reasonLabel->setStyleSheet(QStringLiteral("color:#475569;font-size:11px;"));
        textLayout->addWidget(reasonLabel);

        const QString riskText = aggregateImageRiskIsEmpty(item.riskTags) ? QString() : item.riskTags;
        auto* riskLabel = new QLabel(riskText.isEmpty() ? QStringLiteral("需人工确认") : QStringLiteral("风险：%1").arg(riskText.left(40)), textBox);
        riskLabel->setMinimumWidth(0);
        riskLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        riskLabel->setTextFormat(Qt::PlainText);
        riskLabel->setWordWrap(false);
        riskLabel->setToolTip(riskText.isEmpty() ? item.riskNotice : item.riskNotice + QLatin1Char('\n') + riskText);
        riskLabel->setStyleSheet(riskText.isEmpty()
                                     ? QStringLiteral("color:#64748B;font-size:11px;")
                                     : QStringLiteral("color:#B45309;font-size:11px;"));
        textLayout->addWidget(riskLabel);
        row->addWidget(textBox, 1);

        auto* actions = new QWidget(card);
        actions->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        auto* actionLayout = new QVBoxLayout(actions);
        actionLayout->setContentsMargins(0, 0, 0, 0);
        actionLayout->setSpacing(4);

        auto* previewBtn = new QToolButton(actions);
        previewBtn->setText(QStringLiteral("预览"));
        previewBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        previewBtn->setCursor(Qt::PointingHandCursor);
        previewBtn->setAutoRaise(false);
        previewBtn->setFixedSize(56, 24);
        previewBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:#EEF2FF;border:1px solid #C7D2FE;border-radius:4px;color:#3730A3;font-size:11px;}"
            "QToolButton:hover{background:#E0E7FF;}"));
        connect(previewBtn, &QToolButton::clicked, this, [this, path = item.filePath]() {
            previewSuggestedImageAttachment(path);
        });
        actionLayout->addWidget(previewBtn);

        auto* markBtn = new QToolButton(actions);
        const bool selected = !m_selectedSuggestedImagePath.isEmpty()
            && QFileInfo(m_selectedSuggestedImagePath).absoluteFilePath() == QFileInfo(item.filePath).absoluteFilePath();
        markBtn->setText(selected ? QStringLiteral("已标记") : QStringLiteral("标记"));
        markBtn->setText(selected ? QStringLiteral("已添加") : QStringLiteral("添加"));
        markBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        markBtn->setCursor(Qt::PointingHandCursor);
        markBtn->setAutoRaise(false);
        markBtn->setEnabled(!selected);
        markBtn->setFixedSize(56, 24);
        markBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:#F0FDF4;border:1px solid #BBF7D0;border-radius:4px;color:#166534;font-size:11px;}"
            "QToolButton:hover{background:#DCFCE7;}"
            "QToolButton:disabled{background:#E2E8F0;border-color:#CBD5E1;color:#64748B;}"));
        connect(markBtn, &QToolButton::clicked, this, [this, path = item.filePath]() {
            markSuggestedImageForUse(path);
        });
        actionLayout->addWidget(markBtn);
        row->addWidget(actions, 0, Qt::AlignVCenter);

        m_suggestedImageLayout->addWidget(card, 1);
    }

    m_suggestedImageLayout->addStretch(1);
    m_suggestedImagePanel->setVisible(visibleCount > 0);
    scheduleChatInputRelayout();
}

void AggregateChatForm::previewSuggestedImageAttachment(const QString& path)
{
    if (path.trimmed().isEmpty() || !QFileInfo::exists(path)) {
        showStatusMessage(QStringLiteral("建议附图不存在或已被移动"), 5000);
        return;
    }
    ImagePreviewDialog dlg(path, this);
    dlg.exec();
}

void AggregateChatForm::markSuggestedImageForUse(const QString& path)
{
    if (path.trimmed().isEmpty() || !QFileInfo::exists(path)) {
        showStatusMessage(QStringLiteral("建议附图不存在或已被移动"), 5000);
        return;
    }
    if (!addComposeFileAttachment(path)) {
        showStatusMessage(QStringLiteral("建议附图添加失败，请确认图片文件可访问"), 5000);
        return;
    }
    m_selectedSuggestedImagePath = path;
    refreshSuggestedImagePanel();
    showStatusMessage(QStringLiteral("已添加建议附图，发送本条回复时会一起发送"), 5000);
    return;
    showStatusMessage(QStringLiteral("已标记建议附图，请预览确认后再手动处理发送"), 5000);
}

void AggregateChatForm::persistCurrentDraft()
{
    if (m_currentConvId <= 0 || !m_inputEdit)
        return;

    ConversationDao dao;
    const QString content = m_inputEdit->toPlainText();
    if (RuntimeMode::isSingleHostServiceDb()) {
        const auto conv = dao.findById(m_currentConvId);
        if (conv)
            AppDataUiStateDao().saveDraft(conv->platform, conv->platformConversationId, content);
    } else {
        dao.saveCachedDraft(m_currentConvId, content);
    }
    m_draftAttachments[m_currentConvId] = m_composeAttachments;
}

void AggregateChatForm::restoreDraftForConversation(int conversationId)
{
    if (!m_inputEdit)
        return;

    ConversationDao dao;
    QString draft = RuntimeMode::isSingleHostServiceDb()
        ? QString()
        : (conversationId > 0 ? dao.cachedDraftForConversation(conversationId) : QString());
    if (RuntimeMode::isSingleHostServiceDb() && conversationId > 0) {
        const auto conv = dao.findById(conversationId);
        if (conv) {
            const QString appDataDraft =
                AppDataUiStateDao().draftForConversation(conv->platform, conv->platformConversationId);
            draft = appDataDraft;
        }
    }
    m_restoringDraft = true;
    {
        const QSignalBlocker blocker(m_inputEdit->document());
        m_inputEdit->setPlainText(draft);
    }
    m_composeAttachments = m_draftAttachments.value(conversationId);
    refreshComposeAttachments();
    m_restoringDraft = false;
    scheduleChatInputRelayout();
}

void AggregateChatForm::restoreLastSelectedConversation()
{
    if (!m_conversationListModel || m_conversationListModel->rowCount() <= 0)
        return;

    ConversationDao dao;
    int conversationId = -1;
    if (RuntimeMode::isSingleHostServiceDb()) {
        QString platform;
        QString conversationKey;
        if (AppDataUiStateDao().lastSelectedConversation(&platform, &conversationKey)) {
            const auto conv = dao.findByPlatformId(platform, conversationKey);
            if (conv && m_conversationListModel->containsConversation(conv->id))
                conversationId = conv->id;
        }
    }
    if (conversationId <= 0)
        conversationId = dao.lastSelectedCachedConversationId();
    if (conversationId <= 0 || !m_conversationListModel->containsConversation(conversationId))
        conversationId = m_conversationListModel->conversationIdAt(0);

    if (conversationId > 0)
        showConversation(conversationId);
}

void AggregateChatForm::refreshConversationList()
{
    if (!m_conversationListModel)
        m_conversationListModel = new ConversationListModel(this);

    auto& mgr = ConversationManager::instance();
    MessageDao msgDao;
    const QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    m_conversationListModel->setFilters(static_cast<int>(m_currentTab),
                                        static_cast<int>(m_platformFilter),
                                        keyword,
                                        m_pendingStickyConvId);
    QVector<ConversationInfo> conversations;
    QHash<int, QString> lastDirections;
    conversations = mgr.allConversations();
    lastDirections = msgDao.lastCachedDirectionsByConversation();
    qInfo() << "[AggregateChatForm] conversation list loaded from unified db"
            << "count=" << conversations.size();
    m_conversationListModel->setSourceConversations(conversations, lastDirections);
    renderConversationListFromModel();
    return;
}

void AggregateChatForm::reloadFromLocalCache()
{
    refreshConversationList();

    if (m_currentConvId > 0
        && m_conversationListModel
        && m_conversationListModel->containsConversation(m_currentConvId)) {
        const bool hadRenderedMessages = m_messageListModel && !m_messageListModel->messages().isEmpty();
        const int previousMessageCount = m_messageListModel ? m_messageListModel->messages().size() : 0;
        QString previousLatestMessageKey;
        if (hadRenderedMessages)
            previousLatestMessageKey = buildMessageIdentityKey(m_messageListModel->messages().constLast());
        const bool wasNearBottom = isMessageViewNearBottom();
        const auto messages = ConversationManager::instance().messages(m_currentConvId);
        QString latestMessageKey;
        if (!messages.isEmpty())
            latestMessageKey = buildMessageIdentityKey(messages.constLast());
        const bool latestMessageChanged = !latestMessageKey.isEmpty()
            && latestMessageKey != previousLatestMessageKey;
        if (!m_messageListModel)
            m_messageListModel = new MessageListModel(this);
        m_messageListModel->setConversationMessages(m_currentConvId, messages);
        renderConversationMessagesFromModel();
        m_currentMessageSignature = m_messageListModel->signature();

        const auto conv = m_conversationService
                              ? m_conversationService->conversationById(m_currentConvId)
                              : std::optional<ConversationInfo>();
        if (conv) {
            setChatHeaderTitle(QStringLiteral("%1 (%2)").arg(conv->customerName, conv->platform));
            updateCustomerInfo(*conv);
        }
        const int addedMessages = qMax(0, messages.size() - previousMessageCount);
        if (!hadRenderedMessages || wasNearBottom) {
            scheduleScrollChatToBottom(true);
        } else if (addedMessages > 0 || latestMessageChanged) {
            showPendingNewMessageHint(addedMessages > 0 ? addedMessages : 1);
        }
        if (hadRenderedMessages && latestMessageChanged && !messages.isEmpty()) {
            const MessageRecord& latest = messages.constLast();
            if (latest.direction == QLatin1String("in")) {
                const QString inboundMessageKey = buildMessageIdentityKey(latest);
                const bool duplicateAutoReplyTrigger =
                    m_lastAutoReplyInboundMessageKeyByConversation.value(m_currentConvId) == inboundMessageKey;
                appendAggregateAutoReplyLog(
                    QStringLiteral("cache_reload_new_inbound_message"),
                    {
                        aggregateLogField(QStringLiteral("conversation_id"), QString::number(m_currentConvId)),
                        aggregateLogField(QStringLiteral("message_id"), QString::number(latest.id)),
                        aggregateLogField(QStringLiteral("platform_message_id"), latest.platformMsgId),
                        aggregateLogField(QStringLiteral("added_messages"), QString::number(addedMessages)),
                        aggregateLogField(QStringLiteral("latest_message_changed"),
                                          aggregateBoolLabel(latestMessageChanged)),
                        aggregateLogField(QStringLiteral("duplicate_auto_reply_trigger"),
                                          aggregateBoolLabel(duplicateAutoReplyTrigger)),
                        aggregateLogField(QStringLiteral("auto_reply_enabled"),
                                          aggregateBoolLabel(isAggregateAutoReplyEnabled())),
                        aggregateLogField(QStringLiteral("content_preview"), aggregateLogPreview(latest.content)),
                    });
                if (duplicateAutoReplyTrigger) {
                    appendAggregateAutoReplyLog(
                        QStringLiteral("skip_duplicate_auto_reply_trigger"),
                        {
                            aggregateLogField(QStringLiteral("trigger"), QStringLiteral("cache-reload")),
                            aggregateLogField(QStringLiteral("conversation_id"), QString::number(m_currentConvId)),
                            aggregateLogField(QStringLiteral("message_id"), QString::number(latest.id)),
                            aggregateLogField(QStringLiteral("platform_message_id"), latest.platformMsgId),
                            aggregateLogField(QStringLiteral("content_preview"), aggregateLogPreview(latest.content)),
                        });
                } else {
                    m_lastAutoReplyInboundMessageKeyByConversation.insert(m_currentConvId, inboundMessageKey);
                    tryAggregateAutoReply(m_currentConvId, QStringLiteral("cache-reload"));
                }
            }
        }
        qInfo() << "[AggregateChatForm] reloaded current conversation from local cache:"
                << m_currentConvId << "messages=" << messages.size();
        return;
    }

    restoreLastSelectedConversation();
    if (m_currentConvId <= 0) {
        showCenterEmptyState();
        showRightEmptyState();
    }
    qInfo() << "[AggregateChatForm] reloaded conversation cache from local cache";
}

void AggregateChatForm::reloadFromDatabase()
{
    reloadFromLocalCache();
}

void AggregateChatForm::applyCacheSnapshotToLocalCache(const QJsonObject& snapshot)
{
    if (RuntimeMode::isSingleHostServiceDb()) {
        qInfo() << "[AggregateChatForm] cache snapshot skipped in single-host service DB mode";
        return;
    }

    if (snapshot.value(QStringLiteral("status")).toString() != QLatin1String("success"))
        return;

    const QString platform = snapshot.value(QStringLiteral("platform")).toString().trimmed().toLower();
    const QString nextCursor = snapshot.value(QStringLiteral("snapshot_cursor")).toString().trimmed();
    const bool fullSnapshot = snapshot.value(QStringLiteral("full_snapshot")).toBool(false);
    const QJsonArray conversations = snapshot.value(QStringLiteral("conversations")).toArray();
    if (conversations.isEmpty()) {
        if (fullSnapshot && !platform.isEmpty()) {
            const int removed = ConversationDao().deleteMissingSnapshotCacheConversations(
                platform,
                {});
            qInfo() << "[AggregateChatForm] cache snapshot empty full sync"
                    << "platform=" << platform
                    << "removedConversations=" << removed;
        }
        if (!platform.isEmpty() && !nextCursor.isEmpty())
            ConversationDao().setSnapshotCursor(platform, nextCursor);
        return;
    }

    ConversationDao conversationDao;
    MessageDao messageDao;
    QSet<QString> keepConversationIds;
    int appliedConversations = 0;
    int appliedMessages = 0;
    int removedConversations = 0;
    int removedMessages = 0;
    for (const QJsonValue& value : conversations) {
        const QJsonObject conversation = value.toObject();
        if (conversation.isEmpty())
            continue;
        const QString platformConversationId =
            conversation.value(QStringLiteral("platform_conversation_id")).toString().trimmed();
        if (!platformConversationId.isEmpty())
            keepConversationIds.insert(platformConversationId);
        const int conversationId = conversationDao.upsertSnapshotCacheConversation(conversation);
        if (conversationId <= 0)
            continue;
        ++appliedConversations;

        QSet<QString> keepPlatformMessageIds;
        QSet<QString> keepClientMessageIds;
        const QJsonArray messages = conversation.value(QStringLiteral("messages")).toArray();
        for (const QJsonValue& messageValue : messages) {
            const QJsonObject message = messageValue.toObject();
            if (message.isEmpty())
                continue;
            const QString platformMessageId = message.value(QStringLiteral("platform_msg_id")).toString().trimmed();
            const QString clientMessageId = message.value(QStringLiteral("client_message_id")).toString().trimmed();
            if (!platformMessageId.isEmpty())
                keepPlatformMessageIds.insert(platformMessageId);
            if (!clientMessageId.isEmpty())
                keepClientMessageIds.insert(clientMessageId);
        }
        removedMessages += messageDao.deleteMissingSnapshotCacheMessages(
            conversationId,
            keepPlatformMessageIds,
            keepClientMessageIds);
        const QString conversationPlatform = conversation.value(QStringLiteral("platform")).toString().trimmed().toLower();
        const QString accountId = conversation.value(QStringLiteral("account_id")).toString().trimmed();
        const QString displayName = conversation.value(QStringLiteral("customer_name")).toString().trimmed();
        for (const QJsonValue& messageValue : messages) {
            const QJsonObject message = messageValue.toObject();
            if (message.isEmpty())
                continue;
            const int messageId = messageDao.upsertSnapshotCacheMessage(conversationId, message);
            if (messageId > 0) {
                ++appliedMessages;
                if (conversationPlatform == QLatin1String("wechat")) {
                    WechatMessageDao wechatDao;
                    wechatDao.createMessageExtension(
                        messageId,
                        conversationId,
                        accountId,
                        platformConversationId,
                        displayName,
                        message.value(QStringLiteral("platform_message_id")).toString(
                            message.value(QStringLiteral("platform_msg_id")).toString()),
                        message);
                } else if (conversationPlatform == QLatin1String("qianniu")) {
                    QianniuConversationDao qianniuDao;
                    qianniuDao.createMessageExtension(
                        messageId,
                        conversationId,
                        accountId,
                        platformConversationId,
                        displayName,
                        message.value(QStringLiteral("platform_message_id")).toString(
                            message.value(QStringLiteral("platform_msg_id")).toString()),
                        message);
                } else if (conversationPlatform == QLatin1String("qq")) {
                    QQMessageDao qqDao;
                    qqDao.createMessageExtension(
                        messageId,
                        conversationId,
                        accountId,
                        platformConversationId,
                        displayName,
                        message.value(QStringLiteral("platform_message_id")).toString(
                            message.value(QStringLiteral("platform_msg_id")).toString()),
                        message);
                }
            }
        }
    }

    if (fullSnapshot && !platform.isEmpty()) {
        removedConversations = conversationDao.deleteMissingSnapshotCacheConversations(
            platform,
            keepConversationIds);
    }
    if (!platform.isEmpty() && !nextCursor.isEmpty())
        conversationDao.setSnapshotCursor(platform, nextCursor);

    qInfo() << "[AggregateChatForm] cache snapshot applied"
            << "platform=" << platform
            << "fullSnapshot=" << fullSnapshot
            << "conversations=" << appliedConversations
            << "messages=" << appliedMessages
            << "removedConversations=" << removedConversations
            << "removedMessages=" << removedMessages
            << "nextCursor=" << nextCursor;
}

QVector<MessageRecord> AggregateChatForm::messagesForDisplay(int conversationId) const
{
    return ConversationManager::instance().messages(conversationId);
}

#if 0
    auto& mgr = ConversationManager::instance();
    const auto convs = mgr.allConversations();
    MessageDao msgDao;
    const QHash<int, QString> lastDirs = msgDao.lastCachedDirectionsByConversation();

    QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();

    m_conversationList->clear();
    int count = 0;
    for (const auto& conv : convs) {
        const QString lastDir = lastDirs.value(conv.id);

        bool inThisTab = false;
        if (m_currentTab == AggregateConversationTab::All) {
            inThisTab = true;
        } else if (m_currentTab == AggregateConversationTab::Pending) {
            if (lastDir == QLatin1String("in"))
                inThisTab = true;
            else if (lastDir == QLatin1String("out") && m_pendingStickyConvId == conv.id)
                inThisTab = true;
        } else {
            // 无消息、己方回复、或其它非 in 方向均归入「已回复」（与「待处理」互补，避免遗漏）
            if (lastDir.isEmpty() || lastDir != QLatin1String("in"))
                inThisTab = true;
        }
        if (!inThisTab)
            continue;

        if (m_platformFilter != AggregatePlatformFilter::All) {
            QString wantPlat;
            switch (m_platformFilter) {
            case AggregatePlatformFilter::Qianniu:
                wantPlat = QStringLiteral("qianniu");
                break;
            case AggregatePlatformFilter::Pdd:
                wantPlat = QStringLiteral("pdd_web");
                break;
            case AggregatePlatformFilter::Doudian:
                wantPlat = QStringLiteral("douyin");
                break;
            case AggregatePlatformFilter::Wechat:
                wantPlat = QStringLiteral("wechat");
                break;
            case AggregatePlatformFilter::QQ:
                wantPlat = QStringLiteral("qq");
                break;
            default:
                break;
            }
            if (conv.platform != wantPlat)
                continue;
        }

        if (!keyword.isEmpty() && !conv.customerName.contains(keyword, Qt::CaseInsensitive)
            && !conv.lastMessage.contains(keyword, Qt::CaseInsensitive))
            continue;

        auto* item = new QListWidgetItem(m_conversationList);
        auto* widget = createConversationItem(conv);
        item->setSizeHint(widget->sizeHint() + QSize(0, 4));
        item->setData(Qt::UserRole, conv.id);
        m_conversationList->setItemWidget(item, widget);
        count++;
    }

    m_leftStack->setCurrentIndex(count > 0 ? 0 : 1);

    if (m_currentConvId > 0) {
        bool currentStillVisible = false;
        for (int i = 0; i < m_conversationList->count(); ++i) {
            QListWidgetItem* listItem = m_conversationList->item(i);
            if (listItem && listItem->data(Qt::UserRole).toInt() == m_currentConvId) {
                currentStillVisible = true;
                break;
            }
        }
        if (!currentStillVisible) {
            const int lost = m_currentConvId;
            m_currentConvId = -1;
            if (m_pendingStickyConvId == lost)
                m_pendingStickyConvId = -1;
            ConversationManager::instance().selectConversation(-1);
            abortAggregateAiRequest();
            showCenterEmptyState();
            showRightEmptyState();
        }
    }

    if (m_currentConvId > 0 && m_conversationList->count() > 0) {
        for (int i = 0; i < m_conversationList->count(); ++i) {
            QListWidgetItem* listItem = m_conversationList->item(i);
            if (!listItem || listItem->data(Qt::UserRole).toInt() != m_currentConvId)
                continue;
            m_conversationList->setCurrentItem(listItem);
            listItem->setSelected(true);
            m_conversationList->scrollToItem(
                listItem,
                QAbstractItemView::PositionAtCenter);
            break;
        }
    }
    syncConversationItemVisualState();
}
#endif

void AggregateChatForm::renderConversationListFromModel()
{
    if (!m_conversationList || !m_leftStack || !m_conversationListModel)
        return;

    const int count = m_conversationListModel->rowCount();
    m_leftStack->setCurrentIndex(count > 0 ? 0 : 1);

    if (m_currentConvId > 0 && !m_conversationListModel->containsConversation(m_currentConvId)) {
        const int lost = m_currentConvId;
        persistCurrentDraft();
        m_currentConvId = -1;
        if (m_pendingStickyConvId == lost)
            m_pendingStickyConvId = -1;
        ConversationManager::instance().selectConversation(-1);
        abortAggregateAiRequest();
        showCenterEmptyState();
        showRightEmptyState();
    }

    m_conversationListModel->setSelectedConversationId(m_currentConvId);
    if (m_currentConvId > 0 && count > 0) {
        const QModelIndex idx = m_conversationListModel->indexForConversationId(m_currentConvId);
        if (idx.isValid()) {
            m_conversationList->setCurrentIndex(idx);
            m_conversationList->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        }
    }
    syncConversationItemVisualState();
}

void AggregateChatForm::showConversation(int conversationId)
{
    abortAggregateAiRequest();
    clearSuggestedImageAttachments();

    const int prevId = m_currentConvId;
    if (prevId > 0 && prevId != conversationId)
        persistCurrentDraft();
    if (prevId > 0 && prevId != conversationId && m_pendingStickyConvId == prevId)
        m_pendingStickyConvId = -1;

    m_currentConvId = conversationId;
    clearPendingNewMessageHint();
    auto& mgr = ConversationManager::instance();
    mgr.selectConversation(conversationId);

    auto messages = messagesForDisplay(conversationId);
    if (!m_messageListModel)
        m_messageListModel = new MessageListModel(this);
    m_messageListModel->setConversationMessages(conversationId, messages);
    renderConversationMessagesFromModel();
    m_currentMessageSignature = m_messageListModel->signature();

    // Update header
    const auto conv = m_conversationService
                          ? m_conversationService->conversationById(conversationId)
                          : std::optional<ConversationInfo>();
    if (conv) {
        setChatHeaderTitle(QStringLiteral("%1 (%2)").arg(conv->customerName, conv->platform));
        updateCustomerInfo(*conv);
        resetSendTimelineForConversation();
    }

    m_centerStack->setCurrentWidget(m_chatArea);
    updateCompactHeaderControls();
    updateWechatHistorySyncButtonUi();
    restoreDraftForConversation(conversationId);
    m_inputEdit->setFocus();

    scheduleScrollChatToBottom();
    updateAggregateAiControlsVisibility();
}

void AggregateChatForm::renderConversationMessages(const QVector<MessageRecord>& messages)
{
    if (!m_messageListModel)
        return;
    m_messageListModel->setConversationMessages(m_currentConvId, messages);
}

void AggregateChatForm::renderConversationMessagesFromModel()
{
    if (!m_messageView || !m_messageListModel)
        return;
    m_messageView->setModel(m_messageListModel);
    m_messageView->viewport()->update();
}

void AggregateChatForm::appendMessageBubble(const MessageRecord& msg)
{
    if (m_messageListModel)
        m_messageListModel->appendMessage(msg);
    renderConversationMessagesFromModel();
    if (m_messageListModel)
        m_currentMessageSignature = m_messageListModel->signature();
}

QString AggregateChatForm::buildMessageIdentityKey(const MessageRecord& msg) const
{
    return QStringList{
        QString::number(msg.id),
        msg.direction,
        msg.platformMsgId,
        msg.clientMessageId,
        msg.createdAt.toString(Qt::ISODateWithMs),
        msg.contentType,
        msg.content,
    }.join(QChar(0x1f));
}

QString AggregateChatForm::buildMessageSignature(const QVector<MessageRecord>& messages) const
{
    QStringList parts;
    parts.reserve(messages.size());
    for (const auto& msg : messages) {
        parts.append(QStringLiteral("%1:%2:%3")
                         .arg(msg.id)
                         .arg(msg.syncStatus)
                         .arg(msg.errorReason));
    }
    return parts.join(QChar('|'));
}

void AggregateChatForm::refreshVisibleConversationMessages()
{
    if (m_currentConvId <= 0 || !m_messageView)
        return;
    if (RuntimeMode::ownsBusinessDatabase())
        return;

    auto messages = ConversationManager::instance().messages(m_currentConvId);
    const QString newSignature = buildMessageSignature(messages);
    if (newSignature == m_currentMessageSignature)
        return;

    auto* sb = m_messageView->verticalScrollBar();
    const bool wasNearBottom = !sb || sb->value() >= sb->maximum() - 24;
    const int previousMessageCount = m_messageListModel ? m_messageListModel->messages().size() : 0;
    m_messageListModel->setConversationMessages(m_currentConvId, messages);
    renderConversationMessagesFromModel();
    m_currentMessageSignature = m_messageListModel->signature();
    if (wasNearBottom)
        scheduleScrollChatToBottom(true);
    else if (messages.size() > previousMessageCount)
        showPendingNewMessageHint(messages.size() - previousMessageCount);
}

void AggregateChatForm::scrollToBottom()
{
    if (!m_messageView)
        return;
    auto* sb = m_messageView->verticalScrollBar();
    sb->setValue(sb->maximum());
    m_messageViewNearBottom = true;
    clearPendingNewMessageHint();
}

void AggregateChatForm::scheduleScrollChatToBottom(bool force)
{
    if (!force && !isMessageViewNearBottom())
        return;

    QTimer::singleShot(100, this, [this, force]() {
        if (!m_messageView)
            return;
        if (!force && !isMessageViewNearBottom())
            return;
        m_messageView->viewport()->update();
        scrollToBottom();
        // QScrollArea 内容高度偶发晚一帧才更新，再拉一次
        QTimer::singleShot(0, this, [this]() { scrollToBottom(); });
    });
}

bool AggregateChatForm::isMessageViewNearBottom(int tolerancePx) const
{
    if (!m_messageView)
        return true;
    const QScrollBar* sb = m_messageView->verticalScrollBar();
    if (!sb)
        return true;
    return sb->value() >= sb->maximum() - qMax(0, tolerancePx);
}

void AggregateChatForm::updateMessageScrollState()
{
    const bool nearBottom = isMessageViewNearBottom();
    m_messageViewNearBottom = nearBottom;
    if (nearBottom)
        clearPendingNewMessageHint();
}

void AggregateChatForm::showPendingNewMessageHint(int count)
{
    if (!m_btnNewMessages)
        return;
    m_pendingNewMessageCount = qMax(1, m_pendingNewMessageCount + qMax(1, count));
    m_btnNewMessages->setText(m_pendingNewMessageCount > 1
                                  ? QStringLiteral("%1 条新消息").arg(m_pendingNewMessageCount)
                                  : QStringLiteral("新消息"));
    updateNewMessageHintGeometry();
    m_btnNewMessages->setVisible(true);
    m_btnNewMessages->raise();
}

void AggregateChatForm::clearPendingNewMessageHint()
{
    m_pendingNewMessageCount = 0;
    if (m_btnNewMessages)
        m_btnNewMessages->setVisible(false);
}

void AggregateChatForm::updateNewMessageHintGeometry()
{
    if (!m_btnNewMessages || !m_chatInputOverlayHost)
        return;
    const QSize hintSize = m_btnNewMessages->sizeHint() + QSize(12, 2);
    const int w = qBound(88, hintSize.width(), 180);
    const int h = 28;
    int bottomY = m_chatInputOverlayHost->height() - 88;
    if (m_chatInputPanel)
        bottomY = m_chatInputPanel->geometry().top();
    const int x = qMax(12, (m_chatInputOverlayHost->width() - w) / 2);
    const int y = qMax(12, bottomY - h - 12);
    m_btnNewMessages->setGeometry(x, y, w, h);
}

void AggregateChatForm::updateCustomerInfo(const ConversationInfo& /*conv*/)
{
    if (m_rightStack)
        m_rightStack->setCurrentWidget(m_customerDetail);
    refreshCustomerProfilePanel();
}

void AggregateChatForm::resetSendTimelineForConversation()
{
    if (m_sendTimeline)
        m_sendTimeline->clear();
    MessageSendEventDao dao;
    m_sendTimelineBaselineId = dao.globalMaxId();
    m_aiStageTimelineBaselineId = AiRequestEventDao().globalStageMaxId();
}

void AggregateChatForm::pollSendTimeline()
{
    if (m_currentConvId <= 0 || !m_sendTimeline)
        return;
    if (!m_sendTimelineBody || !m_sendTimelineBody->isVisible())
        return;

    struct TimelineLine {
        QDateTime createdAt;
        QString text;
    };
    QVector<TimelineLine> lines;

    MessageSendEventDao sendDao;
    const QVector<MessageSendEventRecord> rows =
        sendDao.listSince(m_currentConvId, m_sendTimelineBaselineId);
    for (const MessageSendEventRecord& e : rows) {
        const QString line = formatSendEventLine(e);
        if (!line.isEmpty())
            lines.push_back({e.createdAt, line});
        m_sendTimelineBaselineId = std::max(m_sendTimelineBaselineId, e.id);
    }

    AiRequestEventDao aiDao;
    const QVector<AiRequestStageEventRecord> aiRows =
        aiDao.listStagesSince(m_currentConvId, m_aiStageTimelineBaselineId);
    for (const AiRequestStageEventRecord& e : aiRows) {
        const QString line = formatAiStageEventLine(e);
        if (!line.isEmpty())
            lines.push_back({e.createdAt, line});
        m_aiStageTimelineBaselineId = std::max(m_aiStageTimelineBaselineId, e.id);
    }

    if (lines.isEmpty())
        return;

    std::sort(lines.begin(), lines.end(), [](const TimelineLine& a, const TimelineLine& b) {
        return a.createdAt < b.createdAt;
    });
    for (const TimelineLine& line : std::as_const(lines))
        m_sendTimeline->appendPlainText(line.text);
}

void AggregateChatForm::onClearSendTimeline()
{
    if (m_sendTimeline)
        m_sendTimeline->clear();
    MessageSendEventDao dao;
    m_sendTimelineBaselineId = dao.globalMaxId();
    m_aiStageTimelineBaselineId = AiRequestEventDao().globalStageMaxId();
}

void AggregateChatForm::showCenterEmptyState()
{
    abortAggregateAiRequest();
    clearSuggestedImageAttachments();
    clearPendingNewMessageHint();
    m_centerStack->setCurrentWidget(m_centerEmptyState);
    updateAggregateAiControlsVisibility();
    updateCompactHeaderControls();
}

void AggregateChatForm::showRightEmptyState()
{
    if (m_sendTimeline)
        m_sendTimeline->clear();
    m_sendTimelineBaselineId = 0;
    m_aiStageTimelineBaselineId = 0;
    refreshCustomerProfilePanel();
}

// ===================== Slots =====================

void AggregateChatForm::setConversationTab(AggregateConversationTab tab)
{
    if (m_currentTab != tab && m_pendingStickyConvId > 0)
        m_pendingStickyConvId = -1;
    m_currentTab = tab;
    if (m_btnAll)
        m_btnAll->setChecked(tab == AggregateConversationTab::All);
    if (m_btnPending)
        m_btnPending->setChecked(tab == AggregateConversationTab::Pending);
    if (m_btnReplied)
        m_btnReplied->setChecked(tab == AggregateConversationTab::Replied);
    refreshConversationList();
}

void AggregateChatForm::onTabAllClicked()
{
    setConversationTab(AggregateConversationTab::All);
}

void AggregateChatForm::onTabPendingClicked()
{
    setConversationTab(AggregateConversationTab::Pending);
}

void AggregateChatForm::onTabRepliedClicked()
{
    setConversationTab(AggregateConversationTab::Replied);
}

void AggregateChatForm::onConversationItemClicked(QListWidgetItem* item)
{
    if (!item) return;
    int convId = item->data(Qt::UserRole).toInt();
    qDebug() << "点击会话:" << convId;
    showConversation(convId);
    refreshConversationList();
    scheduleAdaptiveRelayout(false);
}

void AggregateChatForm::onConversationIndexClicked(const QModelIndex& index)
{
    if (!index.isValid() || !m_conversationListModel)
        return;
    const int convId = m_conversationListModel->data(index, ConversationListModel::ConversationIdRole).toInt();
    if (convId <= 0)
        return;
    showConversation(convId);
    refreshConversationList();
    scheduleAdaptiveRelayout(false);
}

void AggregateChatForm::onConversationListContextMenu(const QPoint& pos)
{
    if (!m_conversationList)
        return;
    const QModelIndex index = m_conversationList->indexAt(pos);
    if (!index.isValid())
        return;
    const int convId = m_conversationListModel
                            ? m_conversationListModel->data(index, ConversationListModel::ConversationIdRole).toInt()
                            : 0;
    if (convId <= 0)
        return;

    QMenu menu(this);
    QAction* actClear = menu.addAction(QStringLiteral("清空聊天记录"));
    QAction* actDelete = menu.addAction(QStringLiteral("删除会话"));
    QAction* chosen = menu.exec(m_conversationList->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == actClear) {
        QMessageBox confirmBox(this);
        confirmBox.setIcon(QMessageBox::Question);
        confirmBox.setWindowTitle(QStringLiteral("清空聊天记录"));
        confirmBox.setText(
            QStringLiteral("确定清空该会话的全部聊天记录？此操作不可恢复。"));
        confirmBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirmBox.setDefaultButton(QMessageBox::No);
        confirmBox.setStyleSheet(aggregateMessageBoxContrastStyle());
        if (confirmBox.exec() != QMessageBox::Yes)
            return;

        ConversationDao convDao;
        const auto conv = convDao.findById(convId);
        if (conv) {
            if (!applyServiceConversationMutation(*conv, false))
                return;
            if (RuntimeMode::ownsBusinessDatabase())
                return;
        } else if (RuntimeMode::ownsBusinessDatabase()) {
            showAggregateWarning(this,
                                 QStringLiteral("Python 服务端"),
                                 QStringLiteral("未找到本地会话缓存，请先同步 Python 服务端数据后再操作。"));
            return;
        }

        if (!ConversationManager::instance().clearConversationMessages(convId)) {
            QMessageBox warnBox(this);
            warnBox.setIcon(QMessageBox::Warning);
            warnBox.setWindowTitle(QStringLiteral("错误"));
            warnBox.setText(QStringLiteral("清空失败，请查看日志或确认数据库已升级。"));
            warnBox.setStandardButtons(QMessageBox::Ok);
            warnBox.setStyleSheet(aggregateMessageBoxContrastStyle());
            warnBox.exec();
            return;
        }

        return;
    }

    if (chosen == actDelete) {
        QMessageBox delBox(this);
        delBox.setIcon(QMessageBox::Warning);
        delBox.setWindowTitle(QStringLiteral("删除会话"));
        delBox.setText(QStringLiteral(
            "确定删除该会话？将同时删除会话记录、全部聊天消息、入站队列中该会话相关数据，且不可恢复。"));
        delBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        delBox.setDefaultButton(QMessageBox::No);
        delBox.setStyleSheet(aggregateMessageBoxContrastStyle());
        if (delBox.exec() != QMessageBox::Yes)
            return;

        ConversationDao convDao;
        const auto conv = convDao.findById(convId);
        if (conv) {
            if (!applyServiceConversationMutation(*conv, true))
                return;
            if (RuntimeMode::ownsBusinessDatabase())
                return;
        } else if (RuntimeMode::ownsBusinessDatabase()) {
            showAggregateWarning(this,
                                 QStringLiteral("Python 服务端"),
                                 QStringLiteral("未找到本地会话缓存，请先同步 Python 服务端数据后再操作。"));
            return;
        }

        ConversationManager::instance().deleteConversation(convId);
    }
}

void AggregateChatForm::onSendClicked()
{
    QElapsedTimer timer;
    timer.start();
    if (m_currentConvId <= 0) return;
    const QString text = m_inputEdit ? m_inputEdit->toPlainText().trimmed() : QString();
    if (text.isEmpty() && m_composeAttachments.isEmpty()) return;

    for (const OutgoingMessagePart& part : std::as_const(m_composeAttachments)) {
        if (part.localPath.trimmed().isEmpty() || !QFileInfo::exists(part.localPath)) {
            showStatusMessage(QStringLiteral("附件不存在或已被移动：%1").arg(part.fileName), 6000);
            return;
        }
    }

    OutgoingMessagePayload payload;
    if (!text.isEmpty()) {
        OutgoingMessagePart textPart;
        textPart.type = OutgoingPartType::Text;
        textPart.text = text;
        payload.parts.push_back(textPart);
    }
    for (const OutgoingMessagePart& part : std::as_const(m_composeAttachments))
        payload.parts.push_back(part);

    // 发送后先留在「待处理」，直到客服切换会话或主动切换 tab。
    m_pendingStickyConvId = m_currentConvId;
    m_inputEdit->clear();
    clearComposeAttachments();
    clearSuggestedImageAttachments();
    m_draftAttachments.remove(m_currentConvId);
    ConversationDao convDao;
    const auto conv = convDao.findById(m_currentConvId);
    if (RuntimeMode::isSingleHostServiceDb() && conv)
        AppDataUiStateDao().clearDraft(conv->platform, conv->platformConversationId);
    else
        convDao.clearCachedDraft(m_currentConvId);
    ConversationManager::instance().sendPayload(m_currentConvId, payload);
    schedulePythonServiceBackfill(300);
    qInfo() << "[AggregateChatForm] send click timing"
            << "conversationId=" << m_currentConvId
            << "textLength=" << text.size()
            << "partCount=" << payload.parts.size()
            << "elapsedMs=" << timer.elapsed();
}

void AggregateChatForm::onAutoReplyToggleClicked()
{
    const bool enable = m_btnAutoReplyToggle && m_btnAutoReplyToggle->isChecked();
    appendAggregateAutoReplyLog(
        QStringLiteral("toggle_clicked"),
        {
            aggregateLogField(QStringLiteral("desired_enabled"), aggregateBoolLabel(enable)),
            aggregateLogField(QStringLiteral("previous_enabled"), aggregateBoolLabel(isAggregateAutoReplyEnabled())),
            aggregateLogField(QStringLiteral("current_conversation_id"), QString::number(m_currentConvId)),
            aggregateLogField(QStringLiteral("selected_platforms"), aggregateListLabel(selectedPlatformListenTargets())),
            aggregateLogField(QStringLiteral("registered_platforms"),
                              aggregateListLabel(sortedAggregatePlatformSet(m_registeredListenPlatforms))),
            aggregateLogField(QStringLiteral("service_listening_platforms"),
                              aggregateListLabel(sortedAggregatePlatformSet(m_serviceListeningPlatforms))),
            aggregateLogField(QStringLiteral("manager_listening_platforms"),
                              aggregateListLabel(aggregateManagerListeningPlatforms())),
        });
    if (enable) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("AI 自动回复"),
                        QStringLiteral("开启后，AI 将接管当前正在监听的平台会话中的客户新消息，并在满足条件时自动生成并发送回复。\n\n"
                                       "请确认已在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中配置好模型与 API，"
                                       "并了解误发、合规与费用风险。\n\n"
                                       "确定要开启自动回复吗？"),
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setDefaultButton(QMessageBox::No);
        box.setStyleSheet(aggregateMessageBoxContrastStyle());
        if (box.exec() != QMessageBox::Yes) {
            appendAggregateAutoReplyLog(
                QStringLiteral("enable_cancelled"),
                {
                    aggregateLogField(QStringLiteral("current_conversation_id"), QString::number(m_currentConvId)),
                    aggregateLogField(QStringLiteral("registered_platforms"),
                                      aggregateListLabel(sortedAggregatePlatformSet(m_registeredListenPlatforms))),
                    aggregateLogField(QStringLiteral("service_listening_platforms"),
                                      aggregateListLabel(sortedAggregatePlatformSet(m_serviceListeningPlatforms))),
                    aggregateLogField(QStringLiteral("manager_listening_platforms"),
                                      aggregateListLabel(aggregateManagerListeningPlatforms())),
                });
            refreshAutoReplyToggleButtonUi();
            return;
        }

        setAggregateAutoReplyEnabled(true);
        appendAggregateAutoReplyLog(
            QStringLiteral("enabled"),
            {
                aggregateLogField(QStringLiteral("current_conversation_id"), QString::number(m_currentConvId)),
                aggregateLogField(QStringLiteral("registered_platforms"),
                                  aggregateListLabel(sortedAggregatePlatformSet(m_registeredListenPlatforms))),
                aggregateLogField(QStringLiteral("service_listening_platforms"),
                                  aggregateListLabel(sortedAggregatePlatformSet(m_serviceListeningPlatforms))),
                aggregateLogField(QStringLiteral("manager_listening_platforms"),
                                  aggregateListLabel(aggregateManagerListeningPlatforms())),
            });
        showStatusMessage(QStringLiteral("已开启自动回复"), 4000);
        return;
    }

    setAggregateAutoReplyEnabled(false);
    appendAggregateAutoReplyLog(
        QStringLiteral("disabled"),
        {
            aggregateLogField(QStringLiteral("current_conversation_id"), QString::number(m_currentConvId)),
            aggregateLogField(QStringLiteral("auto_reply_busy"), aggregateBoolLabel(m_autoReplyBusy)),
            aggregateLogField(QStringLiteral("target_conversation_id"), QString::number(m_autoReplyTargetConvId)),
            aggregateLogField(QStringLiteral("trace_id"), m_autoReplyTraceId),
        });
    abortAutoReplyRequest();
    showStatusMessage(QStringLiteral("已停止自动回复"), 5000);
}

void AggregateChatForm::onConversationListChanged()
{
    refreshConversationList();
}

void AggregateChatForm::onUnifiedMessageReceived(int conversationId, const Models::Message& message)
{
    QElapsedTimer timer;
    timer.start();
    const MessageRecord record = messageRecordFromUnified(message);
    if (isHistorySyncMessage(message)) {
        if (conversationId == m_currentConvId) {
            auto messages = messagesForDisplay(conversationId);
            if (!messages.isEmpty()) {
                m_messageListModel->setConversationMessages(conversationId, messages);
                renderConversationMessagesFromModel();
            } else {
                appendMessageBubble(record);
            }
            m_currentMessageSignature = m_messageListModel ? m_messageListModel->signature() : m_currentMessageSignature;
        }
        refreshConversationList();
    } else if (message.direction == Models::MessageDirection::Outbound)
        onSentOk(conversationId, record);
    else
        onNewMessage(conversationId, record);
    qInfo() << "[AggregateChatForm] unified message UI timing"
            << "conversationId=" << conversationId
            << "messageId=" << message.id
            << "direction=" << Models::toString(message.direction)
            << "platformMessageId=" << message.platformMessageId
            << "clientMessageId=" << message.clientMessageId
            << "elapsedMs=" << timer.elapsed();
}

void AggregateChatForm::onUnifiedConversationUpdated(const Models::Conversation& conversation)
{
    if (conversation.id == m_currentConvId) {
        const ConversationInfo info = conversationInfoFromUnified(conversation);
        updateCustomerInfo(info);
        setChatHeaderTitle(QStringLiteral("%1 (%2)").arg(info.customerName, info.platform));
        updateWechatHistorySyncButtonUi();
    }
    refreshConversationList();
}

void AggregateChatForm::onConversationMessagesCleared(int conversationId)
{
    if (m_pendingStickyConvId == conversationId)
        m_pendingStickyConvId = -1;
    m_lastAutoReplyInboundMessageKeyByConversation.remove(conversationId);
    if (conversationId == m_currentConvId) {
        clearPendingNewMessageHint();
        m_lastBubbleDate = QDate();
        renderConversationMessages({});
        m_currentMessageSignature = buildMessageSignature({});
        resetSendTimelineForConversation();
    }
    refreshConversationList();
    showStatusMessage(QStringLiteral("已清空聊天记录"), 3000);
}

void AggregateChatForm::onConversationDeleted(int conversationId)
{
    if (m_pendingStickyConvId == conversationId)
        m_pendingStickyConvId = -1;
    m_lastAutoReplyInboundMessageKeyByConversation.remove(conversationId);
    if (conversationId == m_currentConvId) {
        clearPendingNewMessageHint();
        m_currentConvId = -1;
        m_lastBubbleDate = QDate();
        renderConversationMessages({});
        m_currentMessageSignature = buildMessageSignature({});
        showCenterEmptyState();
        showRightEmptyState();
        updateWechatHistorySyncButtonUi();
        if (m_inputEdit)
            m_inputEdit->clear();
    }
    refreshConversationList();
    showStatusMessage(QStringLiteral("已删除会话"), 3000);
}

void AggregateChatForm::onNewMessage(int conversationId, const MessageRecord& msg)
{
    QElapsedTimer timer;
    timer.start();
    if (msg.direction == QLatin1String("in") && m_pendingStickyConvId == conversationId)
        m_pendingStickyConvId = -1;
    if (m_currentConvId <= 0) {
        showConversation(conversationId);
    } else if (conversationId == m_currentConvId) {
        const bool wasNearBottom = isMessageViewNearBottom();
        appendMessageBubble(msg);
        if (wasNearBottom)
            scheduleScrollChatToBottom(true);
        else
            showPendingNewMessageHint();
    }
    refreshConversationList();
    showStatusMessage(QStringLiteral("新消息: %1").arg(msg.content.left(30)), 3000);

    if (msg.direction == QLatin1String("in")) {
        const QString inboundMessageKey = buildMessageIdentityKey(msg);
        const bool duplicateAutoReplyTrigger =
            m_lastAutoReplyInboundMessageKeyByConversation.value(conversationId) == inboundMessageKey;
        appendAggregateAutoReplyLog(
            QStringLiteral("inbound_message_received"),
            {
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("current_conversation_id"), QString::number(m_currentConvId)),
                aggregateLogField(QStringLiteral("message_id"), QString::number(msg.id)),
                aggregateLogField(QStringLiteral("platform_message_id"), msg.platformMsgId),
                aggregateLogField(QStringLiteral("duplicate_auto_reply_trigger"),
                                  aggregateBoolLabel(duplicateAutoReplyTrigger)),
                aggregateLogField(QStringLiteral("auto_reply_enabled"), aggregateBoolLabel(isAggregateAutoReplyEnabled())),
                aggregateLogField(QStringLiteral("service_listening_platforms"),
                                  aggregateListLabel(sortedAggregatePlatformSet(m_serviceListeningPlatforms))),
                aggregateLogField(QStringLiteral("manager_listening_platforms"),
                                  aggregateListLabel(aggregateManagerListeningPlatforms())),
                aggregateLogField(QStringLiteral("content_preview"), aggregateLogPreview(msg.content)),
            });
        if (duplicateAutoReplyTrigger) {
            appendAggregateAutoReplyLog(
                QStringLiteral("skip_duplicate_auto_reply_trigger"),
                {
                    aggregateLogField(QStringLiteral("trigger"), QStringLiteral("T2")),
                    aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                    aggregateLogField(QStringLiteral("message_id"), QString::number(msg.id)),
                    aggregateLogField(QStringLiteral("platform_message_id"), msg.platformMsgId),
                    aggregateLogField(QStringLiteral("content_preview"), aggregateLogPreview(msg.content)),
                });
        } else {
            m_lastAutoReplyInboundMessageKeyByConversation.insert(conversationId, inboundMessageKey);
            tryAggregateAutoReply(conversationId, QStringLiteral("T2"));
        }
    }
    qInfo() << "[AggregateChatForm] inbound message UI timing"
            << "conversationId=" << conversationId
            << "messageId=" << msg.id
            << "direction=" << msg.direction
            << "platformMsgId=" << msg.platformMsgId
            << "elapsedMs=" << timer.elapsed();
}

void AggregateChatForm::onSentOk(int conversationId, const MessageRecord& msg)
{
    QElapsedTimer timer;
    timer.start();
    if (conversationId == m_currentConvId && msg.direction == QLatin1String("out"))
        m_pendingStickyConvId = conversationId;
    if (conversationId == m_currentConvId) {
        appendMessageBubble(msg);
        scheduleScrollChatToBottom(true);
    }
    refreshConversationList();
    qInfo() << "[AggregateChatForm] outbound message UI timing"
            << "conversationId=" << conversationId
            << "messageId=" << msg.id
            << "direction=" << msg.direction
            << "clientMessageId=" << msg.clientMessageId
            << "elapsedMs=" << timer.elapsed();
}

void AggregateChatForm::showStatusMessage(const QString& text, int timeoutMs)
{
    if (!m_statusLabel) return;
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.trimmed().isEmpty());
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this, text]() {
            if (m_statusLabel && m_statusLabel->text() == text) {
                m_statusLabel->clear();
                m_statusLabel->setVisible(false);
            }
        });
    }
}

void AggregateChatForm::openMessageMedia(const MessageRecord& msg)
{
    const QString type = msg.contentType.trimmed().toLower();
    if (type != QLatin1String("image")
        && type != QLatin1String("emoji")
        && type != QLatin1String("file")
        && type != QLatin1String("video")) {
        return;
    }

    const QString path = msg.contentImagePath.trimmed();
    if (path.isEmpty() || !QFile::exists(path)) {
        showAggregateWarning(this, QStringLiteral("媒体不可用"), QStringLiteral("本地媒体文件不存在或已被移动。"));
        return;
    }

    const bool imagePath = isImageFilePath(path);
    if (type == QLatin1String("image") || type == QLatin1String("emoji")) {
        if (!imagePath) {
            showAggregateWarning(this, QStringLiteral("图片不可预览"), QStringLiteral("该消息没有可预览的图片文件。"));
            return;
        }
        ImagePreviewDialog dlg(path, this);
        dlg.exec();
        return;
    }

    if (type == QLatin1String("file")) {
        if (imagePath) {
            ImagePreviewDialog dlg(path, this);
            dlg.setWindowTitle(QStringLiteral("文件截图证据"));
            dlg.exec();
            showStatusMessage(QStringLiteral("当前仅有文件消息截图证据，未获取到原始文件"), 5000);
            return;
        }
        openLocalFileWithDefaultApp(this, path, QStringLiteral("打开文件失败"));
        return;
    }

    if (type == QLatin1String("video")) {
        if (imagePath) {
            ImagePreviewDialog dlg(path, this);
            dlg.setWindowTitle(QStringLiteral("视频截图证据"));
            dlg.exec();
            showStatusMessage(QStringLiteral("当前仅有视频消息截图证据，未获取到可播放视频"), 5000);
            return;
        }
        VideoPreviewDialog dlg(path, this);
        dlg.exec();
    }
}

bool AggregateChatForm::applyServiceConversationMutation(const ConversationInfo& conv, bool deleteConversation)
{
    auto& ipc = Ipc::IpcService::instance();
    QString serviceError;
    if (!ipc.connectToConfiguredService(&serviceError)) {
        qInfo() << "[AggregateChatForm] service mutation skipped: Python service unavailable"
                << "conversationId=" << conv.id
                << "error=" << serviceError;
        if (RuntimeMode::ownsBusinessDatabase()) {
            showAggregateWarning(this,
                                 QStringLiteral("Python 服务端"),
                                 QStringLiteral("Python 服务未启动，无法修改服务端会话数据。请先启动 Python 服务后再操作。"));
            return false;
        }
        return true;
    }

    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    QString error;
    const QJsonObject response = deleteConversation
        ? ipc.deleteConversationOnService(
              conv.platform,
              conv.accountId,
              conv.platformConversationId,
              5000,
              &status,
              &error)
        : ipc.clearConversationMessages(
              conv.platform,
              conv.accountId,
              conv.platformConversationId,
              5000,
              &status,
              &error);
    const bool ok = status == Ipc::ResponseStatus::Success
        && response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("success");
    if (ok) {
        if (RuntimeMode::ownsBusinessDatabase()) {
            const QJsonObject event = response.value(QStringLiteral("event")).toObject();
            if (!ipc.dispatchPlatformEvent(event)) {
                qWarning() << "[AggregateChatForm] service mutation succeeded without dispatchable event"
                           << "conversationId=" << conv.id
                           << "delete=" << deleteConversation;
                reloadFromLocalCache();
            }
        }
        return true;
    }

    QString detail = error;
    if (detail.isEmpty())
        detail = response.value(QStringLiteral("error")).toString();
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    if (detail == QLatin1String("observer_active")) {
        detail = QStringLiteral("平台监听中，无法清空/删除。请先停止监听后再操作。");
    } else if (detail == QLatin1String("recent_observation")) {
        const int quietSeconds = qMax(1, int(result.value(QStringLiteral("quiet_seconds_required")).toDouble(3.0)));
        detail = QStringLiteral("刚检测到平台消息观察事件。请停止监听并等待约 %1 秒后再操作。").arg(quietSeconds);
    }
    if (detail.isEmpty())
        detail = QStringLiteral("unknown_error");

    QMessageBox warnBox(this);
    warnBox.setIcon(QMessageBox::Warning);
    warnBox.setWindowTitle(QStringLiteral("Python 服务端"));
    warnBox.setText(deleteConversation
                        ? QStringLiteral("服务端删除会话失败，已取消本地删除。")
                        : QStringLiteral("服务端清空聊天记录失败，已取消本地清空。"));
    warnBox.setInformativeText(detail);
    warnBox.setStandardButtons(QMessageBox::Ok);
    warnBox.setStyleSheet(aggregateMessageBoxContrastStyle());
    warnBox.exec();
    return false;
}

void AggregateChatForm::updateAggregateAiControlsVisibility()
{
    const bool convOk = m_currentConvId > 0;
    const bool onChat = m_centerStack && m_centerStack->currentWidget() == m_chatArea;
    const bool showControls = convOk && onChat;
    const bool aiBusy = m_aggregateAiGenerating || m_autoReplyBusy || m_customerProfileBusy;
    if (m_btnAiModelPick) {
        m_btnAiModelPick->setVisible(showControls);
        m_btnAiModelPick->setEnabled(showControls && !aiBusy);
    }
    if (m_btnAiGenerate) {
        m_btnAiGenerate->setVisible(showControls);
        m_btnAiGenerate->setEnabled(showControls && !aiBusy);
    }
    if (m_btnAutoReplyToggle) {
        m_btnAutoReplyToggle->setVisible(showControls);
        m_btnAutoReplyToggle->setEnabled(showControls && !m_aggregateAiGenerating && !m_customerProfileBusy);
    }
    if (m_btnOrganizeCustomerProfile) {
        m_btnOrganizeCustomerProfile->setEnabled(showControls && !aiBusy);
    }
    refreshAutoReplyToggleButtonUi();
    refreshCustomerProfilePanel();
}

bool AggregateChatForm::isAggregateAutoReplyEnabled() const
{
    QSettings s = AppSettings::create();
    return s.value(QStringLiteral("aggregateAutoReply/enabled"), false).toBool()
        && !s.value(QStringLiteral("aggregateAutoReply/emergencyUserStop"), false).toBool();
}

void AggregateChatForm::setAggregateAutoReplyEnabled(bool enabled)
{
    QSettings s = AppSettings::create();
    s.setValue(QStringLiteral("aggregateAutoReply/enabled"), enabled);
    s.setValue(QStringLiteral("aggregateAutoReply/emergencyUserStop"), !enabled);
    refreshAutoReplyToggleButtonUi();
}

void AggregateChatForm::refreshAutoReplyToggleButtonUi()
{
    if (!m_btnAutoReplyToggle)
        return;

    const bool enabled = isAggregateAutoReplyEnabled();
    m_btnAutoReplyToggle->setChecked(enabled);
    const QString label = enabled ? QStringLiteral("停止自动回复")
                                  : QStringLiteral("开启自动回复");
    m_btnAutoReplyToggle->setText(QString());
    m_btnAutoReplyToggle->setIcon(QIcon(enabled
        ? QStringLiteral(":/aggregate_reception_icons/stop_auto_reply_icon.svg")
        : QStringLiteral(":/aggregate_reception_icons/turn_on_auto_reply_icon.svg")));
    m_btnAutoReplyToggle->setToolTip(
        enabled ? QStringLiteral("停止 AI 自动回复，当前自动回复任务会被中止。")
                : QStringLiteral("开启 AI 自动回复。开启前会确认误发、合规与费用风险。"));
    m_btnAutoReplyToggle->setAccessibleName(label);
}

void AggregateChatForm::setAggregateAiBusy(bool busy)
{
    m_aggregateAiGenerating = busy;
    if (m_btnSend)
        m_btnSend->setEnabled(!busy);
    if (m_inputEdit)
        m_inputEdit->setReadOnly(busy);
    if (busy)
        showStatusMessage(QStringLiteral("AI 正在生成草稿..."), 0);
    updateAggregateAiControlsVisibility();
}

void AggregateChatForm::abortAggregateAiRequest()
{
    if (m_aggregateAiGenerating && m_aggregateAiRequestEventId > 0) {
        AiRequestEventDao().appendStage(m_aggregateAiRequestEventId, m_currentConvId,
                                         QStringLiteral("canceled"));
        AiRequestEventDao().cancelEvent(m_aggregateAiRequestEventId,
                                        int(m_aggregateAiRequestTimer.elapsed()));
        m_aggregateAiRequestEventId = 0;
        m_aggregateAiFirstTokenMs = 0;
    }
    if (m_autoReplyBusy && m_autoReplyRequestEventId > 0) {
        AiRequestEventDao().appendStage(m_autoReplyRequestEventId, m_autoReplyTargetConvId,
                                         QStringLiteral("canceled"));
        AiRequestEventDao().cancelEvent(m_autoReplyRequestEventId,
                                        int(m_autoReplyRequestTimer.elapsed()));
        m_autoReplyRequestEventId = 0;
        m_autoReplyFirstTokenMs = 0;
    }
    if (m_customerProfileBusy && m_customerProfileRequestEventId > 0) {
        AiRequestEventDao().appendStage(m_customerProfileRequestEventId, m_currentConvId,
                                         QStringLiteral("canceled"));
        AiRequestEventDao().cancelEvent(m_customerProfileRequestEventId,
                                        int(m_customerProfileRequestTimer.elapsed()));
        m_customerProfileRequestEventId = 0;
        m_customerProfileFirstTokenMs = 0;
    }
    if (!m_aggregateAiIpcRequestId.isEmpty()) {
        Ipc::IpcService::instance().cancelRequest(m_aggregateAiIpcRequestId);
        m_aggregateAiIpcRequestId.clear();
    }
    clearStreamingSession(m_aggregateAiSession);
    clearStreamingSession(m_autoReplySession);
    clearStreamingSession(m_customerProfileSession);
    if (m_aggregateAiGenerating)
        setAggregateAiBusy(false);
    if (m_autoReplyBusy) {
        m_autoReplyBusy = false;
        m_autoReplyTargetConvId = -1;
        m_autoReplyAccumulated.clear();
        m_autoReplyTraceId.clear();
        m_autoReplyAttachments.clear();
        m_autoReplyEmailRecipient.clear();
        m_autoReplyEmailScene.clear();
        m_autoReplyEmailTemplateId.clear();
    }
    if (m_customerProfileBusy) {
        m_customerProfileBusy = false;
        m_customerProfileAccumulated.clear();
    }
    updateAggregateAiControlsVisibility();
    refreshRightBarMetrics();
}

void AggregateChatForm::abortAutoReplyRequest()
{
    if (m_autoReplyBusy || m_autoReplyRequestEventId > 0 || !m_autoReplyTraceId.isEmpty()) {
        appendAggregateAutoReplyLog(
            QStringLiteral("abort_requested"),
            {
                aggregateLogField(QStringLiteral("target_conversation_id"), QString::number(m_autoReplyTargetConvId)),
                aggregateLogField(QStringLiteral("auto_reply_busy"), aggregateBoolLabel(m_autoReplyBusy)),
                aggregateLogField(QStringLiteral("trace_id"), m_autoReplyTraceId),
                aggregateLogField(QStringLiteral("request_event_id"), QString::number(m_autoReplyRequestEventId)),
                aggregateLogField(QStringLiteral("elapsed_ms"),
                                  QString::number(m_autoReplyRequestTimer.isValid()
                                                      ? int(m_autoReplyRequestTimer.elapsed())
                                                      : 0)),
            });
    }
    if (m_autoReplyBusy && m_autoReplyRequestEventId > 0) {
        AiRequestEventDao().appendStage(m_autoReplyRequestEventId, m_autoReplyTargetConvId,
                                         QStringLiteral("canceled"));
        AiRequestEventDao().cancelEvent(m_autoReplyRequestEventId,
                                        int(m_autoReplyRequestTimer.elapsed()));
        m_autoReplyRequestEventId = 0;
        m_autoReplyFirstTokenMs = 0;
    }
    clearStreamingSession(m_autoReplySession);
    if (m_autoReplyBusy) {
        m_autoReplyBusy = false;
        m_autoReplyTargetConvId = -1;
        m_autoReplyAccumulated.clear();
        m_autoReplyTraceId.clear();
        m_autoReplyAttachments.clear();
        m_autoReplyEmailRecipient.clear();
        m_autoReplyEmailScene.clear();
        m_autoReplyEmailTemplateId.clear();
    }
    updateAggregateAiControlsVisibility();
    refreshRightBarMetrics();
}

void AggregateChatForm::clearStreamingSession(IAiStreamingSession*& session)
{
    if (!session)
        return;
    session->disconnect(this);
    session->abort();
    session->deleteLater();
    session = nullptr;
}

void AggregateChatForm::startAutoReplyEmailAction(int conversationId,
                                                  qint64 requestEventId,
                                                  const QString& traceId,
                                                  const QString& recipient,
                                                  const QString& scene,
                                                  const QString& templateId)
{
    if (conversationId <= 0 || traceId.trimmed().isEmpty()) {
        finishAutoReplyEmailAction(conversationId,
                                   requestEventId,
                                   traceId,
                                   QJsonObject{{QStringLiteral("status"), QStringLiteral("error")},
                                               {QStringLiteral("error"), QStringLiteral("invalid_context")}},
                                   Ipc::ResponseStatus::Error,
                                   QStringLiteral("invalid_context"),
                                   0);
        return;
    }

    const QString trimmedRecipient = recipient.trimmed();
    if (trimmedRecipient.isEmpty()) {
        finishAutoReplyEmailAction(conversationId,
                                   requestEventId,
                                   traceId,
                                   QJsonObject{{QStringLiteral("status"), QStringLiteral("error")},
                                               {QStringLiteral("error"), QStringLiteral("invalid_recipient")}},
                                   Ipc::ResponseStatus::Error,
                                   QStringLiteral("invalid_recipient"),
                                   0);
        return;
    }
    const QString trimmedTemplateId = templateId.trimmed();
    if (trimmedTemplateId.isEmpty()) {
        finishAutoReplyEmailAction(conversationId,
                                   requestEventId,
                                   traceId,
                                   QJsonObject{{QStringLiteral("status"), QStringLiteral("error")},
                                               {QStringLiteral("error"), QStringLiteral("missing_template_id")},
                                               {QStringLiteral("detail"), QStringLiteral("邮件模板未明确，不能自动发送邮件")}},
                                   Ipc::ResponseStatus::Error,
                                   QStringLiteral("missing_template_id"),
                                   0);
        return;
    }

    appendAggregateAutoReplyLog(
        QStringLiteral("email_action_started"),
        {
            aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
            aggregateLogField(QStringLiteral("trace_id"), traceId),
            aggregateLogField(QStringLiteral("request_event_id"), QString::number(requestEventId)),
            aggregateLogField(QStringLiteral("recipient_chars"), QString::number(trimmedRecipient.size())),
            aggregateLogField(QStringLiteral("scene"), scene.trimmed().isEmpty()
                                                      ? QStringLiteral("link_request")
                                                      : scene.trimmed()),
            aggregateLogField(QStringLiteral("template_id"), trimmedTemplateId),
        });
    if (requestEventId > 0) {
        AiRequestEventDao().appendStage(requestEventId,
                                        conversationId,
                                        QStringLiteral("email_action_started"),
                                        QStringLiteral("workflow=%1 template_id=%2")
                                            .arg(scene.trimmed().isEmpty()
                                                     ? QStringLiteral("link_request")
                                                     : scene.trimmed(),
                                                 trimmedTemplateId));
    }

    const QString endpoint = Ipc::IpcService::instance().serviceEndpoint();
    QPointer<AggregateChatForm> guard(this);
    QThread* worker = QThread::create([guard,
                                       endpoint,
                                       conversationId,
                                       requestEventId,
                                       traceId,
                                       trimmedRecipient,
                                       scene,
                                       trimmedTemplateId]() {
        AggregateWorkflowEmailResult result =
            sendAggregateWorkflowEmailOnWorker(endpoint,
                                               conversationId,
                                               requestEventId,
                                               traceId,
                                               trimmedRecipient,
                                               scene,
                                               trimmedTemplateId);
        if (!guard)
            return;
        QMetaObject::invokeMethod(guard.data(), [guard, result]() {
            if (!guard)
                return;
            guard->finishAutoReplyEmailAction(result.conversationId,
                                              result.requestEventId,
                                              result.traceId,
                                              result.response,
                                              result.status,
                                              result.error,
                                              result.elapsedMs);
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void AggregateChatForm::finishAutoReplyEmailAction(int conversationId,
                                                   qint64 requestEventId,
                                                   const QString& traceId,
                                                   const QJsonObject& response,
                                                   Ipc::ResponseStatus status,
                                                   const QString& error,
                                                   int elapsedMs)
{
    if (!m_autoReplyBusy
        || conversationId != m_autoReplyTargetConvId
        || traceId != m_autoReplyTraceId) {
        appendAggregateAutoReplyLog(
            QStringLiteral("email_action_result_ignored"),
            {
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("trace_id"), traceId),
                aggregateLogField(QStringLiteral("current_target_conversation_id"), QString::number(m_autoReplyTargetConvId)),
                aggregateLogField(QStringLiteral("current_trace_id"), m_autoReplyTraceId),
                aggregateLogField(QStringLiteral("auto_reply_busy"), aggregateBoolLabel(m_autoReplyBusy)),
            });
        return;
    }

    const bool success =
        status == Ipc::ResponseStatus::Success
        && response.value(QStringLiteral("status")).toString(QStringLiteral("success")) != QLatin1String("error");
    const QString detail = aggregateEmailResponseDetail(response, error);
    const QString customerMessage = success
        ? aggregateWorkflowEmailSuccessMessage()
        : aggregateWorkflowEmailFailureMessage();

    OutgoingMessagePayload payload;
    OutgoingMessagePart textPart;
    textPart.type = OutgoingPartType::Text;
    textPart.text = customerMessage;
    payload.parts.push_back(textPart);
    ConversationManager::instance().sendPayload(conversationId, payload);

    appendAggregateAutoReplyLog(
        success ? QStringLiteral("email_action_succeeded") : QStringLiteral("email_action_failed"),
        {
            aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
            aggregateLogField(QStringLiteral("trace_id"), traceId),
            aggregateLogField(QStringLiteral("request_event_id"), QString::number(requestEventId)),
            aggregateLogField(QStringLiteral("elapsed_ms"), QString::number(elapsedMs)),
            aggregateLogField(QStringLiteral("response_status"), response.value(QStringLiteral("status")).toString()),
            aggregateLogField(QStringLiteral("error_code"), response.value(QStringLiteral("error")).toString()),
            aggregateLogField(QStringLiteral("message_id"), response.value(QStringLiteral("message_id")).toString()),
            aggregateLogField(QStringLiteral("template_id"), response.value(QStringLiteral("template_id")).toString()),
            aggregateLogField(QStringLiteral("detail"), detail.left(220)),
            aggregateLogField(QStringLiteral("customer_message"), customerMessage),
        });

    QStringList traceLines;
    traceLines << QStringLiteral("\n-------------------- Aggregate Workflow Email Action --------------------");
    traceLines << QStringLiteral("trace_id: %1").arg(traceId);
    traceLines << QStringLiteral("conversation_id: %1").arg(conversationId);
    traceLines << QStringLiteral("time: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    traceLines << QStringLiteral("status: %1").arg(success ? QStringLiteral("success") : QStringLiteral("failed"));
    traceLines << QStringLiteral("transport_status: %1").arg(Ipc::toString(status));
    traceLines << QStringLiteral("elapsed_ms: %1").arg(elapsedMs);
    traceLines << QStringLiteral("message_id: %1").arg(response.value(QStringLiteral("message_id")).toString());
    traceLines << QStringLiteral("template_id: %1").arg(response.value(QStringLiteral("template_id")).toString());
    traceLines << QStringLiteral("error_code: %1").arg(response.value(QStringLiteral("error")).toString());
    if (!detail.isEmpty())
        traceLines << QStringLiteral("detail: %1").arg(detail.left(300));
    traceLines << QStringLiteral("customer_message: %1").arg(customerMessage);
    traceLines << QStringLiteral("-----------------------------------------------------------------------");
    appendAggregateAiTraceLog(traceLines.join(QLatin1Char('\n')) + QLatin1Char('\n'));

    if (requestEventId > 0) {
        AiRequestEventDao eventDao;
        if (success) {
            eventDao.appendStage(requestEventId,
                                 conversationId,
                                 QStringLiteral("email_action_succeeded"),
                                 response.value(QStringLiteral("message_id")).toString());
            eventDao.appendStage(requestEventId, conversationId, QStringLiteral("send_submitted"));
            eventDao.completeEvent(requestEventId,
                                   int(m_autoReplyRequestTimer.elapsed()),
                                   m_autoReplyFirstTokenMs,
                                   customerMessage.size());
        } else {
            eventDao.appendStage(requestEventId,
                                 conversationId,
                                 QStringLiteral("email_action_failed"),
                                 detail.left(120));
            eventDao.appendStage(requestEventId,
                                 conversationId,
                                 QStringLiteral("send_failure_notice_submitted"));
            eventDao.failEvent(requestEventId,
                               int(m_autoReplyRequestTimer.elapsed()),
                               detail.isEmpty() ? error : detail);
        }
    }

    m_autoReplyBusy = false;
    m_autoReplyTargetConvId = -1;
    m_autoReplyTraceId.clear();
    m_autoReplyAccumulated.clear();
    m_autoReplyAttachments.clear();
    m_autoReplyEmailRecipient.clear();
    m_autoReplyEmailScene.clear();
    m_autoReplyEmailTemplateId.clear();
    m_autoReplyRequestEventId = 0;
    m_autoReplyFirstTokenMs = 0;
    updateAggregateAiControlsVisibility();
    refreshRightBarMetrics();
    schedulePythonServiceBackfill(300);
    showStatusMessage(success
                          ? QStringLiteral("邮件已发送，已自动提醒客户查收")
                          : QStringLiteral("邮件发送失败，已自动发送异常处理提示"),
                      success ? 4000 : 6000);
}

void AggregateChatForm::destroyStreamingSessionNow(IAiStreamingSession*& session)
{
    if (!session)
        return;
    IAiStreamingSession* doomed = session;
    session = nullptr;
    doomed->disconnect(this);
    doomed->abort();
    delete doomed;
}

void AggregateChatForm::shutdownTransientWork()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    if (m_messageRefreshTimer)
        m_messageRefreshTimer->stop();
    if (m_sendTimelineTimer)
        m_sendTimelineTimer->stop();
    if (m_draftSaveTimer)
        m_draftSaveTimer->stop();
    if (m_pythonBackfillTimer)
        m_pythonBackfillTimer->stop();
    if (m_adaptiveRelayoutTimer)
        m_adaptiveRelayoutTimer->stop();
    if (m_chatInputRelayoutTimer)
        m_chatInputRelayoutTimer->stop();

    if (m_modelSheetWidthAnim) {
        QVariantAnimation* anim = m_modelSheetWidthAnim;
        m_modelSheetWidthAnim = nullptr;
        anim->stop();
        delete anim;
    }

    if (m_chatInputOverlayHost)
        m_chatInputOverlayHost->removeEventFilter(this);

    if (!m_aggregateAiIpcRequestId.isEmpty()) {
        Ipc::IpcService::instance().cancelRequest(m_aggregateAiIpcRequestId);
        m_aggregateAiIpcRequestId.clear();
    }

    destroyStreamingSessionNow(m_aggregateAiSession);
    destroyStreamingSessionNow(m_autoReplySession);
    destroyStreamingSessionNow(m_customerProfileSession);

    m_aggregateAiGenerating = false;
    m_autoReplyBusy = false;
    m_customerProfileBusy = false;
    m_autoReplyTargetConvId = -1;
    m_autoReplyTraceId.clear();
    m_autoReplyEmailRecipient.clear();
    m_autoReplyEmailScene.clear();
    m_autoReplyEmailTemplateId.clear();
    m_aggregateAiRequestEventId = 0;
    m_autoReplyRequestEventId = 0;
    m_customerProfileRequestEventId = 0;

    auto& mgr = ConversationManager::instance();
    disconnect(&mgr, nullptr, this, nullptr);
    auto& ipc = Ipc::IpcService::instance();
    disconnect(&ipc, nullptr, this, nullptr);
    disconnect(&PythonServiceController::instance(), nullptr, this, nullptr);
}

void AggregateChatForm::refreshAggregateAiModelButtonUi()
{
    if (m_btnAiModelPick)
        m_btnAiModelPick->setText(aggregateModelMenuLabel(m_aggregateAiSessionModelKey));
    if (m_aggregateAiModelMenu) {
        for (QAction* a : m_aggregateAiModelMenu->actions()) {
            const QString k = a->data().toString();
            a->setChecked(k == m_aggregateAiSessionModelKey);
        }
    }
}

void AggregateChatForm::onAggregateAiModelMenuTriggered(QAction* action)
{
    if (!action)
        return;
    const QString key = action->data().toString();
    if (key.isEmpty() || key == m_aggregateAiSessionModelKey)
        return;
    m_aggregateAiSessionModelKey = key;
    QSettings s = AppSettings::create();
    s.setValue(QStringLiteral("aggregateAi/sessionModelKey"), key);
    refreshAggregateAiModelButtonUi();
    refreshRightBarMetrics();
}

void AggregateChatForm::onOrganizeCustomerProfileClicked()
{
    if (m_currentConvId <= 0 || m_customerProfileBusy)
        return;
    if (m_aggregateAiGenerating || m_autoReplyBusy) {
        showStatusMessage(QStringLiteral("AI 正在处理其他任务，请稍后再整理客户信息"), 5000);
        return;
    }
    if (!m_aiChatService) {
        showStatusMessage(QStringLiteral("AI 服务未初始化"), 4000);
        return;
    }

    AggregateAiBuiltRequest built =
        m_aiChatService->buildAggregateCustomerProfileRequest(m_currentConvId, m_aggregateAiSessionModelKey);
    if (!handleAggregateBuildFailure(this, built, false, nullptr))
        return;

    m_customerProfileAccumulated.clear();
    m_customerProfileRequestTimer.restart();
    m_customerProfileFirstTokenMs = 0;
    built.request.extraRootFields.insert(QStringLiteral("max_tokens"), 360);
    clearStreamingSession(m_customerProfileSession);
    m_customerProfileSession = m_aiChatService->createSession(built.config, built.request, this);
    connect(m_customerProfileSession, &IAiStreamingSession::delta,
            this, &AggregateChatForm::onCustomerProfileStreamDelta);
    connect(m_customerProfileSession, &IAiStreamingSession::completed,
            this, &AggregateChatForm::onCustomerProfileCompleted);
    connect(m_customerProfileSession, &IAiStreamingSession::failed,
            this, &AggregateChatForm::onCustomerProfileFailed);

    m_customerProfileRequestEventId = AiRequestEventDao().beginEvent(
        QStringLiteral("aggregate_customer_profile"),
        m_currentConvId,
        m_aggregateAiSessionModelKey,
        built.config.model,
        QStringLiteral("customer-profile"));
    if (m_customerProfileRequestEventId > 0) {
        AiRequestEventDao eventDao;
        eventDao.appendStage(m_customerProfileRequestEventId, m_currentConvId,
                             QStringLiteral("profile_started"));
        eventDao.appendStage(m_customerProfileRequestEventId, m_currentConvId,
                             QStringLiteral("context_ready"));
        eventDao.appendStage(m_customerProfileRequestEventId, m_currentConvId,
                             QStringLiteral("request_sent"),
                             aggregateModelMenuLabel(m_aggregateAiSessionModelKey));
    }

    setCustomerProfileBusy(true);
    showStatusMessage(QStringLiteral("正在整理客户信息..."), 0);
    m_customerProfileSession->start();
}

void AggregateChatForm::onGenerateAiDraftClicked()
{
    if (m_currentConvId <= 0 || m_aggregateAiGenerating)
        return;
    clearSuggestedImageAttachments();
    if (m_autoReplyBusy) {
        showStatusMessage(QStringLiteral("自动回复处理中，请先停止自动回复或稍后再生成草稿"), 5000);
        return;
    }
    if (!m_aiChatService) {
        showStatusMessage(QStringLiteral("AI 服务未初始化"), 4000);
        return;
    }

    AggregateReplyBuildContext replyContext =
        buildAggregateReplyContext(m_aiChatService,
                                   m_currentConvId,
                                   m_aggregateAiSessionModelKey,
                                   QStringLiteral("aggregate_manual_generate"));
    m_aggregateAiTraceId = replyContext.traceId;
    if (!handleAggregateBuildFailure(this, replyContext.built, false, nullptr)) {
        appendAggregateAiTraceFinish(replyContext.traceId,
                                     m_currentConvId,
                                     QStringLiteral("build_failed"),
                                     QString(),
                                     0,
                                     0,
                                     replyContext.built.failureDetail);
        m_aggregateAiTraceId.clear();
        return;
    }

    QVector<SuggestedImageAttachment> suggestedImages;
    suggestedImages.reserve(qMin(3, int(replyContext.imageCandidates.size())));
    for (const AggregateImageCandidate& candidate : std::as_const(replyContext.imageCandidates)) {
        if (!candidate.shouldAttach)
            continue;
        const QString path = candidate.filePath.trimmed();
        if (path.isEmpty() || !QFileInfo::exists(path))
            continue;
        if (candidate.riskTags.contains(QStringLiteral("analysis_failed"), Qt::CaseInsensitive))
            continue;
        suggestedImages.push_back(aggregateSuggestedImageFromCandidate(candidate));
        if (suggestedImages.size() >= 3)
            break;
    }
    setSuggestedImageAttachments(suggestedImages);

    m_aggregateAiBaseline = m_inputEdit ? m_inputEdit->toPlainText() : QString();
    m_aggregateAiAccumulated.clear();
    m_aggregateAiIpcRequestId.clear();

    clearStreamingSession(m_aggregateAiSession);
    m_aggregateAiSession = m_aiChatService->createSession(
        replyContext.built.config, replyContext.built.request, this);
    connect(m_aggregateAiSession, &IAiStreamingSession::delta, this, &AggregateChatForm::onAggregateAiStreamDelta);
    connect(m_aggregateAiSession, &IAiStreamingSession::completed, this, &AggregateChatForm::onAggregateAiCompleted);
    connect(m_aggregateAiSession, &IAiStreamingSession::failed, this, &AggregateChatForm::onAggregateAiFailed);
    m_aggregateAiRequestTimer.restart();
    m_aggregateAiFirstTokenMs = 0;
    m_aggregateAiRequestEventId = AiRequestEventDao().beginEvent(
        QStringLiteral("aggregate_manual"),
        m_currentConvId,
        replyContext.runtimeConfig.sessionModelKey,
        replyContext.built.config.model,
        QStringLiteral("manual"));
    if (m_aggregateAiRequestEventId > 0) {
        AiRequestEventDao eventDao;
        eventDao.appendStage(m_aggregateAiRequestEventId, m_currentConvId,
                             QStringLiteral("manual_started"));
        if (!replyContext.knowledgeSnippets.isEmpty()) {
            eventDao.appendStage(m_aggregateAiRequestEventId, m_currentConvId,
                                 QStringLiteral("knowledge_retrieved"),
                                 QString::number(replyContext.knowledgeSnippets.size()));
        }
        eventDao.appendStage(m_aggregateAiRequestEventId, m_currentConvId,
                             QStringLiteral("context_ready"));
        eventDao.appendStage(m_aggregateAiRequestEventId, m_currentConvId,
                             QStringLiteral("request_sent"),
                             aggregateModelMenuLabel(replyContext.runtimeConfig.sessionModelKey));
    }
    setAggregateAiBusy(true);
    if (!replyContext.knowledgeStatus.isEmpty())
        showStatusMessage(replyContext.knowledgeStatus,
                          replyContext.knowledgeSnippets.isEmpty() ? 4000 : 5000);
    m_aggregateAiSession->start();
}

void AggregateChatForm::onIpcAiSuggestionReceived(const Ipc::AiSuggestionResponse& response)
{
    if (response.requestId != m_aggregateAiIpcRequestId)
        return;

    m_aggregateAiIpcRequestId.clear();
    setAggregateAiBusy(false);

    if (response.status != Ipc::ResponseStatus::Success) {
        showStatusMessage(QStringLiteral("AI 建议失败：%1").arg(response.errorMessage.left(120)), 6000);
        return;
    }
    if (response.suggestions.isEmpty()) {
        showStatusMessage(QStringLiteral("AI 未返回可用建议"), 5000);
        return;
    }

    const QString suggestion = response.suggestions.first().trimmed();
    if (suggestion.isEmpty()) {
        showStatusMessage(QStringLiteral("AI 返回了空建议"), 5000);
        return;
    }

    if (m_inputEdit)
        m_inputEdit->setPlainText(m_aggregateAiBaseline + suggestion);
    persistCurrentDraft();
    showStatusMessage(QStringLiteral("AI 草稿已生成，可直接发送或继续修改"), 4000);
}

void AggregateChatForm::onIpcRequestFailed(const QString& requestId, const QString& reason)
{
    if (requestId != m_aggregateAiIpcRequestId)
        return;

    m_aggregateAiIpcRequestId.clear();
    setAggregateAiBusy(false);
    showStatusMessage(QStringLiteral("AI 建议失败：%1").arg(reason.left(120)), 6000);
}

void AggregateChatForm::tryAggregateAutoReply(int conversationId, const QString& triggerTag)
{
    m_autoReplyAttachments.clear();
    if (conversationId <= 0 || !m_aiChatService) {
        appendAggregateAutoReplyLog(
            QStringLiteral("skip_invalid_context"),
            {
                aggregateLogField(QStringLiteral("trigger"), triggerTag),
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("has_ai_service"), aggregateBoolLabel(m_aiChatService != nullptr)),
            });
        return;
    }
    if (!isAggregateAutoReplyEnabled()) {
        appendAggregateAutoReplyLog(
            QStringLiteral("skip_auto_disabled"),
            {
                aggregateLogField(QStringLiteral("trigger"), triggerTag),
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
            });
        return;
    }

    const auto conv = m_conversationService
                          ? m_conversationService->conversationById(conversationId)
                          : std::optional<ConversationInfo>();
    const QString platform = conv ? conv->platform.trimmed().toLower() : QString();
    appendAggregateAutoReplyLog(
        QStringLiteral("try_start"),
        {
            aggregateLogField(QStringLiteral("trigger"), triggerTag),
            aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
            aggregateLogField(QStringLiteral("current_conversation_id"), QString::number(m_currentConvId)),
            aggregateLogField(QStringLiteral("platform"), platform),
            aggregateLogField(QStringLiteral("platform_conversation_id"),
                              conv ? conv->platformConversationId : QString()),
            aggregateLogField(QStringLiteral("customer_name"), conv ? conv->customerName : QString()),
            aggregateLogField(QStringLiteral("service_listening_platforms"),
                              aggregateListLabel(sortedAggregatePlatformSet(m_serviceListeningPlatforms))),
            aggregateLogField(QStringLiteral("manager_listening_platforms"),
                              aggregateListLabel(aggregateManagerListeningPlatforms())),
            aggregateLogField(QStringLiteral("manual_ai_busy"), aggregateBoolLabel(m_aggregateAiGenerating)),
            aggregateLogField(QStringLiteral("auto_reply_busy"), aggregateBoolLabel(m_autoReplyBusy)),
        });
    if (platform.isEmpty()
        || (!m_serviceListeningPlatforms.contains(platform)
            && !ConversationManager::instance().isPlatformListening(platform))) {
        appendAggregateAutoReplyLog(
            QStringLiteral("skip_platform_not_listening"),
            {
                aggregateLogField(QStringLiteral("trigger"), triggerTag),
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("platform"), platform),
                aggregateLogField(QStringLiteral("service_listening_platforms"),
                                  aggregateListLabel(sortedAggregatePlatformSet(m_serviceListeningPlatforms))),
                aggregateLogField(QStringLiteral("manager_listening_platforms"),
                                  aggregateListLabel(aggregateManagerListeningPlatforms())),
            });
        qDebug() << "[AggregateAutoReply] skip platform not listening trigger=" << triggerTag
                 << "conv=" << conversationId
                 << "platform=" << platform;
        return;
    }

    if (!m_conversationService || !m_conversationService->isAggregateAutoReplyCandidate(conversationId)) {
        appendAggregateAutoReplyLog(
            QStringLiteral("skip_not_candidate"),
            {
                aggregateLogField(QStringLiteral("trigger"), triggerTag),
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("platform"), conv ? conv->platform : QString()),
                aggregateLogField(QStringLiteral("has_conversation_service"),
                                  aggregateBoolLabel(m_conversationService != nullptr)),
            });
        qDebug() << "[AggregateAutoReply] skip eligibility trigger=" << triggerTag << "conv=" << conversationId
                 << "platform=" << (conv ? conv->platform : QString());
        return;
    }

    if (m_aggregateAiGenerating || m_autoReplyBusy) {
        appendAggregateAutoReplyLog(
            QStringLiteral("skip_busy"),
            {
                aggregateLogField(QStringLiteral("trigger"), triggerTag),
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("manual_ai_busy"), aggregateBoolLabel(m_aggregateAiGenerating)),
                aggregateLogField(QStringLiteral("auto_reply_busy"), aggregateBoolLabel(m_autoReplyBusy)),
                aggregateLogField(QStringLiteral("target_conversation_id"), QString::number(m_autoReplyTargetConvId)),
                aggregateLogField(QStringLiteral("trace_id"), m_autoReplyTraceId),
            });
        qInfo() << "[AggregateAutoReply] skip busy trigger=" << triggerTag << "conv=" << conversationId;
        return;
    }

    const AggregateReplyRuntimeConfig autoRuntime =
        resolveAggregateReplyRuntimeConfig(conversationId, m_aggregateAiSessionModelKey);
    if (!autoRuntime.usingRobot) {
        appendAggregateAutoReplyLog(
            QStringLiteral("skip_robot_not_ready"),
            {
                aggregateLogField(QStringLiteral("trigger"), triggerTag),
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("platform"), autoRuntime.platform),
                aggregateLogField(QStringLiteral("config_source"), autoRuntime.source),
                aggregateLogField(QStringLiteral("config_status"), autoRuntime.statusText),
                aggregateLogField(QStringLiteral("bound_robot_id"), autoRuntime.boundRobotId),
                aggregateLogField(QStringLiteral("robot_found"), aggregateBoolLabel(autoRuntime.robotFound)),
                aggregateLogField(QStringLiteral("robot_enabled"), aggregateBoolLabel(autoRuntime.robotEnabled)),
                aggregateLogField(QStringLiteral("selected_session_model_key"), m_aggregateAiSessionModelKey),
            });
        qInfo() << "[AggregateAutoReply] skip robot not ready trigger=" << triggerTag
                << "conv=" << conversationId
                << "platform=" << autoRuntime.platform
                << "status=" << autoRuntime.statusText;
        return;
    }

    QString skipReason;
    appendAggregateAutoReplyLog(
        QStringLiteral("build_started"),
        {
            aggregateLogField(QStringLiteral("trigger"), triggerTag),
            aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
            aggregateLogField(QStringLiteral("selected_session_model_key"), m_aggregateAiSessionModelKey),
            aggregateLogField(QStringLiteral("robot_session_model_key"), autoRuntime.sessionModelKey),
            aggregateLogField(QStringLiteral("bound_robot_id"), autoRuntime.boundRobotId),
            aggregateLogField(QStringLiteral("robot_name"), autoRuntime.robotName),
            aggregateLogField(QStringLiteral("knowledge_base_ids"), autoRuntime.knowledgeBaseIds.join(QStringLiteral(","))),
            aggregateLogField(QStringLiteral("reply_tone"), autoRuntime.replyTone),
            aggregateLogField(QStringLiteral("common_address_terms"), autoRuntime.commonAddressTerms),
            aggregateLogField(QStringLiteral("allow_auto_send_images"),
                              aggregateBoolLabel(autoRuntime.allowAutoSendImages)),
            aggregateLogField(QStringLiteral("allow_auto_send_multi_messages"),
                              aggregateBoolLabel(autoRuntime.allowAutoSendMultiMessages)),
            aggregateLogField(QStringLiteral("max_auto_send_messages"),
                              QString::number(autoRuntime.maxAutoSendMessages)),
        });
    AggregateReplyBuildContext replyContext =
        buildAggregateReplyContext(m_aiChatService,
                                   conversationId,
                                   m_aggregateAiSessionModelKey,
                                   QStringLiteral("aggregate_auto_reply"));
    if (replyContext.actionPlan.emailServiceRequired
        && !replyContext.intent.email.trimmed().isEmpty()
        && !replyContext.intent.templateId.trimmed().isEmpty()) {
        appendAggregateAutoReplyLog(
            QStringLiteral("email_workflow_ready"),
            {
                aggregateLogField(QStringLiteral("trigger"), triggerTag),
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("trace_id"), replyContext.traceId),
                aggregateLogField(QStringLiteral("session_model_key"), replyContext.runtimeConfig.sessionModelKey),
                aggregateLogField(QStringLiteral("config_source"), replyContext.runtimeConfig.source),
                aggregateLogField(QStringLiteral("bound_robot_id"), replyContext.runtimeConfig.boundRobotId),
                aggregateLogField(QStringLiteral("intent"), replyContext.intent.intent),
                aggregateLogField(QStringLiteral("workflow"), replyContext.actionPlan.workflow),
                aggregateLogField(QStringLiteral("intent_source"), replyContext.intent.source),
                aggregateLogField(QStringLiteral("intent_confidence"),
                                  QString::number(replyContext.intent.confidence, 'f', 2)),
                aggregateLogField(QStringLiteral("email_recipient_chars"),
                                  QString::number(replyContext.intent.email.trimmed().size())),
                aggregateLogField(QStringLiteral("email_template_id"),
                                  replyContext.intent.templateId.trimmed()),
                aggregateLogField(QStringLiteral("built_request_ok"), aggregateBoolLabel(replyContext.built.ok())),
                aggregateLogField(QStringLiteral("build_failure_detail"), replyContext.built.failureDetail),
            });

        m_autoReplyTargetConvId = conversationId;
        m_autoReplyTraceId = replyContext.traceId;
        m_autoReplyAllowMultiMessages = false;
        m_autoReplyMaxMessages = 1;
        m_autoReplyAccumulated.clear();
        m_autoReplyAttachments.clear();
        m_autoReplyEmailRecipient = replyContext.intent.email.trimmed();
        m_autoReplyEmailScene = replyContext.intent.intent == QLatin1String("link_request")
            ? QStringLiteral("link_request")
            : (replyContext.actionPlan.workflow.trimmed().isEmpty()
                   ? QStringLiteral("workflow_email")
                   : replyContext.actionPlan.workflow.trimmed());
        m_autoReplyEmailTemplateId = replyContext.intent.templateId.trimmed();
        m_autoReplyBusy = true;
        m_autoReplyRequestTimer.restart();
        m_autoReplyFirstTokenMs = 0;
        m_autoReplyRequestEventId = AiRequestEventDao().beginEvent(
            QStringLiteral("aggregate_auto"),
            conversationId,
            replyContext.runtimeConfig.sessionModelKey,
            replyContext.built.config.model,
            triggerTag);
        if (m_autoReplyRequestEventId > 0) {
            AiRequestEventDao eventDao;
            eventDao.appendStage(m_autoReplyRequestEventId, conversationId,
                                 QStringLiteral("auto_started"));
            eventDao.appendStage(m_autoReplyRequestEventId, conversationId,
                                 QStringLiteral("context_ready"));
        }
        updateAggregateAiControlsVisibility();
        startAutoReplyEmailAction(conversationId,
                                  m_autoReplyRequestEventId,
                                  m_autoReplyTraceId,
                                  m_autoReplyEmailRecipient,
                                  m_autoReplyEmailScene,
                                  m_autoReplyEmailTemplateId);
        showStatusMessage(QStringLiteral("AI 自动回复：正在通过邮件服务发送资料..."), 0);
        qInfo() << "[AggregateAutoReply] email workflow started trigger=" << triggerTag
                << "conv=" << conversationId
                << "traceId=" << m_autoReplyTraceId
                << "templateId=" << m_autoReplyEmailTemplateId
                << "recipientChars=" << m_autoReplyEmailRecipient.size();
        return;
    }
    if (replyContext.actionPlan.needEmail) {
        const QString reason = replyContext.actionPlan.askForTemplate
            ? QStringLiteral("waiting_for_template")
            : (replyContext.actionPlan.askForEmail
                   ? QStringLiteral("waiting_for_email")
                   : QStringLiteral("email_action_not_ready"));
        appendAggregateAutoReplyLog(
            QStringLiteral("email_workflow_not_executed"),
            {
                aggregateLogField(QStringLiteral("trigger"), triggerTag),
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("trace_id"), replyContext.traceId),
                aggregateLogField(QStringLiteral("reason"), reason),
                aggregateLogField(QStringLiteral("intent"), replyContext.intent.intent),
                aggregateLogField(QStringLiteral("workflow"), replyContext.actionPlan.workflow),
                aggregateLogField(QStringLiteral("next_action"), replyContext.intent.nextAction),
                aggregateLogField(QStringLiteral("ask_for_template"),
                                  aggregateBoolLabel(replyContext.actionPlan.askForTemplate)),
                aggregateLogField(QStringLiteral("ask_for_email"),
                                  aggregateBoolLabel(replyContext.actionPlan.askForEmail)),
                aggregateLogField(QStringLiteral("email_service_required"),
                                  aggregateBoolLabel(replyContext.actionPlan.emailServiceRequired)),
                aggregateLogField(QStringLiteral("email_template_id"),
                                  replyContext.intent.templateId.trimmed()),
                aggregateLogField(QStringLiteral("email_present"),
                                  aggregateBoolLabel(!replyContext.intent.email.trimmed().isEmpty())),
                aggregateLogField(QStringLiteral("missing_slots"),
                                  replyContext.intent.missingSlots.join(QStringLiteral(","))),
            });
        qInfo() << "[AggregateAutoReply] email workflow not executed"
                << "trigger=" << triggerTag
                << "conv=" << conversationId
                << "traceId=" << replyContext.traceId
                << "reason=" << reason
                << "templateId=" << replyContext.intent.templateId.trimmed()
                << "emailPresent=" << !replyContext.intent.email.trimmed().isEmpty()
                << "missingSlots=" << replyContext.intent.missingSlots.join(QStringLiteral(","));
    }
    if (!handleAggregateBuildFailure(this, replyContext.built, true, &skipReason)) {
        appendAggregateAiTraceFinish(replyContext.traceId,
                                     conversationId,
                                     QStringLiteral("build_failed"),
                                     QString(),
                                     0,
                                     0,
                                     replyContext.built.failureDetail);
        appendAggregateAutoReplyLog(
            QStringLiteral("build_failed"),
            {
                aggregateLogField(QStringLiteral("trigger"), triggerTag),
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
                aggregateLogField(QStringLiteral("trace_id"), replyContext.traceId),
                aggregateLogField(QStringLiteral("skip_reason"), skipReason),
                aggregateLogField(QStringLiteral("session_model_key"), replyContext.runtimeConfig.sessionModelKey),
                aggregateLogField(QStringLiteral("config_source"), replyContext.runtimeConfig.source),
                aggregateLogField(QStringLiteral("config_status"), replyContext.runtimeConfig.statusText),
                aggregateLogField(QStringLiteral("bound_robot_id"), replyContext.runtimeConfig.boundRobotId),
                aggregateLogField(QStringLiteral("failure_detail"), replyContext.built.failureDetail),
                aggregateLogField(QStringLiteral("intent"), replyContext.intent.intent),
                aggregateLogField(QStringLiteral("workflow"), replyContext.actionPlan.workflow),
                aggregateLogField(QStringLiteral("intent_source"), replyContext.intent.source),
                aggregateLogField(QStringLiteral("knowledge_status"), replyContext.knowledgeStatus),
                aggregateLogField(QStringLiteral("knowledge_failure_stage"),
                                  replyContext.knowledgeTrace.failureStage),
            });
        qInfo() << "[AggregateAutoReply] skip build:" << skipReason << "trigger=" << triggerTag
                << "conv=" << conversationId;
        return;
    }
    m_autoReplyAttachments = replyContext.imageAttachments;
    appendAggregateAutoReplyLog(
        QStringLiteral("build_succeeded"),
        {
            aggregateLogField(QStringLiteral("trigger"), triggerTag),
            aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
            aggregateLogField(QStringLiteral("trace_id"), replyContext.traceId),
            aggregateLogField(QStringLiteral("session_model_key"), replyContext.runtimeConfig.sessionModelKey),
            aggregateLogField(QStringLiteral("config_source"), replyContext.runtimeConfig.source),
            aggregateLogField(QStringLiteral("bound_robot_id"), replyContext.runtimeConfig.boundRobotId),
            aggregateLogField(QStringLiteral("robot_name"), replyContext.runtimeConfig.robotName),
            aggregateLogField(QStringLiteral("reply_tone"), replyContext.runtimeConfig.replyTone),
            aggregateLogField(QStringLiteral("common_address_terms"),
                              replyContext.runtimeConfig.commonAddressTerms),
            aggregateLogField(QStringLiteral("allow_auto_send_images"),
                              aggregateBoolLabel(replyContext.runtimeConfig.allowAutoSendImages)),
            aggregateLogField(QStringLiteral("allow_auto_send_multi_messages"),
                              aggregateBoolLabel(replyContext.runtimeConfig.allowAutoSendMultiMessages)),
            aggregateLogField(QStringLiteral("max_auto_send_messages"),
                              QString::number(replyContext.runtimeConfig.maxAutoSendMessages)),
            aggregateLogField(QStringLiteral("model"), replyContext.built.config.model),
            aggregateLogField(QStringLiteral("intent"), replyContext.intent.intent),
            aggregateLogField(QStringLiteral("workflow"), replyContext.actionPlan.workflow),
            aggregateLogField(QStringLiteral("intent_source"), replyContext.intent.source),
            aggregateLogField(QStringLiteral("intent_confidence"),
                              QString::number(replyContext.intent.confidence, 'f', 2)),
            aggregateLogField(QStringLiteral("need_image_search"),
                              aggregateBoolLabel(replyContext.actionPlan.needImageSearch)),
            aggregateLogField(QStringLiteral("need_email"),
                              aggregateBoolLabel(replyContext.actionPlan.needEmail)),
            aggregateLogField(QStringLiteral("ask_for_template"),
                              aggregateBoolLabel(replyContext.actionPlan.askForTemplate)),
            aggregateLogField(QStringLiteral("ask_for_email"),
                              aggregateBoolLabel(replyContext.actionPlan.askForEmail)),
            aggregateLogField(QStringLiteral("email_service_required"),
                              aggregateBoolLabel(replyContext.actionPlan.emailServiceRequired)),
            aggregateLogField(QStringLiteral("email_template_id"),
                              replyContext.intent.templateId.trimmed()),
            aggregateLogField(QStringLiteral("knowledge_status"), replyContext.knowledgeStatus),
            aggregateLogField(QStringLiteral("knowledge_failure_stage"), replyContext.knowledgeTrace.failureStage),
            aggregateLogField(QStringLiteral("knowledge_results"),
                              QString::number(replyContext.knowledgeSnippets.size())),
            aggregateLogField(QStringLiteral("image_candidates"),
                              QString::number(replyContext.imageCandidates.size())),
            aggregateLogField(QStringLiteral("auto_image_attachments"),
                              QString::number(m_autoReplyAttachments.size())),
            aggregateLogField(QStringLiteral("auto_image_policy"),
                              replyContext.runtimeConfig.allowAutoSendImages
                                  ? QStringLiteral("allowed")
                                  : QStringLiteral("disabled_by_robot")),
        });

    m_autoReplyTargetConvId = conversationId;
    m_autoReplyTraceId = replyContext.traceId;
    m_autoReplyAllowMultiMessages = replyContext.runtimeConfig.allowAutoSendMultiMessages;
    m_autoReplyMaxMessages = replyContext.runtimeConfig.maxAutoSendMessages;
    m_autoReplyAccumulated.clear();
    m_autoReplyBusy = true;
    m_autoReplyRequestTimer.restart();
    m_autoReplyFirstTokenMs = 0;
    m_autoReplyRequestEventId = AiRequestEventDao().beginEvent(
        QStringLiteral("aggregate_auto"),
        conversationId,
        replyContext.runtimeConfig.sessionModelKey,
        replyContext.built.config.model,
        triggerTag);
    if (m_autoReplyRequestEventId > 0) {
        AiRequestEventDao eventDao;
        eventDao.appendStage(m_autoReplyRequestEventId, conversationId,
                             QStringLiteral("auto_started"));
        if (!replyContext.knowledgeSnippets.isEmpty()) {
            eventDao.appendStage(m_autoReplyRequestEventId, conversationId,
                                 QStringLiteral("knowledge_retrieved"),
                                 QString::number(replyContext.knowledgeSnippets.size()));
        }
        eventDao.appendStage(m_autoReplyRequestEventId, conversationId,
                             QStringLiteral("context_ready"));
        eventDao.appendStage(m_autoReplyRequestEventId, conversationId,
                             QStringLiteral("request_sent"),
                             aggregateModelMenuLabel(replyContext.runtimeConfig.sessionModelKey));
    }
    updateAggregateAiControlsVisibility();
    clearStreamingSession(m_autoReplySession);
    m_autoReplySession = m_aiChatService->createSession(
        replyContext.built.config, replyContext.built.request, this);
    connect(m_autoReplySession, &IAiStreamingSession::delta, this, &AggregateChatForm::onAutoReplyStreamDelta);
    connect(m_autoReplySession, &IAiStreamingSession::completed, this, &AggregateChatForm::onAutoReplyCompleted);
    connect(m_autoReplySession, &IAiStreamingSession::failed, this, &AggregateChatForm::onAutoReplyFailed);
    m_autoReplySession->start();
    appendAggregateAutoReplyLog(
        QStringLiteral("generation_started"),
        {
            aggregateLogField(QStringLiteral("trigger"), triggerTag),
            aggregateLogField(QStringLiteral("conversation_id"), QString::number(conversationId)),
            aggregateLogField(QStringLiteral("trace_id"), m_autoReplyTraceId),
            aggregateLogField(QStringLiteral("request_event_id"), QString::number(m_autoReplyRequestEventId)),
        });
    showStatusMessage(QStringLiteral("AI 自动回复处理中..."), 0);
    qInfo() << "[AggregateAutoReply] started trigger=" << triggerTag << "conv=" << conversationId;
}

void AggregateChatForm::onAutoReplyStreamDelta(const QString& delta)
{
    if (!m_autoReplyBusy)
        return;
    if (m_autoReplyFirstTokenMs <= 0) {
        m_autoReplyFirstTokenMs = int(m_autoReplyRequestTimer.elapsed());
        appendAggregateAutoReplyLog(
            QStringLiteral("first_token"),
            {
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(m_autoReplyTargetConvId)),
                aggregateLogField(QStringLiteral("trace_id"), m_autoReplyTraceId),
                aggregateLogField(QStringLiteral("first_token_ms"), QString::number(m_autoReplyFirstTokenMs)),
                aggregateLogField(QStringLiteral("delta_length"), QString::number(delta.size())),
            });
        AiRequestEventDao().appendStage(
            m_autoReplyRequestEventId,
            m_autoReplyTargetConvId,
            QStringLiteral("first_token"),
            aggregateMetricDurationLabel(m_autoReplyFirstTokenMs));
    }
    m_autoReplyAccumulated += delta;
}

void AggregateChatForm::onAutoReplyCompleted()
{
    if (!m_autoReplyBusy)
        return;
    const int cid = m_autoReplyTargetConvId;
    const QString traceId = m_autoReplyTraceId;
    m_autoReplyBusy = false;
    clearStreamingSession(m_autoReplySession);
    m_autoReplyTargetConvId = -1;
    const QString text = m_autoReplyAccumulated.trimmed();
    m_autoReplyAccumulated.clear();
    QVector<OutgoingMessagePart> imageAttachments = m_autoReplyAttachments;
    m_autoReplyAttachments.clear();
    const bool allowMultipleMessages = m_autoReplyAllowMultiMessages;
    const int maxMessages = m_autoReplyMaxMessages;
    m_autoReplyAllowMultiMessages = false;
    m_autoReplyMaxMessages = 1;

    if (text.isEmpty()) {
        const int durationMs = int(m_autoReplyRequestTimer.elapsed());
        appendAggregateAiTraceFinish(traceId,
                                     cid,
                                     QStringLiteral("empty_completion"),
                                     m_autoReplyAccumulated,
                                     durationMs,
                                     m_autoReplyFirstTokenMs,
                                     QStringLiteral("empty_completion"));
        appendAggregateAutoReplyLog(
            QStringLiteral("empty_completion"),
            {
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(cid)),
                aggregateLogField(QStringLiteral("trace_id"), traceId),
                aggregateLogField(QStringLiteral("duration_ms"), QString::number(durationMs)),
                aggregateLogField(QStringLiteral("first_token_ms"), QString::number(m_autoReplyFirstTokenMs)),
            });
        m_autoReplyTraceId.clear();
        if (m_autoReplyRequestEventId > 0) {
            AiRequestEventDao().appendStage(m_autoReplyRequestEventId, cid,
                                             QStringLiteral("failed"),
                                             QStringLiteral("模型未返回正文"));
            AiRequestEventDao().failEvent(m_autoReplyRequestEventId,
                                          int(m_autoReplyRequestTimer.elapsed()),
                                          QStringLiteral("empty_completion"));
            m_autoReplyRequestEventId = 0;
            m_autoReplyFirstTokenMs = 0;
        }
        updateAggregateAiControlsVisibility();
        refreshRightBarMetrics();
        qInfo() << "[AggregateAutoReply] empty completion conv=" << cid;
        showStatusMessage(QStringLiteral("自动回复：模型未返回正文"), 5000);
        return;
    }

    const int durationMs = int(m_autoReplyRequestTimer.elapsed());
    appendAggregateAiTraceFinish(traceId,
                                 cid,
                                 QStringLiteral("completed"),
                                 text,
                                 durationMs,
                                 m_autoReplyFirstTokenMs);
    appendAggregateAutoReplyLog(
        QStringLiteral("generation_completed"),
        {
            aggregateLogField(QStringLiteral("conversation_id"), QString::number(cid)),
            aggregateLogField(QStringLiteral("trace_id"), traceId),
            aggregateLogField(QStringLiteral("duration_ms"), QString::number(durationMs)),
            aggregateLogField(QStringLiteral("first_token_ms"), QString::number(m_autoReplyFirstTokenMs)),
            aggregateLogField(QStringLiteral("reply_length"), QString::number(text.size())),
            aggregateLogField(QStringLiteral("reply_preview"), aggregateLogPreview(text)),
        });
    const bool suppressImageAttachments =
        !imageAttachments.isEmpty() && aggregateReplyDeniesImageAvailability(text);
    if (suppressImageAttachments) {
        const int suppressedCount = imageAttachments.size();
        imageAttachments.clear();
        appendAggregateAutoReplyLog(
            QStringLiteral("image_attachments_suppressed"),
            {
                aggregateLogField(QStringLiteral("conversation_id"), QString::number(cid)),
                aggregateLogField(QStringLiteral("trace_id"), traceId),
                aggregateLogField(QStringLiteral("suppressed_count"), QString::number(suppressedCount)),
                aggregateLogField(QStringLiteral("reason"), QStringLiteral("model_denied_image_availability")),
                aggregateLogField(QStringLiteral("reply_preview"), aggregateLogPreview(text)),
            });
    }
    m_autoReplyTraceId.clear();
    if (m_autoReplyRequestEventId > 0) {
        AiRequestEventDao eventDao;
        eventDao.appendStage(
            m_autoReplyRequestEventId,
            cid,
            QStringLiteral("completed"),
            QStringLiteral("%1 字，耗时 %2")
                .arg(text.size())
                .arg(aggregateMetricDurationLabel(durationMs)));
        eventDao.appendStage(m_autoReplyRequestEventId, cid,
                             QStringLiteral("send_submitted"));
        AiRequestEventDao().completeEvent(m_autoReplyRequestEventId,
                                          durationMs,
                                          m_autoReplyFirstTokenMs,
                                          text.size());
        m_autoReplyRequestEventId = 0;
        m_autoReplyFirstTokenMs = 0;
    }
    updateAggregateAiControlsVisibility();
    refreshRightBarMetrics();

    int rawMessageCount = 0;
    const QStringList textMessages = aggregateAutoReplyTextMessages(
        text,
        allowMultipleMessages,
        maxMessages,
        &rawMessageCount);
    OutgoingMessagePayload payload;
    for (const QString& message : textMessages) {
        OutgoingMessagePart textPart;
        textPart.type = OutgoingPartType::Text;
        textPart.text = message;
        payload.parts.push_back(textPart);
    }
    for (const OutgoingMessagePart& part : imageAttachments)
        payload.parts.push_back(part);
    ConversationManager::instance().sendPayload(cid, payload);
    appendAggregateAutoReplyLog(
        QStringLiteral("send_submitted"),
        {
            aggregateLogField(QStringLiteral("conversation_id"), QString::number(cid)),
            aggregateLogField(QStringLiteral("trace_id"), traceId),
            aggregateLogField(QStringLiteral("reply_length"), QString::number(text.size())),
            aggregateLogField(QStringLiteral("multi_message_allowed"),
                              aggregateBoolLabel(allowMultipleMessages)),
            aggregateLogField(QStringLiteral("max_auto_send_messages"), QString::number(maxMessages)),
            aggregateLogField(QStringLiteral("raw_text_message_count"), QString::number(rawMessageCount)),
            aggregateLogField(QStringLiteral("sent_text_message_count"), QString::number(textMessages.size())),
            aggregateLogField(QStringLiteral("deduplicated_message_count"),
                              QString::number(qMax(0, rawMessageCount - textMessages.size()))),
            aggregateLogField(QStringLiteral("image_attachment_count"), QString::number(imageAttachments.size())),
            aggregateLogField(QStringLiteral("reply_preview"), aggregateLogPreview(text)),
        });
    schedulePythonServiceBackfill(300);
    qInfo() << "[AggregateAutoReply] sent conv=" << cid << "len=" << text.size()
            << "textMessages=" << textMessages.size()
            << "imageAttachments=" << imageAttachments.size();
    showStatusMessage(QStringLiteral("已自动发送 AI 回复"), 4000);
}

void AggregateChatForm::onAutoReplyFailed(const QString& reason)
{
    if (!m_autoReplyBusy)
        return;
    const int cid = m_autoReplyTargetConvId;
    const QString traceId = m_autoReplyTraceId;
    m_autoReplyBusy = false;
    clearStreamingSession(m_autoReplySession);
    m_autoReplyTargetConvId = -1;
    const QString partial = m_autoReplyAccumulated;
    m_autoReplyAccumulated.clear();
    m_autoReplyAttachments.clear();
    m_autoReplyEmailRecipient.clear();
    m_autoReplyEmailScene.clear();
    m_autoReplyEmailTemplateId.clear();
    m_autoReplyAllowMultiMessages = false;
    m_autoReplyMaxMessages = 1;
    const int durationMs = int(m_autoReplyRequestTimer.elapsed());
    appendAggregateAiTraceFinish(traceId,
                                 cid,
                                 QStringLiteral("failed"),
                                 partial,
                                 durationMs,
                                 m_autoReplyFirstTokenMs,
                                 reason);
    appendAggregateAutoReplyLog(
        QStringLiteral("generation_failed"),
        {
            aggregateLogField(QStringLiteral("conversation_id"), QString::number(cid)),
            aggregateLogField(QStringLiteral("trace_id"), traceId),
            aggregateLogField(QStringLiteral("duration_ms"), QString::number(durationMs)),
            aggregateLogField(QStringLiteral("first_token_ms"), QString::number(m_autoReplyFirstTokenMs)),
            aggregateLogField(QStringLiteral("partial_length"), QString::number(partial.size())),
            aggregateLogField(QStringLiteral("reason"), reason.left(300)),
        });
    m_autoReplyTraceId.clear();
    if (m_autoReplyRequestEventId > 0) {
        AiRequestEventDao().appendStage(m_autoReplyRequestEventId, cid,
                                         QStringLiteral("failed"),
                                         reason.left(120));
        AiRequestEventDao().failEvent(m_autoReplyRequestEventId,
                                      int(m_autoReplyRequestTimer.elapsed()),
                                      reason);
        m_autoReplyRequestEventId = 0;
        m_autoReplyFirstTokenMs = 0;
    }
    updateAggregateAiControlsVisibility();
    refreshRightBarMetrics();
    qInfo() << "[AggregateAutoReply] failed:" << reason;
    showStatusMessage(QStringLiteral("自动回复失败：%1").arg(reason.left(120)), 6000);
}

void AggregateChatForm::onAggregateAiStreamDelta(const QString& delta)
{
    if (!m_aggregateAiGenerating)
        return;
    if (m_aggregateAiFirstTokenMs <= 0) {
        m_aggregateAiFirstTokenMs = int(m_aggregateAiRequestTimer.elapsed());
        AiRequestEventDao().appendStage(
            m_aggregateAiRequestEventId,
            m_currentConvId,
            QStringLiteral("first_token"),
            aggregateMetricDurationLabel(m_aggregateAiFirstTokenMs));
    }
    m_aggregateAiAccumulated += delta;
    if (m_inputEdit)
        m_inputEdit->setPlainText(m_aggregateAiBaseline + m_aggregateAiAccumulated);
}

void AggregateChatForm::onAggregateAiCompleted()
{
    if (!m_aggregateAiGenerating)
        return;
    clearStreamingSession(m_aggregateAiSession);
    setAggregateAiBusy(false);
    if (m_aggregateAiAccumulated.trimmed().isEmpty()) {
        appendAggregateAiTraceFinish(m_aggregateAiTraceId,
                                     m_currentConvId,
                                     QStringLiteral("empty_completion"),
                                     m_aggregateAiAccumulated,
                                     int(m_aggregateAiRequestTimer.elapsed()),
                                     m_aggregateAiFirstTokenMs,
                                     QStringLiteral("empty_completion"));
        m_aggregateAiTraceId.clear();
        if (m_aggregateAiRequestEventId > 0) {
            AiRequestEventDao().appendStage(m_aggregateAiRequestEventId, m_currentConvId,
                                             QStringLiteral("failed"),
                                             QStringLiteral("模型未返回正文"));
            AiRequestEventDao().failEvent(m_aggregateAiRequestEventId,
                                          int(m_aggregateAiRequestTimer.elapsed()),
                                          QStringLiteral("empty_completion"));
            m_aggregateAiRequestEventId = 0;
            m_aggregateAiFirstTokenMs = 0;
        }
        refreshRightBarMetrics();
        showStatusMessage(QStringLiteral("AI 未返回可用草稿"), 5000);
        return;
    }
    appendAggregateAiTraceFinish(m_aggregateAiTraceId,
                                 m_currentConvId,
                                 QStringLiteral("completed"),
                                 m_aggregateAiAccumulated,
                                 int(m_aggregateAiRequestTimer.elapsed()),
                                 m_aggregateAiFirstTokenMs);
    m_aggregateAiTraceId.clear();
    if (m_aggregateAiRequestEventId > 0) {
        const int durationMs = int(m_aggregateAiRequestTimer.elapsed());
        AiRequestEventDao().appendStage(
            m_aggregateAiRequestEventId,
            m_currentConvId,
            QStringLiteral("completed"),
            QStringLiteral("%1 字，耗时 %2")
                .arg(m_aggregateAiAccumulated.trimmed().size())
                .arg(aggregateMetricDurationLabel(durationMs)));
        AiRequestEventDao().completeEvent(m_aggregateAiRequestEventId,
                                          durationMs,
                                          m_aggregateAiFirstTokenMs,
                                          m_aggregateAiAccumulated.trimmed().size());
        m_aggregateAiRequestEventId = 0;
        m_aggregateAiFirstTokenMs = 0;
    }
    refreshRightBarMetrics();
    persistCurrentDraft();
    showStatusMessage(QStringLiteral("AI 草稿已生成，可直接发送或继续修改"), 4000);
}

void AggregateChatForm::onAggregateAiFailed(const QString& reason)
{
    if (!m_aggregateAiGenerating)
        return;
    clearStreamingSession(m_aggregateAiSession);
    appendAggregateAiTraceFinish(m_aggregateAiTraceId,
                                 m_currentConvId,
                                 QStringLiteral("failed"),
                                 m_aggregateAiAccumulated,
                                 int(m_aggregateAiRequestTimer.elapsed()),
                                 m_aggregateAiFirstTokenMs,
                                 reason);
    m_aggregateAiTraceId.clear();
    const QString tail =
        m_aggregateAiAccumulated.isEmpty()
            ? QStringLiteral("\n\n（AI 未能完成：%1）").arg(reason)
            : QStringLiteral("\n\n（后续内容未能完成：%1）").arg(reason);
    if (m_inputEdit)
        m_inputEdit->setPlainText(m_aggregateAiBaseline + m_aggregateAiAccumulated + tail);
    persistCurrentDraft();
    if (m_aggregateAiRequestEventId > 0) {
        AiRequestEventDao().appendStage(m_aggregateAiRequestEventId, m_currentConvId,
                                         QStringLiteral("failed"),
                                         reason.left(120));
        AiRequestEventDao().failEvent(m_aggregateAiRequestEventId,
                                      int(m_aggregateAiRequestTimer.elapsed()),
                                      reason);
        m_aggregateAiRequestEventId = 0;
        m_aggregateAiFirstTokenMs = 0;
    }
    setAggregateAiBusy(false);
    refreshRightBarMetrics();
    showStatusMessage(QStringLiteral("AI 草稿生成失败：%1").arg(reason.left(120)), 6000);
}

void AggregateChatForm::onCustomerProfileStreamDelta(const QString& delta)
{
    if (!m_customerProfileBusy)
        return;
    if (m_customerProfileFirstTokenMs <= 0) {
        m_customerProfileFirstTokenMs = int(m_customerProfileRequestTimer.elapsed());
        AiRequestEventDao().appendStage(
            m_customerProfileRequestEventId,
            m_currentConvId,
            QStringLiteral("first_token"),
            aggregateMetricDurationLabel(m_customerProfileFirstTokenMs));
    }
    m_customerProfileAccumulated += delta;
}

void AggregateChatForm::onCustomerProfileCompleted()
{
    if (!m_customerProfileBusy)
        return;
    clearStreamingSession(m_customerProfileSession);

    const int durationMs = int(m_customerProfileRequestTimer.elapsed());
    const QString raw = m_customerProfileAccumulated.trimmed();
    if (raw.isEmpty()) {
        if (m_customerProfileRequestEventId > 0) {
            AiRequestEventDao().appendStage(m_customerProfileRequestEventId, m_currentConvId,
                                             QStringLiteral("failed"),
                                             QStringLiteral("模型未返回客户信息"));
            AiRequestEventDao().failEvent(m_customerProfileRequestEventId,
                                          durationMs,
                                          QStringLiteral("empty_completion"));
            m_customerProfileRequestEventId = 0;
            m_customerProfileFirstTokenMs = 0;
        }
        m_customerProfileAccumulated.clear();
        setCustomerProfileBusy(false);
        refreshRightBarMetrics();
        showStatusMessage(QStringLiteral("客户信息整理失败：模型未返回内容"), 5000);
        return;
    }

    const QJsonObject profile = parseCustomerProfileJson(raw);
    const bool saved = CustomerProfileDao().upsert(m_currentConvId,
                                                   profile,
                                                   m_aggregateAiSessionModelKey,
                                                   m_customerProfileRequestEventId);
    if (m_customerProfileRequestEventId > 0) {
        AiRequestEventDao eventDao;
        if (saved) {
            eventDao.appendStage(m_customerProfileRequestEventId, m_currentConvId,
                                 QStringLiteral("profile_completed"),
                                 QStringLiteral("耗时 %1").arg(aggregateMetricDurationLabel(durationMs)));
            eventDao.appendStage(m_customerProfileRequestEventId, m_currentConvId,
                                 QStringLiteral("profile_saved"));
            eventDao.completeEvent(m_customerProfileRequestEventId,
                                   durationMs,
                                   m_customerProfileFirstTokenMs,
                                   raw.size());
        } else {
            eventDao.appendStage(m_customerProfileRequestEventId, m_currentConvId,
                                 QStringLiteral("failed"),
                                 QStringLiteral("客户信息保存失败"));
            eventDao.failEvent(m_customerProfileRequestEventId,
                               durationMs,
                               QStringLiteral("save_failed"));
        }
        m_customerProfileRequestEventId = 0;
        m_customerProfileFirstTokenMs = 0;
    }

    m_customerProfileAccumulated.clear();
    setCustomerProfileBusy(false);
    refreshRightBarMetrics();
    refreshCustomerProfilePanel();
    showStatusMessage(saved ? QStringLiteral("客户信息已更新")
                            : QStringLiteral("客户信息保存失败"),
                      saved ? 4000 : 6000);
}

void AggregateChatForm::onCustomerProfileFailed(const QString& reason)
{
    if (!m_customerProfileBusy)
        return;
    clearStreamingSession(m_customerProfileSession);
    m_customerProfileAccumulated.clear();
    if (m_customerProfileRequestEventId > 0) {
        AiRequestEventDao().appendStage(m_customerProfileRequestEventId, m_currentConvId,
                                         QStringLiteral("failed"),
                                         reason.left(120));
        AiRequestEventDao().failEvent(m_customerProfileRequestEventId,
                                      int(m_customerProfileRequestTimer.elapsed()),
                                      reason);
        m_customerProfileRequestEventId = 0;
        m_customerProfileFirstTokenMs = 0;
    }
    setCustomerProfileBusy(false);
    refreshRightBarMetrics();
    showStatusMessage(QStringLiteral("客户信息整理失败：%1").arg(reason.left(120)), 6000);
}
