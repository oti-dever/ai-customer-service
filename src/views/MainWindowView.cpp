#include "MainWindowView.h"

#include "../models/PlatformModel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <QStatusBar>

namespace {

// 平台树的自定义数据角色枚举
enum PlatformTreeRole {
    PlatformIdRole = Qt::UserRole,  ///< 平台ID角色
    IsGroupRole,                    ///< 是否为分组角色
    DotColorRole                    ///< 圆点颜色角色
};

/**
 * @brief 左侧平台树的自定义委托
 *
 * 分组项显示为：圆点 + 标题 + 展开/折叠箭头
 * 子项显示为：图标 + 标题卡片
 */
class PlatformTreeDelegate final : public QStyledItemDelegate
{
public:
    explicit PlatformTreeDelegate(QTreeView* tree, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_tree(tree) {}

    // 返回项的大小提示
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool isGroup = index.data(IsGroupRole).toBool();
        return {option.rect.width(), isGroup ? 48 : 56};
    }

    // 绘制项
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QString title = index.data(Qt::DisplayRole).toString();
        const bool isGroup = index.data(IsGroupRole).toBool();
        const QColor dotColor = index.data(DotColorRole).value<QColor>();
        const bool expanded = m_tree && m_tree->isExpanded(index);
        const bool sel = (option.state & QStyle::State_Selected) != 0; // 是否选中
        const bool hover = (option.state & QStyle::State_MouseOver) != 0; // 是否悬停

        QRect r = option.rect.adjusted(6, 4, -6, -4); // 调整矩形边距

        if (isGroup) {
            // 绘制分组项背景
            QColor bg = QColor(245, 245, 247);  // 默认背景色
            if (hover) bg = QColor(238, 240, 245);  // 悬停背景色
            if (sel) bg = QColor(232, 235, 245);    // 选中背景色
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, 8, 8); // 圆角矩形

            // 绘制分组圆点
            const int dot = 8;
            QRect dotRect(r.left() + 12, r.center().y() - dot / 2, dot, dot);
            painter->setBrush(dotColor);
            painter->drawEllipse(dotRect);

            // 绘制分组标题
            QFont f = option.font;
            f.setBold(true);
            painter->setFont(f);
            painter->setPen(QColor(40, 40, 40));
            const bool hasChildren = index.model() && index.model()->rowCount(index) > 0;
            const int textRight = hasChildren ? r.right() - 28 : r.right() - 8;
            QRect textRect(r.left() + 12 + dot + 8, r.top(), textRight - (r.left() + 12 + dot + 8), r.height());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

            // 绘制展开/折叠箭头（如果有子项）
            if (hasChildren) {
                painter->setPen(QColor(120, 120, 120));
                painter->drawText(QRect(r.right() - 24, r.top(), 20, r.height()), Qt::AlignCenter,
                                  expanded ? QStringLiteral("▾") : QStringLiteral("▸"));
            }
        } else {
            // 绘制子项卡片背景
            QColor bg = QColor(255, 255, 255);  // 默认白色背景
            if (hover) bg = QColor(248, 249, 252);  // 悬停浅蓝色
            if (sel) bg = QColor(245, 247, 255);    // 选中浅蓝色
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, 10, 10);  // 圆角矩形

            // 绘制平台图标
            QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            QSize iconSize(24, 24);
            QRect iconRect(r.left() + 12, r.center().y() - iconSize.height() / 2, iconSize.width(), iconSize.height());
            if (!icon.isNull())
                icon.paint(painter, iconRect, Qt::AlignCenter);

            // 绘制平台标题
            painter->setPen(QColor(40, 40, 40));
            QRect textRect = r.adjusted(12 + iconSize.width() + 10, 0, -8, 0);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);
        }

        painter->restore();
    }

private:
    QTreeView* m_tree = nullptr; ///< 关联的树视图
};

/**
 * @brief 创建卡片组件
 * @param parent 父组件
 * @return 卡片Frame指针
 */
QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName("card");
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

/**
 * @brief 创建顶部图标按钮
 * @param parent 父组件
 * @param icon 按钮图标
 * @param toolTip 工具提示文本
 * @return 工具按钮指针
 */
QToolButton* makeTopIconButton(QWidget* parent, const QIcon& icon, const QString& toolTip)
{
    auto* btn = new QToolButton(parent);
    btn->setIcon(icon);
    btn->setToolTip(toolTip);
    btn->setAutoRaise(true);      // 自动凸起效果
    btn->setCursor(Qt::PointingHandCursor);  // 手型光标
    btn->setIconSize(QSize(18, 18));  // 图标大小
    return btn;
}

} // namespace

