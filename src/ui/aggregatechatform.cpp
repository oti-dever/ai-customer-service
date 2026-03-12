#include "aggregatechatform.h"
#include "../core/conversationmanager.h"
#include "../data/conversationdao.h"
#include "../services/platforms/simplatformadapter.h"
#include "../utils/applystyle.h"
#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
#include <QLabel>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

AggregateChatForm::AggregateChatForm(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("管理后台-对话接待"));
    setWindowIcon(QIcon(QStringLiteral(":/app_icon.svg")));
    setMinimumSize(1000, 640);
    resize(1200, 720);

    setupUI();
    setupStyles();
    connectSignals();
    refreshConversationList();
}

AggregateChatForm::~AggregateChatForm()
{
}

void AggregateChatForm::setupUI()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(buildCenterPanel());
    splitter->addWidget(buildRightPanel());
    splitter->setSizes({280, 520, 260});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    mainLayout->addWidget(splitter);
}

void AggregateChatForm::setupStyles()
{
    setStyleSheet(ApplyStyle::aggregateChatFormStyle());
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
                statusBar()->showMessage(QStringLiteral("发送失败: %1").arg(reason), 5000);
            });

    auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(shortcut, &QShortcut::activated, this, &AggregateChatForm::onSendClicked);
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

    // Header
    auto* header = new QWidget(panel);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 0, 0, 0);
    headerLayout->setSpacing(8);
    auto* logo = new QLabel(header);
    logo->setPixmap(qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(22, 22));
    auto* title = new QLabel(QStringLiteral("聚合对话接待"), header);
    title->setObjectName("aggregateLeftTitle");
    headerLayout->addWidget(logo);
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);
    layout->addWidget(header);

    // Mode row
    auto* modeRow = new QWidget(panel);
    auto* modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(8);
    auto* modeLabel = new QLabel(QStringLiteral("模式："), modeRow);
    modeLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    m_modeCombo = new QComboBox(modeRow);
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
    m_searchEdit = new QLineEdit(panel);
    m_searchEdit->setObjectName("aggregateSearch");
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索会话或客户名"));
    m_searchEdit->addAction(qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView),
                            QLineEdit::LeadingPosition);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { refreshConversationList(); });
    layout->addWidget(m_searchEdit);

    // Conversation list
    m_leftStack = new QStackedWidget(panel);
    m_conversationList = new QListWidget(panel);
    m_conversationList->setObjectName("aggregateConversationList");
    m_conversationList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_conversationList, &QListWidget::itemClicked,
            this, &AggregateChatForm::onConversationItemClicked);
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
    auto* closeBtn = new QPushButton(QStringLiteral("\u00d7"), rightHeader);
    closeBtn->setObjectName("aggregateRightHeaderClose");
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    headerRow->addWidget(rightTitle);
    headerRow->addStretch(1);
    headerRow->addWidget(closeBtn);
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

    detailLayout->addWidget(m_customerName);
    detailLayout->addWidget(m_customerPlatform);
    detailLayout->addWidget(m_customerStatus);
    detailLayout->addStretch();
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
                           ? "#52c41a" : "#1890ff";
    dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(dotColor));
    layout->addWidget(dot, 0, Qt::AlignTop | Qt::AlignLeft);

    // Text info
    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(3);

    auto* nameRow = new QHBoxLayout();
    auto* nameLabel = new QLabel(conv.customerName, widget);
    nameLabel->setObjectName("convItemName");
    nameLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #e8e8e8;");
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
    timeLabel->setStyleSheet("font-size: 11px; color: #888;");
    nameRow->addWidget(timeLabel);
    textCol->addLayout(nameRow);

    auto* msgRow = new QHBoxLayout();
    auto* msgLabel = new QLabel(conv.lastMessage.left(30), widget);
    msgLabel->setStyleSheet("font-size: 12px; color: #aaa;");
    msgLabel->setMaximumWidth(180);
    msgRow->addWidget(msgLabel);
    msgRow->addStretch(1);

    if (conv.unreadCount > 0) {
        auto* badge = new QLabel(QString::number(conv.unreadCount), widget);
        badge->setObjectName("unreadBadge");
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedSize(20, 20);
        badge->setStyleSheet(
            "background: #ff4d4f; color: white; font-size: 11px; "
            "font-weight: bold; border-radius: 10px;");
        msgRow->addWidget(badge);
    }
    textCol->addLayout(msgRow);

    layout->addLayout(textCol, 1);
    return widget;
}

