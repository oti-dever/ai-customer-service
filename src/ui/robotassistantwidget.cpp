#include "robotassistantwidget.h"
#include "../data/aiassistantdao.h"
#include "../data/userdao.h"
#include "../services/ai/openaicompatclient.h"
#include "../utils/applystyle.h"
#include <QApplication>
#include <QEventLoop>
#include <QComboBox>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
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
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {

/** 与聚合会话一致：浅色弹窗底 + 深色字，避免继承父窗口深色 QSS 导致看不清。 */
QString robotMessageBoxContrastStyle()
{
    return QStringLiteral(
        "QMessageBox { background-color: #f4f4f5; }"
        "QMessageBox QLabel { color: #18181b; font-size: 13px; }"
        "QMessageBox QPushButton {"
        "  background-color: #ffffff;"
        "  color: #18181b;"
        "  border: 1px solid #d4d4d8;"
        "  border-radius: 6px;"
        "  padding: 6px 16px;"
        "  min-width: 72px;"
        "}"
        "QMessageBox QPushButton:hover { background-color: #e4e4e7; }"
        "QMessageBox QPushButton:default {"
        "  background-color: #2563eb;"
        "  color: #ffffff;"
        "  border-color: #1d4ed8;"
        "}");
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

QPixmap pixmapFromSvgResource(const QString& resPath, int logicalSide)
{
    QSvgRenderer renderer(resPath);
    if (!renderer.isValid())
        return {};
    const qreal dpr = qApp->devicePixelRatio() > 0 ? qApp->devicePixelRatio() : 1.0;
    const int px = qMax(1, qRound(logicalSide * dpr));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    renderer.render(&p, QRectF(0, 0, px, px));
    p.end();
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QPixmap pixmapFromRasterResource(const QString& resPath, int logicalSide)
{
    QPixmap pm;
    if (!pm.load(resPath))
        return {};
    const qreal dpr = qApp->devicePixelRatio() > 0 ? qApp->devicePixelRatio() : 1.0;
    const int px = qMax(1, qRound(logicalSide * dpr));
    pm = pm.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pm.setDevicePixelRatio(dpr);
    return pm;
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

} // namespace

QString RobotAssistantWidget::defaultSystemPrompt()
{
    return QStringLiteral(
        "你是桌面应用「AI 客服」的内置帮助助手。回答与本软件相关的使用问题，例如登录、添加与管理各平台窗口、聚合会话、RPA 启动与控制台、主题与设置等。"
        "辅助设定：若用户询问其他话题，也可以陪伴聊天。"
        "性格：幽默、活泼、可爱，但不要显得太刻意。"
        "回答时可分步说明操作路径；不要编造不存在的菜单或按钮名称。\n\n"
        "【软件功能摘要】\n"
        "- 支持多平台客服窗口聚合管理（微信、千牛、拼多多等以实际已接入为准）。\n"
        "- 提供聚合会话、RPA 启动/停止与管理、控制台日志查看。\n"
        "- 支持主题切换与个人账户相关设置。\n"
        "- 本助手仅用于解答本软件用法，不会自动向各平台客户发送消息。");
}

RobotAssistantWidget::RobotAssistantWidget(const QString& loginUsername, QWidget* parent)
    : QWidget(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_client(new OpenAiCompatClient(m_nam, this))
    , m_loginUsername(loginUsername)
{
    setObjectName(QStringLiteral("robotAssistantRoot"));
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    rootLayout->addWidget(m_tabs, 1);

    // —— 对话 Tab（布局对齐聚合会话中心：aggregateCenterPanel + chatArea + messageScroll + bubble 样式）——
    auto* chatPage = new QWidget(m_tabs);
    auto* chatOuter = new QVBoxLayout(chatPage);
    chatOuter->setContentsMargins(0, 0, 0, 0);
    chatOuter->setSpacing(0);

    auto* centerPanel = new QWidget(chatPage);
    centerPanel->setObjectName(QStringLiteral("aggregateCenterPanel"));
    auto* centerLay = new QVBoxLayout(centerPanel);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(0);

    auto* chatArea = new QWidget(centerPanel);
    chatArea->setObjectName(QStringLiteral("chatArea"));
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
    populateModelPresetCombo();
    headerLay->addWidget(m_modelPresetCombo, 0, Qt::AlignVCenter);
    chatAreaLay->addWidget(headerRow);

    m_scroll = new QScrollArea(chatArea);
    m_scroll->setObjectName(QStringLiteral("messageScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);

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
    m_clearBtn->setObjectName(QStringLiteral("sendButton"));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    inputBtnRow->addWidget(m_sendBtn);
    inputBtnRow->addWidget(m_clearBtn);
    inputAreaLay->addLayout(inputBtnRow);
    chatAreaLay->addWidget(inputArea);

    centerLay->addWidget(chatArea, 1);

    m_statusLabel = new QLabel(centerPanel);
    m_statusLabel->setObjectName(QStringLiteral("robotAssistantStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setContentsMargins(12, 4, 12, 8);
    centerLay->addWidget(m_statusLabel);

    chatOuter->addWidget(centerPanel, 1);
    m_tabs->addTab(chatPage, QStringLiteral("对话"));

    // —— 设置 Tab ——
    auto* settingsPage = new QWidget(m_tabs);
    settingsPage->setObjectName(QStringLiteral("robotAssistantSettingsPage"));
    auto* setLay = new QVBoxLayout(settingsPage);
    setLay->setContentsMargins(16, 16, 16, 16);
    setLay->setSpacing(12);

    auto addLabeledRow = [&](const QString& label, QLineEdit* edit) {
        auto* row = new QWidget(settingsPage);
        auto* v = new QVBoxLayout(row);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        auto* lab = new QLabel(label, row);
        lab->setObjectName(QStringLiteral("robotSettingsFieldLabel"));
        v->addWidget(lab);
        edit->setObjectName(QStringLiteral("robotSettingsField"));
        v->addWidget(edit);
        setLay->addWidget(row);
    };

    m_baseUrlEdit = new QLineEdit(settingsPage);
    m_baseUrlEdit->setPlaceholderText(QStringLiteral("https://api.deepseek.com"));
    addLabeledRow(QStringLiteral("API Base URL"), m_baseUrlEdit);

    m_modelEdit = new QLineEdit(settingsPage);
    m_modelEdit->setPlaceholderText(QStringLiteral("deepseek-chat"));
    addLabeledRow(QStringLiteral("模型名称"), m_modelEdit);

    m_apiKeyEdit = new QLineEdit(settingsPage);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(QStringLiteral("在 DeepSeek 开放平台创建的 API Key"));
    addLabeledRow(QStringLiteral("API Key"), m_apiKeyEdit);

    auto* settingsBtnRow = new QWidget(settingsPage);
    auto* settingsBtnLay = new QHBoxLayout(settingsBtnRow);
    settingsBtnLay->setContentsMargins(0, 0, 0, 0);
    settingsBtnLay->setSpacing(8);
    m_saveBtn = new QPushButton(QStringLiteral("保存设置"), settingsBtnRow);
    m_saveBtn->setObjectName(QStringLiteral("sendButton"));
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_testBtn = new QPushButton(QStringLiteral("测试连接"), settingsBtnRow);
    m_testBtn->setObjectName(QStringLiteral("simulateButton"));
    m_testBtn->setCursor(Qt::PointingHandCursor);
    settingsBtnLay->addWidget(m_saveBtn);
    settingsBtnLay->addWidget(m_testBtn);
    settingsBtnLay->addStretch(1);
    setLay->addWidget(settingsBtnRow);

    m_privacyLabel = new QLabel(
        QStringLiteral(
            "说明：为获得回答，您输入的内容会发送至所配置的 API 服务商（如 DeepSeek）。\n"
            "请勿将 API Key 告知他人或提交到公开代码库。\n"
            "本助手与微信、千牛等平台内的客户聊天无关，也不会自动向客户发送消息。"),
        settingsPage);
    m_privacyLabel->setWordWrap(true);
    m_privacyLabel->setObjectName(QStringLiteral("robotAssistantPrivacy"));
    setLay->addWidget(m_privacyLabel);
    setLay->addStretch(1);

    m_tabs->addTab(settingsPage, QStringLiteral("设置"));

    connect(m_saveBtn, &QPushButton::clicked, this, &RobotAssistantWidget::onSaveSettings);
    connect(m_testBtn, &QPushButton::clicked, this, &RobotAssistantWidget::onTestConnection);
    connect(m_sendBtn, &QPushButton::clicked, this, &RobotAssistantWidget::onSendMessage);
    connect(m_clearBtn, &QPushButton::clicked, this, &RobotAssistantWidget::onClearChat);

    connect(m_client, &OpenAiCompatClient::streamDelta, this, &RobotAssistantWidget::onClientDelta);
    connect(m_client, &OpenAiCompatClient::completed, this, &RobotAssistantWidget::onClientCompleted);
    connect(m_client, &OpenAiCompatClient::failed, this, &RobotAssistantWidget::onClientFailed);

    auto* sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_input);
    connect(sc, &QShortcut::activated, this, &RobotAssistantWidget::onSendMessage);

    m_theme = ApplyStyle::loadSavedMainWindowTheme();
    applyTheme(m_theme);
    loadSettings();
    loadSelfBubbleIdentity();
    rebindSessionFromCurrentConfig();

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 0)
            rebindSessionFromCurrentConfig();
    });

    connect(m_modelPresetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                rebindSessionFromCurrentConfig();
            });
}

void RobotAssistantWidget::refreshLocalUserProfile()
{
    loadSelfBubbleIdentity();
}

void RobotAssistantWidget::populateModelPresetCombo()
{
    if (!m_modelPresetCombo)
        return;
    m_modelPresetCombo->clear();
    auto add = [this](const QString& label, const QString& sessionKey, bool avail) {
        QVariantMap m;
        m.insert(QStringLiteral("sessionModelKey"), sessionKey);
        m.insert(QStringLiteral("available"), avail);
        const int idx = m_modelPresetCombo->count();
        m_modelPresetCombo->addItem(label);
        m_modelPresetCombo->setItemData(idx, m, Qt::UserRole);
    };
    add(QStringLiteral("DeepSeek"), QStringLiteral("deepseek:deepseek-chat"), true);
    add(QStringLiteral("通义千问（即将支持）"), QStringLiteral("qwen:placeholder"), false);
    add(QStringLiteral("豆包（即将支持）"), QStringLiteral("doubao:placeholder"), false);
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
    m_sendBtn->setEnabled(currentPresetAvailable() && !m_busy);
}

void RobotAssistantWidget::loadSelfBubbleIdentity()
{
    m_selfDisplayName = m_loginUsername;

    constexpr int kAvatarLogical = 36;
    const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;

    UserDao dao;
    const auto u = dao.findByUsername(m_loginUsername);
    if (u && !u->displayName.isEmpty())
        m_selfDisplayName = u->displayName;

    QPixmap pm;
    if (u && !u->avatarPath.isEmpty()) {
        const QString abs = UserDao::absolutePathFromProjectRelative(u->avatarPath);
        if (QFile::exists(abs)) {
            const QImage img(abs);
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
    m_theme = theme;
    setStyleSheet(robotAssistantQss(theme));
    applySendButtonPolicy();
}

void RobotAssistantWidget::loadSettings()
{
    QSettings s;
    m_baseUrlEdit->setText(
        s.value(QStringLiteral("ai/baseUrl"), QStringLiteral("https://api.deepseek.com")).toString());
    m_modelEdit->setText(s.value(QStringLiteral("ai/model"), QStringLiteral("deepseek-chat")).toString());
    m_apiKeyEdit->setText(s.value(QStringLiteral("ai/apiKey")).toString());
}

void RobotAssistantWidget::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("ai/baseUrl"), m_baseUrlEdit->text().trimmed());
    s.setValue(QStringLiteral("ai/model"), m_modelEdit->text().trimmed());
    s.setValue(QStringLiteral("ai/apiKey"), m_apiKeyEdit->text());
    m_statusLabel->setText(QStringLiteral("设置已保存。"));
}

void RobotAssistantWidget::onSaveSettings()
{
    saveSettings();
    rebindSessionFromCurrentConfig();
}

void RobotAssistantWidget::onTestConnection()
{
    const QString key = m_apiKeyEdit->text().trimmed();
    if (key.isEmpty()) {
        showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("测试连接"),
                            QStringLiteral("请先填写 API Key。"));
        return;
    }
    const QString url = OpenAiCompatClient::buildCompletionsUrl(m_baseUrlEdit->text().trimmed());
    const QString model = m_modelEdit->text().trimmed().isEmpty()
                              ? QStringLiteral("deepseek-chat")
                              : m_modelEdit->text().trimmed();

    QJsonArray msgs;
    msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                            {QStringLiteral("content"), QStringLiteral("ping")}});

    m_testBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("正在测试连接…"));

    // 独立 client，避免与对话请求的 completed/failed 串线
    auto* testClient = new OpenAiCompatClient(m_nam, this);
    const QPointer<RobotAssistantWidget> self(this);
    connect(testClient, &OpenAiCompatClient::completed, this, [this, self, testClient]() {
        testClient->deleteLater();
        if (!self)
            return;
        m_testBtn->setEnabled(true);
        m_statusLabel->setText(QStringLiteral("连接成功。"));
        showRobotMessageBox(QMessageBox::Information, self.get(), QStringLiteral("测试连接"),
                            QStringLiteral("连接成功。"));
    });
    connect(testClient, &OpenAiCompatClient::failed, this, [this, self, testClient](const QString& reason) {
        testClient->deleteLater();
        if (!self)
            return;
        m_testBtn->setEnabled(true);
        m_statusLabel->setText(QStringLiteral("连接失败：%1").arg(reason));
        showRobotMessageBox(QMessageBox::Warning, self.get(), QStringLiteral("测试连接"),
                            QStringLiteral("连接失败：\n%1").arg(reason));
    });
    testClient->requestChatCompletion(url, key, model, msgs, false);
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
    if (m_sessionId > 0) {
        AiAssistantDao dao;
        dao.clearMessages(m_sessionId);
    }
    m_history.clear();
    clearChatLayout();
    m_statusLabel->clear();
    if (!currentPresetAvailable())
        m_statusLabel->setText(QStringLiteral("该模型即将接入，请先用 DeepSeek。"));
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