/**
 * @brief 主窗口视图构造函数
 * @param parent 父窗口
 */
MainWindowView::MainWindowView(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("羊羊AI客服"));
    resize(1320, 760); // 设置窗口大小

    // 创建根组件
    auto* root = new QWidget(this);
    root->setObjectName("root");
    setCentralWidget(root);

    // 根布局（水平）
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 左侧平台栏 + 右侧主区域
    rootLayout->addWidget(buildLeftSidebar());

    // 右侧区域
    auto* right = new QWidget(root);
    right->setObjectName("rightArea");
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    rightLayout->addWidget(buildTopBar()); // 顶部工具栏
    rightLayout->addWidget(buildCenterContent(), /*stretch*/ 1); // 中心内容区域（可伸缩）

    rootLayout->addWidget(right, /*stretch*/ 1); // 右侧区域可伸缩

    // 状态栏（占位：可用于显示连接状态/当前平台）
    statusBar()->showMessage(QStringLiteral("就绪"));

    applyStyle(); // 应用样式表
}

/**
 * @brief 设置平台数据模型
 * @param model 平台模型指针
 */
void MainWindowView::setPlatformModel(PlatformModel* model)
{
    m_platformModel = model;
}

/**
 * @brief 构建顶部工具栏
 * @return 工具栏组件指针
 */
