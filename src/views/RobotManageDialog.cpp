#include "RobotManageDialog.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QScrollArea>
#include <QScreen>
#include <QStyle>
#include <QListWidgetItem>
#include <QFont>
#include <QColor>
#include <QStackedWidget>
#include <QToolButton>
#include <QDate>
#include <QPainter>
#include <QPaintEvent>
#include <QProgressBar>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QTreeWidget>
#include <QSplitter>
#include <QIcon>
#include <QPalette>
#include <QTextEdit>
#include <QCheckBox>
#include <QScrollBar>
#include <QTableWidget>
#include <QHeaderView>
#include "utils/ApplyStyle.h"

namespace {

/** 环形进度条（用于系统概览-系统状态），仅绘制圆环，中心透明以显示内部文字 */
class RingProgressWidget : public QWidget
{
public:
    explicit RingProgressWidget(QWidget* parent = nullptr)
        : QWidget(parent), m_value(0), m_ringColor(Qt::gray)
    {
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground);
    }
    void setValue(int value) { m_value = qBound(0, value, 100); update(); }
    void setRingColor(const QColor& c) { m_ringColor = c; update(); }
    int value() const { return m_value; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const int side = qMin(width(), height());
        const int margin = 4;
        const int ringWidth = 6;
        QRectF rect(margin, margin, side - 2 * margin, side - 2 * margin);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xe5, 0xe6, 0xeb), ringWidth));
        p.drawEllipse(rect);
        if (m_value > 0) {
            p.setPen(QPen(m_ringColor, ringWidth));
            const double span = 360.0 * m_value / 100.0;
            p.drawArc(rect, 90 * 16, -int(span * 16));
        }
    }

private:
    int m_value;
    QColor m_ringColor;
};

} // namespace

/**
 * @brief 管理后台-机器人管理 窗口构造函数
 */
RobotManageDialog::RobotManageDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowModality(Qt::NonModal);
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint | Qt::WindowTitleHint);
    setWindowTitle(QStringLiteral("管理后台-机器人管理"));

    const QSize screenSize = qApp->primaryScreen()->availableSize();
    resize(qMin(screenSize.width() * 0.85, 1280.0), qMin(screenSize.height() * 0.8, 800.0));
    setMinimumSize(900, 560);

    buildUI();
    // applyStyle();
    // 设置robotManage所有样式
    setStyleSheet(ApplyStyle::robotManageFullStyle());
}

void RobotManageDialog::buildUI()
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildLeftNav());

    m_contentStack = new QStackedWidget(this);
    m_contentStack->setObjectName("robotContentStack");
    m_contentStack->addWidget(buildOverviewPage());    // index 0 系统概览
    m_contentStack->addWidget(buildRobotManagePage()); // index 1，默认显示机器人管理
    m_contentStack->addWidget(buildKnowledgePage());   // index 2 知识库管理
    m_contentStack->addWidget(buildMessagePage());     // index 3 消息处理
    m_contentStack->addWidget(buildJargonPage());      // index 4 行话转换
    m_contentStack->addWidget(buildForbiddenPage());   // index 5 违禁词管理
    m_contentStack->addWidget(buildHistoryPage());     // index 6 对话历史
    m_contentStack->addWidget(buildBackupPage()); // index 7 数据备份
    m_contentStack->addWidget(buildLogPage()); // index 8 日志管理
    m_contentStack->setCurrentIndex(1);
    rootLayout->addWidget(m_contentStack, 1);
}