QLabel* RobotAssistantWidget::appendAssistantBubble()
{
    auto* row = new QWidget(m_chatInner);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(8);

    auto* avatar = new QLabel(row);
    avatar->setObjectName(QStringLiteral("robotAssistantAvatarIn"));
    avatar->setFixedSize(36, 36);
    QPixmap ds = pixmapFromRasterResource(QStringLiteral(":/deepseek_icon.png"), 36);
    if (!ds.isNull())
        avatar->setPixmap(ds);
    rowLayout->addWidget(avatar, 0, Qt::AlignTop);

    auto* col = new QWidget(row);
    auto* colLayout = new QVBoxLayout(col);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(4);
    auto* meta = new QLabel(QStringLiteral("DeepSeek"), col);
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

QJsonArray RobotAssistantWidget::buildMessagesForRequest() const
{
    QJsonArray arr;
    arr.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                            {QStringLiteral("content"), defaultSystemPrompt()}});
    const QList<RobotChatTurn> tail = tailHistoryForApi(m_history);
    for (const RobotChatTurn& t : tail) {
        arr.append(
            QJsonObject{{QStringLiteral("role"), t.role}, {QStringLiteral("content"), t.content}});
    }
    return arr;
}

void RobotAssistantWidget::setBusy(bool busy)
{
    m_busy = busy;
    m_input->setReadOnly(busy);
    m_testBtn->setEnabled(!busy);
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
        m_statusLabel->setText(QStringLiteral("该模型即将接入，请先用 DeepSeek。"));
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
    m_statusLabel->clear();

    if (m_sessionId <= 0) {
        applySendButtonPolicy();
        return;
    }

    const QVector<AiAssistantChatTurn> rows = adao.listMessages(m_sessionId);
    for (const AiAssistantChatTurn& row : rows) {
        m_history.append({row.role, row.content});
        if (row.role == QLatin1String("user"))
            appendUserBubble(row.content);
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
                            QStringLiteral("该模型即将支持。请先选择 DeepSeek，并在「设置」中配置 API Key。"));
        return;
    }
    m_client->abortActive();
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty())
        return;

    const QString key = m_apiKeyEdit->text().trimmed();
    if (key.isEmpty()) {
        showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("发送"),
                            QStringLiteral("请先在「设置」中填写并保存 API Key。"));
        m_tabs->setCurrentIndex(1);
        return;
    }

    rebindSessionFromCurrentConfig();
    if (m_sessionId <= 0) {
        showRobotMessageBox(QMessageBox::Warning, this, QStringLiteral("发送"),
                            QStringLiteral("无法加载当前账号，请重新登录后再试。"));
        return;
    }

    m_input->clear();
    m_history.append({QStringLiteral("user"), text});
    appendUserBubble(text);
    AiAssistantDao().appendMessage(m_sessionId, QStringLiteral("user"), text);

    m_accumulatedAssistant.clear();
    m_streamingLabel = appendAssistantBubble();
    m_streamingLabel->clear();

    const QString url = OpenAiCompatClient::buildCompletionsUrl(m_baseUrlEdit->text().trimmed());
    const QString model = m_modelEdit->text().trimmed().isEmpty()
                              ? QStringLiteral("deepseek-chat")
                              : m_modelEdit->text().trimmed();

    setBusy(true);
    m_statusLabel->setText(QStringLiteral("正在生成…"));
    m_client->requestChatCompletion(url, key, model, buildMessagesForRequest(), true);
    scheduleScrollChatToBottom();
}

