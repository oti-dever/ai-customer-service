#include "mainwindow.h"
#include "../utils/applystyle.h"
#include "addwindowdialog.h"
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QPainter>
#include <QToolButton>
#include <QHBoxLayout>
#include <qapplication.h>

namespace {
enum PlatformTreeRole {
    PlatformIdRole = Qt::UserRole,
    IsGroupRole,
    DotColorRole
};

class PlatformTreeDelegate : public QStyledItemDelegate
{
public:
    explicit PlatformTreeDelegate(QTreeView* tree, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_tree(tree) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool isGroup = index.data(IsGroupRole).toBool();
        return {option.rect.width(), isGroup ? 48 : 56};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QString title = index.data(Qt::DisplayRole).toString();
        const bool isGroup = index.data(IsGroupRole).toBool();
        const QColor dotColor = index.data(DotColorRole).value<QColor>();
        const bool expanded = m_tree && m_tree->isExpanded(index);
        const bool sel = (option.state & QStyle::State_Selected) != 0;
        const bool hover = (option.state & QStyle::State_MouseOver) != 0;

        QRect r = option.rect.adjusted(6, 4, -6, -4);

        if (isGroup) {
            QColor bg(245, 245, 247);
            if (hover) bg = QColor(238, 240, 245);
            if (sel) bg = QColor(232, 235, 245);
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, 8, 8);

            const int dot = 8;
            QRect dotRect(r.left() + 12, r.center().y() - dot / 2, dot, dot);
            painter->setBrush(dotColor);
            painter->drawEllipse(dotRect);

            QFont f = option.font;
            f.setBold(true);
            painter->setFont(f);
            painter->setPen(QColor(40, 40, 40));
            const bool hasChildren = index.model() && index.model()->rowCount(index) > 0;
            const int textRight = hasChildren ? r.right() - 28 : r.right() - 8;
            QRect textRect(r.left() + 12 + dot + 8, r.top(), textRight - (r.left() + 12 + dot + 8), r.height());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

            if (hasChildren) {
                painter->setPen(QColor(120, 120, 120));
                painter->drawText(QRect(r.right() - 24, r.top(), 20, r.height()), Qt::AlignCenter,
                    expanded ? QStringLiteral("▾") : QStringLiteral("▸"));
            }
        } else {
            QColor bg(255, 255, 255);
            if (hover) bg = QColor(248, 249, 252);
            if (sel) bg = QColor(245, 247, 255);
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, 10, 10);

            QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            QSize iconSize(24, 24);
            QRect iconRect(r.left() + 12, r.center().y() - iconSize.height() / 2, iconSize.width(), iconSize.height());
            if (!icon.isNull())
                icon.paint(painter, iconRect, Qt::AlignCenter);

            painter->setPen(QColor(40, 40, 40));
            QRect textRect = r.adjusted(12 + iconSize.width() + 10, 0, -8, 0);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);
        }

        painter->restore();
    }
private:
    QTreeView* m_tree = nullptr;
};

QFrame* makeCard(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("card");
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

QToolButton* makeTopIconButton(QWidget* parent, const QIcon& icon, const QString& toolTip) {
    auto* button = new QToolButton(parent);
    button->setIcon(icon);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(16, 16));
    return button;
}
}

MainWindow::MainWindow(const QString& username, QWidget* parent)
    : QMainWindow(parent)
    , m_username(username)
{
    setWindowTitle(QString("AI客服 - %1").arg(username));
    setMinimumSize(1024, 640);
    resize(1320, 760);

    auto* root = new QWidget(this);
    root->setObjectName("root");
    setCentralWidget(root);
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(buildLeftSidebar());

    auto* right = new QWidget(root);
    right->setObjectName("rightArea");
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(buildTopBar());
    rightLayout->addWidget(buildCenterContent(), 1);
    rootLayout->addWidget(right, 1);

    setupStyles();
    showSystemReadyPage();
}