QFrame* RobotManageDialog::makeCard(QWidget* parent, const QString& objectName)
{
    auto* card = new QFrame(parent);
    if (!objectName.isEmpty())
        card->setObjectName(objectName);
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

enum class AppIcon {
    Overview,
    Robot,
    Knowledge,
    Message,
    Jargon,
    Forbidden,
    History,
    Backup,
    Log
};

// 图标管理类
class IconManager {
public:
    static QIcon getIcon(AppIcon icon, const QSize& size = QSize(28, 28)) {
        static QMap<AppIcon, QString> iconMap = {
            {AppIcon::Overview, ":/res/RobotManage/overview.png"},
            {AppIcon::Robot, ":/res/RobotManage/robot.png"},
            {AppIcon::Knowledge, ":/res/RobotManage/knowledge.png"},
            {AppIcon::Message, ":/res/RobotManage/message.png"},
            {AppIcon::Jargon, ":/res/RobotManage/Jargon.png"},
            {AppIcon::Forbidden, ":/res/RobotManage/forbidden.png"},
            {AppIcon::History, ":/res/RobotManage/history.png"},
            {AppIcon::Backup, ":/res/RobotManage/backup.png"},
            {AppIcon::Log, ":/res/RobotManage/Log.png"}
        };

        QPixmap pixmap(iconMap.value(icon, ":/icons/default.png"));
        return QIcon(pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
};

/**
 * @brief 构建左侧深色导航栏（#25262b）
 */
QWidget* RobotManageDialog::buildLeftNav()
{
    auto* nav = new QWidget(this);
    nav->setObjectName("robotNavSidebar");
    nav->setFixedWidth(240);

    auto* layout = new QVBoxLayout(nav);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 顶部系统标识
    auto* brand = new QWidget(nav);
    brand->setObjectName("navBrand");
    auto* brandLayout = new QVBoxLayout(brand);
    brandLayout->setContentsMargins(20, 20, 20, 16);
    brandLayout->setSpacing(6);
    auto* titleLabel = new QLabel(QStringLiteral("羊羊AI客服系统 v1.0"), brand);
    titleLabel->setObjectName("navBrandTitle");
    auto* subLabel = new QLabel(QStringLiteral("多机器人多角色管理平台"), brand);
    subLabel->setObjectName("navBrandSub");
    brandLayout->addWidget(titleLabel);
    brandLayout->addWidget(subLabel);
    layout->addWidget(brand);

    // 分割线
    auto* line = new QFrame(nav);
    line->setObjectName("navDivider");
    line->setFixedHeight(1);
    layout->addWidget(line);

    // 今日概况
    auto* todayCard = new QFrame(nav);
    todayCard->setObjectName("navStatCard");
    auto* todayLayout = new QVBoxLayout(todayCard);
    todayLayout->setContentsMargins(8, 8, 8, 8);
    todayLayout->setSpacing(8);
    auto* todayTitleRow = new QHBoxLayout();
    auto* todayTitle = new QLabel(QStringLiteral("今日概况"), todayCard);
    todayTitle->setObjectName("navStatTitle");
    auto* refreshBtn = new QToolButton(todayCard);
    refreshBtn->setObjectName("navRefreshBtn");
    refreshBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshBtn->setIconSize(QSize(14, 14));
    refreshBtn->setAutoRaise(true);
    todayTitleRow->addWidget(todayTitle);
    todayTitleRow->addWidget(refreshBtn, 0, Qt::AlignRight);
    todayLayout->addLayout(todayTitleRow);
    auto* todayRow = new QHBoxLayout();
    auto* todayCount = new QLabel(QStringLiteral("今日对话数"), todayCard);
    todayCount->setObjectName("navStatLabel");
    auto* todayVal = new QLabel(QStringLiteral("0"), todayCard);
    todayVal->setObjectName("navStatValue");
    todayRow->addWidget(todayCount);
    todayRow->addWidget(todayVal, 0, Qt::AlignRight);
    auto* aiRateLabel = new QLabel(QStringLiteral("AI成功率"), todayCard);
    aiRateLabel->setObjectName("navStatLabel");
    auto* aiRateVal = new QLabel(QStringLiteral("0%"), todayCard);
    aiRateVal->setObjectName("navStatValueRed");
    auto* aiRow = new QHBoxLayout();
    aiRow->addWidget(aiRateLabel);
    aiRow->addWidget(aiRateVal, 0, Qt::AlignRight);
    todayLayout->addLayout(todayRow);
    todayLayout->addLayout(aiRow);
    layout->addWidget(todayCard);

    // 分隔
    auto* sep1 = new QFrame(nav);
    sep1->setObjectName("navStatDivider");
    sep1->setFixedHeight(1);
    layout->addWidget(sep1);

    // 算力剩余
    auto* powerCard = new QFrame(nav);
    powerCard->setObjectName("navStatCard");
    auto* powerLayout = new QHBoxLayout(powerCard);
    powerLayout->setContentsMargins(16, 12, 16, 12);
    auto* powerText = new QLabel(QStringLiteral("算力剩余"), powerCard);
    powerText->setObjectName("navStatTitle");
    auto* powerVal = new QLabel(QStringLiteral("暂无数据"), powerCard);
    powerVal->setObjectName("navStatValue");
    auto* powerIcon = new QLabel(QStringLiteral("⚡"), powerCard);
    powerIcon->setObjectName("navPowerIcon");
    powerLayout->addWidget(powerText);
    powerLayout->addWidget(powerVal, 1);
    powerLayout->addWidget(powerIcon);
    layout->addWidget(powerCard);

    auto* sep2 = new QFrame(nav);
    sep2->setObjectName("navStatDivider");
    sep2->setFixedHeight(1);
    layout->addWidget(sep2);

    // 导航菜单
    m_navList = new QListWidget(nav);
    m_navList->setObjectName("robotNavList");
    m_navList->setFrameShape(QFrame::NoFrame);

    auto* style = this->style();
    const int iconSz = 18;

    auto addGroup = [this](const QString& groupName) {
        auto* item = new QListWidgetItem(groupName, m_navList);
        item->setFlags(Qt::ItemIsEnabled);
        item->setData(Qt::UserRole, QStringLiteral("group"));
        item->setForeground(QColor(0x8a, 0x8b, 0x90));
        QFont f = m_navList->font();
        f.setPointSize(11);
        f.setBold(true);
        item->setFont(f);
    };

    // auto addItem = [this, style, iconSz](QStyle::StandardPixmap pix, const QString& text, const QString& id, bool selected) {
    //     auto* item = new QListWidgetItem(
    //         style->standardIcon(pix).pixmap(iconSz, iconSz),
    //         text,
    //         m_navList);
    //     item->setData(Qt::UserRole, id);
    //     if (selected)
    //         m_navList->setCurrentItem(item);
    // };
    auto addItem = [this, iconSz](AppIcon icon, const QString& text, const QString& id, bool selected) {
        auto* item = new QListWidgetItem(
            IconManager::getIcon(icon, QSize(iconSz, iconSz)),
            text,
            m_navList);
        item->setData(Qt::UserRole, id);
        if (selected)
            m_navList->setCurrentItem(item);
    };

    // addGroup(QStringLiteral("核心功能"));
    // addItem(QStyle::SP_FileIcon, QStringLiteral("系统概览"), QStringLiteral("overview"), false);
    // addItem(QStyle::SP_ComputerIcon, QStringLiteral("机器人管理"), QStringLiteral("robot"), true);
    // addItem(QStyle::SP_DirIcon, QStringLiteral("知识库管理"), QStringLiteral("knowledge"), false);
    addGroup(QStringLiteral("核心功能"));
    addItem(AppIcon::Overview, QStringLiteral("系统概览"), QStringLiteral("overview"), false);
    addItem(AppIcon::Robot, QStringLiteral("机器人管理"), QStringLiteral("robot"), true);
    addItem(AppIcon::Knowledge, QStringLiteral("知识库管理"), QStringLiteral("knowledge"), false);
    addGroup(QStringLiteral("对话过程管理"));
    addItem(AppIcon::Message, QStringLiteral("消息处理"), QStringLiteral("message"), false);
    addItem(AppIcon::Jargon, QStringLiteral("行话转换"), QStringLiteral("jargon"), false);
    addItem(AppIcon::Forbidden, QStringLiteral("违禁词管理"), QStringLiteral("forbidden"), false);
    addItem(AppIcon::History, QStringLiteral("对话历史"), QStringLiteral("history"), false);
    addGroup(QStringLiteral("系统管理"));
    addItem(AppIcon::Backup, QStringLiteral("数据备份"), QStringLiteral("backup"), false);
    addItem(AppIcon::Log, QStringLiteral("日志管理"), QStringLiteral("log"), false);

    connect(m_navList, &QListWidget::currentItemChanged, this, &RobotManageDialog::onNavItemChanged);
    layout->addWidget(m_navList, 1);
    return nav;
}

void RobotManageDialog::onNavItemChanged()
{
    if (!m_contentStack || !m_navList->currentItem())
        return;
    const QString id = m_navList->currentItem()->data(Qt::UserRole).toString();
    if (id == QLatin1String("overview"))
        m_contentStack->setCurrentIndex(0);
    else if (id == QLatin1String("robot"))
        m_contentStack->setCurrentIndex(1);
    else if (id == QLatin1String("knowledge"))
        m_contentStack->setCurrentIndex(2);
    else if (id == QLatin1String("message"))
        m_contentStack->setCurrentIndex(3);
    else if (id == QLatin1String("jargon"))
        m_contentStack->setCurrentIndex(4);
    else if (id == QLatin1String("forbidden"))
        m_contentStack->setCurrentIndex(5);
    else if (id == QLatin1String("history"))
        m_contentStack->setCurrentIndex(6);
    else if (id == QLatin1String("backup"))
        m_contentStack->setCurrentIndex(7);
    else if (id == QLatin1String("log"))
        m_contentStack->setCurrentIndex(8);
    // 其他菜单项暂保持当前页或可后续扩展
}

/**
 * @brief 构建系统概览页面（右侧主内容区之一）
 */
QWidget* RobotManageDialog::buildOverviewPage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与操作栏：系统概览 + 5 个彩色按钮
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(QStringLiteral("系统概览"), content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(QStringLiteral("数据统计和监控"), content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);
    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    // 3. 欢迎横幅
    auto* welcomeBanner = new QFrame(content);
    welcomeBanner->setObjectName("overviewWelcomeBanner");
    welcomeBanner->setMinimumHeight(100);
    auto* welcomeLayout = new QHBoxLayout(welcomeBanner);
    welcomeLayout->setContentsMargins(16, 16, 16, 16);
    auto* welcomeTextCol = new QVBoxLayout();
    welcomeTextCol->setSpacing(6);
    auto* welcomeTitle = new QLabel(QStringLiteral("欢迎回来，管理员！"), welcomeBanner);
    welcomeTitle->setObjectName("overviewWelcomeTitle");
    const QString dateStr = QLocale(QLocale::Chinese).toString(QDate::currentDate(), QStringLiteral("今天是 yyyy年M月d日dddd"));
    auto* welcomeSub = new QLabel(dateStr + QStringLiteral("，系统运行状态优秀"), welcomeBanner);
    welcomeSub->setObjectName("overviewWelcomeSub");
    auto* welcomeStats = new QLabel(QStringLiteral("🟢 0个机器人在线    🟡 0条所有对话"), welcomeBanner);
    welcomeStats->setObjectName("overviewWelcomeSub");
    welcomeTextCol->addWidget(welcomeTitle);
    welcomeTextCol->addWidget(welcomeSub);
    welcomeTextCol->addWidget(welcomeStats);
    welcomeLayout->addLayout(welcomeTextCol, 1);
    auto* robotIconLabel = new QLabel(welcomeBanner);
    robotIconLabel->setObjectName("overviewRobotIcon");
    robotIconLabel->setFixedSize(64, 64);
    robotIconLabel->setAlignment(Qt::AlignCenter);
    robotIconLabel->setStyleSheet("background: rgba(255,255,255,0.15); border-radius: 32px; font-size: 32px;");
    robotIconLabel->setText(QStringLiteral("🤖"));
    welcomeLayout->addWidget(robotIconLabel, 0, Qt::AlignRight);
    mainLayout->addWidget(welcomeBanner);

    // 4. 核心指标（5 张卡片）
    auto* coreLabelRow = new QHBoxLayout();
    auto* coreIcon = new QLabel(content);
    coreIcon->setFixedSize(20, 20);
    coreIcon->setStyleSheet("background: #00b42a; border-radius: 4px;");
    auto* coreTitle = new QLabel(QStringLiteral("核心指标"), content);
    coreTitle->setObjectName("overviewSectionTitle");
    coreLabelRow->addWidget(coreIcon);
    coreLabelRow->addSpacing(8);
    coreLabelRow->addWidget(coreTitle);
    coreLabelRow->addStretch(1);
    mainLayout->addLayout(coreLabelRow);
    auto* coreCardRow = new QHBoxLayout();
    coreCardRow->setSpacing(12);
    struct CoreCard { const char* objName; const char* title; QColor iconBg; };
    for (const CoreCard& c : {
        CoreCard{"overviewCardOrange", "所有对话数", QColor(0xff, 0x7d, 0x00)},
        CoreCard{"overviewCardGreen",  "活跃机器人", QColor(0x00, 0xb4, 0x2a)},
        CoreCard{"overviewCardPurple", "平均准确率", QColor(0x92, 0x54, 0xde)},
        CoreCard{"overviewCardPink",   "今日转人工", QColor(0xf5, 0x31, 0x9d)},
        CoreCard{"overviewCardBlue",   "总机器人数", QColor(0x40, 0x80, 0xff)}
    }) {
        auto* card = makeCard(content, QString::fromUtf8(c.objName));
        card->setMinimumHeight(88);
        auto* cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(12, 12, 12, 12);
        auto* iconLabel = new QLabel(card);
        iconLabel->setFixedSize(32, 32);
        iconLabel->setStyleSheet(QString("background: %1; border-radius: 6px;").arg(c.iconBg.name()));
        auto* titleLabel = new QLabel(QString::fromUtf8(c.title), card);
        titleLabel->setObjectName("overviewCardTitle");
        auto* valueLabel = new QLabel(QStringLiteral("--"), card);
        valueLabel->setObjectName("overviewCardValue");
        cardLay->addWidget(iconLabel);
        cardLay->addWidget(titleLabel);
        cardLay->addWidget(valueLabel);
        coreCardRow->addWidget(card, 1);
    }
    mainLayout->addLayout(coreCardRow);

    // 5. 系统资源概览（5 张卡片）
    auto* resLabelRow = new QHBoxLayout();
    auto* resIcon = new QLabel(content);
    resIcon->setFixedSize(20, 20);
    resIcon->setStyleSheet("background: #4080ff; border-radius: 4px;");
    auto* resTitle = new QLabel(QStringLiteral("系统资源概览"), content);
    resTitle->setObjectName("overviewSectionTitle");
    resLabelRow->addWidget(resIcon);
    resLabelRow->addSpacing(8);
    resLabelRow->addWidget(resTitle);
    resLabelRow->addStretch(1);
    mainLayout->addLayout(resLabelRow);
    auto* resCardRow = new QHBoxLayout();
    resCardRow->setSpacing(12);
    struct ResCard { const char* objName; int value; const char* label; QColor iconBg; };
    for (const ResCard& r : {
        ResCard{"resCardPurple", 10, "AI模型", QColor(0x92, 0x54, 0xde)},
        ResCard{"resCardBlue",   14, "行业",   QColor(0x40, 0x80, 0xff)},
        ResCard{"resCardGreen",  6,  "平台",   QColor(0x00, 0xb4, 0x2a)},
        ResCard{"resCardPink",   1,  "店铺",   QColor(0xf5, 0x31, 0x9d)},
        ResCard{"resCardYellow", 2,  "知识条目", QColor(0xfa, 0xad, 0x14)}
    }) {
        auto* card = makeCard(content, QString::fromUtf8(r.objName));
        card->setMinimumHeight(72);
        auto* cardLay = new QHBoxLayout(card);
        cardLay->setContentsMargins(12, 12, 12, 12);
        auto* iconLabel = new QLabel(card);
        iconLabel->setFixedSize(36, 36);
        iconLabel->setStyleSheet(QString("background: %1; border-radius: 6px;").arg(r.iconBg.name()));
        auto* textCol = new QVBoxLayout();
        auto* valueLabel = new QLabel(QString::number(r.value), card);
        valueLabel->setObjectName("overviewCardValue");
        auto* labelLabel = new QLabel(QString::fromUtf8(r.label), card);
        labelLabel->setObjectName("overviewCardTitle");
        textCol->addWidget(valueLabel);
        textCol->addWidget(labelLabel);
        cardLay->addWidget(iconLabel);
        cardLay->addLayout(textCol, 1);
        resCardRow->addWidget(card, 1);
    }
    mainLayout->addLayout(resCardRow);

    // 6. 系统状态（4 个环形进度）
    auto* statusLabelRow = new QHBoxLayout();
    auto* statusIcon = new QLabel(content);
    statusIcon->setFixedSize(20, 20);
    statusIcon->setStyleSheet("background: #00b42a; border-radius: 4px;");
    auto* statusTitle = new QLabel(QStringLiteral("系统状态"), content);
    statusTitle->setObjectName("overviewSectionTitle");
    statusLabelRow->addWidget(statusIcon);
    statusLabelRow->addSpacing(8);
    statusLabelRow->addWidget(statusTitle);
    statusLabelRow->addStretch(1);
    mainLayout->addLayout(statusLabelRow);
    auto* statusRow = new QHBoxLayout();
    statusRow->setSpacing(24);
    statusRow->setAlignment(Qt::AlignCenter);
    auto* ring1 = new RingProgressWidget(content);
    ring1->setFixedSize(80, 80);
    ring1->setValue(100);
    ring1->setRingColor(QColor(0x00, 0xb4, 0x2a));
    auto* ring1Label = new QLabel(QStringLiteral("优秀\n系统状态"), content);
    ring1Label->setObjectName("overviewRingLabelGreen");
    ring1Label->setAlignment(Qt::AlignCenter);
    auto* ring2 = new RingProgressWidget(content);
    ring2->setFixedSize(80, 80);
    ring2->setValue(0);
    ring2->setRingColor(QColor(0x8a, 0x8b, 0x90));
    auto* ring2Label = new QLabel(QStringLiteral("0%\nCPU"), content);
    ring2Label->setObjectName("overviewRingLabelGray");
    ring2Label->setAlignment(Qt::AlignCenter);
    auto* ring3 = new RingProgressWidget(content);
    ring3->setFixedSize(80, 80);
    ring3->setValue(57);
    ring3->setRingColor(QColor(0xfa, 0xad, 0x14));
    auto* ring3Label = new QLabel(QStringLiteral("56.8%\n内存"), content);
    ring3Label->setObjectName("overviewRingLabelYellow");
    ring3Label->setAlignment(Qt::AlignCenter);
    auto* ring4 = new RingProgressWidget(content);
    ring4->setFixedSize(80, 80);
    ring4->setValue(100);
    ring4->setRingColor(QColor(0x00, 0xb4, 0x2a));
    auto* ring4Label = new QLabel(QStringLiteral("平均响应"), content);
    ring4Label->setObjectName("overviewRingLabelGreen");
    ring4Label->setAlignment(Qt::AlignCenter);
    auto* wrap1 = new QWidget(content);
    auto* stack1 = new QStackedLayout(wrap1);
    stack1->setStackingMode(QStackedLayout::StackAll);
    stack1->addWidget(ring1);
    ring1Label->setAttribute(Qt::WA_TransparentForMouseEvents);
    ring1Label->setStyleSheet("background: transparent;");
    auto* labelContainer1 = new QWidget(content);
    labelContainer1->setAutoFillBackground(false);
    labelContainer1->setAttribute(Qt::WA_TranslucentBackground);
    auto* lc1 = new QVBoxLayout(labelContainer1);
    lc1->setContentsMargins(0, 0, 0, 0);
    lc1->addWidget(ring1Label, 0, Qt::AlignCenter);
    stack1->addWidget(labelContainer1);
    auto* wrap2 = new QWidget(content);
    auto* stack2 = new QStackedLayout(wrap2);
    stack2->setStackingMode(QStackedLayout::StackAll);
    stack2->addWidget(ring2);
    ring2Label->setAttribute(Qt::WA_TransparentForMouseEvents);
    ring2Label->setStyleSheet("background: transparent;");
    auto* labelContainer2 = new QWidget(content);
    labelContainer2->setAutoFillBackground(false);
    labelContainer2->setAttribute(Qt::WA_TranslucentBackground);
    auto* lc2 = new QVBoxLayout(labelContainer2);
    lc2->setContentsMargins(0, 0, 0, 0);
    lc2->addWidget(ring2Label, 0, Qt::AlignCenter);
    stack2->addWidget(labelContainer2);
    auto* wrap3 = new QWidget(content);
    auto* stack3 = new QStackedLayout(wrap3);
    stack3->setStackingMode(QStackedLayout::StackAll);
    stack3->addWidget(ring3);
    ring3Label->setAttribute(Qt::WA_TransparentForMouseEvents);
    ring3Label->setStyleSheet("background: transparent;");
    auto* labelContainer3 = new QWidget(content);
    labelContainer3->setAutoFillBackground(false);
    labelContainer3->setAttribute(Qt::WA_TranslucentBackground);
    auto* lc3 = new QVBoxLayout(labelContainer3);
    lc3->setContentsMargins(0, 0, 0, 0);
    lc3->addWidget(ring3Label, 0, Qt::AlignCenter);
    stack3->addWidget(labelContainer3);
    auto* wrap4 = new QWidget(content);
    auto* stack4 = new QStackedLayout(wrap4);
    stack4->setStackingMode(QStackedLayout::StackAll);
    stack4->addWidget(ring4);
    ring4Label->setAttribute(Qt::WA_TransparentForMouseEvents);
    ring4Label->setStyleSheet("background: transparent;");
    auto* labelContainer4 = new QWidget(content);
    labelContainer4->setAutoFillBackground(false);
    labelContainer4->setAttribute(Qt::WA_TranslucentBackground);
    auto* lc4 = new QVBoxLayout(labelContainer4);
    lc4->setContentsMargins(0, 0, 0, 0);
    lc4->addWidget(ring4Label, 0, Qt::AlignCenter);
    stack4->addWidget(labelContainer4);
    statusRow->addWidget(wrap1);
    statusRow->addWidget(wrap2);
    statusRow->addWidget(wrap3);
    statusRow->addWidget(wrap4);
    mainLayout->addLayout(statusRow);

    scroll->setWidget(content);
    return scroll;
}

/**
 * @brief 构建机器人管理页面（右侧主内容区之一）
 */
QWidget* RobotManageDialog::buildRobotManagePage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题栏：左侧标题+副标题，右侧 5 个彩色按钮
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(QStringLiteral("机器人管理"), content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(QStringLiteral("查看和管理所有机器人"), content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);

    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    // 3. 数据统计卡片行（4 个）
    auto* cardRow = new QHBoxLayout();
    cardRow->setSpacing(12);
    struct CardDef { const char* objName; const char* title; const char* sub; };
    for (const CardDef& d : {
        CardDef{"statCardBlue",   "总机器人",   "系统管理"},
        CardDef{"statCardGreen",  "活跃机器人", "暂无数据"},
        CardDef{"statCardPurple", "今日对话",   "暂无数据"},
        CardDef{"statCardOrange", "成功率",     "暂无数据"}
    }) {
        auto* card = makeCard(content, QString::fromUtf8(d.objName));
        card->setMinimumHeight(88);
        auto* cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(16, 16, 16, 16);
        cardLay->setSpacing(6);
        auto* cardTitle = new QLabel(QString::fromUtf8(d.title), card);
        cardTitle->setObjectName("robotStatCardTitle");
        auto* cardSub = new QLabel(QString::fromUtf8(d.sub), card);
        cardSub->setObjectName("robotStatCardSub");
        cardLay->addWidget(cardTitle);
        cardLay->addWidget(cardSub);
        cardRow->addWidget(card, 1);
    }
    mainLayout->addLayout(cardRow);

    // 4. 筛选与操作栏
    auto* filterBar = new QHBoxLayout();
    filterBar->setSpacing(12);
    auto* searchRobot = new QLineEdit(content);
    searchRobot->setObjectName("robotFilterSearch");
    searchRobot->setPlaceholderText(QStringLiteral("搜索机器人名称或行业"));
    searchRobot->setMinimumWidth(220);
    auto* comboIndustry = new QComboBox(content);
    comboIndustry->setObjectName("robotFilterCombo");
    comboIndustry->addItem(QStringLiteral("全部行业"));
    comboIndustry->addItem(QStringLiteral("游戏行业"));
    comboIndustry->addItem(QStringLiteral("软件行业"));
    comboIndustry->addItem(QStringLiteral("电商行业"));
    comboIndustry->setMinimumWidth(120);
    filterBar->addWidget(searchRobot);
    filterBar->addWidget(comboIndustry);
    filterBar->addSpacing(16);
    auto* btnIndustry = new QPushButton(QStringLiteral("管理行业"), content);
    btnIndustry->setObjectName("filterBtnOrange");
    auto* btnPlatform = new QPushButton(QStringLiteral("管理平台"), content);
    btnPlatform->setObjectName("filterBtnPurple");
    auto* btnStore = new QPushButton(QStringLiteral("店铺管理"), content);
    btnStore->setObjectName("filterBtnBlue");
    auto* btnTrain = new QPushButton(QStringLiteral("上岗前培训"), content);
    btnTrain->setObjectName("filterBtnGreen");
    for (QPushButton* b : { btnIndustry, btnPlatform, btnStore, btnTrain }) {
        b->setFixedHeight(32);
        filterBar->addWidget(b);
    }
    filterBar->addStretch(1);
    auto* btnCreate = new QPushButton(QStringLiteral("+ 创建机器人"), content);
    btnCreate->setObjectName("robotCreateBtn");
    btnCreate->setFixedHeight(36);
    filterBar->addWidget(btnCreate);
    mainLayout->addLayout(filterBar);

    // 5. 空状态区域
    auto* emptyPanel = makeCard(content, "robotEmptyPanel");
    emptyPanel->setMinimumHeight(320);
    auto* emptyLayout = new QVBoxLayout(emptyPanel);
    emptyLayout->setContentsMargins(40, 40, 40, 40);
    emptyLayout->setSpacing(16);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto* emptyIconWrap = new QFrame(emptyPanel);
    emptyIconWrap->setObjectName("robotEmptyIconWrap");
    emptyIconWrap->setFixedSize(96, 96);
    emptyIconWrap->setStyleSheet("background: #e9e5ff; border-radius: 48px;");
    auto* emptyIconLay = new QVBoxLayout(emptyIconWrap);
    emptyIconLay->setAlignment(Qt::AlignCenter);
    auto* emptyIcon = new QLabel(QStringLiteral("🤖"), emptyIconWrap);
    emptyIcon->setStyleSheet("font-size: 48px;");
    emptyIconLay->addWidget(emptyIcon);
    auto* emptyTitle = new QLabel(QStringLiteral("还没有机器人"), emptyPanel);
    emptyTitle->setObjectName("robotEmptyTitle");
    auto* emptySub = new QLabel(QStringLiteral("创建您的第一个AI客服机器人，开启智能客服之旅"), emptyPanel);
    emptySub->setObjectName("robotEmptySub");
    auto* btnCreateCenter = new QPushButton(QStringLiteral("+ 创建机器人"), emptyPanel);
    btnCreateCenter->setObjectName("robotCreateBtn");
    btnCreateCenter->setFixedHeight(36);
    emptyLayout->addWidget(emptyIconWrap);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptySub);
    emptyLayout->addWidget(btnCreateCenter);
    mainLayout->addWidget(emptyPanel, 1);

    scroll->setWidget(content);
    return scroll;
}

/**
 * @brief 构建知识库管理页面（右侧主内容区之一）
 */
QWidget* RobotManageDialog::buildKnowledgePage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与操作栏
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(QStringLiteral("知识库管理"), content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(QStringLiteral("结构化层级知识库管理"), content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);
    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    // 3. 主内容区：左侧树状导航 + 右侧内容展示
    auto* splitter = new QSplitter(Qt::Horizontal, content);
    splitter->setObjectName("kbSplitter");
    splitter->setChildrenCollapsible(false);

    // 左侧：树状导航栏（固定宽度，白色背景）
    auto* leftPanel = new QWidget(splitter);
    leftPanel->setObjectName("kbLeftPanel");
    leftPanel->setMinimumWidth(280);
    leftPanel->setMaximumWidth(360);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);
    auto* treeSearch = new QLineEdit(leftPanel);
    treeSearch->setObjectName("kbTreeSearch");
    treeSearch->setPlaceholderText(QStringLiteral("搜索店名、行业、商品ID、商品"));
    treeSearch->setClearButtonEnabled(false);
    leftLayout->addWidget(treeSearch);
    auto* treeBtnRow = new QHBoxLayout();
    auto* btnSearch = new QPushButton(QStringLiteral("搜索"), leftPanel);
    btnSearch->setObjectName("kbTreeSearchBtn");
    btnSearch->setFixedHeight(32);
    auto* btnAdd = new QPushButton(QStringLiteral("+"), leftPanel);
    btnAdd->setObjectName("kbTreeAddBtn");
    btnAdd->setFixedSize(32, 32);
    treeBtnRow->addWidget(btnSearch);
    treeBtnRow->addWidget(btnAdd);
    leftLayout->addLayout(treeBtnRow);
    auto* tree = new QTreeWidget(leftPanel);
    tree->setObjectName("kbTree");
    tree->setHeaderHidden(true);
    tree->setColumnCount(2);
    tree->setColumnWidth(0, 200);
    tree->setColumnWidth(1, 36);
    tree->setRootIsDecorated(true);
    tree->setAnimated(true);
    tree->setMinimumWidth(220);
    auto treePalette = tree->palette();
    treePalette.setColor(QPalette::Text, QColor(0x1d, 0x1d, 0x1f));
    treePalette.setColor(QPalette::WindowText, QColor(0x1d, 0x1d, 0x1f));
    tree->setPalette(treePalette);
    tree->setFont(QFont(tree->font().family(), tree->font().pointSize() > 0 ? tree->font().pointSize() : 13));
    const int iconSz = 16;
    auto* style = this->style();
    auto greenIcon = style->standardIcon(QStyle::SP_DialogYesButton).pixmap(iconSz, iconSz);
    auto purpleIcon = style->standardIcon(QStyle::SP_FileIcon).pixmap(iconSz, iconSz);
    auto redIcon = style->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(iconSz, iconSz);
    auto* productRoot = new QTreeWidgetItem(tree, { QStringLiteral("产品知识"), QStringLiteral("0") });
    productRoot->setIcon(0, QIcon(greenIcon));
    auto* platformRoot = new QTreeWidgetItem(tree, { QStringLiteral("平台知识"), QStringLiteral("2") });
    platformRoot->setIcon(0, QIcon(purpleIcon));
    platformRoot->setExpanded(true);
    auto* jd = new QTreeWidgetItem(platformRoot, { QStringLiteral("京东"), QStringLiteral("1") });
    jd->setIcon(0, QIcon(purpleIcon));
    auto* unassigned = new QTreeWidgetItem(platformRoot, { QStringLiteral("未分配数据"), QStringLiteral("0") });
    unassigned->setIcon(0, QIcon(purpleIcon));
    auto* industryRoot = new QTreeWidgetItem(tree, { QStringLiteral("行业知识"), QStringLiteral("14") });
    industryRoot->setIcon(0, QIcon(redIcon));
    industryRoot->setExpanded(true);
    const char* industryNames[] = {
        "五金建材", "安防监控", "游戏", "软件行业", "服装行业", "食品行业", "电商行业",
        "3C配件行业", "手机数码行业", "美妆护肤行业", "家居家装行业", "母婴用品行业", "教育培训行业", "旅游服务行业"
    };
    const int industryCounts[] = { 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 14; ++i) {
        auto* item = new QTreeWidgetItem(industryRoot, { QString::fromUtf8(industryNames[i]), QString::number(industryCounts[i]) });
        item->setIcon(0, QIcon(redIcon));
    }
    leftLayout->addWidget(tree, 1);
    splitter->addWidget(leftPanel);

    // 右侧：内容展示区（浅粉紫渐变空状态）
    auto* rightPanel = new QWidget(splitter);
    rightPanel->setObjectName("kbRightPanel");
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(20, 20, 20, 20);
    rightLayout->setSpacing(16);
    auto* rightTitle = new QLabel(QStringLiteral("选择节点查看知识"), rightPanel);
    rightTitle->setObjectName("kbRightTitle");
    auto* rightSub = new QLabel(QStringLiteral("请从左侧选择一个节点开始管理知识"), rightPanel);
    rightSub->setObjectName("kbRightSub");
    rightLayout->addWidget(rightTitle);
    rightLayout->addWidget(rightSub);
    auto* emptyPanel = makeCard(rightPanel, "kbEmptyPanel");
    emptyPanel->setObjectName("kbEmptyPanel");
    emptyPanel->setMinimumHeight(320);
    auto* emptyLayout = new QVBoxLayout(emptyPanel);
    emptyLayout->setContentsMargins(40, 40, 40, 40);
    emptyLayout->setSpacing(16);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto* emptyIconWrap = new QFrame(emptyPanel);
    emptyIconWrap->setObjectName("kbEmptyIconWrap");
    emptyIconWrap->setFixedSize(64, 64);
    emptyIconWrap->setStyleSheet("background: #e9e5ff; border-radius: 32px;");
    auto* emptyIconLay = new QVBoxLayout(emptyIconWrap);
    emptyIconLay->setAlignment(Qt::AlignCenter);
    auto* emptyIcon = new QLabel(emptyIconWrap);
    emptyIcon->setPixmap(style->standardIcon(QStyle::SP_DirIcon).pixmap(32, 32));
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIconLay->addWidget(emptyIcon);
    auto* emptyTitle = new QLabel(QStringLiteral("选择节点开始管理"), emptyPanel);
    emptyTitle->setObjectName("kbEmptyTitle");
    auto* emptySub = new QLabel(QStringLiteral("从左侧树状导航中选择一个节点来查看和管理知识"), emptyPanel);
    emptySub->setObjectName("kbEmptySub");
    emptyLayout->addWidget(emptyIconWrap);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptySub);
    rightLayout->addWidget(emptyPanel, 1);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    QList<int> sizes;
    sizes << 300 << 600;
    splitter->setSizes(sizes);

    mainLayout->addWidget(splitter, 1);
    scroll->setWidget(content);
    return scroll;
}