QWidget* MainWindowView::buildTopBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName("topBar");
    bar->setFixedHeight(52);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(10);

    // 左侧：logo + 文案
    auto* logo = new QLabel(bar);
    logo->setObjectName("logo");
    logo->setFixedSize(22, 22);
    // logo->setPixmap(qApp->style()->standardIcon(QStyle::SP_DesktopIcon).pixmap(22, 22));
    logo->setPixmap(QPixmap(":/res/Platform-Management.png").scaled(22, 22, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto* title = new QLabel(QStringLiteral("平台管理"), bar);
    title->setObjectName("topTitle");

    layout->addWidget(logo);
    layout->addWidget(title);
    layout->addSpacing(8);

    // 顶部按钮： + / 刷新 / 快速启动
    // m_btnAdd = makeTopIconButton(bar, qApp->style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("添加新窗口"));
    // m_btnRefresh = makeTopIconButton(bar, qApp->style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新平台列表"));
    // m_btnQuickStart = makeTopIconButton(bar, qApp->style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("请先选择平台"));
    // 使用项目资源中的图标
    m_btnAdd = makeTopIconButton(bar, QIcon(":/res/AddNewWindow.png"), QStringLiteral("添加新窗口"));
    m_btnRefresh = makeTopIconButton(bar, QIcon(":/res/RefreshPlatformList.png"), QStringLiteral("刷新平台列表"));
    m_btnQuickStart = makeTopIconButton(bar, QIcon(":/res/Restart.png"), QStringLiteral("请先选择平台"));

    layout->addWidget(m_btnAdd);
    layout->addWidget(m_btnRefresh);

    // 中间：系统就绪标签（占位状态）
    auto* readyWrap = new QWidget(bar);
    readyWrap->setObjectName("readyWrap");
    auto* readyLayout = new QHBoxLayout(readyWrap);
    readyLayout->setContentsMargins(10, 6, 10, 6);
    readyLayout->setSpacing(6);

    auto* readyIcon = new QLabel(readyWrap);
    // readyIcon->setPixmap(qApp->style()->standardIcon(QStyle::SP_ArrowUp).pixmap(16, 16));
    // 替换占位资源
    readyIcon->setPixmap(QPixmap(":/res/System-Ready.png").scaled(16, 16));

    auto* readyText = new QLabel(QStringLiteral("系统就绪"), readyWrap);
    readyText->setObjectName("readyText");

    readyLayout->addWidget(readyIcon);
    readyLayout->addWidget(readyText);
    layout->addWidget(readyWrap);

    layout->addStretch(1);
    layout->addWidget(m_btnQuickStart);

    // 信号：交互逻辑由 Controller 处理（这里不实现业务）
    connect(m_btnAdd, &QToolButton::clicked, this, &MainWindowView::requestAdd);
    connect(m_btnRefresh, &QToolButton::clicked, this, &MainWindowView::requestRefresh);
    connect(m_btnQuickStart, &QToolButton::clicked, this, &MainWindowView::requestQuickStart);

    return bar;
}

/**
 * @brief 构建左侧边栏
 * @return 左侧边栏组件指针
 */
QWidget* MainWindowView::buildLeftSidebar()
{
    auto* left = new QWidget(this);
    left->setObjectName("leftSidebar");
    // left->setFixedWidth(300); // 固定宽度
    // 不再设置固定宽度，让布局自动计算（或者只设置最小宽度）
    left->setMinimumWidth(300);  // 改为最小宽度，而不是固定宽度

    auto* layout = new QVBoxLayout(left);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);  // 重要：顶部对齐，不拉伸

    // 创建平台树数据模型
    m_platformTreeModel = new QStandardItemModel(this);

    // ------创建"在线平台"分组------
    auto* online = new QStandardItem(QStringLiteral("在线平台"));
    online->setData(QStringLiteral("online"), PlatformIdRole);
    online->setData(true, IsGroupRole);
    online->setData(QColor(0, 200, 120), DotColorRole); // 绿色圆点
    online->setFlags(online->flags() & ~Qt::ItemIsDropEnabled); // 禁止拖放
    // ---------------------------

    // ------创建"管理后台"分组------
    const QIcon IconManagement = QIcon(":/res/manageConsole.png");
    auto* manageConsole = new QStandardItem(IconManagement, QStringLiteral("管理后台"));
    manageConsole->setData(QStringLiteral("manageconsole"), PlatformIdRole);
    manageConsole->setData(false, IsGroupRole);
    // manageConsole->setData(QColor(), DotColorRole); // 绿色圆点
    manageConsole->setFlags(manageConsole->flags() & ~Qt::ItemIsDropEnabled); // 禁止拖放

    // 使用项目资源中的图标
    const QIcon iconRobot = QIcon(":/res/Robot.png");
    const QIcon iconReception = QIcon(":/res/Reception.png");

    // 创建"机器人管理"项
    auto* itemRobot = new QStandardItem(iconRobot, QStringLiteral("机器人管理"));
    itemRobot->setData(QStringLiteral("managerobot"), PlatformIdRole);
    itemRobot->setData(false, IsGroupRole);
    itemRobot->setFlags(itemRobot->flags() & ~Qt::ItemIsDropEnabled);
    // 将子项添加到管理后台
    manageConsole->appendRow(itemRobot);

    // 创建"聚合接待"项
    auto* itemReception = new QStandardItem(iconReception, QStringLiteral("聚合接待"));
    itemReception->setData(QStringLiteral("groupreception"), PlatformIdRole);
    itemReception->setData(false, IsGroupRole);
    itemReception->setFlags(itemReception->flags() & ~Qt::ItemIsDropEnabled);
    manageConsole->appendRow(itemReception);
    // ---------------------------

    // ------创建"离线平台"分组------
    auto* offline = new QStandardItem(QStringLiteral("离线平台"));
    offline->setData(QStringLiteral("offline"), PlatformIdRole);
    offline->setData(true, IsGroupRole);
    offline->setData(QColor(160, 160, 160), DotColorRole); // 灰色圆点
    offline->setFlags(offline->flags() & ~Qt::ItemIsDropEnabled);

    // const QIcon iconQq = qApp->style()->standardIcon(QStyle::SP_ComputerIcon);
    // const QIcon iconWork = qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    // const QIcon iconQianniu = qApp->style()->standardIcon(QStyle::SP_DialogApplyButton);
    // 使用项目资源中的图标
    const QIcon iconQq = QIcon(":/res/QQ-Normal.png");
    const QIcon iconWork = QIcon(":/res/WeChat-Normal.png");
    const QIcon iconQianniu = QIcon(":/res/Qianniu-Normal.png");

    // 创建 QQ平台项
    auto* itemQq = new QStandardItem(iconQq, QStringLiteral("QQ"));
    itemQq->setData(QStringLiteral("qq"), PlatformIdRole);
    itemQq->setData(false, IsGroupRole);
    itemQq->setFlags(itemQq->flags() & ~Qt::ItemIsDropEnabled);
    // 将子项添加到离线分组
    offline->appendRow(itemQq);

    // 创建企业微信平台项
    auto* itemWork = new QStandardItem(iconWork, QStringLiteral("企业微信"));
    itemWork->setData(QStringLiteral("workwechat"), PlatformIdRole);
    itemWork->setData(false, IsGroupRole);
    itemWork->setFlags(itemWork->flags() & ~Qt::ItemIsDropEnabled);
    offline->appendRow(itemWork);

    // 创建千牛平台项
    auto* itemQianniu = new QStandardItem(iconQianniu, QStringLiteral("千牛"));
    itemQianniu->setData(QStringLiteral("qianniu"), PlatformIdRole);
    itemQianniu->setData(false, IsGroupRole);
    itemQianniu->setFlags(itemQianniu->flags() & ~Qt::ItemIsDropEnabled);
    offline->appendRow(itemQianniu);
    // ---------------------------

    // 将分组添加到模型
    m_platformTreeModel->appendRow(online);
    m_platformTreeModel->appendRow(manageConsole);
    m_platformTreeModel->appendRow(offline);

    // 创建平台树视图
    m_platformTree = new QTreeView(left);
    m_platformTree->setObjectName("platformList");
    m_platformTree->setModel(m_platformTreeModel);
    m_platformTree->setItemDelegate(new PlatformTreeDelegate(m_platformTree, m_platformTree));
    m_platformTree->setHeaderHidden(true);  // 隐藏表头
    m_platformTree->setIndentation(16);     // 缩进大小
    m_platformTree->setRootIsDecorated(false);  // 不显示根装饰
    m_platformTree->setExpandsOnDoubleClick(true);  // 双击展开/折叠
    m_platformTree->setSelectionMode(QAbstractItemView::SingleSelection);  // 单选模式
    m_platformTree->setEditTriggers(QAbstractItemView::NoEditTriggers);    // 禁止编辑
    m_platformTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 隐藏水平滚动条
    m_platformTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);    // 隐藏垂直滚动条
    m_platformTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);  // 逐像素滚动
    m_platformTree->setUniformRowHeights(false);  // 非统一行高
    m_platformTree->setMouseTracking(true);       // 启用鼠标跟踪
    m_platformTree->expandAll();  // 展开所有项

    layout->addWidget(m_platformTree);

    // 添加伸缩空间，将设置按钮推到底部
    layout->addStretch(1);

    // 创建设置按钮
    m_btnSettings = new QToolButton(left);
    m_btnSettings->setObjectName("settingsButton");
    m_btnSettings->setText(QStringLiteral("设置"));
    m_btnSettings->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const QIcon iconSettings = QIcon(":/res/Settings/Settings.png");
    // m_btnSettings->setIcon(qApp->style()->standardIcon(QStyle::SP_FileDialogStart));
    m_btnSettings->setIcon(iconSettings);
    m_btnSettings->setCursor(Qt::PointingHandCursor);
    m_btnSettings->setAutoRaise(false);
    m_btnSettings->setFixedHeight(48);

    // 连接设置按钮信号
    connect(m_btnSettings, &QToolButton::clicked, this, &MainWindowView::requestSettings);

    layout->addWidget(m_btnSettings);

    // 树视图在展开/折叠时动态调整高度
    updateTreeViewHeight();

    // 连接选择变化信号
    connect(m_platformTree->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindowView::onPlatformTreeSelectionChanged);
    connect(m_platformTree, &QTreeView::clicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid()) return;
        const bool isGroup = idx.data(IsGroupRole).toBool();
        const QString id = idx.data(PlatformIdRole).toString();

        if (isGroup && id == QLatin1String("offline")) {
            m_platformTree->setExpanded(idx, !m_platformTree->isExpanded(idx));
        }
    });

    // 树视图在展开/折叠时动态调整高度
    connect(m_platformTree, &QTreeView::expanded, this, [this](const QModelIndex&) {
        updateTreeViewHeight();
    });

    connect(m_platformTree, &QTreeView::collapsed, this, [this](const QModelIndex&) {
        updateTreeViewHeight();
    });

    return left;
}

