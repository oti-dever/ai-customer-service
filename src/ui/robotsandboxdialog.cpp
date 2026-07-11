#include "robotsandboxdialog.h"

#include "../data/robotsandboxmessagedao.h"
#include "../ipc/ipcservice.h"
#include "../services/ai/aiprovidercatalog.h"
#include "../services/ai/aistreamingsession.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLayoutItem>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <utility>

namespace {

QStringList jsonStringList(const QJsonArray& values)
{
    QStringList out;
    for (const QJsonValue& value : values) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    out.removeDuplicates();
    return out;
}

bool robotEnabled(const QJsonObject& robot)
{
    const QJsonValue value = robot.value(QStringLiteral("enabled"));
    if (value.isBool())
        return value.toBool();
    return value.toInt(1) != 0;
}

bool requestsImage(const QString& query)
{
    const QString text = query.trimmed().toLower();
    const QStringList keywords = {
        QStringLiteral("图片"),
        QStringLiteral("照片"),
        QStringLiteral("实拍"),
        QStringLiteral("商品图"),
        QStringLiteral("外观图"),
        QStringLiteral("细节图"),
        QStringLiteral("配色图"),
        QStringLiteral("发图"),
        QStringLiteral("看图"),
        QStringLiteral("有图"),
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

bool looksLikeImageFilename(const QString& text)
{
    const QString lower = text.trimmed().toLower();
    return lower.endsWith(QLatin1String(".jpg"))
        || lower.endsWith(QLatin1String(".jpeg"))
        || lower.endsWith(QLatin1String(".png"))
        || lower.endsWith(QLatin1String(".webp"))
        || lower.endsWith(QLatin1String(".bmp"))
        || lower.endsWith(QLatin1String(".gif"));
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
        QStringLiteral("发一下"),
        QStringLiteral("发个"),
        QStringLiteral("有"),
        QStringLiteral("没有"),
        QStringLiteral("实拍图"),
        QStringLiteral("实拍"),
        QStringLiteral("外观图"),
        QStringLiteral("外观"),
        QStringLiteral("细节图"),
        QStringLiteral("细节"),
        QStringLiteral("商品图"),
        QStringLiteral("商品"),
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
    query = normalizedImageText(query);
    return query.size() >= 2 ? query : QString();
}

QString firstLinkedImageName(const QList<KnowledgeSnippetContext>& snippets)
{
    static const QRegularExpression imageNameRegex(
        QStringLiteral(R"(图片名\s*=\s*([^\s\r\n；;，,]+))"));
    for (const KnowledgeSnippetContext& snippet : snippets) {
        const QRegularExpressionMatch match = imageNameRegex.match(snippet.snippet);
        if (match.hasMatch())
            return match.captured(1).trimmed();
    }
    return {};
}

bool candidateMatchesImageFocus(const RobotSandboxDialog::ImageCandidate& candidate,
                                const QString& latestQuestion,
                                const QString& linkedImageName)
{
    if (!linkedImageName.trimmed().isEmpty()) {
        const QString wanted = normalizedImageText(QFileInfo(linkedImageName).fileName());
        const QString original = normalizedImageText(candidate.originalFilename);
        const QString title = normalizedImageText(candidate.title);
        const QString pathName = normalizedImageText(QFileInfo(candidate.filePath).fileName());
        return (!wanted.isEmpty()
                && (original.contains(wanted) || title.contains(wanted) || pathName.contains(wanted)));
    }

    const QString focus = imageObjectFocus(latestQuestion);
    if (focus.isEmpty())
        return true;
    const QString haystack = normalizedImageText(
        QStringLiteral("%1\n%2\n%3\n%4").arg(candidate.title,
                                             candidate.originalFilename,
                                             candidate.filePath,
                                             candidate.reason));
    return haystack.contains(focus);
}

RobotSandboxDialog::ImageCandidate sandboxImageCandidateFromReply(const ReplyImageCandidate& candidate)
{
    RobotSandboxDialog::ImageCandidate out;
    out.assetId = candidate.assetId;
    out.title = candidate.sourceTitle;
    out.originalFilename = candidate.originalFilename;
    out.filePath = candidate.filePath;
    out.reason = candidate.recommendationReason;
    out.riskTags = candidate.riskTags;
    out.score = candidate.score;
    out.shouldAttach = candidate.shouldAttach;
    return out;
}

QList<RobotSandboxDialog::ImageCandidate> sandboxImageCandidatesFromReply(const QList<ReplyImageCandidate>& candidates)
{
    QList<RobotSandboxDialog::ImageCandidate> out;
    out.reserve(candidates.size());
    for (const ReplyImageCandidate& candidate : candidates)
        out.append(sandboxImageCandidateFromReply(candidate));
    return out;
}

bool replyDeniesImageAvailability(const QString& text)
{
    const QString normalized = text.simplified();
    const QStringList denyPatterns = {
        QStringLiteral("没有"),
        QStringLiteral("暂无"),
        QStringLiteral("暂时没有"),
        QStringLiteral("没有图片"),
        QStringLiteral("没有实拍"),
        QStringLiteral("无实拍"),
        QStringLiteral("帮您核实"),
        QStringLiteral("进一步核实"),
    };
    const bool mentionsImage = normalized.contains(QStringLiteral("图"))
        || normalized.contains(QStringLiteral("实拍"))
        || normalized.contains(QStringLiteral("照片"));
    if (!mentionsImage)
        return false;
    for (const QString& pattern : denyPatterns) {
        if (normalized.contains(pattern))
            return true;
    }
    return false;
}

bool isShortAcknowledgement(QString text)
{
    text = text.trimmed().toLower();
    text.remove(QRegularExpression(QStringLiteral("[\\s，。！？,.!?~～]+")));
    static const QStringList values = {
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
    return values.contains(text);
}

bool expectsContinuation(const QString& text)
{
    const QStringList keywords = {
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
    for (const QString& keyword : keywords) {
        if (text.contains(keyword))
            return true;
    }
    return false;
}

QString arrayLabel(const QJsonArray& values)
{
    const QStringList list = jsonStringList(values);
    return list.join(QStringLiteral("、"));
}

bool riskTagsEmpty(const QString& value)
{
    const QString text = value.trimmed();
    return text.isEmpty()
        || text == QLatin1String("(none)")
        || text.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("no_risk"), Qt::CaseInsensitive) == 0
        || text == QStringLiteral("无")
        || text == QStringLiteral("无风险");
}

QString sandboxLogField(const QString& key, const QString& value)
{
    return QStringLiteral("%1: %2")
        .arg(key, value.trimmed().isEmpty() ? QStringLiteral("(empty)") : value.trimmed());
}

QString sandboxPartKindLabel(AiMessagePartKind kind)
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

QString formatSandboxAiRequest(const AiRequest& request)
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
            lines << QStringLiteral("[part %1 kind=%2]").arg(j + 1).arg(sandboxPartKindLabel(part.kind));
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
    lines << QStringLiteral("extra_root_fields:\n%1")
                 .arg(QString::fromUtf8(QJsonDocument(request.extraRootFields)
                                             .toJson(QJsonDocument::Indented))
                          .trimmed());
    lines << QStringLiteral("stream: %1").arg(request.stream ? QStringLiteral("true") : QStringLiteral("false"));
    return lines.join(QLatin1Char('\n'));
}

QString formatSandboxKnowledgeSnippets(const QList<KnowledgeSnippetContext>& snippets)
{
    QStringList lines;
    lines << QStringLiteral("results: %1").arg(snippets.size());
    for (int i = 0; i < snippets.size(); ++i) {
        const KnowledgeSnippetContext& item = snippets.at(i);
        lines << QStringLiteral("--- result %1 ---").arg(i + 1);
        lines << sandboxLogField(QStringLiteral("chunk_id"), item.chunkId);
        lines << sandboxLogField(QStringLiteral("source_title"), item.sourceTitle);
        lines << sandboxLogField(QStringLiteral("title_path"), item.titlePath);
        lines << QStringLiteral("score: %1").arg(item.score, 0, 'f', 4);
        lines << sandboxLogField(QStringLiteral("match_type"), item.matchType);
        lines << QStringLiteral("snippet:\n%1").arg(item.snippet.trimmed());
    }
    return lines.join(QLatin1Char('\n'));
}

QString robotSandboxTraceLogPath()
{
    const QString logDir = QDir(QStringLiteral(PROJECT_ROOT_DIR))
                               .filePath(QStringLiteral("python/rpa/logs/robot_sandbox"));
    QDir().mkpath(logDir);
    return QDir(logDir).filePath(
        QStringLiteral("robot_sandbox_trace_%1.log")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"))));
}

void appendRobotSandboxTraceBlock(const QStringList& lines)
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    QFile file(robotSandboxTraceLogPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[RobotSandboxTrace] failed to open log" << file.fileName() << file.errorString();
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "\n==================== Robot Sandbox AI Call ====================\n";
    stream << sandboxLogField(QStringLiteral("time"),
                              QDateTime::currentDateTime().toString(Qt::ISODateWithMs)) << "\n";
    for (const QString& line : lines)
        stream << line << "\n";
}

QString normalizedMessage(QString text)
{
    text = text.toLower().trimmed();
    QString normalized;
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber())
            normalized.append(ch);
    }
    return normalized;
}

bool messagesSimilar(const QString& left, const QString& right)
{
    const QString a = normalizedMessage(left);
    const QString b = normalizedMessage(right);
    if (a.isEmpty() || b.isEmpty())
        return false;
    if (a == b)
        return true;
    if (qMin(a.size(), b.size()) >= 12 && (a.contains(b) || b.contains(a)))
        return true;

    QSet<QString> gramsA;
    QSet<QString> gramsB;
    for (int i = 0; i + 1 < a.size(); ++i)
        gramsA.insert(a.mid(i, 2));
    for (int i = 0; i + 1 < b.size(); ++i)
        gramsB.insert(b.mid(i, 2));
    if (gramsA.isEmpty() || gramsB.isEmpty())
        return false;
    int intersection = 0;
    for (const QString& gram : std::as_const(gramsA)) {
        if (gramsB.contains(gram))
            ++intersection;
    }
    return (2.0 * intersection) / double(gramsA.size() + gramsB.size()) >= 0.72;
}

QString buildRobotInfo(const QJsonObject& robot)
{
    const QString modelKey = robot.value(QStringLiteral("model_config_id")).toString().trimmed();
    const QString modelLabel = aiPresetLabel(modelKey).trimmed();
    QStringList knowledgeNames =
        jsonStringList(robot.value(QStringLiteral("knowledge_base_names")).toArray());
    if (knowledgeNames.isEmpty())
        knowledgeNames = jsonStringList(robot.value(QStringLiteral("knowledge_base_ids")).toArray());
    const QString tone = robot.value(QStringLiteral("reply_tone"))
                             .toString(QStringLiteral("亲切温和、不失热情"));
    const QString addresses = robot.value(QStringLiteral("common_address_terms"))
                                  .toString(QStringLiteral("亲、宝子"));
    return QStringLiteral("模型：%1    知识库：%2\n语气：%3    常用称呼：%4    AI 发图：%5")
        .arg(modelLabel.isEmpty() ? modelKey : modelLabel,
             knowledgeNames.isEmpty() ? QStringLiteral("未绑定") : knowledgeNames.join(QStringLiteral("、")),
             tone,
             addresses,
             robot.value(QStringLiteral("allow_auto_send_images")).toBool(false)
                 ? QStringLiteral("允许")
                 : QStringLiteral("不允许"));
}

} // namespace

RobotSandboxDialog::RobotSandboxDialog(const QJsonObject& robot, QWidget* parent)
    : QDialog(parent)
    , m_robot(robot)
    , m_robotId(robot.value(QStringLiteral("robot_id")).toString().trimmed())
    , m_robotName(robot.value(QStringLiteral("robot_name")).toString(QStringLiteral("未命名机器人")))
    , m_aiService(new AiChatAppService(this))
{
    buildUi();
    loadHistory();
}

RobotSandboxDialog::~RobotSandboxDialog()
{
    if (m_session)
        m_session->abort();
}

void RobotSandboxDialog::buildUi()
{
    setWindowTitle(QStringLiteral("机器人沙盒 - %1").arg(m_robotName));
    setMinimumSize(780, 620);
    resize(920, 760);
    setModal(true);
    setStyleSheet(QStringLiteral(R"QSS(
QDialog { background: #f4f6f8; }
QLabel#sandboxTitle { color: #0f172a; font-size: 20px; font-weight: 700; }
QLabel#sandboxInfo, QLabel#sandboxStatus { color: #64748b; font-size: 13px; }
QFrame#sandboxCandidateCard { background: #ffffff; border: 1px solid #d9e2ef; border-radius: 10px; }
QLabel#sandboxCandidateText { color: #475569; font-size: 12px; line-height: 1.4; }
QFrame#sandboxChatCard { background: #f8fafc; border: 1px solid #d9e2ef; border-radius: 12px; }
QScrollArea#sandboxChatScroll { background: transparent; border: none; }
QScrollArea#sandboxChatScroll > QWidget > QWidget { background: #f8fafc; }
QWidget#sandboxChatBody { background: #f8fafc; }
QFrame#sandboxUserBubble { background: #2563eb; border-radius: 12px; }
QFrame#sandboxAssistantBubble { background: #ffffff; border: 1px solid #d9e2ef; border-radius: 12px; }
QLabel#sandboxUserText { color: #ffffff; font-size: 14px; line-height: 1.45; }
QLabel#sandboxAssistantText { color: #0f172a; font-size: 14px; line-height: 1.45; }
QFrame#sandboxInputCard {
  background: #ffffff; border: 1px solid #cbd5e1; border-radius: 12px;
}
QFrame#sandboxInputCard:focus-within { border-color: #2563eb; }
QPlainTextEdit#sandboxInput {
  background: transparent; color: #0f172a; border: none;
  padding: 10px 12px 2px 12px; font-size: 14px;
}
QPushButton#sandboxSecondary {
  background: #ffffff; color: #334155; border: 1px solid #cbd5e1;
  border-radius: 9px; padding: 8px 14px; font-weight: 600;
}
QPushButton#sandboxSecondary:hover { background: #f8fafc; border-color: #94a3b8; }
QPushButton#sandboxPrimary {
  background: #2563eb; color: #ffffff; border: none;
  border-radius: 9px; padding: 8px 18px; font-weight: 600;
}
QPushButton#sandboxPrimary:hover { background: #1d4ed8; }
QPushButton#sandboxPrimary:disabled { background: #94a3b8; }
)QSS"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 18, 20, 18);
    outer->setSpacing(12);

    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("机器人沙盒 - %1").arg(m_robotName), this);
    title->setObjectName(QStringLiteral("sandboxTitle"));
    titleRow->addWidget(title, 1);
    m_clearButton = new QPushButton(QStringLiteral("清空历史"), this);
    m_clearButton->setObjectName(QStringLiteral("sandboxSecondary"));
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(m_clearButton, 0, Qt::AlignTop);
    outer->addLayout(titleRow);

    auto* info = new QLabel(buildRobotInfo(m_robot), this);
    info->setObjectName(QStringLiteral("sandboxInfo"));
    info->setWordWrap(true);
    outer->addWidget(info);

    if (!robotEnabled(m_robot)) {
        auto* warning = new QLabel(
            QStringLiteral("当前机器人处于停用状态，沙盒仍可测试，但真实自动回复不会使用该机器人。"),
            this);
        warning->setObjectName(QStringLiteral("sandboxStatus"));
        warning->setStyleSheet(QStringLiteral("color:#b45309;"));
        outer->addWidget(warning);
    }

    auto* chatCard = new QFrame(this);
    chatCard->setObjectName(QStringLiteral("sandboxChatCard"));
    auto* chatCardLayout = new QVBoxLayout(chatCard);
    chatCardLayout->setContentsMargins(0, 0, 0, 0);
    m_chatScroll = new QScrollArea(chatCard);
    m_chatScroll->setObjectName(QStringLiteral("sandboxChatScroll"));
    m_chatScroll->setWidgetResizable(true);
    m_chatScroll->setFrameShape(QFrame::NoFrame);
    m_chatScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chatScroll->viewport()->setAutoFillBackground(false);
    m_chatBody = new QWidget(m_chatScroll);
    m_chatBody->setObjectName(QStringLiteral("sandboxChatBody"));
    m_messageLayout = new QVBoxLayout(m_chatBody);
    m_messageLayout->setContentsMargins(20, 20, 20, 20);
    m_messageLayout->setSpacing(12);
    m_messageLayout->addStretch(1);
    m_chatScroll->setWidget(m_chatBody);
    chatCardLayout->addWidget(m_chatScroll);
    outer->addWidget(chatCard, 1);

    m_statusLabel = new QLabel(QStringLiteral("输入一个客户问题，测试当前机器人的回复效果。"), this);
    m_statusLabel->setObjectName(QStringLiteral("sandboxStatus"));
    m_statusLabel->setWordWrap(true);
    outer->addWidget(m_statusLabel);

    auto* candidateCard = new QFrame(this);
    candidateCard->setObjectName(QStringLiteral("sandboxCandidateCard"));
    candidateCard->setAttribute(Qt::WA_StyledBackground, true);
    auto* candidateLayout = new QVBoxLayout(candidateCard);
    candidateLayout->setContentsMargins(12, 9, 12, 9);
    m_candidateLabel = new QLabel(candidateCard);
    m_candidateLabel->setObjectName(QStringLiteral("sandboxCandidateText"));
    m_candidateLabel->setWordWrap(true);
    m_candidateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    candidateLayout->addWidget(m_candidateLabel);
    candidateCard->setVisible(false);
    outer->addWidget(candidateCard);

    auto* inputCard = new QFrame(this);
    inputCard->setObjectName(QStringLiteral("sandboxInputCard"));
    inputCard->setAttribute(Qt::WA_StyledBackground, true);
    auto* inputCardLayout = new QVBoxLayout(inputCard);
    inputCardLayout->setContentsMargins(0, 0, 10, 10);
    inputCardLayout->setSpacing(4);
    m_inputEdit = new QPlainTextEdit(this);
    m_inputEdit->setObjectName(QStringLiteral("sandboxInput"));
    m_inputEdit->setPlaceholderText(QStringLiteral("输入测试客户问题，例如：可以看下 K87 的实拍图吗？"));
    m_inputEdit->setFixedHeight(74);
    inputCardLayout->addWidget(m_inputEdit, 1);
    auto* sendRow = new QHBoxLayout;
    sendRow->setContentsMargins(12, 0, 0, 0);
    sendRow->addStretch(1);
    m_sendButton = new QPushButton(QStringLiteral("发送"), inputCard);
    m_sendButton->setObjectName(QStringLiteral("sandboxPrimary"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setFocusPolicy(Qt::NoFocus);
    m_sendButton->setMinimumWidth(84);
    sendRow->addWidget(m_sendButton, 0, Qt::AlignRight | Qt::AlignBottom);
    inputCardLayout->addLayout(sendRow);
    outer->addWidget(inputCard, 0);

    connect(m_clearButton, &QPushButton::clicked, this, &RobotSandboxDialog::clearHistory);
    connect(m_sendButton, &QPushButton::clicked, this, &RobotSandboxDialog::sendCurrentQuestion);
    auto* sendShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(sendShortcut, &QShortcut::activated, this, &RobotSandboxDialog::sendCurrentQuestion);
}

void RobotSandboxDialog::loadHistory()
{
    m_imageCandidates.clear();
    while (m_messageLayout->count() > 1) {
        QLayoutItem* item = m_messageLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const QVector<RobotSandboxMessageRecord> messages =
        RobotSandboxMessageDao().listForRobot(m_robotId, 300);
    for (const RobotSandboxMessageRecord& message : messages) {
        if (message.contentType == QLatin1String("image"))
            addImageBubble(message.imagePath, message.metadata);
        else
            addTextBubble(message.role, message.content);
    }
    m_statusLabel->setText(messages.isEmpty()
                               ? QStringLiteral("暂无测试消息，输入一个客户问题开始测试。")
                               : QStringLiteral("已加载 %1 条测试消息。").arg(messages.size()));
    updateImageCandidateSummary(QString());
    scrollToBottom();
}

void RobotSandboxDialog::addTextBubble(const QString& role, const QString& text)
{
    const bool user = role == QLatin1String("user");
    auto* row = new QWidget(m_chatBody);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    if (user)
        rowLayout->addStretch(1);

    auto* bubble = new QFrame(row);
    bubble->setObjectName(user ? QStringLiteral("sandboxUserBubble")
                               : QStringLiteral("sandboxAssistantBubble"));
    bubble->setMaximumWidth(620);
    auto* bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 9, 12, 9);
    auto* label = new QLabel(text, bubble);
    label->setObjectName(user ? QStringLiteral("sandboxUserText")
                              : QStringLiteral("sandboxAssistantText"));
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubbleLayout->addWidget(label);
    rowLayout->addWidget(bubble, 0, Qt::AlignTop);
    if (!user)
        rowLayout->addStretch(1);
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, row);
}