/**
 * @brief 构建消息处理页面（右侧主内容区之一）
 */
QWidget* RobotManageDialog::buildMessagePage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与操作栏
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(QStringLiteral("消息处理"), content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(QStringLiteral("统一管理消息处理规则"), content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);
    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    // 3. 功能状态总览
    auto* statusHeader = new QHBoxLayout();
    auto* statusIcon = new QLabel(content);
    statusIcon->setFixedSize(20, 20);
    statusIcon->setStyleSheet("background: #9254de; border-radius: 4px;");
    auto* statusTitle = new QLabel(QStringLiteral("功能状态总览"), content);
    statusTitle->setObjectName("msgSectionTitle");
    auto* statusHint = new QLabel(QStringLiteral("3项启用  0项禁用"), content);
    statusHint->setObjectName("msgSectionHint");
    statusHeader->addWidget(statusIcon);
    statusHeader->addSpacing(8);
    statusHeader->addWidget(statusTitle);
    statusHeader->addStretch(1);
    statusHeader->addWidget(statusHint);
    mainLayout->addLayout(statusHeader);

    auto* cardRow = new QHBoxLayout();
    cardRow->setSpacing(12);
    auto* style = this->style();
    const int iconSz = 20;

    // 接收处理卡片
    auto* cardRecv = makeCard(content, "msgCardRecv");
    cardRecv->setMinimumHeight(140);
    auto* layRecv = new QVBoxLayout(cardRecv);
    layRecv->setContentsMargins(16, 16, 16, 16);
    layRecv->setSpacing(10);
    auto* recvIcon = new QLabel(cardRecv);
    recvIcon->setPixmap(style->standardIcon(QStyle::SP_ArrowUp).pixmap(iconSz, iconSz));
    auto* recvTitle = new QLabel(QStringLiteral("接收处理"), cardRecv);
    recvTitle->setObjectName("msgCardTitle");
    auto* recvSwitch1 = new QCheckBox(QStringLiteral("行话转换"), cardRecv);
    recvSwitch1->setObjectName("msgToggle");
    recvSwitch1->setChecked(true);
    auto* recvSwitch2 = new QCheckBox(QStringLiteral("消息预处理"), cardRecv);
    recvSwitch2->setObjectName("msgToggle");
    recvSwitch2->setChecked(true);
    auto* recvFooter = new QLabel(QStringLiteral("今日处理: 1256"), cardRecv);
    recvFooter->setObjectName("msgCardFooter");
    layRecv->addWidget(recvIcon);
    layRecv->addWidget(recvTitle);
    layRecv->addWidget(recvSwitch1);
    layRecv->addWidget(recvSwitch2);
    layRecv->addWidget(recvFooter);
    cardRow->addWidget(cardRecv, 1);

    // 发送处理卡片
    auto* cardSend = makeCard(content, "msgCardSend");
    cardSend->setMinimumHeight(140);
    auto* laySend = new QVBoxLayout(cardSend);
    laySend->setContentsMargins(16, 16, 16, 16);
    laySend->setSpacing(10);
    auto* sendIcon = new QLabel(cardSend);
    sendIcon->setPixmap(style->standardIcon(QStyle::SP_ArrowDown).pixmap(iconSz, iconSz));
    auto* sendTitle = new QLabel(QStringLiteral("发送处理"), cardSend);
    sendTitle->setObjectName("msgCardTitle");
    auto* sendSwitch = new QCheckBox(QStringLiteral("违禁词检测"), cardSend);
    sendSwitch->setObjectName("msgToggle");
    sendSwitch->setChecked(true);
    auto* sendFooter = new QLabel(QStringLiteral("今日处理: 1189"), cardSend);
    sendFooter->setObjectName("msgCardFooter");
    laySend->addWidget(sendIcon);
    laySend->addWidget(sendTitle);
    laySend->addWidget(sendSwitch);
    laySend->addWidget(sendFooter);
    cardRow->addWidget(cardSend, 1);

    // 处理统计卡片
    auto* cardStat = makeCard(content, "msgCardStat");
    cardStat->setMinimumHeight(140);
    auto* layStat = new QVBoxLayout(cardStat);
    layStat->setContentsMargins(16, 16, 16, 16);
    layStat->setSpacing(8);
    auto* statIcon = new QLabel(cardStat);
    statIcon->setPixmap(style->standardIcon(QStyle::SP_FileDialogContentsView).pixmap(iconSz, iconSz));
    auto* statTitle = new QLabel(QStringLiteral("处理统计"), cardStat);
    statTitle->setObjectName("msgCardTitle");
    auto* stat1 = new QLabel(QStringLiteral("总消息数: 2445"), cardStat);
    stat1->setObjectName("msgStatRow");
    auto* stat2 = new QLabel(QStringLiteral("处理成功: 2367"), cardStat);
    stat2->setObjectName("msgStatRow");
    auto* stat3 = new QLabel(QStringLiteral("检测违规: 78"), cardStat);
    stat3->setObjectName("msgStatRow");
    auto* stat4 = new QLabel(QStringLiteral("平均耗时: 45ms"), cardStat);
    stat4->setObjectName("msgStatRow");
    layStat->addWidget(statIcon);
    layStat->addWidget(statTitle);
    layStat->addWidget(stat1);
    layStat->addWidget(stat2);
    layStat->addWidget(stat3);
    layStat->addWidget(stat4);
    cardRow->addWidget(cardStat, 1);

    // 快速操作卡片
    auto* cardQuick = makeCard(content, "msgCardQuick");
    cardQuick->setMinimumHeight(140);
    auto* layQuick = new QVBoxLayout(cardQuick);
    layQuick->setContentsMargins(16, 16, 16, 16);
    layQuick->setSpacing(10);
    auto* quickTitleRow = new QHBoxLayout();
    auto* quickIcon = new QLabel(cardQuick);
    quickIcon->setPixmap(style->standardIcon(QStyle::SP_BrowserReload).pixmap(iconSz, iconSz));
    auto* quickTitle = new QLabel(QStringLiteral("快速操作"), cardQuick);
    quickTitle->setObjectName("msgCardTitle");
    auto* quickHint = new QLabel(QStringLiteral("3项启用 | 0项禁用"), cardQuick);
    quickHint->setObjectName("msgQuickHint");
    quickTitleRow->addWidget(quickIcon);
    quickTitleRow->addSpacing(8);
    quickTitleRow->addWidget(quickTitle);
    quickTitleRow->addStretch(1);
    quickTitleRow->addWidget(quickHint);
    layQuick->addLayout(quickTitleRow);
    auto* btnEnableAll = new QPushButton(QStringLiteral("全部启用"), cardQuick);
    btnEnableAll->setObjectName("msgBtnGreen");
    btnEnableAll->setFixedHeight(32);
    auto* btnDisableAll = new QPushButton(QStringLiteral("全部禁用"), cardQuick);
    btnDisableAll->setObjectName("msgBtnGray");
    btnDisableAll->setFixedHeight(32);
    auto* btnRestore = new QPushButton(QStringLiteral("恢复默认"), cardQuick);
    btnRestore->setObjectName("msgBtnBlue");
    btnRestore->setFixedHeight(32);
    layQuick->addWidget(btnEnableAll);
    layQuick->addWidget(btnDisableAll);
    layQuick->addWidget(btnRestore);
    cardRow->addWidget(cardQuick, 1);

    mainLayout->addLayout(cardRow);

    // 4. 消息处理流程
    auto* flowHeader = new QHBoxLayout();
    auto* flowIcon = new QLabel(content);
    flowIcon->setFixedSize(20, 20);
    flowIcon->setStyleSheet("background: #4080ff; border-radius: 4px;");
    auto* flowTitle = new QLabel(QStringLiteral("消息处理流程"), content);
    flowTitle->setObjectName("msgSectionTitle");
    flowHeader->addWidget(flowIcon);
    flowHeader->addSpacing(8);
    flowHeader->addWidget(flowTitle);
    flowHeader->addStretch(1);
    mainLayout->addLayout(flowHeader);

    auto* flowRow = new QHBoxLayout();
    flowRow->setSpacing(8);
    struct FlowStep { const char* title; const char* sub; QStyle::StandardPixmap pix; bool highlight; };
    FlowStep steps[] = {
        {"原始消息", "用户输入", QStyle::SP_MessageBoxInformation, false},
        {"消息预处理", nullptr, QStyle::SP_FileDialogContentsView, false},
        {"行话转换", nullptr, QStyle::SP_BrowserReload, false},
        {"AI回复", nullptr, QStyle::SP_ComputerIcon, false},
        {"违禁词检测", nullptr, QStyle::SP_MessageBoxCritical, false},
        {"发送完成", "发送给用户", QStyle::SP_DialogOkButton, true}
    };
    for (int i = 0; i < 6; ++i) {
        const FlowStep& s = steps[i];
        auto* stepCard = makeCard(content, s.highlight ? "msgFlowStepHighlight" : "msgFlowStep");
        stepCard->setMinimumWidth(100);
        auto* stepLay = new QVBoxLayout(stepCard);
        stepLay->setContentsMargins(12, 12, 12, 12);
        stepLay->setAlignment(Qt::AlignCenter);
        auto* stepIcon = new QLabel(stepCard);
        stepIcon->setPixmap(style->standardIcon(s.pix).pixmap(24, 24));
        stepIcon->setAlignment(Qt::AlignCenter);
        auto* stepTitle = new QLabel(QString::fromUtf8(s.title), stepCard);
        stepTitle->setObjectName("msgFlowStepTitle");
        stepTitle->setAlignment(Qt::AlignCenter);
        stepLay->addWidget(stepIcon);
        stepLay->addWidget(stepTitle);
        if (s.sub) {
            auto* stepSub = new QLabel(QString::fromUtf8(s.sub), stepCard);
            stepSub->setObjectName("msgFlowStepSub");
            stepSub->setAlignment(Qt::AlignCenter);
            stepLay->addWidget(stepSub);
        }
        flowRow->addWidget(stepCard);
        if (i < 5)
            flowRow->addWidget(new QLabel(QStringLiteral("→"), content), 0, Qt::AlignCenter);
    }
    mainLayout->addLayout(flowRow);

    // 5. 智能文本处理
    auto* textHeader = new QHBoxLayout();
    auto* textIcon = new QLabel(content);
    textIcon->setFixedSize(20, 20);
    textIcon->setStyleSheet("background: #00b42a; border-radius: 4px;");
    auto* textTitle = new QLabel(QStringLiteral("智能文本处理"), content);
    textTitle->setObjectName("msgSectionTitle");
    textHeader->addWidget(textIcon);
    textHeader->addSpacing(8);
    textHeader->addWidget(textTitle);
    textHeader->addStretch(1);
    mainLayout->addLayout(textHeader);

    auto* textSplitter = new QSplitter(Qt::Horizontal, content);
    textSplitter->setObjectName("msgTextSplitter");
    textSplitter->setChildrenCollapsible(false);

    // 左侧：原始文本
    auto* leftText = new QWidget(textSplitter);
    auto* leftTextLayout = new QVBoxLayout(leftText);
    leftTextLayout->setContentsMargins(0, 0, 12, 0);
    auto* origTitle = new QLabel(QStringLiteral("原始文本"), leftText);
    origTitle->setObjectName("msgSubSectionTitle");
    auto* origEdit = new QTextEdit(leftText);
    origEdit->setObjectName("msgTextEdit");
    origEdit->setPlaceholderText(QStringLiteral("请输入需要处理的文本..."));
    origEdit->setMinimumHeight(120);
    auto* origCount = new QLabel(QStringLiteral("字符数: 0"), leftText);
    origCount->setObjectName("msgCharCount");
    leftTextLayout->addWidget(origTitle);
    leftTextLayout->addWidget(origEdit, 1);
    leftTextLayout->addWidget(origCount);
    auto* optTitle = new QLabel(QStringLiteral("处理选项"), leftText);
    optTitle->setObjectName("msgSubSectionTitle");
    auto* opt1 = new QCheckBox(QStringLiteral("去除多余空格"), leftText);
    opt1->setObjectName("msgToggle");
    opt1->setChecked(true);
    auto* opt2 = new QCheckBox(QStringLiteral("去除多余空行"), leftText);
    opt2->setObjectName("msgToggle");
    opt2->setChecked(true);
    leftTextLayout->addWidget(optTitle);
    leftTextLayout->addWidget(opt1);
    leftTextLayout->addWidget(opt2);
    textSplitter->addWidget(leftText);

    // 右侧：处理结果
    auto* rightText = new QWidget(textSplitter);
    auto* rightTextLayout = new QVBoxLayout(rightText);
    rightTextLayout->setContentsMargins(12, 0, 0, 0);
    auto* resultTitleRow = new QHBoxLayout();
    auto* resultTitle = new QLabel(QStringLiteral("处理结果"), rightText);
    resultTitle->setObjectName("msgSubSectionTitle");
    auto* btnStart = new QPushButton(QStringLiteral("开始处理"), rightText);
    btnStart->setObjectName("msgBtnBlue");
    btnStart->setFixedHeight(32);
    auto* btnClear = new QPushButton(QStringLiteral("清空"), rightText);
    btnClear->setObjectName("msgBtnGray");
    btnClear->setFixedHeight(32);
    resultTitleRow->addWidget(resultTitle);
    resultTitleRow->addStretch(1);
    resultTitleRow->addWidget(btnStart);
    resultTitleRow->addSpacing(8);
    resultTitleRow->addWidget(btnClear);
    rightTextLayout->addLayout(resultTitleRow);
    auto* resultEdit = new QTextEdit(rightText);
    resultEdit->setObjectName("msgTextEdit");
    resultEdit->setPlaceholderText(QStringLiteral("处理后的文本将显示在这里..."));
    resultEdit->setReadOnly(true);
    resultEdit->setMinimumHeight(120);
    auto* resultCount = new QLabel(QStringLiteral("字符数: 0"), rightText);
    resultCount->setObjectName("msgCharCount");
    rightTextLayout->addWidget(resultEdit, 1);
    rightTextLayout->addWidget(resultCount);
    textSplitter->addWidget(rightText);
    textSplitter->setStretchFactor(0, 1);
    textSplitter->setStretchFactor(1, 1);
    QList<int> textSizes;
    textSizes << 400 << 400;
    textSplitter->setSizes(textSizes);

    mainLayout->addWidget(textSplitter, 1);
    scroll->setWidget(content);
    return scroll;
}