MainWindow::~MainWindow()
{
    // delete ui;
}

QWidget *MainWindow::buildLeftSidebar()
{
    auto* left = new QWidget(this);
    left->setObjectName("leftSiderbar");
    left->setMinimumWidth(200);

    auto* layout = new QVBoxLayout(left);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(6);
    layout->setAlignment(Qt::AlignTop);

    m_platformTreeModel = new QStandardItemModel(this);
    auto* online = new QStandardItem(QStringLiteral("在线平台"));
    online->setData(QStringLiteral("online"), PlatformIdRole);
    online->setData(true, IsGroupRole);
    online->setData(QColor(0, 200, 120), DotColorRole);
    online->setFlags(online->flags() & ~Qt::ItemIsDropEnabled);

    const QIcon iconManage = qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView);
    auto* manageConsole = new QStandardItem(iconManage, QStringLiteral("管理后台"));
    manageConsole->setData(QStringLiteral("manage"), PlatformIdRole);
    manageConsole->setData(false, IsGroupRole);
    manageConsole->setFlags(manageConsole->flags() & ~Qt::ItemIsDropEnabled);

    const QIcon iconRobot = qApp->style()->standardIcon(QStyle::SP_ComputerIcon);
    auto* itemRobot = new QStandardItem(iconRobot, QStringLiteral("机器人管理"));
    itemRobot->setData(QStringLiteral("robot"), PlatformIdRole);
    itemRobot->setData(false, IsGroupRole);
    itemRobot->setFlags(itemRobot->flags() & ~Qt::ItemIsDropEnabled);
    manageConsole->appendRow(itemRobot);

    const QIcon iconReception = qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation);
    auto* itemReception = new QStandardItem(iconReception, QStringLiteral("聚合接待"));
    itemReception->setData(QStringLiteral("aggregate"), PlatformIdRole);
    itemReception->setData(false, IsGroupRole);
    itemReception->setFlags(itemReception->flags() & ~Qt::ItemIsDropEnabled);
    manageConsole->appendRow(itemReception);

    auto* offline = new QStandardItem(QStringLiteral("离线平台"));
    offline->setData(QStringLiteral("offline"), PlatformIdRole);
    offline->setData(true, IsGroupRole);
    offline->setData(QColor(160, 160, 160), DotColorRole);
    offline->setFlags(offline->flags() & ~Qt::ItemIsDropEnabled);

    const QIcon iconPlatform = qApp->style()->standardIcon(QStyle::SP_DialogApplyButton);
    const char* offlineItems[] = {"千牛", "拼多多", "抖店"};
    const char* offlineIds[] = {"qianniu", "pinduoduo", "douyin"};
    for (int i = 0; i < 3; i++) {
        auto* item = new QStandardItem(iconPlatform, QString::fromUtf8(offlineItems[i]));
        item->setData(QString::fromUtf8(offlineIds[i]), PlatformIdRole);
        item->setData(false, IsGroupRole);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        offline->appendRow(item);
    }
    m_platformTreeModel->appendRow(online);
    m_platformTreeModel->appendRow(manageConsole);
    m_platformTreeModel->appendRow(offline);

    m_platformTree = new QTreeView(left);
    m_platformTree->setObjectName("platformlist");
    m_platformTree->setModel(m_platformTreeModel);
    m_platformTree->setItemDelegate(new PlatformTreeDelegate(m_platformTree, m_platformTree));
    m_platformTree->setHeaderHidden(true);
    m_platformTree->setIndentation(16);
    m_platformTree->setRootIsDecorated(false);
    m_platformTree->setExpandsOnDoubleClick(true);
    m_platformTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_platformTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_platformTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_platformTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_platformTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_platformTree->setUniformRowHeights(false);
    m_platformTree->setMouseTracking(true);
    m_platformTree->expandAll();
    layout->addWidget(m_platformTree);

    updateTreeViewHeight();
    connect(m_platformTree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onPlatformTreeSelectionChanged);
    connect(m_platformTree, &QTreeView::clicked, this, &MainWindow::onPlatformTreeClicked);
    connect(m_platformTree, &QTreeView::expanded, this, [this](const QModelIndex&) { updateTreeViewHeight(); });
    connect(m_platformTree, &QTreeView::collapsed, this, [this](const QModelIndex&) { updateTreeViewHeight(); });

    return left;
}