void RobotSandboxDialog::addImageBubble(const QString& path, const QJsonObject& metadata)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return;

    auto* row = new QWidget(m_chatBody);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    auto* bubble = new QFrame(row);
    bubble->setObjectName(QStringLiteral("sandboxAssistantBubble"));
    auto* bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(8, 8, 8, 8);

    const QPixmap original(path);
    auto* imageButton = new QPushButton(bubble);
    imageButton->setFlat(true);
    imageButton->setCursor(Qt::PointingHandCursor);
    imageButton->setIcon(QIcon(original));
    imageButton->setIconSize(original.size().scaled(QSize(320, 220), Qt::KeepAspectRatio));
    imageButton->setFixedSize(imageButton->iconSize() + QSize(12, 12));
    imageButton->setToolTip(
        QStringLiteral("%1\n匹配理由：%2\n分数：%3")
            .arg(metadata.value(QStringLiteral("title")).toString(info.fileName()),
                 metadata.value(QStringLiteral("recommendation_reason")).toString(QStringLiteral("图片素材匹配")),
                 QString::number(metadata.value(QStringLiteral("score")).toDouble(), 'f', 3)));
    connect(imageButton, &QPushButton::clicked, this, [this, path]() { previewImage(path); });
    bubbleLayout->addWidget(imageButton);

    auto* title = new QLabel(metadata.value(QStringLiteral("title")).toString(info.fileName()), bubble);
    title->setObjectName(QStringLiteral("sandboxAssistantText"));
    title->setWordWrap(true);
    bubbleLayout->addWidget(title);
    rowLayout->addWidget(bubble, 0, Qt::AlignTop);
    rowLayout->addStretch(1);
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, row);
}

