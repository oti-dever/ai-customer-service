#include "SettingDialog.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollArea>
#include <QGroupBox>
#include <QTabWidget>
#include <QDate>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QScreen>
#include <QScrollBar>
#include <QButtonGroup>
#include <QRadioButton>
#include <QSplitter>
#include <QStyle>
#include <QIcon>

/**
 * @brief 设置对话框构造函数
 * @param parent 父窗口
 */
SettingDialog::SettingDialog(QWidget* parent)
    : QDialog(parent)
{
    // 设置窗口属性
    setWindowModality(Qt::NonModal);
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint | Qt::WindowTitleHint);
    setWindowTitle(QStringLiteral("管理后台"));

    // 自适应屏幕比例，设置合理尺寸；窗口可缩放以适配不同分辨率
    const QSize screenSize = qApp->primaryScreen()->availableSize();
    resize(screenSize.width() * 0.7, screenSize.height() * 0.75);
    setMinimumSize(900, 560);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 构建UI
    buildUI();
    applyStyle();
}

/**
 * @brief 构建对话框UI
 */
void SettingDialog::buildUI()
{
    // 根布局
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 不再添加自定义“管理后台”顶栏，直接使用系统窗口标题栏

    // 主内容区域（水平布局：左侧导航 + 右侧内容，随窗口自适应缩放）
    auto* mainWidget = new QWidget(this);
    mainWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* mainLayout = new QHBoxLayout(mainWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 左侧导航栏（固定宽度）
    mainLayout->addWidget(buildLeftSidebar());

    // 右侧内容区域（占剩余宽度，可伸缩）
    QWidget* rightContent = buildRightContent();
    rightContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(rightContent, 1);

    rootLayout->addWidget(mainWidget, 1);

    // 底部按钮栏
    auto* bottomBar = new QFrame(this);
    bottomBar->setObjectName("bottomBar");
    bottomBar->setFixedHeight(60);
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(20, 0, 20, 0);
    bottomLayout->setAlignment(Qt::AlignRight);

    auto* resetBtn = new QPushButton(QStringLiteral("重置"), bottomBar);
    resetBtn->setObjectName("resetBtn");
    resetBtn->setFixedHeight(32);

    auto* saveBtn = new QPushButton(QStringLiteral("保存配置"), bottomBar);
    saveBtn->setObjectName("saveBtn");
    saveBtn->setFixedHeight(32);

    bottomLayout->addStretch();
    bottomLayout->addWidget(resetBtn);
    bottomLayout->addWidget(saveBtn);

    rootLayout->addWidget(bottomBar);
}

/**
 * @brief 构建顶部标题栏
 * @return 标题栏组件指针
 */
QWidget* SettingDialog::buildHeader()
{
    auto* header = new QFrame(this);
    header->setObjectName("header");
    header->setFixedHeight(50);

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(20, 0, 20, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("管理后台"), header);
    title->setObjectName("headerTitle");

    layout->addWidget(title);
    layout->addStretch();

    // 窗口控制按钮（最小化、最大化、关闭）
    auto* minBtn = new QPushButton(QStringLiteral("—"), header);
    minBtn->setObjectName("windowControlBtn");
    minBtn->setFixedSize(30, 30);
    minBtn->setToolTip(QStringLiteral("最小化"));

    auto* maxBtn = new QPushButton(QStringLiteral("□"), header);
    maxBtn->setObjectName("windowControlBtn");
    maxBtn->setFixedSize(30, 30);
    maxBtn->setToolTip(QStringLiteral("最大化"));

    auto* closeBtn = new QPushButton(QStringLiteral("×"), header);
    closeBtn->setObjectName("windowControlBtn");
    closeBtn->setFixedSize(30, 30);
    closeBtn->setToolTip(QStringLiteral("关闭"));

    layout->addWidget(minBtn);
    layout->addWidget(maxBtn);
    layout->addWidget(closeBtn);

    // 连接窗口控制按钮
    connect(minBtn, &QPushButton::clicked, this, &QDialog::showMinimized);
    connect(maxBtn, &QPushButton::clicked, this, [this, maxBtn]() {
        if (isMaximized()) {
            showNormal();
            maxBtn->setText(QStringLiteral("□"));
        } else {
            showMaximized();
            maxBtn->setText(QStringLiteral("❐"));
        }
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    return header;
}

/**
 * @brief 构建左侧导航栏
 * @return 导航栏组件指针
 */
QWidget* SettingDialog::buildLeftSidebar()
{
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("leftSidebar");
    sidebar->setFixedWidth(220);

    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 导航标题（文档：智能回复设置 白色粗体14px，左侧小图标）
    auto* navTitleRow = new QWidget(sidebar);
    navTitleRow->setObjectName("navTitleRow");
    auto* navTitleLayout = new QHBoxLayout(navTitleRow);
    navTitleLayout->setContentsMargins(20, 16, 20, 12);
    navTitleLayout->setSpacing(8);
    auto* navTitleIcon = new QLabel(navTitleRow);
    navTitleIcon->setPixmap(style()->standardIcon(QStyle::SP_FileDialogDetailedView).pixmap(16, 16));
    navTitleIcon->setStyleSheet("background: transparent;");
    auto* navTitle = new QLabel(QStringLiteral("智能回复设置"), navTitleRow);
    navTitle->setObjectName("navTitle");
    navTitleLayout->addWidget(navTitleIcon);
    navTitleLayout->addWidget(navTitle);
    navTitleLayout->addStretch();
    layout->addWidget(navTitleRow);
    // 下方浅灰色细分割线（文档）
    auto* navDivider = new QFrame(sidebar);
    navDivider->setObjectName("navDivider");
    navDivider->setFixedHeight(1);
    layout->addWidget(navDivider);

    // 导航列表
    m_navList = new QListWidget(sidebar);
    m_navList->setObjectName("navList");
    m_navList->setFrameShape(QFrame::NoFrame);

    // 添加导航项（与文档「完整菜单列表」一致）
    QStringList navItems = {
        QStringLiteral("简易AI (支持FastGPT/Dify)"),
        QStringLiteral("AI配置 (OpenAI通用格式)"),
        QStringLiteral("首响提速"),
        QStringLiteral("关键词规则"),
        QStringLiteral("内容替换"),
        QStringLiteral("默认回复"),
        QStringLiteral("消息推送"),
        QStringLiteral("线索列表")
    };

    auto* style = this->style();
    QVector<QStyle::StandardPixmap> navIcons = {
        QStyle::SP_MessageBoxInformation,  /* 简易AI - 闪电用信息图标占位 */
        QStyle::SP_FileDialogDetailedView, /* AI配置 - 齿轮用设置类图标占位 */
        QStyle::SP_ArrowUp,                /* 首响提速 */
        QStyle::SP_FileIcon,               /* 关键词规则 */
        QStyle::SP_BrowserReload,          /* 内容替换 */
        QStyle::SP_MessageBoxInformation,  /* 默认回复 */
        QStyle::SP_MessageBoxInformation,  /* 消息推送 - 铃铛用信息占位 */
        QStyle::SP_FileDialogListView      /* 线索列表 */
    };
    const int iconSize = 18;
    for (int i = 0; i < navItems.size(); ++i) {
        auto* listItem = new QListWidgetItem(
            style->standardIcon(i < navIcons.size() ? navIcons[i] : QStyle::SP_FileIcon)
                .pixmap(iconSize, iconSize),
            navItems[i],
            m_navList);
        listItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        listItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }

    layout->addWidget(m_navList, 1);

    // 连接导航项选择信号
    connect(m_navList, &QListWidget::currentRowChanged, this, &SettingDialog::onNavigationItemChanged);

    // 默认选中第一项
    m_navList->setCurrentRow(0);

    return sidebar;
}

/**
 * @brief 构建右侧内容区域
 * @return 内容区域组件指针
 */
QWidget* SettingDialog::buildRightContent()
{
    // 创建滚动区域
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("scrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 创建内容堆叠窗口（作为滚动区域的子对象）
    m_contentStack = new QStackedWidget(m_scrollArea);

    // 添加各个设置页面
    m_contentStack->addWidget(buildSimpleAIPage());      // 0: 简易AI
    m_contentStack->addWidget(buildAIConfigPage());     // 1: AI配置
    m_contentStack->addWidget(buildFirstResponsePage()); // 2: 首响提速
    m_contentStack->addWidget(buildKeywordRulesPage());  // 3: 关键词规则
    m_contentStack->addWidget(buildContentReplacePage()); // 4: 内容替换
    m_contentStack->addWidget(buildDefaultReplyPage());  // 5: 默认回复
    m_contentStack->addWidget(buildMessagePushPage());    // 6: 消息推送
    m_contentStack->addWidget(buildLeadListPage());       // 7: 线索列表

    // 将堆叠窗口放入滚动区域
    m_scrollArea->setWidget(m_contentStack);

    return m_scrollArea;
}

/**
 * @brief 创建卡片容器
 * @param parent 父组件
 * @return 卡片Frame指针
 */
QFrame* SettingDialog::makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName("card");
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

/**
 * @brief 处理导航项选择变化
 * @param index 选中的索引
 */
void SettingDialog::onNavigationItemChanged(int index)
{
    if (m_contentStack && index >= 0 && index < m_contentStack->count()) {
        m_contentStack->setCurrentIndex(index);
        // 滚动到顶部
        if (m_scrollArea && m_scrollArea->verticalScrollBar()) {
            m_scrollArea->verticalScrollBar()->setValue(0);
        }
    }
}

/**
 * @brief 应用样式表（配色参考管理后台智能回复设置界面）
 */
void SettingDialog::applyStyle()
{
    setStyleSheet(QStringLiteral(R"QSS(
        /* 主窗口：白色主内容区 */
        QDialog {
            background: #ffffff;
        }

        /* 顶部标题栏：白底深色字 */
        QFrame#header {
            background: #ffffff;
            border-bottom: 1px solid #E4E7ED;
        }
        QLabel#headerTitle {
            color: #303133;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton#windowControlBtn {
            background: transparent;
            border: none;
            color: #909399;
            font-size: 14px;
        }
        QPushButton#windowControlBtn:hover {
            background: #F5F7FA;
            color: #303133;
        }

        /* 左侧导航栏：文档 深黑蓝 #1a1d29，选中项蓝色 #2563eb */
        QWidget#leftSidebar {
            background: #1a1d29;
        }
        QWidget#navTitleRow {
            background: #1a1d29;
        }
        QLabel#navTitle {
            color: #ffffff;
            font-size: 14px;
            font-weight: bold;
            background: transparent;
        }
        QFrame#navDivider {
            background: #3d4152;
        }
        QListWidget#navList {
            background: #1a1d29;
            border: none;
            outline: none;
        }
        QListWidget#navList::item {
            color: #ffffff;
            padding: 12px 20px;
            border: none;
            border-left: 3px solid transparent;
            background: transparent;
        }
        QListWidget#navList::item:hover {
            background: #252836;
            color: #ffffff;
        }
        QListWidget#navList::item:selected {
            background: #2563eb;
            color: #ffffff;
            border-left: 3px solid #1d4ed8;
        }

        /* 右侧内容区 */
        QScrollArea#scrollArea {
            background: #ffffff;
            border: none;
        }
        QScrollArea#scrollArea QWidget {
            background: #ffffff;
        }
        /* 内容区所有标签默认深色字（避免未设 objectName 的标签如“适用平台”显示为白字） */
        QScrollArea#scrollArea QLabel {
            color: #303133;
        }

        /* 页面标题 */
        QLabel#pageTitle {
            font-size: 18px;
            font-weight: bold;
            color: #303133;
        }

        /* 卡片：白底、浅灰边框、圆角 */
        QFrame#card {
            background: #ffffff;
            border-radius: 8px;
            border: 1px solid #DCDFE6;
        }

        /* 顶部提示条 - 文档 浅蓝 #e0f2fe，圆角4px */
        QFrame#tipBar {
            background: #e0f2fe;
            border: 1px solid #bae6fd;
            border-radius: 4px;
        }
        QFrame#tipBar QLabel {
            color: #606266;
        }
        QFrame#tipBar QPushButton {
            background: transparent;
            border: none;
            color: #909399;
            font-size: 16px;
        }
        QFrame#tipBar QPushButton:hover {
            color: #303133;
        }

        /* 算力余额警示文案（橘红色粗体） */
        QLabel#balanceWarning {
            font-size: 16px;
            color: #E6A23C;
            font-weight: bold;
        }
        /* 可用变量标签（浅蓝底深色字，保证常显不发白） */
        QPushButton#variableBtn {
            background-color: #A9D1ED;
            border: none;
            border-radius: 4px;
            color: #303133;
            padding: 6px 12px;
            font-size: 12px;
        }
        QScrollArea#scrollArea QPushButton#variableBtn {
            background-color: #A9D1ED;
            color: #303133;
        }
        QPushButton#variableBtn:hover {
            background-color: #7BAED9;
            color: #ffffff;
        }
        QScrollArea#scrollArea QPushButton#variableBtn:hover {
            background-color: #7BAED9;
            color: #ffffff;
        }

        /* 内容区输入框、下拉框、多行文本：白底、浅灰边框、圆角 */
        QScrollArea#scrollArea QLineEdit,
        QScrollArea#scrollArea QComboBox,
        QScrollArea#scrollArea QSpinBox,
        QScrollArea#scrollArea QDateEdit,
        QScrollArea#scrollArea QDateTimeEdit {
            background: #ffffff;
            border: 1px solid #DCDFE6;
            border-radius: 4px;
            color: #303133;
            padding: 6px 10px;
            min-height: 18px;
        }
        QScrollArea#scrollArea QLineEdit:focus,
        QScrollArea#scrollArea QComboBox:focus,
        QScrollArea#scrollArea QSpinBox:focus {
            border: 1px solid #5B9BD5;
        }
        QScrollArea#scrollArea QComboBox::drop-down {
            border: none;
            background: transparent;
            width: 20px;
        }
        QScrollArea#scrollArea QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #909399;
            margin-right: 6px;
            width: 0;
            height: 0;
        }
        /* 下拉列表项：白底深色字（避免下拉内容为白字不可见） */
        QComboBox QAbstractItemView {
            background-color: #ffffff;
            color: #303133;
            selection-background-color: #E1EFF9;
            selection-color: #303133;
        }
        QScrollArea#scrollArea QTextEdit {
            background: #ffffff;
            border: 1px solid #DCDFE6;
            border-radius: 4px;
            color: #303133;
            padding: 8px;
        }
        QScrollArea#scrollArea QTextEdit:focus {
            border: 1px solid #5B9BD5;
        }

        /* 标签页栏 - 文档 基础设置选中白底+浅灰边框，未选中浅灰背景 */
        QTabWidget::pane {
            border: 1px solid #e5e7eb;
            border-radius: 4px;
            border-top: none;
            top: -1px;
            background: #ffffff;
        }
        QTabBar::tab {
            background: #f3f4f6;
            color: #303133;
            padding: 10px 20px;
            margin-right: 4px;
            border-radius: 4px 4px 0 0;
        }
        QTabBar::tab:selected {
            background: #ffffff;
            color: #303133;
            font-weight: bold;
            border: 1px solid #e5e7eb;
            border-bottom: 1px solid #ffffff;
        }
        QTabBar::tab:hover:!selected {
            color: #2563eb;
        }

        /* 卡片标题（各页统一） */
        QLabel#cardTitle {
            font-size: 14px;
            font-weight: bold;
            color: #303133;
        }
        /* 辅助说明文字 */
        QLabel#hintLabel {
            color: #909399;
            font-size: 12px;
        }

        /* 算力余额 - 文档 浅蓝 #e0f2fe，圆角4px */
        QFrame#balancePanel {
            background: #e0f2fe;
            border: 1px solid #bae6fd;
            border-radius: 4px;
        }

        /* 红色警示条（不携带说明书等） */
        QFrame#dangerCard {
            background: #FEE8E7;
            border: 1px solid #FBC4C4;
            border-radius: 8px;
        }
        QFrame#dangerCard QLabel {
            color: #A94442;
        }

        /* 复选框：开关风格，选中为柔和蓝 */
        QCheckBox {
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 44px;
            height: 22px;
            border-radius: 11px;
            background: #DCDFE6;
            border: none;
        }
        QCheckBox::indicator:checked {
            background: #5B9BD5;
        }
        QCheckBox::indicator:hover {
            background: #C0C4CC;
        }
        QCheckBox::indicator:checked:hover {
            background: #7BAED9;
        }

        /* 表格 */
        QTableWidget {
            background: #ffffff;
            border: 1px solid #DCDFE6;
            border-radius: 4px;
            gridline-color: #EBEEF5;
        }
        QTableWidget::item {
            padding: 8px;
            color: #303133;
        }
        QTableWidget::item:selected {
            background: #E1EFF9;
            color: #303133;
        }
        QHeaderView::section {
            background: #F5F7FA;
            color: #606266;
            padding: 10px 8px;
            border: none;
            border-bottom: 1px solid #EBEEF5;
            border-right: 1px solid #EBEEF5;
        }

        /* 滚动条 */
        QScrollBar:vertical {
            background: #F5F7FA;
            width: 8px;
            border-radius: 4px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #C0C4CC;
            border-radius: 4px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background: #909399;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }

        /* 底部操作栏 - 文档 按钮高度32px，保存配置 #22c55e */
        QFrame#bottomBar {
            background: #ffffff;
            border-top: 1px solid #E4E7ED;
        }
        QPushButton#resetBtn {
            background: #ffffff;
            border: 1px solid #DCDFE6;
            border-radius: 4px;
            color: #303133;
            font-size: 14px;
            padding: 0 16px;
            min-height: 32px;
        }
        QPushButton#resetBtn:hover {
            background: #F5F7FA;
            color: #2563eb;
            border-color: #93c5fd;
        }
        QPushButton#saveBtn {
            background: #22c55e;
            border: none;
            border-radius: 4px;
            color: #ffffff;
            font-size: 14px;
            font-weight: bold;
            padding: 0 16px;
            min-height: 32px;
        }
        QPushButton#saveBtn:hover {
            background: #16a34a;
        }

        /* 内容区次要按钮（恢复默认、清空、导出Excel 等）- 强制可见，避免发白 */
        QPushButton#secondaryBtn {
            background-color: #ffffff;
            border: 1px solid #DCDFE6;
            border-radius: 4px;
            color: #606266;
            padding: 5px 12px;
        }
        QPushButton#secondaryBtn:hover {
            background-color: #F5F7FA;
            color: #5B9BD5;
            border-color: #C6E2FF;
        }
        QScrollArea#scrollArea QPushButton#secondaryBtn {
            background-color: #ffffff;
            color: #606266;
            border: 1px solid #DCDFE6;
        }
        QScrollArea#scrollArea QPushButton#secondaryBtn:hover {
            background-color: #F5F7FA;
            color: #5B9BD5;
        }

        /* 主操作按钮（添加规则、添加说明书、查询、发送测试推送 等）- 强制蓝底白字可见 */
        QPushButton#primaryBtn {
            background-color: #5B9BD5;
            border: none;
            border-radius: 4px;
            color: #ffffff;
            padding: 8px 15px;
        }
        QPushButton#primaryBtn:hover {
            background-color: #7BAED9;
            color: #ffffff;
        }
        QScrollArea#scrollArea QPushButton#primaryBtn {
            background-color: #5B9BD5;
            color: #ffffff;
            border: none;
        }
        QScrollArea#scrollArea QPushButton#primaryBtn:hover {
            background-color: #7BAED9;
            color: #ffffff;
        }

        /* 功能说明折叠按钮（三角图标） */
        QPushButton#collapseBtn {
            background: transparent;
            border: none;
            color: #909399;
            font-size: 12px;
        }
        QPushButton#collapseBtn:hover {
            color: #606266;
        }

        /* 温和色强调链接蓝 */
        QLabel#linkLabel {
            color: #5B9BD5;
            font-size: 12px;
        }
        /* 空状态提示（暂无数据） */
        QLabel#emptyStateLabel {
            color: #909399;
            font-size: 14px;
        }
        /* 线索列表：日期范围单框（一个框内 开始 至 结束） */
        QFrame#dateRangeBox {
            background: #ffffff;
            border: 1px solid #DCDFE6;
            border-radius: 4px;
            min-height: 18px;
        }
        QFrame#dateRangeBox QDateEdit {
            border: none;
            background: transparent;
            min-width: 90px;
        }
        QFrame#dateRangeBox QLabel {
            color: #909399;
        }

        /* 日历弹层：强制深色数字与白底，避免受全局样式影响导致数字发白不可见 */
        QCalendarWidget QWidget {
            background-color: #ffffff;
            color: #303133;
        }
        QCalendarWidget QTableView {
            background-color: #ffffff;
            color: #303133;
            gridline-color: #EBEEF5;
        }
        QCalendarWidget QTableView::item {
            color: #303133;
            background-color: #ffffff;
        }
        QCalendarWidget QTableView::item:hover {
            background-color: #F5F7FA;
            color: #303133;
        }
        QCalendarWidget QTableView::item:selected {
            background-color: #5B9BD5;
            color: #ffffff;
        }
        QCalendarWidget QToolButton {
            background-color: #ffffff;
            color: #303133;
            border: none;
        }
        QCalendarWidget QMenu {
            background-color: #ffffff;
            color: #303133;
        }
        QCalendarWidget QSpinBox {
            background-color: #ffffff;
            color: #303133;
            border: 1px solid #DCDFE6;
        }
    )QSS"));
}

