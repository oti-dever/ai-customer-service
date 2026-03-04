#include "aggregatechatform.h"
#include "../utils/applystyle.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QApplication>
#include <QStyle>
#include <QToolButton>
#include <QStackedWidget>

AggregateChatForm::AggregateChatForm(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("管理后台-对话接待"));
    setMinimumSize(1000, 640);
    resize(1200, 720);

    setupUI();
    setupStyles();
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
    m_leftPanel = buildLeftPanel();
    splitter->addWidget(m_leftPanel);
    m_centerPanel = buildCenterPanel();
    splitter->addWidget(m_centerPanel);
    m_rightPanel = buildRightPanel();
    splitter->addWidget(m_rightPanel);
    splitter->setSizes({ 280, 520, 260 });
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    mainLayout->addWidget(splitter);
}

void AggregateChatForm::setupStyles()
{
    setStyleSheet(ApplyStyle::aggregateChatFormStyle());
}

QWidget *AggregateChatForm::buildLeftPanel()
{
    auto* panel = new QWidget(this);
    panel->setObjectName("aggregateLeftPanel");
    panel->setMinimumWidth(260);
    panel->setMaximumWidth(360);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    auto* header = new QWidget(panel);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    auto* logo = new QLabel(header);
    logo->setPixmap(qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(24, 24));
    auto* title = new QLabel(QStringLiteral("聚合对话接待"), header);
    title->setObjectName("aggregateLeftTitle");
    headerLayout->addWidget(logo);
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);
    auto* homeBtn = new QToolButton(header);
    homeBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_DirHomeIcon));
    homeBtn->setToolTip(QStringLiteral("回到主界面"));
    headerLayout->addWidget(homeBtn);
    layout->addWidget(header);

    auto* modeRow = new QWidget(panel);
    auto* modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(8);
    auto* modeLabel = new QLabel(QStringLiteral("当前模式："), modeRow);
    m_modeCombo = new QComboBox(modeRow);
    m_modeCombo->addItem(QStringLiteral("人工休息"));
    m_modeCombo->addItem(QStringLiteral("人工接待"));
    m_modeCombo->addItem(QStringLiteral("机器人处理"));
    m_modeCombo->setMinimumWidth(100);
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_modeCombo);
    modeLayout->addStretch(1);
    layout->addWidget(modeRow);

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

    m_searchEdit = new QLineEdit(panel);
    m_searchEdit->setObjectName("aggregateSearch");
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索会话或客户名"));
    m_searchEdit->addAction(qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView), QLineEdit::LeadingPosition);
    layout->addWidget(m_searchEdit);

    auto* listStack = new QStackedWidget(panel);
    m_conversationList = new QListWidget(panel);
    m_conversationList->setObjectName("aggregateConversationList");
    listStack->addWidget(m_conversationList);

    auto* listEmpty = new QWidget(panel);
    listEmpty->setObjectName("aggregateListEmpty");
    auto* listEmptyLayout = new QVBoxLayout(listEmpty);
    listEmptyLayout->setAlignment(Qt::AlignCenter);
    listEmptyLayout->setSpacing(8);
    auto* emptyIcon = new QLabel(listEmpty);
    emptyIcon->setObjectName("aggregateListEmptyIcon");
    emptyIcon->setPixmap(qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(48, 48));
    emptyIcon->setStyleSheet("opacity: 0.5;");
    auto* emptyText = new QLabel(QStringLiteral("暂无会话"), listEmpty);
    emptyText->setObjectName("aggregateListEmptyText");
    listEmptyLayout->addWidget(emptyIcon);
    listEmptyLayout->addWidget(emptyText);
    listStack->addWidget(listEmpty);
    listStack->setCurrentWidget(listEmpty);
    layout->addWidget(listStack, 1);

    return panel;
}