void RobotSandboxDialog::addPendingBubble()
{
    removePendingBubble();
    m_pendingRow = new QWidget(m_chatBody);
    auto* rowLayout = new QHBoxLayout(m_pendingRow);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    auto* bubble = new QFrame(m_pendingRow);
    bubble->setObjectName(QStringLiteral("sandboxAssistantBubble"));
    auto* bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 9, 12, 9);
    m_pendingLabel = new QLabel(QStringLiteral("AI 正在回复..."), bubble);
    m_pendingLabel->setObjectName(QStringLiteral("sandboxAssistantText"));
    m_pendingLabel->setWordWrap(true);
    bubbleLayout->addWidget(m_pendingLabel);
    rowLayout->addWidget(bubble);
    rowLayout->addStretch(1);
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, m_pendingRow);
    scrollToBottom();
}

void RobotSandboxDialog::removePendingBubble()
{
    if (!m_pendingRow)
        return;
    m_messageLayout->removeWidget(m_pendingRow);
    m_pendingRow->deleteLater();
    m_pendingRow = nullptr;
    m_pendingLabel = nullptr;
}

void RobotSandboxDialog::scrollToBottom()
{
    QTimer::singleShot(0, this, [this]() {
        if (m_chatScroll && m_chatScroll->verticalScrollBar())
            m_chatScroll->verticalScrollBar()->setValue(m_chatScroll->verticalScrollBar()->maximum());
    });
}