/**
 * @brief 构建简易AI设置页面
 * @return 页面组件指针
 */
QWidget* SettingDialog::buildSimpleAIPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    // 页面标题（文档：简易AI，左对齐黑色粗体）
    auto* title = new QLabel(QStringLiteral("简易AI"), page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // 顶部提示条：浅蓝色背景条（文档要求）
    auto* tipBar = new QFrame(page);
    tipBar->setObjectName("tipBar");
    tipBar->setFixedHeight(44);
    auto* tipLayout = new QHBoxLayout(tipBar);
    tipLayout->setContentsMargins(12, 8, 12, 8);
    tipLayout->setSpacing(10);
    auto* tipIcon = new QLabel(QStringLiteral("✓"), tipBar);
    tipIcon->setObjectName("tipBarIcon");
    tipIcon->setStyleSheet("color: #67C23A; font-size: 16px; font-weight: bold;");
    auto* tipText = new QLabel(QStringLiteral("携带商品说明书：简易AI可对接FastGPT/Dify等知识库，并携带本地「商品说明书」内容一起发送给AI。"), tipBar);
    tipText->setWordWrap(true);
    auto* tipClose = new QPushButton(QStringLiteral("×"), tipBar);
    tipClose->setFixedSize(24, 24);
    tipClose->setCursor(Qt::PointingHandCursor);
    tipLayout->addWidget(tipIcon);
    tipLayout->addWidget(tipText, 1);
    tipLayout->addWidget(tipClose);
    layout->addWidget(tipBar);

    // 标签页（带系统图标占位）
    auto* tabWidget = new QTabWidget(page);

    // 基础设置标签页
    auto* basicTab = new QWidget();
    auto* basicTabLayout = new QVBoxLayout(basicTab);
    basicTabLayout->setContentsMargins(0, 0, 0, 0);
    basicTabLayout->setSpacing(20);

    // 商品说明书标签页（按文档与参考图完善）
    auto* manualTab = new QWidget();
    auto* manualTabLayout = new QVBoxLayout(manualTab);
    manualTabLayout->setContentsMargins(20, 20, 20, 20);
    manualTabLayout->setSpacing(20);

    auto* manualTitle = new QLabel(QStringLiteral("商品说明书管理"), manualTab);
    manualTitle->setObjectName("pageTitle");
    auto* manualDesc = new QLabel(QStringLiteral("根据商品ID或名称动态加载说明书到系统提示词"), manualTab);
    manualDesc->setObjectName("hintLabel");
    manualTabLayout->addWidget(manualTitle);
    manualTabLayout->addWidget(manualDesc);

    auto* manualToolbar = new QHBoxLayout();
    auto* manualSearch = new QLineEdit(manualTab);
    manualSearch->setPlaceholderText(QStringLiteral("搜索说明书名称/内容"));
    manualSearch->setMinimumWidth(220);
    auto* selectAllPageBtn = new QPushButton(QStringLiteral("全选本页"), manualTab);
    selectAllPageBtn->setObjectName("secondaryBtn");
    auto* exportExcelBtn = new QPushButton(QStringLiteral("导出Excel"), manualTab);
    exportExcelBtn->setObjectName("secondaryBtn");
    auto* importExcelBtn = new QPushButton(QStringLiteral("导入Excel"), manualTab);
    importExcelBtn->setObjectName("secondaryBtn");
    auto* addManualBtn = new QPushButton(QStringLiteral("+ 添加说明书"), manualTab);
    addManualBtn->setObjectName("primaryBtn");
    manualToolbar->addWidget(manualSearch);
    manualToolbar->addWidget(selectAllPageBtn);
    manualToolbar->addStretch();
    manualToolbar->addWidget(exportExcelBtn);
    manualToolbar->addWidget(importExcelBtn);
    manualToolbar->addWidget(addManualBtn);
    manualTabLayout->addLayout(manualToolbar);

    auto* manualCard = makeCard(manualTab);
    manualCard->setMinimumHeight(360);
    auto* manualCardLayout = new QVBoxLayout(manualCard);
    manualCardLayout->setContentsMargins(40, 40, 40, 40);
    manualCardLayout->setAlignment(Qt::AlignCenter);
    auto* manualEmptyIcon = new QLabel(QStringLiteral("📄"), manualCard);
    manualEmptyIcon->setStyleSheet("font-size: 64px; color: #C0C4CC;");
    manualEmptyIcon->setAlignment(Qt::AlignCenter);
    auto* manualEmptyText = new QLabel(QStringLiteral("暂无商品说明书，点击上方按钮添加或导入Excel"), manualCard);
    manualEmptyText->setObjectName("emptyStateLabel");
    manualEmptyText->setAlignment(Qt::AlignCenter);
    manualCardLayout->addWidget(manualEmptyIcon);
    manualCardLayout->addSpacing(12);
    manualCardLayout->addWidget(manualEmptyText);
    manualTabLayout->addWidget(manualCard, 1);

    auto* style = this->style();
    tabWidget->addTab(basicTab, style->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("基础设置"));
    tabWidget->addTab(manualTab, style->standardIcon(QStyle::SP_FileIcon), QStringLiteral("商品说明书"));
    layout->addWidget(tabWidget);

    // 上半部分：双列卡片（文档：左卡片=基础配置模块，右卡片=模型与余额模块）
    auto* upperRow = new QHBoxLayout();
    upperRow->setSpacing(20);

    auto addCardTitleRow = [this, style](QVBoxLayout* cardLayout, QWidget* parent, QStyle::StandardPixmap pix, const QString& text) {
        auto* row = new QHBoxLayout();
        auto* iconLbl = new QLabel(parent);
        iconLbl->setPixmap(style->standardIcon(pix).pixmap(16, 16));
        auto* titleLbl = new QLabel(text, parent);
        titleLbl->setObjectName("cardTitle");
        row->addWidget(iconLbl);
        row->addWidget(titleLbl);
        row->addStretch();
        cardLayout->addLayout(row);
    };

    // 左卡片：基础配置模块（标题 + 启用简易AI + 子模块 API令牌）
    auto* leftCard = makeCard(page);
    auto* leftLayout = new QVBoxLayout(leftCard);
    leftLayout->setContentsMargins(20, 15, 20, 15);
    leftLayout->setSpacing(15);
    addCardTitleRow(leftLayout, leftCard, QStyle::SP_FileDialogDetailedView, QStringLiteral("基础配置"));
    auto* enableLayout = new QHBoxLayout();
    auto* enableLabel = new QLabel(QStringLiteral("启用简易AI"), leftCard);
    auto* enableSwitch = new QCheckBox(leftCard);
    enableSwitch->setChecked(false);
    enableLayout->addWidget(enableLabel);
    enableLayout->addWidget(enableSwitch);
    enableLayout->addStretch();
    auto* enableDesc = new QLabel(QStringLiteral("启用后，将使用简易AI替代完整版AI（互斥关系）"), leftCard);
    enableDesc->setObjectName("hintLabel");
    leftLayout->addLayout(enableLayout);
    leftLayout->addWidget(enableDesc);
    addCardTitleRow(leftLayout, leftCard, QStyle::SP_FileDialogContentsView, QStringLiteral("API令牌"));
    auto* tokenInput = new QLineEdit(leftCard);
    tokenInput->setPlaceholderText(QStringLiteral("请输入API令牌"));
    tokenInput->setEchoMode(QLineEdit::Password);
    auto* tokenBtnRow = new QHBoxLayout();
    tokenBtnRow->addWidget(tokenInput, 1);
    auto* tokenEyeBtn = new QPushButton(leftCard);
    tokenEyeBtn->setIcon(style->standardIcon(QStyle::SP_MessageBoxQuestion));
    tokenEyeBtn->setFixedSize(28, 28);
    tokenEyeBtn->setObjectName("secondaryBtn");
    tokenEyeBtn->setFlat(true);
    auto* tokenRefreshBtn = new QPushButton(leftCard);
    tokenRefreshBtn->setIcon(style->standardIcon(QStyle::SP_BrowserReload));
    tokenRefreshBtn->setFixedSize(28, 28);
    tokenRefreshBtn->setObjectName("secondaryBtn");
    tokenRefreshBtn->setFlat(true);
    tokenBtnRow->addWidget(tokenEyeBtn);
    tokenBtnRow->addWidget(tokenRefreshBtn);
    leftLayout->addLayout(tokenBtnRow);
    auto* tokenLink = new QLabel(QStringLiteral("没有令牌？注册账号或联系经销商购买"), leftCard);
    tokenLink->setObjectName("linkLabel");
    tokenLink->setCursor(Qt::PointingHandCursor);
    leftLayout->addWidget(tokenLink);
    upperRow->addWidget(leftCard, 1);

    // 右卡片：模型与余额模块（模型选择 + 算力余额）
    auto* rightCard = makeCard(page);
    auto* rightLayout = new QVBoxLayout(rightCard);
    rightLayout->setContentsMargins(20, 15, 20, 15);
    rightLayout->setSpacing(15);
    addCardTitleRow(rightLayout, rightCard, QStyle::SP_ArrowUp, QStringLiteral("模型选择"));
    auto* modelLabel = new QLabel(QStringLiteral("AI模型"), rightCard);
    auto* modelCombo = new QComboBox(rightCard);
    modelCombo->addItem(QStringLiteral("通义千问 Plus（推荐）"));
    modelCombo->addItem(QStringLiteral("豆包 Pro 32K"));
    modelCombo->addItem(QStringLiteral("DeepSeek Chat"));
    modelCombo->addItem(QStringLiteral("GPT-4o Mini"));
    modelCombo->addItem(QStringLiteral("自定义模型"));
    rightLayout->addWidget(modelLabel);
    rightLayout->addWidget(modelCombo);
    addCardTitleRow(rightLayout, rightCard, QStyle::SP_MessageBoxInformation, QStringLiteral("算力余额"));
    auto* balancePanel = new QFrame(rightCard);
    balancePanel->setObjectName("balancePanel");
    balancePanel->setMinimumHeight(80);
    auto* balancePanelLayout = new QHBoxLayout(balancePanel);
    balancePanelLayout->setContentsMargins(16, 12, 16, 12);
    auto* balanceLeft = new QVBoxLayout();
    balanceLeft->setSpacing(4);
    auto* balanceInfo = new QLabel(QStringLiteral("请先输入令牌"), balancePanel);
    balanceInfo->setObjectName("balanceWarning");
    auto* balanceDetails = new QLabel(QStringLiteral("剩余：- 算力    已用：-    总计：-"), balancePanel);
    balanceDetails->setObjectName("hintLabel");
    balanceLeft->addWidget(balanceInfo);
    balanceLeft->addWidget(balanceDetails);
    balancePanelLayout->addLayout(balanceLeft, 1);
    auto* balanceRefreshBtn = new QPushButton(balancePanel);
    balanceRefreshBtn->setIcon(style->standardIcon(QStyle::SP_BrowserReload));
    balanceRefreshBtn->setFixedSize(28, 28);
    balanceRefreshBtn->setObjectName("secondaryBtn");
    balanceRefreshBtn->setFlat(true);
    balancePanelLayout->addWidget(balanceRefreshBtn);
    rightLayout->addWidget(balancePanel);
    upperRow->addWidget(rightCard, 1);

    basicTabLayout->addLayout(upperRow);

    // 下半部分：大卡片「系统提示词」（文档：标题+适用平台+文本区+提示词模板栏+可用变量栏）
    auto* promptCard = makeCard(page);
    auto* promptLayout = new QVBoxLayout(promptCard);
    promptLayout->setContentsMargins(20, 15, 20, 15);
    promptLayout->setSpacing(15);
    addCardTitleRow(promptLayout, promptCard, QStyle::SP_FileIcon, QStringLiteral("系统提示词（System Prompt）"));
    auto* platformLabel = new QLabel(QStringLiteral("适用平台"), promptCard);
    auto* platformCombo = new QComboBox(promptCard);
    platformCombo->addItem(QStringLiteral("全局默认"));
    platformCombo->addItem(QStringLiteral("京东"));
    platformCombo->addItem(QStringLiteral("千牛"));
    platformCombo->addItem(QStringLiteral("抖店"));
    platformCombo->addItem(QStringLiteral("小红书"));
    platformCombo->addItem(QStringLiteral("微信"));
    auto* promptDesc = new QLabel(QStringLiteral("编辑全局默认提示词，未配置的平台将使用此提示词"), promptCard);
    promptDesc->setObjectName("hintLabel");
    auto* promptText = new QTextEdit(promptCard);
    const QString defaultPrompt = QStringLiteral(
        "你是一位专业的电商客服，负责回复客户的购物咨询。当前时间：{current_time}\n\n"
        "核心职责：\n"
        "1. 解答客户关于商品详情、规格、价格等问题\n"
        "2. 处理订单查询、物流跟踪、退换货等问题\n"
        "3. 推荐相关商品，提升客户购物体验\n"
        "4. 处理客户投诉和售后问题\n"
        "5. 引导客户完成购买流程\n\n"
        "回复要求：\n"
        "- 使用亲切、热情、专业的语气\n"
        "- 回复及时、准确、简洁\n"
        "- 主动推荐相关商品和优惠活动\n"
        "- 耐心解答客户疑问，消除购买顾虑\n"
        "- 拟人化控制字数 尽量简短（10-40个字），因为人工打字风格是惜字如金的简短表达\n"
        "- 禁止使用markdown格式，不要用**加重符号，直接用文本和换行\n\n"
        "请根据客户的问题，提供专业、贴心的回复。");
    promptText->setPlainText(defaultPrompt);
    promptText->setMinimumHeight(200);
    promptLayout->addWidget(platformLabel);
    promptLayout->addWidget(platformCombo);
    promptLayout->addWidget(promptDesc);
    promptLayout->addWidget(promptText);

    // 提示词模板栏（浅色圆角按钮）
    auto* templateTitle = new QLabel(QStringLiteral("提示词模板（点击快速填充）"), promptCard);
    templateTitle->setObjectName("cardTitle");
    promptLayout->addWidget(templateTitle);
    auto* templateBtnLayout = new QHBoxLayout();
    QStringList templateNames = { QStringLiteral("电商模板"), QStringLiteral("车商模板"), QStringLiteral("教育培训模板"),
                                   QStringLiteral("房产模板"), QStringLiteral("医疗健康模板"), QStringLiteral("售后模板") };
    for (const QString& name : templateNames) {
        auto* tb = new QPushButton(name, promptCard);
        tb->setObjectName("secondaryBtn");
        tb->setFlat(true);
        templateBtnLayout->addWidget(tb);
    }
    templateBtnLayout->addStretch();
    promptLayout->addLayout(templateBtnLayout);

    // 可用变量栏（文档格式：{当前时间[time]} 等）
    auto* variableTitle = new QLabel(QStringLiteral("可用变量（点击自动填入完整内容）"), promptCard);
    variableTitle->setObjectName("cardTitle");
    promptLayout->addWidget(variableTitle);
    QStringList variables = {
        QStringLiteral("{当前时间[time]}"), QStringLiteral("{平台[platform]}"), QStringLiteral("{店铺名[shop_name]}"),
        QStringLiteral("{用户ID[user_id]}"), QStringLiteral("{商品名[goods_name]}"), QStringLiteral("{商品ID[goods_id]}"),
        QStringLiteral("{规格[sku_name]}"), QStringLiteral("{规格ID[sku_id]}"), QStringLiteral("{订单状态[order_status]}"),
        QStringLiteral("{订单详情[order_list]}"), QStringLiteral("{商品说明书[product_manuals]}")
    };
    const int varsPerRow = 4;
    for (int i = 0; i < variables.size(); i += varsPerRow) {
        auto* rowLayout = new QHBoxLayout();
        for (int j = i; j < qMin(i + varsPerRow, variables.size()); ++j) {
            auto* vb = new QPushButton(variables[j], promptCard);
            vb->setObjectName("variableBtn");
            rowLayout->addWidget(vb);
        }
        rowLayout->addStretch();
        promptLayout->addLayout(rowLayout);
    }
    basicTabLayout->addWidget(promptCard);
    basicTabLayout->addStretch();

    return page;
}