QWidget *MainWindow::buildTopBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName("topBar");
    bar->setFixedHeight(50);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(10);

    auto* logo = new QLabel(bar);
    logo->setObjectName("logo");
    logo->setFixedSize(22, 22);
    logo->setPixmap(qApp->style()->standardIcon(QStyle::SP_DesktopIcon).pixmap(22, 22));

    auto* title = new QLabel(QStringLiteral("AI客服 - %1").arg(m_username), bar);
    title->setObjectName("topTitle");

    layout->addWidget(logo);
    layout->addWidget(title);
    layout->addSpacing(6);

    m_btnAdd = makeTopIconButton(bar, qApp->style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("添加新窗口"));
    m_btnRefresh = makeTopIconButton(bar, qApp->style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新平台列表"));
    m_btnQuickStart = makeTopIconButton(bar, qApp->style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("快速启动应用"));
    layout->addWidget(m_btnAdd);
    layout->addWidget(m_btnRefresh);

    auto* readyWrap = new QWidget(bar);
    readyWrap->setObjectName("readyWrap");
    auto* readyLayout = new QHBoxLayout(readyWrap);
    readyLayout->setContentsMargins(10, 6, 10, 6);
    readyLayout->setSpacing(6);
    auto* readyIcon = new QLabel(readyWrap);
    readyIcon->setPixmap(qApp->style()->standardIcon(QStyle::SP_ArrowUp).pixmap(16, 16));
    auto* readyText = new QLabel(QStringLiteral("系统就绪"), readyWrap);
    readyText->setObjectName("readyText");
    readyLayout->addWidget(readyIcon);
    readyLayout->addWidget(readyText);
    layout->addWidget(readyWrap);
    layout->addStretch(1);
    layout->addWidget(m_btnQuickStart);

    connect(m_btnAdd, &QToolButton::clicked, this, &MainWindow::openAddWindowDialog);
    connect(m_btnRefresh, &QToolButton::clicked, this, &MainWindow::showSystemReadyPage);
    connect(m_btnQuickStart, &QToolButton::clicked, this, &MainWindow::showSystemReadyPage);
    return bar;
}

QWidget *MainWindow::buildCenterContent()
{
    m_centerStack = new QStackedWidget(this);
    m_centerStack->setObjectName("centerStack");
    m_centerStack->addWidget(buildReadyPage());

    m_placeholderPage = new QWidget(this);
    m_placeholderPage->setObjectName("placeholderPage");
    auto* phLayout = new QVBoxLayout(m_placeholderPage);
    phLayout->setContentsMargins(0, 0, 0, 0);
    phLayout->setAlignment(Qt::AlignCenter);

    m_placeholderLabel = new QLabel(m_placeholderPage);
    m_placeholderLabel->setText("placeholderText");
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    phLayout->addWidget(m_placeholderLabel);
    m_centerStack->addWidget(m_placeholderPage);

    m_embedPage = new QWidget(this);
    m_embedPage->setObjectName("embedPage");
    auto* embedLayout = new QVBoxLayout(m_embedPage);
    embedLayout->setContentsMargins(12, 12, 12, 12);
    embedLayout->setSpacing(0);
    m_embedContainer = new EmbeddedWindowContainer(m_embedPage);
    m_embedContainer->setObjectName("embedContainer");
    embedLayout->addWidget(m_embedContainer);
    m_centerStack->addWidget(m_embedPage);
    return m_centerStack;
}