/**
 * @brief 构建行话转换页面（右侧主内容区之一）
 */
QWidget* RobotManageDialog::buildJargonPage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与操作栏
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(QStringLiteral("行话转换"), content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(QStringLiteral("收到客户消息时，进行相关词汇的替换方便AI理解"), content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);
    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    // 3. 平台筛选栏 + 右上操作按钮
    auto* controlBar = new QHBoxLayout();
    auto* platformLabel = new QLabel(QStringLiteral("当前平台："), content);
    platformLabel->setObjectName("jargonPlatformLabel");
    auto* platformCombo = new QComboBox(content);
    platformCombo->setObjectName("jargonPlatformCombo");
    platformCombo->addItem(QStringLiteral("全部平台"));
    platformCombo->addItem(QStringLiteral("通用"));
    platformCombo->addItem(QStringLiteral("千牛"));
    platformCombo->addItem(QStringLiteral("京东"));
    platformCombo->addItem(QStringLiteral("拼多多"));
    platformCombo->addItem(QStringLiteral("抖音"));
    platformCombo->addItem(QStringLiteral("小红书商家"));
    platformCombo->addItem(QStringLiteral("抖店"));
    platformCombo->setMinimumWidth(140);
    controlBar->addWidget(platformLabel);
    controlBar->addWidget(platformCombo);
    controlBar->addStretch(1);
    auto* btnTest = new QPushButton(QStringLiteral("功能测试"), content);
    btnTest->setObjectName("jargonBtnTest");
    btnTest->setFixedHeight(32);
    auto* btnAddRule = new QPushButton(QStringLiteral("+ 添加规则"), content);
    btnAddRule->setObjectName("jargonBtnAdd");
    btnAddRule->setFixedHeight(32);
    controlBar->addWidget(btnTest);
    controlBar->addSpacing(8);
    controlBar->addWidget(btnAddRule);
    mainLayout->addLayout(controlBar);

    // 4. 统计卡片行（4 个）
    auto* cardRow = new QHBoxLayout();
    cardRow->setSpacing(12);
    auto* style = this->style();
    const int iconSz = 24;

    auto* cardTotal = makeCard(content, "jargonCardTotal");
    cardTotal->setMinimumHeight(88);
    auto* layTotal = new QHBoxLayout(cardTotal);
    layTotal->setContentsMargins(16, 16, 16, 16);
    auto* totalIcon = new QLabel(cardTotal);
    totalIcon->setPixmap(style->standardIcon(QStyle::SP_FileDialogListView).pixmap(iconSz, iconSz));
    auto* totalCol = new QVBoxLayout();
    auto* totalLabel = new QLabel(QStringLiteral("规则总数"), cardTotal);
    totalLabel->setObjectName("jargonCardLabel");
    auto* totalVal = new QLabel(QStringLiteral("0"), cardTotal);
    totalVal->setObjectName("jargonCardValue");
    totalCol->addWidget(totalLabel);
    totalCol->addWidget(totalVal);
    layTotal->addWidget(totalIcon);
    layTotal->addLayout(totalCol, 1);
    cardRow->addWidget(cardTotal, 1);

    auto* cardEnabled = makeCard(content, "jargonCardEnabled");
    cardEnabled->setMinimumHeight(88);
    auto* layEnabled = new QHBoxLayout(cardEnabled);
    layEnabled->setContentsMargins(16, 16, 16, 16);
    auto* enabledIcon = new QLabel(cardEnabled);
    enabledIcon->setPixmap(style->standardIcon(QStyle::SP_DialogOkButton).pixmap(iconSz, iconSz));
    auto* enabledCol = new QVBoxLayout();
    auto* enabledLabel = new QLabel(QStringLiteral("已启用"), cardEnabled);
    enabledLabel->setObjectName("jargonCardLabel");
    auto* enabledVal = new QLabel(QStringLiteral("0"), cardEnabled);
    enabledVal->setObjectName("jargonCardValue");
    enabledCol->addWidget(enabledLabel);
    enabledCol->addWidget(enabledVal);
    layEnabled->addWidget(enabledIcon);
    layEnabled->addLayout(enabledCol, 1);
    cardRow->addWidget(cardEnabled, 1);

    auto* cardReplace = makeCard(content, "jargonCardReplace");
    cardReplace->setMinimumHeight(88);
    auto* layReplace = new QHBoxLayout(cardReplace);
    layReplace->setContentsMargins(16, 16, 16, 16);
    auto* replaceIcon = new QLabel(cardReplace);
    replaceIcon->setPixmap(style->standardIcon(QStyle::SP_BrowserReload).pixmap(iconSz, iconSz));
    auto* replaceCol = new QVBoxLayout();
    auto* replaceLabel = new QLabel(QStringLiteral("替换规则"), cardReplace);
    replaceLabel->setObjectName("jargonCardLabel");
    auto* replaceVal = new QLabel(QStringLiteral("0"), cardReplace);
    replaceVal->setObjectName("jargonCardValue");
    replaceCol->addWidget(replaceLabel);
    replaceCol->addWidget(replaceVal);
    layReplace->addWidget(replaceIcon);
    layReplace->addLayout(replaceCol, 1);
    cardRow->addWidget(cardReplace, 1);

    auto* cardDelete = makeCard(content, "jargonCardDelete");
    cardDelete->setMinimumHeight(88);
    auto* layDelete = new QHBoxLayout(cardDelete);
    layDelete->setContentsMargins(16, 16, 16, 16);
    auto* deleteIcon = new QLabel(cardDelete);
    deleteIcon->setPixmap(style->standardIcon(QStyle::SP_TrashIcon).pixmap(iconSz, iconSz));
    auto* deleteCol = new QVBoxLayout();
    auto* deleteLabel = new QLabel(QStringLiteral("删除规则"), cardDelete);
    deleteLabel->setObjectName("jargonCardLabel");
    auto* deleteVal = new QLabel(QStringLiteral("0"), cardDelete);
    deleteVal->setObjectName("jargonCardValue");
    deleteCol->addWidget(deleteLabel);
    deleteCol->addWidget(deleteVal);
    layDelete->addWidget(deleteIcon);
    layDelete->addLayout(deleteCol, 1);
    cardRow->addWidget(cardDelete, 1);

    mainLayout->addLayout(cardRow);

    // 5. 全部规则模块
    auto* rulesHeader = new QHBoxLayout();
    auto* rulesTitle = new QLabel(QStringLiteral("全部规则"), content);
    rulesTitle->setObjectName("jargonSectionTitle");
    auto* rulesSearch = new QLineEdit(content);
    rulesSearch->setObjectName("jargonRulesSearch");
    rulesSearch->setPlaceholderText(QStringLiteral("搜索行话或转换结果..."));
    rulesSearch->setClearButtonEnabled(false);
    rulesSearch->setMinimumWidth(220);
    rulesHeader->addWidget(rulesTitle);
    rulesHeader->addStretch(1);
    rulesHeader->addWidget(rulesSearch);
    mainLayout->addLayout(rulesHeader);

    auto* table = new QTableWidget(content);
    table->setObjectName("jargonTable");
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({ QStringLiteral("状态"), QStringLiteral("平台"), QStringLiteral("原始行话"),
                                       QStringLiteral("转换结果"), QStringLiteral("处理方式"), QStringLiteral("操作") });
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QTableWidget::SelectRows);
    table->setEditTriggers(QTableWidget::NoEditTriggers);
    table->setMinimumHeight(200);
    table->setRowCount(0);
    mainLayout->addWidget(table, 1);

    scroll->setWidget(content);
    return scroll;
}