/**
 * @brief 构建AI配置设置页面
 * @return 页面组件指针
 */
QWidget* SettingDialog::buildAIConfigPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    // 页面标题
    auto* title = new QLabel(QStringLiteral("AI配置 (OpenAI通用格式)"), page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // 红色警示条：不携带商品说明书
    auto* dangerCard = new QFrame(page);
    dangerCard->setObjectName("dangerCard");
    dangerCard->setFrameShape(QFrame::NoFrame);
    auto* dangerLayout = new QHBoxLayout(dangerCard);
    dangerLayout->setContentsMargins(16, 12, 16, 12);

    auto* dangerIcon = new QLabel(QStringLiteral("⚠"), dangerCard);
    dangerIcon->setStyleSheet("font-size: 18px; color: #F56C6C;");
    auto* dangerText = new QLabel(QStringLiteral("不携带商品说明书: 本页适用于 OpenAI 通用格式的API, 不会携带「商品说明书」内容。若需要携带说明书, 请使用「简易AI」。"), dangerCard);
    dangerText->setWordWrap(true);
    auto* goToSimpleBtn = new QPushButton(QStringLiteral("去简易AI"), dangerCard);
    goToSimpleBtn->setObjectName("primaryBtn");
    dangerLayout->addWidget(dangerIcon);
    dangerLayout->addWidget(dangerText, 1);
    dangerLayout->addWidget(goToSimpleBtn);
    layout->addWidget(dangerCard);

    // 完整版AI配置卡片
    auto* configCard = makeCard(page);
    auto* configLayout = new QVBoxLayout(configCard);
    configLayout->setContentsMargins(20, 15, 20, 15);
    configLayout->setSpacing(15);

    auto* configTitle = new QLabel(QStringLiteral("完整版AI配置"), configCard);
    configTitle->setObjectName("cardTitle");

    auto* platformLabel = new QLabel(QStringLiteral("平台"), configCard);
    auto* platformCombo = new QComboBox(configCard);
    platformCombo->addItem(QStringLiteral("全局有效"));
    platformCombo->addItem(QStringLiteral("QQ"));
    platformCombo->addItem(QStringLiteral("微信"));
    platformCombo->addItem(QStringLiteral("千牛"));
    auto* platformHint = new QLabel(QStringLiteral("默认全局有效,选择指定平台可单独配置"), configCard);
    platformHint->setObjectName("hintLabel");

    auto* apiAddrLabel = new QLabel(QStringLiteral("API地址"), configCard);
    auto* apiAddrLayout = new QHBoxLayout();
    auto* apiAddrInput = new QLineEdit(configCard);
    apiAddrInput->setText(QStringLiteral("http://localhost:9998/v1/chat/completions"));
    auto* restoreBtn = new QPushButton(QStringLiteral("恢复默认"), configCard);
    restoreBtn->setObjectName("secondaryBtn");
    apiAddrLayout->addWidget(apiAddrInput, 1);
    apiAddrLayout->addWidget(restoreBtn);

    auto* apiKeyLabel = new QLabel(QStringLiteral("API密钥"), configCard);
    auto* apiKeyLayout = new QHBoxLayout();
    auto* apiKeyInput = new QLineEdit(configCard);
    apiKeyInput->setPlaceholderText(QStringLiteral("输入API密钥"));
    apiKeyInput->setEchoMode(QLineEdit::Password);
    auto* eyeBtn = new QPushButton(QStringLiteral("👁"), configCard);
    eyeBtn->setFixedWidth(30);
    eyeBtn->setObjectName("secondaryBtn");
    apiKeyLayout->addWidget(apiKeyInput, 1);
    apiKeyLayout->addWidget(eyeBtn);

    configLayout->addWidget(configTitle);
    configLayout->addWidget(platformLabel);
    configLayout->addWidget(platformCombo);
    configLayout->addWidget(platformHint);
    configLayout->addWidget(apiAddrLabel);
    configLayout->addLayout(apiAddrLayout);
    configLayout->addWidget(apiKeyLabel);
    configLayout->addLayout(apiKeyLayout);

    layout->addWidget(configCard);
    layout->addStretch();

    return page;
}