void MainWindow::updateTreeViewHeight()
{
    if (!m_platformTree || !m_platformTreeModel) return;
    int totalHeight = 0;
    int itemCount = m_platformTreeModel->rowCount();
    for (int i = 0; i < itemCount; ++i) {
        QModelIndex index = m_platformTreeModel->index(i, 0);
        bool isGroup = index.data(IsGroupRole).toBool();
        totalHeight += isGroup ? 48 : 56;
        if (isGroup && m_platformTree->isExpanded(index)) {
            int childCount = m_platformTreeModel->rowCount(index);
            totalHeight += childCount * 56;
        }
    }
    m_platformTree->setMinimumHeight(totalHeight + 20);
}

void MainWindow::onPlatformTreeSelectionChanged()
{
    QModelIndex idx = m_platformTree->currentIndex();
    if (!idx.isValid()) {
        showSystemReadyPage();
        return;
    }
    QString id = idx.data(PlatformIdRole).toString();
    if (id == QLatin1String("online") || id == QLatin1String("offline")) {
        showSystemReadyPage();
        return;
    }
    if (id == QLatin1String("aggregate")) {
        openAggregateChatForm();
        return;
    }
    if (id == QLatin1String("manage") || id == QLatin1String("robot")) {
        showPlaceholderPage(idx.data(Qt::DisplayRole).toString());
        return;
    }
    showPlaceholderPage(idx.data(Qt::DisplayRole).toString());
}

void MainWindow::onPlatformTreeClicked(const QModelIndex &idx)
{
    if (!idx.isValid()) {
        return;
    }
    bool isGroup = idx.data(IsGroupRole).toBool();
    QString id = idx.data(PlatformIdRole).toString();
    if (isGroup && id == QLatin1String("offline")) {
        m_platformTree->setExpanded(idx, !m_platformTree->isExpanded(idx));
    }
}

void MainWindow::showSystemReadyPage()
{
    m_centerStack->setCurrentIndex(0);
}

void MainWindow::showPlaceholderPage(const QString &title)
{
    m_placeholderLabel->setText(QStringLiteral("%1功能开发中...").arg(title));
    m_centerStack->setCurrentWidget(m_placeholderPage);
}

void MainWindow::setupStyles()
{
    setStyleSheet(ApplyStyle::mainWindowStyle());
}