/**
 * @brief 构建违禁词管理页面（右侧主内容区之一）
 */
QWidget* RobotManageDialog::buildForbiddenPage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与操作栏
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(QStringLiteral("违禁词管理"), content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(QStringLiteral("AI发出消息时，检查违禁词并替换或删除"), content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);
    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    // 3. 顶部操作栏：测试检测 + 右侧按钮组
    auto* topBar = new QHBoxLayout();
    auto* btnTest = new QPushButton(QStringLiteral("测试检测"), content);
    btnTest->setObjectName("forbiddenBtnTest");
    btnTest->setFixedHeight(32);
    btnTest->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    btnTest->setIconSize(QSize(16, 16));
    topBar->addWidget(btnTest);
    topBar->addStretch(1);
    auto* btnExport = new QPushButton(QStringLiteral("↓导出Excel"), content);
    btnExport->setObjectName("forbiddenBtnExport");
    btnExport->setFixedHeight(32);
    auto* btnImport = new QPushButton(QStringLiteral("↑导入Excel"), content);
    btnImport->setObjectName("forbiddenBtnImport");
    btnImport->setFixedHeight(32);
    auto* btnAiGen = new QPushButton(QStringLiteral("AI生成"), content);
    btnAiGen->setObjectName("forbiddenBtnAi");
    btnAiGen->setFixedHeight(32);
    auto* btnBatchDir = new QPushButton(QStringLiteral("批量导入目录"), content);
    btnBatchDir->setObjectName("forbiddenBtnBatchDir");
    btnBatchDir->setFixedHeight(32);
    auto* btnBatchEdit = new QPushButton(QStringLiteral("批量修改"), content);
    btnBatchEdit->setObjectName("forbiddenBtnBatchEdit");
    btnBatchEdit->setFixedHeight(32);
    auto* btnBatchDel = new QPushButton(QStringLiteral("批量删除"), content);
    btnBatchDel->setObjectName("forbiddenBtnBatchDel");
    btnBatchDel->setFixedHeight(32);
    auto* btnAdd = new QPushButton(QStringLiteral("+ 添加违禁词"), content);
    btnAdd->setObjectName("forbiddenBtnAdd");
    btnAdd->setFixedHeight(32);
    for (QPushButton* b : { btnExport, btnImport, btnAiGen, btnBatchDir, btnBatchEdit, btnBatchDel, btnAdd }) {
        topBar->addWidget(b);
        topBar->addSpacing(8);
    }
    mainLayout->addLayout(topBar);

    // 4. 平台筛选与搜索
    auto* filterRow = new QHBoxLayout();
    auto* platformLabel = new QLabel(QStringLiteral("平台："), content);
    platformLabel->setObjectName("forbiddenPlatformLabel");
    auto* platformCombo = new QComboBox(content);
    platformCombo->setObjectName("forbiddenPlatformCombo");
    platformCombo->addItem(QStringLiteral("全部平台"));
    platformCombo->addItem(QStringLiteral("通用"));
    platformCombo->addItem(QStringLiteral("千牛"));
    platformCombo->addItem(QStringLiteral("京东"));
    platformCombo->addItem(QStringLiteral("拼多多"));
    platformCombo->addItem(QStringLiteral("抖音"));
    platformCombo->addItem(QStringLiteral("小红书商家"));
    platformCombo->addItem(QStringLiteral("抖店"));
    platformCombo->setMinimumWidth(140);
    auto* ruleSearch = new QLineEdit(content);
    ruleSearch->setObjectName("forbiddenRuleSearch");
    ruleSearch->setPlaceholderText(QStringLiteral("搜索违禁词或替换词..."));
    ruleSearch->setClearButtonEnabled(false);
    ruleSearch->setMinimumWidth(220);
    filterRow->addWidget(platformLabel);
    filterRow->addWidget(platformCombo);
    filterRow->addStretch(1);
    filterRow->addWidget(ruleSearch);
    mainLayout->addLayout(filterRow);

    // 5. 统计卡片行（4 个）
    auto* cardRow = new QHBoxLayout();
    cardRow->setSpacing(12);
    auto* style = this->style();
    const int iconSz = 24;

    auto* cardTotal = makeCard(content, "forbiddenCardTotal");
    cardTotal->setMinimumHeight(88);
    auto* layTotal = new QHBoxLayout(cardTotal);
    layTotal->setContentsMargins(16, 16, 16, 16);
    auto* totalIcon = new QLabel(cardTotal);
    totalIcon->setPixmap(style->standardIcon(QStyle::SP_FileDialogListView).pixmap(iconSz, iconSz));
    auto* totalCol = new QVBoxLayout();
    auto* totalLabel = new QLabel(QStringLiteral("总数"), cardTotal);
    totalLabel->setObjectName("forbiddenCardLabel");
    auto* totalVal = new QLabel(QStringLiteral("3"), cardTotal);
    totalVal->setObjectName("forbiddenCardValue");
    totalCol->addWidget(totalLabel);
    totalCol->addWidget(totalVal);
    layTotal->addWidget(totalIcon);
    layTotal->addLayout(totalCol, 1);
    cardRow->addWidget(cardTotal, 1);

    auto* cardEnabled = makeCard(content, "forbiddenCardEnabled");
    cardEnabled->setMinimumHeight(88);
    auto* layEnabled = new QHBoxLayout(cardEnabled);
    layEnabled->setContentsMargins(16, 16, 16, 16);
    auto* enabledIcon = new QLabel(cardEnabled);
    enabledIcon->setPixmap(style->standardIcon(QStyle::SP_DialogOkButton).pixmap(iconSz, iconSz));
    auto* enabledCol = new QVBoxLayout();
    auto* enabledLabel = new QLabel(QStringLiteral("已启用"), cardEnabled);
    enabledLabel->setObjectName("forbiddenCardLabel");
    auto* enabledVal = new QLabel(QStringLiteral("3"), cardEnabled);
    enabledVal->setObjectName("forbiddenCardValue");
    enabledCol->addWidget(enabledLabel);
    enabledCol->addWidget(enabledVal);
    layEnabled->addWidget(enabledIcon);
    layEnabled->addLayout(enabledCol, 1);
    cardRow->addWidget(cardEnabled, 1);

    auto* cardDisabled = makeCard(content, "forbiddenCardDisabled");
    cardDisabled->setMinimumHeight(88);
    auto* layDisabled = new QHBoxLayout(cardDisabled);
    layDisabled->setContentsMargins(16, 16, 16, 16);
    auto* disabledIcon = new QLabel(cardDisabled);
    disabledIcon->setPixmap(style->standardIcon(QStyle::SP_DialogCloseButton).pixmap(iconSz, iconSz));
    auto* disabledCol = new QVBoxLayout();
    auto* disabledLabel = new QLabel(QStringLiteral("已禁用"), cardDisabled);
    disabledLabel->setObjectName("forbiddenCardLabel");
    auto* disabledVal = new QLabel(QStringLiteral("0"), cardDisabled);
    disabledVal->setObjectName("forbiddenCardValue");
    disabledCol->addWidget(disabledLabel);
    disabledCol->addWidget(disabledVal);
    layDisabled->addWidget(disabledIcon);
    layDisabled->addLayout(disabledCol, 1);
    cardRow->addWidget(cardDisabled, 1);

    auto* cardReplace = makeCard(content, "forbiddenCardReplace");
    cardReplace->setMinimumHeight(88);
    auto* layReplace = new QHBoxLayout(cardReplace);
    layReplace->setContentsMargins(16, 16, 16, 16);
    auto* replaceIcon = new QLabel(cardReplace);
    replaceIcon->setPixmap(style->standardIcon(QStyle::SP_BrowserReload).pixmap(iconSz, iconSz));
    auto* replaceCol = new QVBoxLayout();
    auto* replaceLabel = new QLabel(QStringLiteral("替换处理"), cardReplace);
    replaceLabel->setObjectName("forbiddenCardLabel");
    auto* replaceVal = new QLabel(QStringLiteral("3"), cardReplace);
    replaceVal->setObjectName("forbiddenCardValue");
    replaceCol->addWidget(replaceLabel);
    replaceCol->addWidget(replaceVal);
    layReplace->addWidget(replaceIcon);
    layReplace->addLayout(replaceCol, 1);
    cardRow->addWidget(cardReplace, 1);

    mainLayout->addLayout(cardRow);

    // 6. 违禁词规则表格（首列复选框 + 6 列数据）
    auto* table = new QTableWidget(content);
    table->setObjectName("forbiddenTable");
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({ QString(), QStringLiteral("平台"), QStringLiteral("违禁词"),
                                       QStringLiteral("替换词"), QStringLiteral("处理方式"), QStringLiteral("状态"), QStringLiteral("操作") });
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->setColumnWidth(0, 40);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QTableWidget::SelectRows);
    table->setEditTriggers(QTableWidget::NoEditTriggers);
    table->setMinimumHeight(200);
    table->setRowCount(3);

    struct ForbiddenRow { const char* platform; bool platformRed; const char* word; const char* replacement; };
    ForbiddenRow rows[] = { {"京东", true, "你", "您"}, {"通用", false, "最好", "非常好"}, {"京东", true, "你好", "您好"} };
    for (int r = 0; r < 3; ++r) {
        auto* check = new QCheckBox(table);
        check->setStyleSheet("margin-left: 8px;");
        table->setCellWidget(r, 0, check);
        auto* platformItem = new QTableWidgetItem(QString::fromUtf8(rows[r].platform));
        platformItem->setTextAlignment(Qt::AlignCenter);
        if (rows[r].platformRed)
            platformItem->setForeground(QColor(0xef, 0x44, 0x44));
        table->setItem(r, 1, platformItem);
        table->setItem(r, 2, new QTableWidgetItem(QString::fromUtf8(rows[r].word)));
        table->setItem(r, 3, new QTableWidgetItem(QString::fromUtf8(rows[r].replacement)));
        auto* methodItem = new QTableWidgetItem(QStringLiteral("替换"));
        methodItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(r, 4, methodItem);
        auto* statusItem = new QTableWidgetItem(QStringLiteral("启用"));
        statusItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setForeground(QColor(0x22, 0xc5, 0x5e));
        table->setItem(r, 5, statusItem);
        auto* opWidget = new QWidget(table);
        auto* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 2, 4, 2);
        opLayout->setSpacing(4);
        auto* btnEdit = new QPushButton(opWidget);
        btnEdit->setFixedSize(28, 28);
        btnEdit->setIcon(style->standardIcon(QStyle::SP_FileDialogContentsView));
        btnEdit->setIconSize(QSize(16, 16));
        btnEdit->setFlat(true);
        auto* btnDel = new QPushButton(opWidget);
        btnDel->setFixedSize(28, 28);
        btnDel->setIcon(style->standardIcon(QStyle::SP_TrashIcon));
        btnDel->setIconSize(QSize(16, 16));
        btnDel->setFlat(true);
        opLayout->addWidget(btnEdit);
        opLayout->addWidget(btnDel);
        table->setCellWidget(r, 6, opWidget);
    }
    mainLayout->addWidget(table, 1);

    scroll->setWidget(content);
    return scroll;
}

