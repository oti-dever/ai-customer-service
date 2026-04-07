#include "aggregatechatform.h"
#include "foldarrowcombobox.h"
#include "../core/conversationmanager.h"
#include "../data/conversationdao.h"
#include "../data/messagedao.h"
#include "../data/messagesendeventdao.h"
#include "../data/userdao.h"
#include "../services/platforms/simplatformadapter.h"
#include "../utils/applystyle.h"
#include <algorithm>
#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QAction>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString phaseDisplayName(const QString& phase)
{
    if (phase == QLatin1String("dequeued"))
        return QStringLiteral("已取出待发");
    if (phase == QLatin1String("lock_acquired"))
        return QStringLiteral("已获得窗口锁");
    if (phase == QLatin1String("lock_timeout"))
        return QStringLiteral("窗口锁超时");
    if (phase == QLatin1String("switch_chat"))
        return QStringLiteral("切换会话");
    if (phase == QLatin1String("send_text"))
        return QStringLiteral("输入并发送");
    if (phase == QLatin1String("receipt_check"))
        return QStringLiteral("回执校验");
    if (phase == QLatin1String("receipt_result"))
        return QStringLiteral("回执结果");
    if (phase == QLatin1String("send_attempt"))
        return QStringLiteral("发送尝试");
    if (phase == QLatin1String("success"))
        return QStringLiteral("成功");
    if (phase == QLatin1String("failed"))
        return QStringLiteral("失败");
    return phase;
}

QString formatSendEventLine(const MessageSendEventRecord& e)
{
    const QString t = e.createdAt.isValid()
                          ? e.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                          : QStringLiteral("-");
    QString detail = e.detail.trimmed();
    if (detail.size() > 200)
        detail = detail.left(197) + QStringLiteral("...");
    return QStringLiteral("[%1] %2 消息#%3 %4")
        .arg(t, phaseDisplayName(e.phase))
        .arg(e.messageId)
        .arg(detail.isEmpty() ? QStringLiteral("-") : detail);
}

/** 与父级 AggregateChatForm 样式隔离，避免 QLabel 继承深色字压在系统深色弹窗背景上。 */
QString aggregateMessageBoxContrastStyle()
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

} // namespace

AggregateChatForm::AggregateChatForm(const QString& loginUsername, QWidget* parent)
    : QWidget(parent)
    , m_loginUsername(loginUsername)
{
    setupUI();
    setupStyles();
    loadSelfBubbleIdentity();
    connectSignals();
    refreshConversationList();
}

void AggregateChatForm::loadSelfBubbleIdentity()
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
    m_selfAvatarPixmap = roundedAggregateAvatarPixmap(pm, kAvatarLogical, dpr, kAvatarLogical / 2);
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
}

void AggregateChatForm::setupUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(buildCenterPanel());
    splitter->addWidget(buildRightPanel());
    splitter->setSizes({280, 520, 260});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    outerLayout->addWidget(splitter, 1);

    m_statusLabel = nullptr;
    m_messageRefreshTimer = new QTimer(this);
    m_messageRefreshTimer->setInterval(800);
    m_sendTimelineTimer = new QTimer(this);
    m_sendTimelineTimer->setInterval(900);
}

void AggregateChatForm::setupStyles()
{
    setStyleSheet(ApplyStyle::aggregateChatFormStyle(ApplyStyle::loadSavedMainWindowTheme()));
}

void AggregateChatForm::applyTheme(ApplyStyle::MainWindowTheme theme)
{
    setStyleSheet(ApplyStyle::aggregateChatFormStyle(theme));
}

void AggregateChatForm::connectSignals()
{
    auto& mgr = ConversationManager::instance();

    connect(&mgr, &ConversationManager::conversationListChanged,
            this, &AggregateChatForm::onConversationListChanged);
    connect(&mgr, &ConversationManager::newMessageReceived,
            this, &AggregateChatForm::onNewMessage);
    connect(&mgr, &ConversationManager::messageSentOk,
            this, &AggregateChatForm::onSentOk);
    connect(&mgr, &ConversationManager::messageSendFailed,
            this, [this](int convId, const QString& reason) {
                Q_UNUSED(convId)
                showStatusMessage(QStringLiteral("发送失败: %1").arg(reason), 5000);
            });

    auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(shortcut, &QShortcut::activated, this, &AggregateChatForm::onSendClicked);
    connect(m_messageRefreshTimer, &QTimer::timeout,
            this, &AggregateChatForm::refreshVisibleConversationMessages);
    m_messageRefreshTimer->start();
    connect(m_btnClearSendTimeline, &QPushButton::clicked,
            this, &AggregateChatForm::onClearSendTimeline);
    connect(m_sendTimelineTimer, &QTimer::timeout,
            this, &AggregateChatForm::pollSendTimeline);
    m_sendTimelineTimer->start();
}