void RobotSandboxDialog::updateImageCandidateSummary(const QString& status)
{
    if (!m_candidateLabel)
        return;

    QStringList lines;
    if (!status.trimmed().isEmpty())
        lines << QStringLiteral("图片素材：%1").arg(status.trimmed());

    const int count = qMin(3, m_imageCandidates.size());
    for (int i = 0; i < count; ++i) {
        const ImageCandidate& item = m_imageCandidates.at(i);
        const QFileInfo info(item.filePath);
        const QString title = item.title.trimmed().isEmpty() ? info.fileName() : item.title.trimmed();
        lines << QStringLiteral("%1. %2｜分数 %3｜%4｜风险：%5")
                     .arg(i + 1)
                     .arg(title)
                     .arg(item.score, 0, 'f', 3)
                     .arg(item.shouldAttach ? QStringLiteral("建议附图") : QStringLiteral("不建议自动附图"))
                     .arg(riskTagsEmpty(item.riskTags) ? QStringLiteral("无") : item.riskTags);
        if (!item.reason.trimmed().isEmpty())
            lines << QStringLiteral("   理由：%1").arg(item.reason.trimmed());
    }
    if (m_imageCandidates.size() > count)
        lines << QStringLiteral("还有 %1 个候选未展示。").arg(m_imageCandidates.size() - count);

    const bool visible = !lines.isEmpty();
    m_candidateLabel->setText(lines.join(QLatin1Char('\n')));
    if (QWidget* card = m_candidateLabel->parentWidget())
        card->setVisible(visible);
}