/**
 * @brief 构建系统就绪页面
 * @return 就绪页面组件指针
 */
QWidget* MainWindowView::buildReadyPage()
{
    auto* center = new QWidget(this);
    center->setObjectName("centerArea");

    auto* layout = new QVBoxLayout(center);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1); // 顶部伸缩空间

    // 就绪卡片
    m_readyCard = makeCard(center);
    m_readyCard->setObjectName("readyCard");
    m_readyCard->setFixedWidth(560); // 固定长度

    auto* cardLayout = new QVBoxLayout(m_readyCard);
    cardLayout->setContentsMargins(28, 26, 28, 26);
    cardLayout->setSpacing(10);
    cardLayout->setAlignment(Qt::AlignHCenter); // 水平居中

    // 火箭图标容器
    auto* rocketWrap = new QFrame(m_readyCard);
    rocketWrap->setObjectName("rocketWrap");
    rocketWrap->setFixedSize(360, 94);

    auto* rocketLayout = new QHBoxLayout(rocketWrap);
    rocketLayout->setContentsMargins(16, 16, 16, 16);
    rocketLayout->addStretch(1);
    auto* rocket = new QLabel(rocketWrap);
    // rocket->setPixmap(qApp->style()->standardIcon(QStyle::SP_ArrowUp).pixmap(44, 44));
    // 替换占位资源
    rocket->setPixmap(QPixmap(":/res/Ready.png").scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    rocketLayout->addWidget(rocket);
    rocketLayout->addStretch(1);

    // 就绪标题
    m_readyTitle = new QLabel(QStringLiteral("系统就绪"), m_readyCard);
    m_readyTitle->setObjectName("readyTitle");
    m_readyTitle->setAlignment(Qt::AlignHCenter); // 水平居中

    // 分割线
    auto* divider = new QFrame(m_readyCard);
    divider->setObjectName("divider");
    divider->setFixedHeight(1);
    divider->setFixedWidth(220);

    // 就绪副标题
    m_readySubtitle = new QLabel(QStringLiteral("选择左侧平台开始管理客服窗口\n或使用右上角的启动按钮快速启动应用"), m_readyCard);
    m_readySubtitle->setObjectName("readySubtitle");
    m_readySubtitle->setAlignment(Qt::AlignHCenter);

    cardLayout->addWidget(rocketWrap);
    cardLayout->addWidget(m_readyTitle);
    cardLayout->addWidget(divider);
    cardLayout->addWidget(m_readySubtitle);

    // 快捷操作行
    auto* quickRow = new QWidget(center);
    auto* quickLayout = new QHBoxLayout(quickRow);
    quickLayout->setContentsMargins(0, 18, 0, 0);
    quickLayout->setSpacing(18);
    quickLayout->setAlignment(Qt::AlignHCenter);  // 水平居中

    // Lambda函数：创建快捷卡片按钮
    auto makeQuick = [&](const QIcon& icon, const QString& text) -> QToolButton* {
        auto* b = new QToolButton(quickRow);
        b->setObjectName("quickCard");
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);  // 文字在图标下方
        b->setIcon(icon);
        b->setIconSize(QSize(26, 26));
        b->setText(text);
        b->setCursor(Qt::PointingHandCursor);  // 手型光标
        b->setAutoRaise(false);  // 不自动凸起
        b->setFixedSize(150, 120);  // 固定大小
        return b;
    };

    // auto* btnPick = makeQuick(qApp->style()->standardIcon(QStyle::SP_ArrowRight), QStringLiteral("点击选择平台"));
    // auto* btnEmbed = makeQuick(qApp->style()->standardIcon(QStyle::SP_FileDialogListView), QStringLiteral("自动嵌入窗口"));
    // auto* btnStart = makeQuick(qApp->style()->standardIcon(QStyle::SP_DialogOkButton), QStringLiteral("快速启动应用"));
    // 使用项目资源中的图标
    auto* btnPick = makeQuick(QIcon(":/res/Click-to-Select-Platform.png"), QStringLiteral("点击选择平台"));
    auto* btnEmbed = makeQuick(QIcon(":/res/Auto-Embed-Window.png"), QStringLiteral("自动嵌入窗口"));
    auto* btnStart = makeQuick(QIcon(":/res/Quick-Launch-Application.png"), QStringLiteral("快速启动应用"));

    quickLayout->addWidget(btnPick);
    quickLayout->addWidget(btnEmbed);
    quickLayout->addWidget(btnStart);

    // 连接快捷按钮信号
    connect(btnStart, &QToolButton::clicked, this, &MainWindowView::requestQuickStart);
    connect(btnEmbed, &QToolButton::clicked, this, &MainWindowView::requestAutoEmbed);
    connect(btnPick, &QToolButton::clicked, this, [this]() { emit platformGroupClicked(0); });

    layout->addWidget(m_readyCard, 0, Qt::AlignHCenter);  // 水平居中
    layout->addWidget(quickRow, 0, Qt::AlignHCenter);
    layout->addStretch(2);  // 底部伸缩空间（比顶部大）
    return center;
}