// ===================== Left Panel =====================

QWidget* AggregateChatForm::buildLeftPanel()
{
    auto* panel = new QWidget(this);
    panel->setObjectName("aggregateLeftPanel");
    panel->setMinimumWidth(260);
    panel->setMaximumWidth(360);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    // Mode row
    auto* modeRow = new QWidget(panel);
    auto* modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(8);
    auto* modeLabel = new QLabel(QStringLiteral("模式："), modeRow);
    modeLabel->setObjectName(QStringLiteral("aggregateModeLabel"));
    m_modeCombo = new FoldArrowComboBox(modeRow);
    m_modeCombo->addItem(QStringLiteral("人工接待"));
    m_modeCombo->addItem(QStringLiteral("人工休息"));
    m_modeCombo->setMinimumWidth(90);
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_modeCombo);
    modeLayout->addStretch(1);

    m_btnSimulate = new QPushButton(QStringLiteral("模拟消息"), modeRow);
    m_btnSimulate->setObjectName("simulateButton");
    m_btnSimulate->setCursor(Qt::PointingHandCursor);
    m_btnSimulate->setToolTip(QStringLiteral("模拟收到一条买家消息（测试用）"));
    connect(m_btnSimulate, &QPushButton::clicked, this, &AggregateChatForm::onSimulateClicked);
    modeLayout->addWidget(m_btnSimulate);
    layout->addWidget(modeRow);

    // Tabs
    auto* tabRow = new QWidget(panel);
    auto* tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(8);
    m_btnPending = new QPushButton(QStringLiteral("待处理"), tabRow);
    m_btnPending->setObjectName("aggregateTabPending");
    m_btnPending->setCheckable(true);
    m_btnPending->setChecked(true);
    m_btnPending->setCursor(Qt::PointingHandCursor);
    m_btnAll = new QPushButton(QStringLiteral("全部会话"), tabRow);
    m_btnAll->setObjectName("aggregateTabAll");
    m_btnAll->setCheckable(true);
    m_btnAll->setChecked(false);
    m_btnAll->setCursor(Qt::PointingHandCursor);
    tabLayout->addWidget(m_btnPending);
    tabLayout->addWidget(m_btnAll);
    tabLayout->addStretch(1);
    layout->addWidget(tabRow);
    connect(m_btnPending, &QPushButton::clicked, this, &AggregateChatForm::onTabPendingClicked);
    connect(m_btnAll, &QPushButton::clicked, this, &AggregateChatForm::onTabAllClicked);

    // Search
    auto* searchRow = new QWidget(panel);
    auto* searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(6);
    auto* searchIconLabel = new QLabel(searchRow);
    searchIconLabel->setFixedSize(20, 20);
    searchIconLabel->setAlignment(Qt::AlignCenter);
    searchIconLabel->setPixmap(QIcon(QStringLiteral(":/aggregate_reception_icons/search_icon.svg")).pixmap(16, 16));
    m_searchEdit = new QLineEdit(searchRow);
    m_searchEdit->setObjectName("aggregateSearch");
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索会话或客户名"));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { refreshConversationList(); });
    searchLayout->addWidget(searchIconLabel);
    searchLayout->addWidget(m_searchEdit, 1);
    layout->addWidget(searchRow);

    // Conversation list
    m_leftStack = new QStackedWidget(panel);
    m_conversationList = new QListWidget(panel);
    m_conversationList->setObjectName("aggregateConversationList");
    m_conversationList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_conversationList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_conversationList, &QListWidget::itemClicked,
            this, &AggregateChatForm::onConversationItemClicked);
    connect(m_conversationList, &QListWidget::customContextMenuRequested,
            this, &AggregateChatForm::onConversationListContextMenu);
    m_leftStack->addWidget(m_conversationList);

    auto* listEmpty = new QWidget(panel);
    listEmpty->setObjectName("aggregateListEmpty");
    auto* listEmptyLayout = new QVBoxLayout(listEmpty);
    listEmptyLayout->setAlignment(Qt::AlignCenter);
    listEmptyLayout->setSpacing(8);
    auto* emptyIcon = new QLabel(listEmpty);
    emptyIcon->setPixmap(qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(40, 40));
    emptyIcon->setAlignment(Qt::AlignCenter);
    auto* emptyText = new QLabel(QStringLiteral("暂无会话\n点击「模拟消息」开始测试"), listEmpty);
    emptyText->setObjectName("aggregateListEmptyText");
    emptyText->setAlignment(Qt::AlignCenter);
    listEmptyLayout->addWidget(emptyIcon);
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
    panel->setObjectName("aggregateCenterPanel");
    m_centerStack = new QStackedWidget(panel);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_centerStack);

    // Empty state
    m_centerEmptyState = new QWidget(panel);
    auto* emptyLayout = new QVBoxLayout(m_centerEmptyState);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(16);
    auto* iconWrap = new QFrame(m_centerEmptyState);
    iconWrap->setObjectName("aggregateEmptyIcon");
    iconWrap->setFixedSize(80, 80);
    auto* iconLayout = new QHBoxLayout(iconWrap);
    iconLayout->setAlignment(Qt::AlignCenter);
    auto* iconLabel = new QLabel(iconWrap);
    iconLabel->setPixmap(qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(40, 40));
    iconLayout->addWidget(iconLabel);
    auto* mainText = new QLabel(QStringLiteral("选择一个会话开始聊天"), m_centerEmptyState);
    mainText->setObjectName("aggregateEmptyMain");
    mainText->setAlignment(Qt::AlignHCenter);
    auto* subText = new QLabel(
        QStringLiteral("从左侧列表中选择会话，或点击「模拟消息」生成测试会话"),
        m_centerEmptyState);
    subText->setObjectName("aggregateEmptySub");
    subText->setAlignment(Qt::AlignHCenter);
    subText->setWordWrap(true);
    emptyLayout->addWidget(iconWrap, 0, Qt::AlignCenter);
    emptyLayout->addWidget(mainText);
    emptyLayout->addWidget(subText);
    m_centerStack->addWidget(m_centerEmptyState);

    // Chat area
    m_chatArea = new QWidget(panel);
    m_chatArea->setObjectName("chatArea");
    auto* chatLayout = new QVBoxLayout(m_chatArea);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    // Chat header
    m_chatHeader = new QLabel(m_chatArea);
    m_chatHeader->setObjectName("chatHeader");
    m_chatHeader->setFixedHeight(48);
    m_chatHeader->setAlignment(Qt::AlignVCenter);
    m_chatHeader->setContentsMargins(16, 0, 16, 0);
    chatLayout->addWidget(m_chatHeader);

    // Message scroll area
    m_messageScroll = new QScrollArea(m_chatArea);
    m_messageScroll->setObjectName("messageScroll");
    m_messageScroll->setWidgetResizable(true);
    m_messageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageContainer = new QWidget();
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(16, 12, 16, 12);
    m_messageLayout->setSpacing(8);
    m_messageLayout->addStretch(1);
    m_messageScroll->setWidget(m_messageContainer);
    chatLayout->addWidget(m_messageScroll, 1);

    // Divider
    auto* divider = new QFrame(m_chatArea);
    divider->setFrameShape(QFrame::HLine);
    divider->setObjectName("inputDivider");
    chatLayout->addWidget(divider);

    // Input area
    auto* inputArea = new QWidget(m_chatArea);
    auto* inputLayout = new QVBoxLayout(inputArea);
    inputLayout->setContentsMargins(16, 8, 16, 12);
    inputLayout->setSpacing(8);
    m_inputEdit = new QPlainTextEdit(inputArea);
    m_inputEdit->setObjectName("messageInput");
    m_inputEdit->setPlaceholderText(QStringLiteral("输入消息，Ctrl+Enter 发送"));
    m_inputEdit->setMaximumHeight(100);
    inputLayout->addWidget(m_inputEdit);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_btnSend = new QPushButton(QStringLiteral("发送"), inputArea);
    m_btnSend->setObjectName("sendButton");
    m_btnSend->setCursor(Qt::PointingHandCursor);
    m_btnSend->setFixedWidth(80);
    connect(m_btnSend, &QPushButton::clicked, this, &AggregateChatForm::onSendClicked);
    btnRow->addWidget(m_btnSend);
    inputLayout->addLayout(btnRow);
    chatLayout->addWidget(inputArea);

    m_centerStack->addWidget(m_chatArea);
    m_centerStack->setCurrentWidget(m_centerEmptyState);

    return panel;
}