QWidget *AggregateChatForm::buildCenterPanel()
{
    auto* panel = new QWidget(this);
    panel->setObjectName("aggregateCenterPanel");
    auto* stack = new QStackedWidget(panel);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack);
    m_centerEmptyState = new QWidget(panel);
    auto* emptyLayout = new QVBoxLayout(m_centerEmptyState);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(16);

    auto* iconWrap = new QFrame(m_centerEmptyState);
    iconWrap->setObjectName("aggregateEmptyIcon");
    iconWrap->setFixedSize(100, 100);
    auto* iconLayout = new QHBoxLayout(iconWrap);
    iconLayout->setAlignment(Qt::AlignCenter);
    auto* iconLabel = new QLabel(iconWrap);
    iconLabel->setPixmap(qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(48, 48));
    iconLayout->addWidget(iconLabel);

    auto* mainText = new QLabel(QStringLiteral("选择一个会话开始聊天"), m_centerEmptyState);
    mainText->setObjectName("aggregateEmptyMain");
    mainText->setAlignment(Qt::AlignHCenter);

    auto* subText = new QLabel(
        QStringLiteral("从左侧列表中选择会话，开始与客户对话。\n优先处理转人工请求，提供及时的客户服务。"),
        m_centerEmptyState);
    subText->setObjectName("aggregateEmptySub");
    subText->setAlignment(Qt::AlignHCenter);
    subText->setWordWrap(true);

    emptyLayout->addWidget(iconWrap);
    emptyLayout->addWidget(mainText);
    emptyLayout->addWidget(subText);

    stack->addWidget(m_centerEmptyState);

    m_chatArea = new QWidget(panel);
    auto* chatLayout = new QVBoxLayout(m_chatArea);
    chatLayout->setContentsMargins(16, 16, 16, 16);
    m_inputEdit = new QPlainTextEdit(m_chatArea);
    m_inputEdit->setPlaceholderText(QStringLiteral("输入消息，Ctrl+Enter 发送"));
    m_btnSend = new QPushButton(QStringLiteral("发送"), m_chatArea);
    chatLayout->addStretch(1);
    chatLayout->addWidget(m_inputEdit);
    chatLayout->addWidget(m_btnSend);
    stack->addWidget(m_chatArea);

    stack->setCurrentWidget(m_centerEmptyState);
    return panel;
}

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
    rightHeader->setFixedHeight(70);
    auto* headerLayout = new QVBoxLayout(rightHeader);
    headerLayout->setContentsMargins(16, 12, 16, 12);
    headerLayout->setSpacing(4);
    auto* headerRow = new QHBoxLayout();
    auto* rightTitle = new QLabel(QStringLiteral("聚合对话接待"), rightHeader);
    rightTitle->setObjectName("aggregateRightHeaderTitle");
    auto* closeBtn = new QPushButton(QStringLiteral("×"), rightHeader);
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

    auto* stack = new QStackedWidget(panel);
    auto* stackWrap = new QWidget(panel);
    auto* stackLayout = new QVBoxLayout(stackWrap);
    stackLayout->setContentsMargins(16, 16, 16, 16);
    stackLayout->addWidget(stack);
    layout->addWidget(stackWrap, 1);

    m_rightEmptyState = new QWidget(panel);
    auto* emptyLayout = new QVBoxLayout(m_rightEmptyState);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(12);

    auto* iconWrap = new QFrame(m_rightEmptyState);
    iconWrap->setObjectName("aggregateRightEmptyIcon");
    iconWrap->setFixedSize(72, 72);
    auto* iconLayout = new QHBoxLayout(iconWrap);
    iconLayout->setAlignment(Qt::AlignCenter);
    auto* iconLabel = new QLabel(iconWrap);
    iconLabel->setPixmap(qApp->style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(36, 36));
    iconLayout->addWidget(iconLabel);

    auto* mainText = new QLabel(QStringLiteral("客户信息"), m_rightEmptyState);
    mainText->setObjectName("aggregateRightEmptyMain");
    auto* subText = new QLabel(QStringLiteral("选择会话查看详细信息"), m_rightEmptyState);
    subText->setObjectName("aggregateRightEmptySub");
    subText->setAlignment(Qt::AlignHCenter);

    emptyLayout->addWidget(iconWrap);
    emptyLayout->addWidget(mainText);
    emptyLayout->addWidget(subText);

    stack->addWidget(m_rightEmptyState);

    m_customerDetail = new QWidget(panel);
    auto* detailLayout = new QVBoxLayout(m_customerDetail);
    auto* detailLabel = new QLabel(QStringLiteral("客户详情（占位）"), m_customerDetail);
    detailLayout->addWidget(detailLabel);
    stack->addWidget(m_customerDetail);

    stack->setCurrentWidget(m_rightEmptyState);

    return panel;
}

void AggregateChatForm::onTabPendingClicked()
{
    m_btnPending->setChecked(true);
    m_btnAll->setChecked(false);
    m_currentTabIsPending = true;
}

void AggregateChatForm::onTabAllClicked()
{
    m_btnPending->setChecked(false);
    m_btnAll->setChecked(true);
    m_currentTabIsPending = false;
}

void AggregateChatForm::onConversationItemClicked(int row)
{
    Q_UNUSED(row);

}

void AggregateChatForm::showConversationEmptyState()
{

}

void AggregateChatForm::showCustomerEmptyState()
{

}