/**
 * @brief 构建平台未运行页面
 * @param name 平台名称
 * @param icon 平台图标
 * @param objectName 对象名称
 * @return 平台未运行页面组件指针
 */
QWidget* MainWindowView::buildPlatformNotRunningPage(const QString& name, const QIcon& icon, const QString& objectName)
{
    auto* page = new QWidget(this);
    page->setObjectName(objectName);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);  // 顶部伸缩空间

    // 平台卡片
    auto* card = makeCard(page);
    card->setObjectName(QStringLiteral("platformCard"));
    card->setFixedWidth(480);  // 固定宽度

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(32, 28, 32, 28);
    cardLayout->setSpacing(14);
    cardLayout->setAlignment(Qt::AlignHCenter); // 水平居中

    // 图标容器
    auto* iconWrap = new QFrame(card);
    iconWrap->setObjectName(QStringLiteral("platformIconWrap"));
    iconWrap->setFixedSize(88, 88);

    auto* iconLayout = new QHBoxLayout(iconWrap);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    iconLayout->setAlignment(Qt::AlignCenter); // 居中
    auto* iconLabel = new QLabel(iconWrap);
    iconLabel->setPixmap(icon.pixmap(48, 48)); // 设置图标
    iconLayout->addWidget(iconLabel);

    // 状态栏（显示平台状态）
    auto* statusBar = new QLabel(name + QStringLiteral(" 未运行"), card);
    statusBar->setObjectName(QStringLiteral("platformStatusBar"));
    statusBar->setAlignment(Qt::AlignHCenter);

    // 提示信息
    auto* hint1 = new QLabel(QStringLiteral("该平台当前未运行"), card);
    hint1->setObjectName(QStringLiteral("platformHint"));
    hint1->setAlignment(Qt::AlignHCenter);

    auto* hint2 = new QLabel(QStringLiteral("请使用顶部的创建账号按钮启动程序"), card);
    hint2->setObjectName(QStringLiteral("platformHint"));
    hint2->setAlignment(Qt::AlignHCenter);

    cardLayout->addWidget(iconWrap, 0, Qt::AlignHCenter);
    cardLayout->addWidget(statusBar);
    cardLayout->addWidget(hint1);
    cardLayout->addWidget(hint2);

    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch(2);
    return page;
}