// ===================== Right Panel =====================

QWidget* AggregateChatForm::buildRightPanel()
{
    auto* panel = new QWidget(this);
    panel->setObjectName("aggregateRightPanel");
    panel->setMinimumWidth(220);
    panel->setMaximumWidth(320);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* rightHeader = new QWidget(panel);
    rightHeader->setObjectName("aggregateRightHeader");
    rightHeader->setFixedHeight(60);
    auto* headerLayout = new QVBoxLayout(rightHeader);
    headerLayout->setContentsMargins(16, 10, 16, 10);
    headerLayout->setSpacing(2);
    auto* headerRow = new QHBoxLayout();
    auto* rightTitle = new QLabel(QStringLiteral("客户信息"), rightHeader);
    rightTitle->setObjectName("aggregateRightHeaderTitle");
    headerRow->addWidget(rightTitle);
    headerRow->addStretch(1);
    auto* rightSubtitle = new QLabel(QStringLiteral("统一客户服务平台"), rightHeader);
    rightSubtitle->setObjectName("aggregateRightHeaderSub");
    headerLayout->addLayout(headerRow);
    headerLayout->addWidget(rightSubtitle);
    layout->addWidget(rightHeader);

    m_rightStack = new QStackedWidget(panel);
    auto* stackWrap = new QWidget(panel);
    auto* stackLayout = new QVBoxLayout(stackWrap);
    stackLayout->setContentsMargins(16, 16, 16, 16);
    stackLayout->addWidget(m_rightStack);
    layout->addWidget(stackWrap, 1);

    // Empty state
    m_rightEmptyState = new QWidget(panel);
    auto* emptyLayout = new QVBoxLayout(m_rightEmptyState);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(12);
    auto* iconWrap = new QFrame(m_rightEmptyState);
    iconWrap->setObjectName("aggregateRightEmptyIcon");
    iconWrap->setFixedSize(64, 64);
    auto* iconLayout = new QHBoxLayout(iconWrap);
    iconLayout->setAlignment(Qt::AlignCenter);
    auto* iconLabel = new QLabel(iconWrap);
    iconLabel->setPixmap(qApp->style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(32, 32));
    iconLayout->addWidget(iconLabel);
    auto* mainText = new QLabel(QStringLiteral("客户信息"), m_rightEmptyState);
    mainText->setObjectName("aggregateRightEmptyMain");
    mainText->setAlignment(Qt::AlignCenter);
    auto* subText = new QLabel(QStringLiteral("选择会话查看详细信息"), m_rightEmptyState);
    subText->setObjectName("aggregateRightEmptySub");
    subText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(iconWrap, 0, Qt::AlignCenter);
    emptyLayout->addWidget(mainText);
    emptyLayout->addWidget(subText);
    m_rightStack->addWidget(m_rightEmptyState);

    // Customer detail
    m_customerDetail = new QWidget(panel);
    auto* detailLayout = new QVBoxLayout(m_customerDetail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(12);
    detailLayout->setAlignment(Qt::AlignTop);

    m_customerName = new QLabel(m_customerDetail);
    m_customerName->setObjectName("customerName");
    m_customerPlatform = new QLabel(m_customerDetail);
    m_customerPlatform->setObjectName("customerPlatform");
    m_customerStatus = new QLabel(m_customerDetail);
    m_customerStatus->setObjectName("customerStatus");

    m_sendTimelineLabel = new QLabel(QStringLiteral("发送状态"), m_customerDetail);
    m_sendTimelineLabel->setObjectName(QStringLiteral("sendTimelineTitle"));
    m_sendTimeline = new QPlainTextEdit(m_customerDetail);
    m_sendTimeline->setObjectName(QStringLiteral("sendStatusTimeline"));
    m_sendTimeline->setReadOnly(true);
    m_sendTimeline->setMinimumHeight(120);
    m_sendTimeline->setPlaceholderText(
        QStringLiteral("出站发送阶段将显示在此（仅记录打开本会话之后产生的新事件）"));
    m_btnClearSendTimeline = new QPushButton(QStringLiteral("清空显示"), m_customerDetail);
    m_btnClearSendTimeline->setObjectName(QStringLiteral("sendTimelineClearBtn"));

    detailLayout->addWidget(m_customerName);
    detailLayout->addWidget(m_customerPlatform);
    detailLayout->addWidget(m_customerStatus);
    detailLayout->addWidget(m_sendTimelineLabel);
    detailLayout->addWidget(m_sendTimeline, 1);
    detailLayout->addWidget(m_btnClearSendTimeline);
    m_rightStack->addWidget(m_customerDetail);

    m_rightStack->setCurrentWidget(m_rightEmptyState);
    return panel;
}

// ===================== Conversation Item =====================

QWidget* AggregateChatForm::createConversationItem(const ConversationInfo& conv)
{
    auto* widget = new QWidget();
    widget->setObjectName("convItemWidget");
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    // Platform dot
    auto* dot = new QLabel(widget);
    dot->setFixedSize(10, 10);
    QString dotColor = (conv.platform == QLatin1String("simulator"))
                           ? "#5aaf68" : "#7eb8e8";
    dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(dotColor));
    layout->addWidget(dot, 0, Qt::AlignTop | Qt::AlignLeft);

    // Text info
    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(3);

    auto* nameRow = new QHBoxLayout();
    auto* nameLabel = new QLabel(conv.customerName, widget);
    nameLabel->setObjectName("convItemName");
    nameLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #000000;");
    nameRow->addWidget(nameLabel);
    nameRow->addStretch(1);

    QString timeStr;
    if (conv.lastTime.isValid()) {
        if (conv.lastTime.date() == QDate::currentDate())
            timeStr = conv.lastTime.toString("HH:mm");
        else
            timeStr = conv.lastTime.toString("MM-dd HH:mm");
    }
    auto* timeLabel = new QLabel(timeStr, widget);
    timeLabel->setStyleSheet("font-size: 11px; color: #000000;");
    nameRow->addWidget(timeLabel);
    textCol->addLayout(nameRow);

    auto* msgRow = new QHBoxLayout();
    auto* msgLabel = new QLabel(conv.lastMessage.left(30), widget);
    msgLabel->setStyleSheet("font-size: 12px; color: #000000;");
    msgLabel->setMaximumWidth(180);
    msgRow->addWidget(msgLabel);
    msgRow->addStretch(1);

    if (conv.unreadCount > 0) {
        auto* badge = new QLabel(QString::number(conv.unreadCount), widget);
        badge->setObjectName("unreadBadge");
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedSize(20, 20);
        badge->setStyleSheet(
            "background: #e8736b; color: white; font-size: 11px; "
            "font-weight: bold; border-radius: 10px;");
        msgRow->addWidget(badge);
    }
    textCol->addLayout(msgRow);

    layout->addLayout(textCol, 1);
    return widget;
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

    // 对方消息：仅在存在 senderName / originalTimestamp 时展示元信息。
    // 微信 RPA：originalTimestamp 为入库/解析时刻；千牛常为 OCR 名称与时间。
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
    bubbleLayout->setSpacing(0);

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

void AggregateChatForm::refreshConversationList()
{
    auto& mgr = ConversationManager::instance();
    auto convs = mgr.conversations(m_currentTabIsPending);

    QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();

    m_conversationList->clear();
    int count = 0;
    for (const auto& conv : convs) {
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
}

void AggregateChatForm::showConversation(int conversationId)
{
    m_currentConvId = conversationId;
    auto& mgr = ConversationManager::instance();
    mgr.selectConversation(conversationId);

    auto messages = mgr.messages(conversationId);
    renderConversationMessages(messages);
    m_currentMessageSignature = buildMessageSignature(messages);

    // Update header
    ConversationDao dao;
    auto conv = dao.findById(conversationId);
    if (conv) {
        m_chatHeader->setText(QStringLiteral("  %1  (%2)").arg(conv->customerName, conv->platform));
        updateCustomerInfo(*conv);
        resetSendTimelineForConversation();
    }

    m_centerStack->setCurrentWidget(m_chatArea);
    m_inputEdit->setFocus();

    scheduleScrollChatToBottom();
}

void AggregateChatForm::renderConversationMessages(const QVector<MessageRecord>& messages)
{
    while (m_messageLayout->count() > 1) {
        auto* item = m_messageLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    m_lastBubbleDate = QDate();
    for (const auto& msg : messages)
        appendMessageBubble(msg);
}

void AggregateChatForm::appendMessageBubble(const MessageRecord& msg)
{
    QDate msgDate = msg.createdAt.isValid() ? msg.createdAt.date() : QDate::currentDate();
    if (!m_lastBubbleDate.isValid() || msgDate != m_lastBubbleDate) {
        int sepIdx = m_messageLayout->count() - 1;
        m_messageLayout->insertWidget(sepIdx, createDateSeparator(msgDate));
        m_lastBubbleDate = msgDate;
    }

    auto* bubble = createBubble(msg);
    int idx = m_messageLayout->count() - 1;
    m_messageLayout->insertWidget(idx, bubble);
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
    if (m_currentConvId <= 0 || !m_messageLayout || !m_messageScroll)
        return;

    auto messages = ConversationManager::instance().messages(m_currentConvId);
    const QString newSignature = buildMessageSignature(messages);
    if (newSignature == m_currentMessageSignature)
        return;

    auto* sb = m_messageScroll->verticalScrollBar();
    const bool wasNearBottom = !sb || sb->value() >= sb->maximum() - 24;
    renderConversationMessages(messages);
    m_currentMessageSignature = newSignature;
    if (wasNearBottom)
        scheduleScrollChatToBottom();
}

void AggregateChatForm::scrollToBottom()
{
    auto* sb = m_messageScroll->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AggregateChatForm::scheduleScrollChatToBottom()
{
    QTimer::singleShot(100, this, [this]() {
        if (!m_messageScroll || !m_messageContainer)
            return;
        m_messageContainer->updateGeometry();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        scrollToBottom();
        // QScrollArea 内容高度偶发晚一帧才更新，再拉一次
        QTimer::singleShot(0, this, [this]() { scrollToBottom(); });
    });
}

void AggregateChatForm::updateCustomerInfo(const ConversationInfo& conv)
{
    m_customerName->setText(QStringLiteral("客户: %1").arg(conv.customerName));
    m_customerPlatform->setText(QStringLiteral("平台: %1").arg(conv.platform));
    QString statusText = (conv.status == QLatin1String("open"))
                             ? QStringLiteral("状态: 进行中") : QStringLiteral("状态: 已关闭");
    m_customerStatus->setText(statusText);
    m_rightStack->setCurrentWidget(m_customerDetail);
}

void AggregateChatForm::resetSendTimelineForConversation()
{
    if (m_sendTimeline)
        m_sendTimeline->clear();
    MessageSendEventDao dao;
    m_sendTimelineBaselineId = dao.globalMaxId();
}

void AggregateChatForm::pollSendTimeline()
{
    if (m_currentConvId <= 0 || !m_sendTimeline)
        return;

    MessageSendEventDao dao;
    const QVector<MessageSendEventRecord> rows =
        dao.listSince(m_currentConvId, m_sendTimelineBaselineId);
    if (rows.isEmpty())
        return;

    for (const MessageSendEventRecord& e : rows) {
        m_sendTimeline->appendPlainText(formatSendEventLine(e));
        m_sendTimelineBaselineId = std::max(m_sendTimelineBaselineId, e.id);
    }
}

void AggregateChatForm::onClearSendTimeline()
{
    if (m_sendTimeline)
        m_sendTimeline->clear();
    MessageSendEventDao dao;
    m_sendTimelineBaselineId = dao.globalMaxId();
}

void AggregateChatForm::showCenterEmptyState()
{
    m_centerStack->setCurrentWidget(m_centerEmptyState);
}

void AggregateChatForm::showRightEmptyState()
{
    if (m_sendTimeline)
        m_sendTimeline->clear();
    m_sendTimelineBaselineId = 0;
    m_rightStack->setCurrentWidget(m_rightEmptyState);
}

// ===================== Slots =====================

void AggregateChatForm::onTabPendingClicked()
{
    m_btnPending->setChecked(true);
    m_btnAll->setChecked(false);
    m_currentTabIsPending = true;
    refreshConversationList();
}

void AggregateChatForm::onTabAllClicked()
{
    m_btnPending->setChecked(false);
    m_btnAll->setChecked(true);
    m_currentTabIsPending = false;
    refreshConversationList();
}

void AggregateChatForm::onConversationItemClicked(QListWidgetItem* item)
{
    if (!item) return;
    int convId = item->data(Qt::UserRole).toInt();
    qDebug() << "点击会话:" << convId;
    showConversation(convId);
    refreshConversationList();
}

void AggregateChatForm::onConversationListContextMenu(const QPoint& pos)
{
    if (!m_conversationList)
        return;
    QListWidgetItem* item = m_conversationList->itemAt(pos);
    if (!item)
        return;
    const int convId = item->data(Qt::UserRole).toInt();
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

        MessageDao msgDao;
        if (!msgDao.clearAllForConversation(convId)) {
            QMessageBox warnBox(this);
            warnBox.setIcon(QMessageBox::Warning);
            warnBox.setWindowTitle(QStringLiteral("错误"));
            warnBox.setText(QStringLiteral("清空失败，请查看日志或确认数据库已升级。"));
            warnBox.setStandardButtons(QMessageBox::Ok);
            warnBox.setStyleSheet(aggregateMessageBoxContrastStyle());
            warnBox.exec();
            return;
        }

        ConversationDao convDao;
        convDao.updateLastMessage(convId, QString(), QDateTime::currentDateTime());

        if (m_currentConvId == convId) {
            m_lastBubbleDate = QDate();
            renderConversationMessages({});
            m_currentMessageSignature = buildMessageSignature({});
            resetSendTimelineForConversation();
        }

        refreshConversationList();
        showStatusMessage(QStringLiteral("已清空聊天记录"), 3000);
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

        if (m_currentConvId == convId) {
            m_currentConvId = -1;
            m_lastBubbleDate = QDate();
            renderConversationMessages({});
            m_currentMessageSignature = buildMessageSignature({});
            showCenterEmptyState();
            showRightEmptyState();
            if (m_inputEdit)
                m_inputEdit->clear();
        }

        ConversationManager::instance().deleteConversation(convId);
        showStatusMessage(QStringLiteral("已删除会话"), 3000);
    }
}

void AggregateChatForm::onSendClicked()
{
    if (m_currentConvId <= 0) return;
    QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    m_inputEdit->clear();
    ConversationManager::instance().sendMessage(m_currentConvId, text);
}

void AggregateChatForm::onSimulateClicked()
{
    auto* sim = ConversationManager::instance().simulator();
    if (!sim) return;

    sim->simulateIncomingMessage(QString(), QString());
}

void AggregateChatForm::onConversationListChanged()
{
    refreshConversationList();
}

void AggregateChatForm::onNewMessage(int conversationId, const MessageRecord& msg)
{
    if (conversationId == m_currentConvId) {
        appendMessageBubble(msg);
        scheduleScrollChatToBottom();
    }
    refreshConversationList();
    showStatusMessage(QStringLiteral("新消息: %1").arg(msg.content.left(30)), 3000);
}

void AggregateChatForm::onSentOk(int conversationId, const MessageRecord& msg)
{
    if (conversationId == m_currentConvId) {
        appendMessageBubble(msg);
        scheduleScrollChatToBottom();
    }
    refreshConversationList();
}

void AggregateChatForm::showStatusMessage(const QString& text, int timeoutMs)
{
    if (!m_statusLabel) return;
    m_statusLabel->setText(text);
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this, text]() {
            if (m_statusLabel && m_statusLabel->text() == text)
                m_statusLabel->clear();
        });
    }
}