/**
 * @brief 构建首响提速设置页面
 * @return 页面组件指针
 */
QWidget* SettingDialog::buildFirstResponsePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    // 页面标题
    auto* title = new QLabel(QStringLiteral("首响提速"), page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // 极速回复卡片
    auto* quickReplyCard = makeCard(page);
    auto* quickLayout = new QVBoxLayout(quickReplyCard);
    quickLayout->setContentsMargins(20, 15, 20, 15);
    quickLayout->setSpacing(15);
    auto* quickTitle = new QLabel(QStringLiteral("极速回复"), quickReplyCard);
    quickTitle->setObjectName("cardTitle");
    auto* quickEnableLayout = new QHBoxLayout();
    auto* quickEnableLabel = new QLabel(QStringLiteral("启用"), quickReplyCard);
    auto* quickEnableSwitch = new QCheckBox(quickReplyCard);
    quickEnableSwitch->setChecked(true);
    quickEnableLayout->addWidget(quickEnableLabel);
    quickEnableLayout->addWidget(quickEnableSwitch);
    quickEnableLayout->addStretch();
    auto* coolLayout = new QHBoxLayout();
    auto* coolLabel = new QLabel(QStringLiteral("冷却时间"), quickReplyCard);
    auto* coolSpin = new QSpinBox(quickReplyCard);
    coolSpin->setValue(300);
    coolSpin->setSuffix(QStringLiteral(" 秒 (同一会话触发间隔)"));
    coolLayout->addWidget(coolLabel);
    coolLayout->addWidget(coolSpin);
    coolLayout->addStretch();
    auto* quickPhraseLabel = new QLabel(QStringLiteral("话术列表"), quickReplyCard);
    auto* quickPhraseDesc = new QLabel(QStringLiteral("每行一条话术,随机选一条发送,允许为空"), quickReplyCard);
    quickPhraseDesc->setObjectName("hintLabel");
    auto* quickPhraseText = new QTextEdit(quickReplyCard);
    quickPhraseText->setPlainText(QStringLiteral("[玫瑰]\n[爱心]\n在的,有货直接拍就行"));
    quickPhraseText->setMinimumHeight(120);
    auto* quickRestoreBtn = new QPushButton(QStringLiteral("恢复默认"), quickReplyCard);
    quickRestoreBtn->setObjectName("secondaryBtn");
    quickRestoreBtn->setFixedWidth(100);
    quickLayout->addWidget(quickTitle);
    quickLayout->addLayout(quickEnableLayout);
    quickLayout->addLayout(coolLayout);
    quickLayout->addWidget(quickPhraseLabel);
    quickLayout->addWidget(quickPhraseDesc);
    quickLayout->addWidget(quickPhraseText);
    quickLayout->addWidget(quickRestoreBtn, 0, Qt::AlignRight);
    layout->addWidget(quickReplyCard);

    // 超时安抚卡片
    auto* timeoutCard = makeCard(page);
    auto* timeoutLayout = new QVBoxLayout(timeoutCard);
    timeoutLayout->setContentsMargins(20, 15, 20, 15);
    timeoutLayout->setSpacing(15);
    auto* timeoutTitle = new QLabel(QStringLiteral("超时安抚"), timeoutCard);
    timeoutTitle->setObjectName("cardTitle");
    auto* enableLayout = new QHBoxLayout();
    auto* enableLabel = new QLabel(QStringLiteral("启用"), timeoutCard);
    auto* enableSwitch = new QCheckBox(timeoutCard);
    enableSwitch->setChecked(true);
    enableLayout->addWidget(enableLabel);
    enableLayout->addWidget(enableSwitch);
    enableLayout->addStretch();
    auto* thresholdLayout = new QHBoxLayout();
    auto* thresholdLabel = new QLabel(QStringLiteral("超时阈值"), timeoutCard);
    auto* thresholdSpin = new QSpinBox(timeoutCard);
    thresholdSpin->setValue(8);
    thresholdSpin->setSuffix(QStringLiteral(" 秒"));
    thresholdLayout->addWidget(thresholdLabel);
    thresholdLayout->addWidget(thresholdSpin);
    thresholdLayout->addStretch();
    auto* phraseLabel = new QLabel(QStringLiteral("话术列表"), timeoutCard);
    auto* phraseDesc = new QLabel(QStringLiteral("每行一条话术,随机选一条发送,允许为空"), timeoutCard);
    phraseDesc->setObjectName("hintLabel");
    auto* phraseText = new QTextEdit(timeoutCard);
    phraseText->setPlainText(QStringLiteral("稍等片刻,马上回复您\n查一下,稍等\n确认下,马上就好\n稍等哈,马上来\n正处理,稍等\n马上就好\n稍等,马上回复"));
    phraseText->setMinimumHeight(150);
    auto* restoreBtn = new QPushButton(QStringLiteral("恢复默认"), timeoutCard);
    restoreBtn->setObjectName("secondaryBtn");
    restoreBtn->setFixedWidth(100);
    timeoutLayout->addWidget(timeoutTitle);
    timeoutLayout->addLayout(enableLayout);
    timeoutLayout->addLayout(thresholdLayout);
    timeoutLayout->addWidget(phraseLabel);
    timeoutLayout->addWidget(phraseDesc);
    timeoutLayout->addWidget(phraseText);
    timeoutLayout->addWidget(restoreBtn, 0, Qt::AlignRight);
    layout->addWidget(timeoutCard);

    // 平台开关卡片
    auto* platformCard = makeCard(page);
    auto* platformLayout = new QVBoxLayout(platformCard);
    platformLayout->setContentsMargins(20, 15, 20, 15);
    platformLayout->setSpacing(15);
    auto* platformTitle = new QLabel(QStringLiteral("平台开关"), platformCard);
    platformTitle->setObjectName("cardTitle");
    auto* selectAllBtn = new QPushButton(QStringLiteral("√ 全选"), platformCard);
    selectAllBtn->setObjectName("secondaryBtn");
    selectAllBtn->setFixedWidth(80);
    auto* deselectAllBtn = new QPushButton(QStringLiteral("× 全不选"), platformCard);
    deselectAllBtn->setObjectName("secondaryBtn");
    deselectAllBtn->setFixedWidth(80);
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addWidget(platformTitle);
    btnLayout->addStretch();
    btnLayout->addWidget(selectAllBtn);
    btnLayout->addWidget(deselectAllBtn);
    platformLayout->addLayout(btnLayout);
    QStringList platforms = { QStringLiteral("京东"), QStringLiteral("千牛"), QStringLiteral("抖店"), QStringLiteral("小红书"), QStringLiteral("微信") };
    for (const QString& p : platforms) {
        auto* itemLayout = new QHBoxLayout();
        auto* platformItemLabel = new QLabel(p, platformCard);
        auto* platformSwitch = new QCheckBox(platformCard);
        platformSwitch->setChecked(true);
        itemLayout->addWidget(platformItemLabel);
        itemLayout->addStretch();
        itemLayout->addWidget(platformSwitch);
        platformLayout->addLayout(itemLayout);
    }
    layout->addWidget(platformCard);
    layout->addStretch();

    return page;
}