void RobotAssistantWidget::onClientDelta(const QString& delta)
{
    m_accumulatedAssistant += delta;
    if (m_streamingLabel)
        m_streamingLabel->setText(m_accumulatedAssistant);
    nudgeScrollAfterContentChange();
}

void RobotAssistantWidget::onClientCompleted()
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
    m_statusLabel->setText(QStringLiteral("完成。"));
}

void RobotAssistantWidget::onClientFailed(const QString& reason)
{
    setBusy(false);
    if (m_streamingLabel) {
        QString body = m_accumulatedAssistant;
        if (body.isEmpty())
            body = QStringLiteral("（未收到正文）");
        m_streamingLabel->setText(
            body + QStringLiteral("\n\n（后续内容未能生成：%1）").arg(reason));
        nudgeScrollAfterContentChange();
    } else {
        m_statusLabel->setText(QStringLiteral("错误：%1").arg(reason));
    }
    if (!m_accumulatedAssistant.isEmpty()) {
        if (m_sessionId > 0)
            AiAssistantDao().appendMessage(m_sessionId, QStringLiteral("assistant"),
                                           m_accumulatedAssistant);
        m_history.append({QStringLiteral("assistant"), m_accumulatedAssistant});
    }
    m_streamingLabel = nullptr;
    m_accumulatedAssistant.clear();
}