/**
 * @brief 构建中心内容区域
 * @return 中心内容组件指针
 */
QWidget* MainWindowView::buildCenterContent()
{
    m_centerStack = new QStackedWidget(this);
    m_centerStack->setObjectName("centerStack");

    // 添加页面到堆叠窗口
    m_centerStack->addWidget(buildReadyPage());  // 系统就绪页面（索引0）

    // 加载平台图标
    // const QIcon iconQq = qApp->style()->standardIcon(QStyle::SP_ComputerIcon);
    // const QIcon iconWork = qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    // const QIcon iconQianniu = qApp->style()->standardIcon(QStyle::SP_DialogApplyButton);
    // 使用项目资源中的图标
    const QIcon iconQq = QIcon(":/res/QQ-Normal.png");
    const QIcon iconWork = QIcon(":/res/WeChat-Normal.png");
    const QIcon iconQianniu = QIcon(":/res/Qianniu-Normal.png");

    // 添加平台未运行页面
    m_centerStack->addWidget(buildPlatformNotRunningPage(QStringLiteral("QQ"), iconQq, QStringLiteral("pageQQ"))); // 索引1
    m_centerStack->addWidget(buildPlatformNotRunningPage(QStringLiteral("企业微信"), iconWork, QStringLiteral("pageWorkWechat"))); // 索引2
    m_centerStack->addWidget(buildPlatformNotRunningPage(QStringLiteral("千牛"), iconQianniu, QStringLiteral("pageQianniu"))); // 索引3

    return m_centerStack;
}

/**
 * @brief 平台树选择变化处理
 */