/**
 * @brief 构建关键词规则设置页面
 * @return 页面组件指针
 */
QWidget* SettingDialog::buildKeywordRulesPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    // 页面标题
    auto* title = new QLabel(QStringLiteral("关键词规则"), page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // 全局关键词规则卡片
    auto* rulesCard = makeCard(page);
    auto* rulesLayout = new QVBoxLayout(rulesCard);
    rulesLayout->setContentsMargins(20, 15, 20, 15);
    rulesLayout->setSpacing(15);

    auto* rulesTitle = new QLabel(QStringLiteral("全局关键词规则"), rulesCard);
    rulesTitle->setObjectName("cardTitle");

    auto* btnLayout = new QHBoxLayout();
    auto* addBtn = new QPushButton(QStringLiteral("添加规则"), rulesCard);
    addBtn->setObjectName("primaryBtn");
    auto* importBtn = new QPushButton(QStringLiteral("导入Excel"), rulesCard);
    importBtn->setObjectName("primaryBtn");
    auto* exportBtn = new QPushButton(QStringLiteral("导出规则"), rulesCard);
    exportBtn->setObjectName("primaryBtn");
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(importBtn);
    btnLayout->addWidget(exportBtn);
    btnLayout->addStretch();

    // 搜索栏
    auto* searchLayout = new QHBoxLayout();
    auto* searchInput = new QLineEdit(rulesCard);
    searchInput->setPlaceholderText(QStringLiteral("搜索关键词或回复内容..."));
    auto* clearBtn = new QPushButton(QStringLiteral("清空"), rulesCard);
    clearBtn->setObjectName("secondaryBtn");

    // 分页控件
    auto* pageSizeCombo = new QComboBox(rulesCard);
    pageSizeCombo->addItems({ QStringLiteral("10条"), QStringLiteral("20条"), QStringLiteral("50条"), QStringLiteral("100条") });
    pageSizeCombo->setCurrentText(QStringLiteral("50条"));
    auto* pageInfo = new QLabel(QStringLiteral("共0条,当前第1/1页"), rulesCard);
    pageInfo->setObjectName("hintLabel");

    searchLayout->addWidget(searchInput, 1);
    searchLayout->addWidget(clearBtn);
    searchLayout->addWidget(pageSizeCombo);
    searchLayout->addWidget(pageInfo);

    // 表格
    auto* table = new QTableWidget(rulesCard);
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({ QStringLiteral(""), QStringLiteral("关键词"), QStringLiteral("回复内容"),
                                      QStringLiteral("回复权限"), QStringLiteral("匹配模式"), QStringLiteral("生效时间"), QStringLiteral("操作") });
    table->setRowCount(0);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setMinimumHeight(300);

    // 空状态提示
    auto* emptyLabel = new QLabel(QStringLiteral("暂无关键词规则"), table);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setObjectName("emptyStateLabel");

    // 分页按钮
    auto* paginationLayout = new QHBoxLayout();
    auto* firstPageBtn = new QPushButton(QStringLiteral("首页"), rulesCard);
    firstPageBtn->setObjectName("secondaryBtn");
    auto* prevPageBtn = new QPushButton(QStringLiteral("上页"), rulesCard);
    prevPageBtn->setObjectName("secondaryBtn");
    auto* pageNumLabel = new QLabel(QStringLiteral("1"), rulesCard);
    pageNumLabel->setAlignment(Qt::AlignCenter);
    pageNumLabel->setFixedWidth(40);
    pageNumLabel->setStyleSheet("background: #5B9BD5; color: white; padding: 5px 10px; border-radius: 4px;");
    auto* nextPageBtn = new QPushButton(QStringLiteral("下页"), rulesCard);
    nextPageBtn->setObjectName("secondaryBtn");
    auto* lastPageBtn = new QPushButton(QStringLiteral("尾页"), rulesCard);
    lastPageBtn->setObjectName("secondaryBtn");
    paginationLayout->addStretch();
    paginationLayout->addWidget(firstPageBtn);
    paginationLayout->addWidget(prevPageBtn);
    paginationLayout->addWidget(pageNumLabel);
    paginationLayout->addWidget(nextPageBtn);
    paginationLayout->addWidget(lastPageBtn);
    paginationLayout->addStretch();

    rulesLayout->addWidget(rulesTitle);
    rulesLayout->addLayout(btnLayout);
    rulesLayout->addLayout(searchLayout);
    rulesLayout->addWidget(table);
    rulesLayout->addLayout(paginationLayout);

    layout->addWidget(rulesCard);
    layout->addStretch();

    return page;
}