AggregateReplyStrategy RobotSandboxDialog::replyStrategy() const
{
    AggregateReplyStrategy strategy;
    strategy.replyTone = m_robot.value(QStringLiteral("reply_tone"))
                             .toString(QStringLiteral("亲切温和、不失热情"));
    strategy.commonAddressTerms = m_robot.value(QStringLiteral("common_address_terms"))
                                      .toString(QStringLiteral("亲、宝子"));
    strategy.allowAutoSendImages =
        m_robot.value(QStringLiteral("allow_auto_send_images")).toBool(false);
    strategy.allowAutoSendMultiMessages =
        m_robot.value(QStringLiteral("allow_auto_send_multi_messages")).toBool(false);
    strategy.maxAutoSendMessages = strategy.allowAutoSendMultiMessages
        ? qBound(1, m_robot.value(QStringLiteral("max_auto_send_messages")).toInt(2), 3)
        : 1;
    return strategy;
}

QList<AiConversationTurn> RobotSandboxDialog::recentTurnsExcludingLatest() const
{
    const QVector<RobotSandboxMessageRecord> messages =
        RobotSandboxMessageDao().listRecent(m_robotId, 11);
    QList<AiConversationTurn> turns;
    const int end = qMax(0, messages.size() - 1);
    const int start = qMax(0, end - 10);
    for (int i = start; i < end; ++i) {
        const RobotSandboxMessageRecord& message = messages.at(i);
        if (message.contentType != QLatin1String("text") || message.content.trimmed().isEmpty())
            continue;
        turns.append(makeAiTextTurn(message.role == QLatin1String("assistant")
                                        ? QStringLiteral("assistant")
                                        : QStringLiteral("user"),
                                    message.content));
    }
    return turns;
}

QString RobotSandboxDialog::knowledgeQueryForLatest(const QString& latest) const
{
    if (!isShortAcknowledgement(latest))
        return latest.left(800);

    const QVector<RobotSandboxMessageRecord> messages =
        RobotSandboxMessageDao().listRecent(m_robotId, 10);
    for (int i = messages.size() - 2; i >= 0; --i) {
        const RobotSandboxMessageRecord& message = messages.at(i);
        if (message.role != QLatin1String("assistant")
            || message.contentType != QLatin1String("text")) {
            continue;
        }
        if (expectsContinuation(message.content)) {
            return QStringLiteral("客户已确认继续上一项服务。\n上一条客服消息：%1")
                .arg(message.content.left(500));
        }
        return {};
    }
    return {};
}

QList<KnowledgeSnippetContext> RobotSandboxDialog::retrieveKnowledge(const QString& query,
                                                                     QString* statusOut)
{
    if (statusOut)
        statusOut->clear();
    const QStringList baseIds =
        jsonStringList(m_robot.value(QStringLiteral("knowledge_base_ids")).toArray());
    if (query.trimmed().isEmpty()) {
        if (statusOut)
            *statusOut = QStringLiteral("短确认场景未检索知识库，将结合沙盒上下文生成。");
        return {};
    }
    if (baseIds.isEmpty()) {
        if (statusOut)
            *statusOut = QStringLiteral("当前机器人未绑定知识库，将只按聊天上下文生成。");
        return {};
    }
    QString error;
    if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
        if (statusOut)
            *statusOut = QStringLiteral("Python 服务不可用，将跳过知识库检索：%1").arg(error.left(100));
        return {};
    }

    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    const QJsonObject response = Ipc::IpcService::instance().searchKnowledge(
        query, QString(), QString(), QStringLiteral("reply_draft"), 3, 3500, &status, &error, baseIds);
    if (status != Ipc::ResponseStatus::Success
        || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
        if (statusOut)
            *statusOut = QStringLiteral("知识库检索失败，将只按上下文生成：%1").arg(error.left(100));
        return {};
    }

    QList<KnowledgeSnippetContext> snippets;
    for (const QJsonValue& value : response.value(QStringLiteral("results")).toArray()) {
        const QJsonObject item = value.toObject();
        KnowledgeSnippetContext snippet;
        snippet.chunkId = item.value(QStringLiteral("chunk_id")).toString();
        snippet.sourceTitle = item.value(QStringLiteral("source_title")).toString();
        snippet.titlePath = item.value(QStringLiteral("title_path")).toString();
        snippet.snippet = item.value(QStringLiteral("snippet")).toString();
        snippet.matchType = item.value(QStringLiteral("match_type")).toString();
        snippet.score = item.value(QStringLiteral("score")).toDouble();
        if (!snippet.snippet.trimmed().isEmpty())
            snippets.append(snippet);
    }
    if (statusOut)
        *statusOut = snippets.isEmpty()
            ? QStringLiteral("知识库未命中，将按沙盒上下文生成。")
            : QStringLiteral("已命中 %1 条知识库片段。").arg(snippets.size());
    return snippets;
}