void MainWindowView::onPlatformTreeSelectionChanged()
{
    const QModelIndex idx = m_platformTree->currentIndex();
    if (!idx.isValid()) {
        m_centerStack->setCurrentIndex(0); // 显示就绪页面
        emit platformSelected(QString()); // 发送空平台ID
        return;
    }
    const QString id = idx.data(PlatformIdRole).toString();

    // 根据平台ID切换页面
    if (id == QLatin1String("online")) {
        m_centerStack->setCurrentIndex(0);
        emit platformSelected(id);
        return;
    }
    if (id == QLatin1String("offline")) {
        m_centerStack->setCurrentIndex(0);
        emit platformSelected(QString());
        return;
    }
    if (id == QLatin1String("qq")) {
        m_centerStack->setCurrentIndex(1);
        emit platformSelected(id);
        return;
    }
    if (id == QLatin1String("workwechat")) {
        m_centerStack->setCurrentIndex(2);
        emit platformSelected(id);
        return;
    }
    if (id == QLatin1String("qianniu")) {
        m_centerStack->setCurrentIndex(3);
        emit platformSelected(id);
        return;
    }
    if (id == QLatin1String("managerobot")) {
        m_centerStack->setCurrentIndex(0);
        emit platformSelected(id);
        return;
    }
    if (id == QLatin1String("groupreception")) {
        m_centerStack->setCurrentIndex(0);
        emit platformSelected(id);
        return;
    }

    // 默认显示就绪页面
    m_centerStack->setCurrentIndex(0);
    emit platformSelected(QString());
}

void MainWindowView::updateTreeViewHeight()
{
    if (!m_platformTree || !m_platformTreeModel) return;

    int totalHeight = 0;
    int itemCount = m_platformTreeModel->rowCount();
    for (int i = 0; i < itemCount; ++i) {
        QModelIndex index = m_platformTreeModel->index(i, 0);
        bool isGroup = index.data(IsGroupRole).toBool();

        int itemHeight = isGroup ? 48 : 56;
        totalHeight += itemHeight;

        if (isGroup && m_platformTree->isExpanded(index)) {
            int childCount = m_platformTreeModel->rowCount(index);
            for (int j = 0; j < childCount; ++j) {
                totalHeight += 56;
            }
        }
    }

    m_platformTree->setMinimumHeight(totalHeight + 20);
}

/**
 * @brief 应用样式表
 *
 * 设置主窗口及所有子组件的视觉样式。
 * 使用QSS（Qt样式表）语法，类似于CSS但有一些Qt特有的扩展。
 * 样式按界面结构分区域注释，便于维护和修改。
 */