/**
 * @brief 构建内容替换设置页面
 * @return 页面组件指针
 */
QWidget* SettingDialog::buildContentReplacePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    // 页面标题
    auto* title = new QLabel(QStringLiteral("内容替换"), page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // AI最终回复内容替换卡片
    auto* replaceCard = makeCard(page);
    auto* replaceLayout = new QVBoxLayout(replaceCard);
    replaceLayout->setContentsMargins(20, 15, 20, 15);
    replaceLayout->setSpacing(15);

    auto* replaceTitle = new QLabel(QStringLiteral("AI最终回复内容替换"), replaceCard);
    replaceTitle->setObjectName("cardTitle");

    auto* enableLayout = new QHBoxLayout();
    auto* enableLabel = new QLabel(QStringLiteral("启用"), replaceCard);
    auto* enableSwitch = new QCheckBox(replaceCard);
    enableSwitch->setChecked(false);
    enableLayout->addWidget(enableLabel);
    enableLayout->addWidget(enableSwitch);
    enableLayout->addStretch();

    auto* replaceDesc = new QLabel(QStringLiteral("仅处理AI最终回复内容;默认回复、首响提速、超时安抚不受影响"), replaceCard);
    replaceDesc->setObjectName("hintLabel");

    auto* replaceHint = new QLabel(QStringLiteral("提示: 想把多行拆成多条发送?添加规则:查找\\n(换行)替换为(系统会按\"|\"顺序分多条发送)"), replaceCard);
    replaceHint->setObjectName("hintLabel");
    replaceHint->setWordWrap(true);

    replaceLayout->addWidget(replaceTitle);
    replaceLayout->addLayout(enableLayout);
    replaceLayout->addWidget(replaceDesc);
    replaceLayout->addWidget(replaceHint);

    layout->addWidget(replaceCard);

    // 替换规则卡片
    auto* rulesCard = makeCard(page);
    auto* rulesLayout = new QVBoxLayout(rulesCard);
    rulesLayout->setContentsMargins(20, 15, 20, 15);
    rulesLayout->setSpacing(15);

    auto* rulesTitle = new QLabel(QStringLiteral("替换规则"), rulesCard);
    rulesTitle->setObjectName("cardTitle");

    auto* actionLayout = new QHBoxLayout();
    auto* addBlankBtn = new QPushButton(QStringLiteral("+ 添加空白规则"), rulesCard);
    addBlankBtn->setObjectName("primaryBtn");
    auto* commonPhraseLabel = new QLabel(QStringLiteral("常用语库:"), rulesCard);
    auto* commonPhraseCombo = new QComboBox(rulesCard);
    commonPhraseCombo->addItem(QStringLiteral("-- 请选择要添加的规则--"));
    auto* addSelectedBtn = new QPushButton(QStringLiteral("添加选中"), rulesCard);
    addSelectedBtn->setObjectName("primaryBtn");
    actionLayout->addWidget(addBlankBtn);
    actionLayout->addWidget(commonPhraseLabel);
    actionLayout->addWidget(commonPhraseCombo, 1);
    actionLayout->addWidget(addSelectedBtn);
    actionLayout->addStretch();

    // 表格
    auto* table = new QTableWidget(rulesCard);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({ QStringLiteral("启用"), QStringLiteral("平台"), QStringLiteral("查找"),
                                      QStringLiteral("替换为(留空=删除)"), QStringLiteral("操作") });
    table->setRowCount(0);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setMinimumHeight(200);

    // 空状态提示
    auto* emptyLabel = new QLabel(QStringLiteral("暂无替换规则"), table);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setObjectName("emptyStateLabel");

    rulesLayout->addWidget(rulesTitle);
    rulesLayout->addLayout(actionLayout);
    rulesLayout->addWidget(table);

    layout->addWidget(rulesCard);
    layout->addStretch();

    return page;
}

/**
 * @brief 构建默认回复设置页面
 * @return 页面组件指针
 */