/**
 * @brief 构建对话历史页面（右侧主内容区之一）
 */
QWidget* RobotManageDialog::buildHistoryPage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与操作栏
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(QStringLiteral("对话历史"), content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(QStringLiteral("检索和分析历史对话"), content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);
    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    // 3. 对话历史管理模块：标题 + 副标题 + 3 标签 + 刷新/导出记录
    auto* mgmtHeader = new QHBoxLayout();
    auto* mgmtTitleCol = new QVBoxLayout();
    mgmtTitleCol->setSpacing(4);
    auto* mgmtTitle = new QLabel(QStringLiteral("对话历史管理"), content);
    mgmtTitle->setObjectName("historyMgmtTitle");
    auto* mgmtSub = new QLabel(QStringLiteral("查看和管理用户与机器人的对话记录"), content);
    mgmtSub->setObjectName("historyMgmtSub");
    mgmtTitleCol->addWidget(mgmtTitle);
    mgmtTitleCol->addWidget(mgmtSub);
    mgmtHeader->addLayout(mgmtTitleCol, 1);
    auto* tag1 = new QLabel(QStringLiteral("● 实时更新"), content);
    tag1->setObjectName("historyTagGreen");
    auto* tag2 = new QLabel(QStringLiteral("● 会话存储"), content);
    tag2->setObjectName("historyTagBlue");
    auto* tag3 = new QLabel(QStringLiteral("● 智能检索"), content);
    tag3->setObjectName("historyTagPurple");
    mgmtHeader->addWidget(tag1);
    mgmtHeader->addSpacing(8);
    mgmtHeader->addWidget(tag2);
    mgmtHeader->addSpacing(8);
    mgmtHeader->addWidget(tag3);
    mgmtHeader->addSpacing(16);
    auto* btnRefresh = new QPushButton(QStringLiteral("刷新"), content);
    btnRefresh->setObjectName("historyBtnRefresh");
    btnRefresh->setFixedHeight(32);
    btnRefresh->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    btnRefresh->setIconSize(QSize(16, 16));
    auto* btnExport = new QPushButton(QStringLiteral("导出记录"), content);
    btnExport->setObjectName("historyBtnExport");
    btnExport->setFixedHeight(32);
    mgmtHeader->addWidget(btnRefresh);
    mgmtHeader->addSpacing(8);
    mgmtHeader->addWidget(btnExport);
    mainLayout->addLayout(mgmtHeader);

    // 4. 统计卡片行（4 个白色卡片）
    auto* cardRow = new QHBoxLayout();
    cardRow->setSpacing(12);
    auto* style = this->style();
    const int iconSz = 24;

    auto* cardSessions = makeCard(content, "historyCardSessions");
    cardSessions->setMinimumHeight(100);
    auto* laySessions = new QVBoxLayout(cardSessions);
    laySessions->setContentsMargins(16, 16, 16, 16);
    auto* sessionsIcon = new QLabel(cardSessions);
    sessionsIcon->setPixmap(style->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(iconSz, iconSz));
    auto* sessionsTitle = new QLabel(QStringLiteral("会话总数"), cardSessions);
    sessionsTitle->setObjectName("historyCardLabel");
    auto* sessionsVal = new QLabel(QStringLiteral("0"), cardSessions);
    sessionsVal->setObjectName("historyCardValue");
    auto* sessionsSub = new QLabel(QStringLiteral("今日新增 0"), cardSessions);
    sessionsSub->setObjectName("historyCardSub");
    laySessions->addWidget(sessionsIcon);
    laySessions->addWidget(sessionsTitle);
    laySessions->addWidget(sessionsVal);
    laySessions->addWidget(sessionsSub);
    cardRow->addWidget(cardSessions, 1);

    auto* cardReception = makeCard(content, "historyCardReception");
    cardReception->setMinimumHeight(100);
    auto* layReception = new QVBoxLayout(cardReception);
    layReception->setContentsMargins(16, 16, 16, 16);
    auto* receptionIcon = new QLabel(cardReception);
    receptionIcon->setPixmap(style->standardIcon(QStyle::SP_ComputerIcon).pixmap(iconSz, iconSz));
    auto* receptionTitle = new QLabel(QStringLiteral("接待总数"), cardReception);
    receptionTitle->setObjectName("historyCardLabel");
    auto* receptionVal = new QLabel(QStringLiteral("0"), cardReception);
    receptionVal->setObjectName("historyCardValue");
    auto* receptionSub = new QLabel(QStringLiteral("独立用户数"), cardReception);
    receptionSub->setObjectName("historyCardSub");
    layReception->addWidget(receptionIcon);
    layReception->addWidget(receptionTitle);
    layReception->addWidget(receptionVal);
    layReception->addWidget(receptionSub);
    cardRow->addWidget(cardReception, 1);

    auto* cardAvg = makeCard(content, "historyCardAvg");
    cardAvg->setMinimumHeight(100);
    auto* layAvg = new QVBoxLayout(cardAvg);
    layAvg->setContentsMargins(16, 16, 16, 16);
    auto* avgIcon = new QLabel(cardAvg);
    avgIcon->setPixmap(style->standardIcon(QStyle::SP_FileDialogContentsView).pixmap(iconSz, iconSz));
    auto* avgTitle = new QLabel(QStringLiteral("平均对话数"), cardAvg);
    avgTitle->setObjectName("historyCardLabel");
    auto* avgVal = new QLabel(QStringLiteral("0"), cardAvg);
    avgVal->setObjectName("historyCardValue");
    auto* avgSub = new QLabel(QStringLiteral("每会话消息数"), cardAvg);
    avgSub->setObjectName("historyCardSub");
    layAvg->addWidget(avgIcon);
    layAvg->addWidget(avgTitle);
    layAvg->addWidget(avgVal);
    layAvg->addWidget(avgSub);
    cardRow->addWidget(cardAvg, 1);

    auto* cardTime = makeCard(content, "historyCardTime");
    cardTime->setMinimumHeight(100);
    auto* layTime = new QVBoxLayout(cardTime);
    layTime->setContentsMargins(16, 16, 16, 16);
    auto* timeIcon = new QLabel(cardTime);
    timeIcon->setPixmap(style->standardIcon(QStyle::SP_FileDialogInfoView).pixmap(iconSz, iconSz));
    auto* timeTitle = new QLabel(QStringLiteral("平均耗时"), cardTime);
    timeTitle->setObjectName("historyCardLabel");
    auto* timeVal = new QLabel(QStringLiteral("0"), cardTime);
    timeVal->setObjectName("historyCardValue");
    auto* timeSub = new QLabel(QStringLiteral("分钟/会话"), cardTime);
    timeSub->setObjectName("historyCardSub");
    layTime->addWidget(timeIcon);
    layTime->addWidget(timeTitle);
    layTime->addWidget(timeVal);
    layTime->addWidget(timeSub);
    cardRow->addWidget(cardTime, 1);

    mainLayout->addLayout(cardRow);

    // 5. 筛选栏
    auto* filterPanel = new QFrame(content);
    filterPanel->setObjectName("historyFilterPanel");
    auto* filterLayout = new QHBoxLayout(filterPanel);
    filterLayout->setContentsMargins(12, 8, 12, 8);
    filterLayout->setSpacing(8);
    auto* comboRobot = new QComboBox(content);
    comboRobot->setObjectName("historyComboRobot");
    comboRobot->addItem(QStringLiteral("选择机器人"));
    comboRobot->setMinimumWidth(120);
    auto* comboStatus = new QComboBox(content);
    comboStatus->setObjectName("historyComboStatus");
    comboStatus->addItem(QStringLiteral("全部状态"));
    comboStatus->addItem(QStringLiteral("活跃"));
    comboStatus->addItem(QStringLiteral("关闭"));
    comboStatus->addItem(QStringLiteral("已归档"));
    comboStatus->setMinimumWidth(100);
    auto* startDate = new QLineEdit(content);
    startDate->setObjectName("historyDateEdit");
    startDate->setPlaceholderText(QStringLiteral("开始日期"));
    startDate->setMinimumWidth(110);
    auto* toLabel = new QLabel(QStringLiteral("至"), content);
    toLabel->setObjectName("historyToLabel");
    auto* endDate = new QLineEdit(content);
    endDate->setObjectName("historyDateEdit");
    endDate->setPlaceholderText(QStringLiteral("结束日期"));
    endDate->setMinimumWidth(110);
    auto* sessionSearch = new QLineEdit(content);
    sessionSearch->setObjectName("historySessionSearch");
    sessionSearch->setPlaceholderText(QStringLiteral("搜索用户ID或会话内容..."));
    sessionSearch->setClearButtonEnabled(false);
    sessionSearch->setMinimumWidth(220);
    filterLayout->addWidget(comboRobot);
    filterLayout->addWidget(comboStatus);
    filterLayout->addWidget(startDate);
    filterLayout->addWidget(toLabel);
    filterLayout->addWidget(endDate);
    filterLayout->addStretch(1);
    filterLayout->addWidget(sessionSearch);
    mainLayout->addWidget(filterPanel);

    // 筛选栏右下角：共 0 个会话 + 实时数据
    auto* filterFooter = new QHBoxLayout();
    auto* countLabel = new QLabel(QStringLiteral("共 0 个会话"), content);
    countLabel->setObjectName("historyCountLabel");
    auto* realtimeLabel = new QLabel(QStringLiteral("● 实时数据"), content);
    realtimeLabel->setObjectName("historyRealtimeLabel");
    filterFooter->addStretch(1);
    filterFooter->addWidget(countLabel);
    filterFooter->addSpacing(12);
    filterFooter->addWidget(realtimeLabel);
    mainLayout->addLayout(filterFooter);

    // 6. 会话记录模块：标题 + 按时间排序 + 空状态
    auto* recordHeader = new QHBoxLayout();
    auto* recordTitle = new QLabel(QStringLiteral("会话记录"), content);
    recordTitle->setObjectName("historyRecordTitle");
    auto* sortLabel = new QLabel(QStringLiteral("按时间排序"), content);
    sortLabel->setObjectName("historySortLabel");
    recordHeader->addWidget(recordTitle);
    recordHeader->addStretch(1);
    recordHeader->addWidget(sortLabel);
    mainLayout->addLayout(recordHeader);

    auto* emptyPanel = makeCard(content, "historyEmptyPanel");
    emptyPanel->setObjectName("historyEmptyPanel");
    emptyPanel->setMinimumHeight(320);
    auto* emptyLayout = new QVBoxLayout(emptyPanel);
    emptyLayout->setContentsMargins(40, 40, 40, 40);
    emptyLayout->setSpacing(16);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto* emptyIconWrap = new QFrame(emptyPanel);
    emptyIconWrap->setObjectName("historyEmptyIconWrap");
    emptyIconWrap->setFixedSize(80, 80);
    emptyIconWrap->setStyleSheet("background: #e0f2fe; border-radius: 40px;");
    auto* emptyIconLay = new QVBoxLayout(emptyIconWrap);
    emptyIconLay->setAlignment(Qt::AlignCenter);
    auto* emptyIcon = new QLabel(emptyIconWrap);
    emptyIcon->setPixmap(style->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(40, 40));
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIconLay->addWidget(emptyIcon);
    auto* emptyTitle = new QLabel(QStringLiteral("暂无会话记录"), emptyPanel);
    emptyTitle->setObjectName("historyEmptyTitle");
    auto* emptySub = new QLabel(QStringLiteral("当前筛选条件下没有找到会话记录"), emptyPanel);
    emptySub->setObjectName("historyEmptySub");
    emptyLayout->addWidget(emptyIconWrap);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptySub);
    mainLayout->addWidget(emptyPanel, 1);

    scroll->setWidget(content);
    return scroll;
}

/**
 * @brief 构建数据备份页面
 */
#if 0
QWidget *RobotManageDialog::buildBackupPage()
{
    auto* scroll = buildCommonPage(QStringLiteral("数据备份"), QStringLiteral("数据导出与备份管理"));
    // ...
    return scroll;
}
#endif

