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
    applyStyle();
}

void RobotManageDialog::buildUI()
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildLeftNav());
    rootLayout->addWidget(buildRightContent(), 1);
}

QFrame* RobotManageDialog::makeCard(QWidget* parent, const QString& objectName)
{
    auto* card = new QFrame(parent);
    if (!objectName.isEmpty())
        card->setObjectName(objectName);
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

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
    auto* titleLabel = new QLabel(QStringLiteral("AI客服系统 v1.4"), brand);
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
    todayLayout->setContentsMargins(16, 12, 16, 12);
    todayLayout->setSpacing(8);
    auto* todayTitle = new QLabel(QStringLiteral("今日概况"), todayCard);
    todayTitle->setObjectName("navStatTitle");
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
    todayLayout->addWidget(todayTitle);
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
    auto addItem = [this, style, iconSz](QStyle::StandardPixmap pix, const QString& text, const QString& id, bool selected) {
        auto* item = new QListWidgetItem(
            style->standardIcon(pix).pixmap(iconSz, iconSz),
            text,
            m_navList);
        item->setData(Qt::UserRole, id);
        if (selected)
            m_navList->setCurrentItem(item);
    };

    addGroup(QStringLiteral("核心功能"));
    addItem(QStyle::SP_FileIcon, QStringLiteral("系统概览"), QStringLiteral("overview"), false);
    addItem(QStyle::SP_ComputerIcon, QStringLiteral("机器人管理"), QStringLiteral("robot"), true);
    addItem(QStyle::SP_DirIcon, QStringLiteral("知识库管理"), QStringLiteral("knowledge"), false);
    addGroup(QStringLiteral("对话过程管理"));
    addItem(QStyle::SP_MessageBoxInformation, QStringLiteral("消息处理"), QStringLiteral("message"), false);
    addItem(QStyle::SP_BrowserReload, QStringLiteral("行话转换"), QStringLiteral("jargon"), false);
    addItem(QStyle::SP_MessageBoxCritical, QStringLiteral("违禁词管理"), QStringLiteral("forbidden"), false);
    addItem(QStyle::SP_ArrowBack, QStringLiteral("对话历史"), QStringLiteral("history"), false);
    addGroup(QStringLiteral("系统管理"));
    addItem(QStyle::SP_DriveHDIcon, QStringLiteral("数据备份"), QStringLiteral("backup"), false);
    addItem(QStyle::SP_FileDialogListView, QStringLiteral("日志管理"), QStringLiteral("log"), false);

    layout->addWidget(m_navList, 1);
    return nav;
}

/**
 * @brief 构建右侧白色主内容区
 */
QWidget* RobotManageDialog::buildRightContent()
{
    m_contentScroll = new QScrollArea(this);
    m_contentScroll->setObjectName("robotContentScroll");
    m_contentScroll->setWidgetResizable(true);
    m_contentScroll->setFrameShape(QFrame::NoFrame);
    m_contentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(m_contentScroll);
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
    comboIndustry->addItem(QStringLiteral("筛选行业"));
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

    m_contentScroll->setWidget(content);
    return m_contentScroll;
}

void RobotManageDialog::applyStyle()
{
    setStyleSheet(QStringLiteral(R"QSS(
        QDialog { background: #ffffff; }
        /* 左侧导航栏 #25262b */
        QWidget#robotNavSidebar { background: #25262b; }
        QWidget#navBrand { background: #25262b; }
        QLabel#navBrandTitle { color: #ffffff; font-size: 16px; font-weight: bold; }
        QLabel#navBrandSub { color: #ffffff; font-size: 12px; }
        QFrame#navDivider { background: #3d3e44; max-height: 1px; }
        QFrame#navStatCard { background: transparent; }
        QLabel#navStatTitle { color: #b0b1b6; font-size: 11px; }
        QLabel#navStatLabel { color: #ffffff; font-size: 12px; }
        QLabel#navStatValue { color: #ffffff; font-size: 12px; }
        QLabel#navStatValueRed { color: #f56c6c; font-size: 12px; font-weight: bold; }
        QLabel#navPowerIcon { color: #ff7d00; font-size: 14px; }
        QFrame#navStatDivider { background: #3d3e44; }
        QListWidget#robotNavList {
            background: #25262b; border: none; outline: none;
        }
        QListWidget#robotNavList::item {
            color: #ffffff; padding: 10px 16px; background: transparent;
        }
        QListWidget#robotNavList::item:hover:!selected {
            background: #2d2e33;
        }
        QListWidget#robotNavList::item:selected {
            background: #3a3b40; color: #ffffff;
        }

        /* 右侧内容区 */
        QScrollArea#robotContentScroll { background: #ffffff; }
        QWidget#robotContentArea { background: #ffffff; }
        QLabel#robotPageTitle { color: #1d1d1f; font-size: 18px; font-weight: bold; }
        QLabel#robotPageSub { color: #8a8b90; font-size: 12px; }
        QPushButton#topBtnOrange { background: #ff7d00; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QPushButton#topBtnPurple { background: #9254de; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QPushButton#topBtnBlue { background: #4080ff; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QPushButton#topBtnGreen { background: #00b42a; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QLineEdit#robotGlobalSearch, QLineEdit#robotFilterSearch {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 8px 12px; color: #1d1d1f;
        }
        QComboBox#robotFilterCombo {
            background: #ffffff; border: 1px solid #e5e6eb; border-radius: 4px;
            padding: 6px 12px; min-height: 20px; color: #1d1d1f;
        }
        QFrame#statCardBlue { background: #e6f4ff; border-radius: 8px; }
        QFrame#statCardGreen { background: #f0fdf4; border-radius: 8px; }
        QFrame#statCardPurple { background: #faf5ff; border-radius: 8px; }
        QFrame#statCardOrange { background: #fff7ed; border-radius: 8px; }
        QFrame#statCardBlue QLabel#robotStatCardTitle,
        QFrame#statCardGreen QLabel#robotStatCardTitle,
        QFrame#statCardPurple QLabel#robotStatCardTitle,
        QFrame#statCardOrange QLabel#robotStatCardTitle {
            color: #1d1d1f; font-size: 14px; font-weight: bold;
        }
        QFrame#statCardBlue QLabel#robotStatCardSub,
        QFrame#statCardGreen QLabel#robotStatCardSub,
        QFrame#statCardPurple QLabel#robotStatCardSub,
        QFrame#statCardOrange QLabel#robotStatCardSub {
            color: #8a8b90; font-size: 12px;
        }
        QPushButton#filterBtnOrange { background: #ff7d00; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QPushButton#filterBtnPurple { background: #9254de; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QPushButton#filterBtnBlue { background: #4080ff; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QPushButton#filterBtnGreen { background: #00b42a; color: #ffffff; border: none; border-radius: 4px; padding: 0 16px; }
        QPushButton#robotCreateBtn { background: #1d1d1f; color: #ffffff; border: none; border-radius: 8px; padding: 0 20px; font-weight: bold; }
        QPushButton#robotCreateBtn:hover { background: #3d3d3f; }
        QFrame#robotEmptyPanel { background: #ffffff; border: 1px solid #e5e6eb; border-radius: 8px; }
        QLabel#robotEmptyTitle { color: #1d1d1f; font-size: 16px; font-weight: bold; }
        QLabel#robotEmptySub { color: #8a8b90; font-size: 12px; }
    )QSS"));
}
