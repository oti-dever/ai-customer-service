#include "robotassistantwidget.h"
#include "mainwindow.h"
#include "../data/aiassistantdao.h"
#include "../data/userdao.h"
#include "../services/app/aichatappservice.h"
#include "../services/app/conversationappservice.h"
#include "../services/ai/aiprovidercatalog.h"
#include "../services/ai/aistreamingsession.h"
#include "../utils/appsettings.h"
#include "../utils/applystyle.h"
#include "../utils/svgresourcepixmap.h"
#include "../utils/imagedataurl.h"
#include <QApplication>
#include <QImage>
#include <QEventLoop>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QToolButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMimeDatabase>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStyle>
#include <QSvgRenderer>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {

/** 与聚合会话一致：浅色弹窗底 + 深色字，避免继承父窗口深色 QSS 导致看不清。 */
QString robotMessageBoxContrastStyle()
{
    return ApplyStyle::messageBoxContrastStyle();
}

void showRobotMessageBox(QMessageBox::Icon icon, QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(icon, title, text, QMessageBox::Ok, parent);
    box.setStyleSheet(robotMessageBoxContrastStyle());
    box.exec();
}

QPixmap roundedRobotAvatarPixmap(const QPixmap& source, int logicalSide, qreal dpr, int cornerRadiusLogical)
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

QString robotAssistantQss(ApplyStyle::MainWindowTheme theme)
{
    QString qss = ApplyStyle::aggregateChatFormStyle(theme);
    qss.replace(QStringLiteral("AggregateChatForm"), QStringLiteral("RobotAssistantWidget"));
    qss += ApplyStyle::robotAssistantExtraStyle(theme);
    return qss;
}

static QPixmap pixmapFromSvgResource(const QString& resPath, int logicalSide)
{
    return svgResourcePixmapFittedInSquare(resPath, logicalSide);
}

static QPixmap pixmapFromRasterResource(const QString& resPath, int logicalSide)
{
    return rasterResourcePixmapFittedInSquare(resPath, logicalSide);
}

/** 请求侧最多 10 轮 user↔assistant（§10.2）；从首条 user 对齐截断，避免 orphan assistant。 */
static QList<RobotChatTurn> tailHistoryForApi(const QList<RobotChatTurn>& history)
{
    constexpr int kMaxPairs = 10;
    const int maxMsgs = kMaxPairs * 2;
    if (history.size() <= maxMsgs)
        return history;

    int start = history.size() - maxMsgs;
    while (start < history.size() && history[start].role != QLatin1String("user"))
        ++start;
    return history.mid(start);
}

static const QString kRobotMmKey = QStringLiteral("mm");
static const QString kRobotPathKey = QStringLiteral("p");
static const QString kRobotTxtKey = QStringLiteral("t");