QWidget* SettingDialog::buildDefaultReplyPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    // 页面标题
    auto* title = new QLabel(QStringLiteral("默认回复"), page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // 默认回复设置卡片
    auto* replyCard = makeCard(page);
    auto* replyLayout = new QVBoxLayout(replyCard);
    replyLayout->setContentsMargins(20, 15, 20, 15);
    replyLayout->setSpacing(15);

    auto* replyTitle = new QLabel(QStringLiteral("默认回复设置"), replyCard);
    replyTitle->setObjectName("cardTitle");

    // 启用开关
    auto* enableLayout = new QHBoxLayout();
    auto* enableLabel = new QLabel(QStringLiteral("启用"), replyCard);
    auto* enableSwitch = new QCheckBox(replyCard);
    enableSwitch->setChecked(true);
    auto* enableDesc = new QLabel(QStringLiteral("无匹配时随机回复"), replyCard);
    enableLayout->addWidget(enableLabel);
    enableLayout->addWidget(enableSwitch);
    enableLayout->addWidget(enableDesc);
    enableLayout->addStretch();

    // 话术列表
    auto* phraseLabel = new QLabel(QStringLiteral("话术列表"), replyCard);
    auto* phraseDesc = new QLabel(QStringLiteral("每行一条话术,随机选一条发送,允许为空"), replyCard);
    phraseDesc->setObjectName("hintLabel");

    auto* phraseText = new QTextEdit(replyCard);
    phraseText->setPlainText(QStringLiteral("在的,有货直接拍就行\n稍等,消息较多,我马上处理。\n[玫瑰]\n抱歉,请您稍等,我马上回复。\n等等哦,我离开了一会马上回您。"));
    phraseText->setMinimumHeight(200);

    auto* restoreBtn = new QPushButton(QStringLiteral("恢复默认"), replyCard);
    restoreBtn->setObjectName("secondaryBtn");
    restoreBtn->setFixedWidth(100);

    replyLayout->addWidget(replyTitle);
    replyLayout->addLayout(enableLayout);
    replyLayout->addWidget(phraseLabel);
    replyLayout->addWidget(phraseDesc);
    replyLayout->addWidget(phraseText);
    replyLayout->addWidget(restoreBtn, 0, Qt::AlignRight);

    layout->addWidget(replyCard);
    layout->addStretch();

    return page;
}

/**
 * @brief 构建消息推送设置页面
 * @return 页面组件指针
 */
QWidget* SettingDialog::buildMessagePushPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    // 页面标题
    auto* title = new QLabel(QStringLiteral("消息推送"), page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // 功能说明：可折叠面板（整栏可点击展开/收起）
    auto* functionCard = makeCard(page);
    functionCard->setObjectName("functionCard");
    auto* functionMainLayout = new QVBoxLayout(functionCard);
    functionMainLayout->setContentsMargins(20, 12, 20, 12);
    functionMainLayout->setSpacing(0);

    auto* functionHeaderBtn = new QPushButton(functionCard);
    functionHeaderBtn->setObjectName("collapseBtn");
    functionHeaderBtn->setFlat(true);
    functionHeaderBtn->setCursor(Qt::PointingHandCursor);
    functionHeaderBtn->setFixedHeight(36);
    auto* headerBtnLayout = new QHBoxLayout(functionHeaderBtn);
    headerBtnLayout->setContentsMargins(0, 0, 0, 0);
    headerBtnLayout->setSpacing(8);
    auto* functionTitle = new QLabel(QStringLiteral("⚡ 功能说明"), functionHeaderBtn);
    functionTitle->setObjectName("cardTitle");
    functionTitle->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto* functionCollapseIcon = new QLabel(QStringLiteral("▼"), functionHeaderBtn);
    functionCollapseIcon->setObjectName("collapseBtn");
    functionCollapseIcon->setFixedWidth(24);
    functionCollapseIcon->setAlignment(Qt::AlignCenter);
    functionCollapseIcon->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    headerBtnLayout->addWidget(functionTitle);
    headerBtnLayout->addStretch();
    headerBtnLayout->addWidget(functionCollapseIcon);
    functionMainLayout->addWidget(functionHeaderBtn);

    auto* functionContent = new QWidget(functionCard);
    auto* functionContentLayout = new QVBoxLayout(functionContent);
    functionContentLayout->setContentsMargins(0, 12, 0, 0);
    functionContentLayout->setSpacing(10);

    auto* workflowTitle = new QLabel(QStringLiteral("工作流程:"), functionContent);
    workflowTitle->setObjectName("cardTitle");
    auto* workflow1 = new QLabel(QStringLiteral("1. 从客户消息中提取联系方式(手机号、微信号等)"), functionContent);
    workflow1->setObjectName("hintLabel");
    workflow1->setWordWrap(true);
    auto* workflow2 = new QLabel(QStringLiteral("2. 保存线索到本地数据库(自动去重)"), functionContent);
    workflow2->setObjectName("hintLabel");
    auto* workflow3 = new QLabel(QStringLiteral("3. 仅新号码才推送到企微/飞鸽/钉钉"), functionContent);
    workflow3->setObjectName("hintLabel");

    auto* manualTitle = new QLabel(QStringLiteral("手动触发推送:"), functionContent);
    manualTitle->setObjectName("cardTitle");
    auto* manualDesc = new QLabel(QStringLiteral("在关键词规则或AI回复中使用 [推送消息] 可主动触发推送，使用 [推送消息], webhook地址 可指定推送到特定地址。"), functionContent);
    manualDesc->setObjectName("hintLabel");
    manualDesc->setWordWrap(true);

    auto* enableRow = new QHBoxLayout();
    auto* enableLabel = new QLabel(QStringLiteral("启用线索信息功能"), functionContent);
    auto* enableSwitch = new QCheckBox(functionContent);
    enableSwitch->setChecked(true);
    auto* functionDesc = new QLabel(QStringLiteral("开启后,AI回复的消息将自动保存到本地数据库并推送到指定平台"), functionContent);
    functionDesc->setObjectName("hintLabel");
    functionDesc->setWordWrap(true);
    enableRow->addWidget(enableLabel);
    enableRow->addWidget(enableSwitch);
    enableRow->addWidget(functionDesc, 1);

    functionContentLayout->addWidget(workflowTitle);
    functionContentLayout->addWidget(workflow1);
    functionContentLayout->addWidget(workflow2);
    functionContentLayout->addWidget(workflow3);
    functionContentLayout->addWidget(manualTitle);
    functionContentLayout->addWidget(manualDesc);
    functionContentLayout->addLayout(enableRow);
    functionMainLayout->addWidget(functionContent);

    connect(functionHeaderBtn, &QPushButton::clicked, this, [functionContent, functionCollapseIcon]() {
        const bool willHide = functionContent->isVisible();
        functionContent->setVisible(!willHide);
        functionCollapseIcon->setText(willHide ? QStringLiteral("▶") : QStringLiteral("▼"));
    });

    layout->addWidget(functionCard);

    // 标签页：推送设置 | 推送模板
    auto* tabWidget = new QTabWidget(page);

    // ---------- 推送设置 Tab ----------
    auto* pushSettingsTab = new QWidget();
    auto* pushSettingsLayout = new QVBoxLayout(pushSettingsTab);
    pushSettingsLayout->setContentsMargins(0, 10, 0, 0);
    pushSettingsLayout->setSpacing(20);

    auto* wecomCard = makeCard(pushSettingsTab);
    auto* wecomLayout = new QVBoxLayout(wecomCard);
    wecomLayout->setContentsMargins(20, 15, 20, 15);
    wecomLayout->setSpacing(15);
    auto* wecomTitle = new QLabel(QStringLiteral("企微推送"), wecomCard);
    wecomTitle->setObjectName("cardTitle");
    auto* wecomWebhookLabel = new QLabel(QStringLiteral("Webhook地址"), wecomCard);
    auto* wecomWebhookInput = new QLineEdit(wecomCard);
    wecomWebhookInput->setText(QStringLiteral("https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=xxx"));
    wecomLayout->addWidget(wecomTitle);
    wecomLayout->addWidget(wecomWebhookLabel);
    wecomLayout->addWidget(wecomWebhookInput);
    pushSettingsLayout->addWidget(wecomCard);

    auto* feigeCard = makeCard(pushSettingsTab);
    auto* feigeLayout = new QVBoxLayout(feigeCard);
    feigeLayout->setContentsMargins(20, 15, 20, 15);
    feigeLayout->setSpacing(15);
    auto* feigeTitle = new QLabel(QStringLiteral("飞鸽推送"), feigeCard);
    feigeTitle->setObjectName("cardTitle");
    auto* feigeWebhookLabel = new QLabel(QStringLiteral("Webhook地址"), feigeCard);
    auto* feigeWebhookInput = new QLineEdit(feigeCard);
    feigeWebhookInput->setText(QStringLiteral("https://api.feige.com/webhook/xxx"));
    feigeLayout->addWidget(feigeTitle);
    feigeLayout->addWidget(feigeWebhookLabel);
    feigeLayout->addWidget(feigeWebhookInput);
    pushSettingsLayout->addWidget(feigeCard);

    auto* dingtalkCard = makeCard(pushSettingsTab);
    auto* dingtalkLayout = new QVBoxLayout(dingtalkCard);
    dingtalkLayout->setContentsMargins(20, 15, 20, 15);
    dingtalkLayout->setSpacing(15);
    auto* dingtalkTitle = new QLabel(QStringLiteral("钉钉推送"), dingtalkCard);
    dingtalkTitle->setObjectName("cardTitle");
    auto* dingtalkWebhookLabel = new QLabel(QStringLiteral("Webhook地址"), dingtalkCard);
    auto* dingtalkWebhookInput = new QLineEdit(dingtalkCard);
    dingtalkWebhookInput->setText(QStringLiteral("https://oapi.dingtalk.com/robot/send?access_token=xxx"));
    dingtalkLayout->addWidget(dingtalkTitle);
    dingtalkLayout->addWidget(dingtalkWebhookLabel);
    dingtalkLayout->addWidget(dingtalkWebhookInput);
    pushSettingsLayout->addWidget(dingtalkCard);

    auto* testCard = makeCard(pushSettingsTab);
    auto* testLayout = new QVBoxLayout(testCard);
    testLayout->setContentsMargins(20, 15, 20, 15);
    testLayout->setSpacing(15);
    auto* testTitle = new QLabel(QStringLiteral("测试推送"), testCard);
    testTitle->setObjectName("cardTitle");
    auto* testMessageLabel = new QLabel(QStringLiteral("测试消息"), testCard);
    auto* testMessageText = new QTextEdit(testCard);
    testMessageText->setPlainText(QStringLiteral("[测试消息]\n平台：测试\n客户：张三\n联系方式：138****1234\n这是一条测试推送消息。"));
    testMessageText->setMaximumHeight(100);
    auto* sendTestBtn = new QPushButton(QStringLiteral("发送测试推送"), testCard);
    sendTestBtn->setObjectName("primaryBtn");
    sendTestBtn->setFixedWidth(120);
    testLayout->addWidget(testTitle);
    testLayout->addWidget(testMessageLabel);
    testLayout->addWidget(testMessageText);
    auto* testBtnRow = new QHBoxLayout();
    testBtnRow->addStretch();
    testBtnRow->addWidget(sendTestBtn);
    testLayout->addLayout(testBtnRow);
    pushSettingsLayout->addWidget(testCard);
    pushSettingsLayout->addStretch();

    // ---------- 推送模板 Tab ----------
    auto* pushTemplateTab = new QWidget();
    auto* pushTemplateLayout = new QVBoxLayout(pushTemplateTab);
    pushTemplateLayout->setContentsMargins(0, 10, 0, 0);
    pushTemplateLayout->setSpacing(20);

    // 线索提取规则卡片
    auto* regexCard = makeCard(pushTemplateTab);
    auto* regexCardLayout = new QVBoxLayout(regexCard);
    regexCardLayout->setContentsMargins(20, 15, 20, 15);
    regexCardLayout->setSpacing(12);
    auto* regexTitle = new QLabel(QStringLiteral("🔍 线索提取规则"), regexCard);
    regexTitle->setObjectName("cardTitle");
    auto* regexLabel = new QLabel(QStringLiteral("自定义正则"), regexCard);
    auto* regexInput = new QLineEdit(regexCard);
    regexInput->setPlaceholderText(QStringLiteral("请输入正则表达式"));
    regexInput->setText(QStringLiteral("/QQ[:::]?(\\d{5,11})/gi"));
    auto* regexHint = new QLabel(QStringLiteral("可用于提取QQ号、邮箱等自定义内容"), regexCard);
    regexHint->setObjectName("hintLabel");
    regexCardLayout->addWidget(regexTitle);
    regexCardLayout->addWidget(regexLabel);
    regexCardLayout->addWidget(regexInput);
    regexCardLayout->addWidget(regexHint);
    pushTemplateLayout->addWidget(regexCard);

    // 自定义推送模板卡片
    auto* templateCard = makeCard(pushTemplateTab);
    auto* templateCardLayout = new QVBoxLayout(templateCard);
    templateCardLayout->setContentsMargins(20, 15, 20, 15);
    templateCardLayout->setSpacing(12);
    auto* templateTitle = new QLabel(QStringLiteral("📝 自定义推送模板"), templateCard);
    templateTitle->setObjectName("cardTitle");
    const QString pushTemplateDefault = QStringLiteral(
        "【消息推送】\n"
        "平台: {platform}\n"
        "账号: {account}\n"
        "客户: {customer}\n"
        "消息: {message}\n"
        "回复: {reply}\n"
        "时间: {time}\n"
        "提取信息: {extracted_info}\n"
        "【联系方式】\n"  
        "{extracted_info}");
    auto* templateText = new QTextEdit(templateCard);
    templateText->setPlainText(pushTemplateDefault);
    templateText->setMinimumHeight(180);
    auto* templateVarLabel = new QLabel(QStringLiteral("可用变量:"), templateCard);
    auto* templateVarLayout = new QHBoxLayout();
    QStringList pushVars = {
        QStringLiteral("{platform}"), QStringLiteral("{account}"), QStringLiteral("{customer}"),
        QStringLiteral("{message}"), QStringLiteral("{reply}"), QStringLiteral("{extracted_info}"),
        QStringLiteral("{time}"), QStringLiteral("{session}")
    };
    for (const QString& pv : pushVars) {
        auto* varBtn = new QPushButton(pv, templateCard);
        varBtn->setObjectName("secondaryBtn");
        varBtn->setFlat(true);
        varBtn->setCursor(Qt::PointingHandCursor);
        templateVarLayout->addWidget(varBtn);
    }
    templateVarLayout->addStretch();
    templateCardLayout->addWidget(templateTitle);
    templateCardLayout->addWidget(templateText);
    templateCardLayout->addWidget(templateVarLabel);
    templateCardLayout->addLayout(templateVarLayout);
    pushTemplateLayout->addWidget(templateCard);
    pushTemplateLayout->addStretch();

    tabWidget->addTab(pushSettingsTab, QStringLiteral("推送设置"));
    tabWidget->addTab(pushTemplateTab, QStringLiteral("推送模板"));
    layout->addWidget(tabWidget);
    layout->addStretch();

    return page;
}