void MainWindowView::applyStyle()
{
    setStyleSheet(QStringLiteral(R"QSS(
        /* ----- 1. 根/主窗口区域 ----- */
        /* 设置主窗口中央部件的背景色为浅灰色 */
        QWidget#root { background: #f4f6f9; }

        /* ----- 2. 左侧边栏区域 ----- */
        /* 左侧边栏背景设置为深灰色，与右侧浅色区域形成对比 */
        QWidget#leftSidebar { background: #1f1f1f; }
        /* 平台树控件：透明背景，无边框和焦点框，以融入左侧边栏深色背景 */
        QTreeView#platformList {
            background: transparent;  /* 透明背景 */
            border: none;             /* 无边框 */
            outline: none;            /* 无焦点框（选中时虚线框） */
        }
        /* 平台树的每个项也设置为透明背景 */
        QTreeView#platformList::item { background: transparent; }

        /* 设置按钮样式：匹配分组项样式 */
        QToolButton#settingsButton {
            background: #F5F5F7;
            border: none;
            border-radius: 8px;
            color: #333333;
            font-size: 14px;
            font-weight: bold;
            text-align: left;
            padding-left: 12px;
        }
        QToolButton#settingsButton:hover {
            background: #E8E8EB;
        }
        QToolButton#settingsButton:pressed {
            background: #DFDFE2;
        }

        /* ----- 3. 顶部栏区域（包含平台管理标题和操作按钮） ----- */
        /* 顶部栏：白色背景，底部浅灰色分隔线 */
        QWidget#topBar {
            background: #ffffff;                 /* 白色背景 */
            border-bottom: 1px solid #edf0f5;    /* 底部边框（浅灰色分隔线） */
        }
        /* 顶部标题"平台管理"：深灰色文字，14px字号，半粗体 */
        QLabel#topTitle {
            color: #2a2a2a;     /* 深灰色文字 */
            font-size: 14px;    /* 字号 */
            font-weight: 600;   /* 字体粗细（半粗体） */
        }
        /* 系统就绪状态标签容器：白色背景，浅灰色边框，圆角 */
        QWidget#readyWrap {
            background: #ffffff;                 /* 白色背景 */
            border: 1px solid #eef1f6;          /* 边框颜色 */
            border-radius: 12px;                /* 圆角半径 */
        }
        /* 系统就绪状态文字：深灰色，13px字号，半粗体 */
        QLabel#readyText {
            color: #2a2a2a;     /* 深灰色文字 */
            font-size: 13px;    /* 字号 */
            font-weight: 600;   /* 字体粗细 */
        }

        /* ----- 4. 右侧主内容区域（包含系统就绪和平台未运行页面） ----- */
        /* 所有中心区域页面背景统一为浅灰色，与左侧深色背景形成对比 */
        QWidget#centerArea, QWidget#centerStack,
        QWidget#pageQQ, QWidget#pageWorkWechat, QWidget#pageQianniu {
            background: #f4f6f9;  /* 浅灰色背景 */
        }

        /* ----- 5. 卡片样式（系统就绪卡片和各平台未运行卡片） ----- */
        /* 卡片通用样式：白色背景，圆角边框，浅灰色边框线 */
        QFrame#readyCard, QFrame#platformCard {
            background: #ffffff;                 /* 白色背景 */
            border-radius: 14px;                /* 圆角半径 */
            border: 1px solid #eef1f6;          /* 边框颜色和宽度 */
        }
        /* 就绪页面的火箭图标容器：浅绿色背景，大圆角 */
        QFrame#rocketWrap {
            background: #eefaf5;   /* 浅绿色背景 */
            border-radius: 20px;   /* 大圆角半径 */
        }
        /* 平台未运行页面的图标容器：浅蓝色背景，圆角 */
        QFrame#platformIconWrap {
            background: #f0f4f8;   /* 浅蓝色背景 */
            border-radius: 16px;   /* 圆角半径 */
        }
        /* 平台状态栏：浅灰色背景，圆角，内边距，深灰色文字，较大字号 */
        QLabel#platformStatusBar {
            background: #f0f2f5;               /* 浅灰色背景 */
            border-radius: 8px;                /* 圆角半径 */
            padding: 8px 16px;                 /* 内边距（上下8px，左右16px） */
            color: #1f2933;                    /* 深灰色文字 */
            font-size: 15px;                   /* 字号（比普通文字稍大） */
            font-weight: 600;                  /* 字体粗细 */
        }
        /* 平台提示文字：中灰色，13px字号，用于辅助说明 */
        QLabel#platformHint {
            color: #6b7280;   /* 中灰色文字 */
            font-size: 13px;  /* 字号 */
        }

        /* ----- 6. 系统就绪页面特定元素 ----- */
        /* 就绪标题：深灰色，较大字号，粗体，用于突出显示 */
        QLabel#readyTitle {
            color: #2a2a2a;      /* 深灰色文字 */
            font-size: 22px;     /* 较大字号（标题级别） */
            font-weight: 700;    /* 粗体 */
        }
        /* 分隔线：浅灰色水平线，用于视觉分隔标题和副标题 */
        QFrame#divider {
            background: #edf0f5;  /* 浅灰色背景（作为分隔线颜色） */
        }
        /* 就绪副标题：中灰色，13px字号，1.5倍行高，多行文本友好 */
        QLabel#readySubtitle {
            color: #6b7280;       /* 中灰色文字 */
            font-size: 13px;      /* 字号 */
            line-height: 18px;    /* 行高（1.5倍字号，提升可读性） */
        }

        /* ----- 7. 快捷入口卡片（就绪页面底部的三个功能卡片） ----- */
        /* 快捷卡片基础样式：白色背景，浅灰色边框，圆角，内边距，深灰色文字 */
        QToolButton#quickCard {
            background: #ffffff;                 /* 白色背景 */
            border: 1px solid #eef1f6;          /* 边框颜色和宽度 */
            border-radius: 12px;                /* 圆角半径 */
            padding: 10px;                      /* 内边距（各方向10px） */
            color: #3b3b3b;                     /* 深灰色文字 */
            font-size: 13px;                    /* 字号 */
        }
        /* 快捷卡片悬停状态：背景色变浅，边框颜色变深 */
        QToolButton#quickCard:hover {
            background: #fbfcfe;     /* 悬停时背景色（非常浅的蓝色） */
            border-color: #e6ebf5;   /* 悬停时边框颜色（稍深一些） */
        }
        /* 快捷卡片按下状态：背景色变为更深的浅蓝色，提供点击反馈 */
        QToolButton#quickCard:pressed {
            background: #f3f5fb;     /* 按下时背景色（浅蓝色） */
        }
    )QSS"));
}