QList<RobotSandboxDialog::ImageCandidate> RobotSandboxDialog::retrieveImages(const QString& query,
                                                                             QString* statusOut)
{
    if (statusOut)
        statusOut->clear();
    const bool imageQuery = requestsImage(query) || looksLikeImageFilename(query);
    if (!replyStrategy().allowAutoSendImages) {
        if (imageQuery && statusOut)
            *statusOut = QStringLiteral("机器人未开启允许自动发图，未检索图片素材。");
        return {};
    }
    if (!imageQuery)
        return {};
    const QStringList baseIds =
        jsonStringList(m_robot.value(QStringLiteral("knowledge_base_ids")).toArray());
    if (baseIds.isEmpty()) {
        if (statusOut)
            *statusOut = QStringLiteral("当前机器人未绑定知识库，无法检索图片素材。");
        return {};
    }
    QString error;
    if (!Ipc::IpcService::instance().ensureServiceAvailable(&error)) {
        if (statusOut)
            *statusOut = QStringLiteral("Python 服务不可用，无法检索图片素材：%1").arg(error.left(100));
        return {};
    }

    Ipc::ResponseStatus status = Ipc::ResponseStatus::Error;
    const QJsonObject response = Ipc::IpcService::instance().searchKnowledgeImages(
        query, QString(), QString(), QStringLiteral("reply_draft"), 3, 15000, &status, &error, baseIds);
    if (status != Ipc::ResponseStatus::Success
        || response.value(QStringLiteral("status")).toString(QStringLiteral("success")) == QLatin1String("error")) {
        if (statusOut)
            *statusOut = QStringLiteral("图片素材检索失败：%1").arg(error.left(100));
        return {};
    }

    QList<ImageCandidate> candidates;
    for (const QJsonValue& value : response.value(QStringLiteral("results")).toArray()) {
        const QJsonObject item = value.toObject();
        ImageCandidate candidate;
        candidate.assetId = item.value(QStringLiteral("asset_id")).toString(
            item.value(QStringLiteral("source_id")).toString());
        candidate.title = item.value(QStringLiteral("source_title")).toString();
        candidate.originalFilename = item.value(QStringLiteral("original_filename")).toString();
        candidate.filePath = item.value(QStringLiteral("file_path")).toString();
        candidate.riskTags = arrayLabel(item.value(QStringLiteral("risk_tags")).toArray());
        candidate.score = item.value(QStringLiteral("score")).toDouble();
        const QJsonObject recommendation = item.value(QStringLiteral("recommendation")).toObject();
        candidate.shouldAttach = recommendation.value(QStringLiteral("should_attach")).toBool(false);
        candidate.reason = recommendation.value(QStringLiteral("reason")).toString();
        if (!candidate.filePath.trimmed().isEmpty())
            candidates.append(candidate);
    }
    if (statusOut)
        *statusOut = candidates.isEmpty()
            ? QStringLiteral("图片素材未命中。")
            : QStringLiteral("已检索到 %1 个图片候选。").arg(candidates.size());
    return candidates;
}