QWidget *RobotManageDialog::buildBackupPage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与操作栏
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(QStringLiteral("数据备份"), content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(QStringLiteral("数据导出与备份管理"), content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);
    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    // 3. 智能提示条
    auto* tipPanel = new QFrame(content);
    tipPanel->setObjectName("backupTipPanel");
    tipPanel->setMinimumHeight(44);
    auto* tipLayout = new QVBoxLayout(tipPanel);
    tipLayout->setContentsMargins(12, 12, 12, 12);
    auto* tipLabel = new QLabel(QStringLiteral("智能数据保护·一键备份恢复·安全可靠的数据管理解决方案"), tipPanel);
    tipLabel->setObjectName("backupTipLabel");
    tipLabel->setAlignment(Qt::AlignCenter);
    tipLayout->addWidget(tipLabel);
    mainLayout->addWidget(tipPanel);

    // 4. 统计卡片行（4 个白色卡片）
    auto* cardRow = new QHBoxLayout();
    cardRow->setSpacing(12);
    auto* style = this->style();
    const int iconSz = 24;

    // 备份文件卡片
    auto* cardBackupFiles = makeCard(content, "backupCard");
    cardBackupFiles->setMinimumHeight(100);
    auto* layBackupFiles = new QVBoxLayout(cardBackupFiles);
    layBackupFiles->setContentsMargins(16, 16, 16, 16);
    auto* backupFilesIcon = new QLabel(cardBackupFiles);
    backupFilesIcon->setPixmap(style->standardIcon(QStyle::SP_FileIcon).pixmap(iconSz, iconSz));
    auto* backupFilesTitle = new QLabel(QStringLiteral("备份文件"), cardBackupFiles);
    backupFilesTitle->setObjectName("backupCardLabel");
    auto* backupFilesVal = new QLabel(QStringLiteral("0"), cardBackupFiles);
    backupFilesVal->setObjectName("backupCardValue");
    layBackupFiles->addWidget(backupFilesIcon);
    layBackupFiles->addWidget(backupFilesTitle);
    layBackupFiles->addWidget(backupFilesVal);
    cardRow->addWidget(cardBackupFiles, 1);

    // 总大小卡片
    auto* cardTotalSize = makeCard(content, "backupCard");
    cardTotalSize->setMinimumHeight(100);
    auto* layTotalSize = new QVBoxLayout(cardTotalSize);
    layTotalSize->setContentsMargins(16, 16, 16, 16);
    auto* totalSizeIcon = new QLabel(cardTotalSize);
    totalSizeIcon->setPixmap(style->standardIcon(QStyle::SP_DriveHDIcon).pixmap(iconSz, iconSz));
    auto* totalSizeTitle = new QLabel(QStringLiteral("总大小"), cardTotalSize);
    totalSizeTitle->setObjectName("backupCardLabel");
    auto* totalSizeVal = new QLabel(QStringLiteral("0 B"), cardTotalSize);
    totalSizeVal->setObjectName("backupCardValue");
    layTotalSize->addWidget(totalSizeIcon);
    layTotalSize->addWidget(totalSizeTitle);
    layTotalSize->addWidget(totalSizeVal);
    cardRow->addWidget(cardTotalSize, 1);

    // 有效备份卡片
    auto* cardValidBackups = makeCard(content, "backupCard");
    cardValidBackups->setMinimumHeight(100);
    auto* layValidBackups = new QVBoxLayout(cardValidBackups);
    layValidBackups->setContentsMargins(16, 16, 16, 16);
    auto* validBackupsIcon = new QLabel(cardValidBackups);
    validBackupsIcon->setPixmap(style->standardIcon(QStyle::SP_DialogApplyButton).pixmap(iconSz, iconSz));
    auto* validBackupsTitle = new QLabel(QStringLiteral("有效备份"), cardValidBackups);
    validBackupsTitle->setObjectName("backupCardLabel");
    auto* validBackupsVal = new QLabel(QStringLiteral("0"), cardValidBackups);
    validBackupsVal->setObjectName("backupCardValue");
    layValidBackups->addWidget(validBackupsIcon);
    layValidBackups->addWidget(validBackupsTitle);
    layValidBackups->addWidget(validBackupsVal);
    cardRow->addWidget(cardValidBackups, 1);

    // 最新备份卡片
    auto* cardLatestBackup = makeCard(content, "backupCard");
    cardLatestBackup->setMinimumHeight(100);
    auto* layLatestBackup = new QVBoxLayout(cardLatestBackup);
    layLatestBackup->setContentsMargins(16, 16, 16, 16);
    auto* latestBackupIcon = new QLabel(cardLatestBackup);
    latestBackupIcon->setPixmap(style->standardIcon(QStyle::SP_FileDialogInfoView).pixmap(iconSz, iconSz));
    auto* latestBackupTitle = new QLabel(QStringLiteral("最新备份"), cardLatestBackup);
    latestBackupTitle->setObjectName("backupCardLabel");
    auto* latestBackupVal = new QLabel(QStringLiteral("无"), cardLatestBackup);
    latestBackupVal->setObjectName("backupCardValue");
    layLatestBackup->addWidget(latestBackupIcon);
    layLatestBackup->addWidget(latestBackupTitle);
    layLatestBackup->addWidget(latestBackupVal);
    cardRow->addWidget(cardLatestBackup, 1);

    mainLayout->addLayout(cardRow);

    // 5. 操作按钮组
    auto* actionButtonsRow = new QHBoxLayout();
    actionButtonsRow->setSpacing(8);
    actionButtonsRow->setAlignment(Qt::AlignCenter);

    // 创建备份按钮
    auto* btnCreateBackup = new QPushButton(QStringLiteral("创建备份"), content);
    btnCreateBackup->setObjectName("backupBtnCreate");
    btnCreateBackup->setFixedHeight(40);
    btnCreateBackup->setIcon(style->standardIcon(QStyle::SP_FileDialogNewFolder));
    btnCreateBackup->setIconSize(QSize(16, 16));
    actionButtonsRow->addWidget(btnCreateBackup);

    // 导入备份按钮
    auto* btnImportBackup = new QPushButton(QStringLiteral("导入备份"), content);
    btnImportBackup->setObjectName("backupBtnImport");
    btnImportBackup->setFixedHeight(40);
    btnImportBackup->setIcon(style->standardIcon(QStyle::SP_ArrowUp));
    btnImportBackup->setIconSize(QSize(16, 16));
    actionButtonsRow->addWidget(btnImportBackup);

    // 刷新列表按钮
    auto* btnRefreshList = new QPushButton(QStringLiteral("刷新列表"), content);
    btnRefreshList->setObjectName("backupBtnRefresh");
    btnRefreshList->setFixedHeight(40);
    btnRefreshList->setIcon(style->standardIcon(QStyle::SP_BrowserReload));
    btnRefreshList->setIconSize(QSize(16, 16));
    actionButtonsRow->addWidget(btnRefreshList);

    mainLayout->addLayout(actionButtonsRow);

    // 6. 备份文件管理模块
    auto* mgmtHeader = new QHBoxLayout();
    auto* mgmtTitleCol = new QVBoxLayout();
    mgmtTitleCol->setSpacing(4);
    auto* mgmtTitle = new QLabel(QStringLiteral("备份文件管理"), content);
    mgmtTitle->setObjectName("backupMgmtTitle");
    mgmtTitleCol->addWidget(mgmtTitle);
    mgmtHeader->addLayout(mgmtTitleCol, 1);

    // 视图切换按钮组
    auto* btnListView = new QPushButton(content);
    btnListView->setObjectName("backupViewBtn");
    btnListView->setFixedSize(32, 32);
    btnListView->setIcon(style->standardIcon(QStyle::SP_FileDialogListView));
    btnListView->setIconSize(QSize(16, 16));
    btnListView->setCheckable(true);

    auto* btnCardView = new QPushButton(content);
    btnCardView->setObjectName("backupViewBtn");
    btnCardView->setFixedSize(32, 32);
    btnCardView->setIcon(style->standardIcon(QStyle::SP_FileDialogDetailedView));
    btnCardView->setIconSize(QSize(16, 16));
    btnCardView->setCheckable(true);
    btnCardView->setChecked(true); // 默认选中卡片视图

    mgmtHeader->addWidget(btnListView);
    mgmtHeader->addSpacing(4);
    mgmtHeader->addWidget(btnCardView);
    mainLayout->addLayout(mgmtHeader);

    // 7. 空状态区域
    auto* emptyPanel = makeCard(content, "backupEmptyPanel");
    emptyPanel->setObjectName("backupEmptyPanel");
    emptyPanel->setMinimumHeight(320);
    auto* emptyLayout = new QVBoxLayout(emptyPanel);
    emptyLayout->setContentsMargins(40, 40, 40, 40);
    emptyLayout->setSpacing(16);
    emptyLayout->setAlignment(Qt::AlignCenter);

    // 空状态图标
    auto* emptyIconWrap = new QFrame(emptyPanel);
    emptyIconWrap->setObjectName("backupEmptyIconWrap");
    emptyIconWrap->setFixedSize(80, 80);
    auto* emptyIconLay = new QVBoxLayout(emptyIconWrap);
    emptyIconLay->setAlignment(Qt::AlignCenter);
    auto* emptyIcon = new QLabel(emptyIconWrap);
    emptyIcon->setPixmap(style->standardIcon(QStyle::SP_ArrowUp).pixmap(40, 40));
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIconLay->addWidget(emptyIcon);

    // 空状态文字
    auto* emptyTitle = new QLabel(QStringLiteral("还没有备份文件"), emptyPanel);
    emptyTitle->setObjectName("backupEmptyTitle");
    auto* emptySub = new QLabel(QStringLiteral("创建您的第一个数据备份，保护重要数据安全"), emptyPanel);
    emptySub->setObjectName("backupEmptySub");

    // 立即创建备份按钮
    auto* btnCreateNow = new QPushButton(QStringLiteral("立即创建备份"), emptyPanel);
    btnCreateNow->setObjectName("backupBtnCreateNow");
    btnCreateNow->setFixedHeight(36);
    btnCreateNow->setIcon(style->standardIcon(QStyle::SP_FileDialogNewFolder));
    btnCreateNow->setIconSize(QSize(16, 16));

    emptyLayout->addWidget(emptyIconWrap);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptySub);
    emptyLayout->addWidget(btnCreateNow);

    mainLayout->addWidget(emptyPanel, 1);

    scroll->setWidget(content);
    return scroll;
}