/**
 * @brief 构建线索列表设置页面
 * @return 页面组件指针
 */
QWidget* SettingDialog::buildLeadListPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    // 页面标题
    auto* title = new QLabel(QStringLiteral("线索列表"), page);
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    // 搜索和筛选卡片
    auto* searchCard = makeCard(page);
    auto* searchLayout = new QVBoxLayout(searchCard);
    searchLayout->setContentsMargins(20, 15, 20, 15);
    searchLayout->setSpacing(15);

    auto* filterLayout = new QHBoxLayout();
    auto* searchInput = new QLineEdit(searchCard);
    searchInput->setPlaceholderText(QStringLiteral("搜索关键词 (消息、会话ID)"));
    auto* platformCombo = new QComboBox(searchCard);
    platformCombo->addItem(QStringLiteral("全部平台"));
    platformCombo->addItems({ QStringLiteral("QQ"), QStringLiteral("微信"), QStringLiteral("千牛") });

    // 日期范围：目标效果为单一框内「开始 至 结束」，无单独“开始日期/结束日期”标签
    auto* dateRangeBox = new QFrame(searchCard);
    dateRangeBox->setObjectName("dateRangeBox");
    dateRangeBox->setFrameShape(QFrame::NoFrame);
    auto* dateRangeLayout = new QHBoxLayout(dateRangeBox);
    dateRangeLayout->setContentsMargins(8, 4, 8, 4);
    dateRangeLayout->setSpacing(8);

    auto* dateFrom = new QDateEdit(dateRangeBox);
    dateFrom->setCalendarPopup(true);
    dateFrom->setDisplayFormat(QStringLiteral("yyyy/MM/dd"));
    dateFrom->setDate(QDate::currentDate().addDays(-7));
    auto* dateToLabel = new QLabel(QStringLiteral("至"), dateRangeBox);
    auto* dateTo = new QDateEdit(dateRangeBox);
    dateTo->setCalendarPopup(true);
    dateTo->setDisplayFormat(QStringLiteral("yyyy/MM/dd"));
    dateTo->setDate(QDate::currentDate());

    auto* clearDateBtn = new QPushButton(QStringLiteral("清除"), dateRangeBox);
    clearDateBtn->setObjectName("secondaryBtn");
    clearDateBtn->setFlat(true);
    clearDateBtn->setCursor(Qt::PointingHandCursor);
    auto* todayDateBtn = new QPushButton(QStringLiteral("今天"), dateRangeBox);
    todayDateBtn->setObjectName("secondaryBtn");
    todayDateBtn->setFlat(true);
    todayDateBtn->setCursor(Qt::PointingHandCursor);

    connect(clearDateBtn, &QPushButton::clicked, this, [dateFrom, dateTo]() {
        dateFrom->setDate(QDate::currentDate().addDays(-7));
        dateTo->setDate(QDate::currentDate());
    });
    connect(todayDateBtn, &QPushButton::clicked, this, [dateFrom, dateTo]() {
        dateFrom->setDate(QDate::currentDate());
        dateTo->setDate(QDate::currentDate());
    });

    dateRangeLayout->addWidget(dateFrom);
    dateRangeLayout->addWidget(dateToLabel);
    dateRangeLayout->addWidget(dateTo);
    dateRangeLayout->addWidget(clearDateBtn);
    dateRangeLayout->addWidget(todayDateBtn);

    auto* queryBtn = new QPushButton(QStringLiteral("查询"), searchCard);
    queryBtn->setObjectName("primaryBtn");
    auto* exportBtn = new QPushButton(QStringLiteral("导出"), searchCard);
    exportBtn->setObjectName("secondaryBtn");

    filterLayout->addWidget(searchInput, 2);
    filterLayout->addWidget(platformCombo);
    filterLayout->addWidget(dateRangeBox);
    filterLayout->addWidget(queryBtn);
    filterLayout->addWidget(exportBtn);

    searchLayout->addLayout(filterLayout);

    layout->addWidget(searchCard);

    // 线索列表表格卡片
    auto* tableCard = makeCard(page);
    auto* tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(20, 15, 20, 15);
    tableLayout->setSpacing(15);

    auto* table = new QTableWidget(tableCard);
    table->setColumnCount(8);
    table->setHorizontalHeaderLabels({ QStringLiteral("ID"), QStringLiteral("平台"), QStringLiteral("会话ID"),
                                      QStringLiteral("客户消息"), QStringLiteral("AI回复"), QStringLiteral("提取信息"),
                                      QStringLiteral("时间"), QStringLiteral("操作") });
    table->setRowCount(0);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setMinimumHeight(400);

    // 空状态提示
    auto* emptyLabel = new QLabel(QStringLiteral("暂无线索记录"), table);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setObjectName("emptyStateLabel");

    auto* recordCount = new QLabel(QStringLiteral("共0条记录"), tableCard);
    recordCount->setObjectName("hintLabel");

    tableLayout->addWidget(table);
    tableLayout->addWidget(recordCount, 0, Qt::AlignRight);

    layout->addWidget(tableCard);
    layout->addStretch();

    return page;
}