// ===================== Message Bubble =====================

QWidget* AggregateChatForm::createBubble(const QString& text, const QString& sender,
                                         const QDateTime& time, bool isOutgoing)
{
    auto* row = new QWidget();
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(8);

    if (isOutgoing)
        rowLayout->addStretch(1);

    auto* bubble = new QFrame(row);
    bubble->setObjectName(isOutgoing ? "bubbleOut" : "bubbleIn");
    auto* bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 8, 12, 8);
    bubbleLayout->setSpacing(4);

    auto* contentLabel = new QLabel(text, bubble);
    contentLabel->setWordWrap(true);
    contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLabel->setObjectName(isOutgoing ? "bubbleTextOut" : "bubbleTextIn");
    bubbleLayout->addWidget(contentLabel);

    QString meta = time.isValid() ? time.toString("HH:mm") : QString();
    if (!sender.isEmpty() && !isOutgoing)
        meta = sender + "  " + meta;
    auto* metaLabel = new QLabel(meta, bubble);
    metaLabel->setObjectName("bubbleMeta");
    bubbleLayout->addWidget(metaLabel);

    bubble->setMaximumWidth(420);
    rowLayout->addWidget(bubble);

    if (!isOutgoing)
        rowLayout->addStretch(1);

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
}

void AggregateChatForm::showConversation(int conversationId)
{
    m_currentConvId = conversationId;
    auto& mgr = ConversationManager::instance();
    mgr.selectConversation(conversationId);

    // Clear existing bubbles
    while (m_messageLayout->count() > 1) {
        auto* item = m_messageLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    // Load messages
    auto messages = mgr.messages(conversationId);
    for (const auto& msg : messages) {
        appendMessageBubble(msg);
    }

    // Update header
    ConversationDao dao;
    auto conv = dao.findById(conversationId);
    if (conv) {
        m_chatHeader->setText(QStringLiteral("  %1  (%2)").arg(conv->customerName, conv->platform));
        updateCustomerInfo(*conv);
    }

    m_centerStack->setCurrentWidget(m_chatArea);
    m_inputEdit->setFocus();

    QTimer::singleShot(50, this, &AggregateChatForm::scrollToBottom);
}

void AggregateChatForm::appendMessageBubble(const MessageRecord& msg)
{
    bool isOut = (msg.direction == QLatin1String("out"));
    auto* bubble = createBubble(msg.content, msg.sender, msg.createdAt, isOut);
    int idx = m_messageLayout->count() - 1;
    m_messageLayout->insertWidget(idx, bubble);
}

void AggregateChatForm::scrollToBottom()
{
    auto* sb = m_messageScroll->verticalScrollBar();
    sb->setValue(sb->maximum());
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

void AggregateChatForm::showCenterEmptyState()
{
    m_centerStack->setCurrentWidget(m_centerEmptyState);
}

void AggregateChatForm::showRightEmptyState()
{
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
        QTimer::singleShot(50, this, &AggregateChatForm::scrollToBottom);
    }
    refreshConversationList();
    statusBar()->showMessage(
        QStringLiteral("新消息: %1").arg(msg.content.left(30)), 3000);
}

void AggregateChatForm::onSentOk(int conversationId, const MessageRecord& msg)
{
    if (conversationId == m_currentConvId) {
        appendMessageBubble(msg);
        QTimer::singleShot(50, this, &AggregateChatForm::scrollToBottom);
    }
    refreshConversationList();
}