QString formatRobotMultimodalJson(const QString& absPath, const QString& text)
{
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(kRobotMmKey, true);
    o.insert(kRobotPathKey, absPath);
    o.insert(kRobotTxtKey, text);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool parseRobotMultimodalJson(const QString& content, QString* outPath, QString* outText)
{
    if (!content.trimmed().startsWith(QLatin1Char('{')))
        return false;
    QJsonParseError err{};
    const QJsonDocument d = QJsonDocument::fromJson(content.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject())
        return false;
    const QJsonObject o = d.object();
    if (!o.value(kRobotMmKey).toBool())
        return false;
    *outPath = o.value(kRobotPathKey).toString();
    *outText = o.value(kRobotTxtKey).toString();
    return true;
}

static const QString kRobotArkFileKey = QStringLiteral("arkFile");

QString formatRobotArkFileJson(const QString& absPath, const QString& userText)
{
    const QFileInfo fi(absPath);
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(kRobotArkFileKey, true);
    o.insert(kRobotPathKey, fi.absoluteFilePath());
    o.insert(QStringLiteral("n"), fi.fileName());
    o.insert(kRobotTxtKey, userText);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool parseRobotArkFileJson(const QString& content, QString* outPath, QString* outName, QString* outText)
{
    if (!content.trimmed().startsWith(QLatin1Char('{')))
        return false;
    QJsonParseError err{};
    const QJsonDocument d = QJsonDocument::fromJson(content.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject())
        return false;
    const QJsonObject o = d.object();
    if (!o.value(kRobotArkFileKey).toBool())
        return false;
    *outPath = o.value(kRobotPathKey).toString();
    *outName = o.value(QStringLiteral("n")).toString();
    if (outName->isEmpty() && outPath && !outPath->isEmpty())
        *outName = QFileInfo(*outPath).fileName();
    *outText = o.value(kRobotTxtKey).toString();
    return true;
}

} // namespace

QString RobotAssistantWidget::assistantDisplayNameForSessionKey(const QString& sessionModelKey) const
{
    const QString label = aiPresetDefinition(sessionModelKey).assistantDisplayName;
    return label.isEmpty() ? QStringLiteral("助手") : label;
}

QString RobotAssistantWidget::assistantAvatarResourceForSessionKey(const QString& sessionModelKey) const
{
    const QString res = aiPresetDefinition(sessionModelKey).assistantAvatarResource;
    return res.isEmpty() ? QStringLiteral(":/aggregate_reception_icons/deepseek_logo_icon.svg")
                         : res;
}

QString RobotAssistantWidget::systemPromptForRequest() const
{
    const QString base = QStringLiteral(
        "你是桌面应用「AI 客服」的内置帮助助手。回答与本软件相关的使用问题，例如登录、添加与管理各平台窗口、聚合会话、RPA 启动与控制台、主题与设置等。"
        "辅助设定：若用户询问其他话题，也可以陪伴聊天。"
        "性格：幽默、活泼、可爱，但不要显得太刻意。"
        "回答时可分步说明操作路径；不要编造不存在的菜单或按钮名称。\n\n"
        "【软件功能摘要】\n"
        "- 支持多平台客服窗口聚合管理（微信、千牛、拼多多等以实际已接入为准）。\n"
        "- 提供聚合会话、RPA 启动/停止与管理、控制台日志查看。\n"
        "- 支持主题切换与个人账户相关设置。\n"
        "- 本助手仅用于解答本软件用法，不会自动向各平台客户发送消息。");

    const QString modelParam = loadAiProviderConfig(m_activePresetSessionKey).model.trimmed();
    const QString modelHint = modelParam.isEmpty() ? QStringLiteral("（由用户在「API 配置/模型」中填写）") : modelParam;

    if (m_activePresetSessionKey == QLatin1String("doubao:ark")) {
        const QString tail = QStringLiteral(
            "\n\n【回答风格】"
            "优先简洁：除非用户明确要求「详细说明」「一步步」等，否则控制篇幅，避免冗长铺垫与重复解释。\n\n"
            "【关于你的身份与底层模型】"
            "用户在「AI 助手」中当前选择的是火山引擎方舟·豆包线路；本软件请求里使用的 model 参数为「%1」。"
            "若用户问你是什么模型、谁提供的、是不是 DeepSeek，请如实说明："
            "你是本软件内置的帮助助手，当前对话由火山方舟接入的豆包大模型（Doubao）提供推理，界面展示名可用「doubao」；"
            "不要自称 DeepSeek 或其它未在设置中选中的厂商。"
            "若问版本或接入点细节，可说明以用户本地填写的模型名称/接入点 ID 为准。")
            .arg(modelHint);
        return base + tail;
    }
    if (m_activePresetSessionKey == QLatin1String("deepseek:deepseek-chat")) {
        const QString tail = QStringLiteral(
            "\n\n【关于你的身份与底层模型】"
            "用户在「AI 助手」中当前选择的是 DeepSeek；请求中的 model 参数一般为「%1」。"
            "若用户问你是什么模型，请说明你由 DeepSeek 提供能力，是本软件内置帮助助手，界面展示名可用「DeepSeek」。"
            "不要自称豆包或其它未选中的厂商。")
            .arg(modelHint);
        return base + tail;
    }
    const QString tail = QStringLiteral(
        "\n\n【关于你的身份】若用户询问所用模型或厂商，说明你是本软件内置帮助助手，具体后端以用户在「AI 客服后台 → API 配置/模型」中的选择为准。");
    return base + tail;
}

RobotAssistantWidget::RobotAssistantWidget(const QString& loginUsername, QWidget* parent)
    : QWidget(parent)
    , m_conversationService(new ConversationAppService())
    , m_aiChatService(new AiChatAppService(this))
    , m_loginUsername(loginUsername)
{
    setObjectName(QStringLiteral("robotAssistantRoot"));
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // —— 单页对话（布局对齐聚合会话中心：aggregateCenterPanel + chatArea + messageScroll + bubble 样式）——
    auto* chatPage = new QWidget(this);
    auto* chatOuter = new QVBoxLayout(chatPage);
    chatOuter->setContentsMargins(0, 0, 0, 0);
    chatOuter->setSpacing(0);

    auto* centerPanel = new QWidget(chatPage);
    centerPanel->setObjectName(QStringLiteral("aggregateCenterPanel"));
    centerPanel->setAutoFillBackground(true);
    auto* centerLay = new QVBoxLayout(centerPanel);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(0);

    auto* chatArea = new QWidget(centerPanel);
    chatArea->setObjectName(QStringLiteral("chatArea"));
    chatArea->setAutoFillBackground(true);
    auto* chatAreaLay = new QVBoxLayout(chatArea);
    chatAreaLay->setContentsMargins(0, 0, 0, 0);
    chatAreaLay->setSpacing(0);

    auto* headerRow = new QWidget(chatArea);
    headerRow->setFixedHeight(48);
    auto* headerLay = new QHBoxLayout(headerRow);
    headerLay->setContentsMargins(16, 0, 16, 0);
    headerLay->setSpacing(12);
    auto* chatTitle = new QLabel(QStringLiteral("内置 AI 助手"), headerRow);
    chatTitle->setObjectName(QStringLiteral("chatHeader"));
    chatTitle->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    headerLay->addWidget(chatTitle, 0, Qt::AlignVCenter);
    headerLay->addStretch(1);
    m_modelPresetCombo = new QComboBox(headerRow);
    m_modelPresetCombo->setObjectName(QStringLiteral("robotAssistantModelCombo"));
    m_modelPresetCombo->setCursor(Qt::PointingHandCursor);
    m_modelPresetCombo->setFocusPolicy(Qt::StrongFocus);
    headerLay->addWidget(m_modelPresetCombo, 0, Qt::AlignVCenter);
    chatAreaLay->addWidget(headerRow);

    m_scroll = new QScrollArea(chatArea);
    m_scroll->setObjectName(QStringLiteral("messageScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    if (QWidget* vp = m_scroll->viewport())
        vp->setObjectName(QStringLiteral("messageScrollViewport"));

    m_chatInner = new QWidget(m_scroll);
    m_chatLayout = new QVBoxLayout(m_chatInner);
    m_chatLayout->setContentsMargins(16, 12, 16, 12);
    m_chatLayout->setSpacing(8);
    m_chatLayout->addStretch(1);
    m_scroll->setWidget(m_chatInner);
    chatAreaLay->addWidget(m_scroll, 1);

    auto* divider = new QFrame(chatArea);
    divider->setFrameShape(QFrame::HLine);
    divider->setObjectName(QStringLiteral("inputDivider"));
    chatAreaLay->addWidget(divider);

    auto* inputArea = new QWidget(chatArea);
    auto* inputAreaLay = new QVBoxLayout(inputArea);
    inputAreaLay->setContentsMargins(16, 8, 16, 12);
    inputAreaLay->setSpacing(8);

    auto* inputToolbar = new QHBoxLayout();
    inputToolbar->setSpacing(8);
    m_pictureBtn = new QToolButton(inputArea);
    m_pictureBtn->setObjectName(QStringLiteral("robotAssistantAttachBtn"));
    m_pictureBtn->setToolTip(QStringLiteral("添加图片（豆包多模态）"));
    m_pictureBtn->setCursor(Qt::PointingHandCursor);
    m_pictureBtn->setAutoRaise(true);
    {
        const QPixmap pic = pixmapFromSvgResource(QStringLiteral(":/picture_icon.svg"), 22);
        if (!pic.isNull())
            m_pictureBtn->setIcon(QIcon(pic));
    }
    m_pictureBtn->setIconSize(QSize(22, 22));
    m_pictureBtn->setFixedSize(32, 32);
    m_fileBtn = new QToolButton(inputArea);
    m_fileBtn->setObjectName(QStringLiteral("robotAssistantAttachBtn"));
    m_fileBtn->setToolTip(QStringLiteral("添加文件（火山方舟 Files API，仅豆包线路）"));
    m_fileBtn->setCursor(Qt::PointingHandCursor);
    m_fileBtn->setAutoRaise(true);
    {
        const QPixmap fi = pixmapFromSvgResource(QStringLiteral(":/file_icon.svg"), 22);
        if (!fi.isNull())
            m_fileBtn->setIcon(QIcon(fi));
    }
    m_fileBtn->setIconSize(QSize(22, 22));
    m_fileBtn->setFixedSize(32, 32);
    inputToolbar->addWidget(m_pictureBtn);
    inputToolbar->addWidget(m_fileBtn);
    inputToolbar->addStretch(1);
    inputAreaLay->addLayout(inputToolbar);

    m_pendingAttachmentRow = new QWidget(inputArea);
    m_pendingAttachmentRow->setVisible(false);
    auto* pendingLay = new QHBoxLayout(m_pendingAttachmentRow);
    pendingLay->setContentsMargins(0, 0, 0, 0);
    pendingLay->setSpacing(8);
    m_pendingThumbLabel = new QLabel(m_pendingAttachmentRow);
    m_pendingThumbLabel->setFixedSize(48, 48);
    m_pendingThumbLabel->setObjectName(QStringLiteral("robotPendingThumb"));
    m_pendingNameLabel = new QLabel(m_pendingAttachmentRow);
    m_pendingNameLabel->setObjectName(QStringLiteral("robotPendingName"));
    m_pendingNameLabel->setWordWrap(true);
    m_pendingClearBtn = new QPushButton(QStringLiteral("移除"), m_pendingAttachmentRow);
    m_pendingClearBtn->setObjectName(QStringLiteral("simulateButton"));
    m_pendingClearBtn->setCursor(Qt::PointingHandCursor);
    pendingLay->addWidget(m_pendingThumbLabel);
    pendingLay->addWidget(m_pendingNameLabel, 1);
    pendingLay->addWidget(m_pendingClearBtn, 0, Qt::AlignTop);
    inputAreaLay->addWidget(m_pendingAttachmentRow);

    m_input = new QPlainTextEdit(inputArea);
    m_input->setObjectName(QStringLiteral("messageInput"));
    m_input->setPlaceholderText(QStringLiteral("输入问题，Ctrl+Enter 发送"));
    m_input->setMaximumHeight(100);
    inputAreaLay->addWidget(m_input);
    auto* inputBtnRow = new QHBoxLayout();
    inputBtnRow->addStretch(1);
    m_sendBtn = new QPushButton(QStringLiteral("发送"), inputArea);
    m_sendBtn->setObjectName(QStringLiteral("sendButton"));
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn = new QPushButton(QStringLiteral("清空会话"), inputArea);
    m_clearBtn->setObjectName(QStringLiteral("simulateButton"));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    inputBtnRow->addWidget(m_sendBtn);
    inputBtnRow->addWidget(m_clearBtn);
    inputAreaLay->addLayout(inputBtnRow);
    chatAreaLay->addWidget(inputArea);

    centerLay->addWidget(chatArea, 1);

    m_statusLabel = new QLabel(centerPanel);
    m_statusLabel->setObjectName(QStringLiteral("robotAssistantStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setVisible(false);
    m_statusLabel->setContentsMargins(12, 4, 12, 8);
    centerLay->addWidget(m_statusLabel);

    chatOuter->addWidget(centerPanel, 1);
    rootLayout->addWidget(chatPage, 1);

    connect(m_sendBtn, &QPushButton::clicked, this, &RobotAssistantWidget::onSendMessage);
    connect(m_clearBtn, &QPushButton::clicked, this, &RobotAssistantWidget::onClearChat);
    connect(m_pictureBtn, &QToolButton::clicked, this, &RobotAssistantWidget::onPickPicture);
    connect(m_fileBtn, &QToolButton::clicked, this, &RobotAssistantWidget::onPickFile);
    connect(m_pendingClearBtn, &QPushButton::clicked, this, [this]() {
        clearPendingAttachment();
        applySendButtonPolicy();
    });
    connect(m_input, &QPlainTextEdit::textChanged, this, &RobotAssistantWidget::applySendButtonPolicy);

    auto* sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_input);
    connect(sc, &QShortcut::activated, this, &RobotAssistantWidget::onSendMessage);

    m_theme = ApplyStyle::MainWindowTheme::Default;
    applyTheme(m_theme);

    migrateLegacyAiSettingsToPresets();
    fillPresetCombo(m_modelPresetCombo);
    if (m_modelPresetCombo->count() > 0)
        m_activePresetSessionKey = sessionModelKeyAtComboIndex(0);

    connect(m_modelPresetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &RobotAssistantWidget::onPresetComboIndexChanged);

    loadSelfBubbleIdentity();
    rebindSessionFromCurrentConfig();
}

void RobotAssistantWidget::onExternalProviderConfigChanged()
{
    rebindSessionFromCurrentConfig();
    applySendButtonPolicy();
}

void RobotAssistantWidget::refreshLocalUserProfile()
{
    loadSelfBubbleIdentity();
}

void RobotAssistantWidget::fillPresetCombo(QComboBox* combo)
{
    if (!combo)
        return;
    combo->clear();
    auto add = [combo](const AiPresetDefinition& def) {
        QVariantMap m;
        m.insert(QStringLiteral("sessionModelKey"), def.sessionModelKey);
        m.insert(QStringLiteral("available"), def.available);
        const int idx = combo->count();
        combo->addItem(def.label);
        combo->setItemData(idx, m, Qt::UserRole);
    };
    for (const AiPresetDefinition& def : aiPresetDefinitions())
        add(def);
}

void RobotAssistantWidget::migrateLegacyAiSettingsToPresets()
{
    migrateLegacyAiSettingsToPreset(QStringLiteral("deepseek:deepseek-chat"));
}

QString RobotAssistantWidget::sessionModelKeyAtComboIndex(int index) const
{
    if (!m_modelPresetCombo || index < 0 || index >= m_modelPresetCombo->count())
        return {};
    return m_modelPresetCombo->itemData(index, Qt::UserRole)
        .toMap()
        .value(QStringLiteral("sessionModelKey"))
        .toString();
}

void RobotAssistantWidget::onPresetComboIndexChanged(int index)
{
    if (index < 0 || !m_modelPresetCombo || index >= m_modelPresetCombo->count())
        return;

    m_activePresetSessionKey = sessionModelKeyAtComboIndex(index);

    if (m_activePresetSessionKey != QLatin1String("doubao:ark"))
        clearPendingAttachment();
    else
        updatePendingAttachmentUi();

    rebindSessionFromCurrentConfig();
    applySendButtonPolicy();
}

RobotAssistantWidget::~RobotAssistantWidget()
{
    delete m_conversationService;
}

QVariantMap RobotAssistantWidget::currentPresetData() const
{
    if (!m_modelPresetCombo || m_modelPresetCombo->count() <= 0)
        return {};
    return m_modelPresetCombo->currentData(Qt::UserRole).toMap();
}

bool RobotAssistantWidget::currentPresetAvailable() const
{
    return currentPresetData().value(QStringLiteral("available")).toBool();
}

void RobotAssistantWidget::applySendButtonPolicy()
{
    if (!m_sendBtn)
        return;
    const bool base = currentPresetAvailable() && !m_busy;
    bool canSend = base;
    if (base) {
        const AiProviderCapabilities capabilities = aiPresetDefinition(m_activePresetSessionKey).capabilities;
        const QString t = m_input ? m_input->toPlainText().trimmed() : QString();
        const bool hasText = !t.isEmpty();
        const bool hasPendingImg = capabilities.supportsVisionDataUrl && !m_pendingImagePath.isEmpty();
        const bool hasPendingFile = capabilities.supportsFileAttachment && !m_pendingFilePath.isEmpty();
        canSend = hasText || hasPendingImg || hasPendingFile;
    }
    m_sendBtn->setEnabled(canSend);
    applyAttachmentButtonsPolicy();
}

void RobotAssistantWidget::applyAttachmentButtonsPolicy()
{
    const AiPresetDefinition def = aiPresetDefinition(m_activePresetSessionKey);
    if (m_pictureBtn) {
        const bool supported = def.capabilities.supportsVisionDataUrl;
        m_pictureBtn->setVisible(supported);
        m_pictureBtn->setEnabled(supported && !m_busy);
    }
    if (m_fileBtn) {
        const bool supported = def.capabilities.supportsFileAttachment;
        m_fileBtn->setVisible(supported);
        m_fileBtn->setEnabled(supported && !m_busy);
        m_fileBtn->setToolTip(def.capabilities.supportsFileAttachment
                                  ? QStringLiteral("添加文件（当前模型支持附件推理）")
                                  : QStringLiteral("当前模型不支持文件附件"));
    }
}

void RobotAssistantWidget::loadSelfBubbleIdentity()
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
    m_selfAvatarPixmap = roundedRobotAvatarPixmap(pm, kAvatarLogical, dpr, kAvatarLogical / 2);
}

void RobotAssistantWidget::applyTheme(ApplyStyle::MainWindowTheme theme)
{
    Q_UNUSED(theme)
    m_theme = ApplyStyle::MainWindowTheme::Default;
    setStyleSheet(robotAssistantQss(m_theme));
    applySendButtonPolicy();
}

void RobotAssistantWidget::setStatusText(const QString& text)
{
    if (!m_statusLabel)
        return;
    const QString trimmed = text.trimmed();
    m_statusLabel->setText(trimmed);
    m_statusLabel->setVisible(!trimmed.isEmpty());
}

void RobotAssistantWidget::clearChatLayout()
{
    while (m_chatLayout->count() > 1) {
        QLayoutItem* it = m_chatLayout->takeAt(0);
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    m_streamingLabel = nullptr;
    m_accumulatedAssistant.clear();
}

void RobotAssistantWidget::onClearChat()
{
    clearStreamingSession(m_activeSession);
    if (m_sessionId > 0) {
        AiAssistantDao dao;
        dao.clearMessages(m_sessionId);
    }
    m_history.clear();
    clearChatLayout();
    clearPendingAttachment();
    setStatusText(QString());
    if (!currentPresetAvailable())
        setStatusText(QStringLiteral("该模型即将接入，请选用 DeepSeek 或豆包。"));
    applySendButtonPolicy();
}

void RobotAssistantWidget::clearPendingAttachment()
{
    m_pendingImagePath.clear();
    m_pendingFilePath.clear();
    updatePendingAttachmentUi();
}

void RobotAssistantWidget::clearStreamingSession(IAiStreamingSession*& session)
{
    if (!session)
        return;
    session->disconnect(this);
    session->abort();
    session->deleteLater();
    session = nullptr;
}

void RobotAssistantWidget::updatePendingAttachmentUi()
{
    if (!m_pendingAttachmentRow || !m_pendingThumbLabel || !m_pendingNameLabel)
        return;
    const AiProviderCapabilities capabilities = aiPresetDefinition(m_activePresetSessionKey).capabilities;
    const bool hasImg = capabilities.supportsVisionDataUrl && !m_pendingImagePath.isEmpty();
    const bool hasFile = capabilities.supportsFileAttachment && !m_pendingFilePath.isEmpty();
    const bool on = hasImg || hasFile;
    m_pendingAttachmentRow->setVisible(on);
    if (!on) {
        m_pendingThumbLabel->clear();
        m_pendingNameLabel->clear();
        return;
    }
    if (hasImg) {
        const QFileInfo fi(m_pendingImagePath);
        m_pendingNameLabel->setText(fi.fileName());
        QImage img(m_pendingImagePath);
        if (!img.isNull()) {
            const QPixmap pm = QPixmap::fromImage(
                img.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_pendingThumbLabel->setPixmap(pm);
        } else {
            m_pendingThumbLabel->setText(QStringLiteral("图"));
        }
    } else {
        const QFileInfo fi(m_pendingFilePath);
        m_pendingNameLabel->setText(fi.fileName());
        const QPixmap ic = pixmapFromSvgResource(QStringLiteral(":/file_icon.svg"), 40);
        if (!ic.isNull())
            m_pendingThumbLabel->setPixmap(
                ic.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else
            m_pendingThumbLabel->setText(QStringLiteral("文"));
    }
}

void RobotAssistantWidget::onPickPicture()
{
    if (m_activePresetSessionKey != QLatin1String("doubao:ark")) {
        showRobotMessageBox(QMessageBox::Information, this, QStringLiteral("图片"),
                            QStringLiteral("发送图片仅支持「豆包」线路，请先在下拉框中选择豆包。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择图片"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isReadable()) {
        showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("图片"),
                            QStringLiteral("无法读取所选文件。"));
        return;
    }
    constexpr qint64 kMaxBytes = 5 * 1024 * 1024;
    if (fi.size() > kMaxBytes) {
        showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("图片"),
                            QStringLiteral("单张图片请小于约 5MB。"));
        return;
    }
    m_pendingFilePath.clear();
    m_pendingImagePath = fi.absoluteFilePath();
    updatePendingAttachmentUi();
    applySendButtonPolicy();
}

void RobotAssistantWidget::onPickFile()
{
    if (m_activePresetSessionKey != QLatin1String("doubao:ark")) {
        showRobotMessageBox(QMessageBox::Information, this, QStringLiteral("文件"),
                            QStringLiteral("文件上传仅支持「豆包」线路，请先在下拉框中选择豆包。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择文件"), QString(),
        QStringLiteral("文档与媒体 (*.pdf *.png *.jpg *.jpeg *.webp *.bmp *.gif *.mp4 *.mov *.avi);;"
                       "PDF (*.pdf);;图片 (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;视频 (*.mp4 *.mov *.avi);;"
                       "所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isReadable()) {
        showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("文件"),
                            QStringLiteral("无法读取所选文件。"));
        return;
    }
    constexpr qint64 kMaxBytes = 10LL * 1024 * 1024;
    if (fi.size() > kMaxBytes) {
        showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("文件"),
                            QStringLiteral("单文件请不超过 10MB。"));
        return;
    }

    QMimeDatabase db;
    const QString mime = db.mimeTypeForFile(fi.absoluteFilePath()).name();
    if (mime.startsWith(QLatin1String("text/"))) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("文件"),
                                QStringLiteral("无法读取该文本文件。"));
            return;
        }
        constexpr int kMaxInlineChars = 512 * 1024;
        const QByteArray raw = f.readAll();
        if (raw.size() > kMaxInlineChars) {
            showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("文件"),
                                QStringLiteral("文本过大，请拆小文件或改用 PDF 上传。"));
            return;
        }
        const QString content = QString::fromUtf8(raw);
        if (m_input) {
            QString cur = m_input->toPlainText();
            if (!cur.isEmpty() && !cur.endsWith(QChar('\n')))
                cur += QChar('\n');
            m_input->setPlainText(cur + content);
        }
        showRobotMessageBox(
            QMessageBox::Information,
            this,
            QStringLiteral("纯文本文件"),
            QStringLiteral(
                "火山方舟 Files API 不支持上传纯文本（text/plain），已将文件内容填入下方输入框。\n"
                "请直接点「发送」，将走对话 API；若需要「文档理解」能力，请使用 PDF 或图片后再试「添加文件」。"));
        return;
    }

    m_pendingImagePath.clear();
    m_pendingFilePath = fi.absoluteFilePath();
    updatePendingAttachmentUi();
    applySendButtonPolicy();
}

void RobotAssistantWidget::appendUserBubble(const QString& text)
{
    auto* row = new QWidget(m_chatInner);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(8);
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
    const QString nick = m_selfDisplayName.trimmed().isEmpty() ? QStringLiteral("我") : m_selfDisplayName;
    auto* nickLab = new QLabel(nick, metaRow);
    nickLab->setObjectName(QStringLiteral("bubbleOutSenderNick"));
    metaRowLayout->addWidget(nickLab);
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
    bubbleLayout->setSpacing(0);
    auto* contentLabel = new QLabel(text, bubble);
    contentLabel->setWordWrap(true);
    contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLabel->setObjectName(QStringLiteral("bubbleTextOut"));
    bubbleLayout->addWidget(contentLabel);
    bubble->setMaximumWidth(420);
    bubbleRowLayout->addWidget(bubble, 0, Qt::AlignTop);
    rightColLayout->addWidget(bubbleRow);

    rowLayout->addWidget(rightCol, 0, Qt::AlignTop);

    auto* avatar = new QLabel(row);
    avatar->setObjectName(QStringLiteral("bubbleOutAvatar"));
    avatar->setFixedSize(36, 36);
    if (!m_selfAvatarPixmap.isNull())
        avatar->setPixmap(m_selfAvatarPixmap);
    else {
        QPixmap av = pixmapFromSvgResource(QStringLiteral(":/default_avatar_icon.svg"), 36);
        if (!av.isNull())
            avatar->setPixmap(av);
        else
            avatar->setPixmap(
                qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(36, 36));
    }
    rowLayout->addWidget(avatar, 0, Qt::AlignTop);

    m_chatLayout->insertWidget(m_chatLayout->count() - 1, row);
}

void RobotAssistantWidget::appendUserImageBubble(const QString& absolutePath)
{
    auto* row = new QWidget(m_chatInner);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(8);
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
    const QString nick = m_selfDisplayName.trimmed().isEmpty() ? QStringLiteral("我") : m_selfDisplayName;
    auto* nickLab = new QLabel(nick, metaRow);
    nickLab->setObjectName(QStringLiteral("bubbleOutSenderNick"));
    metaRowLayout->addWidget(nickLab);
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
    bubbleLayout->setSpacing(0);
    auto* contentLabel = new QLabel(bubble);
    contentLabel->setObjectName(QStringLiteral("bubbleTextOut"));
    contentLabel->setAlignment(Qt::AlignCenter);
    QPixmap pm;
    if (!absolutePath.isEmpty() && QFile::exists(absolutePath)) {
        QImage img(absolutePath);
        if (!img.isNull())
            pm = QPixmap::fromImage(
                img.scaled(280, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (!pm.isNull())
        contentLabel->setPixmap(pm);
    else
        contentLabel->setText(QStringLiteral("（图片不可用或已移动）"));
    bubbleLayout->addWidget(contentLabel);
    bubble->setMaximumWidth(420);
    bubbleRowLayout->addWidget(bubble, 0, Qt::AlignTop);
    rightColLayout->addWidget(bubbleRow);

    rowLayout->addWidget(rightCol, 0, Qt::AlignTop);

    auto* avatar = new QLabel(row);
    avatar->setObjectName(QStringLiteral("bubbleOutAvatar"));
    avatar->setFixedSize(36, 36);
    if (!m_selfAvatarPixmap.isNull())
        avatar->setPixmap(m_selfAvatarPixmap);
    else {
        QPixmap av = pixmapFromSvgResource(QStringLiteral(":/default_avatar_icon.svg"), 36);
        if (!av.isNull())
            avatar->setPixmap(av);
        else
            avatar->setPixmap(
                qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(36, 36));
    }
    rowLayout->addWidget(avatar, 0, Qt::AlignTop);

    m_chatLayout->insertWidget(m_chatLayout->count() - 1, row);
}

void RobotAssistantWidget::appendUserFileBubble(const QString& absolutePath, const QString& displayName,
                                                const QString& text)
{
    auto* row = new QWidget(m_chatInner);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(8);
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
    const QString nick = m_selfDisplayName.trimmed().isEmpty() ? QStringLiteral("我") : m_selfDisplayName;
    auto* nickLab = new QLabel(nick, metaRow);
    nickLab->setObjectName(QStringLiteral("bubbleOutSenderNick"));
    metaRowLayout->addWidget(nickLab);
    rightColLayout->addWidget(metaRow);

    auto* bubbleRow = new QWidget(rightCol);
    auto* bubbleRowLayout = new QHBoxLayout(bubbleRow);
    bubbleRowLayout->setContentsMargins(0, 0, 0, 0);
    bubbleRowLayout->setSpacing(8);
    bubbleRowLayout->addStretch(1);
    auto* bubble = new QFrame(bubbleRow);
    bubble->setObjectName(QStringLiteral("bubbleOut"));
    auto* bubbleLayout = new QHBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 8, 12, 8);
    bubbleLayout->setSpacing(8);
    auto* iconLab = new QLabel(bubble);
    iconLab->setFixedSize(40, 40);
    const QPixmap ic = pixmapFromSvgResource(QStringLiteral(":/file_icon.svg"), 36);
    if (!ic.isNull())
        iconLab->setPixmap(ic.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        iconLab->setText(QStringLiteral("文"));
    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(4);
    auto* nameLab = new QLabel(QStringLiteral("附件：%1").arg(displayName.isEmpty()
                                                                   ? QFileInfo(absolutePath).fileName()
                                                                   : displayName),
                               bubble);
    nameLab->setObjectName(QStringLiteral("bubbleTextOut"));
    nameLab->setWordWrap(true);
    textCol->addWidget(nameLab);
    if (!text.trimmed().isEmpty()) {
        auto* tlab = new QLabel(text, bubble);
        tlab->setObjectName(QStringLiteral("bubbleTextOut"));
        tlab->setWordWrap(true);
        tlab->setTextInteractionFlags(Qt::TextSelectableByMouse);
        textCol->addWidget(tlab);
    }
    bubbleLayout->addWidget(iconLab);
    bubbleLayout->addLayout(textCol, 1);
    bubble->setMaximumWidth(420);
    bubbleRowLayout->addWidget(bubble, 0, Qt::AlignTop);
    rightColLayout->addWidget(bubbleRow);

    rowLayout->addWidget(rightCol, 0, Qt::AlignTop);

    auto* avatar = new QLabel(row);
    avatar->setObjectName(QStringLiteral("bubbleOutAvatar"));
    avatar->setFixedSize(36, 36);
    if (!m_selfAvatarPixmap.isNull())
        avatar->setPixmap(m_selfAvatarPixmap);
    else {
        QPixmap av = pixmapFromSvgResource(QStringLiteral(":/default_avatar_icon.svg"), 36);
        if (!av.isNull())
            avatar->setPixmap(av);
        else
            avatar->setPixmap(
                qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(36, 36));
    }
    rowLayout->addWidget(avatar, 0, Qt::AlignTop);

    m_chatLayout->insertWidget(m_chatLayout->count() - 1, row);
}

void RobotAssistantWidget::appendUserMultimodalDisplay(const QString& absolutePath, const QString& text)
{
    appendUserImageBubble(absolutePath);
    if (!text.trimmed().isEmpty())
        appendUserBubble(text);
}

QLabel* RobotAssistantWidget::appendAssistantBubble()
{
    const QString sessionKey =
        m_boundModelKey.isEmpty() ? m_activePresetSessionKey : m_boundModelKey;
    const QString metaName = assistantDisplayNameForSessionKey(sessionKey);
    const QString avatarRes = assistantAvatarResourceForSessionKey(sessionKey);

    auto* row = new QWidget(m_chatInner);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(8);

    auto* avatar = new QLabel(row);
    avatar->setObjectName(QStringLiteral("robotAssistantAvatarIn"));
    avatar->setFixedSize(36, 36);
    QPixmap ap;
    if (avatarRes.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        ap = pixmapFromSvgResource(avatarRes, 36);
    else
        ap = pixmapFromRasterResource(avatarRes, 36);
    if (!ap.isNull())
        avatar->setPixmap(ap);
    rowLayout->addWidget(avatar, 0, Qt::AlignTop);

    auto* col = new QWidget(row);
    auto* colLayout = new QVBoxLayout(col);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(4);
    auto* meta = new QLabel(metaName, col);
    meta->setObjectName(QStringLiteral("bubbleMetaIn"));
    colLayout->addWidget(meta);

    auto* bubble = new QFrame(col);
    bubble->setObjectName(QStringLiteral("bubbleIn"));
    auto* bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 8, 12, 8);
    bubbleLayout->setSpacing(0);
    auto* contentLabel = new QLabel(bubble);
    contentLabel->setWordWrap(true);
    contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLabel->setObjectName(QStringLiteral("bubbleTextIn"));
    bubbleLayout->addWidget(contentLabel);
    bubble->setMaximumWidth(420);
    colLayout->addWidget(bubble);

    rowLayout->addWidget(col, 0, Qt::AlignTop);
    rowLayout->addStretch(1);

    m_chatLayout->insertWidget(m_chatLayout->count() - 1, row);
    return contentLabel;
}

void RobotAssistantWidget::scrollToBottom()
{
    if (!m_scroll)
        return;
    auto* sb = m_scroll->verticalScrollBar();
    if (sb)
        sb->setValue(sb->maximum());
}

void RobotAssistantWidget::scheduleScrollChatToBottom()
{
    QTimer::singleShot(100, this, [this]() {
        if (!m_scroll || !m_chatInner)
            return;
        m_chatInner->updateGeometry();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        scrollToBottom();
        QTimer::singleShot(0, this, [this]() { scrollToBottom(); });
    });
}

void RobotAssistantWidget::nudgeScrollAfterContentChange()
{
    if (!m_scroll || !m_chatInner)
        return;
    m_chatInner->updateGeometry();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    scrollToBottom();
    QTimer::singleShot(0, this, [this]() { scrollToBottom(); });
}

AiProviderConfig RobotAssistantWidget::currentAiProviderConfig() const
{
    return m_aiChatService
               ? m_aiChatService->resolveProviderConfig(m_activePresetSessionKey, QString(), QString(), QString())
               : AiProviderConfig{};
}

AiRequest RobotAssistantWidget::buildAiRequestForConversation() const
{
    AiRequest request;
    request.systemPrompt = systemPromptForRequest();
    const QList<RobotChatTurn> tail = tailHistoryForApi(m_history);
    for (const RobotChatTurn& t : tail) {
        AiConversationTurn turn;
        turn.role = t.role;
        if (t.role == QLatin1String("user")) {
            QString pth, txt;
            if (parseRobotMultimodalJson(t.content, &pth, &txt)) {
                turn.parts.append(makeAiImageFilePart(pth));
                if (!txt.trimmed().isEmpty())
                    turn.parts.append(makeAiTextPart(txt));
            } else {
                QString fp, fn, ftx;
                if (parseRobotArkFileJson(t.content, &fp, &fn, &ftx)) {
                    turn.parts.append(makeAiLocalFilePart(fp, fn));
                    if (!ftx.trimmed().isEmpty())
                        turn.parts.append(makeAiTextPart(ftx));
                } else {
                    turn.parts.append(makeAiTextPart(t.content));
                }
            }
        } else {
            turn.parts.append(makeAiTextPart(t.content));
        }
        request.turns.append(turn);
    }
    return request;
}

void RobotAssistantWidget::setBusy(bool busy)
{
    m_busy = busy;
    m_input->setReadOnly(busy);
    applySendButtonPolicy();
}

void RobotAssistantWidget::rebindSessionFromCurrentConfig()
{
    UserDao udao;
    const auto u = udao.findByUsername(m_loginUsername);
    if (!u) {
        m_userId = 0;
        m_sessionId = -1;
        m_boundModelKey.clear();
        applySendButtonPolicy();
        return;
    }
    m_userId = u->id;

    const QVariantMap preset = currentPresetData();
    const bool presetAvail = preset.value(QStringLiteral("available")).toBool();
    const QString mk = preset.value(QStringLiteral("sessionModelKey")).toString();

    if (!presetAvail || mk.isEmpty()) {
        m_boundModelKey.clear();
        m_sessionId = -1;
        m_history.clear();
        clearChatLayout();
        setStatusText(QStringLiteral("该模型即将接入，请选用 DeepSeek 或豆包。"));
        applySendButtonPolicy();
        return;
    }

    if (mk == m_boundModelKey && m_sessionId > 0)
        return;

    m_boundModelKey = mk;
    AiAssistantDao adao;
    m_sessionId = adao.ensureSession(m_userId, mk);

    m_history.clear();
    clearChatLayout();
    setStatusText(QString());

    if (m_sessionId <= 0) {
        applySendButtonPolicy();
        return;
    }

    const QVector<AiAssistantChatTurn> rows = adao.listMessages(m_sessionId);
    for (const AiAssistantChatTurn& row : rows) {
        m_history.append({row.role, row.content});
        if (row.role == QLatin1String("user")) {
            QString imgPath, imgText;
            if (parseRobotMultimodalJson(row.content, &imgPath, &imgText)) {
                appendUserMultimodalDisplay(imgPath, imgText);
            } else {
                QString fp, fn, ftx;
                if (parseRobotArkFileJson(row.content, &fp, &fn, &ftx))
                    appendUserFileBubble(fp, fn, ftx);
                else
                    appendUserBubble(row.content);
            }
        }
        else if (row.role == QLatin1String("assistant")) {
            QLabel* lab = appendAssistantBubble();
            lab->setText(row.content);
        }
    }
    scheduleScrollChatToBottom();
    applySendButtonPolicy();
}

void RobotAssistantWidget::onSendMessage()
{
    if (m_busy)
        return;
    if (!currentPresetAvailable()) {
        showRobotMessageBox(QMessageBox::Information, this, QStringLiteral("模型"),
                            QStringLiteral("该模型即将支持。请先选择 DeepSeek 或豆包，并在"
                                          "左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中填写并保存。"));
        return;
    }
    clearStreamingSession(m_activeSession);
    m_acceptAssistantStreamDeltas = true;
    const QString text = m_input->toPlainText().trimmed();
    const AiProviderConfig config = currentAiProviderConfig();
    const bool hasImg = !m_pendingImagePath.isEmpty();
    const bool hasFile = !m_pendingFilePath.isEmpty();
    if (text.isEmpty() && !hasImg && !hasFile)
        return;
    if (hasImg && !config.capabilities.supportsVisionDataUrl) {
        showRobotMessageBox(QMessageBox::Information, this, QStringLiteral("图片"),
                            QStringLiteral("当前模型不支持图片输入。"));
        return;
    }
    if (hasFile && !config.capabilities.supportsFileAttachment) {
        showRobotMessageBox(QMessageBox::Information, this, QStringLiteral("文件"),
                            QStringLiteral("当前模型不支持文件附件。"));
        return;
    }

    if (config.apiKey.isEmpty()) {
        showRobotMessageBox(
            QMessageBox::Warning, this, QStringLiteral("发送"),
            QStringLiteral("请先在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中填写并保存 API Key。"));
        if (auto* mw = qobject_cast<MainWindow*>(parentWidget()))
            mw->openAiCustomerServiceBackendWindow(true);
        return;
    }

    rebindSessionFromCurrentConfig();
    if (m_sessionId <= 0) {
        showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("发送"),
                            QStringLiteral("无法加载当前账号，请重新登录后再试。"));
        return;
    }

    if (config.model.isEmpty()) {
        showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("发送"),
                            QStringLiteral("请先在左栏「管理后台」→「AI 客服后台」→「API 配置/模型」中填写模型或接入点。"));
        if (auto* mw = qobject_cast<MainWindow*>(parentWidget()))
            mw->openAiCustomerServiceBackendWindow(true);
        return;
    }

    if (hasImg) {
        QString dummy, err;
        if (!imageFileToDataUrl(m_pendingImagePath, &dummy, &err)) {
            showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("图片"), err);
            return;
        }
    }

    if (hasFile) {
        const QString abs = QFileInfo(m_pendingFilePath).absoluteFilePath();
        if (!QFile::exists(abs)) {
            showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("文件"),
                                QStringLiteral("文件已不存在或无法访问。"));
            return;
        }
        m_input->clear();
        const QString stored = formatRobotArkFileJson(abs, text);
        appendUserFileBubble(abs, QFileInfo(abs).fileName(), text);
        m_history.append({QStringLiteral("user"), stored});
        AiAssistantDao().appendMessage(m_sessionId, QStringLiteral("user"), stored);
        clearPendingAttachment();

        m_accumulatedAssistant.clear();
        m_streamingLabel = appendAssistantBubble();
        m_streamingLabel->clear();

        setBusy(true);
        setStatusText(QStringLiteral("正在上传并分析附件…"));
        AiRequest request = buildAiRequestForConversation();
        m_activeSession = m_aiChatService->createSession(config, request, this);
        connect(m_activeSession, &IAiStreamingSession::delta, this, &RobotAssistantWidget::onClientDelta);
        connect(m_activeSession, &IAiStreamingSession::completed, this, &RobotAssistantWidget::onClientCompleted);
        connect(m_activeSession, &IAiStreamingSession::failed, this, &RobotAssistantWidget::onClientFailed);
        m_activeSession->start();
        scheduleScrollChatToBottom();
        return;
    }

    if (hasImg) {
        const QString abs = QFileInfo(m_pendingImagePath).absoluteFilePath();
        const QString stored = formatRobotMultimodalJson(abs, text);
        m_input->clear();
        appendUserMultimodalDisplay(abs, text);
        m_history.append({QStringLiteral("user"), stored});
        AiAssistantDao().appendMessage(m_sessionId, QStringLiteral("user"), stored);
        clearPendingAttachment();
    } else {
        m_input->clear();
        m_history.append({QStringLiteral("user"), text});
        appendUserBubble(text);
        AiAssistantDao().appendMessage(m_sessionId, QStringLiteral("user"), text);
    }

    m_accumulatedAssistant.clear();
    m_streamingLabel = appendAssistantBubble();
    m_streamingLabel->clear();

    setBusy(true);
    setStatusText(QStringLiteral("正在生成…"));
    AiRequest request = buildAiRequestForConversation();
    m_activeSession = m_aiChatService->createSession(config, request, this);
    connect(m_activeSession, &IAiStreamingSession::delta, this, &RobotAssistantWidget::onClientDelta);
    connect(m_activeSession, &IAiStreamingSession::completed, this, &RobotAssistantWidget::onClientCompleted);
    connect(m_activeSession, &IAiStreamingSession::failed, this, &RobotAssistantWidget::onClientFailed);
    m_activeSession->start();
    scheduleScrollChatToBottom();
}

void RobotAssistantWidget::finishAssistantStreamSuccess(const QString& statusText)
{
    setBusy(false);
    if (!m_accumulatedAssistant.isEmpty()) {
        if (m_sessionId > 0)
            AiAssistantDao().appendMessage(m_sessionId, QStringLiteral("assistant"),
                                           m_accumulatedAssistant);
        m_history.append({QStringLiteral("assistant"), m_accumulatedAssistant});
    }
    m_streamingLabel = nullptr;
    m_accumulatedAssistant.clear();
    m_acceptAssistantStreamDeltas = true;
    setStatusText(statusText);
}

void RobotAssistantWidget::onClientDelta(const QString& delta)
{
    if (!m_acceptAssistantStreamDeltas)
        return;
    if (delta.isEmpty())
        return;

    constexpr int kMaxAssistantReplyChars = 400000;
    QString toAdd = delta;
    if (m_accumulatedAssistant.size() >= kMaxAssistantReplyChars)
        return;
    if (m_accumulatedAssistant.size() + toAdd.size() > kMaxAssistantReplyChars)
        toAdd = toAdd.left(kMaxAssistantReplyChars - m_accumulatedAssistant.size());

    m_accumulatedAssistant += toAdd;
    if (m_streamingLabel)
        m_streamingLabel->setText(m_accumulatedAssistant);
    nudgeScrollAfterContentChange();

    if (m_accumulatedAssistant.size() >= kMaxAssistantReplyChars) {
        m_acceptAssistantStreamDeltas = false;
        clearStreamingSession(m_activeSession);
        const QString tail =
            QStringLiteral("\n\n（单条回复过长，已在此停止接收后续内容，以上为已生成部分。）");
        m_accumulatedAssistant += tail;
        if (m_streamingLabel)
            m_streamingLabel->setText(m_accumulatedAssistant);
        nudgeScrollAfterContentChange();
        finishAssistantStreamSuccess(QStringLiteral("已完成（已达本机单条长度上限）。"));
    }
}

void RobotAssistantWidget::onClientCompleted()
{
    if (!m_busy)
        return;
    clearStreamingSession(m_activeSession);
    finishAssistantStreamSuccess(QStringLiteral("完成。"));
}

void RobotAssistantWidget::onClientFailed(const QString& reason)
{
    if (!m_busy)
        return;
    clearStreamingSession(m_activeSession);
    setBusy(false);
    if (m_streamingLabel) {
        QString body = m_accumulatedAssistant;
        if (body.isEmpty())
            body = QStringLiteral("（未收到正文）");
        m_streamingLabel->setText(
            body + QStringLiteral("\n\n（后续内容未能生成：%1）").arg(reason));
        nudgeScrollAfterContentChange();
    } else {
        setStatusText(QStringLiteral("错误：%1").arg(reason));
    }
    if (!m_accumulatedAssistant.isEmpty()) {
        if (m_sessionId > 0)
            AiAssistantDao().appendMessage(m_sessionId, QStringLiteral("assistant"),
                                           m_accumulatedAssistant);
        m_history.append({QStringLiteral("assistant"), m_accumulatedAssistant});
    }
    m_streamingLabel = nullptr;
    m_accumulatedAssistant.clear();
    m_acceptAssistantStreamDeltas = true;
}