QStringList RobotSandboxDialog::splitReplyMessages(const QString& text)
{
    const AggregateReplyStrategy strategy = replyStrategy();
    static const QRegularExpression separator(
        QStringLiteral(R"((?m)^\s*-{3,}\s*MSG\s*-{3,}\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList raw = strategy.allowAutoSendMultiMessages
        ? text.split(separator, Qt::SkipEmptyParts)
        : QStringList{text};
    QStringList messages;
    const int limit = strategy.allowAutoSendMultiMessages ? strategy.maxAutoSendMessages : 1;
    int duplicateCount = 0;
    int truncatedCount = 0;
    for (int rawIndex = 0; rawIndex < raw.size(); ++rawIndex) {
        QString part = raw.at(rawIndex).trimmed();
        if (part.isEmpty())
            continue;
        bool duplicate = false;
        for (const QString& existing : std::as_const(messages)) {
            if (messagesSimilar(existing, part)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            ++duplicateCount;
        } else {
            messages.append(part);
        }
        if (messages.size() >= limit) {
            for (int j = rawIndex + 1; j < raw.size(); ++j) {
                if (!raw.at(j).trimmed().isEmpty())
                    ++truncatedCount;
            }
            break;
        }
    }
    if (messages.isEmpty() && !text.trimmed().isEmpty())
        messages.append(text.trimmed());
    m_lastSplitStatus = QStringLiteral("raw_parts=%1, final_messages=%2, max_messages=%3, duplicates_removed=%4, truncated=%5")
                            .arg(raw.size())
                            .arg(messages.size())
                            .arg(limit)
                            .arg(duplicateCount)
                            .arg(truncatedCount);
    return messages;
}

void RobotSandboxDialog::appendTraceStart(const AiProviderConfig& config, const AiRequest& request)
{
    QStringList imageLines;
    imageLines << QStringLiteral("candidates: %1").arg(m_imageCandidates.size());
    for (int i = 0; i < m_imageCandidates.size(); ++i) {
        const ImageCandidate& item = m_imageCandidates.at(i);
        imageLines << QStringLiteral("--- candidate %1 ---").arg(i + 1);
        imageLines << sandboxLogField(QStringLiteral("asset_id"), item.assetId);
        imageLines << sandboxLogField(QStringLiteral("title"), item.title);
        imageLines << sandboxLogField(QStringLiteral("original_filename"), item.originalFilename);
        imageLines << sandboxLogField(QStringLiteral("file_path"), item.filePath);
        imageLines << QStringLiteral("score: %1").arg(item.score, 0, 'f', 4);
        imageLines << QStringLiteral("should_attach: %1").arg(item.shouldAttach ? QStringLiteral("yes") : QStringLiteral("no"));
        imageLines << sandboxLogField(QStringLiteral("risk_tags"), item.riskTags);
        imageLines << sandboxLogField(QStringLiteral("reason"), item.reason);
    }

    appendRobotSandboxTraceBlock({
        sandboxLogField(QStringLiteral("event"), QStringLiteral("start")),
        sandboxLogField(QStringLiteral("trace_id"), m_traceId),
        sandboxLogField(QStringLiteral("robot_id"), m_robotId),
        sandboxLogField(QStringLiteral("robot_name"), m_robotName),
        sandboxLogField(QStringLiteral("model_key"), config.sessionModelKey),
        sandboxLogField(QStringLiteral("model"), config.model),
        sandboxLogField(QStringLiteral("knowledge_base_ids"),
                        jsonStringList(m_robot.value(QStringLiteral("knowledge_base_ids")).toArray()).join(QStringLiteral(","))),
        sandboxLogField(QStringLiteral("latest_user_text"), m_lastQuestion),
        sandboxLogField(QStringLiteral("knowledge_query"), m_lastKnowledgeQuery),
        sandboxLogField(QStringLiteral("knowledge_status"), m_lastKnowledgeStatus),
        sandboxLogField(QStringLiteral("linked_image_name"), m_lastLinkedImageName),
        QStringLiteral("\n[knowledge_results]\n%1").arg(formatSandboxKnowledgeSnippets(m_lastKnowledgeSnippets)),
        sandboxLogField(QStringLiteral("image_status"), m_lastImageStatus),
        QStringLiteral("\n[image_candidates]\n%1").arg(imageLines.join(QLatin1Char('\n'))),
        QStringLiteral("\n[full_ai_request]\n%1").arg(formatSandboxAiRequest(request)),
    });
}

void RobotSandboxDialog::appendTraceFinish(const QString& status,
                                           const QStringList& finalMessages,
                                           const QString& detail)
{
    if (m_traceFinished || m_traceId.trimmed().isEmpty())
        return;
    m_traceFinished = true;

    QStringList lines;
    lines << sandboxLogField(QStringLiteral("event"), QStringLiteral("finish"));
    lines << sandboxLogField(QStringLiteral("trace_id"), m_traceId);
    lines << sandboxLogField(QStringLiteral("status"), status);
    if (!detail.trimmed().isEmpty())
        lines << sandboxLogField(QStringLiteral("detail"), detail);
    lines << sandboxLogField(QStringLiteral("latest_user_text"), m_lastQuestion);
    lines << sandboxLogField(QStringLiteral("knowledge_query"), m_lastKnowledgeQuery);
    lines << sandboxLogField(QStringLiteral("knowledge_status"), m_lastKnowledgeStatus);
    lines << sandboxLogField(QStringLiteral("linked_image_name"), m_lastLinkedImageName);
    lines << sandboxLogField(QStringLiteral("image_status"), m_lastImageStatus);
    lines << sandboxLogField(QStringLiteral("split_status"), m_lastSplitStatus);
    lines << QStringLiteral("\n[raw_model_output]\n%1")
                 .arg(m_accumulated.trimmed().isEmpty() ? QStringLiteral("(empty)") : m_accumulated.trimmed());
    lines << QStringLiteral("\n[final_messages] count=%1").arg(finalMessages.size());
    for (int i = 0; i < finalMessages.size(); ++i)
        lines << QStringLiteral("--- message %1 ---\n%2").arg(i + 1).arg(finalMessages.at(i).trimmed());

    ImageCandidate selectedImage;
    if (!replyDeniesImageAvailability(finalMessages.join(QStringLiteral("\n")))) {
        for (const ImageCandidate& item : std::as_const(m_imageCandidates)) {
            if (item.shouldAttach && riskTagsEmpty(item.riskTags) && QFileInfo(item.filePath).isFile()) {
                selectedImage = item;
                break;
            }
        }
    }
    if (!selectedImage.filePath.trimmed().isEmpty()) {
        lines << QStringLiteral("\n[selected_image]");
        lines << sandboxLogField(QStringLiteral("asset_id"), selectedImage.assetId);
        lines << sandboxLogField(QStringLiteral("title"), selectedImage.title);
        lines << sandboxLogField(QStringLiteral("original_filename"), selectedImage.originalFilename);
        lines << sandboxLogField(QStringLiteral("file_path"), selectedImage.filePath);
        lines << sandboxLogField(QStringLiteral("reason"), selectedImage.reason);
        lines << sandboxLogField(QStringLiteral("risk_tags"), selectedImage.riskTags);
        lines << QStringLiteral("score: %1").arg(selectedImage.score, 0, 'f', 4);
    }
    appendRobotSandboxTraceBlock(lines);
}

void RobotSandboxDialog::sendCurrentQuestion()
{
    if (m_busy)
        return;
    const QString question = m_inputEdit->toPlainText().trimmed();
    if (question.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请输入测试问题。"));
        return;
    }
    const QString modelKey = m_robot.value(QStringLiteral("model_config_id")).toString().trimmed();
    if (modelKey.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("当前机器人未配置模型，无法测试。"));
        return;
    }
    m_traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_lastQuestion = question;
    m_lastKnowledgeQuery.clear();
    m_lastKnowledgeStatus.clear();
    m_lastImageStatus.clear();
    m_lastSplitStatus.clear();
    m_lastLinkedImageName.clear();
    m_lastKnowledgeSnippets.clear();
    m_imageCandidates.clear();
    m_accumulated.clear();
    m_traceFinished = false;
    updateImageCandidateSummary(QString());

    RobotSandboxMessageRecord userMessage;
    userMessage.robotId = m_robotId;
    userMessage.role = QStringLiteral("user");
    userMessage.content = question;
    userMessage.contentType = QStringLiteral("text");
    if (!RobotSandboxMessageDao().append(userMessage)) {
        m_statusLabel->setText(QStringLiteral("保存测试消息失败。"));
        return;
    }
    addTextBubble(QStringLiteral("user"), question);
    m_inputEdit->clear();
    scrollToBottom();
    setBusy(true, QStringLiteral("正在检索知识库并生成回复..."));

    ReplyRuntimeConfig runtime;
    runtime.source = QStringLiteral("robot_sandbox");
    runtime.statusText = QStringLiteral("机器人沙盒");
    runtime.sessionModelKey = modelKey;
    runtime.boundRobotId = m_robotId;
    runtime.robotName = m_robotName;
    runtime.knowledgeBaseIds = jsonStringList(m_robot.value(QStringLiteral("knowledge_base_ids")).toArray());
    runtime.knowledgeBaseNames = jsonStringList(m_robot.value(QStringLiteral("knowledge_base_names")).toArray());
    runtime.strategy = replyStrategy();
    runtime.usingRobot = true;
    runtime.robotFound = true;
    runtime.robotEnabled = robotEnabled(m_robot);

    ReplyContextInput input;
    input.source = ReplyContextInput::Source::RobotSandbox;
    input.latestUserText = question;
    input.recentTurns = recentTurnsExcludingLatest();
    input.runtimeConfig = runtime;
    const ReplyContextResult replyContext = m_aiService->buildReplyContext(input);

    m_lastKnowledgeQuery = replyContext.knowledgeTrace.searchQuery;
    m_lastKnowledgeStatus = replyContext.knowledgeStatus;
    m_lastKnowledgeSnippets = replyContext.knowledgeSnippets;
    m_lastLinkedImageName = replyContext.linkedImageName;
    m_lastImageStatus = replyContext.imageStatus;
    m_imageCandidates = sandboxImageCandidatesFromReply(replyContext.imageCandidates);
    updateImageCandidateSummary(m_lastImageStatus);

    const AggregateAiBuiltRequest built = replyContext.built;
    if (!built.ok()) {
        failReply(built.failureDetail);
        return;
    }

    m_accumulated.clear();
    appendTraceStart(built.config, built.request);
    addPendingBubble();
    m_statusLabel->setText(
        QStringLiteral("%1%2")
            .arg(m_lastKnowledgeStatus,
                 m_lastImageStatus.isEmpty() ? QString() : QStringLiteral(" %1").arg(m_lastImageStatus)));

    m_session = m_aiService->createSession(built.config, built.request, this);
    connect(m_session, &IAiStreamingSession::delta, this, [this](const QString& delta) {
        m_accumulated += delta;
        if (m_pendingLabel)
            m_pendingLabel->setText(m_accumulated.isEmpty() ? QStringLiteral("AI 正在回复...")
                                                            : m_accumulated);
        scrollToBottom();
    });
    connect(m_session, &IAiStreamingSession::completed, this, &RobotSandboxDialog::finishReply);
    connect(m_session, &IAiStreamingSession::failed, this, &RobotSandboxDialog::failReply);
    m_session->start();
}

void RobotSandboxDialog::finishReply()
{
    if (!m_busy)
        return;
    removePendingBubble();
    if (m_session)
        m_session->deleteLater();
    m_session = nullptr;

    const QStringList replies = splitReplyMessages(m_accumulated);
    QVector<RobotSandboxMessageRecord> records;
    for (const QString& reply : replies) {
        RobotSandboxMessageRecord record;
        record.robotId = m_robotId;
        record.role = QStringLiteral("assistant");
        record.content = reply;
        record.contentType = QStringLiteral("text");
        record.metadata.insert(QStringLiteral("trace_id"), m_traceId);
        records.append(record);
    }

    ImageCandidate selectedImage;
    const bool replyDeniesImage = replyDeniesImageAvailability(replies.join(QStringLiteral("\n")));
    if (!replyDeniesImage) {
        for (const ImageCandidate& candidate : std::as_const(m_imageCandidates)) {
            if (!candidate.shouldAttach || !riskTagsEmpty(candidate.riskTags))
                continue;
            const QFileInfo info(candidate.filePath);
            if (!info.exists() || !info.isFile())
                continue;
            selectedImage = candidate;
            RobotSandboxMessageRecord imageRecord;
            imageRecord.robotId = m_robotId;
            imageRecord.role = QStringLiteral("assistant");
            imageRecord.contentType = QStringLiteral("image");
            imageRecord.imagePath = info.absoluteFilePath();
            imageRecord.metadata.insert(QStringLiteral("trace_id"), m_traceId);
            imageRecord.metadata.insert(QStringLiteral("asset_id"), candidate.assetId);
            imageRecord.metadata.insert(QStringLiteral("title"),
                                        candidate.title.isEmpty() ? info.fileName() : candidate.title);
            imageRecord.metadata.insert(QStringLiteral("recommendation_reason"), candidate.reason);
            imageRecord.metadata.insert(QStringLiteral("risk_tags"), candidate.riskTags);
            imageRecord.metadata.insert(QStringLiteral("score"), candidate.score);
            records.append(imageRecord);
            break;
        }
    }
    if (replyDeniesImage && !m_imageCandidates.isEmpty()) {
        m_lastImageStatus += QStringLiteral(" 模型文字表示图片不可用，本次已阻止附图。");
    }

    if (records.isEmpty() || !RobotSandboxMessageDao().appendMany(records)) {
        appendTraceFinish(records.isEmpty() ? QStringLiteral("empty_model_output")
                                            : QStringLiteral("persist_failed"),
                          replies,
                          records.isEmpty() ? QStringLiteral("模型未返回可用正文")
                                            : QStringLiteral("保存 AI 测试回复失败"));
        failReply(records.isEmpty() ? QStringLiteral("模型未返回可用正文")
                                    : QStringLiteral("保存 AI 测试回复失败"));
        return;
    }

    appendTraceFinish(QStringLiteral("success"), replies);

    for (const RobotSandboxMessageRecord& record : std::as_const(records)) {
        if (record.contentType == QLatin1String("image"))
            addImageBubble(record.imagePath, record.metadata);
        else
            addTextBubble(record.role, record.content);
    }
    m_accumulated.clear();
    m_imageCandidates.clear();
    setBusy(false,
            selectedImage.filePath.isEmpty()
                ? QStringLiteral("测试回复已生成。")
                : QStringLiteral("测试回复已生成，并附带 1 张知识库图片。"));
    scrollToBottom();
}

void RobotSandboxDialog::failReply(const QString& reason)
{
    removePendingBubble();
    appendTraceFinish(QStringLiteral("failed"), {}, reason);
    if (m_session) {
        m_session->deleteLater();
        m_session = nullptr;
    }
    m_accumulated.clear();
    m_imageCandidates.clear();
    setBusy(false, QStringLiteral("测试回复失败：%1").arg(reason.left(160)));
}

void RobotSandboxDialog::clearHistory()
{
    if (m_busy)
        return;
    if (QMessageBox::question(this,
                              QStringLiteral("清空测试历史"),
                              QStringLiteral("确定清空“%1”的全部沙盒测试消息吗？").arg(m_robotName),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    if (!RobotSandboxMessageDao().clearForRobot(m_robotId)) {
        m_statusLabel->setText(QStringLiteral("清空测试历史失败。"));
        return;
    }
    loadHistory();
}

void RobotSandboxDialog::setBusy(bool busy, const QString& status)
{
    m_busy = busy;
    m_sendButton->setEnabled(!busy);
    m_clearButton->setEnabled(!busy);
    m_inputEdit->setEnabled(!busy);
    if (!status.isEmpty())
        m_statusLabel->setText(status);
}

void RobotSandboxDialog::previewImage(const QString& path)
{
    const QPixmap pixmap(path);
    if (pixmap.isNull())
        return;
    QDialog preview(this);
    preview.setWindowTitle(QFileInfo(path).fileName());
    preview.resize(820, 620);
    auto* layout = new QVBoxLayout(&preview);
    auto* scroll = new QScrollArea(&preview);
    scroll->setWidgetResizable(true);
    auto* label = new QLabel(scroll);
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(pixmap.scaled(QSize(780, 560), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    scroll->setWidget(label);
    layout->addWidget(scroll);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), &preview);
    connect(closeButton, &QPushButton::clicked, &preview, &QDialog::accept);
    layout->addWidget(closeButton, 0, Qt::AlignRight);
    preview.exec();
}