QWidget *MainWindow::buildReadyPage()
{
    auto* center = new QWidget(this);
    center->setObjectName("centerArea");

    auto* layout = new QVBoxLayout(center);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);

    m_readyCard = makeCard(center);
    m_readyCard->setObjectName("readyCard");
    m_readyCard->setFixedWidth(560);

    auto* cardLayout = new QVBoxLayout(m_readyCard);
    cardLayout->setContentsMargins(28, 26, 28, 26);
    cardLayout->setSpacing(10);
    cardLayout->setAlignment(Qt::AlignHCenter);

    auto* rocketWrap = new QFrame(m_readyCard);
    rocketWrap->setObjectName("rocketWrap");
    rocketWrap->setFixedSize(360, 94);
    auto* rocketLayout = new QHBoxLayout(rocketWrap);
    rocketLayout->setContentsMargins(16, 16, 16, 16);
    rocketLayout->addStretch(1);
    auto* rocket = new QLabel(rocketWrap);
    rocket->setPixmap(qApp->style()->standardIcon(QStyle::SP_ArrowUp).pixmap(44, 44));
    rocketLayout->addWidget(rocket);
    rocketLayout->addStretch(1);

    m_readyTitle = new QLabel(QStringLiteral("系统就绪"), m_readyCard);
    m_readyTitle->setObjectName("readyTitle");
    m_readyTitle->setAlignment(Qt::AlignHCenter);

    auto* divider = new QFrame(m_readyCard);
    divider->setObjectName("divider");
    divider->setFixedHeight(1);
    divider->setFixedWidth(220);

    m_readySubtitle = new QLabel(QStringLiteral("选择左侧平台管理窗口"), m_readyCard);
    m_readySubtitle->setObjectName("readySubtitle");
    m_readySubtitle->setAlignment(Qt::AlignHCenter);

    cardLayout->addWidget(rocketWrap);
    cardLayout->addWidget(m_readyTitle);
    cardLayout->addWidget(divider);
    cardLayout->addWidget(m_readySubtitle);

    auto* quickRow = new QWidget(center);
    auto* quickLayout = new QHBoxLayout(quickRow);
    quickLayout->setContentsMargins(0, 18, 0, 0);
    quickLayout->setSpacing(18);
    quickLayout->setAlignment(Qt::AlignHCenter);

    auto makeQuick = [&](const QIcon& icon, const QString& text) -> QToolButton* {
        auto* btn = new QToolButton(quickRow);
        btn->setObjectName("quickCard");
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIcon(icon);
        btn->setIconSize(QSize(26, 26));
        btn->setText(text);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setAutoRaise(false);
        btn->setFixedSize(150, 120);
        return btn;
    };
    auto* btnPick = makeQuick(qApp->style()->standardIcon(QStyle::SP_ArrowRight), QStringLiteral("点击选择平台"));
    auto* btnEmbed = makeQuick(qApp->style()->standardIcon(QStyle::SP_FileDialogListView), QStringLiteral("自动嵌入窗口"));
    auto* btnStart = makeQuick(qApp->style()->standardIcon(QStyle::SP_DialogOkButton), QStringLiteral("快速启动应用"));
    connect(btnPick, &QToolButton::clicked, this, [this]() { m_platformTree->setFocus(); });
    connect(btnEmbed, &QToolButton::clicked, this, [this]() { showPlaceholderPage(QStringLiteral("自动嵌入窗口")); });
    connect(btnStart, &QToolButton::clicked, this, [this]() { showSystemReadyPage(); });
    quickLayout->addWidget(btnPick);
    quickLayout->addWidget(btnEmbed);
    quickLayout->addWidget(btnStart);
    layout->addWidget(m_readyCard, 0, Qt::AlignHCenter);
    layout->addWidget(quickRow, 0, Qt::AlignHCenter);
    layout->addStretch(2);

    return center;
}

void MainWindow::openAddWindowDialog()
{
    AddWindowDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const WindowInfo info = dlg.selectedWindow();
    if (info.handle == 0) {
        return;
    }
    if (m_embeddedHandle) {
        Win32WindowHelper::detachWindow(m_embeddedHandle);
        m_embeddedHandle = 0;
    }
    m_embeddedHandle = info.handle;
    if (!m_embedContainer) {
        return;
    }
    m_centerStack->setCurrentWidget(m_embedPage);
    m_embedContainer->setEmbeddedHandle(m_embeddedHandle);
}

void MainWindow::openAggregateChatForm()
{
    if (!m_aggregateChatForm) {
        m_aggregateChatForm = new AggregateChatForm(nullptr);
        m_aggregateChatForm->setAttribute(Qt::WA_DeleteOnClose, true);
        connect(m_aggregateChatForm, &QObject::destroyed, this, [this]() { m_aggregateChatForm = nullptr; });
    }
    m_aggregateChatForm->show();
    m_aggregateChatForm->raise();
    m_aggregateChatForm->activateWindow();
}

EmbeddedWindowContainer::EmbeddedWindowContainer(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
}

void EmbeddedWindowContainer::setEmbeddedHandle(quintptr handle)
{
    m_handle = handle;
    if (m_handle) {
        Win32WindowHelper::embedWindowIntoWidget(m_handle, this);
    }
}

quintptr EmbeddedWindowContainer::embeddedHandle() const
{
    return m_handle;
}

void EmbeddedWindowContainer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_handle) {
        Win32WindowHelper::resizeEmbeddedWindow(m_handle, this);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_embeddedHandle) {
        Win32WindowHelper::detachWindow(m_embeddedHandle);
        m_embeddedHandle = 0;
    }
    QMainWindow::closeEvent(event);
}