QWidget* RobotManageDialog::buildLogPage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与操作栏
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(QStringLiteral("日志管理"), content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(QStringLiteral("系统日志查看与检索"), content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);
    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    // 3. 顶部紫色日志管理横幅
    auto* bannerPanel = new QFrame(content);
    bannerPanel->setObjectName("logBannerPanel");
    bannerPanel->setMinimumHeight(80);

    auto* bannerLayout = new QHBoxLayout(bannerPanel);
    bannerLayout->setContentsMargins(16, 16, 16, 16);

    // 左侧内容
    auto* bannerLeft = new QVBoxLayout();
    bannerLeft->setSpacing(4);

    auto* bannerTitleRow = new QHBoxLayout();
    bannerTitleRow->setSpacing(8);
    auto* bannerIcon = new QLabel(bannerPanel);
    auto* style = this->style();
    bannerIcon->setPixmap(style->standardIcon(QStyle::SP_FileDialogInfoView).pixmap(24, 24));

    auto* bannerTitle = new QLabel(QStringLiteral("日志管理"), bannerPanel);
    bannerTitle->setObjectName("logBannerTitle");

    bannerTitleRow->addWidget(bannerIcon);
    bannerTitleRow->addWidget(bannerTitle);
    bannerLeft->addLayout(bannerTitleRow);

    auto* bannerDesc = new QLabel(QStringLiteral("实时监控系统运行状态，查看操作记录和系统日志"), bannerPanel);
    bannerDesc->setObjectName("logBannerDesc");
    bannerLeft->addWidget(bannerDesc);

    bannerLayout->addLayout(bannerLeft, 1);

    // 右侧操作按钮
    auto* bannerBtnRefresh = new QPushButton(QStringLiteral("刷新数据"), bannerPanel);
    bannerBtnRefresh->setObjectName("logBannerBtnRefresh");
    bannerBtnRefresh->setFixedHeight(32);
    bannerBtnRefresh->setIcon(style->standardIcon(QStyle::SP_BrowserReload));
    bannerBtnRefresh->setIconSize(QSize(16, 16));

    auto* bannerBtnExport = new QPushButton(QStringLiteral("导出日志"), bannerPanel);
    bannerBtnExport->setObjectName("logBannerBtnExport");
    bannerBtnExport->setFixedHeight(32);
    bannerBtnExport->setIcon(style->standardIcon(QStyle::SP_DialogSaveButton));
    bannerBtnExport->setIconSize(QSize(16, 16));

    bannerLayout->addWidget(bannerBtnRefresh);
    bannerLayout->addSpacing(8);
    bannerLayout->addWidget(bannerBtnExport);

    mainLayout->addWidget(bannerPanel);

    // 4. 统计卡片行（4个白色卡片）
    auto* cardRow = new QHBoxLayout();
    cardRow->setSpacing(12);
    const int iconSz = 24;

    // 重要日志卡片
    auto* cardImportant = makeCard(content, "logCard");
    cardImportant->setMinimumHeight(120);
    auto* layImportant = new QVBoxLayout(cardImportant);
    layImportant->setContentsMargins(16, 16, 16, 12);

    auto* importantIconRow = new QHBoxLayout();
    auto* importantIcon = new QLabel(cardImportant);
    importantIcon->setPixmap(style->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(iconSz, iconSz));
    importantIconRow->addWidget(importantIcon);
    importantIconRow->addStretch(1);

    auto* importantTitle = new QLabel(QStringLiteral("重要日志"), cardImportant);
    importantTitle->setObjectName("logCardTitle");
    auto* importantValue = new QLabel(QStringLiteral("0"), cardImportant);
    importantValue->setObjectName("logCardValue");

    // 进度条
    auto* importantProgress = new QProgressBar(cardImportant);
    importantProgress->setObjectName("logProgressRed");
    importantProgress->setValue(0);
    importantProgress->setTextVisible(false);
    importantProgress->setMaximumHeight(4);

    layImportant->addLayout(importantIconRow);
    layImportant->addWidget(importantTitle);
    layImportant->addWidget(importantValue);
    layImportant->addWidget(importantProgress);
    cardRow->addWidget(cardImportant, 1);

    // 普通日志卡片
    auto* cardNormal = makeCard(content, "logCard");
    cardNormal->setMinimumHeight(120);
    auto* layNormal = new QVBoxLayout(cardNormal);
    layNormal->setContentsMargins(16, 16, 16, 12);

    auto* normalIconRow = new QHBoxLayout();
    auto* normalIcon = new QLabel(cardNormal);
    normalIcon->setPixmap(style->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(iconSz, iconSz));
    normalIconRow->addWidget(normalIcon);
    normalIconRow->addStretch(1);

    auto* normalTitle = new QLabel(QStringLiteral("普通日志"), cardNormal);
    normalTitle->setObjectName("logCardTitle");
    auto* normalValue = new QLabel(QStringLiteral("2"), cardNormal);
    normalValue->setObjectName("logCardValue");

    auto* normalProgress = new QProgressBar(cardNormal);
    normalProgress->setObjectName("logProgressBlue");
    normalProgress->setValue(100);
    normalProgress->setTextVisible(false);
    normalProgress->setMaximumHeight(4);

    layNormal->addLayout(normalIconRow);
    layNormal->addWidget(normalTitle);
    layNormal->addWidget(normalValue);
    layNormal->addWidget(normalProgress);
    cardRow->addWidget(cardNormal, 1);

    // 今日日志卡片
    auto* cardToday = makeCard(content, "logCard");
    cardToday->setMinimumHeight(120);
    auto* layToday = new QVBoxLayout(cardToday);
    layToday->setContentsMargins(16, 16, 16, 12);

    auto* todayIconRow = new QHBoxLayout();
    auto* todayIcon = new QLabel(cardToday);
    todayIcon->setPixmap(style->standardIcon(QStyle::SP_FileDialogInfoView).pixmap(iconSz, iconSz));
    todayIconRow->addWidget(todayIcon);
    todayIconRow->addStretch(1);

    auto* todayTitle = new QLabel(QStringLiteral("今日日志"), cardToday);
    todayTitle->setObjectName("logCardTitle");
    auto* todayValue = new QLabel(QStringLiteral("0"), cardToday);
    todayValue->setObjectName("logCardValue");

    auto* todayProgress = new QProgressBar(cardToday);
    todayProgress->setObjectName("logProgressGreen");
    todayProgress->setValue(0);
    todayProgress->setTextVisible(false);
    todayProgress->setMaximumHeight(4);

    layToday->addLayout(todayIconRow);
    layToday->addWidget(todayTitle);
    layToday->addWidget(todayValue);
    layToday->addWidget(todayProgress);
    cardRow->addWidget(cardToday, 1);

    // 总共日志卡片
    auto* cardTotal = makeCard(content, "logCard");
    cardTotal->setMinimumHeight(120);
    auto* layTotal = new QVBoxLayout(cardTotal);
    layTotal->setContentsMargins(16, 16, 16, 12);

    auto* totalIconRow = new QHBoxLayout();
    auto* totalIcon = new QLabel(cardTotal);
    totalIcon->setPixmap(style->standardIcon(QStyle::SP_FileIcon).pixmap(iconSz, iconSz));
    totalIconRow->addWidget(totalIcon);
    totalIconRow->addStretch(1);

    auto* totalTitle = new QLabel(QStringLiteral("总共日志"), cardTotal);
    totalTitle->setObjectName("logCardTitle");
    auto* totalValue = new QLabel(QStringLiteral("2"), cardTotal);
    totalValue->setObjectName("logCardValue");

    auto* totalProgress = new QProgressBar(cardTotal);
    totalProgress->setObjectName("logProgressPurple");
    totalProgress->setValue(100);
    totalProgress->setTextVisible(false);
    totalProgress->setMaximumHeight(4);

    layTotal->addLayout(totalIconRow);
    layTotal->addWidget(totalTitle);
    layTotal->addWidget(totalValue);
    layTotal->addWidget(totalProgress);
    cardRow->addWidget(cardTotal, 1);

    mainLayout->addLayout(cardRow);

    // 5. 操作日志模块
    // 模块标题
    auto* moduleHeader = new QHBoxLayout();
    auto* moduleTitleIcon = new QLabel(content);
    moduleTitleIcon->setPixmap(style->standardIcon(QStyle::SP_FileDialogListView).pixmap(20, 20));

    auto* moduleTitle = new QLabel(QStringLiteral("操作日志"), content);
    moduleTitle->setObjectName("logModuleTitle");

    moduleHeader->addWidget(moduleTitleIcon);
    moduleHeader->addWidget(moduleTitle);
    moduleHeader->addStretch(1);
    mainLayout->addLayout(moduleHeader);

    // 筛选栏
    auto* filterPanel = new QFrame(content);
    filterPanel->setObjectName("logFilterPanel");
    auto* filterLayout = new QHBoxLayout(filterPanel);
    filterLayout->setContentsMargins(12, 12, 12, 12);
    filterLayout->setSpacing(12);

    // 时间范围选择器（简化处理，使用QLineEdit）
    auto* timeStart = new QLineEdit(filterPanel);
    timeStart->setObjectName("logTimeStart");
    timeStart->setPlaceholderText(QStringLiteral("开始时间"));
    timeStart->setMinimumWidth(120);

    auto* toLabel = new QLabel(QStringLiteral("至"), filterPanel);
    toLabel->setObjectName("logToLabel");

    auto* timeEnd = new QLineEdit(filterPanel);
    timeEnd->setObjectName("logTimeEnd");
    timeEnd->setPlaceholderText(QStringLiteral("结束时间"));
    timeEnd->setMinimumWidth(120);

    // 操作类型下拉框
    auto* typeCombo = new QComboBox(filterPanel);
    typeCombo->setObjectName("logTypeCombo");
    typeCombo->addItem(QStringLiteral("全部类型"));
    typeCombo->addItem(QStringLiteral("登录"));
    typeCombo->addItem(QStringLiteral("创建"));
    typeCombo->addItem(QStringLiteral("修改"));
    typeCombo->addItem(QStringLiteral("删除"));
    typeCombo->addItem(QStringLiteral("查询"));
    typeCombo->setMinimumWidth(100);

    // 操作结果下拉框
    auto* resultCombo = new QComboBox(filterPanel);
    resultCombo->setObjectName("logResultCombo");
    resultCombo->addItem(QStringLiteral("全部结果"));
    resultCombo->addItem(QStringLiteral("成功"));
    resultCombo->addItem(QStringLiteral("失败"));
    resultCombo->setMinimumWidth(100);

    // 搜索框
    auto* logSearch = new QLineEdit(filterPanel);
    logSearch->setObjectName("logSearch");
    logSearch->setPlaceholderText(QStringLiteral("搜索操作描述或用户名"));
    logSearch->setClearButtonEnabled(false);
    logSearch->setMinimumWidth(200);

    // 查询和重置按钮
    auto* btnQuery = new QPushButton(QStringLiteral("查询"), filterPanel);
    btnQuery->setObjectName("logBtnQuery");
    btnQuery->setFixedHeight(32);

    auto* btnReset = new QPushButton(QStringLiteral("重置"), filterPanel);
    btnReset->setObjectName("logBtnReset");
    btnReset->setFixedHeight(32);

    // 添加到布局
    filterLayout->addWidget(timeStart);
    filterLayout->addWidget(toLabel);
    filterLayout->addWidget(timeEnd);
    filterLayout->addWidget(typeCombo);
    filterLayout->addWidget(resultCombo);
    filterLayout->addWidget(logSearch, 1);
    filterLayout->addWidget(btnQuery);
    filterLayout->addWidget(btnReset);

    mainLayout->addWidget(filterPanel);

    // 操作日志表格
    auto* logTable = new QTableWidget(content);
    logTable->setObjectName("logTable");
    logTable->setColumnCount(7);
    logTable->setHorizontalHeaderLabels({
        QStringLiteral("操作时间"),
        QStringLiteral("操作类型"),
        QStringLiteral("操作用户"),
        QStringLiteral("操作描述"),
        QStringLiteral("操作IP"),
        QStringLiteral("耗时"),
        QStringLiteral("状态")
    });
    logTable->setRowCount(5); // 示例数据5条
    logTable->setAlternatingRowColors(true);
    logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable->setSelectionMode(QAbstractItemView::SingleSelection);
    logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 设置列宽
    logTable->setColumnWidth(0, 140); // 操作时间
    logTable->setColumnWidth(1, 100); // 操作类型
    logTable->setColumnWidth(2, 80);  // 操作用户
    logTable->setColumnWidth(3, 200); // 操作描述
    logTable->setColumnWidth(4, 100); // 操作IP
    logTable->setColumnWidth(5, 60);  // 耗时
    logTable->setColumnWidth(6, 80);  // 状态

    // 添加示例数据（实际应用中应从数据源加载）
    QStringList times = {
        "2026/2/4 07:59:09",
        "2026/2/4 07:58:23",
        "2026/2/4 07:57:15",
        "2026/2/4 07:56:42",
        "2026/2/4 07:55:31"
    };

    QStringList types = {
        "删除",
        "创建",
        "修改",
        "查询",
        "登录"
    };

    QStringList users = {
        "系统",
        "管理员",
        "系统",
        "管理员",
        "系统"
    };

    QStringList descriptions = {
        "删除机器人: 客服助手",
        "创建知识库: 产品文档",
        "修改配置: 对话时长限制",
        "查询日志: 操作记录",
        "用户登录: admin"
    };

    QStringList ips = {
        "127.0.0.1",
        "192.168.1.100",
        "127.0.0.1",
        "192.168.1.101",
        "127.0.0.1"
    };

    QStringList durations = {
        "-",
        "-",
        "-",
        "-",
        "-"
    };

    QStringList statuses = {
        "成功",
        "成功",
        "成功",
        "成功",
        "成功"
    };

    for (int row = 0; row < 5; ++row) {
        logTable->setItem(row, 0, new QTableWidgetItem(times[row]));
        logTable->setItem(row, 1, new QTableWidgetItem(types[row]));
        logTable->setItem(row, 2, new QTableWidgetItem(users[row]));
        logTable->setItem(row, 3, new QTableWidgetItem(descriptions[row]));
        logTable->setItem(row, 4, new QTableWidgetItem(ips[row]));
        logTable->setItem(row, 5, new QTableWidgetItem(durations[row]));
        logTable->setItem(row, 6, new QTableWidgetItem(statuses[row]));

        // 设置文本居中
        for (int col = 0; col < 7; ++col) {
            QTableWidgetItem* item = logTable->item(row, col);
            if (item) {
                item->setTextAlignment(Qt::AlignCenter);
            }
        }
    }

    mainLayout->addWidget(logTable, 1);

    // 分页栏
    auto* paginationPanel = new QFrame(content);
    paginationPanel->setObjectName("logPaginationPanel");
    auto* paginationLayout = new QHBoxLayout(paginationPanel);
    paginationLayout->setContentsMargins(12, 8, 12, 8);

    // 左侧文字
    auto* pageInfo = new QLabel(QStringLiteral("共5条记录，当前显示第1-5条"), paginationPanel);
    pageInfo->setObjectName("logPageInfo");

    // 每页条数选择
    auto* pageSizeLabel = new QLabel(QStringLiteral("每页"), paginationPanel);
    pageSizeLabel->setObjectName("logPageSizeLabel");

    auto* pageSizeCombo = new QComboBox(paginationPanel);
    pageSizeCombo->setObjectName("logPageSizeCombo");
    pageSizeCombo->addItems({ "10条/页", "20条/页", "50条/页", "100条/页" });
    pageSizeCombo->setCurrentIndex(0);
    pageSizeCombo->setMaximumWidth(100);

    // 分页控件
    auto* btnPrev = new QPushButton(QStringLiteral("上一页"), paginationPanel);
    btnPrev->setObjectName("logBtnPrev");
    btnPrev->setFixedWidth(70);

    auto* btnPage1 = new QPushButton(QStringLiteral("1"), paginationPanel);
    btnPage1->setObjectName("logBtnPage");
    btnPage1->setFixedWidth(36);
    btnPage1->setCheckable(true);
    btnPage1->setChecked(true);

    auto* btnNext = new QPushButton(QStringLiteral("下一页"), paginationPanel);
    btnNext->setObjectName("logBtnNext");
    btnNext->setFixedWidth(70);

    auto* gotoLabel = new QLabel(QStringLiteral("前往"), paginationPanel);
    gotoLabel->setObjectName("logGotoLabel");

    auto* gotoInput = new QLineEdit(paginationPanel);
    gotoInput->setObjectName("logGotoInput");
    gotoInput->setFixedWidth(50);
    gotoInput->setAlignment(Qt::AlignCenter);

    auto* pageLabel = new QLabel(QStringLiteral("页"), paginationPanel);
    pageLabel->setObjectName("logPageLabel");

    // 添加到布局
    paginationLayout->addWidget(pageInfo);
    paginationLayout->addStretch(1);
    paginationLayout->addWidget(pageSizeLabel);
    paginationLayout->addWidget(pageSizeCombo);
    paginationLayout->addSpacing(20);
    paginationLayout->addWidget(btnPrev);
    paginationLayout->addWidget(btnPage1);
    paginationLayout->addWidget(btnNext);
    paginationLayout->addSpacing(20);
    paginationLayout->addWidget(gotoLabel);
    paginationLayout->addWidget(gotoInput);
    paginationLayout->addWidget(pageLabel);

    mainLayout->addWidget(paginationPanel);

    scroll->setWidget(content);
    return scroll;
}

#if 0
QScrollArea *RobotManageDialog::buildCommonPage(const QString &title, const QString &sub)
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("robotContentScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName("robotContentArea");
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // 1. 顶部标题与操作栏
    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);
    auto* pageTitle = new QLabel(title, content);
    pageTitle->setObjectName("robotPageTitle");
    auto* pageSub = new QLabel(sub, content);
    pageSub->setObjectName("robotPageSub");
    titleCol->addWidget(pageTitle);
    titleCol->addWidget(pageSub);
    headerRow->addLayout(titleCol, 1);
    headerRow->addSpacing(8);
    auto* btnAi = new QPushButton(QStringLiteral("AI配置"), content);
    btnAi->setObjectName("topBtnOrange");
    auto* btnAgg = new QPushButton(QStringLiteral("聚合对话"), content);
    btnAgg->setObjectName("topBtnPurple");
    auto* btnShare = new QPushButton(QStringLiteral("分享"), content);
    btnShare->setObjectName("topBtnBlue");
    auto* btnGuide = new QPushButton(QStringLiteral("使用向导"), content);
    btnGuide->setObjectName("topBtnPurple");
    auto* btnContact = new QPushButton(QStringLiteral("联系我们"), content);
    btnContact->setObjectName("topBtnGreen");
    for (QPushButton* b : { btnAi, btnAgg, btnShare, btnGuide, btnContact }) {
        b->setFixedHeight(32);
        headerRow->addWidget(b);
        headerRow->addSpacing(8);
    }
    mainLayout->addLayout(headerRow);

    // 2. 全局搜索框
    auto* globalSearch = new QLineEdit(content);
    globalSearch->setObjectName("robotGlobalSearch");
    globalSearch->setPlaceholderText(QStringLiteral("搜索功能..."));
    globalSearch->setClearButtonEnabled(false);
    mainLayout->addWidget(globalSearch);

    return scroll;
}
#endif
